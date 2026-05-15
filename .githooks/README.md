# Git hooks

These hooks enforce the same lint rules CI runs, locally and fast.

Hooks live in `.githooks/` (tracked) rather than `.git/hooks/` (untracked) so
they version with the repo.

## Install

Run once per clone:

```powershell
pwsh ./scripts/setup/install-hooks.ps1
# or, on bash:
git config core.hooksPath .githooks
chmod +x .githooks/*
```

## What runs

| Hook        | Checks                                                    |
|-------------|-----------------------------------------------------------|
| pre-commit  | secret patterns, clang-format on staged C++              |
| commit-msg  | Conventional Commits subject, 72-char cap                |
| pre-push    | refuses direct push to `main`                            |

## Bypassing

Don't `--no-verify` casually. If a hook is wrong, fix the hook.
