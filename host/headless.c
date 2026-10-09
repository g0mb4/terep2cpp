/*
 * Headless host for tests: runs the game for a number of frames with a
 * scripted keyboard and prints, per frame, a CRC of the frame buffer and
 * of the data segment (scratch variables masked), in the same format as
 * tools/oracle so the outputs can be compared directly.
 *
 *   terep2-headless [-d data.bin] trackdir frames [script] [ppmdir]
 *
 * script: comma separated frame:scancode pairs (scancode | 0x80 = release)
 * ppmdir: write each frame as fNNNN.ppm there (only frame n with DUMP_FRAME=n)
 */
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include "host.h"
#include "../tests/dsmask.h"

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return;
    }
    fprintf(f, "P6 320 200 255\n");
    const uint8_t *scr = screen();
    for (int i = 0; i < 64000; i++)
        fwrite(host_palette(scr[i]), 1, 3, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *data = "data.bin";
    int a = 1;
    if (a + 1 < argc && !strcmp(argv[a], "-d")) {
        data = argv[a + 1];
        a += 2;
    }
    if (argc - a < 2) {
        fprintf(stderr, "usage: %s [-d data.bin] trackdir frames [script] [ppmdir]\n", argv[0]);
        return 2;
    }
    const char *track = argv[a], *script = argc - a > 2 ? argv[a + 2] : "";
    int frames = atoi(argv[a + 1]);
    char ppmdir[PATH_MAX] = "";
    if (argc - a > 3 && !realpath(argv[a + 3], ppmdir)) {
        perror(argv[a + 3]);
        return 1;
    }

    if (host_load_dataseg(data) != 0)
        return 1;
    if (chdir(track) != 0) {
        perror(track);
        return 1;
    }
    if (game_init() != 0) {
        fprintf(stderr, "init failed\n");
        return 1;
    }
    printf("cars=%d\n", G->num_cars);

    /* DUMP_FRAME=n: write only that frame's image */
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
        host_step();                    /* Esc is ignored here */
        printf("%d %08lx %08lx\n", fr, crc32_buf(screen(), 64000), ds_masked_crc((const uint8_t *)G));
        if (*ppmdir && (only < 0 || only == fr)) {
            char fn[PATH_MAX + 16];
            snprintf(fn, sizeof fn, "%s/f%04d.ppm", ppmdir, fr);
            write_ppm(fn);
        }
    }
    return 0;
}
