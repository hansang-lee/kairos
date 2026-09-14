#!/bin/bash
set -e

if command -v git &>/dev/null && git rev-parse --show-toplevel &>/dev/null; then
    BASE_DIR="$(git rev-parse --show-toplevel)"
elif BASE_DIR="$(find "$(realpath "${PWD}")" \
    -maxdepth 10 \
    -type f \
    -name .root \
    -exec dirname {} \; | head -n1)" && [ -n "${BASE_DIR}" ]; then :
else
    BASE_DIR="$(dirname "$(realpath "$0")")"
fi

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
