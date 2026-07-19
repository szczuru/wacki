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

if ! command -v 3dsxtool &> /dev/null; then
    echo "✗ ERROR: 3dsxtool not found"
    echo "Install 3ds-tools: sudo dkp-pacman -S 3ds-tools"
    exit 1
fi

echo "✓ arm-none-eabi-gcc found"
echo "✓ 3dsxtool found"
echo ""

# Check for SDL2
if [ ! -d "$DEVKITPRO/portlibs/3ds" ]; then
    echo "✗ ERROR: 3DS portlibs not installed"
    echo "Install SDL2: sudo dkp-pacman -S 3ds-sdl2"
    exit 1
fi

if [ ! -f "$DEVKITPRO/portlibs/3ds/lib/libSDL2.a" ]; then
    echo "✗ ERROR: SDL2 for 3DS not found"
    echo "Install: sudo dkp-pacman -S 3ds-sdl2"
    exit 1
fi

echo "✓ SDL2 for 3DS found"
echo ""

# Check for WACKI.EXE
if [ ! -f "data/WACKI.EXE" ]; then
    echo "⚠️  WARNING: data/WACKI.EXE not found"
    echo "The game will not run without it!"
    echo ""
fi

# Clean previous build
echo "🧹 Cleaning previous build..."
make TARGET=3ds clean 2>/dev/null || true
echo ""

# Build
echo "🔨 Building for TARGET=3ds..."
echo ""
make TARGET=3ds -j$(nproc) || {
    echo ""
    echo "✗ Build failed!"
    exit 1
}

echo ""
echo "✓ Compilation successful!"
echo ""

# Check if ELF was created
if [ ! -f "dist/wacki" ]; then
    echo "✗ ERROR: dist/wacki not created"
    exit 1
fi

# Create .3dsx file (homebrew executable)
echo "📦 Creating .3dsx homebrew executable..."

3dsxtool dist/wacki dist/wacki.3dsx || {
    echo "✗ Failed to create .3dsx"
    exit 1
}

echo "✓ Created dist/wacki.3dsx"
echo ""

# Create SMDH (icon/metadata) if icon exists
if [ -f "assets/icons/wacki-48.png" ]; then
    echo "🎨 Creating SMDH metadata..."
    
    if command -v smdhtool &> /dev/null; then
        smdhtool --create "Wacki" \
                 "Point-and-click adventure" \
                 "szczuru" \
                 assets/icons/wacki-48.png \
                 dist/wacki.smdh 2>/dev/null && \
        echo "✓ Created dist/wacki.smdh"
    else
        echo "⚠️  smdhtool not found - no icon metadata"
        echo "   Install: sudo dkp-pacman -S smdhtool"
    fi
    echo ""
else
    echo "⚠️  No icon found (assets/icons/wacki-48.png)"
    echo "   .3dsx will use default icon"
    echo ""
fi

# Print file sizes
echo "▪ Build artifacts:"
ls -lh dist/wacki* 2>/dev/null | awk '{print "   " $9 " (" $5 ")"}'
echo ""

# Final instructions
echo "========================================="
echo "  ✓ Build Complete!"
echo "========================================="
echo ""
echo "📂 Output files:"
echo "   dist/wacki.3dsx - Homebrew executable"
if [ -f "dist/wacki.smdh" ]; then
    echo "   dist/wacki.smdh - Icon metadata"
fi
echo ""
echo "📝 Installation instructions:"
echo ""
echo "1. Copy to SD card:"
echo "   dist/wacki.3dsx → sdmc:/3ds/wacki/wacki.3dsx"
echo "   data/WACKI.EXE  → sdmc:/3ds/wacki/data/WACKI.EXE"
echo ""
echo "2. Launch via Homebrew Launcher"
echo ""
echo "========================================="
