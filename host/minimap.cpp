/*
 * The minimap: each cell of the map in its colour (col.pcx through the
 * game's palette), darker where it is low, x to the right and y down, as
 * the map is laid out (a camera with yaw 0 looks up the picture).  A car
 * is a square in its own colour at its reference point (where the camera
 * of F1 sits), with its number next to it in the game's font.
 */
#include <cstdio>
#include "host.hpp"
#include "minimap.hpp"

static const SDL_Color CAR_COLOURS[10] = {
    { 255, 60, 60, 255 }, { 255, 230, 40, 255 }, { 60, 220, 255, 255 }, { 255, 80, 255, 255 },
    { 80, 255, 90, 255 }, { 255, 150, 30, 255 }, { 255, 255, 255, 255 }, { 120, 120, 255, 255 },
    { 180, 255, 180, 255 }, { 255, 180, 200, 255 },
};
constexpr Uint8 MAP_ALPHA = 150;        // about 60 %

Minimap::Minimap(SDL_Renderer *ren) : ren(ren)
{
    map = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, 256, 256);
    if (!map)
        return;
    static uint32_t pixels[256 * 256];
    for (int i = 0; i < 256 * 256; i++) {
        const uint8_t *rgb = host_palette(colormap()[i]);
        int light = 150 + heightmap()[i] * 106 / 255;          // 150..256: low ground darker
        pixels[i] = 0xff000000u | (uint32_t)(rgb[2] * light >> 8) << 16 |
                    (uint32_t)(rgb[1] * light >> 8) << 8 | (uint32_t)(rgb[0] * light >> 8);
    }
    SDL_UpdateTexture(map, nullptr, pixels, 256 * 4);
    SDL_SetTextureBlendMode(map, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(map, MAP_ALPHA);
}

Minimap::~Minimap()
{
    SDL_DestroyTexture(map);
}

// In the game's own font (the glyphs in the data segment: columns, bit 0 at the top).
void Minimap::text(const char *s, int x, int y, int px, SDL_Color c) const
{
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
    for (; *s; s++, x += px) {
        const uint8_t *col = &G->font_glyphs[G->font_offset[(uint8_t)*s & 0x7f]];
        for (; *col; col++, x += px)
            for (int row = 0; row < 5; row++)
                if (*col >> row & 1) {
                    SDL_Rect r = { x, y + row * px, px, px };
                    SDL_RenderFillRect(ren, &r);
                }
    }
}

void Minimap::draw(int w, int h) const
{
    if (!map)
        return;
    const int size = h * 3 / 10, margin = h / 50;
    const SDL_Rect area = { w - size - margin, h - size - margin, size, size };
    SDL_RenderCopy(ren, map, nullptr, &area);

    const int mark = size / 80 > 1 ? size / 80 : 1, px = h / 200 > 1 ? h / 200 : 1;   // mark: half the square
    for (int i = 0; i < G->num_cars; i++) {
        const Pos &p = car_get(i).ref_vertex().pos;
        int x = area.x + (int)((p.x.raw >> 8) * (uint64_t)size >> 24);   // 8.24 cells -> pixels
        int y = area.y + (int)((p.y.raw >> 8) * (uint64_t)size >> 24);
        SDL_Color c = CAR_COLOURS[i % 10];
        SDL_Rect frame = { x - mark - 1, y - mark - 1, 2 * mark + 3, 2 * mark + 3 }, dot = { x - mark, y - mark, 2 * mark + 1, 2 * mark + 1 };
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderFillRect(ren, &frame);
        SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
        SDL_RenderFillRect(ren, &dot);
        char n[4];
        snprintf(n, sizeof n, "%d", i + 1);
        text(n, x + mark + 2 * px, y - 2 * px, px, c);
    }
}
