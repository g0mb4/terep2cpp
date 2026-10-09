/*
 * Soft-body car physics, run at a fixed rate (twice per rendered frame in
 * the original timer setup).
 *
 * Every car is a set of mass points (CarVertex) joined by springs.  Each
 * tick: read the controls, spin the wheels, relax the springs, integrate
 * the points against gravity and the ground, then resolve car-vs-car
 * contacts and move the particles.
 */
#include <math.h>
#include "terep.h"

/* ------------------------------------------------------------------------
 * Controls
 * ---------------------------------------------------------------------- */

/* orig 1000:0d2a */
static void car_read_controls(Car *car, const uint8_t binding[6])
{
    int pressed_accel = !(keys[binding[0]] & 0x80);
    int pressed_brake = !(keys[binding[1]] & 0x80);
    int16_t throttle = car->throttle;
    int16_t step = 0x32;

    if (pressed_accel && throttle <= 0x2000) {
        if (throttle < 0)
            step <<= 2;                 /* reversing direction is quicker */
        throttle += step;
    }
    if (pressed_brake && throttle >= -0x2000) {
        if (throttle >= 0)
            step <<= 2;                 /* note: step may already be x4 */
        throttle -= step;
    }
    if (!pressed_accel && !pressed_brake && throttle != 0)
        throttle -= throttle > 0 ? 300 : -300;  /* no clamp at 0: it wobbles */
    car->throttle = throttle;

    int pressed_left = !(keys[binding[2]] & 0x80);
    int pressed_right = !(keys[binding[3]] & 0x80);
    int16_t steer = car->steer;

    /* Steering gets slower with speed.  The original meant to use the
     * absolute value of the speed but negates the low word only and then
     * keeps the high word, so this is really 0x40 - (speed >> 16). */
    uint32_t speed = (uint32_t)(car->wheel_speed[0] + car->wheel_speed[1]);
    step = (int16_t)(0x40 - (uint16_t)(speed >> 16));

    if (pressed_left && steer >= -0x2000) {
        if (steer >= 0)
            step <<= 2;
        steer -= step;
    }
    if (pressed_right && steer <= 0x2000) {
        if (steer < 0)
            step <<= 2;
        steer += step;
    }
    if (!pressed_left && !pressed_right && steer != 0)
        steer -= steer > 0 ? 0x50 : -0x50;
    car->steer = steer;

    car->handbrake = (keys[binding[4]] & 0x80) ? 0 : 1;
}

static inline uint32_t rol32(uint32_t v, int n)
{
    return (v << n) | (v >> (32 - n));
}

/* Wheel friction and engine torque.  orig 1000:1004 */
static void car_update_wheels(Car *car)
{
    int32_t *w = car->wheel_speed;     /* wheel_speed[4] + spin[12] */
    int i;

    car->steer_used = car->steer;
    car->throttle_used = car->throttle;

    for (i = 0; i < 16; i++)
        w[i] -= w[i] >> 7;
    if (car->handbrake & 1)
        for (i = 0; i < 16; i++)
            w[i] -= w[i] >> 2;

    /* The torque is scaled with ROL rather than SHL, which for negative
     * values drags sign bits into the bottom; kept as is. */
    uint32_t torque = (uint32_t)(int32_t)(int16_t)(((int32_t)car->throttle_used * 0x4000) >> 16);
    switch (car->drive) {
    case 0:     /* rear */
        torque = rol32(torque, 4);
        w[2] += (int32_t)torque;
        w[3] += (int32_t)torque;
        break;
    case 1:     /* front */
        torque = rol32(torque, 4);
        w[0] += (int32_t)torque;
        w[1] += (int32_t)torque;
        break;
    default:    /* all wheels */
        torque = rol32(torque, 3);
        w[0] += (int32_t)torque;
        w[1] += (int32_t)torque;
        w[2] += (int32_t)torque;
        w[3] += (int32_t)torque;
        break;
    }
}

