/*
 * Loading a track directory: the map layers (PCX images), the fog and
 * translucency tables, and up to five cars.
 *
 * File names are the ones stored in the data segment; the host opens them
 * (data_fopen, case-insensitive on systems that care).
 */
#include "terep.h"

/* ------------------------------------------------------------------------
 * PCX (8-bit, single plane, RLE)
 * ---------------------------------------------------------------------- */

/* Read the 128-byte header.  Like the original, the check only rejects
 * fewer than 8 bits per pixel, or 8 bits with no planes.  orig 1000:5a95 */
static int pcx_read_header(FILE *f)
{
    fread(&G->pcx_header, 1, sizeof G->pcx_header, f);
    G->pcx_width = (uint16_t)(G->pcx_header.xmax - G->pcx_header.xmin + 1);
    G->pcx_height = (uint16_t)(G->pcx_header.ymax - G->pcx_header.ymin + 1);
    uint8_t bpp = G->pcx_header.bits_per_pixel;
    return !(bpp < 8 || (bpp == 8 && G->pcx_header.planes == 0));
}

/* Decode the image into a segment, at most 256 x 256 pixels: wider
 * images are cropped (rows are packed, not padded to 256).  The file is
 * read through G->pcx_buffer in 768-byte chunks.  orig 1000:5acf */
static void pcx_decode(FILE *f, uint8_t *dst)
{
    uint16_t in = sizeof G->pcx_buffer;     /* buffer empty */
    uint16_t out = 0;
    uint8_t value = 0, repeat = 0;
    int16_t rows = (int16_t)G->pcx_height > 0x100 ? 0x100 : (int16_t)G->pcx_height;
    uint16_t row = (uint16_t)rows;

    do {
        uint16_t x = 0;
        do {
            if (repeat) {
                repeat--;
            } else {
                if (in == sizeof G->pcx_buffer) {
                    fread(G->pcx_buffer, 1, sizeof G->pcx_buffer, f);
                    in = 0;
                }
                value = G->pcx_buffer[in++];
                if ((value & 0xc0) == 0xc0) {
                    repeat = (uint8_t)((value & 0x3f) - 1);   /* a count of 0 gives 255 */
                    if (in == sizeof G->pcx_buffer) {
                        fread(G->pcx_buffer, 1, sizeof G->pcx_buffer, f);
                        in = 0;
                    }
                    value = G->pcx_buffer[in++];
                }
            }
            if (x < 0x100)
                dst[out++] = value;
            x++;
        } while (x < G->pcx_width);
    } while (--row);
}

/* orig 1000:5a60 */
static int pcx_load(const char *name, uint8_t *dst)
{
    FILE *f = data_fopen(name);
    if (!f)
        return 0;
    if (!pcx_read_header(f)) {
        fclose(f);
        return 0;
    }
    fseek(f, 0x80, SEEK_SET);
    pcx_decode(f, dst);
    fclose(f);
    return 1;
}

static void read_table(const char *name, void *dst, size_t size)
{
    FILE *f = data_fopen(name);
    if (!f)
        return;
    fread(dst, 1, size, f);
    fclose(f);
}

/* Height map, terrain textures, colour map with the palette, fog and
 * translucency tables.  Stops at the first missing image (the original
 * goes on with whatever is in memory).  orig 1000:24c0 */
static void load_map(void)
{
    if (!pcx_load(G->fname_map, heightmap()))
        return;
    if (!pcx_load(G->fname_maptex, seg_ptr(G->seg_maptex)))
        return;

    FILE *f = data_fopen(G->fname_col);
    if (!f)
        return;
    if (!pcx_read_header(f) || fseek(f, -0x300, SEEK_END) != 0) {
        fclose(f);
        return;
    }
    fread(G->palette, 1, sizeof G->palette, f);
    fseek(f, 0x80, SEEK_SET);
    pcx_decode(f, colormap());
    fclose(f);

    read_table(G->fname_fog, G->fog_table, sizeof G->fog_table);
    read_table(G->fname_smoke, G->shade_table, sizeof G->shade_table);
}

/* ------------------------------------------------------------------------
 * Cars
 * ---------------------------------------------------------------------- */

/* Load a car file at DS offset ofs and put it on the map at (x, y), 100
 * units above the ground.  Returns the file size, 0 if it is missing.
 * orig 1000:2454 */
