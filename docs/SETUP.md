# Local development setup

Target host for this guide: Windows 10/11 x64. Linux and macOS notes inline.

## Required toolchain

| Tool         | Version    | Why                                  |
|--------------|------------|--------------------------------------|
| Visual Studio Build Tools 2022 | 17.10+ | MSVC C++23 compiler |
| CMake        | >= 3.28    | Build generator (project requires 3.28) |
| Ninja        | >= 1.11    | Fast build driver                    |
| **Vulkan SDK** | **>= 1.4.309** | Headers, loader, validation layers, **slangc** (Slang shader compiler) |
| **Qt 6**     | **>= 6.7** (6.8 LTS recommended) | v1.0 UI framework per [ADR 0034](architecture/0034-ui-framework-migration-qt6.md). Modules: `qtbase`, `qtdeclarative`, `qtshadertools`. |
| Git          | >= 2.40    | Already installed                    |
| Git LFS      | >= 3.7     | Already installed                    |

Slang is the only shader language. `slangc` ships with the Vulkan SDK 1.4+;
no separate install. See [ADR 0012](architecture/0012-slang-shaders.md).

## Windows install (winget)

```powershell
winget install --id Kitware.CMake -e
winget install --id Ninja-build.Ninja -e
winget install --id Microsoft.VisualStudio.2022.BuildTools -e `
    --override "--passive --add Microsoft.VisualStudio.Workload.VCTools `
                --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                --add Microsoft.VisualStudio.Component.Windows11SDK.22621"
winget install --id KhronosGroup.VulkanSDK -e
```

Restart the shell after installing the Vulkan SDK so `VULKAN_SDK` is exported.

### Qt 6 install (Windows, via aqt)

`aqt` is the scripted alternative to the Qt online installer — same
prebuilt artefacts, no GUI, can run unattended. Requires Python /
uv (already on most dev machines).

```powershell
uv tool install aqtinstall
aqt install-qt windows desktop 6.8.1 win64_msvc2022_64 `
    -m qtdeclarative qtshadertools `
    -O C:\Qt
```

Disk usage: ~3.5 GB. Time: ~10-15 min depending on bandwidth.

Then point CMake at the install:
```powershell
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.1\msvc2022_64"
# (or pass `-DCMAKE_PREFIX_PATH=...` to the `cmake -S . -B build`
# invocation, or export it in your shell profile.)
```

Alternative — the Qt online installer (GUI) downloads the same kit
with a wizard; `aqt` is just faster + scriptable.

Verify:
```powershell
& "$env:CMAKE_PREFIX_PATH\bin\qmake.exe" --version
```

Verify:
```powershell
cmake --version
ninja --version
$env:VULKAN_SDK
& "$env:VULKAN_SDK\Bin\slangc.exe" -v
```

## Linux install (Ubuntu 24.04)

```bash
sudo apt update
sudo apt install -y build-essential clang-18 cmake ninja-build \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libgl1-mesa-dev libwayland-dev libxkbcommon-dev wayland-protocols pkg-config
# Vulkan SDK: follow https://vulkan.lunarg.com/sdk/home#linux
# Qt 6: either `sudo apt install qt6-base-dev qt6-declarative-dev qt6-shadertools-dev`
# or use aqt:
pipx install aqtinstall
aqt install-qt linux desktop 6.8.1 linux_gcc_64 \
    -m qtdeclarative qtshadertools \
    -O ~/Qt
export CMAKE_PREFIX_PATH=$HOME/Qt/6.8.1/gcc_64
```

## macOS install

```bash
brew install cmake ninja qt6
# Vulkan SDK (MoltenVK): https://vulkan.lunarg.com/sdk/home#mac
# brew exports Qt via `$(brew --prefix qt6)`; point CMake at it:
export CMAKE_PREFIX_PATH=$(brew --prefix qt6)
```

## Build

From a "x64 Native Tools Command Prompt for VS 2022" (or any shell with MSVC env):

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
.\build\bin\noted_app.exe
```

Expected output:
```
selected GPU: <GPU name>
swapchain: 3 images | 1600x1000 | format=43 | mode=1
```
A 1600x1000 window opens showing the textured fullscreen quad
(checkerboard if `sample.png` is not next to the binary).

## Common issues

- **`Could NOT find Vulkan`** — Vulkan SDK not installed, or `VULKAN_SDK` env var not set. Reopen shell after install.
- **`Could NOT find Qt6` / `Qt6 not found`** — Qt 6.7+ not installed, or `CMAKE_PREFIX_PATH` not pointing at the kit's CMake config dir. Install via the aqt block above and set `CMAKE_PREFIX_PATH` to the kit root (e.g. `C:\Qt\6.8.1\msvc2022_64`).
- **MSVC not found** — run from `x64 Native Tools Command Prompt`, or call `vcvars64.bat` manually.
- **GLFW Wayland errors on Linux** — install `libwayland-dev` and `wayland-protocols`.
