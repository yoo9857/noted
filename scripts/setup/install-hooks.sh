#!/usr/bin/env bash
# Install repository-tracked git hooks.
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

git config core.hooksPath .githooks
chmod +x .githooks/pre-commit .githooks/commit-msg .githooks/pre-push || true

echo "Git hooks installed (core.hooksPath -> .githooks)."
