# PROJECT NOISE DSI - Nintendo DS / DSi homebrew
# Requires devkitPro devkitARM + libnds + libfat (nds-dev group).
ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO is not set")
endif
ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set")
endif

include $(DEVKITARM)/ds_rules

GAME_TITLE := PROJECT NOISE DSI
GAME_SUBTITLE1 := MADE BY GRIMZZZZ
GAME_SUBTITLE2 := DSi MP3 Player
GAME_ICON := resources/icon.bmp

TARGET := project_noise_dsi
BUILD := build
SOURCES := source
INCLUDES := include
LIBDIRS := $(LIBNDS)
LIBS := -lmm9 -lfat -lnds9

CFLAGS := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections
CFLAGS += -march=armv5te -mtune=arm946e-s -mthumb -DARM9
CFLAGS += -I$(CURDIR)/include

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export OFILES := $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-iquote $(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
.PHONY: all clean deps
all: deps $(BUILD)
deps:
	@mkdir -p source
	@if [ ! -s source/minimp3.h ]; then echo "Fetching minimp3 (CC0)..."; curl -fsSL https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h -o source/minimp3.h || exit 1; fi
$(BUILD):
	mkdir -p $@
	$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nds $(TARGET).ds.gba
else
DEPENDS := $(OFILES:.o=.d)
$(OUTPUT).nds: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
mp3impl.o: CFLAGS += -marm -O3 -fno-strict-aliasing
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDE) -MMD -MP -c $< -o $@
-include $(DEPENDS)
endif
