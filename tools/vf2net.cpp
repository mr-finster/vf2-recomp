// vf2net: VF2 netplay, two boards in one process: a host and a guest, each with its own netplay session (src/net)
// and its own recompiled code, talking through a simulated network (latency, jitter, loss), each playing its own
// side of the inputs. The sessions compare their states every 30 frames as netplay does (a desync ends the run);
// at the end a third board runs the same inputs straight through, and both players' boards must have been in its
// state at every checkpoint. Reports the rollbacks and what a frame cost each side (the wall time of a session's
// step: its rollback, its save and its frame). VF3's vf3net, for VF2.
//   vf2net <vf2.zip> [--script FILE | --random SEED] [--frames N] [--latency MS] [--jitter MS] [--loss PERCENT]
//          [--delay D] [--interp] [--soft] [--stage N] [--hashes FILE]
// --stage N: the host's place in the stages' list (else a random one, as a match has). --hashes FILE: the straight
// board's state hash every 30 frames ("frame hash", hex): a release's file against the last online release's says
// whether the two play each other (docs: the online version); with --stage and --random, the same every run
//   vf2net --synthetic ...: no ROM, the made-up one (tools/synthetic_rom.h), on the interpreter
#include "../src/host/input_script.h"
#include "../src/host/vf2_netplay.h"
#include "../src/machine/model2a.h"
#include "../src/net/session.h"
#include "random_players.h"
#include "synthetic_rom.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Board
{
	std::unique_ptr<model2a> m;
	std::unique_ptr<vf2net::Adapter> adapter;
	bool open(romset::result &&roms, bool interp, std::string &error)
	{
		if (!roms.ok) { error = roms.error; return false; }
		m = std::make_unique<model2a>(std::move(roms));
		if (interp) m->use_interpreter();
		m->set_draw_3d(false);
		adapter = std::make_unique<vf2net::Adapter>(*m);
		return true;
	}
};

struct Timing
{
	double total = 0, worst = 0;
	uint64_t n = 0;
	void add(double ms) { total += ms; worst = std::max(worst, ms); n++; }
};

// A player's input word from a frame's ports (active high)
uint16_t word(const players::Ports &p, int player) { return vf2net::pack(uint8_t(~p.in0), uint8_t(~(player ? p.in2 : p.in1)), player); }

} // namespace

