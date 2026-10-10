// A quick look at a map for the launcher: the terrain drawn the way
// Comanche did ("voxel space"), with a camera circling the map.  Software,
// RGBA, independent from the game's renderer.
#pragma once
#include <cstdint>
#include <string>

class VoxelPreview {
public:
    // Reads map.pcx (heights) and col.pcx (colour of each cell, and the
    // palette) from a map directory.  False if they are not both there.
    bool load(const std::string &dir);
    bool loaded() const { return ok; }

    // Draws the terrain seen from the camera at angle (radians) around
    // the middle of the map into rgba (w * h pixels, 0xAABBGGRR).
    void render(uint32_t *rgba, int w, int h, float angle) const;

private:
    bool ok = false;
    uint8_t height[256][256];       // map.pcx: height of each grid point (x 16 map units)
    uint32_t colour[256][256];      // col.pcx through its palette
    int max_height = 0;
};

// The file in dir whose name matches name ignoring case ("" if none).
std::string find_file_nocase(const std::string &dir, const char *name);
