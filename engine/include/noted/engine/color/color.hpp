#pragma once

// Color management.
//
//   - ICC profile parsing and CMM via lcms2.
//   - OpenColorIO 2 wired in for film-grade pipelines.
//   - Working space is linear scene-referred float16 unless overridden.
//
// Real implementation lands in feat/color-ocio.

namespace noted::color {

class Profile;

}  // namespace noted::color
