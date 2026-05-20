# ADR 0032 — App layer responsibility decomposition (R.1–R.4)

**Status:** Accepted (design). Implementation lands across PRs R.1–R.4.
**Date:** 2026-05-20

## Context

[ADR 0027](0027-ui-stack-selection.md) and [PR #54](https://github.com/yoo9857/noted/pull/54)
moved everything `main()` used to construct into a single `noted::app::App`
class. That refactor (1055 → 53 lines for `main.cpp`) was the right move
for v0.x — it gave the engine + GPU stack + scene + UI session one
non-movable owner with a clean two-phase lifecycle. Heap-allocated
`unique_ptr<App>` made `this`-capturing hook subscriptions safe.

Phases A.1 → B.4 then accreted onto `App`:

| Phase | Surface area added to App |
|---|---|
| A.1   | `Camera` member, scroll-zoom + middle-pan pointer subscriptions, cursor tracker |
| A.2   | (none — StrokeEngine subsumed the vector-ink work internally) |
| A.3.b | `PageRenderer` init + member |
| A.3.c | `page_strip` widget call in `draw_widgets` |
| A.3.d | `Document`-owned `PageList` access (no new App member, but `draw_widgets` grows page-mutation branches) |
| B.1   | `tools_` member + `tool_palette` widget + brush swap on switch |
| B.2   | `strokes_target_` + `strokes_set_` + `composite_overlay_pipeline_` + a fourth render-pass record method |
| B.3   | `brush_options` widget + per-frame brush push into the stroke engine |
| B.4   | `selection_` + `selection_drag_` + 3 selection-drag pointer subscribers + `selection_overlay` widget call |

**Result at main `8c87ee0`:**

- `app/src/app.cpp` is **1402 lines**.
- `app/src/app.hpp` is **283 lines**.
- `App::install_frame_hook` is **~150 lines** of pointer / scroll / framebuffer
  subscriptions, and **every new tool grows it**.
- `App::draw_widgets` is **~150 lines** of widget calls + per-frame state
  synchronization, and **every new panel grows it**.
- `App` has **23 member fields** across 9 distinct subsystems.

The roadmap ahead (`B.5 shape` / `B.6 text` / `B.7 image` / Phase C / Phase D)
adds another **10–15 tools and a dozen panels**. At the current accretion
rate `App` heads toward 3000+ lines — past the point where any contributor
(or LLM) can hold the contract in their head AND test the seams in
isolation.

The user explicitly raised this at the end of B.4: *"app.cpp 등 코드가
길어지는 것들이 있어, … 기능들이 몇백개가 추가되면 더 어렵지 않을까?"*
The concern is correct AND the timing is right: between B.4 (last tool
implemented under the old pattern) and B.5 (first tool to land under
the new pattern) is the optimal seam to refactor.

## Decision

Split `App`'s current responsibilities into **four focused subsystems**,
one per follow-up PR. Each PR is **zero behaviour change** (refactor only);
the 322 unit tests that pass at `8c87ee0` continue to pass unchanged across
all four PRs. App becomes a **~300-line orchestrator** whose sole job is
to wire the subsystems together in the right order at startup.

### R.1 — `app::input::ToolInputRouter` + per-tool input handlers (next PR)

**Highest ROI.** Every new tool currently grows `App::install_frame_hook` by
3 lambda subscriptions; after R.1 it grows by 1 line of registration in
`App::create()`.

New interface:

```cpp
// app/src/input/tool_input_handler.hpp
class ToolInputHandler {
public:
    virtual ~ToolInputHandler() = default;
    [[nodiscard]] virtual auto handled_kind() const noexcept
        -> noted::domain::tool::ToolKind = 0;
    virtual void on_pressed(double canvas_x, double canvas_y,
                            bool shift, bool alt) = 0;
    virtual void on_moved(double canvas_x, double canvas_y) = 0;
    virtual void on_released(double canvas_x, double canvas_y) = 0;
    // Called when the active tool switches AWAY from this handler.
    // Must commit / discard any in-flight drag state cleanly.
    virtual void on_deactivated() noexcept = 0;
};
```

New router:

```cpp
// app/src/input/tool_input_router.hpp
class ToolInputRouter {
public:
    ToolInputRouter(noted::hook::Registry& reg,
                    const noted::canvas::Camera& cam);

    void register_handler(std::unique_ptr<ToolInputHandler> h);
    void set_active(noted::domain::tool::ToolKind kind);  // calls on_deactivated on previous

private:
    // Subscribes to PointerPressed/Moved/Released. Each callback
    // looks up the active handler (O(N) over a 6-element vector, no
    // hash table needed) and dispatches with canvas-space
    // coordinates already unprojected through the camera.
    std::vector<std::unique_ptr<ToolInputHandler>> handlers_;
    ToolInputHandler* active_{nullptr};
    // ... hook subscriptions ...
};
```

R.1 also extracts the B.4 selection drag into a `SelectionToolHandler`
and the B.1/B.2 stroke-tool gate into a thin `StrokeToolGate` handler
that wraps `StrokeEngine::set_active`. The pan / zoom / cursor-tracker
logic stays in App for R.1 (it's not tool-scoped — moves to
`CameraController` in R.2).

### R.2 — `app::input::CameraController`

Pan + zoom + scroll-anchor cursor tracking + the `framebuffer_resized`
sync all share state with `Camera`. Pulling them into a single class
makes the camera-input surface self-contained: any future "trackpad
gesture" / "two-finger pinch zoom" addition modifies one TU, not App.

```cpp
class CameraController {
public:
    CameraController(noted::canvas::Camera& cam,
                     noted::hook::Registry& reg,
                     const noted::app::config::CanvasConfig& cfg);
    // Owns: cursor_x_, cursor_y_, panning_, pan_last_*, on_scrolled
    //       handler, on_pointer_moved (pan delta), on_pointer_pressed
    //       (middle-button), on_pointer_released (middle-button),
    //       on_framebuffer_resized handler.
};
```

### R.3 — `app::frame::RenderPasses`

The 4-pass canvas pipeline (canvas / strokes / overlay / swapchain) plus
`render_one_frame` and `recreate_swapchain` are tightly coupled GPU
orchestration. Pulling them out:

```cpp
class RenderPasses {
public:
    // Holds references to all the GPU resources App owns; the App-as-
    // owner contract doesn't change, RenderPasses is a non-owning
    // *coordinator*.
    auto render_frame(...) -> Result<void>;
private:
    void record_canvas_pass(VkCommandBuffer, VkExtent2D);
    void record_strokes_pass(VkCommandBuffer, VkExtent2D);
    void record_overlay_pass(VkCommandBuffer, VkExtent2D);
    void record_swapchain_pass(VkCommandBuffer, VkExtent2D);
};
```

After R.3, adding a fifth pass (e.g. selection-mask compositor wired in)
modifies one class with a clear contract instead of grafting onto App.

### R.4 — `app::ui::UiPanels`

`draw_widgets` is currently a 150-line procedure calling 8 widgets +
synchronizing 6 per-frame state pushes. Extract:

```cpp
class UiPanels {
public:
    // Owns: MenuBarState, DebugOverlayState, OutlineRenameState,
    //       applied_theme, last_window_title.
    void draw(/* references to everything the panels read/mutate */);
};
```

App's `draw_widgets` becomes a single `panels_.draw(...)`. Adding a new
panel modifies `UiPanels`, not `App`.

## Alternatives considered

- **One big refactor PR.** Rejected. 1400-line touches across 4 subsystems
  in one PR is unreviewable AND the squash-merge would erase the seam
  history. Four sequential PRs let the user merge between each and stop
  the refactor at any point if the cost/benefit shifts.

- **Keep God-class App, write style discipline doc.** Rejected. Discipline
  doesn't survive 10 contributors / 10 LLM sessions. The architecture
  must make the right thing easy AND the wrong thing impossible (or at
  least visibly ugly). A class with 23 member fields stops being
  inspectable by reading the header.

- **Free-function-style tool dispatch** (no `ToolInputHandler` interface,
  just a `std::variant<SelectionDrag, ShapeDrag, ...>` in App). Rejected.
  Variant grows past comprehensibility around the 5th alternative; the
  switch statements at each event site explode combinatorially with the
  number of events × tools. A polymorphic interface scales linearly:
  one new tool = one new handler subclass, no central switch grows.

- **Use ECS / dependency-injection container.** Rejected for v0.x.
  Premature for a codebase with one binary, one main(), no need for
  runtime composition. The handler-registry pattern is the simplest
  thing that buys the decoupling; we can promote to a heavier framework
  later if a real need appears (none in the roadmap through Phase E).

- **Extract subsystems but keep them as App member fields, not separate
  classes.** Rejected — that's where `App` is today (camera_, tools_,
  selection_, ...) and the problem the user flagged is exactly that
  these scattered members each grow their integration code inside App's
  methods. Extracting *both* state AND its integration logic is the
  refactor that moves the needle.

