/*
 * Memory and files for the standalone hosts.
 *
 * The data segment is a 64 KB block initialised from data.bin (the
 * original program's data segment as it is on disk: tables, font, names,
 * default settings).  "Segments" are 64 KB blocks too, so that 16-bit
 * offsets into them can never leave them; handle 0 means "none".
 */
#include <cstdlib>
#include "host.hpp"

constexpr int MAX_SEGMENTS = 32;

static uint8_t *segments[MAX_SEGMENTS];
static uint16_t used = 1;

uint8_t *seg_ptr(uint16_t handle)
{
    return handle < used ? segments[handle] : nullptr;
}

int seg_alloc(uint16_t paragraphs, uint16_t *handle)
{
    (void)paragraphs;
    if (used >= MAX_SEGMENTS)
        return -1;
    auto *p = static_cast<uint8_t *>(calloc(1, 0x10000));
    if (!p)
        return -1;
    segments[used] = p;
    *handle = used++;
    return 0;
}

bool host_load_dataseg(const char *path)
{
    G = static_cast<DataSeg *>(calloc(1, 0x10000));
    FILE *f = fopen(path, "rb");
    if (!G || !f) {
        perror(path);
        return false;
    }
    size_t n = fread(G, 1, 0x10000, f);
    fclose(f);
    if (n < offsetof(DataSeg, pcx_width)) {
        fprintf(stderr, "%s: too short for a data segment image\n", path);
        return false;
    }
    return true;
}

bool host_step()
{
    physics_tick();
    physics_tick();
    return game_frame() != 0;
}

#ifdef _WIN32
FILE *data_fopen(const char *name)
{
    return fopen(name, "rb");           /* the file system ignores case */
}
#else
#include <dirent.h>
#include <strings.h>
/* Track files come with any mix of upper and lower case. */
FILE *data_fopen(const char *name)
{
    FILE *f = fopen(name, "rb");
    if (f)
        return f;
    DIR *d = opendir(".");
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (strcasecmp(e->d_name, name) == 0) {
            f = fopen(e->d_name, "rb");
            break;
        }
    }
    if (d)
        closedir(d);
    return f;
}
#endif
