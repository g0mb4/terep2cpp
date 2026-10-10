/*
 * The game writes OPL2 registers (as the original did to ports 388h/389h);
 * here each write goes to an emulated OPL3 chip (which starts in OPL2 mode),
 * and the SDL audio callback asks the chip for samples.  Writes come from
 * the game's thread, so they take the audio device's lock; they are heard
 * from the next buffer on (about 10 ms).
 */
#include <cstdio>
#include <SDL.h>
#include "host.hpp"
#include "opl_sound.hpp"
extern "C" {
#include "opl3.h"
}

static opl3_chip chip;
static SDL_AudioDeviceID device;

static void fill(void *, Uint8 *stream, int len)
{
    OPL3_GenerateStream(&chip, (int16_t *)stream, (uint32_t)(len / 4));    // stereo, 16 bits
}

static void write_reg(uint8_t reg, uint8_t value)
{
    SDL_LockAudioDevice(device);
    OPL3_WriteReg(&chip, reg, value);
    SDL_UnlockAudioDevice(device);
}

bool opl_sound_start()
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "no sound: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want = {}, have;
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = fill;
    device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!device) {
        fprintf(stderr, "no sound: %s\n", SDL_GetError());
        return false;
    }
    OPL3_Reset(&chip, (uint32_t)have.freq);
    opl_write_hook = write_reg;
    SDL_PauseAudioDevice(device, 0);
    return true;
}

void opl_sound_stop()
{
    if (!device)
        return;
    opl_write_hook = nullptr;
    SDL_CloseAudioDevice(device);
    device = 0;
}
