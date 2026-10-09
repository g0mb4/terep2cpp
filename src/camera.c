/*
 * The five cameras (F1..F5; F6 cycles the second player's):
 *   0  mounted on the car, following its roll and pitch
 *   1  mounted on the car, pitch and roll smoothed from the terrain ahead
 *   2  standing still where the chase camera was left, looking at the car
 *   3  chase camera following behind the car
 *   4  looking at the middle of all cars from behind
 * Each one sets G->eye and G->eye_angles.
 */
#include "terep.h"

/* Integer (map unit) part of a 16.16 value, readable and writable. */
static int16_t ipart(const int32_t *v) { return fx_int(*v); }
static void set_ipart(int32_t *v, int16_t i)
{
    *v = (int32_t)(((uint32_t)*v & 0xffff) | (uint32_t)(uint16_t)i << 16);
}

/* Middle 16 bits of a 16.16 unit vector component (1.0 = 0x100). */
static int16_t mid16(int32_t v)
{
    return (int16_t)((uint32_t)v >> 8);
}

static void eye_at_vertex(Car *car)
{
    CarVertex *ref = car_ref_vertex(car);
    G->eye = ref->pos;
    G->cam_axis_x = car_axis_x(car_vertices(car));
    G->cam_axis_y = car_axis_y(car_vertices(car));
}

/* orig 1000:0693 */
static void camera_on_car(Car *car)
{
    eye_at_vertex(car);
    int16_t xx = mid16(G->cam_axis_x.x), xy = mid16(G->cam_axis_x.y), xz = mid16(G->cam_axis_x.z);
    int16_t yx = mid16(G->cam_axis_y.x), yy = mid16(G->cam_axis_y.y), yz = mid16(G->cam_axis_y.z);

    uint16_t yaw = fx_atan2((int16_t)-yx, yy);
    G->eye_angles[2] = (int16_t)yaw;

    /* roll: the side axis against the horizontal, signed by whether that
     * axis points left or right of the heading */
    int16_t side_len = (int16_t)vec2_length(xx, xy);
    uint16_t side_dir = fx_atan2((int16_t)-xx, xy);
    if ((int16_t)(yaw - side_dir) < 0)
        side_len = (int16_t)-side_len;
    G->eye_angles[0] = (int16_t)-fx_atan2(xz, side_len);

    int16_t fwd_len = (int16_t)vec2_length(yx, yy);
    G->eye_angles[1] = (int16_t)-fx_atan2(yz, fwd_len);
}

/* orig 1000:073f */
static void camera_on_car_smooth(Car *car)
{
    eye_at_vertex(car);
    int16_t ex = ipart(&G->eye.x), ey = ipart(&G->eye.y), ez = ipart(&G->eye.z);

    uint16_t yaw = fx_atan2((int16_t)-mid16(G->cam_axis_y.x), mid16(G->cam_axis_y.y));
    G->eye_angles[2] = (int16_t)yaw;

    /* pitch towards the ground 1/32 of the cos/sin unit ahead */
    int16_t ahead = terrain_height((uint16_t)((fx_sin(yaw) >> 5) + ex),
                                   (uint16_t)(((int16_t)-fx_cos(yaw) >> 5) + ey));
    int16_t pitch = (int16_t)fx_atan2((int16_t)(ahead - ez), 0x3ff);
    G->eye_angles[1] = (int16_t)(G->eye_angles[1] + ((int16_t)(pitch - G->eye_angles[1]) >> 2));

    /* roll from the height difference ahead-left / ahead-right */
    uint16_t a = (uint16_t)(yaw + 0x2000), b = (uint16_t)(yaw - 0x2000);
    int16_t h1 = terrain_height((uint16_t)((fx_sin(a) >> 6) + ex), (uint16_t)(((int16_t)-fx_cos(a) >> 6) + ey));
    int16_t h2 = terrain_height((uint16_t)((fx_sin(b) >> 6) + ex), (uint16_t)(((int16_t)-fx_cos(b) >> 6) + ey));
    int16_t roll = (int16_t)fx_atan2((int16_t)(h2 - h1), 0x1ff);
    G->eye_angles[0] = (int16_t)(G->eye_angles[0] + ((int16_t)(roll - G->eye_angles[0]) >> 2));
}

/* Heading and pitch towards a target. */
static int16_t heading_to(int16_t dx, int16_t dy)
{
    return (int16_t)fx_atan2(dx, (int16_t)-dy);
}

