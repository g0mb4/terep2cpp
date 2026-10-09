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
#include "terep.hpp"

static void transform_point(Pos p, ProjVertex *out)
{
    Vec3 d = p - G->eye;
    view_transform(fx_int(d.x), fx_int(d.y), fx_int(d.z), out);
}

/* Half of the front axle, turned by the steering angle (16.16, length
 * 0x80 map units).  orig 1000:11f0 */
static Vec3 steered_axle(Car &car)
{
    Vec3 ax = car.axis_x(), ay = car.axis_y();
    int32_t s = q31(fx_sin(car.steer_used)), c = q31(fx_cos(car.steer_used));
    auto turn = [&](int32_t y, int32_t x) { return (int32_t)((uint32_t)(mul_hi32(y, s) + mul_hi32(x, c)) << 7); };
    return { turn(ay.x, ax.x), turn(ay.y, ax.y), turn(ay.z, ax.z) };
}

/* orig 1000:1347 */
static void transform_car(Car &car)
{
    CarVertex *v = car.vertices();
    uint16_t n = car.vertex_count();
    ProjVertex *out = G->car_proj;

    for (uint16_t i = 0; i < n; i++) {
        Pos p = v[i].pos;
        transform_point(p, out++);
        p.z = Coord::from_units((uint16_t)terrain_height(p.x.units(), p.y.units()));
        transform_point(p, out++);
    }
    Vec3 a = steered_axle(car);
    transform_point(v[0].pos + a, out++);
    transform_point(v[1].pos - a, out++);
}

/* ------------------------------------------------------------------------
 * Model bytecode
 *
 * The program counter is a data-segment offset; all operands are 16-bit
 * little-endian unless noted.  Point operands index car_proj.  "hidden"
 * is the result of the last facing test (shared with the terrain code).
 * Only the low 5 bits of an opcode count.
 * ---------------------------------------------------------------------- */

enum Op : uint8_t {
    OP_END = 0,         /* also 11..15: end of (sub)program */
    OP_PIXEL = 1,       /* p, color             plot a pixel */
    OP_NEARER = 2,      /* p, q, -              hidden = p is nearer than q */
    OP_FACING = 3,      /* p, q, r              hidden = triangle p q r faces away */
    OP_FLAT = 4,        /* n:8, p*(n+1), color  flat polygon (the last point closes it) */
    OP_NOP = 5,
    OP_SHADED = 6,      /* n:8, (p, shade)*(n+1)        Gouraud polygon */
    OP_SHADED_TAB = 7,  /* n:8, base, (p, idx)*(n+1)    Gouraud, shade = word at car_uv_table+idx, + base */
    OP_TEXTURED = 8,    /* n:8, (p, uv:32)*(n+1)        textured polygon */
    OP_SPRITE = 9,      /* p, size, uv:32*4     camera-facing square */
    OP_WHEEL = 10,      /* p, q, -, 9 x (w0, w1, uv:32*4)  wheel seen from the side: one of
                         * nine sprites chosen by the viewing angle */
    OP_SORT2 = 16,      /* a, b                 call pc+a and pc+b, nearest first (b first if hidden) */
    OP_JUMP = 17,       /* rel */
    OP_CALL = 18,       /* rel */
    OP_JUMP_VISIBLE = 19,   /* rel              jump if not hidden */
    OP_JUMP_HIDDEN = 20,    /* rel              jump if hidden */
};

static uint16_t rd16(uint16_t pc) { return *reinterpret_cast<const uint16_t *>(ds_ptr(pc)); }
static uint32_t rd32(uint16_t pc) { return *reinterpret_cast<const uint32_t *>(ds_ptr(pc)); }
static const ProjVertex &point(uint16_t i) { return *reinterpret_cast<const ProjVertex *>(ds_ptr((uint16_t)(offsetof(DataSeg, car_proj) + i * sizeof(ProjVertex)))); }

/* Reads the outline of an n-sided polygon (n points + the closing one).
 * Returns the number of points that survived the near plane. */
static uint16_t read_outline(uint16_t &pc, uint8_t n, Op kind, uint16_t base)
{
    uint16_t count = 0;
    for (int i = 0; i <= n; i++) {
        const ProjVertex &p = point(rd16(pc));
        uint32_t attr;
        pc += 2;
        switch (kind) {
        case OP_FLAT:       attr = 0; break;
        case OP_SHADED:     attr = rd16(pc); pc += 2; break;
        case OP_SHADED_TAB: attr = (uint16_t)(rd16((uint16_t)(offsetof(DataSeg, car_uv_table) + rd16(pc))) + base); pc += 2; break;
        default:            attr = rd32(pc); pc += 4; break;
        }
        if (i == 0)
            poly_begin(p, attr);
        else if (i < n)
            poly_add(p, attr);
        else
            count = poly_end(p, attr);
    }
    return count;
}

/* Front-facing and not cut away by the near plane? */
static bool outline_visible(uint16_t count)
{
    if ((int8_t)count < 3)
        return false;
    poly_hidden = poly_backfacing();
    return !poly_hidden;
}

