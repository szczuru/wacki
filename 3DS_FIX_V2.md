# Nintendo 3DS Port - Fix V2: Screen Rotation + Debug

## 🎯 Główny Fix: Screen Rotation

### Problem
Ekrany 3DS były obrócone o 90° w prawo pomimo że citro2d ma obsługiwać rotację.

### Rozwiązanie
Znalazłem w dokumentacji citro2d funkcję `C2D_SceneSize(width, height, **tilt**)`:

```c
void C2D_SceneSize(u32 width, u32 height, bool tilt)
```

**Parametr `tilt`**: "Whether the scene is tilted like the 3DS's sideways screens"

To jest dokładnie to czego potrzebujemy! 3DS screens są fizycznie obrócone (portrait w hardware, landscape w użytkowaniu).

### Zmiana w SDL_Compat.c

```c
/* PRZED - używaliśmy tylko C2D_SceneBegin */
C2D_SceneBegin(s_top_screen);
C2D_DrawImageAt(...);

/* PO - dodajemy C2D_SceneSize z tilt=true */
C2D_SceneBegin(s_top_screen);
C2D_SceneSize(400, 240, true);  /* tilt=true for 3DS rotated screens */
C2D_DrawImageAt(...);
```

Teraz citro2d wie że ekran jest fizycznie obrócony i obsługuje to automatycznie!

## 🐛 Debug: Przyciski

Dodałem tymczasowe logi (pierwsze 10 kliknięć) aby sprawdzić czy:
1. Przycisk A faktycznie ustawia `g_lmb_clicked` 
2. `HandleSceneInput()` widzi i przetwarza `g_lmb_clicked`

### Logi w gamepad_3ds.c
```c
static int a_press_count = 0;
if ((kDown & KEY_A) && a_press_count < 10) {
    a_press_count++;
    LOG_INFO("input-debug", "A button press #%d - setting g_lmb_clicked=1", a_press_count);
}
```

### Logi w scene_input.c
```c
static int click_handled_count = 0;
if (g_lmb_clicked && click_handled_count < 10) {
    click_handled_count++;
    LOG_INFO("scene-input", "HandleSceneInput #%d: g_lmb_clicked=1 detected", click_handled_count);
}
```

Te logi pomogą zdiagnozować:
- Czy przycisk A jest wykrywany?
- Czy engine widzi `g_lmb_clicked`?
- Czy jest jakieś opóźnienie między naciśnięciem a przetworzeniem?

## 📝 Pliki Zmodyfikowane

1. **src/platform/3ds/SDL_compat.c**
   - Dodano `C2D_SceneSize(width, height, true)` dla obu ekranów
   - Parametr `tilt=true` informuje citro2d o fizycznej rotacji ekranów

2. **src/platform/3ds/gamepad_3ds.c**
   - Dodano debug log pierwszych 10 naciśnięć przycisku A
   - Dodano logi dla SELECT i X (zoom/hand mode)

3. **src/scene/scene_input.c**
   - Dodano debug log pierwszych 10 kliknięć przetwarzanych przez silnik

## 🧪 Testowanie

### Sprawdź po skompilowaniu:

1. **Rotacja Ekranu:**
   - ✅ Czy obraz jest prawidłowo zorientowany? (NIE obrócony 90°)
   - ✅ Czy gra wypełnia górny ekran (320x240 z czarnymi pasami)?
   - ✅ Czy dolny ekran pokazuje zoom?

2. **Przyciski:**
   - ✅ Naciśnij A podczas intro - czy pomija?
   - ✅ Sprawdź log: `sdmc:/3ds/wacki/wacki.log`
   - ✅ Poszukaj linii: `[info/input-debug] A button press #1`
   - ✅ Poszukaj linii: `[info/scene-input] HandleSceneInput #1: g_lmb_clicked=1`

3. **Inne Przyciski:**
   - ✅ X - czy zmienia zoom? (powinien być log w pliku)
   - ✅ SELECT - czy zmienia tryb lewo/prawo? (powinien być log)
   - ✅ D-Pad i Circle Pad - czy poruszają kursorem?

### Interpretacja Logów

**Scenariusz 1: Oba logi się pojawiają**
```
[info/input-debug] A button press #1 - setting g_lmb_clicked=1
[info/scene-input] HandleSceneInput #1: g_lmb_clicked=1 detected
```
✅ **Przyciski działają poprawnie!** Jeśli intro nadal się nie pomija, problem jest gdzie indziej.

**Scenariusz 2: Tylko log input-debug**
```
[info/input-debug] A button press #1 - setting g_lmb_clicked=1
```
❌ **Engine nie widzi klika.** `HandleSceneInput()` nie jest wywoływane lub `g_lmb_clicked` jest zerowane przed wywołaniem.

**Scenariusz 3: Brak logów**
```
(brak logów z input-debug ani scene-input)
```
❌ **Przycisk A nie jest wykrywany** lub `hidScanInput()` nie działa poprawnie.

**Scenariusz 4: Logi są ale intro nie pomija**
Jeśli oba logi się pojawiają ale intro nadal gra, może to oznaczać:
- Intro jest w specjalnym trybie gdzie pomijanie jest wyłączone
- Potrzebne jest konkretne event (np. SDL_MOUSEBUTTONDOWN)
- Timing problem - klik jest konsumowany zanim dotrze do właściwego miejsca

## 🔧 Następne Kroki (Po Testach)

### Jeśli rotacja działa:
✅ Usuń komentarze o rotacji z kodu

### Jeśli przyciski nie działają:
1. Sprawdź logi aby zdiagnozować gdzie jest problem
2. Może potrzebujemy generować SDL events? (jak w SDL platformie)
3. Może `plat_input_flush()` jest wywoływane w złym momencie?

### Jeśli audio nadal nie działa:
1. Dodamy logi do `SDL_OpenAudio` i `update_audio_buffers`
2. Sprawdzimy czy callback jest wywoływany
3. Może potrzebujemy innych ustawień ndsp?

## 📚 Źródła

- [citro2d Documentation - Basic Functions](https://citro2d.devkitpro.org/group__Base.html)
- `C2D_SceneSize()` - parametr `tilt` dla ekranów 3DS
- [citro2d GitHub](https://github.com/devkitPro/citro2d)

## ✅ Oczekiwane Rezultaty

Po tej zmianie powinieneś zobaczyć:
1. **Prawidłową rotację** - obraz NIE obrócony 90°
2. **Logi w pliku** - potwierdzające działanie przycisków
3. **Lepszą diagnozę** - dokładnie gdzie jest problem z przyciskami

---

**Data:** 2026-07-01
**Branch:** feature/3ds-port-v2
**Commit:** (pending)
