#!/usr/bin/env bash
# Download a PREFIX of a real NASDAQ TotalView-ITCH 5.0 sample file via an HTTP
# Range request, into the WSL-native filesystem (NOT the OneDrive-synced repo,
# and NOT committed to git). ITCH is time-ordered and front-loads the full stock
# directory plus the market open, so a ~1 GB prefix is genuine data covering
# every symbol's open - enough for a real benchmark and demo.
#
# Usage: ./scripts/fetch_itch.sh [SIZE_MB] [DATE]
#   SIZE_MB  prefix size in megabytes (default 1024)
#   DATE     sample date MMDDYYYY (default 01302019)
set -euo pipefail

MB="${1:-1024}"
DATE="${2:-01302019}"
URL="https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/${DATE}.NASDAQ_ITCH50.gz"
DIR="${TRADEFLOW_DATA:-$HOME/tradeflow_data}"
OUT="$DIR/${DATE}.NASDAQ_ITCH50.partial.gz"
BYTES=$(( MB * 1024 * 1024 ))

mkdir -p "$DIR"
echo "==> Fetching first ${MB} MB of:"
echo "    $URL"
echo "    -> $OUT"
curl -s --fail --location --max-time 3600 -r "0-$((BYTES - 1))" "$URL" -o "$OUT"
echo "==> Downloaded:"
ls -lh "$OUT"
echo "==> Data dir: $DIR (export TRADEFLOW_DATA to override; never committed)."
