// An optional minimap for the SDL host: the map in the bottom right corner,
// semi-transparent, with a numbered marker for each car.  Drawn over the
// game's picture with the SDL renderer; it only reads the game's state.
#pragma once
#include <SDL.h>

class Minimap {
public:
    // The map's picture from the loaded game (call after game_init).
    explicit Minimap(SDL_Renderer *ren);
    ~Minimap();
    Minimap(const Minimap &) = delete;
    Minimap &operator=(const Minimap &) = delete;

    // Draws it into the bottom right corner of a w x h picture.
    void draw(int w, int h) const;

private:
    SDL_Renderer *ren;
    SDL_Texture *map = nullptr;     // 256 x 256, a pixel per cell

    void text(const char *s, int x, int y, int px, SDL_Color c) const;
};
