# mk/3ds.mk — Nintendo 3DS with NovaGL (OpenGL ES 1.1 → citro3d hardware rendering)

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM

CC       := $(DEVKITARM)/bin/arm-none-eabi-gcc
CXX      := $(DEVKITARM)/bin/arm-none-eabi-g++
BIN_NAME := wacki

# NovaGL paths
NOVAGL_DIR := external/NovaGL
NOVAGL_INC := $(NOVAGL_DIR)/include
NOVAGL_LIB := $(NOVAGL_DIR)/lib

# 3DS architecture flags
ARCH_FLAGS := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

# 3DS-specific defines
CFLAGS += -D__3DS__ -DWACKI_HANDHELD -DWACKI_3DS -DWACKI_VERBOSE \
          $(ARCH_FLAGS) \
          -I$(DEVKITPRO)/libctru/include \
          -I$(DEVKITPRO)/portlibs/3ds/include \
          -I$(NOVAGL_INC)

# C++ flags for NovaGL
CXXFLAGS := $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions

# Size optimization
CFLAGS_SIZE  := -Os -ffunction-sections -fdata-sections
LDFLAGS_SIZE := -Wl,--gc-sections

# NovaGL + citro3d + libctru
LIBS_3DS := -L$(NOVAGL_LIB) -lNovaGL \
            -lcitro2d -lcitro3d \
            -lctru -lm

# Static linking flags
LDFLAGS_STATIC := -specs=3dsx.specs \
                   -L$(DEVKITPRO)/libctru/lib \
                   -L$(DEVKITPRO)/portlibs/3ds/lib \
                   $(LIBS_3DS)

# Platform sources
PLATFORM_SRCS = src/platform/sdl/save_host.c \
                src/platform/sdl/file_host.c \
                src/platform/sdl/audio_sdl.c \
                src/platform/sdl/flic_host.c \
                $(SDL_DATAROOT_HANDHELD) \
                src/platform/3ds/video_3ds_gl.c \
                src/platform/3ds/gamepad_3ds.c \
                src/platform/3ds/system_3ds.c

# SDL stub
SDL_CFG := -I$(DEVKITPRO)/portlibs/3ds/include
SDL_LIB :=
