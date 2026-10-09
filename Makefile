# terep2 in C++ ("C with classes").
#   make            headless test host (no dependencies)
#   make sdl        SDL2 version (needs SDL2 development files)
#   make test       compare the headless host with the recorded reference
CXX      ?= c++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=gnu++17 -fwrapv -Wall -Wno-address-of-packed-member -Wno-unused-result
GAME     := $(wildcard src/*.cpp)
HEADERS  := $(wildcard src/*.hpp) src/dataseg.hpp host/host.hpp
HOST     := host/common.cpp

all: terep2-headless

src/dataseg.hpp: src/dataseg.def tools/gen_dataseg.py
	python3 tools/gen_dataseg.py

terep2-headless: $(GAME) $(HOST) host/headless.cpp $(HEADERS) tests/dsmask.h
	$(CXX) $(CXXFLAGS) -o $@ $(GAME) $(HOST) host/headless.cpp -lm

sdl: terep2
terep2: $(GAME) $(HOST) host/sdl_main.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $$(sdl2-config --cflags) -o $@ $(GAME) $(HOST) host/sdl_main.cpp $$(sdl2-config --libs) -lm

test: terep2-headless
	sh tests/run_tests.sh

clean:
	rm -f terep2 terep2-headless

.PHONY: all sdl test clean
