#!/usr/bin/env bash
set -euo pipefail

# Build and run the Moba engine sandbox.
cd "$(dirname "$0")"

if [ ! -d build ]; then
  cmake -B build -DCMAKE_BUILD_TYPE=Release
fi
cmake --build build -j

cd build
exec ./moba
