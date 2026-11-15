#!/usr/bin/env bash
set -euo pipefail
if [[ -f cmake-build-debug/CMakeCache.txt ]]; then
    cmake --build cmake-build-debug --target hw2
else
    cmake -S . -B build
    cmake --build build --target hw2
fi
