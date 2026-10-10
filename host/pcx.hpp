// Reading 8-bit PCX images for the host (the map preview).  The game has
// its own reader (src/load.cpp), which works on its segments; this one is
// separate on purpose, so the game code stays as it is.
#pragma once
#include <cstdint>
#include <vector>

struct PcxImage {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;    // width * height palette indices, row by row
    std::vector<uint8_t> palette;   // 768 bytes (RGB, 8 bits), empty if the file has none

    bool load(const char *path);
    uint8_t at(int x, int y) const { return pixels[(size_t)y * width + x]; }
};
