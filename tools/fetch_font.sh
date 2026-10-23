#!/usr/bin/env bash
# Fetches the Fredoka font (SIL Open Font License) from the official Google Fonts repository.
set -euo pipefail

OUT="${1:-$(dirname "$0")/../games/brickbreaker/assets/fonts}"
BASE="https://raw.githubusercontent.com/google/fonts/main/ofl/fredoka"
mkdir -p "$OUT"
curl -fsSL "$BASE/Fredoka%5Bwdth,wght%5D.ttf" -o "$OUT/Fredoka.ttf"
curl -fsSL "$BASE/OFL.txt" -o "$OUT/OFL.txt"
echo "fetched Fredoka.ttf + OFL.txt into $OUT"
