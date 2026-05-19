#include "noted/platform/fs/fs.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>

#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace noted::platform::fs {

namespace {
constexpr std::uint32_t kSpirvMagic = 0x07230203U;
}  // namespace

auto read_all(const std::filesystem::path& p) -> noted::Result<std::string> {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        return std::unexpected(noted::Error{
            .code = noted::ErrorCode::file_not_found,
            .message = p.string(),
            .where = std::source_location::current(),
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
            .code = noted::ErrorCode::permission_denied,
            .message = p.string(),
            .where = std::source_location::current(),
        });
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return {};
}

auto executable_dir() -> std::filesystem::path {
#if defined(_WIN32)
    // Win32: ask the loader for our HMODULE's full path. `MAX_PATH`
    // is too short for long-path-enabled systems; we grow the buffer
    // until the return doesn't equal the supplied size (the signal
    // that the path fit).
    std::vector<wchar_t> buf(1024U);
    for (;;) {
        const auto written =
            ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (written == 0U) {
            return {};
        }
        if (written < buf.size()) {
            return std::filesystem::path{buf.data()}.parent_path();
        }
        // Buffer was too small — GetModuleFileNameW returns the
        // supplied size in that case. Grow and retry.
        buf.resize(buf.size() * 2U);
    }
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);  // returns required size
    std::vector<char> buf(size);
    if (_NSGetExecutablePath(buf.data(), &size) != 0) {
        return {};
    }
    return std::filesystem::path{buf.data()}.parent_path();
#else
    // POSIX (Linux): /proc/self/exe is a symlink to the binary.
    std::error_code ec;
    auto resolved = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        return {};
    }
    return resolved.parent_path();
#endif
}

auto read_spirv(const std::filesystem::path& p) -> noted::Result<std::vector<std::uint32_t>> {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) {
        return std::unexpected(noted::Error{
            .code = noted::ErrorCode::file_not_found,
            .message = p.string(),
            .where = std::source_location::current(),
        });
    }
    const auto bytes = static_cast<std::size_t>(in.tellg());
    if (bytes == 0 || (bytes % 4U) != 0U) {
        return std::unexpected(noted::Error{
            .code = noted::ErrorCode::invalid_image_format,
            .message = p.string() + ": not a valid SPIR-V (byte size not multiple of 4)",
            .where = std::source_location::current(),
        });
    }
    std::vector<std::uint32_t> words(bytes / 4U);
    in.seekg(0);
    in.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(bytes));
    if (words.empty() || words.front() != kSpirvMagic) {
        return std::unexpected(noted::Error{
            .code = noted::ErrorCode::invalid_image_format,
            .message = p.string() + ": SPIR-V magic mismatch",
            .where = std::source_location::current(),
        });
    }
    return words;
}

}  // namespace noted::platform::fs
