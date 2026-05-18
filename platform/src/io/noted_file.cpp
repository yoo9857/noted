#include "noted/platform/io/noted_file.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <ios>
#include <string>
#include <utility>

#include <miniz.h>

#include "noted/domain/document/document.hpp"
#include "noted/domain/io/document_json.hpp"

namespace noted::platform::io {

namespace {

// RAII guard for mz_zip_archive — ensures the writer / reader end
// function runs even on early returns / exception unwinds. miniz's
// state struct is large; one instance per call site is fine.
struct ZipWriterGuard {
    mz_zip_archive zip{};
    bool engaged{false};
    ~ZipWriterGuard() {
        if (engaged) {
            (void) mz_zip_writer_end(&zip);
        }
    }
};

struct ZipReaderGuard {
    mz_zip_archive zip{};
    bool engaged{false};
    ~ZipReaderGuard() {
        if (engaged) {
            (void) mz_zip_reader_end(&zip);
        }
    }
};

// Heap-allocated buffer returned by miniz that the caller must
// release via mz_free. Wrapping it in a unique_ptr-like guard keeps
// the cleanup path tidy.
struct HeapBuf {
    void* ptr{nullptr};
    std::size_t size{0};
    ~HeapBuf() {
        if (ptr != nullptr) {
            mz_free(ptr);
        }
    }
};

}  // namespace

auto document_to_archive_bytes(const noted::domain::Document& doc) -> std::vector<std::byte> {
    const std::string json = noted::domain::io::document_to_json(doc);

    ZipWriterGuard writer{};
    // Heap writer: miniz allocates a growing buffer; we take ownership
    // at finalize time and copy into our std::vector.
    if (mz_zip_writer_init_heap(&writer.zip, 0, json.size() + 1024) == 0) {
        // Allocation failure is the only way this returns 0; the
        // signature can't return Result<T> per the header contract
        // (it's marked noexcept by intent — see the header). Best we
        // can do here is return an empty buffer; callers that want
        // diagnostics should use save_noted_file which goes through
        // the filesystem layer with proper error reporting.
        return {};
    }
    writer.engaged = true;

    // MZ_DEFAULT_COMPRESSION is -1 in miniz's enum; the API takes
    // `mz_uint level_and_flags` and interprets ~0u as "default". Cast
    // explicitly so MSVC's signed→unsigned warning stays quiet.
    if (mz_zip_writer_add_mem(&writer.zip,
                              kDocumentEntryName,
                              json.data(),
                              json.size(),
                              static_cast<mz_uint>(MZ_DEFAULT_COMPRESSION)) == 0) {
        return {};
    }

    void* archive_ptr = nullptr;
    std::size_t archive_size = 0;
    if (mz_zip_writer_finalize_heap_archive(&writer.zip, &archive_ptr, &archive_size) == 0) {
        return {};
    }
    HeapBuf owned{archive_ptr, archive_size};

    std::vector<std::byte> out(archive_size);
    if (archive_size > 0) {
        std::memcpy(out.data(), owned.ptr, archive_size);
    }
    return out;
}

auto document_from_archive_bytes(std::span<const std::byte> bytes)
    -> Result<noted::domain::Document> {
    if (bytes.empty()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "document_from_archive_bytes: archive bytes are empty"));
    }

    ZipReaderGuard reader{};
    if (mz_zip_reader_init_mem(&reader.zip, bytes.data(), bytes.size(), 0) == 0) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "document_from_archive_bytes: not a valid zip archive (or truncated)"));
    }
    reader.engaged = true;

    const auto file_index = mz_zip_reader_locate_file(&reader.zip, kDocumentEntryName, nullptr, 0);
    if (file_index < 0) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state,
            std::string{"document_from_archive_bytes: archive does not contain '"} +
                kDocumentEntryName + "'"));
    }

    mz_zip_archive_file_stat stat{};
    if (mz_zip_reader_file_stat(&reader.zip, static_cast<mz_uint>(file_index), &stat) == 0) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state,
                              "document_from_archive_bytes: failed to stat document entry"));
    }

    // Cap extraction size to a sane upper bound. 256 MB is wildly
    // generous for a JSON block tree; anything larger is almost
    // certainly a corrupt header.
    constexpr std::uint64_t kMaxExtractBytes = 256ULL * 1024ULL * 1024ULL;
    if (stat.m_uncomp_size > kMaxExtractBytes) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state,
                              "document_from_archive_bytes: document entry uncompressed size " +
                                  std::to_string(stat.m_uncomp_size) + " exceeds " +
                                  std::to_string(kMaxExtractBytes) + " byte cap"));
    }

    std::size_t out_size = 0;
    void* heap =
        mz_zip_reader_extract_to_heap(&reader.zip, static_cast<mz_uint>(file_index), &out_size, 0);
    if (heap == nullptr) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state,
                              "document_from_archive_bytes: failed to extract document entry"));
    }
    HeapBuf owned{heap, out_size};

    const std::string_view json_text{static_cast<const char*>(owned.ptr), out_size};
    auto parsed = noted::domain::io::document_from_json(json_text);
    if (!parsed) {
        return std::unexpected(std::move(parsed).error());
    }
    return std::move(*parsed);
}

auto save_noted_file(const std::filesystem::path& path,
                     const noted::domain::Document& doc) -> Result<void> {
    // std::ofstream's path ctor uses the native wide API on Windows
    // (since C++17), so non-ASCII paths round-trip correctly without
    // a manual _wfopen detour.
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state,
            "save_noted_file: failed to open '" + path.string() + "' for writing"));
    }
    const auto bytes = document_to_archive_bytes(doc);
    if (bytes.empty()) {
        // Either an empty Document yielded a 0-byte archive (impossible
        // for a valid zip; a valid empty-document zip is > 0 bytes), or
        // miniz failed somewhere in document_to_archive_bytes.
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state,
            "save_noted_file: archive serialization produced 0 bytes (miniz failure?)"));
    }
    ofs.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!ofs.good()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state,
            "save_noted_file: write to '" + path.string() + "' failed mid-stream"));
    }
    return {};
}

auto load_noted_file(const std::filesystem::path& path) -> Result<noted::domain::Document> {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "load_noted_file: failed to open '" + path.string() + "' for reading"));
    }
    const auto end_pos = ifs.tellg();
    if (end_pos < 0) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state,
                              "load_noted_file: tellg() failed on '" + path.string() + "'"));
    }
    ifs.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(end_pos));
    if (end_pos > 0) {
        ifs.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!ifs.good() && !ifs.eof()) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_state,
                                  "load_noted_file: read from '" + path.string() + "' failed"));
        }
    }
    return document_from_archive_bytes(bytes);
}

}  // namespace noted::platform::io