/* ------------------------------------------------------------------------
 * Car orientation
 * ---------------------------------------------------------------------- */

/* Unit vector across the car (vertex 6 -> vertex 7).  orig 1000:1091 */
Vec3fx car_axis_x(CarVertex *v)
{
    return vec3_normalize(pos_delta(v[7].pos.x, v[6].pos.x),
                          pos_delta(v[7].pos.y, v[6].pos.y),
                          pos_delta(v[7].pos.z, v[6].pos.z));
}

/* Unit vector along the car ((4 - 6) + (5 - 7)).  orig 1000:10b6 */
Vec3fx car_axis_y(CarVertex *v)
{
    return vec3_normalize(pos_delta(v[4].pos.x + v[5].pos.x, v[6].pos.x + v[7].pos.x),
                          pos_delta(v[4].pos.y + v[5].pos.y, v[6].pos.y + v[7].pos.y),
                          pos_delta(v[4].pos.z + v[5].pos.z, v[6].pos.z + v[7].pos.z));
}

static struct {
    unsigned valid;                 /* bit 0: steered, bit 1: straight (orig 1000:0e67) */
    Vec3fx   steered;               /* orig 1000:12a7 */
    Vec3fx   straight;              /* orig 1000:12bf */
} wheel_dirs;

/* Longitudinal axis turned by the steering angle.  orig 1000:1136 */
static void compute_steered_dir(Car *car)
{
    Vec3fx ax = car_axis_x(car_vertices(car));
    Vec3fx ay = car_axis_y(car_vertices(car));
    int32_t s = (int32_t)((uint32_t)(uint16_t)fx_sin(car->steer_used) << 16);
    int32_t c = (int32_t)((uint32_t)(uint16_t)fx_cos(car->steer_used) << 16);

    wheel_dirs.straight = ay;
    wheel_dirs.steered.x = (mul_hi32(ay.x, c) - mul_hi32(ax.x, s)) << 1;
    wheel_dirs.steered.y = (mul_hi32(ay.y, c) - mul_hi32(ax.y, s)) << 1;
    wheel_dirs.steered.z = (mul_hi32(ay.z, c) - mul_hi32(ax.z, s)) << 1;
}

/* Rolling direction of wheel i (vertices 0..3).  orig 1000:0e69
 * prev_edx stands for the value EDX happened to hold: the original returns
 * it unchanged for vertices that are not wheels 0..3. */
static Vec3fx wheel_direction(Car *car, uint16_t i, int32_t *speed, int32_t prev_edx)
{
    Vec3fx zero = { 0, 0, 0 };
    switch (i) {
    case 0:
    case 1:
        if (!(wheel_dirs.valid & 1)) {
            compute_steered_dir(car);
            wheel_dirs.valid |= 1;
        }
        *speed = car->wheel_speed[i];
        return wheel_dirs.steered;
    case 2:
    case 3:
        if (!(wheel_dirs.valid & 2)) {
            wheel_dirs.straight = car_axis_y(car_vertices(car));
            wheel_dirs.valid |= 2;
        }
        *speed = car->wheel_speed[i];
        return wheel_dirs.straight;
    default:
        *speed = prev_edx;
        return zero;
    }
}

/* Feed the ground speed back into the wheel: free-rolling wheels follow
 * the ground completely, driven ones only by a quarter.  orig 1000:0f67 */
static void wheel_set_speed(Car *car, uint16_t i, int32_t ground_speed)
{
    int driven;
    switch (i) {
    case 0: case 1: driven = car->drive != 0; break;
    case 2: case 3: driven = car->drive & 1; break;
    default: return;
    }
    int32_t *w = &car->wheel_speed[i];
    if (driven)
        *w = ((*w - ground_speed) >> 2) + ground_speed;
    else
        *w = ground_speed;
}

