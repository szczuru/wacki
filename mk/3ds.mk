# mk/3ds.mk — Nintendo 3DS homebrew (devkitARM + libctru + picaGL).

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM

CC       := $(DEVKITARM)/bin/arm-none-eabi-gcc
BIN_NAME := wacki

CFLAGS += -D__3DS__ -DWACKI_HANDHELD -DWACKI_3DS \
          -DARM11 -D_3DS \
          -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
          -I$(DEVKITPRO)/libctru/include \
          -I$(DEVKITPRO)/portlibs/3ds/include

CFLAGS_SIZE  := -Os -ffunction-sections -fdata-sections
LDFLAGS_SIZE := -Wl,--gc-sections

LDFLAGS_STATIC := -L$(DEVKITPRO)/libctru/lib \
                   -L$(DEVKITPRO)/portlibs/3ds/lib \
                   -specs=3dsx.specs

# picaGL + citro3d + libctru
LIBS_3DS := -lpicaGL -lcitro3d -lctru -lm

# Jesli data/WACKI.EXE istnieje, uzywamy embed-pe-data. W przeciwnym razie - pusty stub.
ifeq ($(wildcard data/WACKI.EXE),)
    EMBEDDED_PE_SRC := src/platform/3ds/embedded_wacki_pe_stub.c
endif

# 3DS uzywa wlasnych plikow platformy zamiast SDL
ENGINE_SRCS += src/platform/3ds/3ds.c \
               src/platform/3ds/platform_3ds.c \
               src/platform/3ds/video_3ds_gl.c \
               src/platform/3ds/gamepad_3ds.c \
               src/platform/3ds/storage_3ds.c \
               src/platform/3ds/data_root_3ds.c \
               src/platform/3ds/system_3ds.c \
               src/platform/3ds/audio_3ds_stub.c \
               src/platform/sdl/file_host.c \
               src/platform/sdl/flic_host.c \
               src/vm/script_obj.c \
               src/vm/parser.c

# ---- .3dsx packaging ------------------------------------------------------
3DS_ICON     := assets/icons/wacki-3ds.png
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

