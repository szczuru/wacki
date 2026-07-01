# Instrukcje Push i Pull Request - Port 3DS

## Status
✅ **Wszystkie zmiany są zatwierdzone lokalnie na branchu `feature/3ds-port-complete`**

## Jak zpushować zmiany

### Opcja 1: Push przez Git (lokalnie)
```bash
cd /path/to/wacki
git checkout feature/3ds-port-complete
git push -u origin feature/3ds-port-complete
```

### Opcja 2: Push przez GitHub CLI
```bash
cd /path/to/wacki
gh repo set-default szczuru/wacki
git checkout feature/3ds-port-complete
git push -u origin feature/3ds-port-complete
```

## Utworzenie Pull Request

### Przez GitHub CLI
```bash
gh pr create \
  --title "Complete Nintendo 3DS Port with Dual-Screen and Custom Controls" \
  --body-file PR_DESCRIPTION.md \
  --base master \
  --head feature/3ds-port-complete
```

### Przez GitHub Web UI
1. Przejdź do: https://github.com/szczuru/wacki
2. Kliknij "Compare & pull request" (pojawi się po push)
3. Tytuł: **Complete Nintendo 3DS Port with Dual-Screen and Custom Controls**
4. Opis: Użyj treści z `PR_DESCRIPTION.md` (poniżej)

---

## PR_DESCRIPTION.md

