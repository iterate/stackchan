#!/usr/bin/env bash
# Clone AI_StackChan_Ex into ./upstream and apply our SPIFFS fallback patch.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM_DIR="$ROOT/upstream"
REPO_URL="${AI_STACKCHAN_EX_URL:-https://github.com/ronron-gh/AI_StackChan_Ex.git}"
PATCH="$ROOT/patches/0001-cores3-spiffs-config-fallback.patch"

if [[ ! -d "$UPSTREAM_DIR/.git" ]]; then
  echo "Cloning $REPO_URL → $UPSTREAM_DIR"
  git clone --depth 1 "$REPO_URL" "$UPSTREAM_DIR"
else
  echo "Upstream already present at $UPSTREAM_DIR"
fi

cd "$UPSTREAM_DIR"

if git apply --check "$PATCH" 2>/dev/null; then
  echo "Applying patch $PATCH"
  git apply "$PATCH"
elif git apply --reverse --check "$PATCH" 2>/dev/null; then
  echo "Patch already applied."
else
  # Dirty tree from a previous partial apply — try normal apply and report.
  if git apply "$PATCH"; then
    echo "Patch applied."
  else
    echo "WARN: could not apply patch cleanly. Inspect upstream/firmware/src/main.cpp" >&2
    exit 1
  fi
fi

echo "Bootstrap OK. Next:"
echo "  1. Create local/SC_SecConfig.yaml from config/*.example"
echo "  2. ./scripts/apply-local-config.sh"
echo "  3. ./scripts/flash.sh"
