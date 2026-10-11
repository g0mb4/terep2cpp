/*
 * The external OpenGL renderer (Harrison's library).  The library draws the
 * map and the cars its own way, with its own camera and textures, into the
 * host's OpenGL 3.3 core context; the game's own drawing (game_frame) is not
 * called, so what game_frame does besides drawing is done here: the engine
 * sound and the keys that change the game (Tab, Q, [, ], F9).
 *
 * The calls into the library are marked LIB: they are compiled only with
 * the library (make sdl GLR=1 ...).  What else it will need: TODO-lib.md.
 */
#include "game_renderer.hpp"
#include "gui.hpp"
#include "renderer_api.hpp"

#ifdef TEREP2_GLR
    #include "gl_renderer/renderizeitor.h"
    static RzContext* ctx;
#endif

bool opengl_start()
{
#ifdef TEREP2_GLR
    MapFiles files = map_files(".");        // real paths of the map's files
    MapLayers layers = map_layers();        // height and tile, 256 x 256, change while playing
    (void)files;
    (void)layers;

    auto w = gui_window();

    int ok = rzCreateWindow(w, &ctx);
    if(ok != RZ_OK){
        return false;
    }
    rzSetHeightmap(ctx, layers.height, 256,256);
    rzLoadTileAtlas(ctx, files.maptex.c_str());
    rzSetTileMap(ctx, layers.tile, 256,256);

    rzLoadFallbackTexture(ctx,files.textures.c_str());

    if (rzGetError(ctx, NULL) != RZ_OK){
        //TODO actually say the error
        return false;
    }

    //TODO continuacao
    
    return true;
#else
    return false;
#endif
}

// The keys game_frame would handle (keys[0] is the last key pressed); the
// cameras' keys are the library's business.
static void game_keys()
{
    switch (keys[0]) {
    case 0x0f:                              // Tab: next car for view 1
        if (++G->player_car[0] >= (uint16_t)G->num_cars)
            G->player_car[0] = 0;
        break;
    case 0x10:                              // Q: next car for view 2
        if (++G->player_car[1] >= (uint16_t)G->num_cars)
            G->player_car[1] = 0;
        break;
    case 0x1a:                              // [ and ]: gravity
        G->gravity += 50;
        break;
    case 0x1b:
        G->gravity -= 50;
        break;
    case 0x43:                              // F9: split screen
        G->split_screen ^= 1;
        break;
    }
    keys[0] = 0;
}

bool opengl_frame(int win_w, int win_h, PictureRect *where)
{
    game_keys();
    engine_sound(G->player_car[0], 0);      // what draw_view does for the sound

    #ifdef TEREP2_GLR
        rzRender(ctx);
    #endif
    
    // LIB: draw the frame (cars[i].points, map_layers(), ...) on the whole window,
    // into framebuffer 0, without swapping
    *where = { win_w - screen_w() * ((float)win_h / screen_h()), 0, (float)win_h / screen_h() };
    return true;
}

void opengl_stop()
{
#ifdef TEREP2_GLR
    rzDestroy(ctx);
#endif
}
