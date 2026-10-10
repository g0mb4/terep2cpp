// The cars as the original keeps them in its data segment, for comparing
// with it (the game itself only keeps them as Car structures).
#pragma once
#include <cstdint>

// Read again from the car files what the game does not keep (the model's
// bytes, an unknown header word).  After game_init, in the track directory.
bool car_files_read();

// Write the cars into a copy of the data segment where the original has
// them (G->car_ofs), in the file format.
void cars_to_dataseg(uint8_t *ds);
