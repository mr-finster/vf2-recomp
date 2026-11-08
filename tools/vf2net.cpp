// vf2net: VF2 netplay, two boards in one process: a host and a guest, each with its own netplay session (src/net)
// and its own recompiled code, talking through a simulated network (latency, jitter, loss), each playing its own
// side of the inputs. The sessions compare their states every 30 frames as netplay does (a desync ends the run);
// at the end a third board runs the same inputs straight through, and both players' boards must have been in its
// state at every checkpoint. Reports the rollbacks and what a frame cost each side (the wall time of a session's
// step: its rollback, its save and its frame). VF3's vf3net, for VF2.
//   vf2net <vf2.zip> [--script FILE | --random SEED] [--frames N] [--latency MS] [--jitter MS] [--loss PERCENT]
//          [--delay D] [--interp] [--soft] [--stage N] [--hashes FILE] [--p1 F] [--p2 F] [--match-seed N] [--mix]
// --p1, --p2: each player's fighter (Cheats' random fighters), as the handshake carries it (R, S<place>) or as the
// Cheats page has it (*, a name), either one making it a random fighters' match (start_blob_random's; neither: v0.1.16's
// match, start_blob's); --match-seed: the host's blob's seed (else a fixed one)
// --stage N: the host's place in the stages' list (else a random one, as a match has). --hashes FILE: the straight
// board's state hash every 30 frames ("frame hash", hex): a release's file against the last online release's says
// whether the two play each other (docs: the online version); with --stage and --random, the same every run
// --blob-out FILE: the host's start blob written to FILE; --blob-in FILE: the match started from FILE's blob instead of
// this build's (a cross-version test: another build's host, this build's guest and straight board, the hashes compared)
//   vf2net --synthetic ...: no ROM, the made-up one (tools/synthetic_rom.h), on the interpreter
// --watchers N: N more boards watching the match (src/net/watch.h), as the game's watchers do: the host's uploads every
// 100 ms to a stand-in for the lobby (--watch-ms each way), each watcher asking it for the frames and running them a few
// seconds behind; the first from the start, the others joining at --watch-late F (catching up from the start, unseen).
// The start is the game's (vf2net::watch_blob: the guest's fighter after the session's blob, split again by each
// watcher). Each watcher's board must be the straight run's at every checkpoint, its own checks of the players' hashes
// all good (one come late: every one of them, from the match's start); the players' rollbacks and waits are printed as
// ever, to be compared with a run without watchers.
// --watch-snap: the late watchers come in from the host's snapshot (watch.h's Snapper, asked for a little before they
// come, as the lobby asks when one does), not from the match's start; its sound bytes (the host's frames before it)
// checked against the straight run's. --snap-out DIR: the last snapshot, the session's start blob and both players'
// inputs written to DIR (snap.bin, blob.bin, inputs.bin); --replay DIR: none of the above, but a watcher in this process
// loading DIR's snapshot as the game does and running the inputs after it, its state hash every 30 frames to --hashes:
// another process's (another build's, another machine's) run against the first's --hashes from the snapshot's frame on.
#include "../src/host/input_script.h"
#include "../src/host/vf2_netplay.h"
#include "../src/machine/model2a.h"
#include "../src/net/session.h"
#include "../src/net/watch.h"
#include "watch_pace.h"
#include "random_players.h"
#include "synthetic_rom.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

// --pace old|new (with --replay): a watcher come late timed as the game runs one (watch_pace.h), --behind S seconds
// behind the match as it comes (from the snapshot's frame; --from-start: from the match's start, no snapshot), for
// --cap S seconds at most
const char *g_pace = nullptr;
double g_behind = 60, g_cap = 120;
bool g_from_start = false;

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

bool read_file(const std::string &path, std::vector<uint8_t> &out)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) return false;
	out.clear();
	for (int ch; (ch = fgetc(f)) != EOF;) out.push_back(uint8_t(ch));
	fclose(f);
	return true;
}

bool write_file(const std::string &path, const std::vector<uint8_t> &v)
{
	FILE *f = fopen(path.c_str(), "wb");
	if (!f) return false;
	const bool ok = fwrite(v.data(), 1, v.size(), f) == v.size();
	return fclose(f) == 0 && ok;
}

// (a snapshot's extra, VF2's: the host's frames' sound bytes before it, all of them one after another)
bool tail_bytes(const std::vector<uint8_t> &x, uint32_t &first, uint32_t &n, std::vector<uint8_t> &all)
{
	if (x.size() < 12) return false;
	auto u32 = [&](size_t at) { return uint32_t(x[at]) | uint32_t(x[at + 1]) << 8 | uint32_t(x[at + 2]) << 16 | uint32_t(x[at + 3]) << 24; };
	if (u32(0) != vf2net::TAIL_MAGIC) return false;
	first = u32(4);
	n = u32(8);
	size_t at = 12;
	all.clear();
	for (uint32_t i = 0; i < n; i++) {
		if (x.size() - at < 2) return false;
		const size_t len = x[at] | size_t(x[at + 1]) << 8;
		at += 2;
		if (x.size() - at < len) return false;
		all.insert(all.end(), x.begin() + ptrdiff_t(at), x.begin() + ptrdiff_t(at + len));
		at += len;
	}
	return true;
}

} // namespace