/* Middle of the body, used by the cameras.  orig 1000:0e28 */
static void car_update_mid(Car *car)
{
    CarVertex *v = car_vertices(car);
    car->mid[0] = (int16_t)((map_int(v[4].pos.x) >> 1) + (map_int(v[6].pos.x) >> 1));
    car->mid[1] = (int16_t)((map_int(v[4].pos.y) >> 1) + (map_int(v[6].pos.y) >> 1));
    car->mid[2] = (int16_t)((map_int(v[4].pos.z) >> 1) + (map_int(v[6].pos.z) >> 1));
    wheel_dirs.valid = 0;
}

/* ------------------------------------------------------------------------
 * Springs
 * ---------------------------------------------------------------------- */

/* CVTSS2SI: round to nearest, out-of-range gives 0x80000000. */
static int32_t float_to_i32(float f)
{
    if (!(f > -2147483648.0f && f < 2147483648.0f))
        return INT32_MIN;
    return (int32_t)lrintf(f);
}

enum {
    SPRING_LIMITS = 0,      /* rope: only acts outside [min_len, max_len] */
    SPRING_ELASTIC = 1,     /* spring that bends permanently or breaks */
    SPRING_BROKEN = 0x80,   /* flag; a broken spring behaves like a damper */
};

/* orig 1000:4e0a (the length used SSE in the Win16 port) */
static void car_update_springs(Car *car)
{
    CarVertex *verts = car_vertices(car);
    CarSpring *s = car_springs(car);
    uint16_t n = car_spring_count(car);

    do {
        CarVertex *pa = &verts[s->a];
        CarVertex *pb = &verts[s->b];
        int32_t dx = pos_delta(pa->pos.x, pb->pos.x);
        int32_t dy = pos_delta(pa->pos.y, pb->pos.y);
        int32_t dz = pos_delta(pa->pos.z, pb->pos.z);

        /* length one step ahead: positions plus relative velocity */
        float fx = (float)(dx + (pa->vel.x - pb->vel.x));
        float fy = (float)(dy + (pa->vel.y - pb->vel.y));
        float fz = (float)(dz + (pa->vel.z - pb->vel.z));
        fx *= fx;
        fy *= fy;
        fz *= fz;
        int32_t len = float_to_i32(sqrtf((fy + fx) + fz)) >> 10;

        int32_t rest = s->rest;
        int32_t force, ref;

        if (rest < 0) {                 /* first tick: take the current length */
            s->rest = (int16_t)len;
            s->last = (int16_t)len;
            goto next;
        }

        uint8_t type = (uint8_t)s->flags;
        if (type == SPRING_ELASTIC) {
            force = rest - len;
            if (force == 0)
                goto next;
            if ((int16_t)force > G->spring_break_stretch) {
                /* overstretched: the rest length gives way a little */
                force >>= 4;
                s->rest = (int16_t)(rest - force);
                s->last = s->rest;
                ref = len;
            } else if ((int16_t)force < G->spring_break_squash) {
                s->flags |= SPRING_BROKEN;
                goto next;
            } else {
                ref = len;
            }
        } else {
            int32_t limit;
            if (len > s->max_len)
                limit = s->max_len;
            else if (len < s->min_len)
                limit = s->min_len;
            else if (type == SPRING_LIMITS)
                goto next;
            else {
                /* damper: pull to the rest length and resist the change
                 * since the last tick */
                force = (rest - len) / (int32_t)(type >> 1);
                force += ((int32_t)(uint16_t)s->last - len) >> 1;
                s->last = (int16_t)len;
                ref = len;
                goto apply;
            }
            force = (limit - len) >> 1;
            if (force == 0)
                goto next;
            s->last = (int16_t)len;
            ref = len;
        }
apply:
        {
            /* split the impulse between both ends; the high byte of the
             * flags makes the spring act more on one end than the other */
            int32_t f64 = force << 6;
            int64_t r64 = (int64_t)(ref << 6);
            int shift = (uint8_t)((s->flags >> 8) + 1) & 31;
            int32_t d[3] = { dx, dy, dz };
            int32_t *va[3] = { &pa->vel.x, &pa->vel.y, &pa->vel.z };
            int32_t *vb[3] = { &pb->vel.x, &pb->vel.y, &pb->vel.z };
            for (int k = 0; k < 3; k++) {
                int32_t f = (int32_t)(((int64_t)d[k] * f64) / r64);
                int32_t part = f >> shift;
                *va[k] += part;
                *vb[k] -= f - part;
            }
        }
next:
        s++;
    } while (--n);
}

