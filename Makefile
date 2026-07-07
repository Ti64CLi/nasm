DEBUG = FALSE

GCC = nspire-gcc
AS  = nspire-as
GXX = nspire-g++
LD  = nspire-ld
GENZEHN = genzehn

GCCFLAGS = -Wall -W -marm
LDFLAGS =
ZEHNFLAGS = --name "nasm" --240x320-support true --uses-lcd-blit true

ifeq ($(DEBUG),FALSE)
	GCCFLAGS += -Os
else
	GCCFLAGS += -O0 -g
endif

# header dependency tracking
GCCFLAGS += -MMD -MP

OBJS = $(patsubst %.c, %.o, $(shell find . -path ./tests -prune -o -name \*.c -print))
OBJS += $(patsubst %.cpp, %.o, $(shell find . -path ./tests -prune -o -name \*.cpp -print))
OBJS += $(patsubst %.S, %.o, $(shell find . -path ./tests -prune -o -name \*.S -print))
EXE = nasm
DISTDIR = .
vpath %.tns $(DISTDIR)
vpath %.elf $(DISTDIR)

all: $(EXE).tns

test:
	@sh tests/run_tests.sh

.PHONY: all clean test

%.o: %.c
	$(GCC) $(GCCFLAGS) -c $< -o $@

%.o: %.cpp
	$(GXX) $(GCCFLAGS) -c $< -o $@
	
%.o: %.S
	$(AS) -c $< -o $@

$(EXE).elf: $(OBJS)
	mkdir -p $(DISTDIR)
	$(LD) $^ -o $@ $(LDFLAGS)

$(EXE).tns: $(EXE).elf
	$(GENZEHN) --input $^ --output $@.zehn $(ZEHNFLAGS)
	make-prg $@.zehn $@
	rm $@.zehn

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(DISTDIR)/$(EXE).tns $(DISTDIR)/$(EXE).elf $(DISTDIR)/$(EXE).zehn

-include $(OBJS:.o=.d)
