/*
 * Initial contents of the data segment.  The original program loaded them
 * from its data segment image (data.bin); here they are written by code,
 * giving byte for byte the same 64 KB, so runs still match the original
 * frame by frame.  What game_init sets anyway (gravity, viewport, handles,
 * spring limits) is not repeated here.
 */
#include <cmath>
#include "terep.hpp"

/* atan(i / 256) in binary angle units (0x10000 = 360 deg), i = 0..256.
 * Not quite a rounded atan (39 entries are 1 lower), so kept as a table. */
static const uint16_t atan_values[257] = {
    0, 41, 81, 122, 163, 204, 244, 285, 326, 367, 407, 448, 489, 529, 570, 610,
    651, 692, 732, 773, 813, 854, 894, 935, 975, 1015, 1056, 1096, 1136, 1177, 1217, 1257,
    1297, 1337, 1377, 1417, 1457, 1497, 1537, 1577, 1617, 1656, 1696, 1736, 1775, 1815, 1854, 1894,
    1933, 1973, 2012, 2051, 2090, 2129, 2168, 2207, 2246, 2285, 2324, 2363, 2401, 2440, 2478, 2517,
    2555, 2593, 2632, 2670, 2708, 2746, 2784, 2822, 2860, 2897, 2935, 2973, 3010, 3047, 3085, 3122,
    3159, 3196, 3233, 3270, 3307, 3344, 3380, 3417, 3453, 3490, 3526, 3562, 3598, 3634, 3670, 3706,
    3742, 3778, 3813, 3849, 3884, 3919, 3955, 3990, 4025, 4060, 4095, 4129, 4164, 4198, 4233, 4267,
    4301, 4336, 4370, 4404, 4437, 4471, 4505, 4538, 4572, 4605, 4638, 4672, 4705, 4738, 4770, 4803,
    4836, 4868, 4901, 4933, 4965, 4998, 5030, 5062, 5093, 5125, 5157, 5188, 5220, 5251, 5282, 5313,
    5344, 5375, 5406, 5437, 5467, 5498, 5528, 5558, 5589, 5619, 5649, 5678, 5708, 5738, 5767, 5797,
    5826, 5855, 5885, 5914, 5943, 5971, 6000, 6029, 6057, 6086, 6114, 6142, 6170, 6198, 6226, 6254,
    6282, 6310, 6337, 6364, 6392, 6419, 6446, 6473, 6500, 6527, 6554, 6580, 6607, 6633, 6659, 6686,
    6712, 6738, 6764, 6790, 6815, 6841, 6866, 6892, 6917, 6942, 6968, 6993, 7018, 7043, 7067, 7092,
    7117, 7141, 7165, 7190, 7214, 7238, 7262, 7286, 7310, 7334, 7357, 7381, 7404, 7428, 7451, 7474,
    7497, 7520, 7543, 7566, 7589, 7612, 7634, 7657, 7679, 7702, 7724, 7746, 7768, 7790, 7812, 7834,
    7855, 7877, 7899, 7920, 7941, 7963, 7984, 8005, 8026, 8047, 8068, 8089, 8110, 8130, 8151, 8171,
    8192,
};

/* The font (5 pixels high), in memory order.  Each glyph lists the characters that use
 * it; every character not listed shows glyph 0 ('A').  "#" is a lit pixel,
 * top row first; each string is one row.  Some glyphs are not used by any
 * character. */
struct Glyph {
    const char *chars;
    const char *rows[5];
};