int main(int argc, char **argv)
{
	std::string rom, script_path;
	long frames = 3000;
	uint32_t seed = 1;
	vfnet::SimWire::Params p{ 40, 8, 0 };
	int delay = 2;
	int stage = -1;
	int watchers = 0;
	long watch_late = 1500;
	double watch_ms = 60;
	int vs_rounds = 0;   // --vs-rounds N: the host's Rounds to win (vs player), set before the match (tests: the guest plays it)
	const char *hashes_path = nullptr, *blob_out = nullptr, *blob_in = nullptr, *snap_out = nullptr, *replay_dir = nullptr;
	bool watch_snap = false;
	bool mix = false;   // --mix: a pool match (MIX_TAG): the random fighters' protocol, Normal fighters too
	bool interp = false, soft = false, synthetic = false;   // soft: each side's clock a little slower while ahead, as the game's
	std::string p1, p2;
	uint32_t match_seed = 0x2545f491;
	const char *usage = "usage: vf2net <vf2.zip> [--script FILE | --random SEED] [--frames N] [--latency MS] [--jitter MS] [--loss PERCENT] [--delay D] [--interp] [--soft]\n"
	                    "       [--watchers N] [--watch-late FRAME] [--watch-ms MS]\n"
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
		else if (a == "--vs-rounds" && i + 1 < argc) vs_rounds = atoi(argv[++i]);
		else if (a == "--hashes" && i + 1 < argc) hashes_path = argv[++i];
		else if (a == "--blob-out" && i + 1 < argc) blob_out = argv[++i];
		else if (a == "--mix") mix = true;
		else if (a == "--blob-in" && i + 1 < argc) blob_in = argv[++i];
		else if (a == "--p1" && i + 1 < argc) p1 = argv[++i];
		else if (a == "--p2" && i + 1 < argc) p2 = argv[++i];
		else if (a == "--match-seed" && i + 1 < argc) match_seed = uint32_t(strtoul(argv[++i], nullptr, 10));
		else if (a == "--watchers" && i + 1 < argc) watchers = std::clamp(atoi(argv[++i]), 0, 4);
		else if (a == "--watch-late" && i + 1 < argc) watch_late = strtol(argv[++i], nullptr, 10);
		else if (a == "--watch-ms" && i + 1 < argc) watch_ms = atof(argv[++i]);
		else if (a == "--watch-snap") watch_snap = true;
		else if (a == "--snap-out" && i + 1 < argc) snap_out = argv[++i];
		else if (a == "--replay" && i + 1 < argc) replay_dir = argv[++i];
		else if (a == "--pace" && i + 1 < argc) g_pace = argv[++i];
		else if (a == "--behind" && i + 1 < argc) g_behind = atof(argv[++i]);
		else if (a == "--cap" && i + 1 < argc) g_cap = atof(argv[++i]);
		else if (a == "--from-start") g_from_start = true;
		else if (a == "--interp") interp = true;
		else if (a == "--soft") soft = true;
		else if (a == "--synthetic") synthetic = interp = true;
		else if (a[0] != '-' && rom.empty()) rom = a;
		else { fprintf(stderr, "vf2net: unknown argument %s\n%s", a.c_str(), usage); return 2; }
	}
	if (rom.empty() && !synthetic) { fprintf(stderr, "%s", usage); return 2; }
	auto load = [&]() { return synthetic ? synthetic::roms() : romset::load(rom); };
	if (replay_dir) {   // (a watcher in a process of its own, from another's snapshot, through the game's own way)
		namespace vw = vfnet::watch;
		const std::string dir = replay_dir;
		std::vector<uint8_t> blob, snap_bytes, in_bytes;
		if (!read_file(dir + "/blob.bin", blob) || !read_file(dir + "/snap.bin", snap_bytes) || !read_file(dir + "/inputs.bin", in_bytes)) { fprintf(stderr, "vf2net: --replay: %s/blob.bin, snap.bin, inputs.bin\n", dir.c_str()); return 1; }
		vw::Snap sn;
		const auto d0 = Clock::now();
		if (!vw::decode_snap(snap_bytes, sn)) { fprintf(stderr, "vf2net: --replay: the snapshot can't be read\n"); return 1; }
		const double decode_ms = std::chrono::duration<double, std::milli>(Clock::now() - d0).count();
		std::string error;
		Board W;
		if (!W.open(load(), interp, error)) { fprintf(stderr, "vf2net: %s\n", error.c_str()); return 1; }
		W.adapter->checkpoint_every = 30;
		std::vector<uint8_t> unheard;
		W.adapter->sound_unheard = [&](const std::vector<uint8_t> &b) { unheard.insert(unheard.end(), b.begin(), b.end()); };
		vw::Spectator sp(*W.adapter, vw::Spectator::Config{});
		vw::Start st;
		st.match = sn.match;
		st.meta.game = "vf2";
		st.blob = blob;
		const auto b0 = Clock::now();
		if (!sp.start(st)) { fprintf(stderr, "vf2net: --replay: the start blob didn't start\n"); return 1; }
		const double begin_ms = std::chrono::duration<double, std::milli>(Clock::now() - b0).count();
		if (g_pace) {   // (timed as the game runs a late watcher: from the match's start, or from the snapshot)
			const uint32_t total_in = uint32_t(in_bytes.size() / 4);
			std::vector<uint16_t> pin(size_t(total_in) * 2);
			memcpy(pin.data(), in_bytes.data(), size_t(total_in) * 4);
			const bool old_way = std::string(g_pace) == "old";
			double snap_ms = 0;
			uint32_t from = 0;
			if (!g_from_start) {
				const auto l1 = Clock::now();
				if (!sp.snapshot(sn)) { fprintf(stderr, "--pace: the snapshot wasn't taken: %s\n", sp.why.c_str()); return 1; }
				snap_ms = std::chrono::duration<double, std::milli>(Clock::now() - l1).count();
				from = sn.frame;
			}
			const watch_pace::Result r = watch_pace::run(sp, pin, from + uint32_t(g_behind * 57.524160), sn.match, old_way, g_cap);
			char what[200];
			snprintf(what, sizeof what, "%s, %s (its start %.1f s before; the snapshot read in %.0f ms, loaded and checked in %.0f ms)", old_way ? "the old way" : "the new way",
			         g_from_start ? "from the match's start" : "from the snapshot", begin_ms / 1000, g_from_start ? 0.0 : decode_ms, snap_ms);
			watch_pace::say(what, r, g_behind);
			return 0;
		}
		W.adapter->sound_starts = true;
		const auto l0 = Clock::now();
		if (!sp.snapshot(sn)) { fprintf(stderr, "vf2net: --replay: the snapshot wasn't taken: %s\n", sp.why.c_str()); return 1; }
		const double load_ms = std::chrono::duration<double, std::milli>(Clock::now() - l0).count();
		const uint32_t total = uint32_t(in_bytes.size() / 4);
		std::vector<uint16_t> in(size_t(total) * 2);
		memcpy(in.data(), in_bytes.data(), size_t(total) * 4);
		sp.on_catch_up = [&](bool on) { W.adapter->mute = on; };
		double t = 1000;
		const auto r0 = Clock::now();
		while (sp.frame() < total && sp.state() != vw::Spectator::State::out_of_step && sp.state() != vw::Spectator::State::failed) {
			if (sp.want(t) == "frames") {
				sp.asked(t);
				vw::Reply r;
				r.match = sn.match;
				r.state = 2;
				r.total = total;
				r.frames.first = sp.ask_from();
				const uint32_t nn = std::min<uint32_t>(total - r.frames.first, vw::MAX_REPLY);
				r.frames.in.assign(in.begin() + ptrdiff_t(r.frames.first) * 2, in.begin() + ptrdiff_t(r.frames.first + nn) * 2);
				sp.reply(r, t);
				sp.answered(t);
			}
			sp.run(t);
			t += 1;
		}
		const double run_ms = std::chrono::duration<double, std::milli>(Clock::now() - r0).count();
		uint32_t tf = 0, tn = 0;
		std::vector<uint8_t> tb;
		const bool tail = tail_bytes(sn.extra, tf, tn, tb);
		printf("replay: the snapshot at frame %u (%zu bytes packed, %zu its state, %zu its extra: %u frames' sound bytes, %zu of them), read in %.0f ms; the match's start %.0f ms, "
		       "the snapshot loaded and checked in %.0f ms; %u frames after it in %.0f ms (%.2f ms a frame); the sound board given %zu bytes unheard\n",
		       sn.frame, snap_bytes.size(), sn.state.size(), sn.extra.size(), tail ? tn : 0, tb.size(), decode_ms, begin_ms, load_ms, sp.frame() - sn.frame, run_ms,
		       run_ms / std::max(1u, sp.frame() - sn.frame), unheard.size());
		if (sp.state() == vw::Spectator::State::out_of_step) { printf("replay: FAILED: %s\n", sp.why.c_str()); return 1; }
		if (!tail || unheard.size() < tb.size() || !std::equal(tb.begin(), tb.end(), unheard.begin())) { printf("replay: FAILED: the snapshot's sound bytes didn't reach the sound board first\n"); return 1; }
		if (hashes_path) {
			FILE *hf = fopen(hashes_path, "w");
			if (!hf) return 1;
			for (auto &[f, h] : W.adapter->checkpoints) fprintf(hf, "%u %016llx\n", f, (unsigned long long)h);
			fclose(hf);
		}
		return 0;
	}

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
	if (vs_rounds) {   // (the host's settings as its own game wrote them, then the round count changed, as the menu does)
		if (!A.adapter->begin(vf2net::start_blob(*A.m))) { fprintf(stderr, "vf2net: the host's board didn't start\n"); return 1; }   // (its first boot writes its settings)
		A.m->set_assignment(model2a::game_assignment::match_vs, vs_rounds);
		printf("the host's Rounds to win (vs player): %d\n", A.m->assignment(model2a::game_assignment::match_vs));
	}
	// (each player's fighter as the handshake has it: the host's in its blob and its hello, the guest's in its hello)
	auto extra = [&](const std::string &v) {
		const bool sent = v.empty() || v == "R" || (v.size() > 1 && v[0] == 'S' && isdigit(uint8_t(v[1])));
		return sent ? v : vf2net::fighter_extra(v, *A.m);
	};
	p1 = extra(p1);
	p2 = extra(p2);
	// (a fighter on either side: a random fighters' match, both sides' online version vf2net::RANDOM_TAG's; else v0.1.16's)
	// (--mix: a pool match, vf2net::MIX_TAG's: the random fighters' protocol whatever each chose, Normal both included)
	const bool random = mix || !p1.empty() || !p2.empty();
	A.adapter->random_fighters = B.adapter->random_fighters = random;
	A.adapter->hosting = true;
	A.adapter->own_extra = p1;
	B.adapter->own_extra = p2;
	if (!p1.empty() || !p2.empty()) printf("fighters: host %s (%s), guest %s (%s)\n", p1.empty() ? "Normal" : p1.c_str(), vf2net::fighter_words(p1, *A.m).c_str(),
	                                    p2.empty() ? "Normal" : p2.c_str(), vf2net::fighter_words(p2, *A.m).c_str());
	std::vector<uint8_t> blob = random ? vf2net::start_blob_random(*A.m, 0, p1, match_seed) : vf2net::start_blob(*A.m);
	if (stage >= 0) blob[4 + A.m->backup_ram().size() + 64 * 2 + 1] = uint8_t(stage % vf2net::STAGES);
	if (blob_in) {
		FILE *f = fopen(blob_in, "rb");
		if (!f) { fprintf(stderr, "vf2net: can't read %s\n", blob_in); return 1; }
		blob.clear();
		for (int ch; (ch = fgetc(f)) != EOF;) blob.push_back(uint8_t(ch));
		fclose(f);
		printf("the start blob from %s (%zu bytes)\n", blob_in, blob.size());
	}
	if (blob_out) {
		FILE *f = fopen(blob_out, "wb");
		if (!f || fwrite(blob.data(), 1, blob.size(), f) != blob.size()) { fprintf(stderr, "vf2net: can't write %s\n", blob_out); return 1; }
		fclose(f);
	}
	{   // (read through the host's board, its own backup put back: -1, no settings yet, the game's factory 2 as it starts)
		std::vector<uint8_t> own = A.m->backup_ram();
		if (blob.size() >= 4 + own.size()) {
			std::copy(blob.begin() + 4, blob.begin() + 4 + long(own.size()), A.m->backup_ram().begin());
			printf("the blob's Rounds to win (vs player): %d\n", A.m->assignment(model2a::game_assignment::match_vs));
			A.m->backup_ram() = own;
		}
	}
	const uint64_t content = vf2net::content_hash(*A.m);
	if (content != vf2net::content_hash(*B.m)) { fprintf(stderr, "vf2net: the two boards' ROM hashes differ\n"); return 1; }

	vfnet::SimWire wire(p, p, 7);
	vfnet::Config cfg;
	cfg.delay = delay;
	cfg.soft_sync = soft;
	cfg.handshake_ms = 300000;   // (both boots run here one after the other, on the one clock)
	const std::string build = random ? std::string("test") + vf2net::RANDOM_TAG : std::string("test");
	const vfnet::Identity ia{ "vf2", build, content, "HOST", p1 }, ib{ "vf2", build, content, "GUEST", p2 };
	A.adapter->checkpoint_every = B.adapter->checkpoint_every = 300;
	std::vector<uint8_t> sound[2];   // (the bytes each side sent its sound board)
	A.adapter->sound_out = [&](const std::vector<uint8_t> &b) { sound[0].insert(sound[0].end(), b.begin(), b.end()); };
	B.adapter->sound_out = [&](const std::vector<uint8_t> &b) { sound[1].insert(sound[1].end(), b.begin(), b.end()); };
	vfnet::Session sa(wire.a(), *A.adapter, ia, cfg, true, blob);
	vfnet::Session sb(wire.b(), *B.adapter, ib, cfg, false);

	// watching: the host's feed (the same whether anyone watches), a stand-in lobby, the watchers' boards
	namespace vw = vfnet::watch;
	vw::Feed feed;
	bool feed_on = false;
	struct Lobby { std::vector<uint8_t> start; std::vector<uint16_t> in; std::map<uint32_t, uint64_t> hashes; vw::Info info; uint64_t match = 0; std::vector<uint8_t> snap, snap_in; } lobby;
	vw::Snapper snapper;   // (--watch-snap: the host's snapshots, as the game makes them)
	struct Due { double at; std::function<void()> fn; };
	std::deque<Due> dues;   // (the uploads and answers on their way, in the order sent: the same delay for all)
	struct Watcher { std::unique_ptr<Board> b; std::unique_ptr<vw::Spectator> sp; long join = 0; bool on = false; std::vector<double> ran_at; double on_at = 0; uint32_t snapped = 0, was_at = 0; std::vector<uint8_t> unheard; };   // (ran_at: when it ran each frame)
	std::map<const vw::Spectator *, std::map<uint32_t, double>> hash_got;   // (when each of the players' hashes reached each watcher)
	std::vector<Watcher> ws;
	ws.reserve(size_t(watchers));   // (each watcher's own callbacks keep its place)
	for (int i = 0; i < watchers; i++) {
		Watcher w;
		w.b = std::make_unique<Board>();
		if (!w.b->open(load(), interp, error)) { fprintf(stderr, "vf2net: a watcher's board: %s\n", error.c_str()); return 1; }
		w.b->adapter->random_fighters = random;   // (the match's terms, as the game's watcher has them from the start's)
		w.b->adapter->checkpoint_every = 300;
		w.sp = std::make_unique<vw::Spectator>(*w.b->adapter, vw::Spectator::Config{});
		w.join = i == 0 ? 0 : watch_late;
		ws.push_back(std::move(w));
		Watcher &wb = ws.back();
		wb.b->adapter->sound_unheard = [&wb](const std::vector<uint8_t> &b) { wb.unheard.insert(wb.unheard.end(), b.begin(), b.end()); };
		wb.sp->on_catch_up = [&wb](bool on) { wb.b->adapter->mute = on; };
	}
	auto serve = [&](Watcher &w, const std::string &want) -> std::function<void(double)> {
		vw::Spectator &sp = *w.sp;
		if (want == "start") {   // (as the game's watcher takes it: the guest's fighter split off the blob, played as the guest's)
			std::vector<uint8_t> b = lobby.start;
			return [&w, b](double) {
				vw::Start st;
				std::string guest;
				std::vector<uint8_t> session_blob;
				if (!vw::decode(b, st) || !vf2net::watch_blob_split(st.blob, session_blob, guest)) { printf("a watcher: the start didn't decode\n"); return; }
				st.blob = session_blob;
				w.b->adapter->hosting = false;
				w.b->adapter->own_extra = w.b->adapter->random_fighters ? guest : std::string();
				if (!w.sp->start(st)) printf("a watcher: the start didn't load\n");
				w.b->adapter->sound_starts = true;
			};
		}
		vw::Reply r;
		r.match = lobby.match;
		r.state = 1;
		const uint32_t total = uint32_t(lobby.in.size() / 2), from = std::min(sp.ask_from() / 256 * 256, total), n = std::min<uint32_t>(total - from, 2048);
		r.total = total;
		r.frames.first = from;
		r.frames.in.assign(lobby.in.begin() + ptrdiff_t(from) * 2, lobby.in.begin() + ptrdiff_t(from + n) * 2);
		for (auto &[f, h] : lobby.hashes) if (f + 120 >= from && f <= from + n) r.frames.hashes.push_back({ f, h });   // (and those made late, as the lobby's)
		r.frames.info = lobby.info;
		return [&sp, r, &hash_got](double at) { if (sp.reply(r, at)) for (auto &[f, h] : r.frames.hashes) hash_got[&sp].emplace(f, at); };
	};
	// (a pass of the stand-in lobby's: what's due delivered, each watcher asking and running; true: all to its end)
	auto watchers_pass = [&](double now) {
		while (!dues.empty() && dues.front().at <= now) { auto fn = dues.front().fn; dues.pop_front(); fn(); }
		bool all = true;
		for (Watcher &w : ws) {
			if (!w.on && long(sa.frame()) >= w.join && !lobby.start.empty()) { w.on = true; w.on_at = now; }
			if (!w.on) { all = false; continue; }
			const bool snap_wait = watch_snap && w.join && !w.snapped && now - w.on_at < 20000;   // (the frames from the snapshot's, as the game holds them)
			if (snap_wait && !lobby.snap.empty() && w.sp->state() != vw::Spectator::State::waiting) {
				vw::Snap sn;
				w.was_at = w.sp->frame();
				if (vw::decode_snap(lobby.snap, sn) && w.sp->snapshot(sn)) w.snapped = sn.frame;
				else printf("watcher: the snapshot wasn't taken: %s\n", w.sp->why.c_str());
			}
			const std::string want = w.sp->want(now);
			if (!want.empty() && !(want == "frames" && snap_wait)) {
				w.sp->asked(now);
				dues.push_back({ now + watch_ms, [&, wp = &w, want, at = now + watch_ms] {
					auto answer = serve(*wp, want);
					dues.push_back({ at + watch_ms, [sp = wp->sp.get(), answer, done = at + watch_ms] { answer(done); sp->answered(done); } });
				} });
			}
			w.sp->run(now);
			while (w.ran_at.size() < w.sp->frame()) w.ran_at.push_back(now);
			all = all && w.sp->frame() >= lobby.in.size() / 2;
		}
		return all;
	};
	Timing ta, tb;
	size_t na = 0, nb = 0;   // each side's frames run (its next frame's inputs)
	double t = 1000, next_a = 1000, next_b = 1000 + 23;
	const double fa = cfg.frame_ms, fb = cfg.frame_ms * 1.002;
	const auto t0 = Clock::now();
	bool connected = false;
	vf2net::Score score_a, score_b, score_c;   // (the detector on each side, as the game runs it: after each step, guesses and all)
	std::map<uint32_t, vf2net::Score> score_a_at, score_b_at;   // (each side's after each frame it ran: compared at the frames confirmed, not past them)
	while (sa.state() != vfnet::Session::State::ended && sb.state() != vfnet::Session::State::ended &&
	       (long(sa.frame()) < frames || long(sb.frame()) < frames)) {
		wire.now = t;
		sa.poll(t);
		sb.poll(t);
		if (!connected && sa.state() == vfnet::Session::State::running && sb.state() == vfnet::Session::State::running) {
			connected = true;
			printf("connected (both boots run) after %.1f s\n", std::chrono::duration<double>(Clock::now() - t0).count());
			if (vs_rounds) printf("the guest's board plays Rounds to win (vs player): %d\n", B.m->assignment(model2a::game_assignment::match_vs));
		}
		if (t >= next_a) {
			const auto s0 = Clock::now();
			snapper.before_step(sa);
			if (sa.step(word(real[std::min(na, real.size() - 1)], 0))) { na++; score_a.frame(*A.m); score_a_at[sa.frame()] = score_a; }
			if (!feed_on && sa.state() == vfnet::Session::State::running) {   // (the host's match to the lobby, as the game sends it)
				vw::Start st;
				st.match = 0x5eed;
				st.meta = { "vf2", build, vw::hex(content), build, { "HOST", "GUEST" }, { "", "" } };
				st.blob = vf2net::watch_blob(blob, sa.opponent_extra());
				feed.begin(st, t);
				feed_on = true;
				A.adapter->sound_log = watch_snap;
			}
			feed.take(sa);
			if (watch_snap) {
				for (const Watcher &w : ws)   // (a late watcher about to come: asked for, as the lobby asks)
					if (w.join && long(sa.frame()) == w.join - 240) snapper.want(t);
				snapper.step(sa, *A.adapter, feed.match(), t);
			}
			vw::Info info;
			for (int q = 0; q < 2; q++) { info.wins[q] = uint8_t(score_a.matches[q]); info.rounds[q] = uint8_t(score_a.rounds[q]); }
			feed.set_info(info);
			if (connected) ta.add(std::chrono::duration<double, std::milli>(Clock::now() - s0).count());
			next_a += fa / (1 - (soft ? std::clamp(sa.stats().lead * 0.02, 0.0, 0.04) : 0.0));
		}
		if (t >= next_b) {
			const auto s0 = Clock::now();
			if (sb.step(word(real[std::min(nb, real.size() - 1)], 1))) { nb++; score_b.frame(*B.m); score_b_at[sb.frame()] = score_b; }
			if (connected) tb.add(std::chrono::duration<double, std::milli>(Clock::now() - s0).count());
			next_b += fb / (1 - (soft ? std::clamp(sb.stats().lead * 0.02, 0.0, 0.04) : 0.0));
		}
		// the host's upload, the lobby's keeping it, the watchers asking and running theirs
		vw::Feed::Upload up;
		if (feed.next(t, up))
			dues.push_back({ t + watch_ms, [&, up, at = t + watch_ms] {
				uint32_t has = 0;
				if (up.start) { vw::Start st; if (vw::decode(up.body, st)) { lobby.start = up.body; lobby.match = st.match; } }
				else {
					vw::Frames f;
					if (vw::decode_push(up.body, f)) {
						const uint32_t had = uint32_t(lobby.in.size() / 2);
						if (f.first <= had && f.first + f.count() > had) lobby.in.insert(lobby.in.end(), f.in.begin() + ptrdiff_t(had - f.first) * 2, f.in.end());
						for (auto &[fr, h] : f.hashes) lobby.hashes[fr] = h;
						lobby.info = f.info;
					}
					has = uint32_t(lobby.in.size() / 2);
				}
				dues.push_back({ at + watch_ms, [&feed, &snapper, up, has, done = at + watch_ms] { feed.done(up, true, has, int(0), done); snapper.heard(2 * 60.0, done); } });   // (the lobby's round trip heard, as the game hears it)
			} });
		vw::Snapper::Part part;
		if (snapper.next(t, part))
			dues.push_back({ t + watch_ms, [&, part, at = t + watch_ms] {
				if (part.index == 0) lobby.snap_in.clear();
				if (part.match == lobby.match && size_t(part.index) * snapper.part_bytes == lobby.snap_in.size()) lobby.snap_in.insert(lobby.snap_in.end(), part.body.begin(), part.body.end());
				if (part.index + 1 == part.count) lobby.snap = lobby.snap_in;
				dues.push_back({ at + watch_ms, [&snapper, part, done = at + watch_ms] { snapper.done(part, true, 2 * 60.0, done); } });
			} });
		watchers_pass(t);
		t += 1;
	}
	// (the watchers to the end of what the lobby has: the players are done)
	for (double end = t + 60000; t < end && !ws.empty(); t += 1)
		if (watchers_pass(t)) break;
	vfnet::Session::Event e;
	bool failed = false;
	while (sa.next_event(e)) if (e.kind == e.ended) { printf("host: %s\n", e.text.c_str()); failed = true; }
	while (sb.next_event(e)) if (e.kind == e.ended) { printf("guest: %s\n", e.text.c_str()); failed = true; }

	// the same inputs straight through, on a third board, against both players' checkpoints
	const uint32_t n = std::min(sa.confirmed(), sb.confirmed());
	// (the players' scores as they were at frame n: a round ending in a frame past it, a guess run on the live board
	// that the straight run never reaches, counted only when it's confirmed)
	for (auto *h : { &score_a_at, &score_b_at }) {
		const auto it = h->upper_bound(n);
		(h == &score_a_at ? score_a : score_b) = it == h->begin() ? decltype(score_a){} : std::prev(it)->second;
	}
	uint32_t checked = 0;
	std::vector<uint8_t> straight_sound;
	if (!failed) {
		auto pc = std::make_unique<Board>();
		Board &C = *pc;
		if (!C.open(load(), interp, error) || (C.adapter->random_fighters = random, C.adapter->own_extra = p2, !C.adapter->begin(blob))) { fprintf(stderr, "vf2net: the straight board didn't start\n"); return 1; }
		C.adapter->checkpoint_every = hashes_path ? 30 : 300;
		C.adapter->sound_out = [&](const std::vector<uint8_t> &b) { straight_sound.insert(straight_sound.end(), b.begin(), b.end()); };
		int won[2] = {}, by[2] = {};   // (each versus match as it ends: its rounds)
		std::vector<size_t> sound_at;   // (where each frame's bytes start in straight_sound)
		for (uint32_t f = 0; f < n; f++) {
			const uint16_t in[2] = { sa.inputs(0)[f], sa.inputs(1)[f] };
			if (in[0] != sb.inputs(0)[f] || in[1] != sb.inputs(1)[f]) { printf("the two sides' inputs differ at frame %u\n", f); failed = true; break; }
			sound_at.push_back(straight_sound.size());
			C.adapter->advance(in, false);
			score_c.frame(*C.m);
			if (score_c.matches[0] + score_c.matches[1] != won[0] + won[1]) {
				printf("a versus match over at frame %u: player %d won it, rounds %d-%d\n", f, score_c.matches[0] != won[0] ? 1 : 2,
				       score_c.rounds[0] - by[0], score_c.rounds[1] - by[1]);
				for (int q = 0; q < 2; q++) won[q] = score_c.matches[q], by[q] = score_c.rounds[q];
			}
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
		for (size_t i = 0; i < ws.size(); i++) {   // (each watcher: the straight run's board at every checkpoint it ran past, the players' hashes matched)
			Watcher &w = ws[i];
			uint32_t same = 0;
			for (auto &[f, h] : w.b->adapter->checkpoints) {
				if (f > n || f % 300) continue;
				if (C.adapter->checkpoints[f] != h) { printf("watcher %zu's board differs from a straight run after frame %u\n", i + 1, f); failed = true; break; }
				same++;
			}
			const bool whole = w.sp->frame() + 60 >= n;
			// (one come late ran the match from its start: every one of the players' hashes in it checked, none skipped; and
			// every watcher, at the live edge, every one that reached it before it ran the hash's frame: one made a few
			// frames after its frame and come just after the watcher ran that frame can't be checked)
			uint32_t there = 0;
			const std::map<uint32_t, double> &got = hash_got[w.sp.get()];
			for (auto &[f, h] : lobby.hashes) {
				const auto at = got.find(f);
				there += f >= w.snapped && f + vw::HASH_PARTS < w.sp->frame() && at != got.end() && f < w.ran_at.size() && at->second <= w.ran_at[f];
			}
			if (w.snapped) {   // (its sound board given the host's frames' bytes before the snapshot first: the straight run's, frame for frame)
				vw::Snap sn;
				uint32_t tf = 0, tn = 0;
				std::vector<uint8_t> tb;
				const bool tail = vw::decode_snap(lobby.snap, sn) && tail_bytes(sn.extra, tf, tn, tb) && tf + tn == w.snapped && tf < sound_at.size() && w.snapped < sound_at.size();
				const bool same = tail && tb.size() == sound_at[w.snapped] - sound_at[tf] && std::equal(tb.begin(), tb.end(), straight_sound.begin() + ptrdiff_t(sound_at[tf])) &&
				                  w.unheard.size() >= tb.size() && std::equal(tb.begin(), tb.end(), w.unheard.begin());
				printf("watcher %zu came in from the host's snapshot at frame %u (it had run %u from the start): %u frames run after it; its sound board given the %u frames before it first (%zu bytes)%s\n",
				       i + 1, w.snapped, w.was_at, w.sp->frame() - w.snapped, tn, tb.size(), same ? ", the straight run's" : ": NOT the straight run's");
				if (!same) failed = true;
			} else if (watch_snap && w.join) { printf("watcher %zu: no snapshot taken\n", i + 1); failed = true; }
			const bool all = w.sp->hashes_checked >= there;
			printf("watcher %zu (from frame %ld): ran to frame %u (%llu frames run, %llu unseen, catching up), the straight run's board at %u checkpoints, %u of the players' hashes matched%s%s\n",
			       i + 1, w.join, w.sp->frame(), (unsigned long long)w.sp->frames_run, (unsigned long long)w.sp->frames_unseen, same, w.sp->hashes_checked,
			       w.join ? (" (all " + std::to_string(there) + " that reached it before it ran their frames: from the match's start)").c_str() : "", whole ? "" : " (NOT to the end)");
			if (!whole || !same || !w.sp->hashes_checked || !all || w.sp->state() == vw::Spectator::State::out_of_step) failed = true;
			if (w.b->adapter->extras[0] != (random ? p1 : "") || w.b->adapter->extras[1] != (random ? p2 : "")) {
				printf("watcher %zu's fighters %s/%s, not the players' %s/%s\n", i + 1, w.b->adapter->extras[0].c_str(), w.b->adapter->extras[1].c_str(), p1.c_str(), p2.c_str());
				failed = true;
			}
		}
		if (!ws.empty()) {
			printf("the host's uploads: %llu, %llu bytes\n", (unsigned long long)feed.uploads, (unsigned long long)feed.bytes);
		}
		if (watch_snap)
			printf("the host's snapshots: %u made, %u sent; the last at frame %u: %.2f MB, %.2f MB packed, copied in %.0f ms and packed in %.0f ms on its own thread; the steps held for it %.2f ms in all\n",
			       snapper.made, snapper.sent, snapper.last_frame, double(snapper.last_raw) / 1e6, double(snapper.last_packed) / 1e6, snapper.last_copy_ms, snapper.last_pack_ms, snapper.stall_ms);
		if (snap_out) {
			std::filesystem::create_directories(snap_out);
			std::vector<uint8_t> ib(size_t(n) * 4);
			for (uint32_t f = 0; f < n; f++) {
				const uint16_t a = sa.inputs(0)[f], b = sa.inputs(1)[f];
				memcpy(ib.data() + size_t(f) * 4, &a, 2);
				memcpy(ib.data() + size_t(f) * 4 + 2, &b, 2);
			}
			if (lobby.snap.empty() || !write_file(std::string(snap_out) + "/snap.bin", lobby.snap) || !write_file(std::string(snap_out) + "/blob.bin", blob) || !write_file(std::string(snap_out) + "/inputs.bin", ib)) {
				printf("--snap-out: nothing written (no snapshot)\n");
				failed = true;
			}
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