int main(int argc, char **argv)
{
	std::string rom, script_path;
	long frames = 3000;
	uint32_t seed = 1;
	vfnet::SimWire::Params p{ 40, 8, 0 };
	int delay = 2;
	int stage = -1;
	const char *hashes_path = nullptr;
	bool interp = false, soft = false, synthetic = false;   // soft: each side's clock a little slower while ahead, as the game's
	const char *usage = "usage: vf2net <vf2.zip> [--script FILE | --random SEED] [--frames N] [--latency MS] [--jitter MS] [--loss PERCENT] [--delay D] [--interp] [--soft]\n"
	                    "       vf2net --synthetic [--frames N] ...\n";
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--frames" && i + 1 < argc) frames = strtol(argv[++i], nullptr, 10);
		else if (a == "--script" && i + 1 < argc) script_path = argv[++i];
		else if (a == "--random" && i + 1 < argc) seed = uint32_t(strtoul(argv[++i], nullptr, 10));
		else if (a == "--latency" && i + 1 < argc) p.latency_ms = atof(argv[++i]);
		else if (a == "--jitter" && i + 1 < argc) p.jitter_ms = atof(argv[++i]);
		else if (a == "--loss" && i + 1 < argc) p.loss = atof(argv[++i]) / 100;
		else if (a == "--delay" && i + 1 < argc) delay = atoi(argv[++i]);
		else if (a == "--stage" && i + 1 < argc) stage = atoi(argv[++i]);
		else if (a == "--hashes" && i + 1 < argc) hashes_path = argv[++i];
		else if (a == "--interp") interp = true;
		else if (a == "--soft") soft = true;
		else if (a == "--synthetic") synthetic = interp = true;
		else if (a[0] != '-' && rom.empty()) rom = a;
		else { fprintf(stderr, "vf2net: unknown argument %s\n%s", a.c_str(), usage); return 2; }
	}
	if (rom.empty() && !synthetic) { fprintf(stderr, "%s", usage); return 2; }
	auto load = [&]() { return synthetic ? synthetic::roms() : romset::load(rom); };

	auto pa = std::make_unique<Board>(), pb = std::make_unique<Board>();
	Board &A = *pa, &B = *pb;
	std::string error;
	if (!A.open(load(), interp, error) || !B.open(load(), interp, error)) { fprintf(stderr, "vf2net: %s\n", error.c_str()); return 1; }

	// the inputs for every frame (each side plays its own player's)
	std::vector<players::Ports> real;
	if (!script_path.empty()) {
		input_script script;
		if (!script.load(script_path, error)) { fprintf(stderr, "vf2net: %s\n", error.c_str()); return 1; }
		for (long f = 0; f < frames + 200; f++) {
			uint8_t ports[input_script::N_PORTS] = { 0xff, 0xff, 0xff, 0xff };
			script.apply(uint64_t(f), ports);
			real.push_back({ uint8_t(~ports[0]), uint8_t(~ports[1]), uint8_t(~ports[2]) });
		}
	} else {
		players::Random rnd(seed);
		for (long f = 0; f < frames + 200; f++) real.push_back(rnd.next(uint64_t(f)));
	}

	// (the host's NVRAM: a fresh board's, as on a first run; the made-up ROM's with the settings in it)
	if (synthetic) synthetic::settings(A.m->backup_ram());
	std::vector<uint8_t> blob = vf2net::start_blob(*A.m);
	if (stage >= 0) blob.back() = uint8_t(stage % vf2net::STAGES);
	const uint64_t content = vf2net::content_hash(*A.m);
	if (content != vf2net::content_hash(*B.m)) { fprintf(stderr, "vf2net: the two boards' ROM hashes differ\n"); return 1; }

	vfnet::SimWire wire(p, p, 7);
	vfnet::Config cfg;
	cfg.delay = delay;
	cfg.soft_sync = soft;
	cfg.handshake_ms = 300000;   // (both boots run here one after the other, on the one clock)
	const vfnet::Identity ia{ "vf2", "test", content, "HOST" }, ib{ "vf2", "test", content, "GUEST" };
	A.adapter->checkpoint_every = B.adapter->checkpoint_every = 300;
	std::vector<uint8_t> sound[2];   // (the bytes each side sent its sound board)
	A.adapter->sound_out = [&](const std::vector<uint8_t> &b) { sound[0].insert(sound[0].end(), b.begin(), b.end()); };
	B.adapter->sound_out = [&](const std::vector<uint8_t> &b) { sound[1].insert(sound[1].end(), b.begin(), b.end()); };
	vfnet::Session sa(wire.a(), *A.adapter, ia, cfg, true, blob);
	vfnet::Session sb(wire.b(), *B.adapter, ib, cfg, false);

	Timing ta, tb;
	size_t na = 0, nb = 0;   // each side's frames run (its next frame's inputs)
	double t = 1000, next_a = 1000, next_b = 1000 + 23;
	const double fa = cfg.frame_ms, fb = cfg.frame_ms * 1.002;
	const auto t0 = Clock::now();
	bool connected = false;
	vf2net::Score score_a, score_b, score_c;   // (the detector on each side, as the game runs it: after each step, guesses and all)
	while (sa.state() != vfnet::Session::State::ended && sb.state() != vfnet::Session::State::ended &&
	       (long(sa.frame()) < frames || long(sb.frame()) < frames)) {
		wire.now = t;
		sa.poll(t);
		sb.poll(t);
		if (!connected && sa.state() == vfnet::Session::State::running && sb.state() == vfnet::Session::State::running) {
			connected = true;
			printf("connected (both boots run) after %.1f s\n", std::chrono::duration<double>(Clock::now() - t0).count());
		}
		if (t >= next_a) {
			const auto s0 = Clock::now();
			if (sa.step(word(real[std::min(na, real.size() - 1)], 0))) { na++; score_a.frame(*A.m); }
			if (connected) ta.add(std::chrono::duration<double, std::milli>(Clock::now() - s0).count());
			next_a += fa / (1 - (soft ? std::clamp(sa.stats().lead * 0.02, 0.0, 0.04) : 0.0));
		}
		if (t >= next_b) {
			const auto s0 = Clock::now();
			if (sb.step(word(real[std::min(nb, real.size() - 1)], 1))) { nb++; score_b.frame(*B.m); }
			if (connected) tb.add(std::chrono::duration<double, std::milli>(Clock::now() - s0).count());
			next_b += fb / (1 - (soft ? std::clamp(sb.stats().lead * 0.02, 0.0, 0.04) : 0.0));
		}
		t += 1;
	}
	vfnet::Session::Event e;
	bool failed = false;
	while (sa.next_event(e)) if (e.kind == e.ended) { printf("host: %s\n", e.text.c_str()); failed = true; }
	while (sb.next_event(e)) if (e.kind == e.ended) { printf("guest: %s\n", e.text.c_str()); failed = true; }

	// the same inputs straight through, on a third board, against both players' checkpoints
	const uint32_t n = std::min(sa.confirmed(), sb.confirmed());
	uint32_t checked = 0;
	std::vector<uint8_t> straight_sound;
	if (!failed) {
		auto pc = std::make_unique<Board>();
		Board &C = *pc;
		if (!C.open(load(), interp, error) || !C.adapter->begin(blob)) { fprintf(stderr, "vf2net: the straight board didn't start\n"); return 1; }
		C.adapter->checkpoint_every = hashes_path ? 30 : 300;
		C.adapter->sound_out = [&](const std::vector<uint8_t> &b) { straight_sound.insert(straight_sound.end(), b.begin(), b.end()); };
		for (uint32_t f = 0; f < n; f++) {
			const uint16_t in[2] = { sa.inputs(0)[f], sa.inputs(1)[f] };
			if (in[0] != sb.inputs(0)[f] || in[1] != sb.inputs(1)[f]) { printf("the two sides' inputs differ at frame %u\n", f); failed = true; break; }
			C.adapter->advance(in, false);
			score_c.frame(*C.m);
		}
		printf("the score: host %d-%d, guest %d-%d, straight %d-%d (rounds %d-%d)%s\n", score_a.matches[0], score_a.matches[1], score_b.matches[0],
		       score_b.matches[1], score_c.matches[0], score_c.matches[1], score_c.rounds[0], score_c.rounds[1],
		       vf2net::Score::known() ? "" : " (its places in RAM not found yet)");
		for (int q = 0; q < 2; q++)
			if (score_a.matches[q] != score_c.matches[q] || score_b.matches[q] != score_c.matches[q] || score_a.rounds[q] != score_c.rounds[q] || score_b.rounds[q] != score_c.rounds[q]) {
				printf("the score differs from the straight run's\n");
				failed = true;
				break;
			}
		if (hashes_path) {
			FILE *hf = fopen(hashes_path, "w");
			if (!hf) { fprintf(stderr, "vf2net: can't write %s\n", hashes_path); return 1; }
			for (auto &[f, h] : C.adapter->checkpoints) if (f <= n) fprintf(hf, "%u %016llx\n", f, (unsigned long long)h);
			fclose(hf);
		}
		for (auto &[f, h] : C.adapter->checkpoints) {
			if (f > n || f % 300) continue;
			if (A.adapter->checkpoints[f] != h) { printf("the host's board differs from a straight run after frame %u\n", f); failed = true; break; }
			if (B.adapter->checkpoints[f] != h) { printf("the guest's board differs from a straight run after frame %u\n", f); failed = true; break; }
			checked++;
		}
		// the sound board's bytes: each side sent each frame's once, guessed or not; a wrong guess's can differ
		printf("bytes to the sound board: host %zu, guest %zu, straight %zu (to frame %u; a wrong guess's stay)\n", sound[0].size(), sound[1].size(), straight_sound.size(), n);
	}
	const auto &xa = sa.stats(), &xb = sb.stats();
	printf("%s: %u frames confirmed, %u checkpoints against a straight run; %.0f ms each way, jitter %.0f, %.0f%% lost; delay %d\n",
	       failed ? "FAILED" : "in step", n, checked, p.latency_ms, p.jitter_ms, p.loss * 100, delay);
	for (int s = 0; s < 2; s++) {
		const auto &x = s ? xb : xa;
		const Timing &tm = s ? tb : ta;
		printf("  %s: ping %.0f ms, %llu rollbacks (%.1f frames on average), %llu waits for the other side, %llu to keep in time; a step %.2f ms on average, %.2f the worst (a frame has %.1f)\n",
		       s ? "guest" : "host ", x.rtt_ms, (unsigned long long)x.rollbacks, x.rollbacks ? double(x.resim_frames) / double(x.rollbacks) : 0.0,
		       (unsigned long long)x.stalls, (unsigned long long)x.waits, tm.n ? tm.total / double(tm.n) : 0.0, tm.worst, cfg.frame_ms);
	}
	return failed ? 1 : 0;
}
