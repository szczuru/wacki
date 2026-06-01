# wacki-src — reconstructed source of WACKI.EXE (Wacki, 1997)

Portable C reconstruction of the **Wacki** point-and-click adventure
engine by Henryk Cygert (1997). Built from a full Ghidra reverse of the
original `WACKI.EXE` (302 592 B, PE32).

Each `.c` file carries the corresponding Ghidra address as a header
comment so any line can be cross-referenced against the original
decompilation. See `../WACKI_RE_REPORT.md` for the full RE write-up
and `docs/architecture.md` for the module-level map.

**Status (round 31, 2026-05-29):** stage 1 grywalne end-to-end —
intro AVI, menu, scene transitions, dialogue, save/load. PKv2 depack
byte-perfect on all 1782 archive entries. Audio mixer 8 channels.
Stages 2-5 not yet validated end-to-end (entry chain działa, ale
brak interactive playthrough).

## Build & run

The engine builds + tests + runs on macOS, Linux, and Windows
(MSYS2 / mingw-w64). Install SDL2 for your platform:

| Platform        | SDL2 install                                   |
|-----------------|------------------------------------------------|
| macOS (Homebrew)| `brew install sdl2`                            |
| Debian / Ubuntu | `sudo apt install libsdl2-dev`                 |
| Fedora          | `sudo dnf install SDL2-devel`                  |
| Arch            | `sudo pacman -S sdl2`                          |
| Windows (MSYS2) | `pacman -S mingw-w64-x86_64-{gcc,SDL2} make`   |

Then:

```
make all                   # builds the engine + extractor tools into ./dist/
mkdir -p data              # drop the .dta files + WACKI.EXE in:
cp /Volumes/WACKI_1/*.DTA data/
cp /Volumes/WACKI_1/WACKI.EXE data/   # build-time dep only — see note
./dist/wacki               # runs the engine (release; quiet by default)
```

> **Note on WACKI.EXE.** The runtime engine is self-contained — once
> built, the resulting `dist/wacki` binary doesn't need WACKI.EXE
> next to it. At build time, `tools/embed-pe-data` reads WACKI.EXE
> once and emits the original `.rdata + .data` sections as a const
> slice table baked straight into the binary. The PE loader resolves
> every original-VA reference (verb tables, scripts, asset name
> strings, komnata tables) against that slice table. .text / .idata
> / .rsrc are skipped — the engine never touches them, and a
> one-shot canary in `PeLoaderRead` would warn if that ever changed.

CI / release builds: pushes to `master` and tagged `v*` builds run
the matrix in [`.github/workflows/build.yml`](.github/workflows/build.yml).
Each runner pulls a copy of WACKI.EXE from a URL stored as the
`WACKI_EXE_URL` GH Actions secret (the bytes never enter the repo
or the published artifacts), builds the engine, runs the test suite,
and uploads a per-platform archive. On tag push, the archives get
attached to a GitHub Release automatically.

The engine looks for the game data in this order:

1. `$WACKI_PATH` if set
2. `./data/`                        ← recommended default
3. `<binary_dir>/data/`             ← when launched from elsewhere
4. `<binary_dir>/` (next to ./wacki)
5. the current working directory

Case-folding is handled at archive-open time, so `DANE_02.DTA`
(uppercase, as macOS reads them from an ISO 9660 disc) resolves the
same as `Dane_02.dta`.

## Build targets

| Target            | Output                       | Notes                                  |
|-------------------|------------------------------|----------------------------------------|
| `make` / `make all` | `wacki` + tools            | Default release build (`-O2 -Wpedantic`) |
| `make engine`     | `wacki`                      | Just the engine                        |
| `make tools`      | `dta-extract`, `pkv2-depack` | Standalone asset tools                 |
| `make debug`      | `wacki-debug`                | `-O0 -g -fsanitize=address,undefined`  |
| `make test`       | `tests/run-tests`            | Unit tests (no SDL, 41 suites, 353 cases — production stubs.c linked) — see `tests/README.md` |
| `make run`        | (runs `./wacki`)             | Quick launch                           |
| `make clean`      | —                            | Removes binaries                       |

## Runtime flags

CLI flag or env var (env equivalents in parentheses):

| Flag                 | Env var              | Effect                                         |
|----------------------|----------------------|------------------------------------------------|
| `--headless`         | `WACKI_HEADLESS=1`   | Skip Window/Renderer/Texture (CI/smoke runs)   |
| `--seed N`           | `WACKI_SEED=N`       | Seed WackiRand for deterministic playback      |
| `--scale N`          | `WACKI_SCALE=N`      | Window = N×640 × N×480 (framebuffer stays 640×480) |
| `--scaler MODE`      | `WACKI_SCALER=MODE`  | `nearest` / `linear` / `best` — render quality hint |

Interactive keys (in-game):

| Key       | Action                                             |
|-----------|----------------------------------------------------|
| `ESC`     | quit                                               |
| `SPACE`   | toggle active actor (Ebek / Fjej)                  |
| `F5`      | quick-save to slot 0                               |
| `F9`      | quick-load from slot 0                             |
| Ctrl-C    | graceful shutdown (SIGINT handler, same as ESC)    |

