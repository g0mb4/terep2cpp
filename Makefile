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

# The SDL host and its map chooser (OpenGL 3.3); Dear ImGui is the git submodule in host/imgui.
ifeq ($(OS),Windows_NT)
GL_LIBS  := -lopengl32
else
GL_LIBS  := -lGL
endif
# The external OpenGL renderer (TODO-lib.md):
#   make sdl GLR=1 GLR_DIR=its/include GLR_LIB="path/to/lib.a and what it needs"
ifdef GLR
GLR_FLAGS := -DTEREP2_GLR $(if $(GLR_DIR),-I$(GLR_DIR))
endif
SDL_HOST := host/sdl_main.cpp host/launcher.cpp host/voxel_preview.cpp host/pcx.cpp host/opl_sound.cpp host/minimap.cpp host/gui.cpp host/renderer_api.cpp \
            host/game_renderer.cpp host/renderer_sw.cpp host/renderer_gl.cpp
IMGUI    := host/imgui
IMGUI_OBJ := $(patsubst $(IMGUI)/%.cpp,build/imgui/%.o,$(addprefix $(IMGUI)/,imgui.cpp imgui_draw.cpp \
             imgui_tables.cpp imgui_widgets.cpp backends/imgui_impl_sdl2.cpp backends/imgui_impl_opengl3.cpp))

$(IMGUI)/imgui.h:
	@echo "Dear ImGui is missing: git submodule update --init" >&2; false

# Nuked-OPL3 (submodule in host/Nuked-OPL3, LGPL 2.1), the AdLib for the SDL host
OPL3     := host/Nuked-OPL3

$(OPL3)/opl3.h:
	@echo "Nuked-OPL3 is missing: git submodule update --init" >&2; false

build/opl3.o: $(OPL3)/opl3.c $(OPL3)/opl3.h
	@mkdir -p build
	$(CC) -O2 -c -o $@ $<

build/imgui/%.o: $(IMGUI)/%.cpp $(IMGUI)/imgui.h
	@mkdir -p $(dir $@)
	$(CXX) -O2 -std=gnu++17 $$(sdl2-config --cflags) -I$(IMGUI) -c -o $@ $<

sdl: terep2
terep2: $(GAME) $(HOST) $(SDL_HOST) $(wildcard host/*.hpp) $(HEADERS) $(IMGUI_OBJ) build/opl3.o
	$(CXX) $(CXXFLAGS) $(GLR_FLAGS) $$(sdl2-config --cflags) -I$(IMGUI) -I$(IMGUI)/backends -I$(OPL3) -o $@ \
		$(GAME) $(HOST) $(SDL_HOST) $(IMGUI_OBJ) build/opl3.o $(GLR_LIB) $$(sdl2-config --libs) $(GL_LIBS) -lm

test: terep2-headless
	sh tests/run_tests.sh

clean:
	rm -f terep2 terep2-headless
	rm -rf build/imgui build/opl3.o

.PHONY: all sdl test clean
