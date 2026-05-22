#include "noted/domain/tool/brush_library.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace noted::domain::tool {

auto label_of(BrushKind kind) noexcept -> const char* {
    switch (kind) {
        case BrushKind::pen:
            return "Pen";
        case BrushKind::pencil:
            return "Pencil";
        case BrushKind::marker:
            return "Marker";
        case BrushKind::soft_brush:
            return "Soft Brush";
        case BrushKind::hard_brush:
            return "Hard Brush";
        case BrushKind::airbrush:
            return "Airbrush";
        case BrushKind::calligraphy:
            return "Calligraphy";
        case BrushKind::texture:
            return "Texture";
    }
    return "?";
}

auto BrushLibrary::with_builtins() -> BrushLibrary {
    BrushLibrary lib;

    // The seven built-in presets cover the major brush families a
    // working illustrator reaches for. Sizing / pressure / softness
    // numbers tuned by hand for the v1 ribbon tessellator — they
    // give visibly distinct feel even before the SDF-edge brush
    // (Phase C+) lands.

    {
        BrushPreset p{};
        p.name = "Ink Pen";
        p.kind = BrushKind::pen;
        p.min_radius_px = 1.5F;
        p.max_radius_px = 6.0F;
        p.alpha_gamma = 1.4F;
        p.stabilizer = 0.45F;
        p.softness = 0.05F;
        p.a = 1.0F;
        p.use_preset_color = false;  // ink takes whatever ink colour is selected
        (void) lib.add(p);
    }
    {
        BrushPreset p{};
        p.name = "Soft Pencil";
        p.kind = BrushKind::pencil;
        p.min_radius_px = 1.0F;
        p.max_radius_px = 4.5F;
        p.alpha_gamma = 2.4F;
        p.stabilizer = 0.55F;
        p.softness = 0.45F;
        p.r = 0.20F;
        p.g = 0.20F;
        p.b = 0.22F;
        p.a = 0.80F;
        p.use_preset_color = true;
        (void) lib.add(p);
    }
    {
        BrushPreset p{};
        p.name = "Marker";
        p.kind = BrushKind::marker;
        p.min_radius_px = 6.0F;
        p.max_radius_px = 14.0F;
        p.alpha_gamma = 1.0F;
        p.stabilizer = 0.35F;
        p.softness = 0.10F;
        p.a = 0.55F;
        p.use_preset_color = false;
        (void) lib.add(p);
    }
    {
        BrushPreset p{};
        p.name = "Soft Brush";
        p.kind = BrushKind::soft_brush;
        p.min_radius_px = 8.0F;
        p.max_radius_px = 32.0F;
        p.alpha_gamma = 1.8F;
        p.stabilizer = 0.65F;
        p.softness = 0.85F;
        p.spacing = 0.05F;
        p.a = 0.65F;
        p.use_preset_color = false;
        (void) lib.add(p);
    }
    {
        BrushPreset p{};
        p.name = "Hard Brush";
        p.kind = BrushKind::hard_brush;
        p.min_radius_px = 3.0F;
        p.max_radius_px = 18.0F;
        p.alpha_gamma = 1.5F;
        p.stabilizer = 0.40F;
        p.softness = 0.10F;
        p.a = 1.0F;
        p.use_preset_color = false;
        (void) lib.add(p);
    }
    {
        BrushPreset p{};
        p.name = "Airbrush";
        p.kind = BrushKind::airbrush;
        p.min_radius_px = 12.0F;
        p.max_radius_px = 48.0F;
        p.alpha_gamma = 2.6F;
        p.stabilizer = 0.50F;
        p.softness = 0.95F;
        p.spacing = 0.03F;
        p.a = 0.30F;
        p.use_preset_color = false;
        (void) lib.add(p);
    }
    {
        BrushPreset p{};
        p.name = "Calligraphy";
        p.kind = BrushKind::calligraphy;
        p.min_radius_px = 2.0F;
        p.max_radius_px = 12.0F;
        p.alpha_gamma = 1.2F;
        p.stabilizer = 0.40F;
        p.softness = 0.15F;
        p.angle_deg = 45.0F;
        p.angle_jitter = 0.0F;
        p.a = 1.0F;
        p.use_preset_color = false;
        (void) lib.add(p);
    }

    lib.mark_builtins_through(lib.size());
    return lib;
}

auto BrushLibrary::add(BrushPreset preset) -> Result<BrushPresetId> {
    preset.id = next_id_++;
    presets_.push_back(std::move(preset));
    return presets_.back().id;
}

auto BrushLibrary::insert(BrushPreset preset) -> Result<void> {
    if (preset.id == invalid_brush_preset_id) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "BrushLibrary::insert: preset.id must be non-zero"));
    }
    for (const auto& existing : presets_) {
        if (existing.id == preset.id) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_argument,
                "BrushLibrary::insert: id " + std::to_string(preset.id) + " already present"));
        }
    }
    if (preset.id >= next_id_) {
        next_id_ = preset.id + 1;
    }
    presets_.push_back(std::move(preset));
    return {};
}

auto BrushLibrary::remove(BrushPresetId id) -> Result<BrushPreset> {
    for (std::size_t i = 0; i < presets_.size(); ++i) {
        if (presets_[i].id == id) {
            auto removed = std::move(presets_[i]);
            presets_.erase(presets_.begin() + static_cast<std::ptrdiff_t>(i));
            // Shift the built-in marker down if a built-in was
            // removed (rare — the UI doesn't expose deletion of
            // factory presets, but we still keep the invariant).
            if (i < builtin_count_) {
                --builtin_count_;
            }
            return removed;
        }
    }
    return std::unexpected(
        noted::make_error(noted::ErrorCode::invalid_argument,
                          "BrushLibrary::remove: id " + std::to_string(id) + " not found"));
}

auto BrushLibrary::update(BrushPresetId id, BrushPreset updated) -> Result<void> {
    for (auto& existing : presets_) {
        if (existing.id == id) {
            updated.id = id;  // id stays authoritative
            existing = std::move(updated);
            return {};
        }
    }
    return std::unexpected(
        noted::make_error(noted::ErrorCode::invalid_argument,
                          "BrushLibrary::update: id " + std::to_string(id) + " not found"));
}

auto BrushLibrary::find(BrushPresetId id) const noexcept -> const BrushPreset* {
    if (id == invalid_brush_preset_id) {
        return nullptr;
    }
    for (const auto& p : presets_) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

auto BrushLibrary::find_by_name(std::string_view name) const noexcept -> const BrushPreset* {
    for (const auto& p : presets_) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

auto BrushLibrary::is_builtin(BrushPresetId id) const noexcept -> bool {
    for (std::size_t i = 0; i < builtin_count_ && i < presets_.size(); ++i) {
        if (presets_[i].id == id) {
            return true;
        }
    }
    return false;
}

void BrushLibrary::replace(std::vector<BrushPreset> presets) noexcept {
    presets_ = std::move(presets);
    BrushPresetId max_id = 0;
    for (const auto& p : presets_) {
        if (p.id > max_id) {
            max_id = p.id;
        }
    }
    next_id_ = max_id + 1;
    builtin_count_ = 0;
}

void BrushLibrary::mark_builtins_through(std::size_t count) noexcept {
    builtin_count_ = std::min(count, presets_.size());
}

}  // namespace noted::domain::tool
