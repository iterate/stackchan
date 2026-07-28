#!/usr/bin/env bash
# Copy local (gitignored) YAML into upstream/firmware/data for SPIFFS upload.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOCAL="$ROOT/local"
DATA="$ROOT/upstream/firmware/data"
CONFIG="$ROOT/config"

if [[ ! -d "$ROOT/upstream/firmware" ]]; then
  echo "Missing upstream. Run ./scripts/bootstrap.sh first." >&2
  exit 1
fi

mkdir -p "$DATA" "$LOCAL"

copy_one() {
  local name="$1"
  if [[ -f "$LOCAL/$name" ]]; then
    cp "$LOCAL/$name" "$DATA/$name"
    echo "Installed local/$name → firmware/data/$name"
  elif [[ -f "$CONFIG/${name}.example" ]]; then
    cp "$CONFIG/${name}.example" "$DATA/$name"
    echo "Installed example $name (no local override)"
  else
    echo "Missing $name (no local/ or example)" >&2
    exit 1
  fi
}

copy_one SC_SecConfig.yaml
copy_one SC_ExConfig.yaml
copy_one SC_BasicConfig.yaml

# Refuse to proceed if secrets still look like placeholders
if grep -qE 'YOUR_WIFI_|CHANGE_ME_|sk-\.\.\.' "$DATA/SC_SecConfig.yaml"; then
  echo "ERROR: SC_SecConfig.yaml still has placeholders. Edit local/SC_SecConfig.yaml" >&2
  exit 1
fi

echo "Config ready under $DATA"
echo "Remember: firmware/data secrets must never be committed (upstream/ is gitignored)."
