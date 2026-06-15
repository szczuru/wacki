# Makefile — builds the reconstructed Wacki engine (SDL2 portable) + tools.

# GNU Make's implicit default goal is "the first target defined in the
# file" UNLESS .DEFAULT_GOAL is set. $(DIST) (= dist/, the output
# directory rule) is defined early — before `all:` — so without this
# line, a bare `make` would treat `dist` itself as the goal (just
# `mkdir -p dist`, nothing else) instead of `all`. Explicit is better
# than implicit here.
.DEFAULT_GOAL := all

CC       ?= cc
SDL2_CFG ?= sdl2-config

# ---- cross-compile knobs ------------------------------------------------- *
#
# TARGET=miyoo cross-compiles for Miyoo Mini Plus and pin-compatible
# handhelds (Cortex-A7 armv7l, hardfloat, NEON). Used together with a
# CROSS_COMPILE prefix:
#
#   make TARGET=miyoo CROSS_COMPILE=arm-linux-gnueabihf-
#
# In practice you run it through tools/build-miyoo.sh which sets up the
# union-miyoomini-toolchain Docker image with the right sdl2-config.
#
# TARGET=switch cross-compiles for Nintendo Switch homebrew via
# devkitA64 (devkitPro). Toolchain comes from the devkitpro/devkita64
# Docker image — no CROSS_COMPILE prefix needed, DEVKITPRO env var
# points at /opt/devkitpro and we call the aarch64-none-elf-* tools
# directly.
TARGET         ?=
CROSS_COMPILE  ?=
DEVKITPRO      ?= /opt/devkitpro
DEVKITA64      ?= $(DEVKITPRO)/devkitA64
# HOSTCC is the compiler for build-time tools (embed-pe-data) which
# MUST run on the build machine, not the target. When cross-compiling
# we still need a host gcc/cc to produce a runnable code generator;
# default to whatever cc resolved to before CROSS_COMPILE overrode it.
HOSTCC         ?= cc
ifneq ($(CROSS_COMPILE),)
    CC := $(CROSS_COMPILE)gcc
endif

# Default build: release-ish with all warnings. -Wpedantic enabled —
# the two GNU extensions we use intentionally (typeof, statement-exprs)
# are suppressed via -Wno-language-extension-token so the build stays
# clean. Any NEW warning surfaces.
CFLAGS   ?= -O2 -Wall -Wextra -Wpedantic \
            -Wno-unused-parameter -Wno-pointer-sign \
            -Wno-language-extension-token \
            -fno-strict-aliasing \
            -std=gnu99 -I include

# Version string baked into the binary's startup banner. Defaults to
# git describe so dev builds carry "<last-tag>-<n>-g<sha>[-dirty]";
# users running a release can read it back from the wacki.log + paste
# it into bug reports. Falls back to "unknown" if we're outside a git
# checkout (release tarball extracted somewhere weird, etc.).
WACKI_VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo unknown)
CFLAGS        += -DWACKI_VERSION='"$(WACKI_VERSION)"'

# Handheld build profiles. WACKI_HANDHELD is the generic "this is a
# handheld" switch (fullscreen, no display-mode picker, gamepad → cursor
# input, SD-card data paths). WACKI_MIYOO layers the Miyoo Mini Plus
# device specifics on top (the mmiyoo SDL2 fork's MI_AO volume restore +
# its keysym button mapping in platform_miyoo.c).
#
#   TARGET=miyoo       Cortex-A7 + NEON + hardfloat, mmiyoo SDL2.
#                      Don't enable LTO — the Miyoo toolchain's ld has
#                      mis-emitted thumb thunks under -flto; -Os + section
#                      GC still gets ~90% of the size win.
#   TARGET=portmaster  Anbernic & friends (PortMaster). Standard SDL2,
#                      stereo ALSA audio, real SDL_GameController input.
#                      Arch flags (aarch64 / armhf) come from the build
#                      environment (tools/build-portmaster.sh), so this
#                      profile stays architecture-agnostic.
#   TARGET=switch      Nintendo Switch homebrew (libnx + SDL2 portlib).
#                      WACKI_SWITCH additionally selects the dynamic
#                      WACKI.EXE loading path in src/main.c (no embedded
#                      PE data — see src/embedded_wacki_pe_stub.c).
ifeq ($(TARGET),miyoo)
    CFLAGS  += -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=hard \
               -DWACKI_HANDHELD -DWACKI_MIYOO
    BIN_NAME := wacki-miyoo
