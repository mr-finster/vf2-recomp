// vf2detect: finds the game's round counters and who's playing (the netplay Score's places, vf2net::Score), the way
// VF3's vf3detect does: a match's start (vf2net::Adapter::begin, free play) and two random players, and each frame
// every byte of work RAM that changed. A round counter only ever goes up by one, or back to 0, and stays small: those
// are printed with when they changed. --shots DIR writes a picture every --every frames (to see what was on screen).
// --watch ADDR,... prints those bytes' changes with their frames.
//   vf2detect <vf2.zip> [--random SEED] [--frames N] [--shots DIR --every N] [--watch A,B,...]
// Addresses are work RAM offsets (0x500000 + offset as the i960 sees it).
#include "../src/machine/model2a.h"
#include "../src/host/vf2_netplay.h"
#include "../src/host/png.h"
#include "random_players.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>



namespace {
struct Counter { uint16_t ups = 0, resets = 0, other = 0; uint8_t top = 0; };
uint16_t word(const players::Ports &p, int player) { return vf2net::pack(uint8_t(~p.in0), uint8_t(~(player ? p.in2 : p.in1)), player); }
} // namespace

int main(int argc, char **argv)
{
	std::string rom, shots;
	long frames = 20000, every = 120;
	uint32_t seed = 1;
	std::vector<uint32_t> watch;
	bool solo = false, solo2 = false;
	long p2_once = -1;   // (player 2 presses Start only at this frame: a challenger who joins once)   // (solo2: player 1 never presses Start)   // (player 2 never presses Start: player 1 against the computer)
	std::vector<long> dumps;   // (work RAM after these frames, to dump_F.bin)
	std::vector<long> tile_dumps;   // (--tiles F: the tile maps' rows 4-9 and the layer registers then, printed)
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--frames" && i + 1 < argc) frames = strtol(argv[++i], nullptr, 10);
		else if (a == "--random" && i + 1 < argc) seed = uint32_t(strtoul(argv[++i], nullptr, 10));
		else if (a == "--shots" && i + 1 < argc) shots = argv[++i];
		else if (a == "--solo") solo = true;
		else if (a == "--solo2") solo2 = true;
		else if (a == "--p2-once" && i + 1 < argc) p2_once = strtol(argv[++i], nullptr, 10);
		else if (a == "--dump" && i + 1 < argc) dumps.push_back(strtol(argv[++i], nullptr, 10));
		else if (a == "--tiles" && i + 1 < argc) tile_dumps.push_back(strtol(argv[++i], nullptr, 10));
		else if (a == "--every" && i + 1 < argc) every = strtol(argv[++i], nullptr, 10);
		else if (a == "--watch" && i + 1 < argc) {
			std::string v = argv[++i];
			for (size_t at = 0; at < v.size();) {
				watch.push_back(uint32_t(strtoul(v.c_str() + at, nullptr, 16)));
				const size_t c = v.find(',', at);
				at = c == std::string::npos ? v.size() : c + 1;
			}
		} else if (a[0] != '-' && rom.empty()) rom = a;
		else { fprintf(stderr, "usage: vf2detect <vf2.zip> [--random SEED] [--frames N] [--shots DIR --every N] [--watch A,B]\n"); return 2; }
	}
	romset::result roms = romset::load(rom);
	if (!roms.ok) { fprintf(stderr, "vf2detect: %s\n", roms.error.c_str()); return 1; }
	auto pm = std::make_unique<model2a>(std::move(roms));
	model2a &m = *pm;
	if (shots.empty()) m.set_draw_3d(false);
	vf2net::Adapter ad(m);
	if (!ad.begin(vf2net::start_blob(m))) { fprintf(stderr, "vf2detect: the match's start failed\n"); return 1; }

	const std::vector<uint8_t> &ram = m.workram();
	const size_t n = ram.size();
	std::vector<uint8_t> prev = ram;
	std::vector<Counter> st(n);
	struct Change { long frame; uint32_t addr; uint8_t from, to; };
	std::vector<Change> log;
	std::vector<int> last_watch(watch.size(), -1);
	players::Random rnd(seed);
	for (long f = 0; f < frames; f++) {
		players::Ports p = rnd.next(uint64_t(f));
		if (solo) p.in0 &= uint8_t(~players::P2_IN0);
		if (solo2) p.in0 &= uint8_t(players::P2_IN0);
		if (p2_once >= 0) { p.in0 &= uint8_t(~players::P2_IN0); if (f >= p2_once && f < p2_once + 6) p.in0 |= 0x20; }
		const uint16_t in[2] = { word(p, 0), word(p, 1) };
		ad.advance(in, false);
		for (size_t k = 0; k < n; k++) {
			const uint8_t v = ram[k], was = prev[k];
			if (v == was) continue;
			Counter &s = st[k];
			if (v == was + 1) s.ups++;
			else if (v == 0) s.resets++;
			else s.other++;
			s.top = std::max(s.top, v);
			if (s.other == 0 && s.top <= 5 && log.size() < 4000000) log.push_back({ f, uint32_t(k), was, v });
			prev[k] = v;
		}
		for (size_t w = 0; w < watch.size(); w++)
			if (watch[w] < n && ram[watch[w]] != last_watch[w]) {
				printf("frame %ld: %05x = %u (mode %u)\n", f, watch[w], ram[watch[w]], m.game_mode());
				last_watch[w] = ram[watch[w]];
			}
		if (std::find(dumps.begin(), dumps.end(), f) != dumps.end()) {
			char path[64];
			snprintf(path, sizeof path, "dump_%ld.bin", f);
			if (FILE *o = fopen(path, "wb")) { fwrite(ram.data(), 1, n, o); fclose(o); }
		}
		if (std::find(tile_dumps.begin(), tile_dumps.end(), f) != tile_dumps.end()) {
			const segas24_tile &t = m.tiles();
			for (int l = 0; l < 4; l++)
				for (int row = 4; row <= 9; row++) {
					printf("frame %ld map %d row %d:", f, l, row);
					for (int c = 0; c < 64; c++) printf(" %04x", t.tile_r(uint32_t(l * 0x1000 + row * 64 + c)));
					printf("\n");
				}
			printf("frame %ld regs:", f);
			for (int k = 0; k < 16; k++) printf(" %04x", t.tile_r(0x5000 + k));
			printf("\n");
		}
		if (!shots.empty() && f % every == 0) {
			const bitmap_rgb32 &b = m.render(true);
			char path[512];
			snprintf(path, sizeof path, "%s/%06ld.png", shots.c_str(), f);
			write_png(path, b.data(), b.width(), b.height());
		}
	}
	if (!watch.empty()) return 0;
	// the candidates: up by one or back to 0 only, a few times, small
	for (size_t k = 0; k < n; k++) {
		const Counter &s = st[k];
		if (s.other || s.ups < 2 || s.resets < 1 || s.top > 5) continue;
		printf("%05zx: %u ups, %u resets, top %u:", k, s.ups, s.resets, s.top);
		int shown = 0;
		for (const Change &c : log)
			if (c.addr == k && shown++ < 24) printf(" %ld:%u", c.frame, c.to);
		printf("\n");
	}
	return 0;
}
