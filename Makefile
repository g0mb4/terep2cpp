# terep2 in C++ ("C with classes").
#   make            headless test host, terep2-headless (no dependencies)
#   make sdl        SDL2 version (needs SDL2 development files)
#   make test       compare the headless host with the recorded reference
CXX      ?= c++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=gnu++17 -fwrapv -Wall -Wno-address-of-packed-member -Wno-unused-result
GAME     := $(wildcard src/*.cpp)
HEADERS  := $(wildcard src/*.hpp) src/dataseg.hpp host/host.hpp src/font.pbm
HOST     := host/common.cpp

all: terep2-headless

src/dataseg.hpp: src/dataseg.def tools/gen_dataseg.py
	python3 tools/gen_dataseg.py

TEST     := $(wildcard host_test/*.cpp)

terep2-headless: $(GAME) $(HOST) $(TEST) $(HEADERS) $(wildcard host_test/*.hpp) host_test/dsmask.h
	$(CXX) $(CXXFLAGS) -o $@ $(GAME) $(HOST) $(TEST) -lm

sdl: terep2
terep2: $(GAME) $(HOST) host/sdl_main.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $$(sdl2-config --cflags) -o $@ $(GAME) $(HOST) host/sdl_main.cpp $$(sdl2-config --libs) -lm

test: terep2-headless
	sh tests/run_tests.sh

clean:
	rm -f terep2 terep2-headless

.PHONY: all sdl test clean
