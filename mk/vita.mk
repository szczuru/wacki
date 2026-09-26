# mk/vita.mk — PlayStation Vita homebrew (VitaSDK + SDL2).

VITASDK ?= $(shell echo $$VITASDK)
ifeq ($(VITASDK),)
    $(error VITASDK environment variable not set)
endif

CC       := $(VITASDK)/bin/arm-vita-eabi-gcc
BIN_NAME := wacki

CFLAGS += -D__VITA__ -DWACKI_HANDHELD -DWACKI_VITA \
          -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard \
          -ftls-model=local-exec \
          -I$(VITASDK)/arm-vita-eabi/include \
          -I$(VITASDK)/arm-vita-eabi/include/SDL2 \
          -I src/platform/sdl

CFLAGS_SIZE  := -Os -ffunction-sections -fdata-sections
LDFLAGS_SIZE := -Wl,--gc-sections

# SDL2 + wymagane stuby Vita
SDL_CFG := -I$(VITASDK)/arm-vita-eabi/include/SDL2
SDL_LIB := -L$(VITASDK)/arm-vita-eabi/lib \
           -lSDL2 -lSDL2_mixer \
           -lSceDisplay_stub -lSceGxm_stub -lSceCtrl_stub \
           -lSceTouch_stub -lSceAudio_stub -lSceAudioIn_stub \
           -lSceSysmodule_stub -lSceCommonDialog_stub \
           -lSceAppMgr_stub -lSceAppUtil_stub \
           -lScePower_stub -lSceIofilemgr_stub \
           -lSceKernelThreadMgr_stub -lSceLibKernel_stub \
           -lm -lc

LDFLAGS_STATIC := -Wl,-q

# Gdy brak data/WACKI.EXE → stub
ifeq ($(wildcard data/WACKI.EXE),)
    EMBEDDED_PE_SRC := src/platform/vita/embedded_wacki_pe_stub.c
endif

ENGINE_SRCS += src/platform/vita/vita.c \
               src/platform/vita/storage_vita.c \
               src/platform/vita/data_root_vita.c \
               src/platform/vita/gamepad_vita.c \
               src/platform/sdl/file_host.c \
               src/platform/sdl/audio_sdl.c \
               src/platform/sdl/flic_host.c \
               src/platform/sdl/video_sdl.c \
               src/platform/sdl/system_sdl.c

# ---- VPK packaging ------------------------------------------------------
VITA_TITLEID  := WACKI00001
VITA_APP_NAME := "Wacki: Kosmiczna rozgrywka"
VITA_VERSION  := $(WACKI_VERSION)
VITA_VPK      := $(DIST)/wacki.vpk
VITA_SELF     := $(DIST)/eboot.bin
VITA_PARAM    := $(DIST)/param.sfo

all: $(VITA_VPK)

$(VITA_PARAM): | $(DIST)
	vita-mksfoex -s TITLE_ID=$(VITA_TITLEID) $(VITA_APP_NAME) $@

$(VITA_SELF): $(DIST)/$(BIN_NAME) $(VITA_PARAM)
	vita-elf-create $(DIST)/$(BIN_NAME) $(DIST)/$(BIN_NAME).velf
	vita-make-fself -s $(DIST)/$(BIN_NAME).velf $@

$(VITA_VPK): $(VITA_SELF) $(VITA_PARAM)
	vita-pack-vpk -s $(VITA_PARAM) -b $(VITA_SELF) \
	    --add assets/icons/wacki-vita.png=sce_sys/icon0.png \
	    $@ 2>/dev/null || vita-pack-vpk -s $(VITA_PARAM) -b $(VITA_SELF) $@