/* ------------------------------------------------------------------------
 * Ground contact
 * ---------------------------------------------------------------------- */

/* The contact frame: n1 = (sin a, cos a), n2 = (sin b, cos b) as Q31.
 * The surface normal is (-n1.sin, -n2.sin, n1.cos * n2.cos). */
static void set_contact_frame(int16_t s1, int16_t c1, int16_t s2, int16_t c2)
{
    /* only the high words are ever written; the low words stay 0 */
    G->ground_n1[0] = (int32_t)((uint32_t)(uint16_t)s1 << 16);
    G->ground_n1[1] = (int32_t)((uint32_t)(uint16_t)c1 << 16);
    G->ground_n2[0] = (int32_t)((uint32_t)(uint16_t)s2 << 16);
    G->ground_n2[1] = (int32_t)((uint32_t)(uint16_t)c2 << 16);
}

#define N1S (G->ground_n1[0])
#define N1C (G->ground_n1[1])
#define N2S (G->ground_n2[0])
#define N2C (G->ground_n2[1])

static inline int32_t mulq31(int32_t a, int32_t b) { return mul_hi32(a, b) << 1; }

/* Shared by the slide/grip helpers below (orig scratch at 1000:e9c8..e9da) */
static struct {
    int32_t  penetration;           /* velocity into the ground (negative) */
    int32_t  slide[2];              /* tangential velocity in the contact frame */
    int32_t  slip_sin, slip_cos;    /* wheel heading in the contact plane */
    int32_t  wheel_speed;
} contact;

/* Friction limit for this surface.  Returns 1 while the tyre/body grips,
 * 0 when the tangential speed is beyond what the surface holds (the point
 * slides).  *div scales the slide down in that case.  orig 1000:4c68 */
static int contact_grips(int32_t *div)
{
    int32_t a = contact.slide[0] < 0 ? -contact.slide[0] : contact.slide[0];
    int32_t b = contact.slide[1] < 0 ? -contact.slide[1] : contact.slide[1];
    int32_t slide = a + b;
    unsigned surf = G->ground_surface >> 4;
    int32_t grip = G->surface_grip[surf];
    int32_t load = -contact.penetration;
    if (load >= G->surface_max_load[surf])
        load = G->surface_max_load[surf];
    int32_t limit = load / grip;
    *div = grip >> 1;
    return limit > (slide >> 6);
}

/* Spawn a particle (dust, smoke) where a point slides.  orig 1000:4d96 */
void spawn_particle(CarVertex *v)
{
    uint16_t at = G->particle_bytes;
    if (at >= 200 * sizeof(Particle))
        return;
    Particle *p = (Particle *)ds_ptr((uint16_t)(offsetof(DataSeg, particles) + at));
    p->pos.x = v->pos.x;
    p->pos.y = v->pos.y;
    p->pos.z = v->pos.z - ((uint32_t)(uint16_t)v->radius << 16);
    p->vel.x = mulq31(contact.slide[0], N1C);
    p->vel.y = mulq31(contact.slide[1], N2C);
    p->vel.z = v->vel.z;
    p->kind = G->ground_surface;
    p->life = 100;
    G->particle_bytes += sizeof(Particle);
}

