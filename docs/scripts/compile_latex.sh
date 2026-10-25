#!/usr/bin/env bash
# Compile script for LaTeX documents using XeLaTeX in podman container or locally
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

TARGET="${1:-docs/plantilla_memoria.tex}"
BASENAME="$(basename "${TARGET}" .tex)"
TARGET_DIR="$(dirname "${TARGET}")"

echo "=== Compiling ${TARGET} using XeLaTeX ==="

if command -v xelatex >/dev/null 2>&1; then
    echo "Using local xelatex..."
    (cd "${PROJECT_ROOT}" && TEXINPUTS=".:docs:" xelatex -interaction=nonstopmode -output-directory="${TARGET_DIR}" "${TARGET}")
    (cd "${PROJECT_ROOT}" && TEXINPUTS=".:docs:" xelatex -interaction=nonstopmode -output-directory="${TARGET_DIR}" "${TARGET}")
else
    echo "Using containerized xelatex (docker.io/pandoc/latex:latest via podman)..."
    podman run --rm \
        -v "${PROJECT_ROOT}:/workspace:Z" \
        -v /usr/share/fonts:/usr/share/fonts:ro \
        -w /workspace \
        --entrypoint /bin/sh \
        docker.io/pandoc/latex:latest \
        -c "export TEXINPUTS='.:docs:' && xelatex -interaction=nonstopmode -output-directory='${TARGET_DIR}' '${TARGET}' && xelatex -interaction=nonstopmode -output-directory='${TARGET_DIR}' '${TARGET}'"
fi

echo "=== Compilation finished successfully: ${TARGET_DIR}/${BASENAME}.pdf ==="
