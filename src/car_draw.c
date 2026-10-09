/*
 * Drawing a car: its vertices are transformed to camera space (with a
 * shadow point on the ground under each one), then the car's model, a
 * small bytecode program stored in the car file, draws polygons using
 * those points.
 *
 * Projected points, in G->car_proj:
 *   [2i]     vertex i
 *   [2i + 1] the ground under vertex i
 *   [2n]     vertex 0 moved along the steered front axle
 *   [2n + 1] vertex 1 moved the other way
 */
#include "terep.h"

void draw_sprite(int16_t sx, int16_t sy, int16_t depth, uint16_t sprite_ofs);

static void transform_point(uint32_t x, uint32_t y, uint32_t z, ProjVertex *out)
{
    view_transform(fx_int(pos_delta(x, G->eye.x)),
                   fx_int(pos_delta(y, G->eye.y)),
                   fx_int(pos_delta(z, G->eye.z)), out);
}

/* Half of the front axle, turned by the steering angle (16.16, length
 * 0x80 map units).  orig 1000:11f0 */
static Vec3fx steered_axle(Car *car)
{
    Vec3fx ax = car_axis_x(car_vertices(car));
    Vec3fx ay = car_axis_y(car_vertices(car));
    int32_t s = (int32_t)((uint32_t)(uint16_t)fx_sin(car->steer_used) << 16);
    int32_t c = (int32_t)((uint32_t)(uint16_t)fx_cos(car->steer_used) << 16);
    Vec3fx r;
    r.x = (int32_t)((uint32_t)(mul_hi32(ay.x, s) + mul_hi32(ax.x, c)) << 7);
    r.y = (int32_t)((uint32_t)(mul_hi32(ay.y, s) + mul_hi32(ax.y, c)) << 7);
    r.z = (int32_t)((uint32_t)(mul_hi32(ay.z, s) + mul_hi32(ax.z, c)) << 7);
    return r;
}

/* orig 1000:1347 */
static void transform_car(Car *car)
{
    CarVertex *v = car_vertices(car);
    uint16_t n = car_vertex_count(car);
    ProjVertex *out = G->car_proj;

    for (uint16_t i = 0; i < n; i++) {
        transform_point(v[i].pos.x, v[i].pos.y, v[i].pos.z, out++);
        int16_t ground = terrain_height(map_int(v[i].pos.x), map_int(v[i].pos.y));
        transform_point(v[i].pos.x, v[i].pos.y, (uint32_t)(uint16_t)ground << 16, out++);
    }
    Vec3fx a = steered_axle(car);
    transform_point(v[0].pos.x + (uint32_t)a.x, v[0].pos.y + (uint32_t)a.y, v[0].pos.z + (uint32_t)a.z, out++);
    transform_point(v[1].pos.x - (uint32_t)a.x, v[1].pos.y - (uint32_t)a.y, v[1].pos.z - (uint32_t)a.z, out++);
}

/* ------------------------------------------------------------------------
 * Model bytecode
 *
 * The program counter is a data-segment offset; all operands are 16-bit
 * little-endian unless noted.  Point operands index car_proj.  "hidden"
 * is the result of the last facing test (shared with the terrain code).
 *
 *  0, 11-15  end of (sub)program
 *  1  p, color             plot a pixel
 *  2  p, q, -              hidden = p is nearer than q
 *  3  p, q, r              hidden = triangle p q r faces away
 *  4  n:8, p*(n+1), color  flat polygon (the last point closes it)
 *  5                       no-op
 *  6  n:8, (p, shade)*(n+1)          Gouraud polygon
 *  7  n:8, base, (p, idx)*(n+1)      Gouraud, shade = word at car_uv_table+idx, + base
 *  8  n:8, (p, uv:32)*(n+1)          textured polygon
 *  9  p, size, uv:32*4     camera-facing square
 * 10  p, q, -, 9 x (w0, w1, uv:32*4) wheel seen from the side: one of nine
 *                          sprites chosen by the viewing angle
 * 16  a, b                 call pc+a and pc+b, nearest first (b first if hidden)
 * 17  rel                  jump
 * 18  rel                  call
 * 19  rel                  jump if not hidden
 * 20  rel                  jump if hidden
 * ---------------------------------------------------------------------- */

