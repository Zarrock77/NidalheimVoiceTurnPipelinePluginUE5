#!/bin/sh
# Enable this repo's versioned git hooks (pre-commit secret scan).
# Run once after cloning: ./scripts/install-git-hooks.sh
set -e

REPO_ROOT=$(git rev-parse --show-toplevel)
cd "$REPO_ROOT"

git config core.hooksPath .githooks
chmod +x .githooks/* 2>/dev/null || true

echo "core.hooksPath -> .githooks"

if command -v gitleaks >/dev/null 2>&1; then
  echo "gitleaks: $(gitleaks version)"
elif command -v docker >/dev/null 2>&1; then
  echo "gitleaks: not on PATH, the hook will fall back to Docker."
else
  echo "gitleaks: not found, and no Docker either. The hook will warn without blocking."
  echo "   Install: https://github.com/gitleaks/gitleaks/releases"
fi
