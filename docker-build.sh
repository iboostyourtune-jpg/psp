#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$PROJECT_ROOT/out"
mkdir -p "$OUT_DIR"
docker run --rm -v "$PROJECT_ROOT":/work -w /work pspdev/pspsdk bash -lc "make clean && make && cp EBOOT.PBP /work/out/EBOOT.PBP"
echo "Built: $OUT_DIR/EBOOT.PBP"
