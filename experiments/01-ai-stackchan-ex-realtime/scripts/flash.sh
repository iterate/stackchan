#!/usr/bin/env bash
# Build + flash CoreS3 realtime firmware and SPIFFS filesystem image.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FW="$ROOT/upstream/firmware"
ENV_NAME="${PIO_ENV:-m5stack-cores3-realtime}"

if [[ ! -d "$FW" ]]; then
  echo "Missing upstream. Run ./scripts/bootstrap.sh first." >&2
  exit 1
fi

if [[ ! -f "$FW/data/SC_SecConfig.yaml" ]]; then
  echo "Missing firmware/data config. Run ./scripts/apply-local-config.sh first." >&2
  exit 1
fi

cd "$FW"

echo "==> Building $ENV_NAME"
pio run -e "$ENV_NAME"

echo "==> Uploading firmware"
pio run -e "$ENV_NAME" -t upload

echo "==> Uploading SPIFFS (YAML config)"
pio run -e "$ENV_NAME" -t uploadfs

echo "Done. Optional serial monitor:"
echo "  cd upstream/firmware && pio device monitor -e $ENV_NAME"
