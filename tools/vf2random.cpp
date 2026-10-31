// vf2random: Cheats' random fighters headless (src/machine/random_fighter.cpp). Boots on free play, sets each
// player's fighter and Random opponents, plays an input script (src/host/input_script.h) and writes its shots
// (each with its frame's fighters: their numbers and the names on their energy bars). --names: the seed book's
// size and its first names; --check SEED,...: each seed's name, made twice, the same.
//   vf2random <vf2.zip> <script> [--out dir] [--p1 SETTING] [--p2 SETTING] [--opponents] [--scale N]
//   vf2random <vf2.zip> --names
// SETTING: "" (Normal), "*" (Random) or a name (a Seed). The pictures are ROM-derived: never commit them.
#include "../src/machine/model2a.h"
#include "../src/host/png.h"
#include "../src/host/input_script.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: vf2random <vf2.zip> <script> [--out dir] [--p1 X] [--p2 X] [--opponents] [--scale N]\n"
		                "       vf2random <vf2.zip> --names\n");
		return 2;
	}
	std::string script_path, out = "build/random", setting[2];
	bool opponents = false, list_names = false;
	int scale = 1;
	for (int i = 2; i < argc; i++) {
		const std::string a = argv[i];
		auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (a == "--out") out = next();
		else if (a == "--p1") setting[0] = next();
		else if (a == "--p2") setting[1] = next();
		else if (a == "--opponents") opponents = true;
		else if (a == "--scale") scale = atoi(next().c_str());
		else if (a == "--names") list_names = true;
		else if (a[0] != '-' && script_path.empty()) script_path = a;
		else { fprintf(stderr, "vf2random: %s?\n", a.c_str()); return 2; }
	}
	romset::result roms = romset::load(argv[1]);
	if (!roms.ok) { fprintf(stderr, "vf2random: %s\n", roms.error.c_str()); return 1; }
	auto pm = std::make_unique<model2a>(std::move(roms));
	model2a &m = *pm;
	if (list_names) {
		const auto &names = m.random_fighter_names();
		printf("%zu names\n", names.size());
		int n = 0;
		for (const auto &[name, seed] : names)
			if (n++ < 40) printf("%-10s seed %u\n", name.c_str(), seed);
		return names.empty() ? 1 : 0;
	}
	input_script script;
	std::string err;
	if (!script.load(script_path, err)) { fprintf(stderr, "vf2random: %s\n", err.c_str()); return 1; }
	m.set_render_scale(std::clamp(scale, 1, 4));
	for (int p = 0; p < 2; p++)
		if (!m.set_random_player(p, setting[p])) { fprintf(stderr, "vf2random: no fighter has the seed %s\n", setting[p].c_str()); return 1; }
	m.set_random_opponents(opponents);
	if (!m.apply_random_fighters()) { fprintf(stderr, "vf2random: not the program known\n"); return 1; }
	// free play: the first boot writes the settings, the next reads them (as vf2run --free-play)
	m.reset();
	for (int i = 0; i < 120 && !m.force_free_play() && !m.free_play(); i++) m.run_frame();
	m.apply_random_fighters();
	m.reset();
	std::filesystem::create_directories(out);
	uint8_t ports[input_script::N_PORTS];
	for (uint64_t f = 0; f <= script.end; f++) {
		m.run_frame();
		auto [first, last] = script.shots.equal_range(f);
		for (auto it = first; it != last; ++it) {
			const bitmap_rgb32 &b = m.render(true);
			const std::string path = out + "/" + it->second + ".png";
			write_png(path.c_str(), b.data(), b.width(), b.height());
			u32 blk[2] = { m.peek32(0x500804), m.peek32(0x500808) };
			printf("%s (frame %llu, mode %u): fighters %u %u, names [%s] [%s]\n", path.c_str(), (unsigned long long)f, m.game_mode(),
				blk[0] ? m.read_byte(blk[0] + 0x1b0) : 0xffu, blk[1] ? m.read_byte(blk[1] + 0x1b0) : 0xffu,
				m.random_fighter_name(0).c_str(), m.random_fighter_name(1).c_str());
			fflush(stdout);
		}
		script.apply(f, ports);
		m.m_in0 = ports[input_script::IN0]; m.m_in1 = ports[input_script::IN1];
		m.m_in2 = ports[input_script::IN2]; m.m_sw = ports[input_script::SW];
	}
	return 0;
}