static uint16_t rd16(uint16_t pc) { return *(const uint16_t *)ds_ptr(pc); }
static uint32_t rd32(uint16_t pc) { return *(const uint32_t *)ds_ptr(pc); }
static ProjVertex *point(uint16_t i) { return (ProjVertex *)ds_ptr((uint16_t)(0x126 + i * 10)); }

/* Reads the outline of an n-sided polygon (n points + the closing one).
 * Returns the number of points that survived the near plane. */
static uint16_t read_outline(uint16_t *pc, uint8_t n, int kind, uint16_t base)
{
    uint16_t count = 0;
    for (int i = 0; i <= n; i++) {
        uint16_t p = rd16(*pc);
        uint32_t attr;
        *pc += 2;
        switch (kind) {
        case 4:  attr = 0; break;
        case 6:  attr = rd16(*pc); *pc += 2; break;
        case 7:  attr = (uint16_t)(rd16((uint16_t)(0x50e + rd16(*pc))) + base); *pc += 2; break;
        default: attr = rd32(*pc); *pc += 4; break;
        }
        if (i == 0)
            poly_begin(point(p), attr);
        else if (i < n)
            poly_add(point(p), attr);
        else
            count = poly_end(point(p), attr);
    }
    return count;
}

/* Front-facing and not cut away by the near plane? */
static int outline_visible(uint16_t count)
{
    if ((int8_t)count < 3)
        return 0;
    poly_hidden = poly_backfacing();
    return !poly_hidden;
}

/* Wheel sprite (opcode 10).  The angle between the line of sight and the
 * axle p->q picks one of nine views; the sprite is rotated to the axle's
 * direction on screen. */
static void draw_wheel(uint16_t ops)
{
    ProjVertex *a = point(rd16(ops)), *b = point(rd16((uint16_t)(ops + 2)));

    int16_t axle = (int16_t)vec3_length((int16_t)(a->x - b->x), (int16_t)(a->y - b->y), (int16_t)(a->z - b->z));
    int32_t sight = vec3_length(a->x, a->y, a->z);
    int32_t lengths = (int32_t)((uint32_t)sight * (uint32_t)(int32_t)axle);
    uint32_t dot = (uint32_t)((int32_t)(int16_t)(b->x - a->x) * a->x)
                 + (uint32_t)((int32_t)(int16_t)(b->y - a->y) * a->y)
                 + (uint32_t)((int32_t)(int16_t)(b->z - a->z) * a->z);
    int16_t cosine = (int16_t)(((int32_t)(dot << 6) / (lengths >> 9)) >> 1);

    int view = 0;
    for (int i = 0; i < 8; i++) {
        if (cosine < G->wheel_sector_limits[i]) {
            view = 8 - i;
            break;
        }
    }
    uint16_t sp = (uint16_t)(ops + 6 + view * 20);
    int16_t w0 = (int16_t)rd16(sp), w1 = (int16_t)rd16((uint16_t)(sp + 2));

    uint16_t angle = fx_atan2((int16_t)(b->sx - a->sx), (int16_t)(b->sy - a->sy));
    int16_t s = fx_sin(angle), c = fx_cos(angle);
    int16_t depth = a->y;
    int16_t c0 = (int16_t)(mul_q15(w0, c) / depth), s0 = (int16_t)(mul_q15(w0, s) / depth);
    int16_t c1 = (int16_t)(mul_q15(w1, c) / depth), s1 = (int16_t)(mul_q15(w1, s) / depth);

    PolyVertex *o = G->poly_buf[0].v;
    int16_t X = a->sx, Y = a->sy;
    o[0].sx = (int16_t)(X - c0 - s1); o[0].sy = (int16_t)(Y + s0 - c1);
    o[1].sx = (int16_t)(X - c0 + s1); o[1].sy = (int16_t)(Y + s0 + c1);
    o[2].sx = (int16_t)(X + c0 + s1); o[2].sy = (int16_t)(Y - s0 + c1);
    o[3].sx = (int16_t)(X + c0 - s1); o[3].sy = (int16_t)(Y - s0 - c1);
    for (int i = 0; i < 4; i++) {
        uint32_t uv = rd32((uint16_t)(sp + 4 + 4 * i));
        o[i].a = (uint16_t)uv;
        o[i].b = (uint16_t)(uv >> 16);
    }
    G->poly_buf[0].count = 4;
    draw_textured();
}

