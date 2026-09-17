#!/bin/bash
set -e

source "$(dirname "$(realpath "$0")")/scripts/common.sh"

exists() {
    if [ -e "$1" ]; then
        return 0
    fi
    return 1
}

format() {
    if exists "$1"; then
        find "$1" \
            -not -path "*/build/*" \
            -not -path "*/.git/*" \
            \( \
                -iname *.h \
                -o -iname *.hpp \
                -o -iname *.cpp \
                -o -iname *.c \
            \) \
            | xargs clang-format -i
    fi
}

if [ $# -gt 0 ]; then
    for path in "$@"; do
        format "$path"
    done
else
    format "${BASE_DIR}"
fi
