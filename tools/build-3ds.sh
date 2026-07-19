#!/bin/bash
# tools/build-3ds.sh — Build Wacki for 3DS with NovaGL

set -e

echo "========================================="
echo "  Building Wacki for Nintendo 3DS"
echo "  Using NovaGL for hardware rendering"
echo "========================================="
echo ""

# Check devkitPro
if [ -z "$DEVKITPRO" ]; then
    export DEVKITPRO=/opt/devkitpro
fi

if [ ! -d "$DEVKITPRO" ]; then
    echo "✗ ERROR: devkitPro not found at $DEVKITPRO"
    exit 1
fi

export DEVKITARM=$DEVKITPRO/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH

# Check tools
echo "🔍 Checking build tools..."

if ! command -v arm-none-eabi-gcc &> /dev/null; then
    echo "✗ ERROR: arm-none-eabi-gcc not found"
    exit 1
fi

if ! command -v 3dsxtool &> /dev/null; then
    echo "✗ ERROR: 3dsxtool not found"
    exit 1
fi

if ! command -v cmake &> /dev/null; then
    echo "✗ ERROR: cmake not found (needed for NovaGL)"
    exit 1
fi

echo "✓ Build tools ready"
echo ""

# Check/Download NovaGL
if [ ! -d "external/NovaGL" ]; then
    echo "✗ ERROR: NovaGL not found at external/NovaGL"
    echo ""
    echo "Downloading NovaGL automatically..."
    
    if command -v git &> /dev/null; then
        git clone --depth=1 https://github.com/efimandreev0/NovaGL.git external/NovaGL
        echo "✓ NovaGL downloaded"
    else
        echo "Download NovaGL manually:"
        echo "  1. Go to: https://github.com/efimandreev0/NovaGL"
        echo "  2. Click 'Code' → 'Download ZIP'"
        echo "  3. Extract to: external/NovaGL"
        exit 1
    fi
fi

# Fix NovaGL CMakeLists.txt
echo "🔧 Patching NovaGL CMakeLists.txt..."
cd external/NovaGL/src

# Remove gas.c from CMakeLists.txt (file doesn't exist in current repo)
if grep -q "gas.c" CMakeLists.txt; then
    sed -i.bak '/gas.c/d' CMakeLists.txt
    echo "  ✓ Removed gas.c reference"
else
    echo "  ✓ Already patched"
fi

cd ../../..

# Build NovaGL
echo "🔨 Building NovaGL..."
cd external/NovaGL

mkdir -p build && cd build

echo "  Running CMake..."
cmake -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/3DS.cmake" \
      -DCMAKE_BUILD_TYPE=Release \
      -DGL2CITRO3D_BUILD_EXAMPLES=OFF \
      .. || {
    echo "✗ CMake configuration failed"
    exit 1
}

echo "  Compiling NovaGL..."
make -j$(nproc) || {
    echo "✗ NovaGL build failed"
    exit 1
}

cd ..

# Find and organize library
mkdir -p lib

if [ -f "build/src/libNovaGL.a" ]; then
    cp build/src/libNovaGL.a lib/
    echo "  ✓ Library: lib/libNovaGL.a"
elif [ -f "build/libNovaGL.a" ]; then
    cp build/libNovaGL.a lib/
    echo "  ✓ Library: lib/libNovaGL.a"
else
    echo "✗ ERROR: libNovaGL.a not found after build!"
    echo "Build artifacts:"
    find build -name "*.a" -type f
    exit 1
fi

cd ../..
echo "✓ NovaGL built successfully"
echo ""

# Prepare icon
ICON_SOURCE="assets/icons/wacki.png"
ICON_48="wacki-48.png"

if [ -f "$ICON_SOURCE" ]; then
    echo "🎨 Preparing icon (48x48)..."
    
    # Try ImageMagick
    if command -v convert &> /dev/null; then
        convert "$ICON_SOURCE" -resize 48x48! "$ICON_48" 2>/dev/null && echo "  ✓ Resized with ImageMagick"
    # Try ffmpeg
    elif command -v ffmpeg &> /dev/null; then
        ffmpeg -i "$ICON_SOURCE" -vf scale=48:48 "$ICON_48" -y 2>/dev/null && echo "  ✓ Resized with ffmpeg"
    # Try Python PIL
    elif command -v python3 &> /dev/null; then
        python3 -c "from PIL import Image; Image.open('$ICON_SOURCE').resize((48,48)).save('$ICON_48')" 2>/dev/null && echo "  ✓ Resized with Python PIL"
    else
        echo "  ⚠ WARNING: No image tool found (convert/ffmpeg/python3+PIL)"
        echo "            Icon will not be created"
    fi
    echo ""
fi

# Build Wacki
echo "🔨 Building Wacki..."
make TARGET=3ds clean 2>/dev/null || true
make TARGET=3ds -j$(nproc) || {
    echo "✗ Wacki build failed"
    exit 1
}

echo "✓ Compilation successful"
echo ""

# Create 3DSX
if [ ! -f "dist/wacki" ]; then
    echo "✗ ERROR: dist/wacki not found"
    exit 1
fi

echo "📦 Creating .3dsx..."
3dsxtool dist/wacki dist/wacki.3dsx || {
    echo "✗ 3dsxtool failed"
    exit 1
}
echo "  ✓ Created dist/wacki.3dsx"

# Create SMDH with icon
if [ -f "$ICON_48" ] && command -v smdhtool &> /dev/null; then
    echo "📦 Creating .smdh..."
    smdhtool --create "Wacki" \
             "Point-and-click adventure (NovaGL)" \
             "szczuru" \
             "$ICON_48" \
             dist/wacki.smdh 2>/dev/null && echo "  ✓ Created dist/wacki.smdh"
    rm -f "$ICON_48"
fi

echo ""
echo "========================================="
echo "  ✓ Build Complete!"
echo "========================================="
echo ""

if [ -f "dist/wacki.3dsx" ]; then
    SIZE=$(du -h dist/wacki.3dsx | cut -f1)
    echo "  📦 dist/wacki.3dsx ($SIZE)"
fi

if [ -f "dist/wacki.smdh" ]; then
    SIZE=$(du -h dist/wacki.smdh | cut -f1)
    echo "  🎨 dist/wacki.smdh ($SIZE)"
fi

echo ""
echo "Installation:"
echo "  Copy dist/wacki.3dsx → sdmc:/3ds/wacki/wacki.3dsx"
echo "  Copy data/WACKI.EXE  → sdmc:/3ds/wacki/data/WACKI.EXE"
echo ""
