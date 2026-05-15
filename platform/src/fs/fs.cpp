#include "noted/platform/fs/fs.hpp"

#include <cstring>
#include <fstream>
#include <sstream>

namespace noted::platform::fs {

namespace {
constexpr std::uint32_t kSpirvMagic = 0x07230203U;
}  // namespace


auto read_all(const std::filesystem::path& p) -> noted::Result<std::string> {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        return std::unexpected(noted::Error{
            .code    = noted::ErrorCode::file_not_found,
            .message = p.string(),
            .where   = std::source_location::current(),
        });
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

auto write_all(const std::filesystem::path& p, std::string_view content) -> noted::Result<void> {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected(noted::Error{
            .code    = noted::ErrorCode::permission_denied,
            .message = p.string(),
            .where   = std::source_location::current(),
        });
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return {};
}

auto read_spirv(const std::filesystem::path& p) -> noted::Result<std::vector<std::uint32_t>> {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) {
        return std::unexpected(noted::Error{
            .code    = noted::ErrorCode::file_not_found,
            .message = p.string(),
            .where   = std::source_location::current(),
        });
    }
    const auto bytes = static_cast<std::size_t>(in.tellg());
    if (bytes == 0 || (bytes % 4U) != 0U) {
        return std::unexpected(noted::Error{
            .code    = noted::ErrorCode::invalid_image_format,
            .message = p.string() + ": not a valid SPIR-V (byte size not multiple of 4)",
            .where   = std::source_location::current(),
        });
    }
    std::vector<std::uint32_t> words(bytes / 4U);
    in.seekg(0);
    in.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(bytes));
    if (words.empty() || words.front() != kSpirvMagic) {
        return std::unexpected(noted::Error{
            .code    = noted::ErrorCode::invalid_image_format,
            .message = p.string() + ": SPIR-V magic mismatch",
            .where   = std::source_location::current(),
        });
    }
    return words;
}

}  // namespace noted::platform::fs
