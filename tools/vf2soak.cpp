// vf2soak: the rollback soak test, netplay's first check (no network). Two boards play the same inputs: one
// straight through, and one as rollback netplay would run it, with player 2's inputs arriving --delay frames late.
// The second board guesses each of player 2's frames (the last input it had, as GGPO does), and as each real input
// arrives it goes back to that frame's save state and runs the frames since again (every frame by default, even
// when its guess was right; --only-misses: only when it was wrong). Each time a frame becomes certain its state
// must be the straight board's, byte for byte; at the first that isn't, the two states are written out and the
// parts that differ named.
//   vf2soak <vf2.zip> [--script FILE | --random SEED] [--frames N] [--delay D] [--only-misses] [--interp]
//           [--hashes FILE] [--quiet]
// Both boards start as a netplay match does (vf2_netplay's begin: a cold start, free play, the boot run unseen,
// the sound board held and the byte link). --script: inputs from a script (src/host/input_script.h, frames
// counted from the match's start); --random SEED: two random players who press Start now and then (a versus: one
// joins, the loser comes back in), the default with seed 1. --interp: MAME's interpreter on both boards, else the
// recompiled code (as the game runs). --hashes FILE: the straight board's state hash for each frame, to compare
// runs on other machines (x86-64 and ARM64 must match). Also reports what a frame costs (save, restore, run),
// against the 17.4 ms a frame has at 57.5 Hz.
//   vf2soak --synthetic [--frames N] ...: no ROM: a made-up one whose small i960 program mixes the inputs into
// numbers it writes all over the RAMs and devices (tile, character, palette, colour and texture RAMs, the
// timers, the UART to the sound board). It checks the save state's mechanics (the scheduler, the timers, the
// pages written since) without the game, on the interpreter.
#include "../src/machine/model2a.h"
#include "../src/host/input_script.h"
#include "../src/host/vf2_netplay.h"
#include "random_players.h"
#include "synthetic_rom.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

using players::Ports;
using players::P2_IN0;
using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

// Player 1's inputs from a, player 2's from b
Ports mix(const Ports &a, const Ports &b) { return { uint8_t((a.in0 & ~P2_IN0) | (b.in0 & P2_IN0)), a.in1, b.in2 }; }
bool same_p2(const Ports &a, const Ports &b) { return (a.in0 & P2_IN0) == (b.in0 & P2_IN0) && a.in2 == b.in2; }

void apply(model2a &m, const Ports &p)
{
	m.m_in0 = uint8_t(~p.in0);
	m.m_in1 = uint8_t(~p.in1);
	m.m_in2 = uint8_t(~p.in2);
	m.m_sw = 0xff;
}

struct Stat
{
	double total = 0, worst = 0;
	uint64_t n = 0;
	void add(double ms) { total += ms; worst = std::max(worst, ms); n++; }
	double mean() const { return n ? total / double(n) : 0; }
};

// Where two states differ, by the sections the save named
void report(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, const std::vector<m2::StateSection> &sec)
{
	if (a.size() != b.size())
		printf("  the states' sizes differ: %zu, %zu\n", a.size(), b.size());
	const size_t n = std::min(a.size(), b.size());
	for (size_t i = 0; i < sec.size(); i++) {
		const size_t from = sec[i].offset, to = i + 1 < sec.size() ? sec[i + 1].offset : n;
		if (from >= n) break;
		size_t count = 0, first = 0;
		for (size_t k = from; k < std::min(to, n); k++)
			if (a[k] != b[k]) { if (!count) first = k - from; count++; }
		if (count)
			printf("  %s: %zu bytes differ, the first %zu bytes in (offset %zu in the state)\n", sec[i].name, count, first, from + first);
	}
}

bool write_file(const char *path, const std::vector<uint8_t> &v)
{
	FILE *f = fopen(path, "wb");
	if (!f) return false;
	const bool ok = fwrite(v.data(), 1, v.size(), f) == v.size();
	return fclose(f) == 0 && ok;
}

// The start of a match: vf2_netplay's begin, from a fresh board's backup RAM and EEPROM (a first run's; the
// made-up ROM's with the settings block in it)
bool start(model2a &m, bool synthetic)
{
	if (synthetic) synthetic::settings(m.backup_ram());
	m.set_draw_3d(false);
	vf2net::Adapter adapter(m);
	return adapter.begin(vf2net::start_blob(m));
}

} // namespace