/* orig 1000:0828 */
static void camera_fixed(Car *car, ChaseCam *cam)
{
    G->eye.x = cam->pos.x;
    G->eye.y = cam->pos.y;
    int16_t ex = ipart(&G->eye.x), ey = ipart(&G->eye.y);
    int16_t ground = terrain_height((uint16_t)ex, (uint16_t)ey);
    G->eye.z = (int32_t)((uint32_t)(uint16_t)(ground + G->chase_height) << 16);

    CarVertex *ref = car_ref_vertex(car);
    int16_t dx = (int16_t)(ipart(&ref->pos.x) - ex);
    int16_t dy = (int16_t)(ipart(&ref->pos.y) - ey);
    G->eye_angles[2] = heading_to(dx, dy);
    int16_t dist = (int16_t)vec2_length(dx, (int16_t)-dy);
    G->eye_angles[1] = (int16_t)fx_atan2((int16_t)(ipart(&ref->pos.z) - ipart(&G->eye.z)), dist);
    G->eye_angles[0] = 0;
}

/* Put (x, y) chase_distance behind target along heading, and smooth z
 * towards chase_height above the ground (never less than 40 above it). */
static void chase_follow(int16_t tx, int16_t ty, int16_t heading, int32_t *x, int32_t *y, int32_t *z)
{
    int16_t s = fx_sin((uint16_t)heading), c = fx_cos((uint16_t)heading);
    int16_t d2 = (int16_t)(G->chase_distance << 1);
    int16_t bx = (int16_t)(((int32_t)s * d2) >> 16);
    int16_t by = (int16_t)(((int32_t)c * d2) >> 16);
    set_ipart(x, (int16_t)(tx - bx));
    set_ipart(y, (int16_t)(ty + by));

    int16_t ground = terrain_height((uint16_t)ipart(x), (uint16_t)ipart(y));
    uint16_t min_z = (uint16_t)(ground + 0x28);
    int16_t cz = ipart(z);
    cz = (int16_t)(cz + ((int16_t)(ground + G->chase_height - cz) >> 3));
    if (min_z > (uint16_t)cz)
        cz = (int16_t)min_z;
    set_ipart(z, cz);
}

static int16_t smooth_pitch(int16_t current, int16_t dz)
{
    int16_t target = (int16_t)fx_atan2(dz, G->chase_distance);
    return (int16_t)(current + ((int16_t)(target - current) >> 2));
}

/* orig 1000:0893 */
static void camera_chase(Car *car, ChaseCam *cam)
{
    CarVertex *ref = car_ref_vertex(car);
    int16_t rx = ipart(&ref->pos.x), ry = ipart(&ref->pos.y);
    cam->yaw = heading_to((int16_t)(rx - ipart(&cam->pos.x)), (int16_t)(ry - ipart(&cam->pos.y)));
    chase_follow(rx, ry, cam->yaw, &cam->pos.x, &cam->pos.y, &cam->pos.z);
    cam->pitch = smooth_pitch(cam->pitch, (int16_t)(ipart(&ref->pos.z) - ipart(&cam->pos.z)));
    cam->roll = 0;

    G->eye_angles[2] = cam->yaw;
    G->eye_angles[1] = cam->pitch;
    G->eye_angles[0] = cam->roll;
    G->eye = cam->pos;
}

/* orig 1000:0948 */
static void camera_all_cars(void)
{
    uint32_t sx = 0, sy = 0, sz = 0;
    uint32_t n = (uint32_t)(uint16_t)G->num_cars;
    for (int i = 0; i < G->num_cars; i++) {
        CarVertex *ref = car_ref_vertex(car_get(i));
        sx += (uint16_t)ipart(&ref->pos.x);
        sy += (uint16_t)ipart(&ref->pos.y);
        sz += (uint16_t)ipart(&ref->pos.z);
    }
    G->cars_center[0] = (int16_t)(sx / n);
    G->cars_center[1] = (int16_t)(sy / n);
    G->cars_center[2] = (int16_t)((int32_t)sz / (int32_t)n);

    int16_t cx = G->cars_center[0], cy = G->cars_center[1];
    G->eye_angles[2] = heading_to((int16_t)(cx - ipart(&G->eye.x)), (int16_t)(cy - ipart(&G->eye.y)));
    chase_follow(cx, cy, G->eye_angles[2], &G->eye.x, &G->eye.y, &G->eye.z);
    G->eye_angles[1] = smooth_pitch(G->eye_angles[1], (int16_t)(G->cars_center[2] - ipart(&G->eye.z)));
    G->eye_angles[0] = 0;
}

/* Place the eye for one view.  orig f_cam_select */
void camera_update(int view)
{
    Car *car = car_get(G->player_car[view]);
    ChaseCam *cam = &G->chase_cam[view];
    switch (G->cam_mode[view] & 7) {
    case 0: camera_on_car(car); break;
    case 1: camera_on_car_smooth(car); break;
    case 2: camera_fixed(car, cam); break;
    case 3: camera_chase(car, cam); break;
    case 4: camera_all_cars(); break;
    default: break;                     /* the original crashes (UD2) */
    }
}
