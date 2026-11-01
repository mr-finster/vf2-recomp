// vf2random: Cheats' random fighters headless (src/machine/random_fighter.cpp). Boots on free play, sets each
// player's fighter and Random opponents, plays an input script (src/host/input_script.h) and writes its shots
// (each with its frame's fighters: their numbers and the names on their energy bars). --names: the seed book's
// size and its first names. --online: a board that played with random fighters on, and one that never had them,
// each started as an online match (vf2net::Adapter::begin: all off), then given the same random inputs for --frames
// frames (default 3000): their states the same at every 60th.
// --ko P@F: from frame F on, player P (1 or 2) knocked out in every round (as vf2run's: a 1P run through its
// matches); each match's fight is shot too, 150 frames in (matchN.png), with its fighters. --every N: a shot every
// N frames too (fNNNNNN.png). --record A-B[,C-D...]: a shot every frame from A to B (fNNNNNN.png, not listed).
//   vf2random <vf2.zip> <script> [--out dir] [--p1 SETTING] [--p2 SETTING] [--opponents] [--scale N] [--ko P@F]
//   vf2random <vf2.zip> --names
//   vf2random <vf2.zip> --book            (every name, what it's made of: head, body, arms, legs, moves, voice: docs/SEEDS.md)
//   vf2random <vf2.zip> --recipes         (the book in the recipe spec's table: what VF3 Recomp carries, docs/random_book.txt)
//   vf2random <vf2.zip> --owners          (each name's recipe, its move groups' owners and its part groups', as VF3 Recomp lists them)
//   vf2random <vf2.zip> --online [--frames N]
// SETTING: "" (Normal), "*" (Random) or a name (a Seed). The pictures are ROM-derived: never commit them.
#include "../src/machine/model2a.h"
#include "../src/host/png.h"
#include "../src/host/input_script.h"
#include "../src/host/vf2_netplay.h"
#include "random_players.h"
#include <algorithm>
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
	bool opponents = false, list_names = false, online = false, book = false, recipes = false, owners = false;
	long frames = 3000;
	int ko = 0, every = 0;
	std::vector<std::pair<uint64_t, uint64_t>> record;
	uint64_t ko_from = 0;
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
		else if (a == "--online") online = true;
		else if (a == "--book") book = true;
		else if (a == "--recipes") recipes = true;
		else if (a == "--owners") owners = true;
		else if (a == "--frames") frames = atol(next().c_str());
		else if (a == "--every") every = atoi(next().c_str());
		else if (a == "--record") {
			const std::string v = next();
			for (const char *q = v.c_str(); *q;) {
				char *e;
				const uint64_t from = strtoull(q, &e, 10);
				if (e == q) { fprintf(stderr, "vf2random: --record %s?\n", v.c_str()); return 2; }
				const uint64_t to = *e == '-' ? strtoull(e + 1, &e, 10) : from;
				record.push_back({ from, to });
				if (*e && *e != ',') { fprintf(stderr, "vf2random: --record %s?\n", v.c_str()); return 2; }
				q = *e ? e + 1 : e;
			}
		}
		else if (a == "--ko") { const std::string v = next(); ko = atoi(v.c_str()); const size_t at = v.find('@'); ko_from = at == std::string::npos ? 0 : strtoull(v.c_str() + at + 1, nullptr, 10); }
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
	if (recipes || owners) {
		if (recipes) printf("# name leader second third head voice\n");
		const std::vector<std::string> lines = m.random_fighter_book(owners);
		for (const std::string &l : lines) printf("%s\n", l.c_str());
		return lines.empty() ? 1 : 0;
	}
	if (book) {
		printf("| Seed | Head | Body | Arms | Legs | Moves (stance first; move groups dealt) | Voice |\n|---|---|---|---|---|---|---|\n");
		for (const auto &[name, seed] : m.random_fighter_names()) {
			std::string row = m.random_fighter_makeup(name);
			for (char &c : row) if (c == '|') c = '\x01';
			std::string cells;
			for (char c : row) cells += c == '\x01' ? std::string(" | ") : std::string(1, c);
			printf("| %s | %s |\n", name.c_str(), cells.c_str());
		}
		return 0;
	}
	if (online) {
		// (the second board: the same ROMs, never a random fighter)
		romset::result roms2 = romset::load(argv[1]);
		auto pb = std::make_unique<model2a>(std::move(roms2));
		model2a &b = *pb;
		m.set_draw_3d(false); b.set_draw_3d(false);
		m.set_random_player(0, "*"); m.set_random_player(1, "AKILAUPAI"); m.set_random_opponents(true);
		m.reset();
		for (int i = 0; i < 120 && !m.force_free_play() && !m.free_play(); i++) m.run_frame();
		m.apply_random_fighters();
		m.reset();
		players::Random warm(7);
		for (int f = 0; f < 3000; f++) {   // (a game on the first: the random fighters made, picked and fought)
			const players::Ports p = warm.next(uint64_t(f));
			m.m_in0 = uint8_t(~p.in0); m.m_in1 = uint8_t(~p.in1); m.m_in2 = uint8_t(~p.in2);
			m.run_frame();
		}
		printf("before: random fighters %s, names [%s] [%s]\n", m.random_fighters_on() ? "on" : "off", m.random_fighter_name(0).c_str(), m.random_fighter_name(1).c_str());
		vf2net::Adapter ad(m), bd(b);
		const std::vector<uint8_t> blob = vf2net::start_blob(b);
		if (!ad.begin(blob) || !bd.begin(blob)) { fprintf(stderr, "vf2random: a match's start failed\n"); return 1; }
		players::Random rnd(1);
		std::vector<uint8_t> sa, sb;
		int checked = 0;
		for (long f = 0; f < frames; f++) {
			const players::Ports p = rnd.next(uint64_t(f));
			const uint16_t in[2] = { vf2net::pack(uint8_t(~p.in0), uint8_t(~p.in1), 0), vf2net::pack(uint8_t(~p.in0), uint8_t(~p.in2), 1) };
			ad.advance(in, false);
			bd.advance(in, false);
			if (f % 60 == 59) {
				ad.save(sa); bd.save(sb);
				if (ad.hash(sa) != bd.hash(sb)) { printf("online: the states differ at frame %ld\n", f); return 1; }
				checked++;
			}
		}
		printf("online: the same, %d states checked over %ld frames (random fighters %s)\n", checked, frames, m.random_fighters_on() ? "on" : "off");
		return 0;
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
	auto shoot = [&](const std::string &name, uint64_t f, bool quiet = false) {
		const bitmap_rgb32 &b = m.render(true);
		const std::string path = out + "/" + name + ".png";
		const bool hidden = m.random_select_hidden();   // (as the window shows it: black, the select hidden)
		if (hidden) { std::vector<uint32_t> black(size_t(b.width()) * b.height(), 0xff000000); write_png(path.c_str(), black.data(), b.width(), b.height()); }
		else write_png(path.c_str(), b.data(), b.width(), b.height());
		if (quiet) return;
		u32 blk[2] = { m.peek32(0x500804), m.peek32(0x500808) };
		printf("%s (frame %llu, mode %u%s): fighters %u %u, names [%s] [%s]\n", path.c_str(), (unsigned long long)f, m.game_mode(), hidden ? ", select hidden" : "",
			blk[0] ? m.read_byte(blk[0] + 0x1b0) : 0xffu, blk[1] ? m.read_byte(blk[1] + 0x1b0) : 0xffu,
			m.random_fighter_name(0).c_str(), m.random_fighter_name(1).c_str());
		fflush(stdout);
	};
	int matches = 0, was_mode = -1, was_fighters = -1;
	uint64_t match_shot = UINT64_MAX;
	for (uint64_t f = 0; f <= script.end; f++) {
		m.run_frame();
		auto [first, last] = script.shots.equal_range(f);
		for (auto it = first; it != last; ++it) shoot(it->second, f);
		// (each match's first fight: shot 150 frames in; a match after a VS screen, or the next of a 1P run, its
		// opponent's number new)
		const u32 b2 = m.peek32(0x500808), b1 = m.peek32(0x500804);
		const int fighters = (b1 ? m.read_byte(b1 + 0x1b0) : 0xff) << 8 | (b2 ? m.read_byte(b2 + 0x1b0) : 0xff);
		if (m.game_mode() == 9 && (was_mode == 8 || (fighters != was_fighters && match_shot == UINT64_MAX))) match_shot = f + 150;
		was_mode = m.game_mode();
		was_fighters = fighters;
		if (f == match_shot) { shoot("match" + std::to_string(++matches), f); match_shot = UINT64_MAX; }
		const bool in_record = std::any_of(record.begin(), record.end(), [&](const auto &r) { return f >= r.first && f <= r.second; });
		if ((every > 0 && f % uint64_t(every) == 0) || in_record) { char n[32]; snprintf(n, sizeof n, "f%06llu", (unsigned long long)f); shoot(n, f, in_record); }
		if (ko && f >= ko_from && m.workram()[0x2b] == 9 && m.workram()[0x31] == 9) {   // (vf2run's --ko)
			const u32 block = ko == 1 ? 0x510980 : 0x512980;
			if (int16_t(m.peek32(block + 0x1ac)) > 0 && !(m.peek32(block) & 0x20)) {
				m.write_word(block + 0x1ac, 0);
				m.write_dword(block, m.peek32(block) | 0x20);
			}
		}
		script.apply(f, ports);
		m.m_in0 = ports[input_script::IN0]; m.m_in1 = ports[input_script::IN1];
		m.m_in2 = ports[input_script::IN2]; m.m_sw = ports[input_script::SW];
	}
	return 0;
}