int main(int argc, char **argv)
{
	std::string rom, script_path, hashes_path;
	long frames = -1, delay = 7;
	uint32_t seed = 1;
	bool quiet = false, interp = false, only_misses = false, synthetic = false;
	const char *usage = "usage: vf2soak <vf2.zip> [--script FILE | --random SEED] [--frames N] [--delay D] [--only-misses] [--interp] [--hashes FILE] [--quiet]\n"
	                    "       vf2soak --synthetic [--frames N] [--delay D] [--only-misses] [--hashes FILE] [--quiet]\n";
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--frames" && i + 1 < argc) frames = strtol(argv[++i], nullptr, 10);
		else if (a == "--script" && i + 1 < argc) script_path = argv[++i];
		else if (a == "--random" && i + 1 < argc) seed = uint32_t(strtoul(argv[++i], nullptr, 10));
		else if (a == "--delay" && i + 1 < argc) delay = strtol(argv[++i], nullptr, 10);
		else if (a == "--hashes" && i + 1 < argc) hashes_path = argv[++i];
		else if (a == "--only-misses") only_misses = true;
		else if (a == "--interp") interp = true;
		else if (a == "--synthetic") synthetic = true;
		else if (a == "--quiet") quiet = true;
		else if (a[0] != '-' && rom.empty()) rom = a;
		else {
			fprintf(stderr, "vf2soak: unknown argument %s\n%s", a.c_str(), usage);
			return 2;
		}
	}
	if ((rom.empty() && !synthetic) || delay < 1 || delay > 60) {
		fprintf(stderr, "%s", usage);
		return 2;
	}
	if (synthetic) {
		interp = true;
		if (frames < 0) frames = 2000;
	}
	auto load = [&]() { return synthetic ? synthetic::roms() : romset::load(rom); };
	romset::result ra = load(), rb = load();
	if (!ra.ok) {
		fprintf(stderr, "vf2soak: %s\n", ra.error.c_str());
		return 1;
	}
	auto pa = std::make_unique<model2a>(std::move(ra)), pb = std::make_unique<model2a>(std::move(rb));
	model2a &A = *pa, &B = *pb;   // A straight through, B rolled back
	if (interp) { A.use_interpreter(); B.use_interpreter(); }
	if (!start(A, synthetic) || !start(B, synthetic)) {
		fprintf(stderr, "vf2soak: the match's start failed (not the ROM known?)\n");
		return 1;
	}
	input_script script;
	std::string error;
	if (!script_path.empty() && !script.load(script_path, error)) {
		fprintf(stderr, "vf2soak: %s\n", error.c_str());
		return 1;
	}
	if (!script_path.empty() && frames < 0) frames = long(script.end);
	if (frames < 0) frames = 20000;
	players::Random rnd(seed);
	FILE *hashes = nullptr;
	if (!hashes_path.empty() && !(hashes = fopen(hashes_path.c_str(), "w"))) {
		fprintf(stderr, "vf2soak: can't write %s\n", hashes_path.c_str());
		return 1;
	}

	// B's save states, the one before each of the last delay + 1 frames (ring[f % size]: the state before frame f).
	// A runs delay frames behind B: it runs frame k once k is certain, after checking B's state before k is its own.
	const size_t ring_n = size_t(delay) + 1;
	std::vector<std::vector<uint8_t>> ring(ring_n);
	std::vector<m2::StateSection> sections;
	std::vector<uint8_t> straight;
	std::vector<Ports> real, used;   // per frame: the inputs; what B last ran it with
	Stat t_save, t_load, t_run, t_tick;
	uint64_t rollbacks = 0, misses = 0, checked = 0;

	auto t0 = Clock::now();
	bool failed = false;
	for (uint64_t f = 0; f < uint64_t(frames); f++) {
		Ports c;
		if (!script_path.empty()) {
			uint8_t ports[input_script::N_PORTS] = { 0xff, 0xff, 0xff, 0xff };
			script.apply(f, ports);
			c = { uint8_t(~ports[0]), uint8_t(~ports[1]), uint8_t(~ports[2]) };
		} else
			c = rnd.next(f);
		real.push_back(c);

		auto tick = Clock::now();
		if (f >= uint64_t(delay)) {
			// Player 2's input for frame k arrives: k is certain, and B's state before it was made with real inputs only
			const uint64_t k = f - uint64_t(delay);
			std::vector<uint8_t> &mine = ring[k % ring_n];
			A.save_state(straight, &sections);
			const size_t H = model2a::STATE_HEADER;   // (each board's own bookkeeping: not compared)
			if (hashes) fprintf(hashes, "%llu %016llx\n", (unsigned long long)k, (unsigned long long)m2::state_hash(straight.data() + H, straight.size() - H));
			if (mine.size() != straight.size() || memcmp(mine.data() + H, straight.data() + H, mine.size() - H) != 0) {
				printf("frame %llu: the rolled-back board's state differs from the straight one's (before the frame)\n", (unsigned long long)k);
				report(straight, mine, sections);
				if (write_file("soak_straight.bin", straight) && write_file("soak_rolled.bin", mine))
					printf("  both written: soak_straight.bin, soak_rolled.bin\n");
				failed = true;
				break;
			}
			checked++;
			apply(A, real[k]);
			A.run_frame();

			// Back to that state and every frame since run again, with k's input (and it guessed held since), if the
			// guess for k was wrong, or always
			const bool miss = !same_p2(used[k], real[k]);
			misses += miss;
			tick = Clock::now();
			if (miss || !only_misses) {
				rollbacks++;
				auto t = Clock::now();
				if (!B.load_state(mine.data(), mine.size())) {
					printf("frame %llu: the save state didn't load\n", (unsigned long long)k);
					failed = true;
					break;
				}
				t_load.add(ms_since(t));
				for (uint64_t g = k; g < f; g++) {
					if (g != k) {
						t = Clock::now();
						B.save_state(ring[g % ring_n]);
						t_save.add(ms_since(t));
					}
					used[g] = mix(real[g], real[k]);
					t = Clock::now();
					apply(B, used[g]);
					B.run_frame();
					t_run.add(ms_since(t));
				}
			}
		}
		// Frame f on B: its state before it kept, player 2's input guessed (the last one come: none yet at first)
		auto t = Clock::now();
		B.save_state(ring[f % ring_n]);
		t_save.add(ms_since(t));
		used.push_back(mix(real[f], f >= uint64_t(delay) ? real[f - uint64_t(delay)] : Ports{}));
		t = Clock::now();
		apply(B, used[f]);
		B.run_frame();
		t_run.add(ms_since(t));
		t_tick.add(ms_since(tick));

		if (!quiet && f % 1000 == 999)
			printf("frame %llu: %llu checked, %llu rollbacks, %llu guesses wrong (game mode %u)\n", (unsigned long long)f + 1,
			       (unsigned long long)checked, (unsigned long long)rollbacks, (unsigned long long)misses, unsigned(A.game_mode()));
	}
	if (hashes) fclose(hashes);
	if (!quiet || !failed) {
		const double budget = 1000.0 / 57.524160;
		printf("%s: %llu frames checked, %llu rollbacks of %ld frames (%llu guesses wrong), %.1f s\n", failed ? "FAILED" : "identical",
		       (unsigned long long)checked, (unsigned long long)rollbacks, delay, (unsigned long long)misses, ms_since(t0) / 1000);
		printf("state %.1f MB; ms mean / worst: save %.2f / %.2f, restore %.2f / %.2f, a frame's run %.2f / %.2f\n",
		       double(ring[0].size()) / 1048576, t_save.mean(), t_save.worst, t_load.mean(), t_load.worst, t_run.mean(), t_run.worst);
		printf("a frame with its rollback: %.2f ms mean, %.2f worst, of the %.1f ms a frame has (the board only: no picture, no sound)\n",
		       t_tick.mean(), t_tick.worst, budget);
		if (synthetic) {
			size_t tiles = 0, work = 0;
			for (uint16_t w : A.tiles().tile_ram) tiles += w != 0;
			for (uint8_t b : A.workram()) work += b != 0;
			printf("the made-up program: i960 at %08x, %zu tile RAM words and %zu work RAM bytes set, %u bytes to the sound board, %llu IRQs\n",
			       A.cpu().ip(), tiles, work, A.m_uart_bytes, (unsigned long long)(A.m_irq_count[0] + A.m_irq_count[1] + A.m_irq_count[2] + A.m_irq_count[3]));
		}
		if (A.polygon_count_reads() || B.polygon_count_reads())
			printf("note: the game read the 3D's polygon count (%llu times): it isn't in the state\n", (unsigned long long)A.polygon_count_reads());
	}
	return failed ? 1 : 0;
}
