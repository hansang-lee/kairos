#!/bin/bash
set -e

source "$(dirname "$(realpath "$0")")/scripts/common.sh"

BUILD_TYPE="${1:-Release}"
BUILD_DIR="${BASE_DIR}/build/${BUILD_TYPE}"

cmake \
    -G Ninja \
    -S "${BASE_DIR}" \
    -B "${BUILD_DIR}" \
    --no-warn-unused-cli \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake \
    --build "${BUILD_DIR}" \
    --config "${BUILD_TYPE}" \
    --target all \
    -- -j$(nproc)

echo ""
echo "Build complete: ${BUILD_DIR}"
