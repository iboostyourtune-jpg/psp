#!/usr/bin/env bash
set -euo pipefail
make clean && make
mkdir -p out
cp EBOOT.PBP out/EBOOT.PBP
echo "Built: out/EBOOT.PBP"
