// vf2soak: the rollback soak test, netplay's first check (no network). Two boards play the same inputs: one
// straight through, and one as rollback netplay would run it, with player 2's inputs arriving --delay frames late.
// The second board guesses each of player 2's frames (the last input it had, as GGPO does), and as each real input
// arrives it goes back to that frame's save state and runs the frames since again (every frame by default, even
// when its guess was right; --only-misses: only when it was wrong). Each time a frame becomes certain its state
// must be the straight board's, byte for byte; at the first that isn't, the two states are written out and the
// parts that differ named.
//   vf2soak <vf2.zip> [--script FILE | --random SEED] [--frames N] [--delay D] [--only-misses] [--interp]
//           [--hashes FILE] [--quiet]
// Both boards start as a netplay match does (vf2_netplay's begin: a cold start, free play, the boot run unseen),
// with the sound board held and the byte link. --script: inputs from a script (src/host/input_script.h, frames
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

using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

// A frame's inputs, active high (a set bit: held): IN0's coins and starts, each player's IN1 / IN2
struct Ports { uint8_t in0 = 0, in1 = 0, in2 = 0; };
constexpr uint8_t P2_IN0 = 0x02 | 0x20;   // coin2, start2

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

// Two random players: the stick and buttons held for a few frames at a time, Start pressed now and then
struct Random
{
	std::mt19937 rng;
	uint8_t held[2] = {};
	int left[2] = {};
	explicit Random(uint32_t seed) : rng(seed) {}
	Ports next(uint64_t f)
	{
		Ports p;
		for (int k = 0; k < 2; k++) {
			if (left[k]-- <= 0) {
				const uint32_t r = rng();
				static const uint8_t STICK[9] = { 0, 0x10, 0x20, 0x40, 0x80, 0x50, 0x90, 0x60, 0xa0 };   // (none, the four ways, the diagonals)
				held[k] = uint8_t(STICK[r % 9] | ((r >> 4) & 7));   // P K G in bits 0-2
				left[k] = 2 + int((r >> 8) % 18);
			}
			// Start for 6 frames every few seconds, the players a second and a half apart
			const bool start = (f + uint64_t(k) * 90) % 400 < 6;
			if (k == 0) { p.in1 = held[0]; if (start) p.in0 |= 0x10; }
			else { p.in2 = held[1]; if (start) p.in0 |= 0x20; }
		}
		return p;
	}
};

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

// ---- the made-up ROM (--synthetic) ----

// i960 instructions (the KB's REG, MEMB and CTRL forms). Registers: r0-r15 are 0-15, g0-g15 16-31.
enum { G0 = 16 };
uint32_t reg(uint32_t op, uint32_t src1, uint32_t src2, uint32_t dst, bool lit1 = false)
{
	return (op >> 4) << 24 | dst << 19 | src2 << 14 | (lit1 ? 1u << 11 : 0) | (op & 15) << 7 | src1;
}
uint32_t memb(uint32_t op, uint32_t r, uint32_t abase, uint32_t mode) { return op << 24 | r << 19 | abase << 14 | 1u << 12 | mode << 10; }
enum : uint32_t { ADDO = 0x590, MULO = 0x701, XOR = 0x586, AND = 0x581, SHRO = 0x598, SHLO = 0x59c,
	LDOB = 0x80, STOB = 0x82, LDA = 0x8c, ST = 0x92, ABS = 0xc, DISP_BASE = 0xd };

