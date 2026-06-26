# mk/switch.mk — Nintendo Switch homebrew via devkitA64 + libnx + SDL2 portlib.
# Built via tools/build-switch.sh (devkitpro/devkita64 Docker image).
#
# WACKI.EXE — two build paths, both fully supported (see also switch.c):
#
#   Embedded (preferred): if data/WACKI.EXE is present at build time
#   (restored from a CI secret or provided locally), the normal embed-pe-data
#   flow runs and .rdata/.data are linked into the binary — identical to
#   PS2/PortMaster. The player needs no WACKI.EXE on their SD card.
#
#   Dynamic fallback: if data/WACKI.EXE is absent (public CI without secret,
#   fresh fork), EMBEDDED_PE_SRC is overridden to point at an empty stub and
#   the engine loads WACKI.EXE from the player's SD card at runtime via
#   PeLoaderInit() in plat_apply_video_prefs() — see src/platform/switch/
#   switch.c. The Makefile's `EMBEDDED_PE_SRC ?=` + ifeq guard around the
#   codegen rule let this override work without touching the top Makefile.

DEVKITPRO ?= /opt/devkitpro
DEVKITA64 ?= $(DEVKITPRO)/devkitA64

CC       := $(DEVKITA64)/bin/aarch64-none-elf-gcc
BIN_NAME := wacki

CFLAGS += -D__SWITCH__ -DWACKI_HANDHELD -DWACKI_SWITCH \
          -march=armv8-a -mtune=cortex-a57 -mtp=soft \
          -ftls-model=local-exec \
          -I$(DEVKITPRO)/libnx/include \
          -I$(DEVKITPRO)/portlibs/switch/include

CFLAGS_SIZE  := -Os -ffunction-sections -fdata-sections
LDFLAGS_SIZE := -Wl,--gc-sections

SWITCH_PKGCONF      := $(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config
SWITCH_PKGCONF_PATH := PKG_CONFIG_PATH=$(DEVKITPRO)/portlibs/switch/lib/pkgconfig
SDL_CFG := $(shell $(SWITCH_PKGCONF_PATH) $(SWITCH_PKGCONF) --cflags sdl2 2>/dev/null)
SDL_LIB := $(shell $(SWITCH_PKGCONF_PATH) $(SWITCH_PKGCONF) --libs sdl2 SDL2_mixer 2>/dev/null) \
           -lEGL -lglapi -ldrm_nouveau -lnx -lm

LDFLAGS_STATIC := -L$(DEVKITPRO)/libnx/lib \
                   -L$(DEVKITPRO)/portlibs/switch/lib \
                   -specs=$(DEVKITPRO)/libnx/switch.specs

# Select embedded vs stub based on whether data/WACKI.EXE exists.
ifeq ($(wildcard data/WACKI.EXE),)
    EMBEDDED_PE_SRC := src/platform/switch/embedded_wacki_pe_stub.c
endif

ENGINE_SRCS += src/platform/switch/switch.c \
               src/platform/switch/storage_switch.c \
               src/platform/switch/data_root_switch.c \
               src/platform/sdl/file_host.c \
               src/platform/sdl/audio_sdl.c \
               src/platform/sdl/flic_host.c \
               src/platform/sdl/video_sdl.c \
               src/platform/sdl/system_sdl.c \
               src/platform/sdl/gamepad_sdl.c \
               src/platform/nintendo/nintendo_gamepad.c

# ---- .nro packaging --------------------------------------------------------
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
