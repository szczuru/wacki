#!/bin/bash
# tools/build-3ds.sh — Build Wacki for Nintendo 3DS using devkitPro toolchain

set -e

echo "========================================="
echo "  Building Wacki for Nintendo 3DS"
echo "========================================="
echo ""

# Check for devkitPro environment
if [ -z "$DEVKITPRO" ]; then
    export DEVKITPRO=/opt/devkitpro
fi

if [ ! -d "$DEVKITPRO" ]; then
    echo "✗ ERROR: devkitPro not found at $DEVKITPRO"
    echo ""
    echo "Install devkitPro:"
    echo "  https://devkitpro.org/wiki/Getting_Started"
    echo ""
    exit 1
fi

export DEVKITARM=$DEVKITPRO/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH

# Check for required tools
echo "🔍 Checking build tools..."

if ! command -v arm-none-eabi-gcc &> /dev/null; then
    echo "✗ ERROR: arm-none-eabi-gcc not found"
    echo "Install devkitARM: sudo dkp-pacman -S devkitARM"
    exit 1
fi

if !