static const Glyph glyphs[] = {
    { "A",      { ".#.", "#.#", "###", "#.#", "..." } },
    { "B",      { "##.", "###", "#.#", "###", "..." } },
    { "C",      { ".##", "#..", "#..", ".##", "..." } },
    { "D",      { "##.", "#.#", "#.#", "##.", "..." } },
    { "E",      { "###", "##.", "#..", "###", "..." } },
    { "F",      { "###", "#..", "##.", "#..", "..." } },
    { "G",      { ".##", "#..", "#.#", ".##", "..." } },
    { "H",      { "#.#", "#.#", "###", "#.#", "..." } },
    { "I",      { "#", "#", "#", "#", "." } },
    { "J",      { "..#", "..#", "..#", "##.", "..." } },
    { "K",      { "#.#", "#.#", "##.", "#.#", "..." } },
    { "L",      { "#..", "#..", "#..", "###", "..." } },
    { "M",      { "#...#", "##.##", "#.#.#", "#...#", "....." } },
    { "N",      { "#..#", "##.#", "#.##", "#..#", "...." } },
    { "O",      { ".#.", "#.#", "#.#", ".#.", "..." } },
    { "P",      { "##.", "#.#", "##.", "#..", "..." } },
    { "Q",      { ".#.", "#.#", "###", ".##", "..." } },
    { "R",      { "##.", "#.#", "##.", "#.#", "..." } },
    { "S",      { ".##", "##.", "..#", "##.", "..." } },
    { "T",      { "###", ".#.", ".#.", ".#.", "..." } },
    { "U",      { "#.#", "#.#", "#.#", ".##", "..." } },
    { "V",      { "#.#", "#.#", "#.#", ".#.", "..." } },
    { "W",      { "#...#", "#...#", "#.#.#", ".#.#.", "....." } },
    { "X",      { "#.#", "#.#", ".#.", "#.#", "..." } },
    { "Y",      { "#.#", "#.#", ".#.", ".#.", "..." } },
    { "Z",      { "###", ".#.", "#..", "###", "..." } },
    { "a",      { "...", ".##", "#.#", ".##", "..." } },
    { "b",      { "#..", "##.", "#.#", "##.", "..." } },
    { "c",      { "...", ".##", "#..", ".##", "..." } },
    { "d",      { "..#", ".##", "#.#", ".##", "..." } },
    { "e",      { "...", ".##", "##.", "###", "..." } },
    { "f",      { ".#", "#.", "##", "#.", "#." } },
    { "g",      { "...", ".##", "###", "..#", ".#." } },
    { "h",      { "#..", "##.", "#.#", "#.#", "..." } },
    { "i",      { "#", ".", "#", "#", "." } },
    { "j",      { ".#", "..", ".#", ".#", "#." } },
    { "k",      { "#..", "#.#", "##.", "#.#", "..." } },
    { "l",      { "#.", "#.", "#.", ".#", ".." } },
    { "m",      { ".....", "##.#.", "#.#.#", "#.#.#", "....." } },
    { "n",      { "...", "##.", "#.#", "#.#", "..." } },
    { "o",      { "...", ".#.", "#.#", ".#.", "..." } },
    { "p",      { "...", "##.", "#.#", "##.", "#.." } },
    { "q",      { "...", ".##", "#.#", ".##", "..#" } },
    { "r",      { "...", ".##", "#..", "#..", "..." } },
    { "s",      { "..", "#.", ".#", "##", ".." } },
    { "t",      { "#.", "##", "#.", ".#", ".." } },
    { "u",      { "...", "#.#", "#.#", ".##", "..." } },
    { "v",      { "...", "#.#", "#.#", ".#.", "..." } },
    { "w",      { ".....", "#...#", "#.#.#", ".#.#.", "....." } },
    { "x",      { "...", "#.#", ".#.", "#.#", "..." } },
    { "y",      { "...", "#.#", ".##", "..#", ".#." } },
    { "z",      { "...", "###", ".#.", "###", "..." } },
    { "!",      { "#", "#", "#", ".", "#" } },
    { "\"",     { "##", "#.", "..", "..", ".." } },
    { "#",      { ".#.#.", "#####", ".#.#.", "#####", ".#.#." } },
    { "$",      { "..#.", ".###", ".##.", "###.", ".#.." } },
    { "%",      { "##..#", "##.#.", "..#..", ".#.##", "#..##" } },
    { "&",      { "...", ".#.", "###", ".#.", "..." } },
    { "'",      { ".#", "#.", "..", "..", ".." } },
    { "(",      { ".#", "#.", "#.", "#.", ".#" } },
    { ")",      { "#.", ".#", ".#", ".#", "#." } },
    { "*",      { "...", "#.#", ".#.", "#.#", "..." } },
    { "+",      { "...", ".#.", "###", ".#.", "..." } },
    { ",",      { "..", "..", "..", ".#", "#." } },
    { "-",      { "...", "...", "###", "...", "..." } },
    { ".",      { ".", ".", ".", "#", "." } },
    { "/",      { "....#", "...#.", "..#..", ".#...", "#...." } },
    { "0",      { ".#.", "#.#", "#.#", ".#.", "..." } },
    { "1",      { ".#.", "##.", ".#.", "###", "..." } },
    { "2",      { "##.", "..#", ".#.", "###", "..." } },
    { "3",      { "##.", ".##", "..#", "##.", "..." } },
    { "4",      { "..#", ".##", "###", "..#", "..." } },
    { "5",      { "###", "##.", "..#", "##.", "..." } },
    { "6",      { "#..", "##.", "#.#", ".#.", "..." } },
    { "7",      { "###", "..#", ".#.", ".#.", "..." } },
    { "8",      { ".#.", "###", "#.#", ".#.", "..." } },
    { "9",      { ".##", "#.#", ".##", "..#", "..." } },
    { ":",      { ".", "#", ".", "#", "." } },
    { ";",      { "..", ".#", "..", ".#", "#." } },
    { "<",      { "..#", ".#.", "#..", ".#.", "..#" } },
    { "=",      { "...", "###", "...", "###", "..." } },
    { ">",      { "#..", ".#.", "..#", ".#.", "#.." } },
    { "?",      { "##.", "..#", ".#.", "...", ".#." } },
    { "{",      { "..#..", "..#..", "#####", ".###.", "..#.." } },
    { "|",      { "..#..", ".###.", "#####", "..#..", "..#.." } },
    { "}",      { "..#..", ".##..", "#####", ".##..", "..#.." } },
    { "~",      { "..#..", "..##.", "#####", "..##.", "..#.." } },
};