/* Wheel sprite (OP_WHEEL).  The angle between the line of sight and the
 * axle p->q picks one of nine views; the sprite is rotated to the axle's
 * direction on screen. */
static void draw_wheel(uint16_t ops)
{
    const ProjVertex &a = point(rd16(ops)), &b = point(rd16((uint16_t)(ops + 2)));

    int16_t axle = (int16_t)vec3_length((int16_t)(a.x - b.x), (int16_t)(a.y - b.y), (int16_t)(a.z - b.z));
    int32_t sight = vec3_length(a.x, a.y, a.z);
    int32_t lengths = (int32_t)((uint32_t)sight * (uint32_t)(int32_t)axle);
    uint32_t dot = (uint32_t)((int32_t)(int16_t)(b.x - a.x) * a.x)
                 + (uint32_t)((int32_t)(int16_t)(b.y - a.y) * a.y)
                 + (uint32_t)((int32_t)(int16_t)(b.z - a.z) * a.z);
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

    uint16_t angle = fx_atan2((int16_t)(b.sx - a.sx), (int16_t)(b.sy - a.sy));
    int16_t s = fx_sin(angle), c = fx_cos(angle);
    int16_t depth = a.y;
    int16_t c0 = (int16_t)(mul_q15(w0, c) / depth), s0 = (int16_t)(mul_q15(w0, s) / depth);
    int16_t c1 = (int16_t)(mul_q15(w1, c) / depth), s1 = (int16_t)(mul_q15(w1, s) / depth);

    PolyVertex *o = G->poly_buf[0].v;
    int16_t X = a.sx, Y = a.sy;
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
        Op op = (Op)(*ds_ptr(pc++) & 0x1f);
        switch (op) {
        case OP_PIXEL: {
            const ProjVertex &p = point(rd16(pc));
            uint8_t color = (uint8_t)rd16((uint16_t)(pc + 2));
            pc += 4;
            if (p.y >= G->near_clip)
                put_pixel(p.sx, p.sy, color);
            break;
        }
        case OP_NEARER:
            poly_hidden = (int16_t)(point(rd16(pc)).y - point(rd16((uint16_t)(pc + 2))).y) < 0;
            pc += 6;
            break;
        case OP_FACING: {
            const ProjVertex &p = point(rd16(pc));
            poly_begin(p, 0);
            poly_add(point(rd16((uint16_t)(pc + 2))), 0);
            poly_add(point(rd16((uint16_t)(pc + 4))), 0);
            poly_end(p, 0);
            pc += 6;
            poly_hidden = poly_backfacing();    /* even if nothing is left */
            break;
        }
        case OP_FLAT: {
            uint8_t n = *ds_ptr(pc++);
            uint16_t count = read_outline(pc, n, OP_FLAT, 0);
            G->poly_color = rd16(pc);
            pc += 2;
            if (outline_visible(count))
                draw_flat();
            break;
        }
        case OP_NOP:
            break;
        case OP_SHADED: {
            uint8_t n = *ds_ptr(pc++);
            if (outline_visible(read_outline(pc, n, OP_SHADED, 0)))
                draw_shaded();
            break;
        }
        case OP_SHADED_TAB: {
            uint8_t n = *ds_ptr(pc++);
            G->poly_color = rd16(pc);
            pc += 2;
            if (outline_visible(read_outline(pc, n, OP_SHADED_TAB, G->poly_color)))
                draw_shaded();
            break;
        }
        case OP_TEXTURED: {
            uint8_t n = *ds_ptr(pc++);
            if (outline_visible(read_outline(pc, n, OP_TEXTURED, 0)))
                draw_textured();
            break;
        }
        case OP_SPRITE: {
            const ProjVertex &p = point(rd16(pc));
            pc += 2;
            if (p.y >= G->near_clip)
                draw_sprite(p.sx, p.sy, p.y, pc);
            pc += 18;
            break;
        }
        case OP_WHEEL:
            if (point(rd16(pc)).y >= G->near_clip)
                draw_wheel(pc);
            pc += 0xba;
            break;
        case OP_SORT2: {
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
        case OP_JUMP:
            pc = (uint16_t)(pc + 2 + rd16(pc));
            break;
        case OP_CALL:
            run_model((uint16_t)(pc + 2 + rd16(pc)));
            pc += 2;
            break;
        case OP_JUMP_VISIBLE:
            pc = (uint16_t)(pc + 2 + (poly_hidden ? 0 : rd16(pc)));
            break;
        case OP_JUMP_HIDDEN:
            pc = (uint16_t)(pc + 2 + (poly_hidden ? rd16(pc) : 0));
            break;
        default:                        /* 0, 11..15 (21..31 crash the original) */
            return;
        }
    }
}

/* orig 1000:1323 */
void draw_car(Car &car)
{
    raster_texture = seg_ptr(car.texture_seg ? car.texture_seg : G->seg_textures);
    transform_car(car);
    run_model((uint16_t)(ds_ofs(&car) + car.model_ofs));
}
