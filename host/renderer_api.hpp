// What an external renderer (an OpenGL library drawing the game its own
// way) gets from the SDL host.  No ImGui in here.
//
// Context: the host owns the window and its OpenGL context (gui.hpp:
// gui_window(), gui_context(); OpenGL 3.3 core profile, double buffered, vsync).  The
// renderer draws into that context, into framebuffer 0, and does NOT swap:
// in the game loop (run() in sdl_main.cpp) it would draw where the game's
// own picture is drawn now, then the host draws its overlay (ImGui: the
// minimap, menus) and calls SDL_GL_SwapWindow.  The ImGui OpenGL3 backend
// saves and restores the GL state it touches, so the renderer only has to
// leave framebuffer 0 bound.  See TODO-lib.md for the rest.
#pragma once
#include <string>
#include <vector>
#include "host.hpp"

// The files of the loaded map, as they are on disk (the original's names
// come in any mix of upper and lower case), with their directory: absolute
// paths, or "" for a file that is not there.  Valid after game_init.
struct MapFiles {
    std::string dir;                // the map's directory
    std::string map;                // heights (map.pcx)
    std::string col;                // tile/colour of each cell, and the palette (col.pcx)
    std::string maptex;             // the 16 x 16 tiles of the terrain (maptex.pcx)
    std::string textures;           // smoke, sparks, and the cars without a skin (textures.pcx)
    std::string fog, smoke;         // the fog and translucency tables (fog.dat, smoke.dat)
    struct Car {
        std::string dat;            // carN.dat
        std::string pcx;            // carN.pcx, its skin ("" if the car uses textures.pcx)
    };
    std::vector<Car> cars;          // the loaded cars, in order (car i is cars[i])
};
MapFiles map_files(const std::string &dir);

// The two map layers, 256 x 256 bytes each, a byte per grid point / cell,
// at [x | y << 8] (row y, x across; x and y are a position's Coord::cell()).
// They belong to the game and change while it runs: ice chipped by the
// cars lowers a height by one and the tile's low nibble by one.  Read them
// again each frame (64 KB each) or watch for changes.
struct MapLayers {
    const uint8_t *height;          // map.pcx: height of the grid point, in steps of 16 units
                                    // (a cell is 256 units across)
    const uint8_t *tile;            // col.pcx: low nibble = column, high nibble = row of the
                                    // cell's 16 x 16 tile in maptex.pcx; also the palette
                                    // index of the cell seen far away; >> 4 = surface type
};
inline MapLayers map_layers() { return { heightmap(), colormap() }; }
