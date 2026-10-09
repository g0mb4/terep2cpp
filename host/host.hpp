// What the game needs from a host, plus helpers shared by the hosts.
#pragma once
#include "../src/terep.hpp"

// Load the initial data segment image (data/data.bin) and allocate the
// segment pool.  Returns false (after printing why) on failure.
bool host_load_dataseg(const char *path);

// Run one display frame of the original timing: two physics ticks
// (120 Hz) per rendered frame (60 Hz).  Returns true when Esc was handled.
bool host_step();

// RGB of palette entry i (the PCX palette, 8 bits per channel).
inline const uint8_t *host_palette(uint8_t i) { return &G->palette[i * 3]; }
