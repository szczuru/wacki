# Nintendo 3DS Port - Podsumowanie Implementacji

## Status
✅ **Kompletny port gotowy do kompilacji i testowania**

## Architektura

Port 3DS używa **hybrydowego podejścia**:
- **Warstwa kompatybilności SDL** (`SDL_compat.c/h` + `SDL.h`) mapuje API SDL na natywne API 3DS
- **Natywne renderowanie dual-screen** poprzez citro3d/citro2d
- **Niestandardowy input** w `gamepad_3ds.c` z custom kontrolkami
- **Reużycie kodu SDL platform** - `platform_sdl.c`, `video_sdl.c`, `audio_sdl.c` działają bez zmian

## Struktura Plików

### Pliki 3DS (src/platform/3ds/)
```
3ds.c                      - Platform hooks (video prefs, PE loader)
SDL_compat.c              - Implementacja SDL compatibility layer
SDL_compat.h              - Deklaracje typów i funkcji SDL
SDL.h                     - Wrapper header (found first by -I flag)
gamepad_3ds.c             - Custom input: A/B swap, zoom, hand modes
storage_3ds.c             - Save file handling (z sync() dla 3DS)
data_root_3ds.c           - SD card data directory discovery
system_3ds.c              - System init/exit (romfs, hid, apt)
embedded_wacki_pe_stub.c  - Empty PE table (gdy WACKI.EXE nie embedded)
README.md                 - Dokumentacja użytkownika po polsku
```

### Pliki Build/CI
```
mk/3ds.mk                 - Build configuration
tools/build-3ds.sh        - Docker build script
.github/workflows/3DS.yml - GitHub Actions CI workflow
assets/icons/wacki-3ds.png - Icon (skopiowany z wacki.png)
```

## Kluczowe Funkcje

### Dual-Screen Rendering
- **Górny ekran (400x240)**: Pełna gra przeskalowana z 640x480
- **Dolny ekran (320x240)**: Zoom wokół kursora (4 poziomy)
- Implementacja w `SDL_RenderCopy()` - jeden RenderCopy rysuje na obu ekranach

### Sterowanie

#### Podstawowe
- **A (prawy)** → Lewy klik myszy
- **B (dolny)** → Prawy klik myszy (zamiana jak w Switch!)
- **X (górny)** → Cycle zoom level (100% → 50% → 25% → 12.5%)
- **START** → Pause menu
- **SELECT** → Toggle left/right-hand mode

#### Hand Modes (L/ZL/R/ZR)

**RIGHT-HAND** (domyślny):
```
L  → quickload
ZL → left click (alternative)
R  → quicksave  
ZR → right click (alternative)
```

**LEFT-HAND** (po SELECT):
```
L  → left click
ZL → right click
R  → quicksave
ZR → quickload
```

#### Kursor
- **Circle Pad** → Płynny ruch
- **D-Pad** → Dyskretny ruch
- **Touch screen** → Bezpośrednie pozycjonowanie (mapowane przez zoom)

### Touch Screen Mapping
Touch na dolnym ekranie:
1. Oblicza pozycję relative (0.0-1.0) w oknie touch
2. Mapuje przez aktualny zoom level do współrzędnych gry
3. Ustawia `g_mouse_x`, `g_mouse_y` (extern globals z engine)

## Kompilacja

### Lokalnie (wymaga devkitARM)
```bash
make TARGET=3ds
```

### Docker
```bash
./tools/build-3ds.sh
```

### GitHub Actions
Automatyczny build przy push do master lub ręcznie (workflow_dispatch).

Workflow pobiera `WACKI.EXE` z sekretu `WACKI_EXE_URL` (opcjonalne).  
Bez sekretu: build używa dynamicznego ładowania z SD card.

## Wynik Kompilacji
```
dist/wacki.3dsx        - Plik wykonywalny homebrew
dist/wacki.smdh        - Metadata + icon
```

## Instalacja na 3DS
1. Skopiuj `wacki.3dsx` → `sdmc:/3ds/wacki/`
2. Skopiuj `WACKI.EXE` → `sdmc:/3ds/wacki/data/WACKI.EXE`
3. Uruchom przez Homebrew Launcher

## Wymagania Hardware
- **New Nintendo 3DS** lub **New 2DS XL** (zalecane)
- CFW (Luma3DS lub podobny)
- Karta SD (min 100MB wolne)

Standardowy 3DS (bez "New") może mieć problemy z wydajnością.

## Biblioteki 3DS
Port używa:
- **libctru** - Core 3DS system API
- **citro3d** - GPU/3D rendering
- **citro2d** - 2D sprites/textures
- **ndsp** - Audio (stub implementation)

## Zoom Levels
Kontrolowane przyciskiem **X**:
```
Level 0: 100% (320x240 game pixels on 320x240 screen)
Level 1:  50% (160x120 game pixels, 2x magnified)
Level 2:  25% ( 80x60  game pixels, 4x magnified)
Level 3: 12.5% ( 40x30  game pixels, 8x magnified)
```

Zoom zawsze centrowany wokół kursora (`g_mouse_x`, `g_mouse_y`).

## Znane Ograniczenia
- Brak stereoskopowego 3D
- Audio jest stub (ndsp init/exit tylko)
- Brak software keyboard dla text input
- Texture format zawsze RGBA8 (nie optymalizowany dla palletized content)

## Następne Kroki (Opcjonalne Ulepszenia)
1. **Audio**: Pełna implementacja ndsp dla music/sfx
2. **Software Keyboard**: SwkbdState dla text input events
3. **Optymalizacja tekstur**: Użyć GPU_LA8 dla paletted content zamiast RGBA8
4. **3D Stereoscopic**: Render two slightly offset views to GFX_LEFT/GFX_RIGHT
5. **Bottom screen UI**: Custom inventory/verb selector zamiast/oprócz zoom

## Pliki Zmodyfikowane (poza src/platform/3ds/)
```
mk/3ds.mk                 - Dodano SDL_compat.c, zmieniono ENGINE_SRCS
Makefile                  - Już wspierał TARGET=3ds (T138)
```

## Testowanie
Port **NIE BYŁ JESZCZE TESTOWANY** na prawdziwym hardware ani emulatorze (Citra).

Konieczne przed release:
1. Kompilacja (test czy się linkuje)
2. Uruchomienie w Citra emulator
3. Test na prawdziwym New 3DS hardware
4. Weryfikacja wszystkich kontrolek (A/B swap, zoom, hand modes)
5. Test touch screen mapping
6. Sprawdzenie wydajności
7. Weryfikacja zapisów (czy sync() działa)

## Credits
- **Port**: Mateusz Szuła (2026)
- **Bazowane na**: Switch port + SDL platform layer
- **Engine**: wacki game engine

## Licencja
GPL-3.0-or-later (zgodnie z resztą projektu wacki)
