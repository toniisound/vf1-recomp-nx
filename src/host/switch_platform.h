// The Nintendo Switch (libnx, a homebrew .nro): what the desktop gets from its system and the Switch
// doesn't have, kept out of main.cpp.
#pragma once
#ifdef __SWITCH__
#include <string>

// The program's folder on the SD card: settings, saves, log.txt and roms/ (with vf.zip) go there.
#define VF_SWITCH_DIR "sdmc:/switch/vf1-recomp-nx"

// At the start: the RomFS (the menu's files), the data folder made.
void switch_platform_init();
// A message on a text screen until a button is pressed (there are no message boxes on the Switch).
// Only before SDL's video starts (the screen is then SDL's): false if it couldn't be shown.
bool switch_message(const char *title, const std::string &msg);
// CPU boost: the CPU at 1785 MHz (the console's own boost mode, as games use while loading), the
// GPU at its lowest: the 3D is drawn by the CPU here, the GPU only puts the picture together.
// Only as an application (title takeover); in applet mode it does nothing.
void switch_cpu_boost(bool on);
// Started as a game (title takeover: all of the memory), not from the Album
bool switch_full_memory();
#endif
