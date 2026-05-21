#include "noted/platform/io/file_picker.hpp"

#include <string>
#include <utility>

#include <nfd.h>

namespace noted::platform::io {

namespace {

// RAII guard around NFD_Init / NFD_Quit. The init is per-call rather
// than process-global because NFD's Windows backend pumps COM and
// installing it globally would conflict with anything else that
// wants to own COM apartment state for this thread. Per-call init
// is documented as supported and is the pattern the upstream
// samples use.
struct NfdSession {
    nfdresult_t init_result;
    NfdSession() noexcept : init_result(NFD_Init()) {}
    NfdSession(const NfdSession&) = delete;
    NfdSession(NfdSession&&) = delete;
    auto operator=(const NfdSession&) -> NfdSession& = delete;
    auto operator=(NfdSession&&) -> NfdSession& = delete;
    ~NfdSession() {
        if (init_result == NFD_OKAY) {
            NFD_Quit();
        }
    }
};

// Frees the UTF-8 path NFD returns. Wrapping the raw pointer in an
// RAII guard so the early-return paths can't leak.
struct NfdPathGuard {
    nfdu8char_t* path{nullptr};
    NfdPathGuard() noexcept = default;
    NfdPathGuard(const NfdPathGuard&) = delete;
    NfdPathGuard(NfdPathGuard&&) = delete;
    auto operator=(const NfdPathGuard&) -> NfdPathGuard& = delete;
    auto operator=(NfdPathGuard&&) -> NfdPathGuard& = delete;
    ~NfdPathGuard() {
        if (path != nullptr) {
            NFD_FreePathU8(path);
        }
    }
};

[[nodiscard]] auto nfd_init_error() -> noted::Error {
    const char* msg = NFD_GetError();
    return noted::make_error(
        noted::ErrorCode::invalid_state,
        std::string{"file_picker: NFD_Init failed: "} + (msg != nullptr ? msg : "(no detail)"));
}

[[nodiscard]] auto nfd_dialog_error(const char* op) -> noted::Error {
    const char* msg = NFD_GetError();
    return noted::make_error(
        noted::ErrorCode::invalid_state,
        std::string{"file_picker: "} + op + " failed: " + (msg != nullptr ? msg : "(no detail)"));
}

// .noted is the only filter we expose today. The friendly label
// appears in the dropdown; the extension is matched case-insensitively
// by NFD.
constexpr nfdu8filteritem_t kNotedFilter{
    .name = "Noted document",
    .spec = "noted",
};

// Raster image formats that `platform::image_io::load_rgba8` (stb_image)
// can decode. NFD treats the comma-separated `spec` as an OR of
// extensions; this single filter entry covers all four formats with one
// dropdown item.
constexpr nfdu8filteritem_t kImageFilter{
    .name = "Image",
    .spec = "png,jpg,jpeg,bmp,tga",
};

}  // namespace

auto pick_noted_open() -> Result<std::optional<std::filesystem::path>> {
    NfdSession session;
    if (session.init_result != NFD_OKAY) {
        return std::unexpected(nfd_init_error());
    }

    NfdPathGuard out;
    nfdopendialogu8args_t args{};
    args.filterList = &kNotedFilter;
    args.filterCount = 1;

    const nfdresult_t r = NFD_OpenDialogU8_With(&out.path, &args);
    if (r == NFD_OKAY) {
        return std::filesystem::path{
            reinterpret_cast<const char8_t*>(static_cast<const char*>(out.path))};
    }
    if (r == NFD_CANCEL) {
        return std::optional<std::filesystem::path>{};
    }
    return std::unexpected(nfd_dialog_error("NFD_OpenDialogU8_With"));
}

auto pick_image_open() -> Result<std::optional<std::filesystem::path>> {
    NfdSession session;
    if (session.init_result != NFD_OKAY) {
        return std::unexpected(nfd_init_error());
    }

    NfdPathGuard out;
    nfdopendialogu8args_t args{};
    args.filterList = &kImageFilter;
    args.filterCount = 1;

    const nfdresult_t r = NFD_OpenDialogU8_With(&out.path, &args);
    if (r == NFD_OKAY) {
        return std::filesystem::path{
            reinterpret_cast<const char8_t*>(static_cast<const char*>(out.path))};
    }
    if (r == NFD_CANCEL) {
        return std::optional<std::filesystem::path>{};
    }
    return std::unexpected(nfd_dialog_error("NFD_OpenDialogU8_With"));
}

auto pick_noted_save(std::string_view default_name)
    -> Result<std::optional<std::filesystem::path>> {
    NfdSession session;
    if (session.init_result != NFD_OKAY) {
        return std::unexpected(nfd_init_error());
    }

    // NFD requires a null-terminated C string for defaultName; the
    // string_view's data() is not guaranteed to be terminated, so
    // copy into a std::string. The lifetime extends past the dialog
    // call because args.defaultName aliases this buffer.
    const std::string default_name_str{default_name};

    NfdPathGuard out;
    nfdsavedialogu8args_t args{};
    args.filterList = &kNotedFilter;
    args.filterCount = 1;
    args.defaultName = default_name_str.empty() ? nullptr : default_name_str.c_str();

    const nfdresult_t r = NFD_SaveDialogU8_With(&out.path, &args);
    if (r == NFD_OKAY) {
        return std::filesystem::path{
            reinterpret_cast<const char8_t*>(static_cast<const char*>(out.path))};
    }
    if (r == NFD_CANCEL) {
        return std::optional<std::filesystem::path>{};
    }
    return std::unexpected(nfd_dialog_error("NFD_SaveDialogU8_With"));
}

}  // namespace noted::platform::io
