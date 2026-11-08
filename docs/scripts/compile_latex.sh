#!/usr/bin/env bash
# Compile script for LaTeX documents using XeLaTeX in podman container or locally
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

TARGET="${1:-docs/memoria.tex}"
BASENAME="$(basename "${TARGET}" .tex)"
TARGET_DIR="$(dirname "${TARGET}")"

echo "=== Compiling ${TARGET} using XeLaTeX ==="

if command -v xelatex >/dev/null 2>&1; then
    echo "Using local xelatex..."
    (cd "${PROJECT_ROOT}" && TEXINPUTS=".:docs:" xelatex -interaction=nonstopmode -output-directory="${TARGET_DIR}" "${TARGET}")
    (cd "${PROJECT_ROOT}" && TEXINPUTS=".:docs:" xelatex -interaction=nonstopmode -output-directory="${TARGET_DIR}" "${TARGET}")
else
    echo "Using containerized xelatex (docker.io/pandoc/latex:latest via podman)..."
    FONT_MOUNT=""
    if [ -d "/mnt/c/Windows/Fonts" ]; then
        FONT_MOUNT="-v /mnt/c/Windows/Fonts:/usr/share/fonts:ro"
    elif [ -d "/usr/share/fonts" ]; then
        FONT_MOUNT="-v /usr/share/fonts:/usr/share/fonts:ro"
    fi

    podman run --rm \
        -v "${PROJECT_ROOT}:/workspace:Z" \
        ${FONT_MOUNT} \
        -w /workspace \
        --entrypoint /bin/sh \
        docker.io/pandoc/latex:latest \
        -c "export TEXINPUTS='.:docs:' && xelatex -interaction=nonstopmode -output-directory='${TARGET_DIR}' '${TARGET}' && xelatex -interaction=nonstopmode -output-directory='${TARGET_DIR}' '${TARGET}'"
fi

echo "=== Compilation finished successfully: ${TARGET_DIR}/${BASENAME}.pdf ==="
