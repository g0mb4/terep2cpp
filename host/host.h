/* What the game needs from a host, plus helpers shared by the hosts. */
#ifndef TEREP_HOST_H
#define TEREP_HOST_H
#include "../src/terep.h"

/* Load the initial data segment image (memdumps/data.bin) and allocate
 * the segment pool.  Returns 0 on success. */
int  host_load_dataseg(const char *path);

/* Run one display frame of the original timing: two physics ticks
 * (120 Hz) per rendered frame (60 Hz).  Returns 1 when Esc was handled. */
int  host_step(void);

/* RGB of palette entry i (the PCX palette, 8 bits per channel). */
static inline const uint8_t *host_palette(uint8_t i) { return &G->palette[i * 3]; }

#endif
