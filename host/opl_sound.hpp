// The game's AdLib register writes (opl_write_hook) played through the
// Nuked-OPL3 emulator (host/Nuked-OPL3 submodule) and SDL audio.
#pragma once

// Opens the audio device and hooks the emulator to the game.  False (with
// a message) if there is no audio; the game then runs silent.
bool opl_sound_start();
void opl_sound_stop();
