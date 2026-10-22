// The player's own MAME `vf2` set (Version 2.1), read straight from the zip: every ROM checked
// by CRC and laid out in the regions MAME's driver (sega/model2.cpp, ROM_START( vf2 )) builds.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace romset {

enum region_id {
	MAINCPU,        // i960 program, 0x200000
	MAIN_DATA,      // i960 data ROMs, 0x2400000 (0x02000000 and 0x06000000 in the memory map)
	COPRO_DATA,     // empty on VF2 (MAME: ROMREGION_ERASE00), 0x800000
	POLYGONS,       // model data for the geometrizer, 0x2000000
	TEXTURES,       // 0x1000000, unused bytes 0xff
	AUDIOCPU,       // 68000 sound program, 0x80000
	SAMPLES,        // SCSP samples, 0x800000
	COPRO_TABLES,   // TGP sin/cos, atan, 1/x, 1/sqrt tables (opr-14742a/14743a), 0x40000
	OTHER_DATA,     // 1/x and 1/sqrt(x) tables (opr-14744..14747), 0x80000
	VIDEO_UNK,      // Model 2A video board tables (mpr-16310..16312), 0x200000
	N_REGIONS
};

const char *region_name(int r);

struct rom_status
{
	std::string name;
	uint32_t crc;
	int region;
	uint32_t offset;
	enum { OK, MISSING, WRONG, DAMAGED } state;
};

struct result
{
	bool ok = false;
	std::string error;                  // what's missing or wrong, for the player
	std::vector<uint8_t> regions[N_REGIONS];
	std::vector<rom_status> roms;       // the checklist, one line per ROM
};

result load(const std::string &zip);

// Copy to a staging file, validate that copy, then atomically install it. A failed
// import leaves an existing destination untouched. No ZIP entries are extracted.
result import_file(const std::string &source, const std::string &destination);

} // namespace romset
