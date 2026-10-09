/*
 * Terep 2 / "DEFORMERS - Atomic Vehicle Simulation System" (C) Nagymathe Denes 1996-1997
 *
 * C reconstruction of the MS-DOS demo, from the disassembly in reasm/.
 * Original function addresses are kept in comments ("orig 1000:xxxx").
 */
#ifndef TEREP_H
#define TEREP_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include "fixed.h"

#define PACKED __attribute__((packed))

#ifdef __cplusplus              /* the reference host is C++ */
#define _Static_assert static_assert
#endif

/* ---------------------------------------------------------------------------
 * Small structures that live inside the data segment
 * ------------------------------------------------------------------------- */

typedef struct PACKED Vec3fx {      /* 16.16 world position or velocity */
    int32_t x, y, z;
} Vec3fx;

/* Integer (map unit) part of a 16.16 coordinate. */
static inline int16_t fx_int(int32_t v) { return (int16_t)(v >> 16); }

typedef struct PACKED ChaseCam {    /* state of the "follow" cameras (F3/F4/F5) */
    Vec3fx  pos;
    int16_t yaw, pitch, roll;
} ChaseCam;

typedef struct PACKED Particle {    /* smoke puffs, sparks and ice chips */
    Vec3fx  pos;
    Vec3fx  vel;
    int16_t life;                   /* counts down by 2 per tick */
    int16_t kind;                   /* surface byte that spawned it; 0x0f = ice chip, 1 = dust */
} Particle;

typedef struct PACKED ProjVertex {  /* a point transformed to camera space */
    int16_t x, y, z;                /* camera space: y is depth */
    int16_t sx, sy;                 /* screen position, valid when y >= near_clip */
} ProjVertex;

typedef struct PACKED PolyVertex {  /* polygon vertex fed to the rasterisers */
    int16_t  sx, sy;
    uint16_t a, b;                  /* attributes: u/v (texture) or shade */
} PolyVertex;

/* A polygon: vertex count followed by the vertices (the clippers write one
 * extra vertex past the last one while working). */
typedef struct PACKED PolyBuf {
    uint16_t   count;
    PolyVertex v[10];
} PolyBuf;

typedef struct PACKED ScanEdge {
    int16_t left, right;
} ScanEdge;

typedef struct PACKED Point2 {
    int16_t x, y;
} Point2;

typedef struct PACKED PointBuf {
    uint16_t count;
    Point2   v[10];
} PointBuf;

typedef struct PACKED HullPlane {
    int32_t x, y, z;
} HullPlane;

typedef struct PACKED PcxHeader {
    uint8_t  manufacturer, version, encoding, bits_per_pixel;
    uint16_t xmin, ymin, xmax, ymax;
    uint16_t hdpi, vdpi;
    uint8_t  ega_palette[48];
    uint8_t  reserved;
    uint8_t  planes;
    uint16_t bytes_per_line;
    uint16_t palette_type;
    uint8_t  filler[58];
} PcxHeader;

#include "dataseg.h"

/* ---------------------------------------------------------------------------
 * Cars
 *
 * A carN.dat file is loaded verbatim into the data segment and *is* the car:
 * a fixed header followed by three variable-length sections that the header
 * points to (offsets are relative to the start of the car).
 * ------------------------------------------------------------------------- */

typedef struct PACKED CarVertex {   /* a mass point of the soft body */
    Vec3fx   pos;
    Vec3fx   vel;
    int16_t  radius;                /* collision radius against the ground */
    uint16_t wheel;                 /* 0 = body point, 0xffff = reference point, else wheel */
} CarVertex;

typedef struct PACKED CarSpring {
    uint16_t a, b;                  /* vertex indices */
    int16_t  rest;                  /* rest length (<0: take the current length) */
    int16_t  last;                  /* length on the previous tick */
    uint16_t flags;                 /* low byte: type, bit 7: broken; high byte: softness shift */
    uint16_t min_len, max_len;
} CarSpring;

typedef struct PACKED Car {
    uint16_t vertices_ofs;          /* -> uint16 count + CarVertex[] */
    uint16_t springs_ofs;           /* -> uint16 count + CarSpring[] */
    uint16_t model_ofs;             /* -> display list (see car_draw.c) */
    uint16_t unk_06;
    uint16_t drive;                 /* 0 = rear, 1 = front, 2 = all wheels driven */
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
} Car;

_Static_assert(sizeof(CarVertex) == 0x1c, "CarVertex");
_Static_assert(sizeof(CarSpring) == 0x0e, "CarSpring");
_Static_assert(offsetof(Car, wheel_speed) == 0x42, "Car.wheel_speed");
_Static_assert(sizeof(Car) == 0x84, "Car header");

