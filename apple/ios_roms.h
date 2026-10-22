#pragma once
#include "../src/host/romset.h"

// Main thread only. Keeps the Files importer responsive until a complete set is available.
bool ios_load_roms(romset::result &out);
void ios_pump_loading();
void ios_finish_loading();
[[noreturn]] void ios_fatal_error(const std::string &message);
