EE_BIN = game.elf
EE_OBJS = src/main_ps2.o src/rt.o src/render.o src/game.o
EE_LIBS = -L$(GSKIT)/lib -lgskit -ldmakit -lpad -lm
EE_INCS += -I$(GSKIT)/include -Isrc
EE_CFLAGS += -O2

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