else ifeq ($(TARGET),portmaster)
    CFLAGS  += -DWACKI_HANDHELD -DWACKI_PORTMASTER
    BIN_NAME := wacki
else ifeq ($(TARGET),switch)
    CC       := $(DEVKITA64)/bin/aarch64-none-elf-gcc
    HOSTCC   ?= cc
    CFLAGS   += -D__SWITCH__ -DWACKI_HANDHELD -DWACKI_SWITCH \
                -march=armv8-a -mtune=cortex-a57 -mtp=soft \
                -ftls-model=local-exec \
                -I$(DEVKITPRO)/libnx/include \
                -I$(DEVKITPRO)/portlibs/switch/include
    BIN_NAME := wacki
else
    BIN_NAME := wacki
endif
# NOTE: -fno-strict-aliasing is REQUIRED. The Entity struct is accessed
# through multiple type-punned aliases — script writes `*(int32_t *)(eb +
# 0x48)` and same memory is read as `*(int16_t *)(eb + 0x4A)` (upper 16
# bits of the int32). Under strict-aliasing the compiler may reorder
# these → walker step loop misses the target-reached comparison and
# overshoots by 1px per tick → cascading "actor walks past target"
# bugs (Fjej weź-kwiatka overshoot, Ebek climb glitches).

# ---- SDL2 flags / libs ----------------------------------------------------
#
# TARGET=switch: the portlibs ship pkg-config .pc files, not
# sdl2-config. SDL2 on Switch is a static .a from portlibs linked into
# the .elf — STATIC_SDL2 doesn't apply (no dynamic linking for
# homebrew .nro), so we bypass the STATIC_SDL2 machinery entirely for
# this target.
ifeq ($(TARGET),switch)
    SWITCH_PKGCONF := $(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config
    SDL_CFG     := $(shell PKG_CONFIG_PATH=$(DEVKITPRO)/portlibs/switch/lib/pkgconfig $(SWITCH_PKGCONF) --cflags sdl2 2>/dev/null)
    SDL_LIB_DYN := $(shell PKG_CONFIG_PATH=$(DEVKITPRO)/portlibs/switch/lib/pkgconfig $(SWITCH_PKGCONF) --libs sdl2 SDL2_mixer 2>/dev/null)
    SDL_LIB     := $(SDL_LIB_DYN)
    STATIC_SDL2 := 0
else
    SDL_CFG  := $(shell $(SDL2_CFG) --cflags 2>/dev/null)

    # STATIC_SDL2=1 selects a fully self-contained engine binary: SDL2 +
    # its transitive system links are baked into the binary so the user
    # doesn't need libSDL2 installed. CI release builds set this; the
    # default (=0) keeps the developer build dynamic for fast iteration
    # + easier debugging.
    #
    # `SDL_LIB_DYN`  always dynamic — used by the sanitized debug build
    #                (ASan + static SDL2 don't mix).
    # `SDL_LIB`      tracks STATIC_SDL2 — used by the release engine.
    STATIC_SDL2 ?= 0
    SDL_LIB_DYN := $(shell $(SDL2_CFG) --libs 2>/dev/null)
    ifeq ($(STATIC_SDL2),1)
        SDL_LIB := $(shell $(SDL2_CFG) --static-libs 2>/dev/null)
    else
        SDL_LIB := $(SDL_LIB_DYN)
    endif
endif

# Extra link flags + libs for TARGET=switch: libnx, GPU/EGL stack the
# SDL2 Switch backend needs, and the switch.specs file that wires up
# the homebrew ABI / crt0. Empty for every other target.
ifeq ($(TARGET),switch)
    SWITCH_LIBS    := -lSDL2_mixer -lSDL2 -lEGL -lglapi -ldrm_nouveau \
                       -lnx -lm
    LDFLAGS_SWITCH := -L$(DEVKITPRO)/libnx/lib \
                       -L$(DEVKITPRO)/portlibs/switch/lib \
                       -specs=$(DEVKITA64)/aarch64-none-elf/lib/switch.specs
else
    SWITCH_LIBS    :=
    LDFLAGS_SWITCH :=
endif

# All built binaries land in $(DIST). The directory is gitignored —
# build artefacts never sit at the repo root or alongside the source
# files they're built from.
DIST     := dist
$(DIST):
	@mkdir -p $@

# Windows (MSYS2/mingw) binaries need a `.exe` suffix. The `OS`
# env var is set to "Windows_NT" by both mingw-make and GNU make
# under MSYS2; all POSIX hosts leave it empty.
ifeq ($(OS),Windows_NT)
    EXE := .exe
else
    EXE :=
endif

# Windows resource: assets/icons/wacki.rc references wacki.ico — the
# multi-size (16/32/48/64/128/256) Win32 icon. windres compiles the
# .rc into a COFF object that gets linked directly into the .exe so
# Explorer + Alt-Tab + taskbar show the artwork even before the
# binary starts (SDL_SetWindowIcon takes over once the window opens).
# POSIX hosts skip this — there's no equivalent embed-file-icon-in-
# ELF pattern.
ifeq ($(OS),Windows_NT)
    WACKI_RES := $(DIST)/wacki.res.o
    WINDRES   ?= windres
endif

# Extra link flags for fully-static Windows: drop the mingw gcc /
# winpthread DLLs that linker pulls in by default. POSIX builds
# don't need this — the static SDL2 already lists every system
# library through `sdl2-config --static-libs`.
ifeq ($(STATIC_SDL2),1)
ifeq ($(OS),Windows_NT)
    LDFLAGS_STATIC := -static -static-libgcc
else
    LDFLAGS_STATIC :=
endif
else
    LDFLAGS_STATIC :=
endif

# Windows subsystem: -mwindows marks the .exe as IMAGE_SUBSYSTEM_
# WINDOWS_GUI so Explorer launches it WITHOUT spawning a console
# window — players double-click wacki.exe and only see the game.
# Without this, the default mingw build emits a CUI (console)
# subsystem binary and Windows pops up a cmd.exe-style black box
# every launch.
#
# Entry point doesn't need a rewrite: SDL2 ships libSDL2main.a with
# a WinMain that calls our int main() — sdl2-config --libs adds it
# to the link line.
#
# We intentionally lose stderr/stdout output as a side-effect (no
# attached console). For dev / CI runs that need the log, build
# WACKI_WINDOWED=0 to skip -mwindows and keep the console.
ifeq ($(OS),Windows_NT)
WACKI_WINDOWED ?= 1
ifeq ($(WACKI_WINDOWED),1)
    LDFLAGS_STATIC += -mwindows
endif
endif

# Size-optimisation knobs for the release artefact. -Os trades a few
# % runtime perf for noticeably smaller code (fine for a 1997 point-
# and-click). -ffunction-sections / -fdata-sections + the linker's
# --gc-sections (Linux/mingw) or -dead_strip (macOS) drop unused
# code paths inside statically-linked SDL2 — a big win since we
# touch ~half of SDL2's API. -flto lets the linker see across TUs
# for further dead-code elimination.
#
# Always on for STATIC_SDL2=1 (release path); never on for the
# default dev build (faster compile, easier to debug).
# Size opts apply to STATIC_SDL2=1 (release artefact) AND TARGET=miyoo
# (always, since 128 MB RAM + ~16 MB free for app on Miyoo Mini Plus
# means every kB counts even with dynamic SDL2). Miyoo also skips
# -flto — the union toolchain's ld + linker plugins have been known
# to mis-emit thumb thunks under -flto.
#
# TARGET=switch: -Os keeps the .nro lean (SD card + load time) and
# section GC trims unused SDL2/libnx code; no -flto for the same
# "be conservative with new toolchains" reasoning as Miyoo.
ifeq ($(TARGET),miyoo)
    CFLAGS_SIZE  := -Os -ffunction-sections -fdata-sections
    # -ldl: platform_sdl.c uses dlsym(RTLD_DEFAULT, "MI_AO_SetVolume") to
    # bind directly to the MStar audio API for OnionOS volume restore
    # (alsa-style tinymix controls don't exist on MMP — kernel exposes
    # MI_AO instead, which the mmiyoo SDL2 backend has already loaded).
    LDFLAGS_SIZE := -Wl,--gc-sections -ldl
else ifeq ($(TARGET),switch)
    CFLAGS_SIZE  := -Os -ffunction-sections -fdata-sections
    LDFLAGS_SIZE := -Wl,--gc-sections
else ifeq ($(STATIC_SDL2),1)
    CFLAGS_SIZE := -Os -ffunction-sections -fdata-sections -flto
    UNAME_S := $(shell uname -s 2>/dev/null)
    ifeq ($(OS),Windows_NT)
        LDFLAGS_SIZE := -flto -Wl,--gc-sections
    else ifeq ($(UNAME_S),Linux)
        LDFLAGS_SIZE := -flto -Wl,--gc-sections
    else ifeq ($(UNAME_S),Darwin)
        LDFLAGS_SIZE := -flto -Wl,-dead_strip
    else
        LDFLAGS_SIZE := -flto
    endif
else
    CFLAGS_SIZE  :=
    LDFLAGS_SIZE :=
endif

# T43 — debug build with AddressSanitizer + UBSan. Use `make debug` to
# rebuild with sanitizers + frame pointer + no opt for actionable
# backtraces. Crashes/leaks abort with a full stack.
# -DWACKI_VERBOSE enables LOG_TRACE / LOG_DEBUG (compiled out in
# release for zero overhead).
DEBUG_CFLAGS = -O0 -g -fno-omit-frame-pointer \
               -fsanitize=address -fsanitize=undefined \
               -fno-strict-aliasing \
               -Wall -Wextra -Wno-unused-parameter -Wno-pointer-sign \
               -Wno-language-extension-token \
               -DWACKI_VERBOSE \
               -std=gnu99 -I include
DEBUG_LDFLAGS = -fsanitize=address -fsanitize=undefined

# ---- embedded WACKI.EXE data sections --------------------------------------
# tools/embed-pe-data reads WACKI.EXE and emits a generated C source
# with the .rdata + .data raw bytes as a const slice table + blob.
# The engine PE-loader resolves original VAs against this table at
# runtime — no PE parsing, no file I/O after build. Other sections
# (.text x86 code, .idata, .rsrc) are skipped (we never reference
# them). Build-time dep: data/WACKI.EXE must exist; generated file is
# gitignored.
#
# TARGET=switch is the EXCEPTION: it does NOT embed WACKI.EXE at all
# (no copyrighted data in the public repo / CI). Instead it links
# src/embedded_wacki_pe_stub.c (an empty slice table) and the engine
# loads the user's own WACKI.EXE at runtime from the SD card via
# PeLoaderInit — see src/main.c's load_wacki_exe_dynamic(). The
# EMBEDDED_PE_SRC variable below is therefore unused for this target
# and data/WACKI.EXE is never required to build it.
EMBEDDED_PE_SRC = src/embedded_wacki_pe.c
EMBEDDED_PE_BIN = data/WACKI.EXE
EMBED_PE_TOOL   = $(DIST)/embed-pe-data$(EXE)

# Built with HOSTCC because this tool runs at build time on the build
# machine; cross-compiling it for the target would mean trying to
# qemu it (or worse, run an ARM binary natively on x86_64) every
# time the embedded PE source needs regenerating.
$(EMBED_PE_TOOL): tools/embed-pe-data.c | $(DIST)
	$(HOSTCC) -O2 -Wall -I include -o $@ $<

$(EMBEDDED_PE_SRC): $(EMBEDDED_PE_BIN) $(EMBED_PE_TOOL)
	$(EMBED_PE_TOOL) $(EMBEDDED_PE_BIN) $(EMBEDDED_PE_SRC)

# Window icon: embed the 64×64 BMP from assets/icons/wacki-window.bmp
# as a C byte array so SDL_SetWindowIcon can hand it to whichever
# window system the user runs (macOS Dock / Linux taskbar / Windows
# titlebar). Generator is plain `xxd -i` plus a hand-written SPDX
# header; checked into git so contributors without xxd installed can
# still build. Regenerate by running this target after changing the
# BMP source. */
EMBEDDED_ICON_SRC = src/embedded_icon.c
EMBEDDED_ICON_BIN = assets/icons/wacki-window.bmp

$(EMBEDDED_ICON_SRC): $(EMBEDDED_ICON_BIN)
	@which xxd >/dev/null || { echo "xxd missing — install vim-common"; exit 1; }
	@(printf '/* SPDX-License-Identifier: GPL-3.0-or-later\n * Copyright (C) 2026 Mateusz Szu\xc5\x82\x61\n *\n * src/embedded_icon.c — GENERATED from %s.\n * Loaded as an SDL_Surface via SDL_LoadBMP_RW and handed to\n * SDL_SetWindowIcon in PlatformInit so the engine'\''s window /\n * taskbar / dock entry carry the game artwork.\n */\n\n#include <stddef.h>\n\n' "$(EMBEDDED_ICON_BIN)"; xxd -i -n wacki_icon_bmp $(EMBEDDED_ICON_BIN)) > $@

# ---- modules ----------------------------------------------------------------
#
# ENGINE_SRCS_COMMON is shared by every target. The embedded-PE source
# differs: every target except switch generates+links
# src/embedded_wacki_pe.c from data/WACKI.EXE; switch links the empty
# src/embedded_wacki_pe_stub.c instead (see comment block above
# EMBEDDED_PE_SRC).
ENGINE_SRCS_COMMON = \
	$(EMBEDDED_ICON_SRC) \
	src/main.c     src/data_root.c src/config.c \
	src/game.c    src/graphics.c  src/audio.c     \
	src/archive.c  src/depack.c  src/assets.c    src/vm/main.c   \
	src/actor/intern.c    src/actor/registration.c \
	src/actor/list.c src/actor/vm.c \
	src/actor/render.c src/actor/alloc.c \
	src/actor/walker.c                            \
	src/save.c    src/font.c      src/flic.c   src/flic/decoder.c \
	src/heap.c     src/cygio.c   src/timer.c     src/stubs.c     \
	src/binary_data.c src/pe_loader.c src/log.c                  \
	src/platform_sdl.c src/vm/script_obj.c src/vm/parser.c       \
	src/util/rng.c                                               \
	src/hud/panel.c src/hud/inventory.c src/hud/items.c          \
	src/scene/click_queue.c src/scene/hit_test.c src/scene/mask_list.c \
	src/scene/navigation.c src/scene/actor_walk.c src/scene/stage.c \
	src/scene/bg_mask.c src/scene/spawn.c src/scene/komnata.c     \
	src/scene/preload.c src/scene/hud_paint.c src/scene/frame_tick.c \
	src/scene/dispatch.c src/hud/cursor.c                            \
	src/scene/walkability.c src/scene/scene_input.c                  \
	src/scene/play_loop.c   src/scene/komnata_scene.c                \
	src/audio/sound_queue.c   src/audio/cutscene.c                \
	src/audio/sfx.c           src/audio/music_stream.c           \
	src/script_bridge/palette.c src/script_bridge/entity.c        \
	src/text/balloon.c src/text/dialog.c                          \
	src/anim/resolver.c src/util/screenshot.c                     \
	src/anim/paint_primitives.c src/anim/alpha_blit.c             \
	src/menu/chapter_select.c src/menu/slot_picker.c                 \
	src/menu/options.c        src/menu/menu_loop.c                   \
	src/menu/main_menu.c      src/menu/port_attribution.c

ifeq ($(TARGET),switch)
    ENGINE_SRCS = $(ENGINE_SRCS_COMMON) src/embedded_wacki_pe_stub.c
else
    ENGINE_SRCS = $(ENGINE_SRCS_COMMON) $(EMBEDDED_PE_SRC)
endif

# Platform-specific glue is appended only for the matching TARGET.
# src/platform_miyoo.c carries the OnionOS/MStar bits (MI_AO volume
# restore) and pulls in libdl — kept out of desktop builds because
# desktop linkers wouldn't find dlsym + libmi_ao.so doesn't exist.
# src/platform_portmaster.c carries the Anbernic SDL_GameController →
# cursor/click input glue. src/platform_switch.c is the equivalent for
# the Switch's combined Joy-Con / Pro Controller.
ifeq ($(TARGET),miyoo)
    ENGINE_SRCS += src/platform_miyoo.c
else ifeq ($(TARGET),portmaster)
    ENGINE_SRCS += src/platform_portmaster.c
else ifeq ($(TARGET),switch)
    ENGINE_SRCS += src/platform_switch.c
endif

# macOS desktop gets a small Objective-C helper that re-titles SDL's
# default English menu bar (App / Window / View) into Polish. clang
# compiles the .m as Objective-C from its extension even inside the
# single mixed C/.m link command; AppKit is pulled in via -framework
# Cocoa. -framework Security pulls in the SecTranslocate SPI used to
# undo Gatekeeper App Translocation (see PlatformMacUntranslocatePath),
# so "drop data/ next to Wacki.app" works for downloaded bundles.
# Darwin-only and never for the (Linux/ARM) Miyoo target.
ifneq ($(TARGET),miyoo)
ifneq ($(OS),Windows_NT)
ifeq ($(shell uname -s 2>/dev/null),Darwin)
    ENGINE_SRCS      += src/platform_macos.m
    MACOS_FRAMEWORKS := -framework Cocoa -framework Security
endif
endif
endif

TOOL_SRCS_EXTRACT = tools/dta-extract.c src/depack.c src/archive.c \
                    src/cygio.c src/heap.c src/log.c
TOOL_SRCS_PKV2    = tools/pkv2-depack.c src/depack.c src/log.c

# ---- tests (no SDL) -----------------------------------------------------
# Unit tests link only the SDL-free subset of the engine + small mocks
# for symbols normally defined in stubs.c (which #includes SDL).
# Coverage map + how to add a suite: see tests/README.md.
TEST_SRCS = \
	tests/test_main.c              tests/test_entity_layout.c \
	tests/test_rng.c               tests/test_pkv2.c          \
	tests/test_archive.c           tests/test_save.c          \
	tests/test_save_io.c           tests/test_assets.c        \
	tests/test_font.c              tests/test_walker.c        \
	tests/test_graphics.c          tests/test_pe_loader.c     \
	tests/test_binary_data.c       tests/test_timer.c         \
	tests/test_script_vm.c         tests/test_real_vm.c       \
	tests/test_vm_control_flow.c                              \
	tests/test_vm_script_parser.c  tests/test_vm_call_stack.c \
	tests/test_vm_more_ops.c       tests/test_heap_cygio.c    \
	tests/test_vm_walk_anim.c      tests/test_vm_dialog.c     \
	tests/test_vm_misc_ops.c       tests/test_vm_safety.c     \
	tests/test_vm_game_over.c      tests/test_per_entity_vm.c \
	tests/test_archive_extended.c  tests/test_pe_loader_malformed.c \
	tests/test_assets_rle.c        tests/test_font_color.c    \
	tests/test_save_slot.c         tests/test_vm_with_pe.c    \
	tests/test_vm_show_text_bind.c tests/test_graphics_alpha.c \
	tests/test_vm_wait_break.c     tests/test_archive_lifecycle.c \
	tests/test_pe_loader_lifecycle.c tests/test_vm_corner_cases.c \
	tests/test_save_io_extended.c                              \
	tests/test_inventory.c         tests/test_sound_queue.c   \
	tests/test_panel_hit_test.c    tests/test_click_hit_test.c \
	tests/test_per_entity_vm_real.c                            \
	tests/test_click_queue.c       tests/test_update_registration.c \
	tests/test_ent_ptr_intern.c    tests/test_sampl_parser.c   \
	tests/test_komnata_load.c                                  \
	tests/test_engine_stubs.c

TEST_ENGINE_SRCS = \
	tests/embedded_wacki_pe_stub.c                 \
	src/depack.c    src/archive.c  src/graphics.c \
	src/pe_loader.c src/heap.c     src/cygio.c    \
	src/assets.c    src/font.c     src/save.c     \
	src/binary_data.c src/timer.c  src/vm/main.c  src/log.c \
	src/vm/script_obj.c src/vm/parser.c          \
	src/util/rng.c                                \
	src/hud/panel.c src/hud/inventory.c src/hud/items.c \
	src/scene/click_queue.c src/scene/hit_test.c src/scene/mask_list.c \
	src/scene/navigation.c src/scene/actor_walk.c src/scene/stage.c \
	src/scene/bg_mask.c src/scene/spawn.c src/scene/komnata.c     \
	src/audio/sound_queue.c   src/audio/sfx.c                    \
	src/script_bridge/palette.c src/script_bridge/entity.c        \
	src/text/balloon.c src/text/dialog.c                          \
	src/anim/resolver.c                                           \
	src/stubs.c     src/actor/intern.c    src/actor/registration.c \
	src/actor/list.c src/actor/vm.c \
	src/actor/render.c src/actor/alloc.c \
	src/actor/walker.c src/anim/alpha_blit.c

# Tests reuse the engine's CFLAGS but use a stub SDL.h (tests/sdl_stub)
# instead of the system SDL2 headers. -I tests/sdl_stub MUST come first
# so the stub is picked up by script.c's `#include <SDL.h>`. Bump to
# gnu11 so `_Static_assert` is a first-class citizen (no
# -Wc11-extensions noise).
TEST_CFLAGS = -O2 -Wall -Wextra -Wpedantic \
              -Wno-unused-parameter -Wno-pointer-sign \
              -Wno-language-extension-token \
              -fno-strict-aliasing \
              -std=gnu11 -I tests/sdl_stub -I include -I tests

# ---- targets ----------------------------------------------------------------
.PHONY: all engine tools clean run debug test miyoo
all: engine tools

engine: $(DIST)/$(BIN_NAME)$(EXE)
$(DIST)/$(BIN_NAME)$(EXE): $(ENGINE_SRCS) $(WACKI_RES) | $(DIST)
	$(CC) $(CFLAGS) $(CFLAGS_SIZE) $(SDL_CFG) -o $@ $(ENGINE_SRCS) $(WACKI_RES) $(LDFLAGS_SWITCH) $(SDL_LIB) $(SWITCH_LIBS) $(LDFLAGS_STATIC) $(LDFLAGS_SIZE) $(MACOS_FRAMEWORKS)

ifeq ($(OS),Windows_NT)
$(WACKI_RES): assets/icons/wacki.rc assets/icons/wacki.ico | $(DIST)
	$(WINDRES) --include-dir=assets/icons -i $< -o $@
endif

# Convenience target: build through the union-miyoomini-toolchain
# Docker image so callers don't need a host-installed ARM cross compiler.
# Inside the container sdl2-config is in PATH and points at the cross-
# built static SDL2 the toolchain ships with.
miyoo:
	./tools/build-miyoo.sh

# Debug build with sanitizers — separate binary so the release build
# stays untouched. Run via $(DIST)/wacki-debug --headless for CI fuzz
# runs. Sanitizers + static linking don't mix, so the debug build
# always uses the dynamic SDL2.
debug: $(DIST)/wacki-debug$(EXE)
$(DIST)/wacki-debug$(EXE): $(ENGINE_SRCS) | $(DIST)
	$(CC) $(DEBUG_CFLAGS) $(SDL_CFG) -o $@ $(ENGINE_SRCS) $(SDL_LIB_DYN) $(DEBUG_LDFLAGS) $(MACOS_FRAMEWORKS)

tools: $(DIST)/dta-extract$(EXE) $(DIST)/pkv2-depack$(EXE)

$(DIST)/dta-extract$(EXE): $(TOOL_SRCS_EXTRACT) | $(DIST)
	$(CC) $(CFLAGS) -o $@ $(TOOL_SRCS_EXTRACT)

$(DIST)/pkv2-depack$(EXE): $(TOOL_SRCS_PKV2) | $(DIST)
	$(CC) $(CFLAGS) -o $@ $(TOOL_SRCS_PKV2)

run: $(DIST)/$(BIN_NAME)$(EXE)
	$(DIST)/$(BIN_NAME)$(EXE)

# Build + run unit tests. Exit non-zero if any test fails.
test: $(DIST)/run-tests$(EXE)
	$(DIST)/run-tests$(EXE)

$(DIST)/run-tests$(EXE): $(TEST_SRCS) $(TEST_ENGINE_SRCS) | $(DIST)
	$(CC) $(TEST_CFLAGS) -o $@ $(TEST_SRCS) $(TEST_ENGINE_SRCS)

# ---- Nintendo Switch packaging (.elf -> .nro) -------------------------------
# elf2nro (part of devkitA64 / switch-tools) converts the statically
# linked ELF into the .nro format Atmosphère / hbmenu can launch.
# Bundles an icon (assets/icons/wacki-switch.jpg, 256x256 JPEG) and a
# .nacp (name/author/version metadata) produced by nacptool.
ifeq ($(TARGET),switch)

SWITCH_ICON  := assets/icons/wacki-switch.jpg
SWITCH_NACP  := $(DIST)/wacki.nacp
SWITCH_NRO   := $(DIST)/wacki.nro
NACPTOOL     := $(DEVKITPRO)/tools/bin/nacptool
ELF2NRO      := $(DEVKITPRO)/tools/bin/elf2nro

all: $(SWITCH_NRO)

$(SWITCH_NACP): | $(DIST)
	$(NACPTOOL) --create "Wacki: Kosmiczna rozgrywka" "mszula" "$(WACKI_VERSION)" $(SWITCH_NACP)

$(SWITCH_NRO): $(DIST)/$(BIN_NAME)$(EXE) $(SWITCH_NACP)
	$(ELF2NRO) $(DIST)/$(BIN_NAME)$(EXE) $(SWITCH_NRO) \
	    --icon=$(SWITCH_ICON) --nacp=$(SWITCH_NACP)

endif

# `clean` blows away the whole $(DIST) tree (every built artefact
# lives there now). Also removes the generated embed source +
# stray .o files left behind by ad-hoc compiles.
clean:
	rm -rf $(DIST)
	rm -f $(EMBEDDED_PE_SRC)
	rm -f *.o src/*.o tools/*.o tests/*.o