romset::result synthetic_roms()
{
	romset::result r;
	r.ok = true;
	const size_t sizes[romset::N_REGIONS] = { 0x200000, 0x2400000, 0x800000, 0x2000000, 0x1000000, 0x80000, 0x800000, 0x40000, 0x80000, 0x200000 };
	for (int i = 0; i < romset::N_REGIONS; i++) r.regions[i].assign(sizes[i], 0);
	std::vector<uint8_t> &rom = r.regions[romset::MAINCPU];
	auto put = [&](uint32_t at, uint32_t w) { memcpy(&rom[at], &w, 4); };
	// the boot: the PRCB at 0x1000 (its interrupt table at 0x2000, the stack in RAM at 0x230000), the code at 0x100
	put(4, 0x1000);
	put(12, 0x100);
	put(0x1000 + 20, 0x2000);
	put(0x1000 + 24, 0x230000);
	std::vector<uint32_t> c;
	auto lda = [&](uint32_t v, int g) { c.push_back(memb(LDA, G0 + g, 0, ABS)); c.push_back(v); };
	auto mem = [&](uint32_t op, int g, int base, uint32_t disp) { c.push_back(memb(op, G0 + g, G0 + base, DISP_BASE)); c.push_back(disp); };
	auto masked = [&](int shift, int mask_g, int to) {   // g<to> = (g0 >> shift) & g<mask>
		c.push_back(reg(SHRO, uint32_t(shift), G0 + 0, G0 + to, true));
		c.push_back(reg(AND, G0 + mask_g, G0 + to, G0 + to));
	};
	lda(0x12345678, 0);    // the number
	lda(1103515245, 1);    // its multiplier
	lda(0xffffc, 6); lda(0xfffc, 9); lda(0x3ffc, 10); lda(0x3ffffc, 11); lda(0x7fffc, 12); lda(0xfff0, 13);
	lda(0x4e, 2); mem(STOB, 2, 14, 0x01c80002);   // the UART: its mode (g14 is 0), then the command: transmit on
	lda(0x05, 2); mem(STOB, 2, 14, 0x01c80002);
	const uint32_t loop = uint32_t(c.size());
	c.push_back(reg(MULO, G0 + 1, G0 + 0, G0 + 0));
	c.push_back(reg(ADDO, 13, G0 + 0, G0 + 0, true));
	mem(LDOB, 3, 14, 0x01c00004);                 // IN1
	mem(LDOB, 4, 14, 0x01c00006);                 // IN2
	c.push_back(reg(SHLO, 8, G0 + 4, G0 + 4, true));
	c.push_back(reg(XOR, G0 + 3, G0 + 0, G0 + 0));
	c.push_back(reg(XOR, G0 + 4, G0 + 0, G0 + 0));
	masked(12, 12, 5); mem(ST, 0, 5, 0x00510000);   // work RAM (past the game's own variables)
	masked(0, 9, 5); mem(ST, 0, 5, 0x01000000);     // tile RAM
	masked(9, 12, 5); mem(ST, 0, 5, 0x01080000);    // character RAM
	masked(3, 10, 5); mem(ST, 0, 5, 0x01800000);    // palette
	masked(7, 9, 5); mem(ST, 0, 5, 0x01810000);     // colour tables
	masked(6, 11, 5); mem(ST, 0, 5, 0x12000000);    // texture RAM 0
	masked(10, 11, 5); mem(ST, 0, 5, 0x12400000);   // texture RAM 1
	masked(4, 13, 5); mem(ST, 5, 14, 0x00f00000);   // timer 0, counting down from a number up to 0xfff0
	mem(STOB, 0, 14, 0x01c80000);                   // a byte to the sound board
	const int32_t back = -int32_t((c.size() - loop) * 4);
	c.push_back(0x08u << 24 | (uint32_t(back) & 0xfffffc));   // b loop
	for (size_t i = 0; i < c.size(); i++) put(uint32_t(0x100 + 4 * i), c[i]);
	return r;
}

// The start of a match, as vf2_netplay's begin: a cold start; on the real ROM, free play once the settings are
// there, the FREE PLAY notices hidden, the WARNING shortened, the boot run unseen
bool start(model2a &m, bool synthetic)
{
	m.set_sound(false);
	m.set_byte_link(true);
	m.set_draw_3d(false);
	m.power_on();
	if (synthetic) return true;
	if (!m.shorten_warning()) return false;
	m.hide_free_play_text();
	for (int i = 0; i < 300 && m.coin_setting() < 0; i++) m.run_frame();
	if (m.coin_setting() < 0) return false;
	m.set_coin_setting(model2a::COIN_FREE_PLAY);
	std::vector<uint8_t> backup = m.backup_ram();
	std::vector<u16> eeprom(m.eeprom_cells(), m.eeprom_cells() + 64);
	m.power_on();
	m.backup_ram() = backup;
	std::copy(eeprom.begin(), eeprom.end(), m.eeprom_cells());
	m.reset();
	for (int i = 0; i < 1200 && !m.past_boot(); i++) m.run_frame();
	return m.past_boot();
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
	auto load = [&]() { return synthetic ? synthetic_roms() : romset::load(rom); };
	romset::result ra = load(), rb = load();
	if (!ra.ok) {
		fprintf(stderr, "vf2soak: %s\n", ra.error.c_str());
		return 1;
	}
	auto pa = std::make_unique<model2a>(std::move(ra)), pb = std::make_unique<model2a>(std::move(rb));
	model2a &A = *pa, &B = *pb;   // A straight through, B rolled back
	if (interp) { A.use_interpreter(); B.use_interpreter(); }
	if (!start(A, synthetic) || !start(B, synthetic)) {
		fprintf(stderr, "vf2soak: the boot didn't reach the attract (not the ROM known?)\n");
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
	Random rnd(seed);
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
