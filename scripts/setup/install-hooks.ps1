# Install repository-tracked git hooks.
# Usage:  pwsh ./scripts/setup/install-hooks.ps1

$ErrorActionPreference = 'Stop'

$repoRoot = (git rev-parse --show-toplevel).Trim()
if (-not $repoRoot) {
    Write-Error "Not inside a git repository."
}

Set-Location $repoRoot

git config core.hooksPath .githooks

# Ensure executable bit on Unix-like checkouts.
$hookFiles = @('pre-commit', 'commit-msg', 'pre-push')
foreach ($h in $hookFiles) {
    $p = Join-Path '.githooks' $h
    if (Test-Path $p) {
        git update-index --chmod=+x $p 2>$null
    }
}

Write-Output "Git hooks installed (core.hooksPath -> .githooks)."
