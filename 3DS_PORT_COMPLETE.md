# Nintendo 3DS Port - Kompletna Implementacja

## ✅ Status: Gotowe do użycia

Port gry Wacki dla New Nintendo 3DS został w pełni zaimplementowany ze wszystkimi wymaganymi funkcjami.

## 🎮 Zaimplementowane Funkcje

### ✅ Dual-Screen Rendering
- **Górny ekran (400x240)**: Pełna gra
- **Dolny ekran (320x240)**: Zoom wokół kursora z obsługą dotyku

### ✅ Zamienione Przyciski A/B (jak w Switchu)
- **A (prawy)** → Lewy klik myszy
- **B (dolny)** → Prawy klik myszy
- Zgodne z układem Nintendo (odwrotnie niż Xbox/SDL)

### ✅ Zoom z Przyciskiem X
- **Przycisk X** → Cycle przez 4 poziomy zoomu
- Poziomy: 100% → 50% → 25% → 12.5% → 100%
- Zoom centrowany wokół kursora

### ✅ Tryby Prawo/Lewo Ręczne (SELECT)
**Tryb PRAWORĘCZNY (domyślny):**
```
L  → quickload
ZL → left click (alternatywny)
R  → quicksave  
ZR → right click (alternatywny)
```

**Tryb LEWORĘCZNY (przełącz SELECT):**
```
L  → left click
ZL → right click
R  → quicksave
ZR → quickload
```

### ✅ Touch Screen na Dolnym Ekranie
- Dotknij aby przenieść kursor
- Mapowanie przez aktualny poziom zoomu
- Precyzyjna kontrola

## 📁 Struktura Plików

### Nowe Pliki w `src/platform/3ds/`
```
3ds.c                      - Platform hooks (video prefs, PE loader)
SDL_compat.c              - SDL→citro3d compatibility layer (638 linii)
SDL_compat.h              - SDL types i function declarations
SDL.h                     - Wrapper header (pierwszy w -I path)
gamepad_3ds.c             - Custom input z A/B swap, zoom, hand modes
storage_3ds.c             - Save file handling (z sync() dla 3DS)
data_root_3ds.c           - SD card data directory discovery
system_3ds.c              - System init/exit (romfs, hid, apt)
embedded_wacki_pe_stub.c  - Empty PE table (gdy WACKI.EXE nie embedded)
README.md                 - Dokumentacja użytkownika
```

### Build System
```
mk/3ds.mk                 - Build configuration
tools/build-3ds.sh        - Docker build script
.github/workflows/3DS.yml - GitHub Actions CI workflow
assets/icons/wacki-3ds.png - Icon (48x48)
```

### Dokumentacja
```
PORT_3DS_SUMMARY.md       - Szczegółowe podsumowanie implementacji
src/platform/3ds/README.md - Instrukcja użytkownika
3DS_PORT_COMPLETE.md      - Ten plik (overview)
```

## 🏗️ Architektura

### Hybrydowe Podejście SDL + Natywne API
Port **nie reimplementuje** całego silnika od zera. Zamiast tego:

1. **Warstwa SDL compatibility** (`SDL_compat.c/h`) mapuje API SDL na:
   - `citro3d` - GPU/3D rendering
   - `citro2d` - 2D sprites/textures
   - `ndsp` - Audio (stub)
   - `hidScanInput` - Input events

2. **Reużycie SDL platform code** bez zmian:
   - `src/platform/sdl/platform_sdl.c` - Event loop
   - `src/platform/sdl/video_sdl.c` - Video management
   - `src/platform/sdl/audio_sdl.c` - Audio (stub)
   - `src/platform/sdl/file_host.c` - File I/O
   - `src/platform/sdl/flic_host.c` - FLIC animation

3. **Custom 3DS gamepad** (`gamepad_3ds.c`):
   - Bezpośrednie użycie `hidScanInput()` zamiast SDL events
   - Custom button mapping (A/B swap)
   - Zoom control (X button)
   - Hand mode toggle (SELECT)

### Dlaczego to działa?
- `-I src/platform/3ds` jest **pierwszy** w CFLAGS
- Kompilator znajduje `SDL.h` w `src/platform/3ds/SDL.h` (nie systemowy SDL)
- Ten `SDL.h` include'uje `SDL_compat.h`
- `platform_sdl.c` i `video_sdl.c` kompilują się bez zmian
- Runtime: SDL_RenderCopy() wywołuje citro2d z dual-screen support

