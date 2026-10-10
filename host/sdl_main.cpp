/*
 * SDL2 host: a window, the keyboard and the original timing (physics at
 * 120 Hz, display at 60 Hz, as in the Win16 port).
 *
 *   terep2 [-s scale] [-F fix]... [trackdir]      (default: data/Asphalt)
 *
 * scale: render at 320*scale x 200*scale (1 = the original picture).
 * -F fix: turn on a fix of a bug of the original (see fix_list), or "all".
 *
 * Esc quits.  The AdLib sound is not played.
 */
#include <cstdlib>
#include <string_view>
#include <unistd.h>
#include <vector>
#include <SDL.h>
#include "host.hpp"

constexpr int MIN_ZOOM = 3;                // a 320x200 picture opens at least this many times bigger

// Initial window size: the picture at the chosen scale, enlarged by a whole
// factor while it is smaller than 320x200 x MIN_ZOOM, then reduced by a
// whole factor while it does not fit on the screen.
static void window_size(int *w, int *h)
{
    int zoom = (MIN_ZOOM + render_scale - 1) / render_scale, div = 1;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
        auto fits = [&](int z, int d) { return screen_w() * z / d <= usable.w && screen_h() * z / d <= usable.h; };
        while (zoom > 1 && !fits(zoom, 1))
            zoom--;
        while (!fits(zoom, div) && div < render_scale)
            div++;
    }
    *w = screen_w() * zoom / div;
    *h = screen_h() * zoom / div;
}

// SDL scancode -> PC keyboard scancode (set 1), what the game expects.
static uint8_t pc_scancode(SDL_Scancode s)
{
    static const struct { SDL_Scancode sdl; uint8_t pc; } map[] = {
        { SDL_SCANCODE_ESCAPE, 0x01 },
        { SDL_SCANCODE_1, 0x02 }, { SDL_SCANCODE_2, 0x03 }, { SDL_SCANCODE_3, 0x04 },
        { SDL_SCANCODE_4, 0x05 }, { SDL_SCANCODE_5, 0x06 }, { SDL_SCANCODE_6, 0x07 },
        { SDL_SCANCODE_7, 0x08 }, { SDL_SCANCODE_8, 0x09 }, { SDL_SCANCODE_9, 0x0a },
        { SDL_SCANCODE_0, 0x0b }, { SDL_SCANCODE_MINUS, 0x0c }, { SDL_SCANCODE_EQUALS, 0x0d },
        { SDL_SCANCODE_BACKSPACE, 0x0e }, { SDL_SCANCODE_TAB, 0x0f },
        { SDL_SCANCODE_Q, 0x10 }, { SDL_SCANCODE_W, 0x11 }, { SDL_SCANCODE_E, 0x12 },
        { SDL_SCANCODE_R, 0x13 }, { SDL_SCANCODE_T, 0x14 }, { SDL_SCANCODE_Y, 0x15 },
        { SDL_SCANCODE_U, 0x16 }, { SDL_SCANCODE_I, 0x17 }, { SDL_SCANCODE_O, 0x18 },
        { SDL_SCANCODE_P, 0x19 }, { SDL_SCANCODE_LEFTBRACKET, 0x1a }, { SDL_SCANCODE_RIGHTBRACKET, 0x1b },
        { SDL_SCANCODE_RETURN, 0x1c }, { SDL_SCANCODE_LCTRL, 0x1d }, { SDL_SCANCODE_RCTRL, 0x1d },
        { SDL_SCANCODE_A, 0x1e }, { SDL_SCANCODE_S, 0x1f }, { SDL_SCANCODE_D, 0x20 },
        { SDL_SCANCODE_F, 0x21 }, { SDL_SCANCODE_G, 0x22 }, { SDL_SCANCODE_H, 0x23 },
        { SDL_SCANCODE_J, 0x24 }, { SDL_SCANCODE_K, 0x25 }, { SDL_SCANCODE_L, 0x26 },
        { SDL_SCANCODE_SEMICOLON, 0x27 }, { SDL_SCANCODE_APOSTROPHE, 0x28 }, { SDL_SCANCODE_GRAVE, 0x29 },
        { SDL_SCANCODE_LSHIFT, 0x2a }, { SDL_SCANCODE_BACKSLASH, 0x2b },
        { SDL_SCANCODE_Z, 0x2c }, { SDL_SCANCODE_X, 0x2d }, { SDL_SCANCODE_C, 0x2e },
        { SDL_SCANCODE_V, 0x2f }, { SDL_SCANCODE_B, 0x30 }, { SDL_SCANCODE_N, 0x31 },
        { SDL_SCANCODE_M, 0x32 }, { SDL_SCANCODE_COMMA, 0x33 }, { SDL_SCANCODE_PERIOD, 0x34 },
        { SDL_SCANCODE_SLASH, 0x35 }, { SDL_SCANCODE_KP_DIVIDE, 0x35 }, { SDL_SCANCODE_RSHIFT, 0x36 },
        { SDL_SCANCODE_KP_MULTIPLY, 0x37 }, { SDL_SCANCODE_LALT, 0x38 }, { SDL_SCANCODE_SPACE, 0x39 },
        { SDL_SCANCODE_F1, 0x3b }, { SDL_SCANCODE_F2, 0x3c }, { SDL_SCANCODE_F3, 0x3d },
        { SDL_SCANCODE_F4, 0x3e }, { SDL_SCANCODE_F5, 0x3f }, { SDL_SCANCODE_F6, 0x40 },
        { SDL_SCANCODE_F7, 0x41 }, { SDL_SCANCODE_F8, 0x42 }, { SDL_SCANCODE_F9, 0x43 },
        { SDL_SCANCODE_F10, 0x44 },
        { SDL_SCANCODE_KP_7, 0x47 }, { SDL_SCANCODE_HOME, 0x47 },
        { SDL_SCANCODE_KP_8, 0x48 }, { SDL_SCANCODE_UP, 0x48 },
        { SDL_SCANCODE_KP_9, 0x49 }, { SDL_SCANCODE_PAGEUP, 0x49 },
        { SDL_SCANCODE_KP_MINUS, 0x4a },
        { SDL_SCANCODE_KP_4, 0x4b }, { SDL_SCANCODE_LEFT, 0x4b },
        { SDL_SCANCODE_KP_5, 0x4c },
        { SDL_SCANCODE_KP_6, 0x4d }, { SDL_SCANCODE_RIGHT, 0x4d },
        { SDL_SCANCODE_KP_PLUS, 0x4e },
        { SDL_SCANCODE_KP_1, 0x4f }, { SDL_SCANCODE_END, 0x4f },
        { SDL_SCANCODE_KP_2, 0x50 }, { SDL_SCANCODE_DOWN, 0x50 },
        { SDL_SCANCODE_KP_3, 0x51 }, { SDL_SCANCODE_PAGEDOWN, 0x51 },
        { SDL_SCANCODE_KP_0, 0x52 }, { SDL_SCANCODE_INSERT, 0x52 },
        { SDL_SCANCODE_KP_PERIOD, 0x53 }, { SDL_SCANCODE_DELETE, 0x53 },
    };
    for (const auto &m : map)
        if (m.sdl == s)
            return m.pc;
    return 0;
}

