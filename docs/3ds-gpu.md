# 3DS: rysowanie przez GPU (citro3d)

Gałąź `3ds-gpu`. Nowy backend obrazu `src/platform/3ds/video_3ds_c3d.c`
zamiast `video_3ds_gl.c` (CPU, najbliższy sąsiad, zapis prosto do
framebuffera). Warstwa GPU oparta na `n3ds_gpu.cpp` z portu ReClaw
(Herbaciarz, GPL-3.0), przepisana do C i uproszczona: Wacki ma jedną
klatkę 640×480 8-bit z paletą, więc nie trzeba przechwytywać wywołań SDL.

## Jak to działa

1. CPU zamienia klatkę 8-bit na RGB565 przez tablicę 256 kolorów
   (4 piksele naraz, odczyt i zapis po kolei) do bufora w pamięci linear
   o szerokości wiersza 1024.
2. `GX_DisplayTransfer` (DMA GPU) kafelkuje go do tekstury 1024×512
   RGB565 w VRAM. CPU nie robi kafelkowania Mortona (to spowalniało
   picaGL).
3. Jeden prostokąt z teksturą na ekran:
   - górny: 4:3 — 320×240 na środku z czarnymi pasami (dokładnie 2:1, więc
     wygładzanie = czysta średnia 2×2), albo stretch — całe 400×240;
   - dolny: wycinek tej samej tekstury (zoom 1×/2×/4× wokół kursora albo
     cała klatka), bez drugiej kopii.
   - stereo 3D (suwak otwarty): druga tekstura z obrazem prawego oka
     (pierwszy plan przesunięty, jak wcześniej).
4. Klatka bez zmian (obraz, paleta, kursor, zoom, suwak) jest pomijana.

`C3D_FrameBegin(0)` czeka tylko na koniec poprzedniej klatki GPU, nie na
odświeżenie ekranu — silnik sam trzyma 30 FPS (`EnginePaceFrame`, 33 ms).
Zamiana buforów jest i tak przy vblank (bez rwania obrazu).

## Wygładzanie

Liniowe przy pomniejszaniu (górny ekran, dolny w widoku całej klatki).
Zoom 2×/4× na dolnym ekranie zostaje ostry (najbliższy sąsiad), żeby
powiększona grafika nie była rozmyta (`-DWACKI_C3D_SMOOTH_MAG=1` włącza
wygładzanie i tam).

Włączone domyślnie. Przełącza je pierwszy przycisk ekranu **Grafika**
(`s_opt_gfx1` / `video_mode`, „lepsza grafika”) — ten sam, który włącza
efekt oświetlenia; zmiana działa od razu i zapisuje się z opcjami.

## Log wydajności

`sdmc:/3ds/wacki/wacki_perf.log`, nadpisywany przy każdym uruchomieniu.
Zapisuje go osobny wątek o niższym priorytecie (na New 3DS na rdzeniu 2,
jeśli system pozwoli) co 0,5 s — zapis na kartę nie trafia w klatkę.

Linia co 0,5 s (średnia/maksimum w ms):

```
t=12.5s fps=30.0 ticks=30.0 frame=33.3/41.2 engine=9.1/14.0 sleep=21.0 draw=2.3/3.1 wait=0.4/1.2 gpu=1.1 skip=0 lin=20480K vram=3072K [gpu smooth=1 aspect=4:3 zoom=1x]
```

| pole | znaczenie |
|---|---|
| `fps` | narysowane klatki na sekundę (bez pominiętych) |
| `ticks` | wywołania `plat_video_present` na sekundę |
| `frame` | czas od jednej klatki do następnej (cała pętla gry) |
| `engine` | `frame` − `draw` − `wait` − `sleep`: praca silnika |
| `sleep` | celowe czekanie silnika (pacing do 30 FPS) na klatkę |
| `draw` | CPU w rysowaniu (paleta → RGB565, komendy GPU) |
| `wait` | czekanie na poprzednią klatkę GPU |
| `gpu` | czas GPU ostatniej klatki (citro3d) |
| `skip` | klatki pominięte (bez zmian) |
| `lin` / `vram` | wolna pamięć linear / VRAM w KB |

Linie z `!` to ostrzeżenia i błędy (`LOG_WARN`/`LOG_ERROR` silnika, błędy
GPU, zmiany wygładzania). Stary backend (`VIDEO_3DS=fb`) pisze ten sam log
(`draw` = cały czas rysowania CPU, `wait`/`gpu` = 0) — do porównania.

## Budowanie

```
tools/build-3ds.sh                  # dist/wacki.3dsx (GPU)
tools/build-3ds.sh VIDEO_3DS=fb     # stary backend CPU, do porównania
tools/build-3ds-cia.sh              # dist/wacki.cia z dist/wacki
```

`build-3ds.sh` używa lokalnego devkitPro (`DEVKITPRO`, domyślnie
`/opt/devkitpro`, z devkitARM + libctru + citro3d), gdy nie ma Dockera
(albo zawsze przy `WACKI_3DS_LOCAL=1`); inaczej obrazu
`devkitpro/devkitarm`. picaGL nie jest już potrzebny.

## Do przetestowania (na sprzęcie)

- [x] Obraz nie jest odwrócony do góry nogami — był przy
      `GX_TRANSFER_FLIP_VERT(1)`; teraz domyślnie `WACKI_C3D_TEX_FLIP=0`
      (sprawdzić ponownie: obraz prosto, bez czarnego paska u góry).
- [ ] 4:3 i stretch (przełączanie w grze), czarne pasy w 4:3.
- [ ] Dolny ekran: zoom 1×/2×/4×/cała klatka, dotyk trafia w kliknięte
      miejsce na każdym poziomie, przytrzymanie palca nie „ucieka”.
- [ ] Wygładzanie: domyślnie włączone; pierwszy przycisk Grafiki je
      wyłącza/włącza od razu; ustawienie zostaje po restarcie.
- [ ] Stereo 3D (poprawione po teście): tło jest ZA ekranem, postacie,
      panel, kursor i napisy na powierzchni ekranu; mocniej niż wcześniej
      (18 px klatki przy pełnym suwaku ≈ 9 px na ekranie w 4:3 — do
      zmiany: `-DSTEREO_MAX_DISPARITY_SRC_PX=…`); menu otwarte w grze
      płaskie; bez kropek/dziur w postaciach.
- [ ] Filmy (AVI/FLIC): płynne, dźwięk w synchronizacji; dolny ekran stoi.
- [ ] `wacki_perf.log` powstaje, linie co 0,5 s, ostrzeżenia z `!`;
      porównać FPS/`engine`/`draw` z buildem `VIDEO_3DS=fb`.
- [ ] Wyjście z gry (HOME → zamknij): bez zawieszenia (wątek logu się
      kończy).
- [ ] .cia instaluje się i działa jak .3dsx.
