/*
 * Terep 2 / "DEFORMERS - Atomic Vehicle Simulation System" (C) Nagymathe Denes 1996-1997
 *
 * C++ reconstruction of the MS-DOS demo, from the disassembly in reasm/.
 * Original function addresses are kept in comments ("orig 1000:xxxx").
 *
 * Style: plain C++ ("C with classes").  Every structure here is a packed
 * POD with exactly the original layout, because the data segment, the car
 * files and the car models are byte images addressed by 16-bit offsets.
 * Methods and operators are used where they make the arithmetic readable;
 * everything else stays as free functions.
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstddef>
#include "fixed.hpp"

#define PACKED __attribute__((packed))

/* ---------------------------------------------------------------------------
 * Vectors and positions
 * ------------------------------------------------------------------------- */

/* 16.16 signed vector: velocity, direction, force. */
struct PACKED Vec3 {
    int32_t x, y, z;

    Vec3 &operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3 &operator-=(Vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3 operator+(Vec3 o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(Vec3 o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator>>(int n) const { return { x >> n, y >> n, z >> n }; }
};

/* Integer part of a signed 16.16 value. */
inline int16_t fx_int(int32_t v) { return (int16_t)(v >> 16); }

/* One coordinate of a position on the map: unsigned, 0 at the corner of
 * the terrain.  In cells it is 8.24 (the top byte is the map cell, the
 * rest the position inside it).  Most of the code works in "map units" of
 * 1/256 cell (heights, radii, camera distances), where the same bits read
 * as 16.16. */
struct PACKED Coord {
    uint32_t raw;

    uint16_t units() const { return (uint16_t)(raw >> 16); }
    int16_t  units_signed() const { return (int16_t)units(); }  /* where the original compares signed */
    uint8_t  cell() const { return (uint8_t)(raw >> 24); }
    void     set_units(uint16_t u) { raw = (raw & 0xffff) | (uint32_t)u << 16; }

    Coord &operator+=(int32_t d) { raw += (uint32_t)d; return *this; }
    Coord &operator-=(int32_t d) { raw -= (uint32_t)d; return *this; }
    Coord operator+(int32_t d) const { return { raw + (uint32_t)d }; }
    Coord operator-(int32_t d) const { return { raw - (uint32_t)d }; }
    int32_t operator-(Coord o) const { return (int32_t)(raw - o.raw); }  /* signed distance */

    static Coord from_units(uint16_t u) { return { (uint32_t)u << 16 }; }
};

struct PACKED Pos {
    Coord x, y, z;

    Pos &operator+=(Vec3 v) { x += v.x; y += v.y; z += v.z; return *this; }
    Pos operator+(Vec3 v) const { return { x + v.x, y + v.y, z + v.z }; }
    Pos operator-(Vec3 v) const { return { x - v.x, y - v.y, z - v.z }; }
    Vec3 operator-(Pos o) const { return { x - o.x, y - o.y, z - o.z }; }
};

/* ---------------------------------------------------------------------------
 * Small structures that live inside the data segment
 * ------------------------------------------------------------------------- */

struct PACKED ChaseCam {            /* state of the "follow" cameras (F3/F4/F5) */
    Pos     pos;
    int16_t yaw, pitch, roll;
};

struct PACKED Particle {            /* smoke puffs, sparks and ice chips */
    Pos     pos;
    Vec3    vel;
    int16_t life;                   /* counts down by 2 per tick */
    int16_t kind;                   /* surface byte that spawned it */

    static constexpr int16_t ICE_CHIP = 0x0f, DUST = 1;
};

struct PACKED ProjVertex {          /* a point transformed to camera space */
    int16_t x, y, z;                /* camera space: y is depth */
    int16_t sx, sy;                 /* screen position, valid when y >= near_clip */
};

struct PACKED PolyVertex {          /* polygon vertex fed to the rasterisers */
    int16_t  sx, sy;
    uint16_t a, b;                  /* attributes: u/v (texture) or shade */
};

/* A polygon: vertex count followed by the vertices (the clippers write one
 * extra vertex past the last one while working). */
struct PACKED PolyBuf {
    uint16_t   count;
    PolyVertex v[10];
};

struct PACKED ScanEdge {
    int16_t left, right;
};

struct PACKED Point2 {
    int16_t x, y;
};

struct PACKED PointBuf {
    uint16_t count;
    Point2   v[10];
};

struct PACKED PcxHeader {
    uint8_t  manufacturer, version, encoding, bits_per_pixel;
    uint16_t xmin, ymin, xmax, ymax;
    uint16_t hdpi, vdpi;
    uint8_t  ega_palette[48];
    uint8_t  reserved;
    uint8_t  planes;
    uint16_t bytes_per_line;
    uint16_t palette_type;
    uint8_t  filler[58];
};

#include "dataseg.hpp"

/* ---------------------------------------------------------------------------
 * Cars
 *
 * A carN.dat file is loaded verbatim into the data segment and *is* the car:
 * a fixed header followed by three variable-length sections that the header
 * points to (offsets are relative to the start of the car).
 * ------------------------------------------------------------------------- */

struct PACKED CarVertex {           /* a mass point of the soft body */
    Pos      pos;
    Vec3     vel;
    int16_t  radius;                /* collision radius against the ground */
    uint16_t wheel;                 /* 0 = body point, 0xffff = reference point, else wheel */

    bool is_wheel() const { return wheel != 0 && wheel != 0xffff; }
};

struct PACKED CarSpring {
    enum : uint8_t {
        LIMITS = 0,                 /* rope: only acts outside [min_len, max_len] */
        ELASTIC = 1,                /* spring that bends permanently or breaks */
        BROKEN = 0x80,              /* flag; a broken spring behaves like a damper */
    };

    uint16_t a, b;                  /* vertex indices */
    int16_t  rest;                  /* rest length (<0: take the current length) */
    int16_t  last;                  /* length on the previous tick */
    uint16_t flags;                 /* low byte: type, bit 7: broken; high byte: softness shift */
    uint16_t min_len, max_len;

    uint8_t type() const { return (uint8_t)flags; }
    int softness() const { return (uint8_t)((flags >> 8) + 1) & 31; }
};

struct PACKED Car {
    enum Drive : uint16_t { REAR = 0, FRONT = 1, ALL = 2 };

    uint16_t vertices_ofs;          /* -> uint16 count + CarVertex[] */
    uint16_t springs_ofs;           /* -> uint16 count + CarSpring[] */
    uint16_t model_ofs;             /* -> display list (see car_draw.cpp) */
    uint16_t unk_06;
    uint16_t drive;                 /* Drive */
    int16_t  steer;                 /* current steering, +-0x2000 */
    int16_t  throttle;              /* +-0x2000 */
    uint16_t handbrake;
    int16_t  mid[3];                /* middle of the body (map units) */
    int16_t  steer_used;            /* steering/throttle latched for this tick */
    int16_t  throttle_used;
    uint16_t cell;                  /* map cell the car is drawn with */
    uint16_t unk_1c;
    uint16_t texture_seg;           /* car skin (0 = use textures.pcx) */
    uint16_t ref_vertex_ofs;        /* -> reference vertex (camera anchor) */
    uint16_t lowest_vertex;         /* DS offset of the vertex pushed by the "jump" keys */
    uint8_t  unk_24[0x1e];
    int32_t  wheel_speed[4];        /* angular speed of each wheel */
    int32_t  spin[12];              /* further per-wheel state, decays with the speeds */
    uint16_t unk_82;

    /* the sections of the car file */
    uint8_t   *bytes() { return reinterpret_cast<uint8_t *>(this); }
    uint16_t   vertex_count() { return *reinterpret_cast<uint16_t *>(bytes() + vertices_ofs); }
    CarVertex *vertices() { return reinterpret_cast<CarVertex *>(bytes() + vertices_ofs + 2); }
    uint16_t   spring_count() { return *reinterpret_cast<uint16_t *>(bytes() + springs_ofs); }
    CarSpring *springs() { return reinterpret_cast<CarSpring *>(bytes() + springs_ofs + 2); }
    CarVertex &ref_vertex() { return *reinterpret_cast<CarVertex *>(bytes() + ref_vertex_ofs); }

    /* physics.cpp */
    Vec3 axis_x();                  /* unit vector across the car */
    Vec3 axis_y();                  /* unit vector along the car */
    void read_controls(const uint8_t binding[6]);
    void update_wheels();
    void update_springs();
    void update_points();
    void build_hull();
    void push_points_of(Car &other);
    void jump(bool keep_point);

private:
    void update_mid();
    void ground_contact(CarVertex &v, uint16_t index);
    Vec3 wheel_direction(uint16_t i, int32_t *speed, int32_t prev_edx);
    void wheel_set_speed(uint16_t i, int32_t ground_speed);
    void compute_steered_dir();
};

static_assert(sizeof(CarVertex) == 0x1c, "CarVertex");
static_assert(sizeof(CarSpring) == 0x0e, "CarSpring");
static_assert(sizeof(Particle) == 0x1c, "Particle");
static_assert(offsetof(Car, wheel_speed) == 0x42, "Car.wheel_speed");
static_assert(sizeof(Car) == 0x84, "Car header");

/* ---------------------------------------------------------------------------
 * Global state
 * ------------------------------------------------------------------------- */

extern DataSeg *G;                  /* the data segment */
extern uint8_t keys[128];           /* [0] = last scancode, [sc] bit 7 clear while held */

/* "Segments": 64 KB blocks allocated at load time and referred to by a
 * 16-bit handle, exactly like the original's DOS memory blocks. */
uint8_t *seg_ptr(uint16_t handle);
int      seg_alloc(uint16_t paragraphs, uint16_t *handle);

inline uint8_t *ds_ptr(uint16_t ofs) { return reinterpret_cast<uint8_t *>(G) + ofs; }
inline uint16_t ds_ofs(const void *p) { return (uint16_t)(static_cast<const uint8_t *>(p) - reinterpret_cast<const uint8_t *>(G)); }

inline Car &car_at(uint16_t ofs) { return *reinterpret_cast<Car *>(ds_ptr(ofs)); }
inline Car &car_get(int i) { return car_at(G->car_ofs[i]); }

/* Map layers.  Indexing with a uint16_t (x | y << 8) wraps around exactly
 * like the original's 16-bit offsets. */
inline uint8_t *heightmap() { return seg_ptr(G->seg_heightmap); }
inline uint8_t *colormap()  { return seg_ptr(G->seg_colormap); }
inline uint8_t *screen()    { return seg_ptr(G->seg_screen); }

/* ---------------------------------------------------------------------------
 * Modules
 * ------------------------------------------------------------------------- */

/* math.cpp */
int16_t  fx_sin(uint16_t angle);
int16_t  fx_cos(uint16_t angle);
uint16_t fx_atan2(int16_t y, int16_t x);
int32_t  vec2_length(int16_t a, int16_t b);
int32_t  vec3_length(int16_t a, int16_t b, int16_t c);
Vec3     vec3_normalize(Vec3 v);
void     project(int16_t x, int16_t depth, int16_t z, int16_t *sx, int16_t *sy);
void     rotate(const int16_t m[9], int16_t x, int16_t y, int16_t z, int16_t out[3]);
void     build_matrix(const int16_t angles[3], int16_t m[9]);
void     transpose(int16_t m[9]);

/* terrain.cpp */
int16_t  terrain_height(uint16_t x, uint16_t y);
int16_t  ground_probe(uint16_t x, uint16_t y);

/* physics.cpp */
void     physics_tick();

/* raster.cpp */
extern const uint8_t *raster_texture;   /* texture for draw_textured */
void     clear_screen();
void     put_pixel(int16_t x, int16_t y, uint8_t color);
int16_t  edge_lerp(int16_t da, int16_t va, int16_t db, int16_t vb);
void     poly_begin(const ProjVertex &p, uint32_t attr);
void     poly_add(const ProjVertex &p, uint32_t attr);
uint16_t poly_end(const ProjVertex &p, uint32_t attr);
bool     poly_backfacing();
void     draw_flat();
void     draw_shaded();
void     draw_textured();

/* scene.cpp */
extern bool poly_hidden;                /* result of the last facing test */
bool     view_transform(int16_t x, int16_t y, int16_t z, ProjVertex *out);
void     draw_sprite(int16_t sx, int16_t sy, int16_t depth, uint16_t sprite_ofs);
void     draw_terrain();
void     draw_particles();

/* car_draw.cpp */
void     draw_car(Car &car);

/* camera.cpp */
void     camera_update(int view);

/* text.cpp */
void     draw_text(const char *s, int16_t x, int16_t y, uint8_t color);

/* load.cpp */
FILE    *data_fopen(const char *name);  /* provided by the host: open a track file for reading */

/* game.cpp */
int      game_init();
int      game_frame();
void     game_key(uint8_t scancode);

/* sound.cpp (AdLib register writes, played by the host if it wants) */
extern void (*opl_write_hook)(uint8_t reg, uint8_t value);
void     opl_write(uint8_t reg, uint8_t value);
void     sound_init();
void     sfx_hit(int n);
void     engine_sound(int car_index, uint8_t channel);
