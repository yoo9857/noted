# noted

Professional note-taking + raster image editing software.
Goal: Goodnotes-class stylus + ink, Photoshop-class layered raster
editing, in one unified document model. AAA-grade performance and
maintainability.

> **Picking up the project?** Read [`HANDOFF.md`](HANDOFF.md) first.
> It tells you where the work is, what builds, and what the next PRs are.

## Status

Foundation complete; rendering pipeline online. The app builds and runs
on Windows and Linux: a 1600×1000 window opens, the GPU is picked, a
Slang-compiled fullscreen quad samples a texture, the frame loop ticks
with zero Vulkan validation errors. See [`HANDOFF.md`](HANDOFF.md) for
the full breakdown.

## Tech stack (current)

- **Language**: C++23
- **GPU**: Vulkan 1.4 (1.2/1.3 modern features enabled by default —
  descriptor indexing, buffer device address, timeline semaphores,
  dynamic rendering, synchronization2)
- **Shaders**: Slang (Microsoft + Khronos) — the only shader language;
  no GLSL anywhere
- **Memory**: VulkanMemoryAllocator (VMA) v3.1
- **Windowing/input**: GLFW 3.4
- **Image decode**: stb_image
- **Build**: CMake 3.28 + Ninja + FetchContent
- **Tests**: GoogleTest

## Architecture goals

- Data-oriented design (DOD), not OOP
- Tile-based image representation (256x256), sparse virtual textures
- Non-destructive layer DAG
- GPU-driven pipeline (bindless via descriptor indexing today,
  `VK_EXT_descriptor_buffer` next)
- Fiber-based job system (TBD)
- Unified document model: text blocks and canvas as one tree

## Repository layout

```
engine/       Core: Vulkan, allocator, hooks, error model, harness
domain/       Pure logic: document, layer DAG, commands, CRDT (stubs)
plugin/       WASM plugin host (stubs)
platform/     Windowing, input, fs, image_io
ui/           View layer (stubs — UI stack TBD)
app/          Executable entry
shaders/      Slang sources
cmake/        CMake helpers + warning/hardening policy
docs/architecture/  12 ADRs documenting every cross-cutting decision
tests/        Unit + integration + bench + fuzz scaffolds
```

## Build

Prerequisites: VS 2022 Build Tools 17.10+, CMake ≥ 3.28, Ninja, Vulkan
SDK ≥ 1.4.309. Full install guide: [`docs/SETUP.md`](docs/SETUP.md).

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
.\build\bin\noted_app.exe
```

You should see a magenta/dark-grey checkerboard fill the window. Drop a
`sample.png` next to the binary to swap in your own image.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md). All work goes through PRs to
`main`. Read the [ADRs](docs/architecture/) before touching cross-cutting
code.

## License

TBD.
