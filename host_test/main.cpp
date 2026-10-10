/*
 * Test host (headless): runs the game for a number of frames with a
 * scripted keyboard and prints, per frame, a CRC of the frame buffer and
 * of the data segment (scratch variables masked), in the same format as
 * tools/oracle so the outputs can be compared directly.  The cars are
 * written back into the segment first (car_image.cpp), where the original
 * keeps them.
 *
 *   terep2-headless [-s scale] [-E option]... trackdir frames [script] [ppmdir]
 *
 * -E option, --extra-options option: turn on an extra option (see extra_option_list),
 * such as a fix of a bug of the original, or "all".
 *
 * script: comma separated frame:scancode pairs (scancode | 0x80 = release)
 * ppmdir: write each frame as fNNNN.ppm there (only frame n with DUMP_FRAME=n)
 */
#include <climits>
#include <cstdlib>
#include <string_view>
#include <unistd.h>
#include "../host/host.hpp"
#include "car_image.hpp"
#include "dsmask.h"

/* The data segment as the original has it: with the cars in car_pool. */
static const uint8_t *dataseg_image()
{
    static uint8_t image[0x10000];
    memcpy(image, G, sizeof image);
    cars_to_dataseg(image);
    return image;
}

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return;
    }
    fprintf(f, "P6 %d %d 255\n", screen_w(), screen_h());
    const uint8_t *scr = screen();
    for (int i = 0; i < screen_w() * screen_h(); i++)
        fwrite(host_palette(scr[i]), 1, 3, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    int a = 1;
    for (; a + 1 < argc && argv[a][0] == '-'; a += 2) {
        std::string_view opt = argv[a];
        if (opt == "-s")
            render_scale = atoi(argv[a + 1]);
        else if ((opt == "-E" || opt == "--extra-options") && !extra_option_enable(argv[a + 1])) {
            fprintf(stderr, "unknown option %s; known:", argv[a + 1]);
            for (int i = 0; i < extra_option_count; i++)
                fprintf(stderr, " %s", extra_option_list[i].name);
            fprintf(stderr, " all\n");
            return 2;
        } else if (opt != "-E" && opt != "--extra-options")
            break;
    }
    if (argc - a < 2 || render_scale < 1 || render_scale > MAX_RENDER_SCALE) {
        fprintf(stderr, "usage: %s [-s 1..%d] [-E option]... trackdir frames [script] [ppmdir]\n",
                argv[0], MAX_RENDER_SCALE);
        return 2;
    }
    const char *track = argv[a], *script = argc - a > 2 ? argv[a + 2] : "";
    int frames = atoi(argv[a + 1]);
    char ppmdir[PATH_MAX] = "";
    if (argc - a > 3 && !realpath(argv[a + 3], ppmdir)) {
        perror(argv[a + 3]);
        return 1;
    }

    if (!host_init_dataseg())
        return 1;
    if (chdir(track) != 0) {
        perror(track);
        return 1;
    }
    if (game_init() != 0 || !car_files_read()) {
        fprintf(stderr, "init failed\n");
        return 1;
    }
    printf("cars=%d\n", G->num_cars);

    // DUMP_FRAME=n: write only that frame's image
    int only = getenv("DUMP_FRAME") ? atoi(getenv("DUMP_FRAME")) : -1;
    for (int fr = 0; fr < frames; fr++) {
        for (const char *s = script; *s;) {
            int f, sc, used;
            if (sscanf(s, "%d:%i%n", &f, &sc, &used) != 2)
                break;
            if (f == fr)
                game_key((uint8_t)sc);
            s += used;
            while (*s == ',')
                s++;
        }
        host_step();                    // Esc is ignored here
        printf("%d %08lx %08lx\n", fr, crc32_buf(screen(), (size_t)screen_w() * screen_h()), ds_masked_crc(dataseg_image()));
        if (*ppmdir && (only < 0 || only == fr)) {
            char fn[PATH_MAX + 16];
            snprintf(fn, sizeof fn, "%s/f%04d.ppm", ppmdir, fr);
            write_ppm(fn);
        }
    }
    return 0;
}
