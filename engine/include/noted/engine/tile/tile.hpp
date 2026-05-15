#pragma once

// Tile-based image memory.
//
//   Tiles are 256x256 by default. A document image is a sparse 2D array of
//   tile handles; only resident tiles occupy VRAM. The Renderer asks the
//   tile manager for the visible set; the rest are streamed from
//   memory-mapped storage on demand.
//
// Real implementation lands in feat/tile-store.

#include <cstdint>

namespace noted::tile {

inline constexpr std::uint32_t tile_size = 256;

using TileHandle = std::uint64_t;

class TileStore;

}  // namespace noted::tile
