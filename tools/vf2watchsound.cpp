// vf2watchsound: a watcher's match sound after catching up, against one's who watched from the start. A match (two
// random players, the game's board through vf2net::Adapter, as a watcher's runs it) gives each frame's bytes for the
// sound board; then main.cpp's match sound board (set_sound_only, the boot's bytes first) is given them: from the
// start, every frame heard; late, the frames before each join point (--step apart) through the UnheardQueue as
// main.cpp's sound thread runs it, at a pace against the catch-up (ratio: board frames a frame caught up; burst: the
// thread getting a pass's frames at once), with v0.1.20's bound (UNHEARD_TIMED, frames let go when it's behind) and
// with the hotfix's (the catch-up waits for it: none let go), the rest heard. For the --window frames after the join
// point: how much of the output is the from-start board's sample for sample, its loudness and key-ons against it.
//   vf2watchsound <vf2.zip> [--frames N] [--step F] [--window F] [--random SEED]
//   vf2watchsound <vf2.zip> --real [--frames N] [--late F] [--after S] [--slow-us U] [--fixed]
#include "../src/host/vf2_netplay.h"
#include "../src/machine/model2a.h"
#include "../src/net/watch.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include "random_players.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

bool write_wav(const std::string &path, const std::vector<int16_t> &v, int rate)
{
	FILE *f = fopen(path.c_str(), "wb");
	if (!f) return false;
	const uint32_t data = uint32_t(v.size() * 2), riff = 36 + data, fmt = 16, byte_rate = uint32_t(rate) * 4;
	const uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
	const uint32_t r = uint32_t(rate);
	fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
	fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&r, 4, 1, f); fwrite(&byte_rate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
	fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(v.data(), 2, v.size(), f);
	return fclose(f) == 0;
}

double rms(const std::vector<int16_t> &v, size_t from, size_t n)
{
	double s = 0;
	for (size_t i = from; i < from + n && i < v.size(); i++) s += double(v[i]) * double(v[i]);
	return std::sqrt(s / double(std::max<size_t>(n, 1)));
}

uint16_t word(const players::Ports &p, int player) { return vf2net::pack(uint8_t(~p.in0), uint8_t(~(player ? p.in2 : p.in1)), player); }

std::unique_ptr<model2a> sound_board(const std::string &rom, const std::vector<std::vector<uint8_t>> &boot)
{
	auto p = std::make_unique<model2a>(romset::load(rom), false);
	model2a &b = *p;
	b.set_draw_3d(false);
	b.set_sound(true);
	b.set_sound_only(true);
	b.set_byte_link(true);
	b.reset();
	for (const auto &f : boot) {   // (as main.cpp's match_sound_start)
		b.sound_bytes(f.data(), f.size());
		b.run_frame();
		b.m_audio_out.clear();
	}
	return p;
}


// --real: a watcher run as the game runs one (Online::step's ticks, the lobby's frames growing at the players' pace), its
// match sound as main.cpp's (match_sound_t and its thread, word for word but SDL's audio stream: a stand-in drained at
// the real clock), the sound thread slowed by --slow-us a frame run unheard (a machine whose sound board is slow
// against its main board); then --after seconds watched. Key-ons a second after the first picture, against a board
// given every frame's bytes heard (from the start).
struct FakeAudio
{
	std::mutex mx;
	double pushed = 0;
	std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
	bool started = false;
	std::vector<int16_t> out;
	int queued()
	{
		std::lock_guard<std::mutex> lk(mx);
		if (!started) return 0;
		const double drained = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 44100 * 4;
		return int(std::max(0.0, pushed - drained));
	}
	void put(const int16_t *d, size_t n)
	{
		std::lock_guard<std::mutex> lk(mx);
		const double drained = started ? std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 44100 * 4 : 0;
		if (!started || pushed < drained) { t0 = std::chrono::steady_clock::now(); pushed = 0; started = true; }   // (ran dry: from now)
		pushed += double(n * 2);
		out.insert(out.end(), d, d + n);
	}
};