## Subsystem status

| Subsystem                  | Status | Notes                                                                |
|----------------------------|--------|----------------------------------------------------------------------|
| Window / event pump        | ✅     | SDL2 — 640×480 8-bpp paletted backbuffer → SDL_Texture               |
| CD detection               | ✅     | volume-label heuristics on Mac; original Win32 path under mingw      |
| Archive container          | ✅     | BASE/SPIS layout fully parsed                                        |
| PKv2 depack                | ✅     | byte-perfect on all 1782 entries (verified via `tools/dta-validate.sh`) |
| Asset registry             | ✅     | ANIM/MASK/FILD/PIC/PAL headers + pointer table fixup                 |
| Software blitter           | ✅     | color-key, opaque, translucent, scaled, alpha-plane (3 modes)        |
| Palette install / present  | ✅     | shadow buffer → SDL_Texture via 8→32 LUT + RGB12 quantization LUT    |
| Save / load                | ✅     | `Wacki.sav` r/w, slot restore, F5/F9 quick-save, atomic write        |
| Font rasteriser            | ✅     | Futura.30 (BE format) — 1-bpp and colour-plane glyphs                |
| Script VM (main)           | ✅     | 78 opcodes 0x00..0x57, line-by-line audit vs Ghidra (`docs/script-vm.md`) |
| Script VM (per-entity)     | ✅     | opcodes 0x00..0x24 with verification                                 |
| Walker / pathfinding       | ✅     | fixed-point line stepper + waypoint Dijkstra (`FUN_00404600`/`FUN_004046b0`) |
| Audio mixer                | ✅     | single SDL_AudioDevice, 22050 Hz S16 stereo, 8 channels (1 music + 1 dialog + 6 SFX) |
| Positional SFX stereo pan  | ✅     | `FUN_00410DA0` 1:1 + per-channel `gain_l`/`gain_r`                   |
| Per-line dialog + lip-sync | ✅     | mixer ch 1 + `IsDialogLinePlaying` poll                              |
| FLIC/AVI cutscenes         | ✅     | custom FLIC decoder (`src/flic.c`) — intro + death + stage-end AVIs  |
| Inventory                  | ✅     | page-swap, AddItem/RemoveItem, op 0x1A-0x1F                          |
| Dialog choices UI          | 🟡    | linear playback works; interactive choice picker (op 0x1A) deferred  |
| Custom cursor animation    | 🟡    | OS cursor + held-item ghost; Krazek 10-state anim deferred           |
| Health bar depletion       | 🟡    | renders, but depletion mechanism not RE'd (see `docs/health-bar-depletion.md`) |
| Scene snapshot (op 0x2C)   | 🟡    | exit-reachability graph deferred — no consumer identified in port    |
| Standalone runtime         | ❌     | `WACKI.EXE` still needed at runtime (Phase S embed planned)          |

Legend: ✅ port 1:1 / ⚠️ approximation but functional / 🟡 partial /
❌ not implemented.

## Source tree

```
include/wacki.h              types, magic numbers, module APIs (681 LOC)
src/
   main.c                    int main() + CheckCdRomDrive + arg parsing + SIGINT
   game.c                    main game loop, stage loader, frame tick, scene I/O
   graphics.c                portable 8-bpp blitter + alpha-plane scaled blit
   audio.c                   SDL2 mixer (8 channels, 22050 Hz S16 stereo)
   archive.c                 .dta (BASE/SPIS) container parser
   depack.c                  PKv2 LZ77 + Huffman-prefix decoder
   assets.c                  ANIM/MASK/FILD asset loaders
   script.c                  .scr text-tagged loader + main bytecode VM (78 ops)
   actor.c                   entity allocator + per-entity VM + walker + Z-sort
   save.c                    Wacki.sav read/write + slot restore + atomic save
   font.c                    Futura.30 parser + text rasteriser
   flic.c                    FLIC/AVI cutscene decoder
   pe_loader.c               passive PE image map + xlat_binary_ptr
   binary_data.c             embedded small blobs (palette etc.)
   stubs.c                   plumbing: globals, LoadKomnata, ScriptCall*,
                             Inventory, PanelHitTest, ClickHitTest,
                             DialogStack*, SpeechBalloon
   heap.c, cygio.c           shims around Cygert's Base_IO_CPP helpers
   timer.c                   multimedia timer stub
   platform_sdl.c            SDL2 window/event/present + audio device init
tools/
   dta-extract.c             dump every file from a Dane_XX.dta
   pkv2-depack.c             decompress a standalone PKv2 blob
   dta-validate.sh           regression harness (all 1782 SHA-256 checksums)
   smoke-runner.sh           deterministic CI smoke (--seed × multiple)
   dta-baseline.sha256       baseline checksums for dta-validate.sh
docs/
   architecture.md           module map + per-frame tick + scene lifecycle
   script-vm.md              full opcode table (78 entries, 0x00..0x57)
   asset-format.md           DTA / PKv2 / ANIM / MASK / FILD / PIC / PAL / font
   health-bar-depletion.md   open research note (depletion mechanism TBD)
```

