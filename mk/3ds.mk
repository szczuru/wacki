# mk/3ds.mk — Nintendo 3DS homebrew (devkitARM + libctru + picaGL).

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM

CC       := $(DEVKITARM)/bin/arm-none-eabi-gcc
BIN_NAME := wacki

# -D__3DS__ is libctru's CURRENT platform define (3ds.h #warns and asks
# for it instead of the old -DARM11 -D_3DS pair — deliberately NOT passed
# here, only picaGL's OWN build needs that historical fixup, applied by
# tools/build-3ds.sh via sed on picaGL's Makefile before `make install`).
CFLAGS += -D__3DS__ -DWACKI_HANDHELD -DWACKI_3DS \
          -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
          -I src/platform/3ds \
          -I$(DEVKITPRO)/libctru/include \
          -I$(DEVKITPRO)/portlibs/3ds/include \
          -I/opt/devkitpro/picaGL/include

# -O2 (NOT -Os): the base Makefile's CFLAGS already sets -O2, but the
# engine-link recipe passes CFLAGS then CFLAGS_SIZE on the SAME command
# line ($(CC) $(CFLAGS) $(CFLAGS_SIZE) ...) — when a compiler sees two
# conflicting -O flags, the LAST one wins, so this var (originally -Os,
# "optimize for size") was silently overriding -O2 and building the
# ENTIRE engine — the VM interpreter, actor walking, rendering, not just
# video_3ds_gl.c — for binary size instead of speed. That mattered on
# space-constrained targets, but the 3DS loads its .3dsx from an SD card
# with no meaningful size limit, so there's no upside to -Os here, only
# the (measured) downside of a slower interpreter loop on top of
# everything else. -ffunction-sections/-fdata-sections + the linker's
# --gc-sections (LDFLAGS_SIZE below) are UNRELATED to the -O level (they
# just let the linker drop unused functions/data) and are kept as-is.
CFLAGS_SIZE  := -O2 -ffunction-sections -fdata-sections
LDFLAGS_SIZE := -Wl,--gc-sections

LDFLAGS_STATIC := -L$(DEVKITPRO)/libctru/lib \
                   -L$(DEVKITPRO)/portlibs/3ds/lib \
                   -L/opt/devkitpro/picaGL/lib \
                   -specs=3dsx.specs

# The engine-link rule (Makefile) passes $(SDL_LIB) to the linker — that
# var defaults to `sdl2-config --libs`, which doesn't exist on this
# toolchain, so it'd otherwise be empty and every libctru/picaGL symbol
# below would come back "undefined reference". Overriding it here (rather
# than introducing a separate LIBS_3DS the Makefile doesn't know about) is
# exactly how mk/switch.mk and mk/ps2.mk redirect the same hook for their
# own non-SDL2 link lines. picaGL installed to /opt/devkitpro/picaGL by
# tools/build-3ds.sh; -lm last since libctru's own deps (romfs, gfx, hid,
# fs) all resolve within libctru itself.
SDL_LIB := -L/opt/devkitpro/picaGL/lib -lpicaGL -lcitro3d -lctru -lm

# Jesli data/WACKI.EXE istnieje, uzywamy embed-pe-data. W przeciwnym razie - pusty stub.
ifeq ($(wildcard data/WACKI.EXE),)
    EMBEDDED_PE_SRC := src/platform/3ds/embedded_wacki_pe_stub.c
endif

# 3DS ma wlasny platform_3ds.c (bez SDL) zamiast platform_sdl.c — wyzeruj
# domyslna wartosc z glownego Makefile (PLATFORM_MAIN_SRC), inaczej
# platform_sdl.c trafiloby do ENGINE_SRCS i #include <SDL.h> szukalby
# funkcjonalnosci ktorej nasz naglowkowy stub SDL.h nie udostepnia.
PLATFORM_MAIN_SRC :=

# 3DS uzywa wlasnych plikow platformy zamiast SDL. src/vm/script_obj.c i
# src/vm/parser.c NIE sa tu duplikowane - sa juz w glownym ENGINE_SRCS
# (Makefile), ktory dla kazdej platformy woli plat_*/PLATFORM_SRCS.
ENGINE_SRCS += src/platform/3ds/3ds.c \
               src/platform/3ds/platform_3ds.c \
               src/platform/3ds/SDL_compat.c \
               src/platform/3ds/video_3ds_gl.c \
               src/platform/3ds/gamepad_3ds.c \
               src/platform/3ds/storage_3ds.c \
               src/platform/3ds/data_root_3ds.c \
               src/platform/3ds/system_3ds.c \
               src/platform/3ds/audio_3ds_ndsp.c \
               src/platform/sdl/file_host.c \
               src/platform/sdl/flic_host.c

# ---- .3dsx packaging ------------------------------------------------------
3DS_ICON     := assets/icons/wacki-3ds-48x48.png
3DS_3DSX     := $(DIST)/wacki.3dsx
3DS_SMDH     := $(DIST)/wacki.smdh
SMDHTOOL     := $(DEVKITPRO)/tools/bin/smdhtool
3DSXTOOL     := $(DEVKITPRO)/tools/bin/3dsxtool
3DSLINK      := $(DEVKITPRO)/tools/bin/3dslink

all: $(3DS_3DSX)

$(3DS_SMDH): | $(DIST)
	@if [ -f "$(3DS_ICON)" ]; then \
		$(SMDHTOOL) --create "Wacki" "Kosmiczna rozgrywka" "mszula/szczuru/AI" $(3DS_ICON) $(3DS_SMDH); \
	else \
		$(SMDHTOOL) --create "Wacki" "Kosmiczna rozgrywka" "mszula/szczuru/AI" $(3DS_SMDH); \
	fi

$(3DS_3DSX): $(DIST)/$(BIN_NAME)$(EXE) $(3DS_SMDH)
	$(3DSXTOOL) $(DIST)/$(BIN_NAME)$(EXE) $(3DS_3DSX) --smdh=$(3DS_SMDH)

# Optional: send to 3DS via 3dslink (requires network connection to console)
.PHONY: send
send: $(3DS_3DSX)
	$(3DSLINK) $(3DS_3DSX)

