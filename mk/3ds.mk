# mk/3ds.mk — Nintendo 3DS homebrew (devkitARM + libctru + citro3d).

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM

CC       := $(DEVKITARM)/bin/arm-none-eabi-gcc
BIN_NAME := wacki

CFLAGS += -D__3DS__ -DWACKI_HANDHELD -DWACKI_3DS \
          -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
          -I$(DEVKITPRO)/libctru/include \
          -I$(DEVKITPRO)/portlibs/3ds/include \
          -I src/platform/3ds

CFLAGS_SIZE  := -Os -ffunction-sections -fdata-sections
LDFLAGS_SIZE := -Wl,--gc-sections

# 3DS libraries: citro3d/citro2d for graphics, ctru for system
LIBS_3DS := -lcitro2d -lcitro3d -lctru -lm

LDFLAGS_STATIC := -L$(DEVKITPRO)/libctru/lib \
                   -L$(DEVKITPRO)/portlibs/3ds/lib \
                   -specs=3dsx.specs \
                   $(LIBS_3DS)

# Jesli data/WACKI.EXE istnieje (CI z sekretem / lokalne budowanie),
# uzywamy standardowego embed-pe-data. W przeciwnym razie - pusty stub.
ifeq ($(wildcard data/WACKI.EXE),)
    EMBEDDED_PE_SRC := src/platform/3ds/embedded_wacki_pe_stub.c
endif

# 3DS uses SDL compatibility layer (SDL_compat.c) + custom gamepad.
# This allows reusing SDL platform code (video_sdl.c, audio_sdl.c, platform_sdl.c)
# while providing dual-screen rendering and custom controls via the compat layer.

# SDL compatibility layer - provides SDL API on top of citro3d/citro2d/ndsp
SDL_COMPAT_SRCS := src/platform/3ds/SDL_compat.c

# 3DS-specific platform files
N3DS_PLATFORM_SRCS := src/platform/3ds/3ds.c \
                      src/platform/3ds/storage_3ds.c \
                      src/platform/3ds/data_root_3ds.c \
                      src/platform/3ds/gamepad_3ds.c \
                      src/platform/3ds/system_3ds.c

# Reuse SDL platform implementations (they use our SDL.h via -I src/platform/3ds)
SDL_PLATFORM_SRCS := src/platform/sdl/platform_sdl.c \
                     src/platform/sdl/video_sdl.c \
                     src/platform/sdl/audio_sdl.c \
                     src/platform/sdl/file_host.c \
                     src/platform/sdl/flic_host.c

ENGINE_SRCS += $(SDL_COMPAT_SRCS) $(N3DS_PLATFORM_SRCS) $(SDL_PLATFORM_SRCS)

# ---- .3dsx packaging ------------------------------------------------------
N3DS_ICON     := assets/icons/wacki-3ds-48x48.png
N3DS_3DSX     := $(DIST)/wacki.3dsx
N3DS_SMDH     := $(DIST)/wacki.smdh
SMDHTOOL      := $(DEVKITPRO)/tools/bin/smdhtool
N3DSXTOOL     := $(DEVKITPRO)/tools/bin/3dsxtool

all: $(N3DS_3DSX)

$(N3DS_SMDH): | $(DIST)
	$(SMDHTOOL) --create "Wacki: Kosmiczna rozgrywka" "Wacki game engine - New 3DS port" "mszula" $(N3DS_ICON) $(N3DS_SMDH)

$(N3DS_3DSX): $(DIST)/$(BIN_NAME)$(EXE) $(N3DS_SMDH)
	$(N3DSXTOOL) $(DIST)/$(BIN_NAME)$(EXE) $(N3DS_3DSX) --smdh=$(N3DS_SMDH)