## Quick demo

```
$ ls data/
Dane_02.dta  Dane_10.dta  …  WACKI.EXE
$ ./wacki
[wacki] data source: ./data
[archive] ./data/Dane_02.dta mounted (1782 entries)
[init] mounted archive Dane_02.dta
[init] Futura.30 loaded (12608 bytes)
[audio] 22050 Hz, stereo, 8 channels
[avi] play ./data/Dane_10.dta (640x480, 100110 us/frame)
[menu] entered: bg='(none)' mask='Tlo.wyc' atlas-frames=25 btns=5
…
```

An SDL2 window opens at 640×480 showing the real WACKI title menu —
the intro AVI plays first (Dane_10.dta, 5372 frames), then the menu
loads from Dane_02.dta. Click "Maluch" to start stage 1.

Close the window, press ESC, or send SIGINT (Ctrl-C) to exit (exit code 0).

## CD-protection

Stripped out per project rule #7. The portable build just scans for
`Dane_02.dta` next to the binary; the original's volume-label check
(`GetVolumeInformationA("WACKI_1")`) has been replaced with a simple
directory scan in `CheckCdRomDrive()` (kept under its old name for
cross-reference convenience).

## Asset extraction

The two standalone tools build with any C compiler:

```
./dta-extract /Volumes/WACKI_1/Dane_10.dta out/
./pkv2-depack out/EBEK.WYC ebek.raw
```

Regression-checking the depacker:

```
make tools
./tools/dta-validate.sh data/DANE_02.DTA
# [validate] PASS — all 1782 files match baseline
```

## Testing

- `make test` — 41 unit-test suites, 353 cases, zero SDL deps. Production `stubs.c` linked → Inventory state machine + sound queue stereo pan exercised end-to-end. Covers
  PKv2 depack, DTA archive parse + ANIM/MASK/FILD asset loader, PE
  loader + xlat_binary_ptr, Wacki.sav file I/O roundtrip + atomic write,
  Futura.30 font parser + tagged-table walker, RNG determinism + golden
  vector, struct layout invariants (compile-time), walker math (16.16
  fixed-point characterization), RLE decoder + palette LUT, .scr text
  parser (LoadScriptFile + FindScriptByStageAndRoom + tag lookup).
  **VM coverage** is deep: production `RunScriptInterpreter` linked via
  SDL stub, exercised with hand-crafted bytecode — arithmetic ops,
  this/that remap, IF/ELSE/GOTO/LABEL/LOOP control flow including
  round-31 regression for `skip_to_endif` 0x55 handling, ScriptCall*
  dispatch with capture stubs (sound, pal, scene, walker, dialog).
  See `tests/README.md` for coverage map + how to add a suite.
- `tools/dta-validate.sh` — depack regression: extracts every file
  from `DANE_02.DTA`, compares SHA-256 vs `tools/dta-baseline.sha256`.
- `tools/smoke-runner.sh` — deterministic smoke runs over `--seed N`,
  exercises menu → stage 1 entry path.
- `make debug` — ASAN/UBSan build for fuzz / crash debugging.
- Manual: launch interactively, click "Maluch" to reach gameplay.
  Headless `./wacki --headless` reaches 0-13 komnaty (variance from
  SDL macOS focus-event injection; ≠ port regression).

## Documentation map

- `README.md` (this file) — overview, build, status
- `CLAUDE.md` — project rules, workflow, round-by-round deltas,
  lessons learned (tribal knowledge for port maintenance)
- `REFACTOR.md` — code-hygiene plan (R0.* mostly shipped, R1+ proposals)
- `REVIEW.md` — project-level review (docs gaps, structural debt)
- `docs/architecture.md` — module map + frame tick + scene lifecycle
- `docs/script-vm.md` — opcode reference (78 entries)
- `docs/asset-format.md` — binary spec for every file format
- `docs/health-bar-depletion.md` — open research note
- `../WACKI_RE_REPORT.md` — full Ghidra RE write-up

## Known limitations

1. **Standalone runtime** — `WACKI.EXE` must be in the data dir at
   runtime (PE loader reads bytecode + tables from it). Phase S
   (embed PE as static buffer at build time) is planned in `REFACTOR.md`
   but not yet shipped.
2. **Stages 2-5** — entry chain works, but no interactive playthrough
   validation. Stage 1 is the only stage with end-to-end test coverage.
3. **Dialog choice picker** — op 0x1A in the main VM dispatches choices,
   but the interactive picker UI (panel page-swap + result_key) is
   deferred (stages 1-2 scripts don't use multi-choice dialogues).
4. **Health bar depletion** — renders correctly, but the depletion
   mechanism wasn't found in Ghidra (see open research note).
5. **Custom cursor animation** — `FUN_004067C0` Krazek 10-state machine
   not implemented; port uses OS cursor + held-item ghost overlay.

## Credits

* Original engine & game design — **Henryk Cygert** (1997).
* This RE + reconstruction — Ghidra MCP session, May 2026.
