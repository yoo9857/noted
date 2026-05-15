# Local development setup

Target host for this guide: Windows 10/11 x64. Linux and macOS notes inline.

## Required toolchain

| Tool         | Version    | Why                                  |
|--------------|------------|--------------------------------------|
| Visual Studio Build Tools 2022 | 17.10+ | MSVC C++23 compiler |
| CMake        | >= 3.28    | Build generator (project requires 3.28) |
| Ninja        | >= 1.11    | Fast build driver                    |
| Vulkan SDK   | >= 1.3.290 | Headers, loader, validation layers   |
| Git          | >= 2.40    | Already installed                    |
| Git LFS      | >= 3.7     | Already installed                    |

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

Verify:
```powershell
cmake --version
ninja --version
$env:VULKAN_SDK
& "$env:VULKAN_SDK\Bin\glslangValidator.exe" --version
```

## Linux install (Ubuntu 24.04)

```bash
sudo apt update
sudo apt install -y build-essential clang-18 cmake ninja-build \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libgl1-mesa-dev libwayland-dev libxkbcommon-dev wayland-protocols pkg-config
# Vulkan SDK: follow https://vulkan.lunarg.com/sdk/home#linux
```

## macOS install

```bash
brew install cmake ninja
# Vulkan SDK (MoltenVK): https://vulkan.lunarg.com/sdk/home#mac
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
noted bootstrap: N Vulkan device(s)
  [0] <GPU name> | API 1.3.x | driver ...
```

## Common issues

- **`Could NOT find Vulkan`** — Vulkan SDK not installed, or `VULKAN_SDK` env var not set. Reopen shell after install.
- **MSVC not found** — run from `x64 Native Tools Command Prompt`, or call `vcvars64.bat` manually.
- **GLFW Wayland errors on Linux** — install `libwayland-dev` and `wayland-protocols`.