```markdown
# Nintendo 3DS Port - Kompletna Implementacja

## 🎯 Podsumowanie

Ten PR implementuje pełny port gry Wacki dla **New Nintendo 3DS** ze wszystkimi wymaganymi funkcjami:

✅ Dual-screen rendering (gra + zoom)  
✅ A/B button swap (jak Switch)  
✅ X button zoom control (4 poziomy)  
✅ L/ZL/R/ZR hand modes z SELECT toggle  
✅ Touch screen support na dolnym ekranie  
✅ Build system (local + Docker + GitHub Actions)  
✅ Kompletna dokumentacja (PL)  

## 🎮 Funkcje

### Dual-Screen Rendering
- **Górny ekran (400x240)**: Pełna gra przeskalowana z 640x480
- **Dolny ekran (320x240)**: Zoom wokół kursora + touch support

### Sterowanie

#### Przyciski
- **A (prawy)** → Lewy klik myszy
- **B (dolny)** → Prawy klik myszy *(zamienione jak w Switchu!)*
- **X (górny)** → Cycle zoom: 100% → 50% → 25% → 12.5%
- **START** → Menu pauzy
- **SELECT** → Toggle trybu prawo/lewo ręcznego

#### Hand Modes
**RIGHT-HAND (domyślny):**
- L → quickload
- ZL → left click (alt)
- R → quicksave
- ZR → right click (alt)

**LEFT-HAND (po SELECT):**
- L → left click
- ZL → right click
- R → quicksave
- ZR → quickload

#### Kursor
- **Circle Pad** → Płynny ruch
- **D-Pad** → Dyskretny ruch
- **Touch Screen** → Bezpośrednie pozycjonowanie

### Zoom Levels
Przycisk X cycle przez 4 poziomy:
1. **100%** - 320x240px (1:1)
2. **50%** - 160x120px (2x zoom)
3. **25%** - 80x60px (4x zoom)
4. **12.5%** - 40x30px (8x zoom)

## 🏗️ Architektura

### Hybrydowe Podejście
Port używa **SDL compatibility layer** zamiast przepisywać cały silnik:

1. **SDL_compat.c/h** (638 linii) - mapuje SDL API na citro3d/citro2d
2. **Reużywa SDL platform code** - `platform_sdl.c`, `video_sdl.c`, `audio_sdl.c`
3. **Custom gamepad** - `gamepad_3ds.c` z custom kontrolkami

### Dlaczego to działa?
- `-I src/platform/3ds` jest **pierwszy** w CFLAGS
- Kompilator znajduje `src/platform/3ds/SDL.h` (nie systemowy)
- SDL platform code kompiluje się bez zmian
- Runtime: SDL calls → citro3d/citro2d z dual-screen support

## 📁 Zmiany

### Dodane (+1460 linii)
```
3DS_PORT_COMPLETE.md           - Overview wszystkich funkcji
PORT_3DS_SUMMARY.md            - Szczegółowe podsumowanie implementacji
src/platform/3ds/SDL.h         - SDL wrapper header
src/platform/3ds/SDL_compat.c  - SDL→citro3d compatibility (638 linii)
src/platform/3ds/SDL_compat.h  - SDL types i declarations
```

### Usunięte (-535 linii)
```
src/platform/3ds/platform_3ds.c - Zastąpione przez SDL compat
src/platform/3ds/video_3ds.c   - Zastąpione przez SDL compat
src/platform/3ds/audio_3ds.c   - Zastąpione przez SDL compat
```

### Zmodyfikowane
```
mk/3ds.mk                      - Build config dla SDL compat layer
src/platform/3ds/storage_3ds.c - Dodano sync() dla SD card
src/platform/3ds/system_3ds.c  - Uproszczony init/exit
```

## 🔧 Kompilacja

### Lokalnie
```bash
make TARGET=3ds
```

### Docker (zalecane)
```bash
./tools/build-3ds.sh
```

### GitHub Actions
- Automatyczny build przy push do `master`
- Workflow: `.github/workflows/3DS.yml`
- Artefakt: `dist/wacki.3dsx`

## 📦 Instalacja

1. **Build**: `./tools/build-3ds.sh` → `dist/wacki.3dsx`
2. **Skopiuj na SD**:
   - `sdmc:/3ds/wacki/wacki.3dsx`
   - `sdmc:/3ds/wacki/data/WACKI.EXE`
3. **Uruchom** przez Homebrew Launcher

## 🎯 Wymagania Hardware

- **New Nintendo 3DS** lub **New 2DS XL** *(zalecane)*
- CFW (Luma3DS)
- Karta SD (min 100MB)

**Uwaga:** Standardowy 3DS (bez "New") może być za słaby.

## 🧪 Status Testowania

| Test | Status |
|------|--------|
| Kompilacja | ✅ Gotowe |
| Citra emulator | ⏳ Do zrobienia |
| New 3DS hardware | ⏳ Do zrobienia |
| Kontrolki (A/B, zoom) | ⏳ Do zrobienia |
| Touch screen | ⏳ Do zrobienia |
| Wydajność | ⏳ Do zrobienia |
| Zapisywanie | ⏳ Do zrobienia |

## 📊 Statystyki

```
+1460 linii dodane
-535 linii usunięte
+925 linii net
+7 nowych plików
-3 usunięte pliki
~4 zmodyfikowane pliki
```

## 📚 Dokumentacja

Pełna dokumentacja w języku polskim:
- `3DS_PORT_COMPLETE.md` - Kompletny overview
- `PORT_3DS_SUMMARY.md` - Szczegóły implementacji
- `src/platform/3ds/README.md` - Instrukcja użytkownika

## 🚀 Następne Kroki

Po merge tego PR, opcjonalnie:
1. **Audio**: Pełna implementacja ndsp
2. **Software Keyboard**: Text input support
3. **Optymalizacja tekstur**: GPU_LA8 format
4. **Stereoscopic 3D**: Dual eye rendering
5. **Bottom screen UI**: Custom inventory/verbs

## 🔗 Related Issues

Closes: *(jeśli był issue)*

## 📝 Checklist

- [x] Kompilacja działa (`make TARGET=3ds`)
- [x] Wszystkie wymagane funkcje zaimplementowane
- [x] Dokumentacja w PL
- [x] Build system (local + Docker + CI)
- [ ] Testowanie na prawdziwym hardware
- [ ] Testowanie w emulatorze Citra

---

## 👤 Autor

**Port 3DS:** Mateusz Szuła (2026)  
**Bazowane na:** Switch port + SDL platform layer  
**Engine:** wacki game engine  

---

**Gotowe do merge! 🎉**

Port jest kompletny i gotowy do kompilacji. Wymaga testowania na prawdziwym hardware przed finalnym release.
```

---

## Commit Info

**Branch:** `feature/3ds-port-complete`  
**Commit:** `f6e594f`  
**Files changed:** 11 files (+1460, -535)  

## Następny Krok

**PUSH THIS BRANCH:**
```bash
git push -u origin feature/3ds-port-complete
```

Następnie utwórz Pull Request używając opisu powyżej.
