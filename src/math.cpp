/*
 * Table-driven trigonometry, vector length and the 3x3 view rotation.
 */
#include "terep.hpp"

/* orig 1000:2aad */
int16_t fx_sin(uint16_t angle)
{
    uint16_t t = angle & 0x7fff;
    if (t & 0x4000)
        t = 0x8000 - t;                 /* mirror the second quadrant */
    int16_t v = G->sin_table[t >> 4];
    return (angle & 0x8000) ? (int16_t)-v : v;
}

/* orig 1000:2ad8 */
int16_t fx_cos(uint16_t angle)
{
    uint16_t t = angle & 0x7fff;
    if (t & 0x4000)
        t -= 0x4000;
    else
        t = 0x4000 - t;
    int16_t v = G->sin_table[t >> 4];
    uint16_t quadrant = angle >> 14;
    return (quadrant == 1 || quadrant == 2) ? (int16_t)-v : v;
}

/* atan(num/den) for 0 <= num < den, orig 1000:2b3b */
static uint16_t atan_ratio(uint16_t num, uint16_t den)
{
    uint16_t q = (uint16_t)(((uint32_t)num << 16) / den);
    return G->atan_table[q >> 8];
}

/* both arguments >= 0, orig 1000:2b2d */
static uint16_t atan_first_quadrant(int16_t y, int16_t x)
{
    if (y > x)
        return (uint16_t)(0x4000 - atan_ratio(x, y));
    if (y < x)
        return atan_ratio(y, x);
    return 0x2000;
}

/* y >= 0, orig 1000:2b1f */
static uint16_t atan_upper_half(int16_t y, int16_t x)
{
    if (x < 0)
        return (uint16_t)(0x8000 - atan_first_quadrant(y, (int16_t)~x));
    if (x == 0)
        return 0x4000;
    return atan_first_quadrant(y, x);
}

/* Angle of the vector (x, y), orig 1000:2b08.  Negative coordinates are
 * folded with NOT rather than NEG, as in the original. */
uint16_t fx_atan2(int16_t y, int16_t x)
{
    if (y < 0)
        return (uint16_t)-atan_upper_half((int16_t)~y, x);
    if (y == 0)
        return x < 0 ? 0x8000 : 0;
    return atan_upper_half(y, x);
}

/* Length of (b, a) computed as a/sin or b/cos of its angle, whichever is
 * better conditioned.  orig 1000:26dd */
int32_t vec2_length(int16_t a, int16_t b)
{
    uint16_t angle = fx_atan2(a, b);
    uint8_t octant_bits = (angle >> 8) & 0x60;
    if (octant_bits == 0x20 || octant_bits == 0x40)
        return ((int32_t)a << 15) / fx_sin(angle);
    return ((int32_t)b << 15) / fx_cos(angle);
}

/* orig 1000:271d */
int32_t vec3_length(int16_t a, int16_t b, int16_t c)
{
    return vec2_length((int16_t)vec2_length(a, b), c);
}

/* Divide a 16.16 vector by its length (+1): the result has 1.0 == 0x10000.
 * orig 1000:2726 */
Vec3 vec3_normalize(Vec3 v)
{
    int32_t len = vec3_length(fx_int(v.x), fx_int(v.y), fx_int(v.z)) + 1;
    return { v.x / len, v.y / len, v.z / len };
}

/* Perspective divide, orig 1000:2760.  depth is the camera-space y. */
void project(int16_t x, int16_t depth, int16_t z, int16_t *sx, int16_t *sz)
{
    *sx = (int16_t)(((int32_t)x << 8) / depth);
    *sz = (int16_t)(((int32_t)z << 8) / depth);
}

/* Row vector times matrix, every product rounded to Q15 on its own.
 * orig 1000:277e */
void rotate(const int16_t m[9], int16_t x, int16_t y, int16_t z, int16_t out[3])
{
    out[0] = (int16_t)(mul_q15(x, m[0]) + mul_q15(y, m[3]) + mul_q15(z, m[6]));
    out[1] = (int16_t)(mul_q15(x, m[1]) + mul_q15(y, m[4]) + mul_q15(z, m[7]));
    out[2] = (int16_t)(mul_q15(x, m[2]) + mul_q15(y, m[5]) + mul_q15(z, m[8]));
}

/* Rotation matrix from (roll, pitch, yaw).  orig 1000:2989 */
void build_matrix(const int16_t angles[3], int16_t m[9])
{
    int16_t s0 = fx_sin(angles[0]), c0 = fx_cos(angles[0]);
    int16_t s1 = fx_sin(angles[1]), c1 = fx_cos(angles[1]);
    int16_t s2 = (int16_t)-fx_sin(angles[2]), c2 = fx_cos(angles[2]);

    int16_t s0s1 = mul_q15(s0, s1);
    int16_t c0c2 = mul_q15(c0, c2);
    int16_t s2c0 = mul_q15(s2, c0);

    m[0] = (int16_t)(c0c2 + neg_sat16(mul_q15(s0s1, s2)));
    m[1] = neg_sat16((int16_t)(mul_q15(s0s1, c2) + s2c0));
    m[2] = neg_sat16(mul_q15(s0, c1));
    m[3] = mul_q15(s2, c1);
    m[4] = mul_q15(c2, c1);
    m[5] = neg_sat16(s1);
    m[6] = (int16_t)(mul_q15(s2c0, s1) + mul_q15(c2, s0));
    m[7] = (int16_t)(mul_q15(c0c2, s1) + neg_sat16(mul_q15(s0, s2)));
    m[8] = mul_q15(c0, c1);
}

/* orig 1000:27f1 */
void transpose(int16_t m[9])
{
    int16_t t;
    t = m[1]; m[1] = m[3]; m[3] = t;
    t = m[2]; m[2] = m[6]; m[6] = t;
    t = m[5]; m[5] = m[7]; m[7] = t;
}
