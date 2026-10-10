// Track chooser shown by the SDL host when no track directory is given: a
// directory browser, a preview of the selected track, the render scale and
// the extra options, such as bug fixes (Dear ImGui).
#pragma once
#include <string>
#include <SDL.h>

// Runs until the player picks a track (true, *track set) or closes the
// window (false).  Starts browsing where *track (if set, as from the last
// call) is, with it selected, or else in the current directory.  render_scale
// and extra_options (see terep.hpp) are edited in place, so values given on the
// command line show up as the initial ones; so does *no_sound (the host's
// "Disable sound").
bool launcher_run(SDL_Window *win, SDL_Renderer *ren, std::string *track, bool *no_sound);