static uint16_t load_car(const char *name, uint16_t ofs, uint16_t x, uint16_t y)
{
    memset(ds_ptr(ofs), 0, 0x82);
    FILE *f = data_fopen(name);
    if (!f)
        return 0;
    uint16_t size = (uint16_t)fread(ds_ptr(ofs), 1, 10000, f);
    fclose(f);

    Car *car = car_at(ofs);
    int16_t z = (int16_t)(terrain_height(x, y) + 100);
    CarVertex *v = car_vertices(car);
    for (uint16_t i = 0; i < car_vertex_count(car); i++) {
        v[i].pos.x += (uint32_t)x << 16;
        v[i].pos.y += (uint32_t)y << 16;
        v[i].pos.z += (uint32_t)(uint16_t)z << 16;
    }
    return size;
}

/* The reference vertex (camera anchor) is the one marked 0xffff, else the
 * first.  The original takes its loop count from the first vertex's x
 * fraction instead of the vertex count; it only matters when there is no
 * marked vertex.  orig 1000:2431 */
static void find_ref_vertex(Car *car)
{
    uint16_t first = (uint16_t)(car->vertices_ofs + 2);
    uint16_t base = ds_ofs(car);
    uint16_t at = (uint16_t)(base + first);
    uint16_t count = *(uint16_t *)ds_ptr(at);

    car->ref_vertex_ofs = first;
    do {
        if (*(uint16_t *)ds_ptr((uint16_t)(at + offsetof(CarVertex, wheel))) == 0xffff) {
            car->ref_vertex_ofs = (uint16_t)(at - base);
            return;
        }
        at = (uint16_t)(at + sizeof(CarVertex));
    } while (--count);
}

/* The car's skin: a PCX of up to 256 pixels width in its own segment.
 * Without it the car keeps the segment named in its file (0 = textures.pcx). */
static void load_car_skin(Car *car, const char *name)
{
    FILE *f = data_fopen(name);
    if (!f)
        return;
    pcx_read_header(f);
    uint16_t w = (int16_t)G->pcx_width > 0x100 ? 0x100 : G->pcx_width;
    uint16_t paragraphs = (uint16_t)(((uint16_t)(w * G->pcx_height) >> 4) + 1);
    uint16_t seg;
    if (seg_alloc(paragraphs, &seg) == 0) {
        car->texture_seg = seg;
        pcx_decode(f, seg_ptr(seg));
    }
    fclose(f);
}

/* Cars are loaded back to back from car_pool until a carN.dat is missing,
 * lined up along y = 0x7a00 every 256 units of x.  The name tables hold 5
 * entries; a sixth car would take its name from the bytes after them. */
static void load_cars(void)
{
    G->num_cars = 0;
    G->car_ofs[0] = (uint16_t)offsetof(DataSeg, car_pool);
    for (int i = 0;; i++) {
        /* read through ds_ptr: i can run past the 5-entry tables */
        const char *dat = (const char *)ds_ptr(*(uint16_t *)ds_ptr((uint16_t)(0x5af7 + 2 * i)));
        const char *pcx = (const char *)ds_ptr(*(uint16_t *)ds_ptr((uint16_t)(0x5b2e + 2 * i)));
        uint16_t size = load_car(dat, *(uint16_t *)ds_ptr((uint16_t)(0x5bbc + 2 * i)), (uint16_t)(0x8000 - i * 256), 0x7a00);
        if (!size)
            break;
        Car *car = car_get(i);
        find_ref_vertex(car);
        load_car_skin(car, pcx);
        G->num_cars++;
        *(uint16_t *)ds_ptr((uint16_t)(0x5bbc + 2 * (i + 1))) = (uint16_t)(G->car_ofs[i] + size);
    }
}

/* ------------------------------------------------------------------------ */

/* Returns 0 on success.  orig f_init */
int game_init(void)
{
    memset(keys, 0xff, sizeof keys);
    G->num_cars = -2;
    G->unk_ec50 = 0x78;
    G->gravity = 0x1800;
    G->spring_break_stretch = 0x800;
    G->spring_break_squash = (int16_t)0xf000;
    G->clip_left = 0;
    G->screen_cx = 160;
    G->clip_right = 319;
    G->clip_top = 0;
    G->screen_cy = 80;
    G->clip_bottom = 199;

    FILE *cfg = data_fopen(G->fname_cfg);   /* optional spring limits */
    if (cfg) {
        fread(&G->spring_break_stretch, 1, 2, cfg);
        fread(&G->spring_break_squash, 1, 2, cfg);
        fclose(cfg);
    }

    if (seg_alloc(0x1000, &G->seg_heightmap) || seg_alloc(0x1000, &G->seg_colormap) ||
        seg_alloc(0x1000, &G->seg_textures) || seg_alloc(0x1000, &G->seg_maptex))
        return 1;
    load_map();
    pcx_load(G->fname_textures, seg_ptr(G->seg_textures));      /* orig 1000:255c */
    load_cars();

    if (seg_alloc(0xfa0, &G->seg_screen))
        return 1;
    sound_init();
    G->sim_running = 1;
    return 0;
}
