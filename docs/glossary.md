# Glosariusz — terminy z silnika i scenariuszy

Skrypty Wackich (`Wacky.scr`, `Item.scr`, `Gadki.scr`) używają polskich
nazw dla wszystkiego. Engine internalnie używa transliteracji bez
ogonków albo skróconych form. Ta lista mapuje to co spotkasz w
plikach na zwykłą terminologię.

## Hierarchia gry

| Polski (skrypt) | Angielski (kod) | Co to |
|---|---|---|
| `etap` | stage / chapter | Rozdział gry — 5 etapów (1..5) |
| `komnata` | room / scene | Pojedyncze pomieszczenie/lokacja — kilkanaście per etap |
| `init` | per-stage default | Specjalna „komnata" w etapie — domyślne [animacja]/[sampl] dla wszystkich pomieszczeń |
| `enter_script` | scene entry script | Bytecode wykonywany przy wejściu do komnaty |
| `secondary script` | secondary entry | Drugi script, runned dla "wraca z" zamiast "wchodzi do" |
| `[etap]` `[komnata]` `[animacja]` `[sampl]` `[rozmowa]` | section tags | Hierarchiczne tagi w `Wacky.scr` |

## Aktorzy i NPC

| Polski | Angielski | Co to |
|---|---|---|
| `Ebek` | — | Główny bohater 1 (atlas `ebek.wyc`) |
| `Fjej` | — | Główny bohater 2 (atlas `fjej.wyc`) |
| `aktywny aktor` | active actor | `g_active_actor` — 0 (Ebek) lub 1 (Fjej), `SPACE` przełącza |
| `partner` | partner actor | Ten drugi |
| `przedm` | item / przedmioty | Asset z inventory items (atlas `przedm.wyc`) |
| `pasek` | bar — życie | Health bar — `pasek#N.wyc` per stage |
| `panel` | verb panel | Pasek z czasownikami na dole (zobacz, weź, użyj...) |

## Akcje / verby (z `Item.scr`)

Skrypty referują verby przez liczbowe ID. Najczęściej spotykane:

| Verb ID | Polski | Angielski |
|---|---|---|
| 0x01 | Zobacz | Look |
| 0x02 | Weź | Take |
| 0x03 | Użyj | Use |
| 0x04 | Otwórz | Open |
| 0x05 | Zamknij | Close |
| 0x06 | Daj | Give |
| 0x07 | Mów | Talk |
| 0x08 | Pchnij | Push |
| 0x09 | Ciągnij | Pull |
| 0x26 | (placeholder) | "Empty" verb — neutral pair, used for enter scripts |

`g_held_item` przechowuje aktualnie używany verb ID (lub ID itemu z
inventory gdy ten jest „w ręce").

## Assety (rozszerzenia plików)

| Ext | Co to | Magic |
|---|---|---|
| `.wyc` | ANIM atlas — sprite sheet z animacją | `ANIM` (`0x4D494E41`) |
| `.msk` | MASK — clickable region / walk-behind | `MASK` (`0x4B53414D`) |
| `.fld` | FILD — walkability bitmap dla sceny | `FILD` (`0x444C4946`) |
| `.pic` | PIC — tło sceny, RAW 8-bit paletted | (no magic — raw RAWB) |
| `.pal` | PAL — paleta 256 × RGB triple | (no magic) |
| `.scr` | Wacky-skrypt (text + bytecode binary) | tekstowy z osadzonym bytecode |
| `.wav` | Standard RIFF WAVE | `RIFF/WAVE` |
| `.dta` | Container archive (multi-asset) | `BASE/SPIS` |

Pełna spec: [asset-format.md](asset-format.md).

## Skrypty z dysku

| Plik | Co zawiera |
|---|---|
| `Wacky.scr` | Główny scenariusz — [etap]/[komnata]/[animacja]/[sampl] hierarchia + bytecode enter_script'ów |
| `Item.scr` | Inventory items + verb-action tables — co się dzieje gdy klikasz „Użyj" itemu na obiekcie |
| `Gadki.scr` | Dialogi — [rozmowa] sekcje z liniami audio + opcjami |
| `Item_*.scr` | Wariant Item.scr per etap (czasem) |

## VM jargon

| Termin | Co znaczy |
|---|---|
| `główna VM` / `main VM` | `RunScriptInterpreter` w `src/vm/main.c` — 78 opcode'ów dla scenariusza |
| `per-entity VM` | `RunVM` w `src/actor/vm.c` — 33 opcode'y dla per-actor animacji |
| `opcode` | Pojedyncza instrukcja bytecode'u — `[op:u8][len:u8][operands]` |
| `dispatcher` | Główna pętla switch w VM |
| `script_vars` | `g_script_vars[]` — globalna tablica 297 int32 vars |
| `return_reg` | `g_script_vars[4]` — rejestr powrotu (used by RAND, MOUSE_X/Y, etc.) |
| `bytecode VA` | Original 0x004xxxxx address — resolved through PE loader slice table |

## Tagi w `[sampl]` tuplach

```
[sampl] wav1.wav wav2.wav (N,) (N,M) (,M)
```

| Kształt | Znaczenie |
|---|---|
| `(N,)` | Play trigger przy klatce N — no loop end |
| `(N,M)` | Play przy N + loop end przy M |
| `(,M)` | Stop-only — przy klatce M zatrzymaj cokolwiek z tego [sampl] |

Pełen opis: [audio-pipeline.md](audio-pipeline.md).

## Save / load

| Termin | Co to |
|---|---|
| `Wacki.sav` | Plik save'a, format `WackiSaveFile` w `src/save.c` |
| `slot` | Jeden z 10 slot'ów — `slots[0]` to quicksave (F5/F9), reszta normal |
| `stage_indicator` | ID komnaty w slocie (0 = empty) |
| `world_default_snapshot` | Backup stanu świata sprzed pierwszego launch'a — base do restartu |

## Często mylone

- **`komnata` vs `scena`** — używane wymiennie. Engine kod używa
  „scene", scripty używają „komnata".
- **`etap` vs `stage`** — to samo. Script używa polskiego.
- **`atlas` vs `asset`** — atlas konkretnie znaczy `.wyc` (sprite sheet),
  asset to ogólnie każdy ładowany plik (atlas, mask, fild itd.).
- **`drawn_x/y` vs `anchor_x/y`** — anchor to **logiczna** pozycja entity
  (gdzie skrypt ją trzyma), drawn to **rendered** pozycja po
  przejściu przez per-frame VM. Mogą się różnić jeśli VM oscyluje
  (`X_OSCILLATE`/`Y_OSCILLATE` opcode'y).
- **`bytecode` vs `script`** — script to plik tekstowy z osadzonym
  bytecodem; bytecode to binarne instrukcje VM'a. Skrypt **zawiera**
  bytecode (plus parametry/teksty).