int run_real(const std::string &rom, long frames, long late, uint32_t seed, double after_s, int slow_us, bool fixed)
{
	namespace vw = vfnet::watch;
	using Clock = std::chrono::steady_clock;
	auto pg = std::make_unique<model2a>(romset::load(rom));
	model2a &g = *pg;
	g.set_draw_3d(false);
	vf2net::Adapter ad(g);
	const std::vector<uint8_t> blob = vf2net::start_blob(g);
	players::Random rnd(seed);
	std::vector<uint16_t> in;
	for (long f = 0; f < frames + 100000; f++) { const players::Ports p = rnd.next(uint64_t(f)); in.push_back(word(p, 0)); in.push_back(word(p, 1)); }
	// every frame's bytes, as the watcher's adapter gives them (heard or not)
	std::vector<std::vector<uint8_t>> fr;
	FakeAudio audio;
	const int frame_bytes = int(model2a::SOUND_RATE / 57.524160) * 4;
	constexpr double QUEUED = 3.5;
	std::atomic<uint32_t> g_vol{ 0x646464 };
	// ---- main.cpp's match_sound_t, word for word ----
	struct match_sound_t {
		std::unique_ptr<model2a> board;
		std::thread th;
		std::mutex mx;
		std::vector<uint8_t> in;
		std::atomic<bool> quit{ false };
		std::atomic<uint64_t> frames{ 0 }, bytes_in{ 0 };
		vf2net::UnheardQueue unheard;
		size_t unheard_at = 0;
		std::atomic<bool> unheard_on{ false };
		std::atomic<bool> held{ false };
		void put(const std::vector<uint8_t> &b)
		{
			std::lock_guard<std::mutex> lk(mx);
			if (unheard_on) unheard.heard(b);
			else in.insert(in.end(), b.begin(), b.end());
			bytes_in += b.size();
		}
		void put_unheard(const std::vector<uint8_t> &b)
		{
			if (!th.joinable()) return;
			std::lock_guard<std::mutex> lk(mx);
			if (!unheard_on) unheard_at = in.size();
			unheard.frame(b);
			unheard_on = true;
		}
		size_t unheard_waiting()
		{
			if (!th.joinable()) return 0;
			std::lock_guard<std::mutex> lk(mx);
			return unheard.frames();
		}
		void stop()
		{
			if (th.joinable()) {
				quit = true;
				th.join();
				printf("the match's sound board played %llu frames, %llu bytes from the game\n", (unsigned long long)frames.load(), (unsigned long long)bytes_in.load());
			}
			quit = false;
			frames = bytes_in = 0;
		}
	} msound;
	if (fixed) msound.unheard.most_timed = vf2net::UNHEARD_CATCH_UP;
	std::atomic<long> keyons_heard{ 0 };
	std::vector<int> kb;   // (key-ons a second, after the first picture)
	std::atomic<bool> shown_yet{ false };
	Clock::time_point shown_at;
	auto match_sound_start = [&] {
		msound.stop();
		if (!msound.board) {
			msound.board = std::make_unique<model2a>(romset::load(rom), false);
			msound.board->set_draw_3d(false);
		}
		model2a *bp = msound.board.get();
		model2a &b = *bp;
		b.set_sound(true);
		b.set_sound_only(true);
		b.set_byte_link(true);
		b.reset();
		b.scsp().on_key_on = [&](const scsp_device::key_on &) {
			if (!shown_yet) return;
			const size_t sec = size_t(std::chrono::duration<double>(Clock::now() - shown_at).count());
			if (kb.size() <= sec) kb.resize(sec + 1);
			kb[sec]++;
		};
		{ std::lock_guard<std::mutex> lk(msound.mx); msound.in.clear(); msound.unheard.clear(); msound.unheard_on = false; }
		const int target = int(frame_bytes * QUEUED);
		msound.th = std::thread([&, target, bp, boot = ad.boot_sound] {
			model2a &b = *bp;
			std::vector<uint8_t> bytes;
			for (const std::vector<uint8_t> &f : boot) {
				if (msound.quit) break;
				b.sound_bytes(f.data(), f.size());
				b.run_frame();
				b.m_audio_out.clear();
			}
			while (!msound.quit) {
				if (msound.unheard_on) {
					vf2net::UnheardQueue::Piece piece;
					{
						std::lock_guard<std::mutex> lk(msound.mx);
						const size_t at = std::min(msound.unheard_at, msound.in.size());
						b.sound_bytes(msound.in.data(), at);
						msound.in.erase(msound.in.begin(), msound.in.begin() + ptrdiff_t(at));
						msound.unheard_at = 0;
						if (!msound.unheard.next(piece)) { msound.unheard_on = false; continue; }
					}
					vf2net::run_unheard(b, piece);
					if (slow_us && piece.kind == piece.frame) std::this_thread::sleep_for(std::chrono::microseconds(slow_us));
					continue;
				}
				if (msound.held) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
				if (audio.queued() >= target) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
				{ std::lock_guard<std::mutex> lk(msound.mx); bytes.swap(msound.in); }
				b.sound_bytes(bytes.data(), bytes.size());
				bytes.clear();
				b.run_frame();
				msound.frames++;
				if (!b.m_audio_out.empty()) audio.put(b.m_audio_out.data(), b.m_audio_out.size());
				b.m_audio_out.clear();
			}
		});
	};
	// ---- the watcher: Online's ----
	ad.sound_out = [&](const std::vector<uint8_t> &b) { fr.back().insert(fr.back().end(), b.begin(), b.end()); msound.put(b); };
	ad.sound_unheard = [&](const std::vector<uint8_t> &b) { fr.back().insert(fr.back().end(), b.begin(), b.end()); msound.put_unheard(b); };
	ad.run = [&](bool) { fr.emplace_back(); g.run_frame(); };
	bool watch_sound_started = false;
	ad.sound_begin = [&] { match_sound_start(); watch_sound_started = true; };
	vw::Spectator sp(ad, vw::Spectator::Config{});
	sp.on_catch_up = [&](bool on) { ad.mute = on; };
	vw::Start st;
	st.match = 1;
	st.meta.game = "vf2";
	st.blob = blob;
	ad.mute = false;
	if (!sp.start(st)) { fprintf(stderr, "the start didn't start\n"); return 1; }
	ad.sound_starts = true;
	const double frame_ms = 1000.0 / 57.524160;
	const auto t0 = Clock::now();
	bool watch_shown = false;
	uint32_t shown_frame = 0;
	double frame_clock = 0;
	for (;;) {
		const auto p0 = Clock::now();
		const double now = std::chrono::duration<double, std::milli>(p0 - t0).count();
		const uint32_t live = uint32_t(late) + uint32_t(now / frame_ms);
		if (sp.ask_from() < live) {
			vw::Reply rp;
			rp.match = 1;
			rp.state = 1;
			rp.total = live;
			rp.frames.first = sp.ask_from();
			const uint32_t n = std::min<uint32_t>(live - rp.frames.first, vw::MAX_REPLY);
			rp.frames.in.assign(in.begin() + ptrdiff_t(rp.frames.first) * 2, in.begin() + ptrdiff_t(rp.frames.first + n) * 2);
			sp.reply(rp, now);
		}
		sp.time(now);
		msound.held = !watch_shown || sp.state() == vw::Spectator::State::catching_up;   // (Online::watch_hushed, online_pass's)
		// main.cpp's pass: the frames due (one tick for each, as Online::step's), the first not shown ending it
		int due = 1;
		if (watch_shown) { due = int(now / frame_ms - frame_clock); frame_clock += due; }
		for (int i = 0; i < due; i++) {
			const bool catching = !watch_shown || sp.state() == vw::Spectator::State::catching_up;
			bool shown = false;
			if (fixed && catching) {   // (Online::step's)
				const size_t ahead = sp.total() > sp.frame() + 600 ? vf2net::UNHEARD_AHEAD : 0;   // (near the live point: all of them run first)
				const auto until = Clock::now() + std::chrono::milliseconds(30);
				while (msound.unheard_waiting() > ahead && Clock::now() < until) std::this_thread::sleep_for(std::chrono::microseconds(500));
				if (msound.unheard_waiting() > ahead) break;
			}
			sp.tick(&shown, catching ? 40.0 : 10.0);
			if (shown && !watch_shown) {
				watch_shown = true;
				shown_frame = sp.frame();
				shown_at = Clock::now();
				shown_yet = true;
				frame_clock = now / frame_ms;
				printf("the first picture at frame %u, %.1f s after coming (%u live)\n", shown_frame, now / 1000, live);
				if (!watch_sound_started) match_sound_start();
				watch_sound_started = false;
			}
			if (!shown) break;
		}
		if (watch_shown && std::chrono::duration<double>(Clock::now() - shown_at).count() > after_s) break;
		if (now > 600000) { printf("never caught up\n"); return 1; }
		const double pass = std::chrono::duration<double, std::milli>(Clock::now() - p0).count();
		if (pass < frame_ms) std::this_thread::sleep_for(std::chrono::microseconds(int64_t((frame_ms - pass) * 1000)));
	}
	const uint32_t end = sp.frame();
	msound.stop();
	printf("frames %u to %u watched; %llu unseen; the queue let go of %llu frames\n", shown_frame, end, (unsigned long long)sp.frames_unseen, (unsigned long long)msound.unheard.dropped);
	// from the start, every frame heard (a board frame each)
	auto pa = sound_board(rom, ad.boot_sound);
	std::vector<int> ka;
	long kf = 0;
	pa->scsp().on_key_on = [&](const scsp_device::key_on &) { if (kf >= long(shown_frame)) { size_t s = size_t((kf - long(shown_frame)) / 57.524160); if (ka.size() <= s) ka.resize(s + 1); ka[s]++; } };
	size_t bytes_after = 0;
	for (size_t f = 0; f < fr.size(); f++) {
		kf = long(f);
		pa->sound_bytes(fr[f].data(), fr[f].size());
		pa->run_frame();
		pa->m_audio_out.clear();
		if (f >= shown_frame) bytes_after += fr[f].size();
	}
	long ta = 0, tb = 0;
	const size_t secs = std::min(ka.size(), kb.size());
	for (size_t s = 0; s < secs; s++) { printf("second %2zu after the first picture: key-ons from the start %3d, this watcher %3d\n", s, ka[s], kb[s]); ta += ka[s]; tb += kb[s]; }
	printf("key-ons after the first picture (%zu s): from the start %ld, this watcher %ld; %zu bytes for the sound board after it\n", secs, ta, tb, bytes_after);
	FILE *w = getenv("WAV") ? fopen(getenv("WAV"), "wb") : nullptr;
	if (w) { fwrite(audio.out.data(), 2, audio.out.size(), w); fclose(w); }
	return 0;
}

} // namespace

