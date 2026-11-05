#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
staging_dir=$(mktemp -d "${TMPDIR:-/tmp}/ql32-olimex-fs.XXXXXX")
trap 'rm -rf "$staging_dir"' EXIT HUP INT TERM
upload_port=${1:-}

if [ "$#" -gt 1 ]; then
    printf '%s\n' 'Usage: tools/upload-olimex-fs.sh [serial-port]' >&2
    exit 2
fi

cp "$repo_dir/data/startup.bas" "$repo_dir/data/ql32.inc" \
   "$repo_dir/data/mandelbrot.bas" "$staging_dir/"
mkdir -p "$staging_dir/supervisor"
cp "$repo_dir"/data/supervisor/worker-*.bas "$staging_dir/supervisor/"

set --
if [ -n "$upload_port" ]; then
    set -- --upload-port "$upload_port"
fi

cd "$repo_dir"
PLATFORMIO_DATA_DIR="$staging_dir" \
    pio run -e olimex_esp32_sbc_fabgl -t uploadfs "$@"
