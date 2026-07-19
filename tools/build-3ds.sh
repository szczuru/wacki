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

# Prepare 48x48 icon for SMDH (automatically resize if needed)
ICON_SOURCE="assets/icons/wacki.png"
ICON_48="assets/icons/wacki-48.png"

if [ -f "$ICON_SOURCE" ]; then
    echo "🎨 Preparing 48x48 icon..."
    
    # Check if we need to resize (if icon is not already 48x48)
    if command -v convert &> /dev/null; then
        # ImageMagick available - use it
        convert "$ICON_SOURCE" -resize 48x48! "$ICON_48" 2>/dev/null && \
        echo "✓ Created $ICON_48 (resized from $ICON_SOURCE)" || \
        echo "⚠️  Failed to resize icon with ImageMagick"
    elif command -v ffmpeg &> /dev/null; then
        # FFmpeg available as fallback
        ffmpeg -i "$ICON_SOURCE" -vf scale=48:48 "$ICON_48" -y 2>/dev/null && \
        echo "✓ Created $ICON_48 (resized with ffmpeg)" || \
        echo "⚠️  Failed to resize icon with ffmpeg"
    elif command -v python3 &> /dev/null; then
        # Python + PIL as fallback
        python3 -c "
from PIL import Image
import sys
try:
    img = Image.open('$ICON_SOURCE')
    img = img.convert('RGBA')
    img_resized = img.resize((48, 48), Image.Resampling.LANCZOS)
    img_resized.save('$ICON_48', 'PNG')
    print('✓ Created $ICON_48 (resized with PIL)')
except ImportError:
    print('⚠️  Python PIL not available')
    sys.exit(1)
except Exception as e:
    print(f'⚠️  Failed to resize: {e}')
    sys.exit(1)
" || echo "⚠️  Python resize failed"
    else
        echo "⚠️  No image tool found (ImageMagick, ffmpeg, or Python PIL)"
        echo "   Icon will use default if smdhtool fails"
    fi
    echo ""
else
    echo "⚠️  Source icon not found: $ICON_SOURCE"
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

# Create SMDH (icon/metadata) if 48x48 icon exists
if [ -f "$ICON_48" ]; then
    echo "🎨 Creating SMDH metadata with icon..."
    
    if command -v smdhtool &> /dev/null; then
        smdhtool --create "Wacki" \
                 "Point-and-click adventure" \
                 "szczuru" \
                 "$ICON_48" \
                 dist/wacki.smdh 2>/dev/null && \
        echo "✓ Created dist/wacki.smdh with custom icon"
        
        # Clean up temporary 48x48 icon
        rm -f "$ICON_48"
    else
        echo "⚠️  smdhtool not found - no icon metadata"
        echo "   Install: sudo dkp-pacman -S smdhtool"
    fi
    echo ""
else
    echo "⚠️  No 48x48 icon available"
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