/* AdLib instruments (11 bytes each, see opl_set_patch). */
static const uint8_t engine_patch[0x21] = {
    0x20, 0x00, 0xfb, 0x05, 0x00, 0x20, 0x00, 0xfb, 0x05, 0x00, 0x0a,
    0x20, 0x00, 0xfb, 0x21, 0x00, 0x20, 0x00, 0x9f, 0x05, 0x00, 0x06,
    0x64, 0x00, 0x76, 0x37, 0x00, 0x21, 0x00, 0x79, 0x00, 0x00, 0x0c,
};
static const uint8_t hit_patch[0x3e] = {
    0x00, 0x00, 0xf7, 0xa8, 0x00, 0x00, 0x00, 0xf3, 0x6a, 0x00, 0x02, 0x80, 0x21, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xf0, 0x13, 0x00, 0x05, 0x00, 0xf2, 0x1d, 0x00, 0x06, 0x80, 0x27, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xf0, 0x13, 0x00, 0x05, 0x00, 0xf2, 0x1d, 0x00, 0x06, 0x80, 0x2f, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x00,
};

/* Leftovers of the DOS version that nothing reads any more.  They stay so
 * the segment keeps its original contents. */
/* five words after the sound init variables */
static const uint8_t leftover_0073[10] = {
    0x93, 0x06, 0x3f, 0x07, 0x28, 0x08, 0x93, 0x08, 0x48, 0x09,
};
/* messages of the DOS version's XMS code */
static const char xms_messages[] = "XMS allocated...$XMS locked...$writing...$reading...$data OK. $XMS freed.$END.$";
/* an older sound effect sequence */
static const uint8_t leftover_ec60[88] = {
    0x20, 0x00, 0x9f, 0x05, 0x01, 0x20, 0x00, 0xfb, 0x21, 0x00, 0x00, 0x20, 0x00, 0xfb, 0x21, 0x00,
    0x20, 0x00, 0x9f, 0x05, 0x00, 0x06, 0x64, 0x00, 0x76, 0x37, 0x00, 0x21, 0x00, 0x79, 0x00, 0x00,
    0x0e, 0x20, 0x00, 0x9f, 0x05, 0x01, 0x20, 0x00, 0xfb, 0x21, 0x00, 0x0a, 0x20, 0x00, 0xfb, 0x05,
    0x01, 0x20, 0x00, 0x9f, 0x21, 0x00, 0x0a, 0x20, 0x00, 0xfb, 0x05, 0x00, 0x20, 0x00, 0x9f, 0x05,
    0x01, 0x0a, 0x20, 0x00, 0xfb, 0x05, 0x01, 0x20, 0x00, 0x9f, 0x05, 0x01, 0x0a, 0x20, 0x00, 0xfb,
    0x07, 0x00, 0x20, 0x00, 0x9f, 0x21, 0x01, 0x0a,
};

