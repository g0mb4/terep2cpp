// Map chooser shown by the SDL host when no map directory is given: a
// directory browser, a preview of the selected map, the render scale and
// the extra options, such as bug fixes (Dear ImGui).
#pragma once
#include <string>
#include <SDL.h>

// What the SDL host does around the game (not options of the game itself).
struct HostOptions {
    bool no_sound = false;          // "Disable sound": no audio device, no OPL emulator
    bool minimap = false;           // the map and the cars in the bottom right corner
};

// Runs until the player picks a map (true, *map set) or closes the
// window (false).  Starts browsing where *map (if set, as from the last
// call) is, with it selected, or else in the current directory.  render_scale
// and extra_options (see terep.hpp) are edited in place, so values given on the
// command line show up as the initial ones; so do the host's options.
bool launcher_run(std::string *map, HostOptions *host);     // draws in the gui (gui.hpp)
