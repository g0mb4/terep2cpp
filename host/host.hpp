// What the game needs from a host, plus helpers shared by the hosts.
#pragma once
#include "../src/terep.hpp"

// Allocate the data segment and fill it with its initial contents.
// Returns false (after printing why) on failure.
bool host_init_dataseg();

// Free the data segment and the segments, to start a new game (a new
// host_init_dataseg and game_init) in the same process.
void host_free_game();

// Run one display frame of the original timing: two physics ticks
// (120 Hz) per rendered frame (60 Hz).  Returns true when Esc was handled.
bool host_step();

// RGB of palette entry i (the PCX palette, 8 bits per channel).
inline const uint8_t *host_palette(uint8_t i) { return &G->palette[i * 3]; }
