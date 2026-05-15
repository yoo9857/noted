#include "noted/platform/fs/fs.hpp"

#include <fstream>
#include <sstream>

namespace noted::platform::fs {

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

}  // namespace noted::platform::fs
