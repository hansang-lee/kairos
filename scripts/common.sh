#!/bin/bash
# Common script to resolve the project root directory (BASE_DIR).
# Source this file from other scripts:
#   source "$(dirname "$(realpath "$0")")/scripts/common.sh"

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