/* Keys of each car: accelerate, brake, left, right, handbrake, unused. */
static const uint8_t default_keys[5][6] = {
    { 0x48, 0x50, 0x4b, 0x4d, 0x4c, 0x52 },     /* arrows, keypad 5 (keypad 0) */
    { 0x11, 0x2c, 0x1e, 0x20, 0x1f, 0x2d },     /* W Z A D S (X) */
    { 0x17, 0x32, 0x24, 0x26, 0x25, 0x27 },     /* I M J L K (;) */
    { 0x14, 0x2f, 0x21, 0x23, 0x22, 0x30 },     /* T V F H G (B) */
    { 0x47, 0x49, 0x4f, 0x51, 0x53, 0x55 },     /* keypad 7 9 1 3 . */
};

/* Per surface type (colour-map byte >> 4). */
static const uint16_t surface_grip[16] = { 16, 25, 20, 40, 30, 25, 16, 20, 40, 25, 25, 25, 16, 25, 16, 16 };
static const uint8_t surface_damp_shift[16] = { 12, 9, 10, 10, 10, 10, 8, 10, 10, 10, 10, 10, 8, 6, 10, 10 };
static const int32_t surface_max_load[16] = {
    0x30000, 0x18000, 0x20000, 0xc000, 0x20000, 0x20000, 0x20000, 0x20000,
    0x20000, 0x20000, 0x20000, 0x20000, 0x10000, 0x10000, 0x10000, 0x10000,
};

/* Triangles of car vertices that form the collision hull. */
static const uint16_t hull_triangles[7][3] = {
    { 4, 5, 9 }, { 7, 6, 24 }, { 6, 9, 5 }, { 8, 7, 25 }, { 0, 2, 3 }, { 0, 1, 2 }, { 15, 14, 13 },
};

/* Which smoke quad each particle kind (>> 4) uses. */
static const uint8_t smoke_quad_of_kind[16] = { 0, 0, 1, 2, 0, 0, 1, 3, 2, 0, 0, 0, 3, 3, 0, 0 };

template <size_t N>
static void set_text(char (&dst)[N], const char *s)
{
    strncpy(dst, s, N);
}

static void put_word(uint16_t ofs, uint16_t value)
{
    memcpy(ds_ptr(ofs), &value, 2);
}

static void init_font()
{
    uint16_t at = 0;
    for (const Glyph &g : glyphs) {
        for (const char *c = g.chars; *c; c++)
            G->font_offset[(uint8_t)*c] = at;
        for (size_t x = 0; g.rows[0][x]; x++) {
            uint8_t column = 0;
            for (int y = 0; y < 5; y++)
                if (g.rows[y][x] == '#')
                    column |= (uint8_t)(1 << y);
            G->font_glyphs[at++] = column;
        }
        G->font_glyphs[at++] = 0;
    }
}

