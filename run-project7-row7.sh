#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

./build/scratch/ns3.45-project7-row7-wired-default \
  --sweep=all \
  --csvFile=project7-row7-wired.csv \
  --appendCsv=0

./build/scratch/ns3.45-project7-row7-lrwpan-static-default \
  --sweep=all \
  --csvFile=project7-row7-lrwpan-static.csv \
  --appendCsv=0

python3 project7-row7-plot.py
