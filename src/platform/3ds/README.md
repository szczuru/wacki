# Nintendo 3DS Port

Port gry Wacki dla New Nintendo 3DS z wykorzystaniem dual-screen i custom controls.

## Wymagania

- **New Nintendo 3DS** lub **New Nintendo 2DS XL** (zalecane ze względu na wydajność)
- Homebrew Launcher (poprzez Luma3DS lub inny CFW)
- Karta SD z minimum 100MB wolnego miejsca

## Instalacja

1. Skopiuj `wacki.3dsx` do `/3ds/wacki/` na karcie SD
2. Skopiuj plik `WACKI.EXE` do `/3ds/wacki/data/WACKI.EXE`
3. Uruchom grę przez Homebrew Launcher

## Konfiguracja ekranów

### Górny ekran (400x240)
- Wyświetla główną grę przeskalowaną z 640x480
- Pełna wizualizacja sceny

### Dolny ekran (320x240)
- Zoom wokół kursora (regulowany przyciskiem X)
- Obsługa ekranu dotykowego do poruszania kursorem
- Wizualny crosshair pokazujący pozycję kursora

## Sterowanie

### Przyciski podstawowe
- **A (prawy)** - Lewy przycisk myszy (interakcja)
- **B (dolny)** - Prawy przycisk myszy (przełączanie aktora)
- **X (górny)** - Zmiana poziomu zoomu (100% → 50% → 25% → 12.5% → 100%)
- **START** - Menu pauzy
- **SELECT** - Przełączanie trybu prawo/lewo ręcznego

### Circle Pad i D-Pad
- **Circle Pad** - Płynne poruszanie kursorem
- **D-Pad** - Dyskretne poruszanie kursorem

### Triggery (zmienne w zależności od trybu)

#### Tryb PRAWORĘCZNY (domyślny):
- **L** - Quick load
- **ZL** - Lewy przycisk myszy (alternatywny)
- **R** - Quick save
- **ZR** - Prawy przycisk myszy (alternatywny)

#### Tryb LEWORĘCZNY (przełącz SELECT):
- **L** - Lewy przycisk myszy
- **ZL** - Prawy przycisk myszy
- **R** - Quick save
- **ZR** - Quick load

### Ekran dotykowy
- Dotknij ekranu dolnego aby przenieść kursor w dane miejsce
- Działa w połączeniu z zoomem - dotknięcie przeskalowane do aktualnego powiększenia

## Poziomy zoomu

Przycisk **X** przełącza między 4 poziomami zoomu na dolnym ekranie:

1. **100%** - Widok 1:1 (320x240 pikseli gry)
2. **50%** - Widok 160x120 pikseli gry (2x powiększenie)
3. **25%** - Widok 80x60 pikseli gry (4x powiększenie)
4. **12.5%** - Widok 40x30 pikseli gry (8x powiększenie)

## Zapisywanie

Zapisane gry trafiają do `/3ds/wacki/wacki.sav` na karcie SD.

## Wydajność

Port został zoptymalizowany dla New Nintendo 3DS:
- Tryb `-Os` (optymalizacja rozmiaru + szybkości)
- Wykorzystanie sprzętowego przyspieszenia citro3d/citro2d
- Skalowanie nearest-neighbor dla maksymalnej wydajności

**Uwaga:** Standardowy Nintendo 3DS (bez "New") może mieć problemy z wydajnością.

## Budowanie

```bash
# Za pomocą Docker
./tools/build-3ds.sh

# Ręcznie (wymaga devkitARM)
make TARGET=3ds
```

Wynikowy plik: `dist/wacki.3dsx`

## Troubleshooting

### Gra nie uruchamia się
- Upewnij się, że masz CFW (Luma3DS)
- Sprawdź czy plik WACKI.EXE jest w `/3ds/wacki/data/`

### Niska wydajność
- Używaj **New** Nintendo 3DS/2DS XL
- Zamknij inne aplikacje w tle
- Spróbuj obniżyć poziom zoomu (zoom 100% jest najszybszy)

### Ekran dotykowy nie działa
- Sprawdź czy dotykasz dolny ekran (nie górny)
- Kalibracja ekranu dotykowego w systemowych ustawieniach 3DS

## Znane ograniczenia

- Brak obsługi 3D (stereoskopowego efektu głębi)
- Podstawowa implementacja audio (wymaga rozwinięcia)
- Brak klawiatury software'owej do wprowadzania tekstu

## Credits

Port stworzony na podstawie istniejących portów Switch i SDL.
