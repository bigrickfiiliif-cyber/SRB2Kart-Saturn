#---------------------------------------------------------------------------------
# SRB2Kart Saturn - Nintendo 3DS (New 3DS / New 2DS XL) build
#   make            -> srb2kart-saturn.3dsx
#   make cia        -> srb2kart-saturn.cia (needs makerom + bannertool in PATH)
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif
include $(DEVKITARM)/3ds_rules

TARGET      := srb2kart-saturn
BUILD       := build
SRCROOT     := ../src
APP_TITLE   := SRB2Kart Saturn
APP_DESCRIPTION := SRB2Kart Saturn client for New 3DS
APP_AUTHOR  := Kart Krew / Saturn team / 3DS port
APP_ICON    := $(CURDIR)/icon.png

# ---- source list ---------------------------------------------------------------
list = $(addprefix $(dir $(1)),$(filter-out \#%,$(shell cat $(1))))
CORESRC  := $(call list,$(SRCROOT)/Sourcefile) \
            $(call list,$(SRCROOT)/blua/Sourcefile) \
            $(call list,$(SRCROOT)/core/Sourcefile) \
            $(SRCROOT)/md5.c \
            $(SRCROOT)/comptime.c
PLATSRC  := $(wildcard $(SRCROOT)/n3ds/*.c) $(wildcard $(SRCROOT)/n3ds/*.cpp)
SOURCES  := $(CORESRC) $(PLATSRC)
OFILES   := $(patsubst $(SRCROOT)/%,$(BUILD)/%.o,$(SOURCES))
DEPS     := $(OFILES:.o=.d)

# ---- flags ---------------------------------------------------------------------
ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
DEFINES  := -D__3DS__ -DN3DS -DUNIXCOMMON -DNOHW -DHAVE_PNG -DHAVE_ZLIB \
            -DDOSATURNJOIN -DDOSATURNPAK -DHAVE_THREADS -DNOMUMBLE \
            -DHAVE_CURL -DCOMPVERSION -D_GNU_SOURCE
# arm-none-eabi newlib makes int32_t a 'long'; the engine assumes int32_t==int
# (C++ overloads, std::min/max). Same size/ABI, so retarget the typedef.
DEFINES  += -U__INT32_TYPE__ -D__INT32_TYPE__=int \
            -U__UINT32_TYPE__ '-D__UINT32_TYPE__=unsigned int' \
            -U__INT_LEAST32_TYPE__ -D__INT_LEAST32_TYPE__=int \
            -U__UINT_LEAST32_TYPE__ '-D__UINT_LEAST32_TYPE__=unsigned int'
# No -ffast-math: float results must match PC builds bit-for-bit or netgames
# with PC players desync. -fsigned-char: ARM's char is unsigned by default,
# the engine (and its network/demo formats) assume x86 semantics.
OPT      ?= -O2 -fno-strict-aliasing -fomit-frame-pointer -fsigned-char
WARN     := -Wall -Wno-unused -Wno-unused-parameter -Wno-sign-compare \
            -Wno-missing-field-initializers -Wno-format-truncation \
            -Wno-stringop-truncation -Wno-address-of-packed-member
INCLUDE  := -I$(CTRULIB)/include -I$(PORTLIBS)/include -I$(SRCROOT) -I$(BUILD)
CFLAGS   := -g $(OPT) $(WARN) $(ARCH) $(DEFINES) $(INCLUDE) -mword-relocations \
            -ffunction-sections -fdata-sections
CXXFLAGS := $(CFLAGS) -std=gnu++17
CFLAGS   += -std=gnu11
LDFLAGS  := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(TARGET).map -Wl,--gc-sections
LIBS     := -L$(PORTLIBS)/lib -L$(CTRULIB)/lib -lcurl -lmbedtls -lmbedx509 -lmbedcrypto \
            -lxmp -lvorbisidec -logg -lpng -lz -lctru -lm

# Renderer hot paths, built without the thread-pool code: with HAVE_THREADS
# the span drawer uses thread_local state (a function call per access with
# -mtp=soft) and planes are split into tiny lambda tasks that copy the whole
# span context. On the ARM11 that overhead costs more than a second core
# gains, so use the engine's direct single-threaded paths instead.
RENDER_SERIAL := $(BUILD)/r_plane.cpp.o $(BUILD)/r_things.cpp.o $(BUILD)/r_draw.cpp.o
$(RENDER_SERIAL): CXXFLAGS += -UHAVE_THREADS
# Column/span drawers (r_draw_span/column.cpp are #included here): unroll.
RENDER_HOT := $(BUILD)/r_draw.cpp.o
$(RENDER_HOT): CXXFLAGS += -O3

# ---- rules ---------------------------------------------------------------------
.PHONY: all clean cia
all: $(TARGET).3dsx

$(BUILD)/comptime.h:
	@mkdir -p $(BUILD)
	@printf 'const char* compbranch = "3ds";\nconst char* comprevision = "%s";\n' \
	  "$$(cd .. && git rev-parse --short HEAD 2>/dev/null || echo unknown)" > $@

$(BUILD)/%.c.o: $(SRCROOT)/%.c | $(BUILD)/comptime.h
	@mkdir -p $(dir $@)
	@echo CC $<
	@$(CC) -MMD -MP $(CFLAGS) -c $< -o $@

$(BUILD)/%.cpp.o: $(SRCROOT)/%.cpp | $(BUILD)/comptime.h
	@mkdir -p $(dir $@)
	@echo CXX $<
	@$(CXX) -MMD -MP $(CXXFLAGS) -c $< -o $@

$(TARGET).elf: $(OFILES)
	@echo LD $@
	@$(CXX) $(LDFLAGS) $(OFILES) $(LIBS) -o $@

$(TARGET).smdh: $(APP_ICON)
	@smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $(APP_ICON) $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh $(wildcard romfs/*)
	@3dsxtool $< $@ --smdh=$(TARGET).smdh --romfs=romfs
	@echo built ... $@

cia: $(TARGET).elf $(TARGET).smdh
	bannertool makebanner -i banner.png -a banner.wav -o $(BUILD)/banner.bnr
	makerom -f cia -o $(TARGET).cia -elf $(TARGET).elf -rsf cia.rsf \
	  -icon $(TARGET).smdh -banner $(BUILD)/banner.bnr -exefslogo -target t

clean:
	rm -rf ./$(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh $(TARGET).map $(TARGET).cia

-include $(DEPS)
