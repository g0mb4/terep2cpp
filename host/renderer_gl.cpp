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
    #include <SDL_opengl.h>
    #include "gl_renderer/renderizeitor.h"
    static RzContext* ctx;

    struct carrodef_s{
        int32_t id;
        int16_t n_points;
        uint16_t camera_pt;
    };

    carrodef_s carros[5];

    static void updateCars();
    static int isLine(uint16_t[], int);
    static void loadCar(int, std::string);

    #define F_8_24(_val) ({ \
        const float scale = 1.0f / (float)(1 << 24); \
        float vv = _val;   \
        vv * scale;        \
    })

    #define F_UV(_val) ({ \
        float vv = _val;   \
        vv / 65535.0f;     \
    })

// rzCreateWindow makes an OpenGL context of its own on the host's window and
// leaves it current, and rzDestroy deletes it.  The host (ImGui: the map
// chooser, the minimap) draws with its own context, so the library's is
// made current only around its calls, and the host's is always put back
// (otherwise, after Esc, nothing would reach the window: the chooser would
// not show and the library's last frame would stay on the screen).
static SDL_GLContext lib_gl;

static void use_lib() { SDL_GL_MakeCurrent(gui_window(), lib_gl); }

static void use_host()
{
    glFlush();                              // the library's drawing first, then the host's
    SDL_GL_MakeCurrent(gui_window(), gui_context());
}
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
    lib_gl = SDL_GL_GetCurrentContext();
    if(ok != RZ_OK){
        SDL_GL_MakeCurrent(w, gui_context());
        return false;
    }
    rzSetHeightmap(ctx, layers.height, 256,256);
    rzLoadTileAtlas(ctx, files.maptex.c_str());
    rzSetTileMap(ctx, layers.tile, 256,256);

    rzLoadFallbackTexture(ctx,files.textures.c_str());

    if (rzGetError(ctx, NULL) != RZ_OK){
        //TODO actually say the error
        rzDestroy(ctx);
        use_host();
        return false;
    }
    use_host();

    for(int i = 0; i < 5; i++){
        carros[i].id = -1;
        if(i >= G->num_cars){
            continue;
        }

        auto cpcx = files.cars[i].pcx;

        loadCar(i, cpcx);

    }

    rzSetCameraTarget(ctx, carros[0].id, 0); //TODO get the actual vertex

    updateCars();
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
        use_lib();
        updateCars();
        rzRender(ctx);
        use_host();
    #endif
    
    // LIB: draw the frame (cars[i].points, map_layers(), ...) on the whole window,
    // into framebuffer 0, without swapping
    *where = { win_w - screen_w() * ((float)win_h / screen_h()), 0, (float)win_h / screen_h() };
    return true;
}

void opengl_stop()
{
#ifdef TEREP2_GLR
    use_lib();
    rzDestroy(ctx);
    for(int i = 0; i < 5; i++){
        carros[i].id = -1;
    }
    use_host();
#endif
}

#ifdef TEREP2_GLR
static void updateCars(){
    for(int ci = 0; ci< 5 ; ci++){
        int id = carros[ci].id;
        if(id < 0){
            continue;
        }

        int n = carros[ci].n_points;
        auto car = car_get(ci);

        RzVertex ptrs[n];

        for(int i =0; i< n; i++){
            auto p = car.points[i].pos;

            ptrs[i].x = F_8_24(p.x.raw);
            ptrs[i].y = F_8_24(p.y.raw);
            ptrs[i].z = F_8_24(p.z.raw);
        }

        //TODO wheels, steering
        rzUpdateObjectVertices(ctx, id, ptrs);
    }
}

static void loadCar(int i, std::string cpcx){
    auto car = car_get(i);
    carros[i].n_points = car.points.size();

    int idCar;
    rzCreateObject(ctx, carros[i].n_points, &idCar);
    carros[i].id = idCar;

    if(cpcx != ""){
        rzLoadObjectTexture(ctx, idCar, cpcx.c_str());
    }

    for (const auto &o : car.model_ops){
        if(o.op == 4){ //flat polygon
            auto color = o.word & 0xff;
            auto pts = o.outline;

            int n = pts.size();
            uint16_t idxs[n];
            int flag = 0;

            for(int i = 0; i < n; i++){
                int v = pts[i].point;
                flag |= v & 1;
                idxs[i] = v >> 1;
            }

            if(flag){
                //shadow polygon, we dont need that
                continue;
            }

            if(isLine(idxs, n)){
                rzAddObjectLine(ctx, idCar, idxs[0], idxs[1], 0.006f, color);
                continue;
            }

            if(color == 240){
                //TODO see if there is a single tone or various
                rzAddObjectTranslucentPolygon(ctx, idCar, idxs, n, 8);
            }else{
                rzAddObjectPolygon(ctx, idCar, idxs, n, color);
            }

            continue;
        }
        if(o.op == 8){ //OP_TEXTURED
            auto pts = o.outline;

            int n = pts.size();

            RzTexVertex vtx[n];

            for(int i =0; i < n;i++){
                auto v = pts[i];
                uint32_t attr = v.attr;
                
                vtx[i].index = v.point >> 1;
                vtx[i].u = F_UV(attr & 0xFFFF);
                vtx[i].v = F_UV(attr >> 16);
                
            }
            
            rzAddObjectTexturedPolygon(ctx, idCar, vtx, n);
        }

        printf("unhandled: %d\n", o.op);
    }
}

static int isLine(uint16_t vertx[], int n){
    //checks if its a polygon with only 2 distinct vertices
    uint8_t z[256] = {0};
    for(int i = 0;i < n;i++){
        auto v = vertx[i];
        z[v] = 1;
    }
    int cnt = 0;
    for(size_t i = 0;i < sizeof(z);i++){
        cnt += z[i];
    }
    return cnt == 2;
}
#endif