int main(int argc, char **argv)
{
	std::string rom;
	long frames = 20000, step = 4000, window = 1200, late = 9000;
	uint32_t seed = 1;
	double after_s = 20;
	bool real = false, fixed = false;
	int slow_us = 0;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--frames" && i + 1 < argc) frames = strtol(argv[++i], nullptr, 10);
		else if (a == "--step" && i + 1 < argc) step = strtol(argv[++i], nullptr, 10);
		else if (a == "--window" && i + 1 < argc) window = strtol(argv[++i], nullptr, 10);
		else if (a == "--late" && i + 1 < argc) late = strtol(argv[++i], nullptr, 10);
		else if (a == "--random" && i + 1 < argc) seed = uint32_t(strtoul(argv[++i], nullptr, 10));
		else if (a == "--real") real = true;
		else if (a == "--fixed") fixed = true;
		else if (a == "--after" && i + 1 < argc) after_s = atof(argv[++i]);
		else if (a == "--slow-us" && i + 1 < argc) slow_us = atoi(argv[++i]);
		else if (a[0] != '-' && rom.empty()) rom = a;
		else { fprintf(stderr, "usage: vf2watchsound <vf2.zip> [--frames N] [--step F] [--window F] [--random SEED] | --real [--late F] [--after S] [--slow-us U] [--fixed]\n"); return 2; }
	}
	if (real) return run_real(rom, frames, std::min(late, frames), seed, after_s, slow_us, fixed);
	// the match: the game's board as a watcher's (no sound of its own), each frame's bytes for the sound board
	auto pg = std::make_unique<model2a>(romset::load(rom));
	model2a &g = *pg;
	g.set_draw_3d(false);
	vf2net::Adapter ad(g);
	if (!ad.begin(vf2net::start_blob(g))) { fprintf(stderr, "vf2watchsound: the match didn't start\n"); return 1; }
	std::vector<std::vector<uint8_t>> fr;
	ad.sound_out = [&](const std::vector<uint8_t> &b) { fr.back() = b; };
	players::Random rnd(seed);
	size_t bytes = 0;
	for (long f = 0; f < frames; f++) {
		const players::Ports p = rnd.next(uint64_t(f));
		const uint16_t in[2] = { word(p, 0), word(p, 1) };
		fr.emplace_back();
		ad.advance(in, false);
		bytes += fr.back().size();
	}
	printf("the match: %ld frames, %zu bytes for the sound board\n", frames, bytes);
	// from the start: every frame heard, its output and key-ons a frame
	std::vector<std::vector<int16_t>> ref;
	std::vector<int> ref_keys(size_t(frames), 0);
	{
		auto pa = sound_board(rom, ad.boot_sound);
		long kf = 0;
		pa->scsp().on_key_on = [&](const scsp_device::key_on &) { ref_keys[size_t(kf)]++; };
		for (long f = 0; f < frames; f++) {
			kf = f;
			pa->sound_bytes(fr[size_t(f)].data(), fr[size_t(f)].size());
			pa->run_frame();
			ref.push_back(pa->m_audio_out);
			pa->m_audio_out.clear();
		}
	}
	int worst[2] = { 100, 100 };
	for (long L = step; L + window <= frames; L += step) {
		for (int bound = 0; bound < 2; bound++)
			for (int pat = 0; pat < 5; pat++) {
				const long bursts[5] = { 0, 0, 0, 300, 600 };
				const double ratios[5] = { 1, 0.5, 0.1, 1, 1 };
				auto bd = sound_board(rom, ad.boot_sound);
				long kf = 0, keys = 0, want = 0;
				bd->scsp().on_key_on = [&](const scsp_device::key_on &) { if (kf >= L) keys++; };
				vf2net::UnheardQueue q;
				q.most_timed = bound ? vf2net::UNHEARD_CATCH_UP : vf2net::UNHEARD_TIMED;
				vf2net::UnheardQueue::Piece piece;
				double credit = 0;
				for (long f = 0; f < L; f++) {
					// (the hotfix: the catch-up waits while the board has more than UNHEARD_AHEAD to run, all near the end)
					if (bound) {
						const size_t ahead = L > f + 600 ? vf2net::UNHEARD_AHEAD : 0;
						while (q.frames() > ahead && q.next(piece)) vf2net::run_unheard(*bd, piece);
					}
					q.frame(fr[size_t(f)]);
					credit += ratios[pat];
					if (bursts[pat] && (f + 1) % bursts[pat] != 0) continue;
					if (bursts[pat]) credit = 1e9;
					while (credit >= 1 && q.next(piece)) { vf2net::run_unheard(*bd, piece); if (piece.kind == piece.frame) credit -= 1; }
				}
				while (q.next(piece)) vf2net::run_unheard(*bd, piece);
				size_t same = 0, n = 0;
				double sa = 0, sb = 0;
				for (long f = L; f < L + window; f++) {
					kf = f;
					want += ref_keys[size_t(f)];
					bd->sound_bytes(fr[size_t(f)].data(), fr[size_t(f)].size());
					bd->run_frame();
					const std::vector<int16_t> &r = ref[size_t(f)], &o = bd->m_audio_out;
					for (size_t i = 0; i < std::min(r.size(), o.size()); i++) { same += r[i] == o[i]; sa += double(r[i]) * r[i]; sb += double(o[i]) * o[i]; }
					n += r.size();
					bd->m_audio_out.clear();
				}
				const int pct = n ? int(100.0 * double(same) / double(n)) : 100;
				worst[bound] = std::min(worst[bound], pct);
				printf("join at %5ld, %s, the sound thread at ratio %.2f burst %3ld: %5llu frames let go; after it %3d%% of the samples the from-start board's, loudness %.0f against %.0f, key-ons %ld against %ld\n", L,
				       bound ? "the hotfix" : "v0.1.20", ratios[pat], bursts[pat], (unsigned long long)q.dropped, pct, std::sqrt(sb / double(std::max<size_t>(n, 1))), std::sqrt(sa / double(std::max<size_t>(n, 1))), keys, want);
				fflush(stdout);
			}
	}
	printf("the worst: v0.1.20 %d%%, the hotfix %d%% of the samples the from-start board's\n", worst[0], worst[1]);
	return worst[1] == 100 ? 0 : 1;
}