/* Body point: plain friction.  orig 1000:4cc3 */
static void body_friction(CarVertex *v)
{
    int32_t sx = contact.slide[0], sy = contact.slide[1];
    int32_t div;
    if (!contact_grips(&div)) {
        spawn_particle(v);
        sy = contact.slide[1] / div;
        sx = contact.slide[0] / div;
    }
    int32_t d;
    d = mul_hi32(sx, N1C);
    v->vel.x -= d;
    v->pos.x -= d;
    d = mul_hi32(sy, N2C);
    v->vel.y -= d;
    v->pos.y -= d;
}

/* Wheel: friction across the wheel, rolling along it.  orig 1000:4d0e */
static void wheel_friction(CarVertex *v)
{
    int32_t div;
    if (!contact_grips(&div)) {
        spawn_particle(v);
        int32_t sy = contact.slide[1] / div;
        int32_t sx = contact.slide[0] / div;
        contact.slide[0] = sx;
        contact.slide[1] = sy;
    }
    /* the part of the slide along the wheel goes into the wheel */
    int32_t along = mul_hi32(contact.slide[0], contact.slip_sin)
                  - mul_hi32(contact.slide[1], contact.slip_cos);
    contact.wheel_speed -= along;
    contact.slide[0] += mul_hi32(along, contact.slip_sin);
    contact.slide[1] -= mul_hi32(along, contact.slip_cos);

    int32_t d;
    d = mul_hi32(contact.slide[0], N1C);
    v->vel.x -= d;
    v->pos.x -= d;
    d = mul_hi32(contact.slide[1], N2C);
    v->vel.y -= d;
    v->pos.y -= d;
}

/* Heading of the wheel in the contact plane.  orig 1000:4c26 */
static uint16_t wheel_heading(Vec3fx dir)
{
    int16_t a = (int16_t)(-(int16_t)mul_hi32(dir.x >> 4, N1C) - (int16_t)mul_hi32(dir.z >> 4, N1S));
    int16_t b = (int16_t)((int16_t)mul_hi32(dir.y >> 4, N2C) + (int16_t)mul_hi32(dir.z >> 4, N2S));
    return fx_atan2(a, b);
}

/* Push a point out of the ground (or a map wall) along the contact
 * normal and apply friction.  orig 1000:4a71 */
static void ground_contact(Car *car, CarVertex *v, uint16_t index)
{
    int32_t vn = mulq31(mulq31(v->vel.z, N1C), N2C)
               - mulq31(v->vel.x, N1S)
               - mulq31(v->vel.y, N2S);
    if (vn >= 0)
        return;                         /* moving away from the surface */

    contact.penetration = vn;
    if (vn <= -0x60000)
        sfx_hit(0);

    int32_t bounce = vn + (vn >> 2);
    int32_t d;
    d = mul_hi32(N1S, bounce);
    v->vel.x += d;
    v->pos.x += d << 1;
    d = mul_hi32(N2S, bounce);
    v->vel.y += d;
    v->pos.y += d << 1;
    int32_t edx = mul_hi32(mulq31(bounce, N1C), N2C);
    v->vel.z -= edx;
    v->pos.z -= edx << 1;

    contact.slide[0] = mulq31(v->vel.x, N1C) + mulq31(v->vel.z, N1S);
    contact.slide[1] = mulq31(v->vel.y, N2C) + mulq31(v->vel.z, N2S);

    if (v->wheel == 0 || v->wheel == 0xffff) {
        body_friction(v);
        return;
    }

    int32_t speed;
    Vec3fx dir = wheel_direction(car, index, &speed, mulq31(v->vel.z, N2S));
    G->tmp_wheel_dir = dir;
    contact.wheel_speed = -speed;
    uint16_t heading = wheel_heading(dir);
    contact.slip_cos = (int32_t)((uint32_t)(uint16_t)fx_cos(heading) << 16);
    contact.slip_sin = (int32_t)((uint32_t)(uint16_t)fx_sin(heading) << 16);
    int32_t w2 = contact.wheel_speed << 1;
    contact.slide[0] += mul_hi32(w2, contact.slip_sin);
    contact.slide[1] -= mul_hi32(w2, contact.slip_cos);
    wheel_friction(v);
    wheel_set_speed(car, index, -contact.wheel_speed);
}

