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

echo "✓ Build tools ready"
echo ""

# Check NovaGL submodule
if [ ! -d "external/NovaGL" ]; then
    echo "✗ ERROR: NovaGL not found!"
    echo ""
    echo "Run these commands:"
    echo "  git submodule add https://github.com/efimandreev0/NovaGL.git external/NovaGL"
    echo "  git submodule update --init --recursive"
    exit 1
fi

# Build NovaGL
echo "🔨 Building NovaGL..."
cd external/NovaGL

if [ ! -f "Makefile" ] && [ ! -f "CMakeLists.txt" ]; then
    echo "✗ ERROR: NovaGL has no Makefile or CMakeLists.txt"
    exit 1
fi

# Try CMake first
if [ -f "CMakeLists.txt" ]; then
    mkdir -p build
    cd build
    
    echo "  Running CMake..."
    cmake -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/3DS.cmake" \
          -DCMAKE_BUILD_TYPE=Release \
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
    mkdir -p lib include
    
    # Copy library
    if [ -f "build/libNovaGL.a" ]; then
        cp build/libNovaGL.a lib/
    elif [ -f "build/source/libNovaGL.a" ]; then
        cp build/source/libNovaGL.a lib/
    else
        echo "✗ ERROR: libNovaGL.a not found after build"
        exit 1
    fi
    
    # Copy headers if needed
    if [ -d "include" ] && [ ! "$(ls -A include)" ]; then
        cp -r source/*.h include/ 2>/dev/null || true
    fi
    
    cd ../..
else
    # Fallback to Makefile
    make -j$(nproc) || {
        echo "✗ NovaGL build failed"
        exit 1
    }
    cd ../..
fi

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

echo ""Rozumiem! Nie używasz CMD - pokażę Ci jak dodać submodule przez stronę GitHub i dam alternatywę.

---

## 🌐 SPOSÓB 1: Przez GitHub Web (Najprostszy)

Niestety **GitHub Web nie wspiera bezpośrednio dodawania submodule** - to wymaga git commands. Ale dam Ci **alternatywę bez CMD**!

---

## ✓ SPOSÓB 2: BEZ SUBMODULE - Kopiuj NovaGL ręcznie

Zamiast submodule, możesz po prostu **skopiować NovaGL do swojego repo**:

### Kroki:

1. **Pobierz NovaGL jako ZIP:**
   - Idź na: https://github.com/efimandreev0/NovaGL
   - Kliknij zielony przycisk **Code** → **Download ZIP**
   - Rozpakuj ZIP

2. **Skopiuj do swojego repo:**
   - Utwórz folder `external/NovaGL` w swoim repo
   - Skopiuj całą zawartość rozpakowanego NovaGL do `external/NovaGL`

3. **Dodaj pliki przez GitHub Web:**
   - Idź na stronę swojego repo: https://github.com/szczuru/wacki
   - Kliknij **Add file** → **Upload files**
   - Przeciągnij folder `external/NovaGL` (lub pojedyncze pliki)
   - Commit z message: `Add NovaGL for 3DS hardware rendering`

4. **Pomiń plik `.gitmodules`:**
   - Jeśli nie używasz submodule - **NIE DODAWAJ** pliku `.gitmodules`
   - Po prostu kopiuj NovaGL jako normalne pliki

---

## 📝 ZMODYFIKOWANE INSTRUKCJE (bez CMD)

### A) Pliki do skopiowania (WSZYSTKIE):

#### 1. `mk/3ds.mk`
