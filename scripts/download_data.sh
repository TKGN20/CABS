#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if (( $# )); then
    exec python3 "$ROOT/scripts/prepare_data.py" "$@"
fi
python3 "$ROOT/scripts/prepare_data.py" --scale 0.1M
python3 "$ROOT/scripts/prepare_data.py" --scale 1M