/* Integrate every point of the car.  orig 1000:48db */
static void car_update_points(Car *car)
{
    car_update_mid(car);
    CarVertex *v = car_vertices(car);
    uint16_t n = car_vertex_count(car);
    uint16_t i = 0;

    do {
        v->vel.z -= G->gravity;

        int16_t z = (int16_t)map_int(v->pos.z);
        int16_t ground = (int16_t)(ground_probe(map_int(v->pos.x), map_int(v->pos.y)) + v->radius);
        if ((int16_t)(ground - z) >= 0) {
            /* below the ground: damp by surface, then collide */
            int shift = G->surface_damp_shift[G->ground_surface >> 4];
            if (shift) {
                v->vel.x -= v->vel.x >> shift;
                v->vel.y -= v->vel.y >> shift;
                v->vel.z -= v->vel.z >> shift;
            }
            uint16_t a = fx_atan2(G->ground_slope_x, 0x100);
            uint16_t b = fx_atan2(G->ground_slope_y, 0x100);
            set_contact_frame(fx_sin(a), fx_cos(a), fx_sin(b), fx_cos(b));
            ground_contact(car, v, i);
        }

        /* invisible walls half a cell inside the map border */
        if ((uint16_t)(map_int(v->pos.x) - v->radius) < 0x80) {
            set_contact_frame((int16_t)0x8000, 0, 0, 0x7f00);
            G->ground_surface = 0;
            ground_contact(car, v, i);
        }
        if ((uint16_t)(map_int(v->pos.y) - v->radius) < 0x80) {
            set_contact_frame(0, 0x7fff, (int16_t)0x8000, 0);
            G->ground_surface = 0;
            ground_contact(car, v, i);
        }
        if ((uint16_t)(map_int(v->pos.x) + v->radius) > 0xfe80) {
            set_contact_frame(0x7fff, 0, 0, 0x7fff);
            G->ground_surface = 0;
            ground_contact(car, v, i);
        }
        if ((uint16_t)(map_int(v->pos.y) + v->radius) > 0xfe80) {
            set_contact_frame(0, 0x7fff, 0x7fff, 0);
            G->ground_surface = 0;
            ground_contact(car, v, i);
        }

        v->pos.x += v->vel.x;
        v->pos.y += v->vel.y;
        v->pos.z += v->vel.z;
        v++;
        i++;
    } while (--n);
}

/* ------------------------------------------------------------------------
 * Car vs car
 * ---------------------------------------------------------------------- */

static int32_t hull_normal_component(int32_t a0, int32_t a1, int32_t b0, int32_t b1)
{
    return (a0 >> 14) * (b0 >> 14) - (a1 >> 14) * (b1 >> 14);
}

/* Build the collision planes of a car from the hull triangles: for each
 * triangle a point and an (unnormalised) normal.  orig 1000:5091 */
static void car_build_hull(Car *car)
{
    CarVertex *verts = car_vertices(car);
    G->hull_plane_bytes = 0;
    for (int t = 0; t < G->hull_tri_count; t++) {
        Vec3pos *p = &verts[G->hull_tri[t][0]].pos;
        Vec3pos *q = &verts[G->hull_tri[t][1]].pos;
        Vec3pos *r = &verts[G->hull_tri[t][2]].pos;
        int32_t pqx = pos_delta(p->x, q->x), pqy = pos_delta(p->y, q->y), pqz = pos_delta(p->z, q->z);
        int32_t prx = pos_delta(p->x, r->x), pry = pos_delta(p->y, r->y), prz = pos_delta(p->z, r->z);
        HullPlane n;
        /* cross product of (p - q) and (p - r) */
        n.x = hull_normal_component(pqy, pry, prz, pqz);
        n.y = -hull_normal_component(prz, pqz, pqx, prx);
        n.z = hull_normal_component(prx, pqx, pqy, pry);
        int k = G->hull_plane_bytes / sizeof(HullPlane);
        G->hull_normal[k] = n;
        G->hull_point[k] = *p;
        G->hull_plane_bytes += sizeof(HullPlane);
    }
}

