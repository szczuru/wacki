# mk/vita.mk — PlayStation Vita homebrew (VitaSDK + oficjalne SDL2 / GXM).

VITASDK ?= $(shell echo $$VITASDK)
ifeq ($(VITASDK),)
    $(error VITASDK environment variable not set)
endif

CC       := $(VITASDK)/bin/arm-vita-eabi-gcc
LD       := $(VITASDK)/bin/arm-vita-eabi-gcc
BIN_NAME := wacki

CFLAGS += -D__VITA__ -DWACKI_HANDHELD -DWACKI_VITA \
          -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard \
          -ftls-model=local-exec \
          -I$(VITASDK)/arm-vita-eabi/include \
          -I$(VITASDK)/arm-vita-eabi/include/SDL2 \
          -I src/platform/sdl

# arm-vita-eabi-gcc nie zna -Wno-language-extension-token
CFLAGS := $(filter-out -Wno-language-extension-token,$(CFLAGS))
CFLAGS += -Wno-type-limits -Wno-misleading-indentation

CFLAGS_SIZE  := -Os -ffunction-sections -fdata-sections
LDFLAGS_SIZE := -Wl,--gc-sections

SDL_CFG := -I$(VITASDK)/arm-vita-eabi/include/SDL2

# Oficjalne SDL2 Vita (GXM) — bez vitaGL / OpenGL / C++
SDL_LIB := -L$(VITASDK)/arm-vita-eabi/lib \
           -lSDL2 -lSDL2_mixer \
           -lSceDisplay_stub -lSceGxm_stub \
           -lSceCtrl_stub -lSceTouch_stub -lSceHid_stub -lSceMotion_stub \
           -lSceAudio_stub -lSceAudioIn_stub \
           -lSceSysmodule_stub -lSceCommonDialog_stub \
           -lSceAppMgr_stub -lSceAppUtil_stub \
           -lScePower_stub -lSceIofilemgr_stub \
           -lSceLibKernel_stub \
           -lm -lc

LDFLAGS_STATIC := -Wl,-q

# Brak data/WACKI.EXE → stub (build bez sekretu)
ifeq ($(wildcard data/WACKI.EXE),)
    EMBEDDED_PE_SRC := src/platform/vita/embedded_wacki_pe_stub.c
endif

ENGINE_SRCS += src/platform/vita/vita.c \
               src/platform/vita/storage_vita.c \
               src/platform/vita/data_root_vita.c \
               src/platform/vita/gamepad_vita.c \
               src/platform/vita/touch_vita.c \
               src/platform/sdl/file_host.c \
               src/platform/sdl/audio_sdl.c \
               src/platform/sdl/flic_host.c \
               src/platform/sdl/video_sdl.c \
               src/platform/sdl/system_sdl.c

# ---- VPK packaging ------------------------------------------------------
# TITLE_ID: dokładnie 9 znaków (4 litery + 5 cyfr)
VITA_TITLEID  := WACK10001
VITA_APP_NAME := "Wacki: Kosmiczna rozgrywka"
VITA_VPK      := $(DIST)/wacki.vpk
VITA_SELF     := $(DIST)/eboot.bin
VITA_PARAM    := $(DIST)/param.sfo
VITA_SCE_SYS  := assets/vita/sce_sys

all: $(VITA_VPK)

$(VITA_PARAM): | $(DIST)
	vita-mksfoex -s TITLE_ID=$(VITA_TITLEID) $(VITA_APP_NAME) $@

$(VITA_SELF): $(DIST)/$(BIN_NAME) $(VITA_PARAM)
	vita-elf-create $(DIST)/$(BIN_NAME) $(DIST)/$(BIN_NAME).velf
	vita-make-fself -s $(DIST)/$(BIN_NAME).velf $@

$(VITA_VPK): $(VITA_SELF) $(VITA_PARAM)
	@set -e; \
	ADD=""; \
	if [ -f $(VITA_SCE_SYS)/icon0.png ]; then \
	  ADD="$$ADD --add $(VITA_SCE_SYS)/icon0.png=sce_sys/icon0.png"; \
	fi; \
	if [ -f $(VITA_SCE_SYS)/pic0.png ]; then \
	  ADD="$$ADD --add $(VITA_SCE_SYS)/pic0.png=sce_sys/pic0.png"; \
	fi; \
	if [ -f $(VITA_SCE_SYS)/livearea/contents/bg0.png ]; then \
	  ADD="$$ADD --add $(VITA_SCE_SYS)/livearea/contents/bg0.png=sce_sys/livearea/contents/bg0.png"; \
	fi; \
	if [ -f $(VITA_SCE_SYS)/livearea/contents/startup.png ]; then \
	  ADD="$$ADD --add $(VITA_SCE_SYS)/livearea/contents/startup.png=sce_sys/livearea/contents/startup.png"; \
	fi; \
	if [ -f $(VITA_SCE_SYS)/livearea/contents/template.xml ]; then \
	  ADD="$$ADD --add $(VITA_SCE_SYS)/livearea/contents/template.xml=sce_sys/livearea/contents/template.xml"; \
	fi; \
	if [ -d $(VITA_SCE_SYS)/manual ]; then \
	  for f in $(VITA_SCE_SYS)/manual/*; do \
	    [ -f "$$f" ] || continue; \
	    b=$$(basename "$$f"); \
	    ADD="$$ADD --add $$f=sce_sys/manual/$$b"; \
	  done; \
	fi; \
	vita-pack-vpk -s $(VITA_PARAM) -b $(VITA_SELF) $$ADD $(VITA_VPK)