int main(int argc, char **argv)
{
    const char *track = "data/Asphalt";
    int a = 1;
    for (; a + 1 < argc && argv[a][0] == '-'; a += 2) {
        std::string_view opt = argv[a];
        if (opt == "-s")
            render_scale = atoi(argv[a + 1]);
        else if (opt == "-F" && !fix_enable(argv[a + 1])) {
            fprintf(stderr, "unknown fix %s; known:", argv[a + 1]);
            for (int i = 0; i < fix_count; i++)
                fprintf(stderr, " %s", fix_list[i].name);
            fprintf(stderr, " all\n");
            return 2;
        } else if (opt != "-F")
            break;
    }
    if (a < argc)
        track = argv[a];
    if (render_scale < 1 || render_scale > MAX_RENDER_SCALE) {
        fprintf(stderr, "scale must be 1..%d\n", MAX_RENDER_SCALE);
        return 1;
    }

    if (!host_init_dataseg())
        return 1;
    if (chdir(track) != 0) {
        perror(track);
        return 1;
    }
    if (game_init() != 0 || G->num_cars <= 0) {
        fprintf(stderr, "could not load the track in %s (no car1.dat?)\n", track);
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    int win_w, win_h;
    window_size(&win_w, &win_h);
    SDL_Window *win = SDL_CreateWindow("Terep2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       win_w, win_h, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!win || !ren) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    const int w = screen_w(), h = screen_h();
    SDL_RenderSetLogicalSize(ren, w, h);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, w, h);
    std::vector<uint8_t> rgb((size_t)w * h * 3);

    const uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t next_tick = SDL_GetPerformanceCounter(), next_frame = next_tick;
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                running = false;
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                uint8_t sc = pc_scancode(e.key.keysym.scancode);
                if (sc == 0x01 && e.type == SDL_KEYDOWN)
                    running = false;
                else if (sc)
                    game_key(e.type == SDL_KEYUP ? sc | 0x80 : sc);
            }
        }

        uint64_t now = SDL_GetPerformanceCounter();
        while (now >= next_tick) {          // 120 Hz physics
            physics_tick();
            next_tick += freq / 120;
        }
        if (now >= next_frame) {            // 60 Hz display
            next_frame += freq / 60;
            if (now > next_frame + freq / 4)
                next_frame = now;           // do not try to catch up after a stall
            if (game_frame())
                running = false;
            const uint8_t *scr = screen();
            for (int i = 0; i < w * h; i++)
                memcpy(&rgb[i * 3], host_palette(scr[i]), 3);
            SDL_UpdateTexture(tex, nullptr, rgb.data(), w * 3);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, nullptr, nullptr);
            SDL_RenderPresent(ren);
        } else {
            SDL_Delay(1);
        }
    }
    SDL_Quit();
    return 0;
}