/* Push the points of car b that are inside the hull of car a.
 * orig 1000:51bd */
static void car_push_points(Car *a, Car *b)
{
    CarVertex *v = car_vertices(b);
    uint16_t n = car_vertex_count(b);
    int planes = G->hull_plane_bytes / sizeof(HullPlane);

    do {
        int best = 0;
        int32_t best_d = INT32_MIN;
        int inside = 1;
        for (int k = 0; k < planes; k++) {
            Vec3pos *q = &G->hull_point[k];
            HullPlane *nrm = &G->hull_normal[k];
            int32_t d = (pos_delta(v->pos.x, q->x) >> 16) * nrm->x
                      + (pos_delta(v->pos.y, q->y) >> 16) * nrm->y
                      + (pos_delta(v->pos.z, q->z) >> 16) * nrm->z;
            if (d >= 0) {
                inside = 0;
                break;
            }
            if (d > best_d) {
                best_d = d;
                best = k;
            }
        }
        if (inside) {
            HullPlane *nrm = &G->hull_normal[best];
            v->vel.x += nrm->x;
            v->vel.y += nrm->y;
            v->vel.z += nrm->z;

            /* The reaction goes to the point of car a with the smallest
             * |x|+|y|+|z|, the integer parts taken as signed 16-bit (so
             * nearest to a corner of the map, not to v).  Surely meant to
             * be the closest point to v, but that's what it does. */
            CarVertex *av = car_vertices(a), *target = av;
            int16_t best_l1 = 0x7fff;
            for (uint16_t j = car_vertex_count(a); j; j--, av++) {
                int16_t l1 = (int16_t)(abs16((int16_t)map_int(av->pos.x)) + abs16((int16_t)map_int(av->pos.y)) + abs16((int16_t)map_int(av->pos.z)));
                if (l1 < best_l1) {
                    best_l1 = l1;
                    target = av;
                }
            }
            target->vel.x -= nrm->x >> 1;
            v->vel.x += nrm->x >> 1;
            target->vel.y -= nrm->y >> 1;
            v->vel.y += nrm->y >> 1;
            target->vel.z -= nrm->z >> 1;
            v->vel.z += nrm->z >> 1;
            spawn_particle(v);
        }
        v++;
    } while (--n);
}

/* orig 1000:500b */
static void cars_collide(void)
{
    G->ground_surface = 0;
    for (int i = 0; i < G->num_cars; i++) {
        Car *a = car_get(i);
        car_build_hull(a);
        for (int j = 0; j < G->num_cars; j++) {
            Car *b = car_get(j);
            if (b == a)
                continue;
            CarVertex *ra = car_vertices(a), *rb = car_vertices(b);
            int16_t dx = (int16_t)(map_int(ra->pos.x) - map_int(rb->pos.x));
            int16_t dy = (int16_t)(map_int(ra->pos.y) - map_int(rb->pos.y));
            int16_t dz = (int16_t)(map_int(ra->pos.z) - map_int(rb->pos.z));
            if (dx > 0x200 || dx < -0x200 || dy > 0x200 || dy < -0x200 || dz > 0x200 || dz < -0x200)
                continue;
            car_push_points(a, b);
        }
    }
}

/* ------------------------------------------------------------------------
 * Particles
 * ---------------------------------------------------------------------- */

