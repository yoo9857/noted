# Contributing

## Branch protocol

- `main` is protected. No direct pushes.
- Work on feature branches: `feat/*`, `fix/*`, `chore/*`, `refactor/*`, `docs/*`.
- Open a Pull Request to `main`. Squash-merge after CI passes and at least one review.

## Commit messages

Conventional Commits, lowercase:

```
<type>(<scope>): <subject>

[optional body]

[optional footer]
```

Types: `feat`, `fix`, `chore`, `refactor`, `docs`, `test`, `perf`, `build`, `ci`.

Example:
```
feat(engine): vulkan instance bootstrap with device enumeration
```

## Code style

- C++23, no extensions (`-std=c++23` / `/std:c++latest`).
- MSVC: `/W4 /permissive-`. Clang/GCC: `-Wall -Wextra -Wpedantic -Wconversion`.
- Treat warnings as errors before tagged releases.
- Format with `clang-format` (config TBD).
- No exceptions across module boundaries. Return `std::expected` (C++23).
- No `using namespace` in headers. No raw `new`/`delete` outside allocators.

## GitHub repo settings (one-time, owner action)

After the first push to `main`:

1. **Settings → Branches → Add branch ruleset** for `main`:
   - Require a pull request before merging
   - Require status checks: `build (windows-latest / Release)`, `build (ubuntu-latest / Release)`
   - Require linear history
   - Block force pushes and deletions
2. **Settings → Actions → General → Workflow permissions** → Read repository contents
3. **Settings → Code security** → Enable Dependabot alerts and CodeQL

## Local development

See [`docs/SETUP.md`](docs/SETUP.md).
