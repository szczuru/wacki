#!/usr/bin/env bash
# tools/build-3ds.sh — Nintendo 3DS Docker build script.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DOCKER_IMAGE="devkitpro/devkitarm:latest"

echo "Building Wacki for Nintendo 3DS..."
echo "Repository: $REPO_ROOT"

# Run make inside Docker container
docker run --rm \
    -v "$REPO_ROOT:/project" \
    -w /project \
    "$DOCKER_IMAGE" \
    bash -c "make clean && make TARGET=3ds -j\$(nproc)"

echo ""
echo "Build complete!"
echo "Output: dist/wacki.3dsx"
