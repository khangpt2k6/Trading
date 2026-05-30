#!/usr/bin/env bash
# Installs the TradeFlow build toolchain inside WSL2 Ubuntu.
set -euo pipefail

echo "==> Updating apt and installing build tools"
sudo apt-get update -y
sudo apt-get install -y build-essential cmake ninja-build git pkg-config curl ca-certificates zlib1g-dev

echo "==> Toolchain versions"
g++ --version | head -1
cmake --version | head -1
ninja --version || true

echo "==> Done. Native build toolchain ready."
echo "    (Node.js, Emscripten, and flatc are installed in later phases.)"