/* ---------------------------------------------------------------------------
 * Global state
 * ------------------------------------------------------------------------- */

extern DataSeg *G;                  /* the data segment */
extern uint8_t keys[128];           /* [0] = last scancode, [sc] bit 7 clear while held */

/* "Segments": 64 KB blocks allocated at load time and referred to by a
 * 16-bit handle, exactly like the original's DOS memory blocks. */
uint8_t *seg_ptr(uint16_t handle);
int      seg_alloc(uint16_t paragraphs, uint16_t *handle);

static inline uint8_t *ds_ptr(uint16_t ofs) { return (uint8_t *)G + ofs; }
static inline uint16_t ds_ofs(const void *p) { return (uint16_t)((const uint8_t *)p - (const uint8_t *)G); }

static inline Car *car_at(uint16_t ofs) { return (Car *)ds_ptr(ofs); }
static inline Car *car_get(int i) { return car_at(G->car_ofs[i]); }
static inline uint16_t car_vertex_count(Car *c) { return *(uint16_t *)((uint8_t *)c + c->vertices_ofs); }
static inline CarVertex *car_vertices(Car *c) { return (CarVertex *)((uint8_t *)c + c->vertices_ofs + 2); }
static inline uint16_t car_spring_count(Car *c) { return *(uint16_t *)((uint8_t *)c + c->springs_ofs); }
static inline CarSpring *car_springs(Car *c) { return (CarSpring *)((uint8_t *)c + c->springs_ofs + 2); }
static inline CarVertex *car_ref_vertex(Car *c) { return (CarVertex *)((uint8_t *)c + c->ref_vertex_ofs); }

/* Map layers.  Indexing with a uint16_t (x | y << 8) wraps around exactly
 * like the original's 16-bit offsets. */
static inline uint8_t *heightmap(void) { return seg_ptr(G->seg_heightmap); }
static inline uint8_t *colormap(void)  { return seg_ptr(G->seg_colormap); }
static inline uint8_t *screen(void)    { return seg_ptr(G->seg_screen); }

/* ---------------------------------------------------------------------------
 * Modules
 * ------------------------------------------------------------------------- */

/* math.c */
int16_t  fx_sin(uint16_t angle);
int16_t  fx_cos(uint16_t angle);
uint16_t fx_atan2(int16_t y, int16_t x);
int32_t  vec2_length(int16_t a, int16_t b);
int32_t  vec3_length(int16_t a, int16_t b, int16_t c);
Vec3fx   vec3_normalize(int32_t x, int32_t y, int32_t z);
void     project(int16_t x, int16_t depth, int16_t z, int16_t *sx, int16_t *sy);
void     rotate(const int16_t m[9], int16_t x, int16_t y, int16_t z, int16_t out[3]);
void     build_matrix(const int16_t angles[3], int16_t m[9]);
void     transpose(int16_t m[9]);

/* terrain.c */
int16_t  terrain_height(uint16_t x, uint16_t y);
int16_t  ground_probe(uint16_t x, uint16_t y);

/* physics.c */
void     physics_tick(void);
Vec3fx   car_axis_x(CarVertex *v);
Vec3fx   car_axis_y(CarVertex *v);
void     spawn_particle(CarVertex *at);

/* raster.c */
extern const uint8_t *raster_texture;   /* texture for draw_textured */
void     clear_screen(void);
void     put_pixel(int16_t x, int16_t y, uint8_t color);
int16_t  edge_lerp(int16_t da, int16_t va, int16_t db, int16_t vb);
void     poly_begin(const ProjVertex *p, uint32_t attr);
void     poly_add(const ProjVertex *p, uint32_t attr);
uint16_t poly_end(const ProjVertex *p, uint32_t attr);
int      poly_backfacing(void);
void     draw_flat(void);
void     draw_shaded(void);
void     draw_textured(void);

/* scene.c */
extern int poly_hidden;                 /* result of the last facing test */
int      view_transform(int16_t x, int16_t y, int16_t z, ProjVertex *out);
void     draw_terrain(void);
void     draw_particles(void);

/* car_draw.c */
void     draw_car(Car *car);

/* camera.c */
void     camera_update(int view);

/* text.c */
void     draw_text(const char *s, int16_t x, int16_t y, uint8_t color);

/* load.c */
FILE    *data_fopen(const char *name);  /* provided by the host: open a track file for reading */

/* game.c */
int      game_init(void);
int      game_frame(void);
void     game_key(uint8_t scancode);

/* sound.c (AdLib register writes, played by the host if it wants) */
extern void (*opl_write_hook)(uint8_t reg, uint8_t value);
void     opl_write(uint8_t reg, uint8_t value);
void     sound_init(void);
void     sfx_hit(int n);
void     engine_sound(int car_index, uint8_t channel);

#endif
