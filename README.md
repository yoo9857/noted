# noted

Professional note-taking + raster image editing software.

## Status

Pre-alpha. Architecture and PoC phase.

## Tech stack (planned)

- **Language**: C++23 (transitioning toward C++26 modules)
- **GPU**: Vulkan 1.3 (Linux/Windows), Metal 3 (macOS), DirectX 12 (Windows)
- **Shaders**: Slang
- **Build**: CMake + Ninja + vcpkg
- **UI**: TBD (Qt 6 QML vs custom immediate-mode)
- **Color**: OpenColorIO 2 + lcms2
- **Text**: HarfBuzz + FreeType + ICU
- **Image IO**: OpenImageIO
- **Plugins**: WASM (wasmtime)
- **Collab**: CRDT (Yjs algorithm port)

## Architecture goals

- Data-oriented design (DOD), not OOP
- Tile-based image representation (256x256), sparse virtual textures
- Non-destructive layer DAG
- GPU-driven pipeline (bindless, indirect)
- Fiber-based job system
- Unified document model: text blocks and canvas as one tree

## Repository layout (target)

```
engine/    rendering core (GPU abstraction, tile mgmt, color, jobs)
domain/    document model, CRDT, command pattern
plugin/    WASM host
ui/        user interface layer
shaders/   Slang sources
tests/     unit + GPU golden-image regression
tools/     profiling, benchmarks
```

## Build (placeholder)

```
cmake -S . -B build -G Ninja
cmake --build build
```

## License

TBD.
