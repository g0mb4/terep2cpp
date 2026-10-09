/*
 * Fixed-point helpers.
 *
 * The original is 16-bit real-mode code that leans on a handful of x86
 * multiply/divide idioms.  Each helper below reproduces one of them
 * bit-for-bit, so the rest of the code can read like ordinary C.
 *
 * Conventions used throughout the code:
 *   - angles are 16-bit "binary degrees": 0x10000 == 360 deg, 0x4000 == 90 deg
 *   - sin/cos values and rotation matrices are Q15 (0x7fff ~= 1.0)
 *   - world positions are 16.16 (int32), the integer part is a map unit;
 *     one terrain cell is 256 units, so the high byte of the integer part
 *     is the cell coordinate
 *
 * Everything is compiled with -fwrapv: the original relies on two's
 * complement wrap-around in many places.
 */
#ifndef TEREP_FIXED_H
#define TEREP_FIXED_H

#include <stdint.h>

/* IMUL r16 followed by "SHL AX,1 / RCL DX,1" and keeping DX:
 * the Q15 product a*b, truncated to 16 bits. */
static inline int16_t mul_q15(int16_t a, int16_t b)
{
    return (int16_t)(((int32_t)a * b) >> 15);
}

/* IMUL r16 keeping only DX (high word of the 32-bit product). */
static inline int16_t mul_hi16(int16_t a, int16_t b)
{
    return (int16_t)(((int32_t)a * b) >> 16);
}

/* IMUL r32 keeping only EDX (high dword of the 64-bit product). */
static inline int32_t mul_hi32(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 32);
}

/* Q15 negation that saturates instead of overflowing (the original
 * checks for 0x8000 and bumps it to 0x8001 before NEG). */
static inline int16_t neg_sat16(int16_t v)
{
    return (int16_t)-(v == INT16_MIN ? INT16_MIN + 1 : v);
}

/* Parity flag of the low byte, as set by x86 TEST/AND (true = even
 * number of set bits).  Only used where the original branches on PF. */
static inline int parity_even(uint8_t v)
{
    return !__builtin_parity(v);
}

static inline int16_t abs16(int16_t v)
{
    return v < 0 ? (int16_t)-v : v;
}

#endif
