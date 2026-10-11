// An optional minimap for the SDL host: the map in the bottom right corner,
// semi-transparent, with a numbered marker for each car.  Drawn over the
// game's picture through ImGui; it only reads the game's state.
#pragma once
#include "gui.hpp"

class Minimap {
public:
    Minimap();                      // the map's picture from the loaded game (after game_init)

    // Draws it into the bottom right corner of the game's w x h picture,
    // which is shown at (x0, y0) on the window, k window pixels per pixel.
    void draw(ImDrawList *dl, float x0, float y0, float k, int w, int h) const;

private:
    GuiTexture map;                 // 256 x 256, a pixel per cell

    void text(ImDrawList *dl, const char *s, float x, float y, float px, ImU32 c) const;
};
