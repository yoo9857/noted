#include "ui/ui_panels.hpp"

#include <iostream>
#include <memory>
#include <utility>

#include <imgui.h>

#include "noted/compositor/layer_compositor.hpp"
#include "noted/domain/command/commands.hpp"
#include "noted/domain/tool/options.hpp"
#include "noted/engine/canvas/camera.hpp"
#include "noted/engine/gpu/swapchain.hpp"
#include "noted/engine/stroke/stroke_engine.hpp"
#include "noted/ui/widget/brush_options.hpp"
#include "noted/ui/widget/image_overlay.hpp"
#include "noted/ui/widget/layer_panel.hpp"
#include "noted/ui/widget/outline_panel.hpp"
#include "noted/ui/widget/page_strip.hpp"
#include "noted/ui/widget/selection_overlay.hpp"
#include "noted/ui/widget/shape_overlay.hpp"
#include "noted/ui/widget/status_bar.hpp"
#include "noted/ui/widget/text_overlay.hpp"

#include "input/selection_tool_handler.hpp"
#include "input/shape_tool_handler.hpp"
#include "input/text_tool_handler.hpp"
#include "input/tool_input_router.hpp"

namespace noted::app::ui {

namespace {

// Settings the stroke engine consumes for the active tool. Mirrors
// what App::draw_widgets did before R.4 — the per-frame push lets
// brush-options slider drags + colour-picker edits take effect on
// the very next stroke. `set_brush` only affects future presses;
// the in-flight stroke holds its snapshotted style.
struct ToolSettings {
    noted::stroke::BrushStyle brush;
    noted::stroke::DrawMode mode;
};

[[nodiscard]] auto is_stroke_tool(noted::domain::tool::ToolKind kind) noexcept -> bool {
    using noted::domain::tool::ToolKind;
    return kind == ToolKind::pen || kind == ToolKind::eraser;
}

[[nodiscard]] auto tool_settings_for_tool(const noted::domain::tool::ToolState& tools) noexcept
    -> ToolSettings {
    using noted::domain::tool::ToolKind;
    switch (tools.active) {
        case ToolKind::eraser:
            return {.brush = noted::domain::tool::brush_from_eraser(tools.eraser),
                    .mode = noted::stroke::DrawMode::erase};
        case ToolKind::pen:
            return {.brush = noted::domain::tool::brush_from_pen(tools.pen),
                    .mode = noted::stroke::DrawMode::draw};
        case ToolKind::select:
        case ToolKind::shape:
        case ToolKind::text:
        case ToolKind::image:
            // Not-yet-implemented tools fall through to Pen
            // behaviour. Reading from tools.pen so they share the
            // user's pen settings — feels less surprising than
            // reverting to defaults mid-session.
            return {.brush = noted::domain::tool::brush_from_pen(tools.pen),
                    .mode = noted::stroke::DrawMode::draw};
    }
    return {.brush = noted::domain::tool::brush_from_pen(tools.pen),
            .mode = noted::stroke::DrawMode::draw};
}

}  // namespace

UiPanels::UiPanels(Deps d) noexcept
    : engine_(d.engine),
      session_(d.session),
      scene_(d.scene),
      layer_compositor_(d.layer_compositor),
      stroke_engine_(d.stroke_engine),
      tool_input_router_(d.tool_input_router),
      selection_handler_(d.selection_handler),
      shape_handler_(d.shape_handler),
      text_handler_(d.text_handler),
      image_handler_(d.image_handler),
      camera_(d.camera),
      swapchain_(d.swapchain),
      cfg_(d.cfg),
      menu_state_(d.menu_state),
      debug_overlay_state_(d.debug_overlay_state),
      outline_rename_(d.outline_rename),
      tools_(d.tools),
      selection_(d.selection),
      shapes_(d.shapes),
      texts_(d.texts),
      images_(d.images),
      prompt_(d.prompt),
      save_for_dirty_prompt_(std::move(d.save_for_dirty_prompt)),
      execute_pending_dirty_action_(std::move(d.execute_pending_dirty_action)),
      on_pick_image_(std::move(d.on_pick_image)) {}

auto UiPanels::create(Deps deps) -> std::unique_ptr<UiPanels> {
    return std::unique_ptr<UiPanels>(new UiPanels{std::move(deps)});
}

void UiPanels::draw() {
    // The workspace dockspace is set up in `App::on_frame` BEFORE any
    // panel's Begin() — moving the call into App made it run before
    // App's own Colour / Navigator panels too. Don't repeat it here:
    // the second DockSpace call on the same id is a no-op but the
    // owning host window would shadow the original.

    prompt_.draw(
        [this]() -> bool { return save_for_dirty_prompt_(); },
        [this](noted::app::DirtyPrompt::PendingAction a) { execute_pending_dirty_action_(a); });

    // Canvas layer stack — strokes the user has drawn pin onto entries
    // here; the panel surfaces visibility + active selection + add /
    // remove. The demo `LayerGraph` in `scene_` stays the compositor's
    // backdrop, but it's no longer surfaced in the product UI — those
    // four bg/red/glow/warm rows are debug fixtures, not a user-
    // facing concept.
    //
    // Structural actions (add / remove) flow through `session_.execute`
    // so Ctrl+Z restores deleted layers. Visibility / opacity / rename
    // stay direct inside the widget — making them undoable would spam
    // the history (Photoshop draws the same line on its Layers panel).
    {
        const auto la =
            noted::ui::widget::layer_panel(session_.document(), &menu_state_.show_layer_panel);
        using K = noted::ui::widget::LayerPanelAction::Kind;
        switch (la.kind) {
            case K::none:
                break;
            case K::add: {
                auto cmd =
                    std::make_unique<noted::domain::AddCanvasLayerCommand>(std::move(la.add_name));
                if (auto r = session_.execute(std::move(cmd)); !r) {
                    std::cerr << r.error().format() << '\n';
                }
                break;
            }
            case K::remove: {
                auto cmd = std::make_unique<noted::domain::RemoveCanvasLayerCommand>(la.index);
                if (auto r = session_.execute(std::move(cmd)); !r) {
                    std::cerr << r.error().format() << '\n';
                }
                // UX assist after the command: if the removed layer
                // was active, promote the new top of stack so paint
                // continues immediately. Not part of the command
                // (undo path restores the exact prior active id).
                if (session_.document().active_layer() == noted::invalid_layer_id &&
                    !session_.document().canvas_layers().empty()) {
                    const auto top = session_.document().canvas_layers().layers().back().id;
                    (void) session_.document().set_active_layer(top);
                }
                break;
            }
            case K::duplicate: {
                auto cmd =
                    std::make_unique<noted::domain::DuplicateCanvasLayerCommand>(la.index);
                if (auto r = session_.execute(std::move(cmd)); !r) {
                    std::cerr << r.error().format() << '\n';
                }
                break;
            }
            case K::move_up:
            case K::move_down: {
                // Reorder is direct (not yet command-wrapped). Goes
                // through `move_canvas_layer` which is a stack-level
                // mutation — undoing it cleanly requires its own
                // command (lands in a follow-up alongside lock /
                // rename / opacity undo coalescing).
                const auto stack_size = session_.document().canvas_layers().size();
                if (stack_size >= 2 && la.index < stack_size) {
                    const std::size_t target = (la.kind == K::move_up)
                                                   ? std::min(la.index + 1U, stack_size - 1U)
                                                   : (la.index == 0U ? 0U : la.index - 1U);
                    if (auto r = session_.document().move_canvas_layer(la.index, target); !r) {
                        std::cerr << r.error().format() << '\n';
                    }
                }
                break;
            }
        }
    }

    // Tool switching is owned by the 12 o'clock floating toolbar
    // (`ui::widget::top_toolbar`) wired in App::on_frame after Phase 3
    // of ADR 0034. The left-rail `tool_palette` widget was retired in
    // the same PR — no legacy parallel UI surface.
    // Brush options panel — slider / colour-picker writes mutate
    // `tools_.pen` / `tools_.eraser` in place. The "Pick image…"
    // button on the Image-tool section is signalled back via the
    // result struct because dialog reach + Document mutation cannot
    // live in the ui layer.
    {
        const auto bo_result = noted::ui::widget::brush_options(
            tools_, session_.document().image_assets(), &menu_state_.show_brush_options);
        if (bo_result.pick_image_requested && on_pick_image_) {
            on_pick_image_();
        }
    }
    // Per-frame push of the active tool's brush + mode into the
    // stroke engine. Live-update: slider drags and colour-picker
    // edits take effect on the very next stroke. The in-flight
    // stroke (if any) is unaffected — its style is snapshotted at
    // press time.
    {
        const auto settings = tool_settings_for_tool(tools_);
        stroke_engine_.set_brush(settings.brush);
        stroke_engine_.set_mode(settings.mode);
    }
    // Gate stroke-engine input to only stroke tools. Switching away
    // from Pen/Eraser drops the engine's left-button subscription
    // path on the floor (any in-flight stroke is committed first).
    // Switching back re-enables it. set_active is idempotent so
    // calling every frame costs only a bool compare.
    //
    // Also gate on the active canvas layer being PAINTABLE — i.e.
    // both visible AND unlocked. Drawing onto a hidden layer would
    // commit a stroke that vanishes immediately; drawing onto a
    // locked layer matches Photoshop's "cannot paint on a locked
    // layer" guard. Refusing the input outright (rather than
    // silently dropping samples) means the cursor / tool palette
    // reflects the state — the user sees the tool is unavailable.
    const auto& cl = session_.document().canvas_layers();
    const auto active_id = session_.document().active_layer();
    const auto* active_layer_ptr = cl.find(active_id);
    const bool active_layer_paintable =
        (active_id == noted::invalid_layer_id) ||
        (active_layer_ptr != nullptr && active_layer_ptr->visible && !active_layer_ptr->locked);
    stroke_engine_.set_active(is_stroke_tool(tools_.active) && active_layer_paintable);
    // Tool input router: sync the active handler with the user's
    // current tool. The router's `set_active` is idempotent on a
    // no-op switch (matching kind), and triggers `on_deactivated`
    // on the previously-active handler on a real switch — which is
    // how the SelectionToolHandler drops its in-flight drag on a
    // mid-drag tool switch.
    tool_input_router_.set_active(tools_.active);

    auto selected = session_.selected_block();
    noted::ui::widget::outline_panel(
        session_.document(), selected, &outline_rename_, &menu_state_.show_outline_panel);
    session_.set_selected_block(selected);

    // Page strip — observes pages, surfaces user intent as a
    // PageStripResult. Sequenced as: apply structural mutations
    // first (add / remove), then resolve focus against the now-
    // current list. add_request + focus_request can co-occur if
    // the user double-clicked; mutate-then-focus keeps the index
    // semantics sane (focus_request always refers to the post-add
    // list because no remove competes in the same frame).
    const auto& pages = session_.document().pages();
    auto strip = noted::ui::widget::page_strip(pages, &menu_state_.show_page_strip);
    // Park the focused page just below the menu bar with a bit of
    // breathing room. Shared by both add-then-auto-focus and the
    // explicit row-click focus so the camera lands at the same y.
    constexpr double kFocusTargetScreenY = 80.0;
    if (strip.add_request) {
        // Match the demo seed's centring so newly-added pages line
        // up with the existing ones at the identity camera transform.
        const auto canvas = swapchain_.summary().extent;
        const float origin_x = std::max(
            20.0F,
            (static_cast<float>(canvas.width) - cfg_.canvas.default_page_extent_w_px) * 0.5F);
        auto cmd =
            std::make_unique<noted::domain::AddPageCommand>(cfg_.canvas.default_page_extent_w_px,
                                                            cfg_.canvas.default_page_extent_h_px,
                                                            cfg_.canvas.default_page_background,
                                                            origin_x);
        auto* cmd_ptr = cmd.get();
        if (auto r = session_.execute(std::move(cmd)); !r) {
            std::cerr << r.error().format() << '\n';
        } else {
            // Auto-focus the newly-added page. Without this the new
            // page lands below the visible camera region and the
            // click feels like a no-op.
            const double new_y =
                noted::ui::widget::camera_translation_y_for_page(session_.document().pages(),
                                                                 cmd_ptr->assigned_index(),
                                                                 camera_.translation_y(),
                                                                 camera_.scale(),
                                                                 kFocusTargetScreenY);
            camera_.set_translation(camera_.translation_x(), new_y);
        }
    }
    if (strip.remove_request) {
        auto cmd = std::make_unique<noted::domain::RemovePageCommand>(*strip.remove_request);
        if (auto r = session_.execute(std::move(cmd)); !r) {
            std::cerr << r.error().format() << '\n';
        }
    }
    if (strip.focus_request) {
        // The current camera translation_y is returned unchanged if
        // the index is now stale (e.g. the page got removed in the
        // same frame), so this is safe.
        const double new_y =
            noted::ui::widget::camera_translation_y_for_page(session_.document().pages(),
                                                             *strip.focus_request,
                                                             camera_.translation_y(),
                                                             camera_.scale(),
                                                             kFocusTargetScreenY);
        camera_.set_translation(camera_.translation_x(), new_y);
    }

    // Drain rename outputs into the command stream. Both flags are
    // mutually exclusive in practice (the widget never sets both in
    // the same frame); check Commit first so an Enter + Escape race
    // resolves to "save what was typed."
    if (outline_rename_.commit_requested) {
        outline_rename_.commit_requested = false;
        const auto target = outline_rename_.target;
        outline_rename_.target = noted::domain::invalid_block_id;
        std::string new_name{outline_rename_.buffer.data()};  // null-terminated by ImGui
        auto cmd = std::make_unique<noted::domain::SetNameCommand>(target, std::move(new_name));
        if (auto r = session_.execute(std::move(cmd)); !r) {
            std::cerr << r.error().format() << '\n';
        }
    }
    if (outline_rename_.cancel_requested) {
        outline_rename_.cancel_requested = false;
        outline_rename_.target = noted::domain::invalid_block_id;
    }

    // Selection overlay — draws committed selection rects + the
    // in-progress drag preview on top of the canvas via ImGui's
    // background draw list.
    {
        std::optional<noted::ui::widget::SelectionDragPreview> preview;
        if (selection_handler_ != nullptr) {
            if (auto d = selection_handler_->current_drag(); d.has_value()) {
                noted::ui::widget::SelectionDragPreview p{};
                p.press_canvas_x = d->press_x;
                p.press_canvas_y = d->press_y;
                p.current_canvas_x = d->current_x;
                p.current_canvas_y = d->current_y;
                preview = p;
            }
        }
        auto project = [this](double cx, double cy) -> std::pair<float, float> {
            return {static_cast<float>(camera_.project_x(cx)),
                    static_cast<float>(camera_.project_y(cy))};
        };
        noted::ui::widget::selection_overlay(
            selection_, preview, project, menu_state_.show_selection_overlay);
    }

    // Shape overlay — committed shapes (read-only inspector on App-
    // owned vector) + the in-flight drag preview from the handler.
    // Same canvas → screen projection as the selection overlay.
    {
        std::optional<noted::ui::widget::ShapeDragPreview> preview;
        if (shape_handler_ != nullptr) {
            if (auto d = shape_handler_->current_drag(); d.has_value()) {
                noted::ui::widget::ShapeDragPreview p{};
                p.press_canvas_x = d->press_x;
                p.press_canvas_y = d->press_y;
                p.current_canvas_x = d->current_x;
                p.current_canvas_y = d->current_y;
                p.options = d->options;
                preview = p;
            }
        }
        auto project = [this](double cx, double cy) -> std::pair<float, float> {
            return {static_cast<float>(camera_.project_x(cx)),
                    static_cast<float>(camera_.project_y(cy))};
        };
        noted::ui::widget::shape_overlay(shapes_, preview, project, menu_state_.show_shape_overlay);
    }

    // Text overlay — interactive. Renders committed primitives via
    // the background draw list AND hosts an in-flight InputText for
    // typing. We translate Enter / Esc into TextToolHandler commit /
    // cancel via lambdas, keeping the overlay pure-domain.
    if (text_handler_ != nullptr) {
        auto project = [this](double cx, double cy) -> std::pair<float, float> {
            return {static_cast<float>(camera_.project_x(cx)),
                    static_cast<float>(camera_.project_y(cy))};
        };
        noted::ui::widget::text_overlay(
            texts_,
            text_handler_->editing(),
            project,
            [h = text_handler_] { h->commit_editing(); },
            [h = text_handler_] { h->cancel_editing(); },
            menu_state_.show_text_overlay);
    }

    // Image overlay (B.7) — non-interactive placeholder draw on the
    // background draw list. Same canvas-to-screen projection as
    // shape_overlay / text_overlay.
    {
        auto project = [this](double cx, double cy) -> std::pair<float, float> {
            return {static_cast<float>(camera_.project_x(cx)),
                    static_cast<float>(camera_.project_y(cy))};
        };
        noted::ui::widget::image_overlay(images_, project, menu_state_.show_image_overlay);
    }

    noted::ui::widget::debug_overlay(
        noted::ui::widget::DebugOverlayInputs{
            .frame_index = engine_.frame_index(),
            .fallback_count = layer_compositor_.fallback_count(),
            .camera_scale = camera_.scale(),
            .camera_translation_x = camera_.translation_x(),
            .camera_translation_y = camera_.translation_y(),
        },
        debug_overlay_state_,
        &menu_state_.show_debug_overlay);

    if (menu_state_.show_demo_window) {
        ImGui::ShowDemoWindow(&menu_state_.show_demo_window);
    }
    if (menu_state_.show_about_window) {
        ImGui::SetNextWindowSize({340.0F, 0.0F}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(
                "About noted", &menu_state_.show_about_window, ImGuiWindowFlags_NoCollapse)) {
            ImGui::TextUnformatted("noted — note-taking + raster editor");
            ImGui::TextUnformatted("노트 + 래스터 이미지 에디터");
            ImGui::TextUnformatted("v0.x development build");
            ImGui::Spacing();
            ImGui::TextDisabled("Engine: C++23 + Vulkan 1.4 + Slang");
            ImGui::TextDisabled("UI: Dear ImGui (ADR 0027)");
        }
        ImGui::End();
    }
    noted::ui::widget::status_bar({
        .frame_index = engine_.frame_index(),
        .zoom_pct = static_cast<float>(camera_.scale() * 100.0),
    });
}

}  // namespace noted::app::ui
