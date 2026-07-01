# mk/3ds.mk — Nintendo 3DS homebrew (devkitARM + libctru + SDL2).

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM

CC       := $(DEVKITARM)/bin/arm-none-eabi-gcc
BIN_NAME := wacki

# SDL2 for 3DS
N3DS_SDL2_CONFIG := $(DEVKITPRO)/portlibs/3ds/bin/arm-none-eabi-pkg-config
SDL2_CFLAGS := $(shell PKG_CONFIG_PATH=$(DEVKITPRO)/portlibs/3ds/lib/pkgconfig $(N3DS_SDL2_CONFIG) --cflags sdl2)
SDL2_LIBS   := $(shell PKG_CONFIG_PATH=$(DEVKITPRO)/portlibs/3ds/lib/pkgconfig $(N3DS_SDL2_CONFIG) --libs sdl2)

CFLAGS += -D__3DS__ -DWACKI_HANDHELD -DWACKI_3DS \
          -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
          -I$(DEVKITPRO)/libctru/include \
          -I$(DEVKITPRO)/portlibs/3ds/include \
          -I src/platform/3ds \
          $(SDL2_CFLAGS)

CFLAGS_SIZE  := -Os -ffunction-sections -fdata-sections
LDFLAGS_SIZE := -Wl,--gc-sections

LDFLAGS_STATIC := -L$(DEVKITPRO)/libctru/lib \
                   -L$(DEVKITPRO)/portlibs/3ds/lib \
                   -specs=3dsx.specs \
                   $(SDL2_LIBS) -lctru -lm

# Jesli data/WACKI.EXE istnieje, uzywamy embed-pe-data
ifeq ($(wildcard data/WACKI.EXE),)
    EMBEDDED_PE_SRC := src/platform/3ds/embedded_wacki_pe_stub.c
endif

# 3DS uzywa wlasnego gamepad_3ds.c ale standardowego SDL video/audio
ENGINE_SRCS += src/platform/3ds/3ds.c \
               src/platform/3ds/storage_3ds.c \
               src/platform/3ds/data_root_3ds.c \
               src/platform/3ds/gamepad_3ds.c \
               src/platform/3ds/system_3ds.c \
               src/platform/sdl/platform_sdl.c \
               src/platform/sdl/file_host.c \
               src/platform/sdl/audio_sdl.c \
               src/platform/sdl/video_sdl.c \
               src/platform/sdl/flic_host.c

# ---- .3dsx packaging ------------------------------------------------------
N3DS_ICON     := assets/icons/wacki-3ds.png
N3DS_3DSX     := $(DIST)/wacki.3dsx
N3DS_SMDH     := $(DIST)/wacki.smdh
SMDHTOOL      := $(DEVKITPRO)/tools/bin/smdhtool
N3DSXTOOL     := $(DEVKITPRO)/tools/bin/3dsxtool

all: $(N3DS_3DSX)

$(N3DS_SMDH): | $(DIST)
	$(SMDHTOOL) --create "Wacki: Kosmiczna rozgrywka" "Wacki game engine - New 3DS port" "mszula" $(N3DS_ICON) $(N3DS_SMDH)

$(N3DS_3DSX): $(DIST)/$(BIN_NAME)$(EXE) $(N3DS_SMDH)
	$(N3DSXTOOL) $(DIST)/$(BIN_NAME)$(EXE) $(N3DS_3DSX) --smdh=$(N3DS_SMDH)
