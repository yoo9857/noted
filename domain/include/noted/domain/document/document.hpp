#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace noted::domain {

using BlockId = std::uint64_t;

enum class BlockType : std::uint8_t {
    text,
    heading,
    canvas,
    image,
    embed,
    code,
};

struct TextBlock {
    std::string utf8;
};
struct CanvasBlock {
    std::uint32_t width  = 0;
    std::uint32_t height = 0;
    std::vector<BlockId> layers;
};

struct Block {
    BlockId   id{};
    BlockType type{BlockType::text};
    std::variant<TextBlock, CanvasBlock> payload;
    std::vector<BlockId> children;
};

// Unified document: notes and canvases live in the same tree so undo/redo,
// search, and CRDT replication operate over one structure.
class Document;

}  // namespace noted::domain