void dataseg_init()
{
    memset(G, 0, sizeof *G);

    set_text(G->title_text, "-DEFORMERS-  ATOMIC VEHICLE SIMULATION SYSTEM (C)NAGYMATHE DENES 1996-1997");
    set_text(G->subtitle_text, "WORK-IN-PROGRESS DEMO VERSION");
    G->subtitle_text[sizeof G->subtitle_text - 1] = (char)0xff;     /* as in the original */
    set_text(G->fname_map, "map.pcx");
    set_text(G->fname_col, "col.pcx");
    set_text(G->fname_textures, "textures.pcx");
    set_text(G->fname_maptex, "maptex.pcx");
    set_text(G->fname_fog, "fog.dat");
    set_text(G->fname_smoke, "smoke.dat");
    set_text(G->fname_cfg, "sim.cfg");
    for (int i = 0; i < 5; i++) {       /* "car1.dat".."car5.dat", "car1.pcx".."car5.pcx" */
        char *dat = &G->car_dat_names[9 * i], *pcx = &G->car_pcx_names[9 * i];
        snprintf(dat, 9, "car%d.dat", i + 1);
        snprintf(pcx, 9, "car%d.pcx", i + 1);
        G->car_dat_name[i] = ds_ofs(dat);
        G->car_pcx_name[i] = ds_ofs(pcx);
    }

    /* cameras: chase camera (F4) in both views, at the middle of the map */
    G->cam_mode[0] = G->cam_mode[1] = 3;
    const Pos centre = { Coord::from_units(0x8080), Coord::from_units(0x9080), Coord::from_units(0x0400) };
    G->chase_cam[0].pos = G->chase_cam[1].pos = G->eye = centre;
    G->cars_center[0] = (int16_t)0x8080;
    G->cars_center[1] = (int16_t)0x8080;
    G->cars_center[2] = 0x0400;
    G->chase_height = 0x100;
    G->chase_distance = 0x400;
    G->near_clip = 30;

    /* cos of the borders between the nine wheel sprites (22.5 deg apart), Q14 */
    static const int16_t sector[8] = { -16069, -13623, -9102, -3196, 3196, 9102, 13623, 16069 };
    memcpy(G->wheel_sector_limits, sector, sizeof sector);
    G->view_reach = 0x21;
    G->view_side_a = 0x19;
    G->view_side_b = 0x14;
    G->fog_start = 0x600;
    G->vis_hi = 0xff;
    G->vis_max = 0xff;
    G->poly_color = 0x0f0f;

    /* smoke: 11x11-texel squares stacked down column 243 of textures.pcx */
    for (int k = 0; k < 4; k++) {
        SpriteQuad q;
        q.size = 0x0a00;
        uint32_t v0 = (uint32_t)(0x1000 * k + 0x80) << 16, v1 = v0 + (0xb00u << 16);
        q.uv[0] = v0 | 0xf380;
        q.uv[1] = v0 | 0xfe80;
        q.uv[2] = v1 | 0xfe80;
        q.uv[3] = v1 | 0xf380;
        G->smoke_quad[k] = q;
    }
    for (int i = 0; i < 16; i++)
        G->smoke_sprite[i] = ds_ofs(&G->smoke_quad[smoke_quad_of_kind[i]]);

    memcpy(G->car_keys, default_keys, sizeof default_keys);
    memcpy(G->surface_grip, surface_grip, sizeof surface_grip);
    memcpy(G->surface_damp_shift, surface_damp_shift, sizeof surface_damp_shift);
    memcpy(G->surface_max_load, surface_max_load, sizeof surface_max_load);
    G->hull_tri_count = 7;
    memcpy(G->hull_tri, hull_triangles, sizeof hull_triangles);

    /* sin of 0..90 deg in 1024 steps, Q15 (1.0 clamped to 0x7fff) */
    const double pi = 3.14159265358979323846;
    for (int i = 0; i <= 1024; i++) {
        long v = std::lround(32768.0 * std::sin(i * pi / 2048.0));
        G->sin_table[i] = (int16_t)(v > 0x7fff ? 0x7fff : v);
    }
    memcpy(G->atan_table, atan_values, sizeof atan_values);
    init_font();
    memcpy(G->sfx_engine_patch, engine_patch, sizeof engine_patch);
    memcpy(G->sfx_hit_patch, hit_patch, sizeof hit_patch);

    memcpy(ds_ptr(0x0073), leftover_0073, sizeof leftover_0073);
    put_word(0x0124, 300);              /* next to near_clip, never read */
    memcpy(ds_ptr(0x5b6b), xms_messages, sizeof xms_messages - 1);
    memcpy(ds_ptr(0xec60), leftover_ec60, sizeof leftover_ec60);
}
