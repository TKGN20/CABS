#!/usr/bin/env bash
set -euo pipefail
: "${CABS_CPUS:?Set CABS_CPUS to the physical CPUs used by this run, for example 0,2,4,6}"
if [[ ! "$CABS_CPUS" =~ ^[0-9,-]+$ ]]; then
    printf '%s\n' 'CABS_CPUS must be a CPU list accepted by taskset' >&2
    exit 2
fi
command -v taskset >/dev/null
cd "$(dirname "$0")/.."
exec taskset -c "$CABS_CPUS" python3 scripts/reproduce.py "$@"
