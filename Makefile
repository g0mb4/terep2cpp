# terep2 in C.
#   make            headless test host (no dependencies)
#   make sdl        SDL2 version (needs SDL2 development files)
#   make test       compare the headless host with the recorded reference
CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -fwrapv -Wall -Wno-address-of-packed-member -Wno-unused-result
GAME    := $(wildcard src/*.c)
HOST    := host/common.c

all: terep2-headless

src/dataseg.h: src/dataseg.def tools/gen_dataseg.py
	python3 tools/gen_dataseg.py

terep2-headless: $(GAME) $(HOST) host/headless.c src/*.h host/host.h tests/dsmask.h
	$(CC) $(CFLAGS) -o $@ $(GAME) $(HOST) host/headless.c -lm

sdl: terep2
terep2: $(GAME) $(HOST) host/sdl_main.c src/*.h host/host.h
	$(CC) $(CFLAGS) $$(sdl2-config --cflags) -o $@ $(GAME) $(HOST) host/sdl_main.c $$(sdl2-config --libs) -lm

test: terep2-headless
	sh tests/run_tests.sh

clean:
	rm -f terep2 terep2-headless

.PHONY: all sdl test clean
