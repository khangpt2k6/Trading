#!/usr/bin/env bash
# Build and run the test suite under ThreadSanitizer.
#
# Note: on Ubuntu 24.04 / recent WSL2 kernels, the default ASLR entropy
# (vm.mmap_rnd_bits = 32) is too high for ThreadSanitizer to map its shadow
# memory, causing "FATAL: ThreadSanitizer: unexpected memory mapping". We lower
# it to 28 for this session (reverts on reboot). See:
# https://github.com/google/sanitizers/issues/1716
set -euo pipefail

CUR=$(cat /proc/sys/vm/mmap_rnd_bits)
if [ "$CUR" -gt 28 ]; then
  echo "==> Lowering vm.mmap_rnd_bits from $CUR to 28 for ThreadSanitizer"
  sudo sysctl -w vm.mmap_rnd_bits=28
fi

echo "==> Configuring TSan build"
cmake -S . -B build-tsan -DTF_SANITIZE_THREAD=ON -DTF_BUILD_BENCH=OFF

echo "==> Building"
cmake --build build-tsan -j

echo "==> Running tests under ThreadSanitizer"
ctest --test-dir build-tsan --output-on-failure
