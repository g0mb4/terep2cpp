/*
 * Test host (headless): runs the game for a number of frames with a
 * scripted keyboard and prints, per frame, a CRC of the frame buffer and
 * of the data segment (scratch variables masked), in the same format as
 * tools/oracle so the outputs can be compared directly.  The cars are
 * written back into the segment first (car_image.cpp), where the original
 * keeps them.
 *
 *   terep2-headless [-s scale] [-E option]... [-p map:frames] mapdir frames [script] [ppmdir]
 *
 * -E option, --extra-options option: turn on an extra option (see extra_option_list),
 * such as a fix of a bug of the original, or "all".
 *
 * -p map:frames: first play that many frames of the script on another
 * map (without output), then free everything and start the real run in
 * the same process; it must give the same output as a fresh process.
 * script: comma separated frame:scancode pairs (scancode | 0x80 = release)
 * ppmdir: write each frame as fNNNN.ppm there (only frame n with DUMP_FRAME=n)
 *
 *   terep2-headless [-E option]... -R recdir mapdir
 *
 * -R: replay a recording made with the Win16 port, at 1x (replay() below).
 */
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>
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

// The keys of the script for frame fr.
static void script_keys(const char *script, int fr)
{
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
}

// A game on map, from a new data segment; the current directory is restored.
static bool start_game(const char *map)
{
    char here[PATH_MAX];
    if (!getcwd(here, sizeof here) || !host_init_dataseg())
        return false;
    if (chdir(map) != 0) {
        perror(map);
        return false;
    }
    bool ok = game_init() == 0 && car_files_read();
    if (!ok)
        fprintf(stderr, "init failed\n");
    return chdir(here) == 0 && ok;
}

// A PGM (P5, 320 x 200) from the recording; false if it cannot be read.
static bool read_pgm(const char *path, std::vector<uint8_t> *pixels)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    char line[256];
    int fields[3], n = 0;                   // width, height, maxval
    bool ok = fgets(line, sizeof line, f) && !strncmp(line, "P5", 2);
    while (ok && n < 3 && fgets(line, sizeof line, f)) {
        if (line[0] == '#')
            continue;
        int used;
        for (char *p = line; n < 3 && sscanf(p, "%d%n", &fields[n], &used) == 1; p += used)
            n++;
    }
    ok = ok && n == 3 && fields[0] == 320 && fields[1] == 200;
    pixels->resize(320 * 200);
    ok = ok && fread(pixels->data(), 1, pixels->size(), f) == pixels->size();
    fclose(f);
    return ok;
}

/* Replays recdir/record.txt, written by the Win16 port, on mapdir: one
 * action per line, called exactly as recorded:
 *   init               new data segment, game_init in mapdir
 *   physics            physics_tick
 *   render             game_frame
 *   handleKey: 0xcd    game_key (scancode, | 0x80 = released)
 *   frame: name.pgm    writes the frame buffer as recdir/novo_name.pgm (P5, as
 *                      the port does) and compares it with recdir/name.pgm
 * Returns 0 if every frame matched, 1 if not (or on errors). */
