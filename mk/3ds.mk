# mk/3ds.mk — Nintendo 3DS (New 3DS/2DS) using real SDL2 from devkitPro portlibs
#
# This port uses the official SDL2 for 3DS instead of a custom compatibility layer.
# SDL2 handles rendering (software), events, and audio. We add 3DS-specific extensions:
# - Dual-screen layout (top: main game, bottom: zoomed view)
# - Touch input mapping to cursor position
# - 3DS button mapping (A/B swapped like Switch)

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM

CC       := $(DEVKITARM)/bin/arm-none-eabi-gcc
BIN_NAME := wacki

# 3DS architecture flags (New 3DS/2DS XL - ARM11 MPCore)
ARCH_FLAGS := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

# 3DS-specific defines
# __3DS__ = building for 3DS
# WACKI_HANDHELD = use handheld data root scanner (SD card)
# WACKI_3DS = 3DS-specific features
# WACKI_VERBOSE = enable INFO level logging (for debugging)
CFLAGS += -D__3DS__ -DWACKI_HANDHELD -DWACKI_3DS -DWACKI_VERBOSE \
          $(ARCH_FLAGS) \
          -I$(DEVKITPRO)/libctru/include \
          -I$(DEVKITPRO)/portlibs/3ds/include

# Size optimization flags (3DS has limited RAM)
# -Os = optimize for size
# -ffunction-sections -fdata-sections = enable dead code elimination
CFLAGS_SIZE  := -Os -ffunction-sections -fdata-sections
LDFLAGS_SIZE := -Wl,--gc-sections

# SDL2 from devkitPro portlibs (real SDL2!)
# This is NOT our custom SDL_compat.c - it's the official SDL2 port
SDL_CFG := -I$(DEVKITPRO)/portlibs/3ds/include/SDL2 -D_REENTRANT
SDL_LIB := -L$(DEVKITPRO)/portlibs/3ds/lib \
           -lSDL2 \
           -lctru -lm

# Static linking flags
# -specs=3dsx.specs = use 3DSX homebrew format
LDFLAGS_STATIC := -specs=3dsx.specs \
                   -L$(DEVKITPRO)/libctru/lib \
                   -L$(DEVKITPRO)/portlibs/3ds/lib

# 3DS platform sources
# We reuse the SDL platform layer (src/platform/sdl/*) and add only 3DS-specific code:
# - video_3ds.c: Dual-screen rendering wrapper around SDL2
# - gamepad_3ds.c: 3DS button/touch input (replaces gamepad_sdl.c)
# - system_3ds.c: 3DS system hooks (osSetSpeedupEnable, etc.)
PLATFORM_SRCS = src/platform/sdl/save_host.c \
                src/platform/sdl/file_host.c \
                src/platform/sdl/audio_sdl.c \
                src/platform/sdl/flic_host.c \
                $(SDL_DATAROOT_HANDHELD) \
                src/platform/3ds/video_3ds.c \
                src/platform/3ds/gamepad_3ds.c \
                src/platform/3ds/system_3ds.c