/* orig 1000:0bb5 */
static void particles_update(void)
{
    uint16_t i = 0;
    while (i < G->particle_bytes / sizeof(Particle)) {
        Particle *p = &G->particles[i];
        p->life -= 2;
        if (p->life < 0) {
            /* dead: move the last particle into this slot and redo it */
            G->particle_bytes -= sizeof(Particle);
            if (G->particle_bytes == 0)
                return;
            *p = G->particles[G->particle_bytes / sizeof(Particle)];
            continue;
        }
        p->pos.x += p->vel.x;
        p->pos.y += p->vel.y;
        p->pos.z += p->vel.z;
        if (p->kind == 0x0f) {
            /* ice chip: falls until it hits the ground, then chips the ice */
            uint16_t x = map_int(p->pos.x), y = map_int(p->pos.y);
            int16_t z = (int16_t)map_int(p->pos.z);
            int16_t h = terrain_height(x, y);
            if ((int16_t)(h - z) < 0) {
                p->vel.z -= G->gravity >> 1;
            } else {
                uint16_t cell = (uint16_t)((y & 0xff00) | ((uint16_t)x >> 8));
                uint8_t *col = &colormap()[cell];
                if (*col & 0x0f)
                    (*col)--;
                cell = (uint16_t)(((uint16_t)(y + 0x80) & 0xff00) | ((uint16_t)(x + 0x80) >> 8));
                heightmap()[cell]--;
                p->kind = 1;
                p->vel.x = 0;
                p->vel.y = 0;
                p->vel.z = 10000;
            }
        }
        i++;
    }
}

/* Keys 1 and 2: kick the lowest point of the player cars upwards.
 * orig 1000:0a3b / 1000:0a82 */
static void car_jump(Car *car, int keep_point)
{
    CarVertex *v = car_vertices(car);
    uint16_t n = car_vertex_count(car);
    int32_t kick = (int32_t)((n >> 1) + 1) * G->gravity;

    if (!keep_point) {
        CarVertex *low = v;
        int16_t low_z = (int16_t)map_int(v->pos.z);  /* compared signed, as the original */
        for (uint16_t i = 1; i < n; i++) {
            if ((int16_t)map_int(v[i].pos.z) < low_z) {
                low_z = (int16_t)map_int(v[i].pos.z);
                low = &v[i];
            }
        }
        car->lowest_vertex = ds_ofs(low);
        /* quirk: the original searches with AX, which is the low half of
         * the kick, so the low word ends up as the last vertex's height */
        kick = (int32_t)(((uint32_t)kick & 0xffff0000u) | map_int(v[n - 1].pos.z));
    }
    /* bug kept: a car that was never kicked has lowest_vertex = 0, so a key
     * still held after switching to it adds to DS 0x14..0x17, which is the
     * "VEHICLE" in the title text */
    ((CarVertex *)ds_ptr(car->lowest_vertex))->vel.z += kick;
}

static void jump_keys(void)
{
    /* the key state goes 0xff (up) -> 0x7f (down) -> 0x3f (held, seen);
     * PF of (state & 0xc0) tells "held" apart from "just pressed" */
    for (int p = 0; p < 2; p++) {
        uint8_t *k = &keys[2 + p];
        int held = parity_even(*k & 0xc0);
        if (!(*k & 0x80)) {
            *k &= 0x3f;
            car_jump(car_get(G->player_car[p]), held);
        }
    }
}

/* ------------------------------------------------------------------------ */

/* orig 1000:5680 (timer interrupt handler in the DOS version) */
void physics_tick(void)
{
    if (G->sim_running != 1)
        return;
    for (int i = 0; i < G->num_cars; i++) {
        Car *car = car_get(i);
        car_read_controls(car, G->car_keys[i]);
        car_update_wheels(car);
        car_update_springs(car);
        car_update_points(car);
    }
    cars_collide();
    particles_update();
    jump_keys();
}