static int replay(const char *rec, const char *mapdir)
{
    char dir[PATH_MAX], map[PATH_MAX];       // absolute: init changes to the map's directory
    if (!realpath(rec, dir) || !realpath(mapdir, map)) {
        perror(realpath(rec, dir) ? mapdir : rec);
        return 1;
    }
    std::string record = std::string(dir) + "/record.txt";
    FILE *in = fopen(record.c_str(), "r");
    if (!in) {
        perror(record.c_str());
        return 1;
    }
    render_scale = 1;                       // the original's frame buffer
    int renders = 0, same = 0, differ = 0, missing = 0, line_no = 0;
    std::string first_diff;
    bool started = false;
    char line[512];
    while (fgets(line, sizeof line, in)) {
        line_no++;
        line[strcspn(line, "\r\n")] = 0;
        int key;
        char name[400];
        if (!*line) {
        } else if (!strcmp(line, "init")) {
            if (started)
                host_free_game();
            if (!host_init_dataseg() || chdir(map) != 0 || game_init() != 0) {
                fprintf(stderr, "%s: init failed\n", map);
                return 1;
            }
            started = true;
        } else if (!started) {
            fprintf(stderr, "record.txt:%d: \"%s\" before init\n", line_no, line);
            return 1;
        } else if (!strcmp(line, "physics")) {
            physics_tick();
        } else if (!strcmp(line, "render")) {
            game_frame();
            renders++;
        } else if (sscanf(line, "handleKey: %i", &key) == 1) {
            game_key((uint8_t)key);
        } else if (sscanf(line, "frame: %399s", name) == 1) {
            const uint8_t *scr = screen();
            std::string mine = std::string(dir) + "/novo_" + name;
            FILE *out = fopen(mine.c_str(), "wb");
            if (out) {
                fprintf(out, "P5\n#frame %d\n320 200\n255\n", renders);
                fwrite(scr, 1, 320 * 200, out);
                fclose(out);
            } else
                perror(mine.c_str());
            std::vector<uint8_t> orig;
            if (!read_pgm((std::string(dir) + "/" + name).c_str(), &orig)) {
                printf("%s: original missing or unreadable\n", name);
                missing++;
                continue;
            }
            int diff = 0, x0 = 320, y0 = 200, x1 = -1, y1 = -1;
            for (int y = 0; y < 200; y++)
                for (int x = 0; x < 320; x++)
                    if (scr[y * 320 + x] != orig[y * 320 + x]) {
                        diff++;
                        x0 = std::min(x0, x), x1 = std::max(x1, x);
                        y0 = std::min(y0, y), y1 = std::max(y1, y);
                    }
            if (!diff) {
                printf("%s: same\n", name);
                same++;
            } else {
                printf("%s: %d pixels differ (%.2f%%), in x %d..%d, y %d..%d\n", name, diff, diff * 100.0 / 64000,
                       x0, x1, y0, y1);
                if (!differ++)
                    first_diff = name;
            }
        } else {
            printf("unhandled line: %s\n", line);
        }
    }
    fclose(in);
    printf("%d frames: %d same, %d differ%s%s, %d without original\n", same + differ + missing, same, differ,
           differ ? ", first " : "", first_diff.c_str(), missing);
    return differ || missing ? 1 : 0;
}

int main(int argc, char **argv)
{
    int a = 1;
    const char *pre = nullptr, *rec = nullptr;
    for (; a + 1 < argc && argv[a][0] == '-'; a += 2) {
        std::string_view opt = argv[a];
        if (opt == "-s")
            render_scale = atoi(argv[a + 1]);
        else if (opt == "-p")
            pre = argv[a + 1];
        else if (opt == "-R")
            rec = argv[a + 1];
        else if ((opt == "-E" || opt == "--extra-options") && !extra_option_enable(argv[a + 1])) {
            fprintf(stderr, "unknown option %s; known:", argv[a + 1]);
            for (int i = 0; i < extra_option_count; i++)
                fprintf(stderr, " %s", extra_option_list[i].name);
            fprintf(stderr, " all\n");
            return 2;
        } else if (opt != "-E" && opt != "--extra-options" && opt != "-p" && opt != "-R")
            break;
    }
    if (rec) {
        if (argc - a != 1) {
            fprintf(stderr, "usage: %s [-E option]... -R recdir mapdir\n", argv[0]);
            return 2;
        }
        return replay(rec, argv[a]);
    }
    if (argc - a < 2 || render_scale < 1 || render_scale > MAX_RENDER_SCALE) {
        fprintf(stderr, "usage: %s [-s 1..%d] [-E option]... [-p map:frames] mapdir frames [script] [ppmdir]\n"
                        "       %s [-E option]... -R recdir mapdir\n",
                argv[0], MAX_RENDER_SCALE, argv[0]);
        return 2;
    }
    const char *map = argv[a], *script = argc - a > 2 ? argv[a + 2] : "";
    int frames = atoi(argv[a + 1]);
    char ppmdir[PATH_MAX] = "";
    if (argc - a > 3 && !realpath(argv[a + 3], ppmdir)) {
        perror(argv[a + 3]);
        return 1;
    }

    if (pre) {
        std::string pre_map(pre, strcspn(pre, ":"));
        int pre_frames = strchr(pre, ':') ? atoi(strchr(pre, ':') + 1) : 0;
        if (!start_game(pre_map.c_str()))
            return 1;
        for (int fr = 0; fr < pre_frames; fr++) {
            script_keys(script, fr);
            host_step();
        }
        host_free_game();
    }

    if (!start_game(map))
        return 1;
    if (chdir(map) != 0)              // the map's files stay at hand, as before
        return 1;
    printf("cars=%d\n", G->num_cars);

    // DUMP_FRAME=n: write only that frame's image
    int only = getenv("DUMP_FRAME") ? atoi(getenv("DUMP_FRAME")) : -1;
    for (int fr = 0; fr < frames; fr++) {
        script_keys(script, fr);
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