## Consequences

- **App.cpp shrinks** from ~1400 LOC to ~300 LOC over the four PRs. Each
  extracted subsystem is its own TU, 100–250 LOC.
- **Every new tool from B.5 onward** ships as one `ToolInputHandler`
  subclass + one registration line in `App::create()`. No App pointer-
  event lambda grows. Predictable scaling to hundreds of tools.
- **Testability**: each subsystem can be exercised with mocks. R.1's
  `ToolInputRouter` is testable by registering a `MockToolHandler` and
  publishing synthetic pointer events through the hook registry.
- **No behaviour change** across R.1–R.4. The 322 unit tests at
  `8c87ee0` continue to pass at every intermediate state, and the
  interactive smoke run (5 s alive, stderr 0) remains the gating
  criterion. R.1–R.4 are **mechanical extractions**; functional changes
  (B.5 shape tool, etc.) land between or after the refactor PRs.
- **Future extractions** beyond R.4 — file I/O orchestration (`File →
  Open/Save` flow currently sits in `handle_menu_actions` and
  `save_for_dirty_prompt`), keyboard shortcut registry (currently a
  ladder of `IsKeyChordPressed` in `wire_keyboard_shortcuts`) — both
  follow the same pattern but defer to a later phase. Today's R.1–R.4
  cover the four largest concentrations of "App did too much".

## References

- [ADR 0027 — UI stack selection](0027-ui-stack-selection.md) — original
  decision to centralize on the `App` class.
- [ADR 0031 — Tool state machine](0031-tool-state-machine.md) — defines
  Phase B's per-tool framework; `ToolInputHandler` is its input-side
  complement.
- [PR #54 — `refactor/app-class-extract`](https://github.com/yoo9857/noted/pull/54)
  — the previous "move responsibilities into App" pass. R.1–R.4 are the
  follow-up "move them OUT of App now that it's grown too big".
