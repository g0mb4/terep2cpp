# terep2 in C++ ("C with classes").
#   make            headless test host, terep2-headless (no dependencies)
#   make sdl        SDL2 version (needs SDL2 development files and the host/imgui submodule)
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

# The SDL host and its track chooser; Dear ImGui is the git submodule in host/imgui.
SDL_HOST := host/sdl_main.cpp host/launcher.cpp host/voxel_preview.cpp host/pcx.cpp
IMGUI    := host/imgui
IMGUI_OBJ := $(patsubst $(IMGUI)/%.cpp,build/imgui/%.o,$(addprefix $(IMGUI)/,imgui.cpp imgui_draw.cpp \
             imgui_tables.cpp imgui_widgets.cpp backends/imgui_impl_sdl2.cpp backends/imgui_impl_sdlrenderer2.cpp))

$(IMGUI)/imgui.h:
	@echo "Dear ImGui is missing: git submodule update --init" >&2; false

build/imgui/%.o: $(IMGUI)/%.cpp $(IMGUI)/imgui.h
	@mkdir -p $(dir $@)
	$(CXX) -O2 -std=gnu++17 $$(sdl2-config --cflags) -I$(IMGUI) -c -o $@ $<

sdl: terep2
terep2: $(GAME) $(HOST) $(SDL_HOST) $(wildcard host/*.hpp) $(HEADERS) $(IMGUI_OBJ)
	$(CXX) $(CXXFLAGS) $$(sdl2-config --cflags) -I$(IMGUI) -I$(IMGUI)/backends -o $@ \
		$(GAME) $(HOST) $(SDL_HOST) $(IMGUI_OBJ) $$(sdl2-config --libs) -lm

test: terep2-headless
	sh tests/run_tests.sh

clean:
	rm -f terep2 terep2-headless
	rm -rf build/imgui

.PHONY: all sdl test clean
