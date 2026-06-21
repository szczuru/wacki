# mk/switch.mk — Nintendo Switch homebrew via devkitA64 + libnx + the SDL2
# portlib. Built via tools/build-switch.sh (devkitpro/devkita64 Docker image,
# matching the miyoo/ps2 wrapper-script pattern), which packages the
# resulting .elf into a .nro via elf2nro.
#
# WACKI_HANDHELD: fullscreen, no display-mode picker, no GUI folder-picker —
#   same flag PortMaster/Miyoo already set.
# WACKI_SWITCH:   device-specific bits live in src/platform/switch/ (hooks
#   provider + save storage) and one #ifdef __SWITCH__ branch in the SHARED
#   src/platform/sdl/system_sdl.c (cwd pin — see that file's comment),
#   mirroring its existing __ANDROID__ branch. __SWITCH__ itself is defined
#   automatically by the devkitA64 toolchain, same class of macro as
#   __ANDROID__ / __APPLE__ / _WIN32.
#
# Copyright note — embedded WACKI.EXE: the public repo / CI can't ship
# WACKI.EXE's copyrighted .rdata/.data bytes, so this target does NOT run
# tools/embed-pe-data at build time. EMBEDDED_PE_SRC is pre-set below to an
# empty-slice-table stub (see embedded_wacki_pe_stub.c); the top-level
# Makefile's `?=` + ifeq guard around the codegen rule leave that alone (see
# PATCH-Makefile.md / the Makefile's own comment above EMBEDDED_PE_SRC).
# Instead, the player's own WACKI.EXE is loaded from the SD card at runtime —
# src/pe_loader.c's PeLoaderRead checks a dynamically-loaded image FIRST, so
# the rest of the engine works completely unmodified. See
# src/platform/switch/switch.c's plat_apply_video_prefs() for the load call
# and its candidate SD-card paths.

DEVKITPRO ?= /opt/devkitpro
DEVKITA64 ?= $(DEVKITPRO)/devkitA64

CC       := $(DEVKITA64)/bin/aarch64-none-elf-gcc
BIN_NAME := wacki

CFLAGS += -D__SWITCH__ -DWACKI_HANDHELD -DWACKI_SWITCH \
          -march=armv8-a -mtune=cortex-a57 -mtp=soft \
          -ftls-model=local-exec \
          -I$(DEVKITPRO)/libnx/include \
          -I$(DEVKITPRO)/portlibs/switch/include

# -Os + section GC keeps the .nro lean (SD card + load time matter on a
# handheld); no -flto here — same "be conservative with a newer/less-battle-
# tested toolchain" reasoning as mk/miyoo.mk.
CFLAGS_SIZE  := -Os -ffunction-sections -fdata-sections
LDFLAGS_SIZE := -Wl,--gc-sections

# The portlibs ship pkg-config .pc files, not sdl2-config. SDL2 on Switch is
# a static .a linked into the .elf (no dynamic linking for homebrew .nro), so
# STATIC_SDL2 / sdl2-config from the top Makefile don't apply — overridden
# wholesale here, same pattern tools/build-ps2.sh uses for SDL_CFG/SDL_LIB.
SWITCH_PKGCONF := $(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config
SWITCH_PKGCONF_PATH := PKG_CONFIG_PATH=$(DEVKITPRO)/portlibs/switch/lib/pkgconfig
SDL_CFG := $(shell $(SWITCH_PKGCONF_PATH) $(SWITCH_PKGCONF) --cflags sdl2 2>/dev/null)
SDL_LIB := $(shell $(SWITCH_PKGCONF_PATH) $(SWITCH_PKGCONF) --libs sdl2 SDL2_mixer 2>/dev/null) \
           -lEGL -lglapi -ldrm_nouveau -lnx -lm

LDFLAGS_STATIC := -L$(DEVKITPRO)/libnx/lib -L$(DEVKITPRO)/portlibs/switch/lib \
                   -specs=$(DEVKITPRO)/libnx/switch.specs

# See the header comment above — this target supplies its own empty PE-slice
# stub instead of letting the top Makefile generate src/embedded_wacki_pe.c
# from data/WACKI.EXE (which doesn't exist on the build machine).
EMBEDDED_PE_SRC := src/platform/switch/embedded_wacki_pe_stub.c

# Platform sources: reuse the SDL family wherever Switch behaves like any
# other SDL2 handheld (file I/O, audio, FLIC streaming, video present,
# gamepad), but swap in our own storage_switch.c for the save image — Switch
# needs an fsdevCommitDevice() call after every write that save_host.c
# doesn't make (see that file's header comment for why) — and our own
# data_root_switch.c, since Switch's sdmc:/switch/<app>/ homebrew convention
# is unrelated to Miyoo/PortMaster's ROMs layout in data_root_handheld.c.
ENGINE_SRCS += src/platform/switch/switch.c \
               src/platform/switch/storage_switch.c \
               src/platform/switch/data_root_switch.c \
               src/platform/sdl/file_host.c \
               src/platform/sdl/audio_sdl.c \
               src/platform/sdl/flic_host.c \
               src/platform/sdl/video_sdl.c \
               src/platform/sdl/system_sdl.c \
               src/platform/sdl/gamepad_sdl.c

# ---- .nro packaging (.elf -> .nro) -----------------------------------------
# elf2nro (devkitA64/switch-tools) wraps the statically linked ELF with an
# icon + .nacp metadata. Hooked onto `all` so a plain `make TARGET=switch`
# produces dist/wacki.nro directly, matching `make TARGET=ps2` producing the
# .elf (and `make ps2-iso` separately for the bootable image).
SWITCH_ICON := assets/icons/wacki-switch.jpg
SWITCH_NACP := $(DIST)/wacki.nacp
SWITCH_NRO  := $(DIST)/wacki.nro
NACPTOOL    := $(DEVKITPRO)/tools/bin/nacptool
ELF2NRO     := $(DEVKITPRO)/tools/bin/elf2nro

all: $(SWITCH_NRO)

$(SWITCH_NACP): | $(DIST)
	$(NACPTOOL) --create "Wacki: Kosmiczna rozgrywka" "mszula" "$(WACKI_VERSION)" $(SWITCH_NACP)

$(SWITCH_NRO): $(DIST)/$(BIN_NAME)$(EXE) $(SWITCH_NACP)
	$(ELF2NRO) $(DIST)/$(BIN_NAME)$(EXE) $(SWITCH_NRO) \
	    --icon=$(SWITCH_ICON) --nacp=$(SWITCH_NACP)