// Track chooser shown by the SDL host when no track directory is given: a
// directory browser, a preview of the selected track, the render scale and
// the optional bug fixes (Dear ImGui).
#pragma once
#include <string>
#include <SDL.h>

// Runs until the player picks a track (true, *track set) or closes the
// window (false).  Starts browsing in the current directory.  render_scale
// and fixes (see terep.hpp) are edited in place, so values given on the
// command line show up as the initial ones.
bool launcher_run(SDL_Window *win, SDL_Renderer *ren, std::string *track);
