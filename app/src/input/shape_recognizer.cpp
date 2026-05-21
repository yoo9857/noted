#include "input/shape_recognizer.hpp"

#include "noted/domain/document/document.hpp"

namespace noted::app::input {

void ShapeRecognizer::on_stroke_added(std::size_t stroke_index, double now_seconds) noexcept {
    pending_stroke_index_ = stroke_index;
    last_event_seconds_ = now_seconds;
}

void ShapeRecognizer::on_pointer_active(double now_seconds) noexcept {
    pending_stroke_index_.reset();
    last_event_seconds_ = now_seconds;
}

auto ShapeRecognizer::tick(const noted::domain::Document& doc,
                           double now_seconds) -> std::optional<ShapeRecognitionAction> {
    if (!cfg_.enabled || !pending_stroke_index_.has_value()) {
        return std::nullopt;
    }
    if (now_seconds - last_event_seconds_ < cfg_.hold_seconds) {
        return std::nullopt;
    }
    const auto idx = *pending_stroke_index_;
    // Mark consumed up front — whether detection succeeds or fails,
    // we never retest the same stroke. Without this an unmatched
    // stroke would re-attempt detection every frame, burning CPU
    // and (worse) shifting the recognition to whatever stroke ends
    // up at the same index after an undo.
    pending_stroke_index_.reset();
    if (idx >= doc.strokes().size()) {
        // The user undid the stroke (or otherwise mutated the
        // document) between the press release and the hold
        // elapsing. Drop the recognition silently.
        return std::nullopt;
    }
    auto result = noted::domain::shape_detect::detect_shape(doc.strokes()[idx], cfg_.detect);
    if (!result) {
        return std::nullopt;
    }
    ShapeRecognitionAction action{};
    action.stroke_index = idx;
    action.shape = result->shape;
    return action;
}

}  // namespace noted::app::input