/* orig 1000:1408 */
static void run_model(uint16_t pc)
{
    for (;;) {
        uint8_t op = *ds_ptr(pc++);
        switch (op & 0x1f) {
        case 1: {
            ProjVertex *p = point(rd16(pc));
            uint8_t color = (uint8_t)rd16((uint16_t)(pc + 2));
            pc += 4;
            if (p->y >= G->near_clip)
                put_pixel(p->sx, p->sy, color);
            break;
        }
        case 2:
            poly_hidden = (int16_t)(point(rd16(pc))->y - point(rd16((uint16_t)(pc + 2)))->y) < 0;
            pc += 6;
            break;
        case 3: {
            ProjVertex *p = point(rd16(pc));
            poly_begin(p, 0);
            poly_add(point(rd16((uint16_t)(pc + 2))), 0);
            poly_add(point(rd16((uint16_t)(pc + 4))), 0);
            poly_end(p, 0);
            pc += 6;
            poly_hidden = poly_backfacing();    /* even if nothing is left */
            break;
        }
        case 4: {
            uint8_t n = *ds_ptr(pc++);
            uint16_t count = read_outline(&pc, n, 4, 0);
            G->poly_color = rd16(pc);
            pc += 2;
            if (outline_visible(count))
                draw_flat();
            break;
        }
        case 5:
            break;
        case 6: {
            uint8_t n = *ds_ptr(pc++);
            if (outline_visible(read_outline(&pc, n, 6, 0)))
                draw_shaded();
            break;
        }
        case 7: {
            uint8_t n = *ds_ptr(pc++);
            G->poly_color = rd16(pc);
            pc += 2;
            if (outline_visible(read_outline(&pc, n, 7, G->poly_color)))
                draw_shaded();
            break;
        }
        case 8: {
            uint8_t n = *ds_ptr(pc++);
            if (outline_visible(read_outline(&pc, n, 8, 0)))
                draw_textured();
            break;
        }
        case 9: {
            ProjVertex *p = point(rd16(pc));
            pc += 2;
            if (p->y >= G->near_clip)
                draw_sprite(p->sx, p->sy, p->y, pc);
            pc += 18;
            break;
        }
        case 10:
            if (point(rd16(pc))->y >= G->near_clip)
                draw_wheel(pc);
            pc += 0xba;
            break;
        case 16: {
            uint16_t a = (uint16_t)(pc + rd16(pc)), b = (uint16_t)(pc + rd16((uint16_t)(pc + 2)));
            if (!poly_hidden) {
                run_model(a);
                run_model(b);
            } else {
                run_model(b);
                run_model(a);
            }
            pc += 4;
            break;
        }
        case 17:
            pc = (uint16_t)(pc + 2 + rd16(pc));
            break;
        case 18:
            run_model((uint16_t)(pc + 2 + rd16(pc)));
            pc += 2;
            break;
        case 19:
            pc = (uint16_t)(pc + 2 + (poly_hidden ? 0 : rd16(pc)));
            break;
        case 20:
            pc = (uint16_t)(pc + 2 + (poly_hidden ? rd16(pc) : 0));
            break;
        default:                        /* 0, 11..15 (21..31 crash the original) */
            return;
        }
    }
}

/* orig 1000:1323 */
void draw_car(Car *car)
{
    raster_texture = seg_ptr(car->texture_seg ? car->texture_seg : G->seg_textures);
    transform_car(car);
    run_model((uint16_t)(ds_ofs(car) + car->model_ofs));
}