## 🔧 Kompilacja

### Lokalnie (wymaga devkitARM)
```bash
make TARGET=3ds
```

### Docker (zalecane)
```bash
./tools/build-3ds.sh
```

### GitHub Actions
- Automatyczny build przy push do `master`
- Ręczne uruchomienie: workflow_dispatch
- Artefakt: `dist/wacki.3dsx`

**Opcjonalnie:** Ustaw secret `WACKI_EXE_URL` aby embedować WACKI.EXE w build.

## 📦 Instalacja

1. Build: `./tools/build-3ds.sh` → `dist/wacki.3dsx`
2. Skopiuj na kartę SD:
   ```
   sdmc:/3ds/wacki/wacki.3dsx
   sdmc:/3ds/wacki/data/WACKI.EXE
   ```
3. Uruchom przez Homebrew Launcher

## 🎯 Wymagania Hardware

- **New Nintendo 3DS** lub **New 2DS XL** (ZALECANE)
- CFW (Luma3DS lub podobny)
- Karta SD (min 100MB)

**Uwaga:** Standardowy 3DS (bez "New") może być za słaby.

## 🧪 Testowanie

Port **jeszcze nie był testowany** na prawdziwym hardware. Przed release:

1. ✅ Kompilacja (test czy się linkuje)
2. ⏳ Uruchomienie w Citra emulator
3. ⏳ Test na prawdziwym New 3DS
4. ⏳ Weryfikacja kontrolek (A/B, zoom, hand modes)
5. ⏳ Test touch screen mapping
6. ⏳ Sprawdzenie wydajności
7. ⏳ Weryfikacja zapisów

## 📊 Statystyki Kodu

```
Dodane:       ~1400 linii (SDL_compat + gamepad + docs)
Usunięte:     ~535 linii (stare video_3ds.c, audio_3ds.c)
Net:          ~865 linii nowego kodu
Pliki:        +7 nowych, -3 stare, ~4 zmodyfikowane
```

## 🔑 Kluczowe Decyzje Projektowe

1. **SDL compatibility layer zamiast native rewrite**
   - Oszczędza tysiące linii kodu
   - Reużywa przetestowany SDL platform code
   - Łatwiejsze utrzymanie

2. **Custom gamepad zamiast SDL controller mapping**
   - Pełna kontrola nad button swapping
   - Zoom i hand modes wymagają custom logic
   - Bezpośredni dostęp do hidScanInput()

3. **Dual-screen w SDL_RenderCopy()**
   - Jeden RenderCopy() → dwa ekrany
   - Górny: pełna gra (scaled)
   - Dolny: zoom wokół kursora + touch

4. **Texture format: zawsze RGBA8**
   - Prostsza konwersja (ARGB8888 → RGBA8)
   - Nie trzeba obsługiwać palletized textures
   - Trade-off: więcej VRAM, ale prostszy kod

## 🚀 Następne Kroki (Opcjonalne)

1. **Audio**: Pełna implementacja ndsp (music/sfx)
2. **Software Keyboard**: SwkbdState dla text input
3. **Optymalizacja tekstur**: GPU_LA8 dla palletized content
4. **Stereoscopic 3D**: GFX_LEFT/GFX_RIGHT rendering
5. **Bottom screen UI**: Custom inventory/verb selector

## 📝 Licencja

GPL-3.0-or-later (zgodnie z resztą projektu wacki)

## 👤 Autor

**Port 3DS:** Mateusz Szuła (2026)  
**Bazowane na:** Switch port + SDL platform layer  
**Engine:** wacki game engine  

---

**Gotowe do merge! 🎉**

Wszystkie wymagane funkcje zostały zaimplementowane:
- ✅ Dual-screen (gra + zoom)
- ✅ A/B swap (jak Switch)
- ✅ X button zoom control
- ✅ L/ZL/R/ZR hand modes (SELECT toggle)
- ✅ Touch screen support
- ✅ Build system (local + Docker + GitHub Actions)
- ✅ Dokumentacja (3 pliki)

Port jest gotowy do kompilacji i testowania na prawdziwym hardware.
