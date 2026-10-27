// Headless runner: boots VF2 (its i960 program recompiled, or --interp on MAME's interpreter) on the
// Model 2A board, logs milestones,
// writes chosen frames as PNGs (as MAME draws them: tile layers and polygons), plays input
// scripts, and can trace instructions or dump memory.
//   vf2run <vf2.zip> <frames> [options]
//     --png a,b,c        frames to write (build/frames/fNNNNN.png); "all" for every frame
//     --out dir          where the PNGs and dumps go (default build/frames)
//     --trace file       instruction trace ("cycle pc" per line, cycles as MAME's totalcycles) ...
//     --trace-frames a-b ... for these frames only (default: all)
//     --tgp-trace file   the same for the TGP (its cycle count and pc)
//     --68k-trace file   the same for the sound board's 68000 (implies --sound)
//     --music N, --effects N   the host's volumes (0-100, default 100: the sound is MAME's)
//     --level G          the host's overall gain with its limiter (default 1: none, MAME's)
//     --scsp-log file    the sound board at work (implies --sound): the bytes the main board
//                        sends it, each SCSP key-on with its slot's settings, and every 1/4 s
//                        each slot's energy straight out (direct) and into the DSP, and the DSP's
//                        output energy
//     --voice-log file   each voice the sound board starts (implies --sound): when, its slot, the
//                        sound program's channel (its table's entry; -1 none, -2 not started by the
//                        program's voice start) and note, its group (0 music, 1 effects), sample
//                        address, TL, DISDL, IMXL, ISEL, its energy out direct and into the DSP
//                        until the slot's next voice, LPCTL and RR. Not with --scsp-log
//     --dump a,b         write the RAMs and registers at the end of these frames (fNNNNN_*), for
//                        tools/compare.py against tools/mame_snap.sh's
//     --frames-log       one line per frame: frame, pc, irq counts, a hash of work RAM
//     --layers           with --png, also each tile layer's whole 512x512 map (fNNNNN_layerK.png)
//     --thunks file      the `balx <thunk>, r14` calls seen, with where they returned (site thunk return)
//     --copro-log file   the i960's writes to the TGP ("w offset value") and the values it reads back
//     --script file      inputs from a script (src/host/input_script.h); runs at least to its end
//     --2d               PNGs of the tile layers only (no polygons)
//     --scale N          PNGs at N times the arcade's resolution (the 3D drawn at it, the 2D
//                        enlarged), as the game's render scale; the default 1 is MAME's picture
//     --aspect A         the picture's shape, as the game's: 4:3 (default, MAME's), 16:10, 16:9,
//                        21:9 (64:27), or a width/height such as 2.0
//     --backgrounds dir  widescreen's painted sides from dir (select.png: see src/host/backgrounds.h)
//     --culling N        the scenery's culling (model2a::set_scenery_culling): 0 the game's, 1
//                        by neighbouring headings, 2 at every heading (widescreen's own)
//     --free-play        boot, switch the coin setting to FREE PLAY, boot again (then frame 0)
//     --backdrop-filter  a fight's stage backdrop softened and smoothly enlarged above 1x (the
//                        game's Display > Background filter: Smooth)
//     --ko P[@a[-b]]     knock player P (1 or 2) out in every round, once its fight is on (0x50002b, a
//                        game's state, and 0x500031, the round's, both 9), from frame a (to b): its
//                        health (0x1ac in its block, 0x510980 / 0x512980) to 0 and its KO flag (bit 5
//                        of the block's first word) set, as the game's damage routine (0x18a54) does
//                        when a hit takes the last of it; the round ends as at any KO. With
//                        tools/scripts/start_game.txt, --ko 2@1600 wins a 1P game to the ending and the
//                        name entry, --ko 1@1600 loses it to the continue and game over
//     --secret N         the player select's Akira is fighter N (model2a::set_secret_character)
//     --poke A=V@a[-b]   write byte V at address A (hex) at the end of every frame from a (to b)
//     --sound            run the sound board (68000 and SCSP); without it the 68000 is held, as
//                        in tools/mame_snap.sh's reference runs
//     --wav file         the sound as heard (16-bit stereo, 44.1 kHz; implies --sound)
//     --raw-audio file   the SCSP's own output, float32 stereo pairs (as MAME's Lua sound hook
//                        sees it; implies --sound), for tools/sound_compare.py
//     --version N        the test mode's GAME ASSIGNMENT version (0 Original, 1 Version 2.1): boot,
//                        set it, boot again (then frame 0), as --free-play does
//     --interp           the i960 on MAME's interpreter (the default: recompiled to native code at
//                        start-up, src/rec); --cpp the recompiled C++ (vf2run-rec only: make rec)
//     --watch            the boot's milestones (the instruction hook: the interpreter)
//     --cov file         coverage for the recompiler (tools/recomp): every instruction run ("pc A N")
//                        and every change of flow the code doesn't spell out ("edge A B": returns,
//                        jumps and calls through registers, interrupts). Appends.
// Everything it writes is ROM-derived: keep it under build/.
#include "../src/machine/model2a.h"
#include "../src/host/png.h"
#include "../src/host/input_script.h"
#include "../src/host/backgrounds.h"
#include "../src/rec/i960_decode.h"
#include <unordered_map>
#include <chrono>
#include <filesystem>
#include <set>
#include <tuple>

static std::set<uint64_t> parse_list(const char *s)
{
	std::set<uint64_t> r;
	while (*s) {
		char *e;
		uint64_t a = strtoull(s, &e, 10), b = a;
		if (*e == '-') b = strtoull(e + 1, &e, 10);
		for (uint64_t i = a; i <= b; i++) r.insert(i);
		s = *e ? e + 1 : e;
	}
	return r;
}

static bool write_wav(const std::string &path, const std::vector<int16_t> &v, int rate)
{
	FILE *f = fopen(path.c_str(), "wb");
	if (!f) return false;
	auto u32le = [&](uint32_t x) { uint8_t b[4] = { uint8_t(x), uint8_t(x >> 8), uint8_t(x >> 16), uint8_t(x >> 24) }; fwrite(b, 1, 4, f); };
	auto u16le = [&](uint16_t x) { uint8_t b[2] = { uint8_t(x), uint8_t(x >> 8) }; fwrite(b, 1, 2, f); };
	const uint32_t bytes = uint32_t(v.size() * 2);
	fwrite("RIFF", 1, 4, f); u32le(36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
	u32le(16); u16le(1); u16le(2); u32le(rate); u32le(rate * 4); u16le(4); u16le(16);
	fwrite("data", 1, 4, f); u32le(bytes);
	for (int16_t x : v) u16le(uint16_t(x));
	return fclose(f) == 0;
}

static uint64_t fnv(const uint8_t *p, size_t n)
{
	uint64_t h = 1469598103934665603ull;
	for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
	return h;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: vf2run <vf2.zip> <frames> [--png a,b|all] [--out dir] [--trace file] [--trace-frames a-b] [--dump a,b] [--frames-log]\n");
		return 2;
	}
	uint64_t frames = strtoull(argv[2], nullptr, 10);
	std::set<uint64_t> png, dump, trace_frames;
	bool png_all = false, frames_log = false, layers = false;
	std::string out = "build/frames", trace_path, thunks_path, copro_path, tgp_trace_path, script_path;
	bool only_2d = false, free_play = false, sound = false, byte_link = false;
	int version = -1;
	std::string cov_path;
	bool watch_milestones = false;
	enum { CPU_NATIVE, CPU_INTERP, CPU_CPP } cpu_mode = CPU_NATIVE;
	int scale = 1;
	float aspect = 4.0f / 3.0f;
	int culling = -1;
	bool backdrop_filter = false;
	std::string backgrounds;
	std::string wav_path, raw_path, snd_trace_path, scsp_log_path, voice_log_path;
	int music = 100, effects = 100;
	float level = 1;
	struct knockout { int player; uint64_t from, to; };
	std::vector<knockout> knockouts;
	struct poke { u32 addr; u8 value; uint64_t from, to; };
	std::vector<poke> pokes;
	int secret = -1;
	for (int i = 3; i < argc; i++) {
		std::string a = argv[i];
		auto next = [&]() -> const char * { if (i + 1 >= argc) { fprintf(stderr, "%s needs a value\n", a.c_str()); exit(2); } return argv[++i]; };
		if (a == "--png") { const char *v = next(); if (!strcmp(v, "all")) png_all = true; else png = parse_list(v); }
		else if (a == "--out") out = next();
		else if (a == "--trace") trace_path = next();
		else if (a == "--trace-frames") trace_frames = parse_list(next());
		else if (a == "--tgp-trace") tgp_trace_path = next();
		else if (a == "--68k-trace") { snd_trace_path = next(); sound = true; }
		else if (a == "--scsp-log") { scsp_log_path = next(); sound = true; }
		else if (a == "--voice-log") { voice_log_path = next(); sound = true; }
		else if (a == "--music") music = atoi(next());
		else if (a == "--effects") effects = atoi(next());
		else if (a == "--level") level = float(atof(next()));
		else if (a == "--dump") dump = parse_list(next());
		else if (a == "--frames-log") frames_log = true;
		else if (a == "--layers") layers = true;
		else if (a == "--thunks") thunks_path = next();
		else if (a == "--copro-log") copro_path = next();
		else if (a == "--script") script_path = next();
		else if (a == "--2d") only_2d = true;
		else if (a == "--scale") scale = atoi(next());
		else if (a == "--culling") culling = atoi(next());
		else if (a == "--backgrounds") backgrounds = next();
		else if (a == "--backdrop-filter") backdrop_filter = true;
		else if (a == "--aspect") {
			const std::string v = next();
			if (v == "4:3") aspect = 4.0f / 3.0f;
			else if (v == "16:10") aspect = 16.0f / 10.0f;
			else if (v == "16:9") aspect = 16.0f / 9.0f;
			else if (v == "21:9") aspect = 64.0f / 27.0f;
			else if (atof(v.c_str()) > 0) aspect = float(atof(v.c_str()));
			else { fprintf(stderr, "--aspect: 4:3, 16:10, 16:9, 21:9 or a number\n"); return 2; }
		}
		else if (a == "--ko") {
			const char *v = next();
			char *e;
			knockout k{ int(strtol(v, &e, 10)), 0, UINT64_MAX };
			if (*e == '@') {
				k.from = strtoull(e + 1, &e, 10);
				if (*e == '-') k.to = strtoull(e + 1, &e, 10);
			}
			if (*e || (k.player != 1 && k.player != 2)) { fprintf(stderr, "--ko: 1 or 2, then @frame or @a-b\n"); return 2; }
			knockouts.push_back(k);
		}
		else if (a == "--poke") {
			const char *v = next();
			char *e;
			poke k{ u32(strtoul(v, &e, 16)), 0, 0, UINT64_MAX };
			if (*e == '=') k.value = u8(strtoul(e + 1, &e, 0));
			if (*e == '@') {
				k.from = strtoull(e + 1, &e, 10);
				k.to = *e == '-' ? strtoull(e + 1, &e, 10) : k.from;
			}
			if (*e) { fprintf(stderr, "--poke: A=V@a[-b]\n"); return 2; }
			pokes.push_back(k);
		}
		else if (a == "--secret") secret = atoi(next());
		else if (a == "--free-play") free_play = true;
		else if (a == "--version") version = atoi(next());
		else if (a == "--watch") watch_milestones = true;
		else if (a == "--interp") cpu_mode = CPU_INTERP;
		else if (a == "--cpp") cpu_mode = CPU_CPP;
		else if (a == "--cov") cov_path = next();
		else if (a == "--byte-link") byte_link = true;
		else if (a == "--sound") sound = true;
		else if (a == "--wav") { wav_path = next(); sound = true; }
		else if (a == "--raw-audio") { raw_path = next(); sound = true; }
		else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
	}

	input_script script;
	if (!script_path.empty()) {
		std::string err;
		if (!script.load(script_path, err)) { fprintf(stderr, "%s\n", err.c_str()); return 2; }
		frames = std::max(frames, script.end + 1);
	}
	if (!script_path.empty()) {
		std::filesystem::create_directories(out);
		script.write_events(out + "/script_events.txt");   // for tools/mame_snap.sh: SCRIPT_EVENTS=...
	}
	romset::result roms = romset::load(argv[1]);
	if (!roms.ok) { fprintf(stderr, "%s\n", roms.error.c_str()); return 1; }
	model2a m(std::move(roms));
	if (const char *hn = getenv("VF2_HUD_NAMES")) {   // (tests: "P1NAME,P2NAME" under the energy bars, as netplay shows them)
		const std::string v = hn;
		const size_t comma = v.find(',');
		const std::string names[2] = { v.substr(0, comma), comma == std::string::npos ? std::string() : v.substr(comma + 1) };
		// (and VF2_HUD_FLAGS=1: a flag beside each, a plain one, the size the game's are)
		m2hud::Flag f;
		if (getenv("VF2_HUD_FLAGS")) { f.w = 12; f.h = 9; f.px.assign(size_t(f.w * f.h), 0xFFD02020u); }
		const m2hud::Flag *fl[2] = { &f, &f };
		m.set_hud_names(names, fl);
	}
	if (cpu_mode == CPU_INTERP) m.use_interpreter();
	else if (cpu_mode == CPU_CPP && !m.use_generated()) { fprintf(stderr, "--cpp: this build has no recompiled C++ (vf2run-rec has: make rec)\n"); return 2; }
	else if (cpu_mode == CPU_NATIVE) {
		if (!m.recompiled()) fprintf(stderr, "couldn't recompile the program: on the interpreter\n");
		else if (!getenv("VF2_QUIET")) {
			const i960rec::native_stats &st = m.native_stats();
			fprintf(stderr, "recompiled %zu functions (%zu instructions) to %zu KB of native code in %.0f ms\n", st.functions, st.instructions, st.code_bytes / 1024, st.ms);
		}
	}
	std::filesystem::create_directories(out);

	// milestones: logged once each, with the frame and cycle they happen at
	auto stamp = [&]() { return string_format("[frame %3d cycle %10llu]", int(m.frame_number()), (unsigned long long)m.now()); };
	m.on_milestone = [&](const std::string &s) { fprintf(stderr, "%s %s\n", stamp().c_str(), s.c_str()); };
	struct watch { u32 pc; const char *what; bool seen = false; };
	std::vector<watch> watches = {
		{ 0x000001b0, "post-IAC entry (PRCB 0x5ff410)" },
		{ 0x00000e60, "TGP upload routine" },
		{ 0x00000fbc, "\"I/O Initialize ...\" printed" },
		{ 0x00000bc0, "first frame IRQ handler (vector 12)" },
		{ 0x00000d50, "first timer IRQ handler (vector 14)" },
		{ 0x00010d54, "scheduler entry" },
		{ 0x0001645c, "first dispatch: fa_game_info" },
		{ 0x0001d320, "first dispatch: fa_camera" },
		{ 0x00029748, "first dispatch: fa_user" },
		{ 0x000439fc, "first dispatch: fa_sound" },
	};
	std::vector<uint8_t> watched(0x200000 / 4, 0);
	for (size_t i = 0; i < watches.size(); i++) watched[watches[i].pc / 4] = uint8_t(i + 1);
	FILE *trace = trace_path.empty() ? nullptr : fopen(trace_path.c_str(), "w");
	const bool want_thunks = !thunks_path.empty();
	bool tracing = false;
	uint64_t instructions = 0;
	// balx <thunk>, r14 sites and their returns (the thunks return past data placed after the call)
	std::set<std::tuple<u32, u32, u32>> thunk_returns;
	std::vector<std::pair<u32, u32>> thunk_stack;
	u32 prev_pc = 0;
	auto is_thunk = [](u32 t) { return t == 0x9444 || (t >= 0x93c4 && t <= 0x9408) || t == 0x4ec00 || t == 0x502a4; };
	auto thunk_exit = [](u32 pc) { return pc == 0x947c || pc == 0x4ec08 || pc == 0x502bc; };
	// coverage: instructions run, and the changes of flow that aren't the code's own static ones
	std::vector<uint64_t> cov_pc(cov_path.empty() ? 0 : 0x200000 / 4, 0);
	std::unordered_map<u32, uint64_t> cov_other;   // outside the program ROM
	std::set<std::pair<u32, u32>> cov_edges;
	u32 cov_prev = ~0u;
	const bool want_cov = !cov_path.empty();
	// The instruction hook runs everything on the interpreter (the recompiled code has no
	// instructions to hook): only when something wants it (traces, thunks, coverage, --watch)
	const bool want_hook = trace || want_thunks || want_cov || watch_milestones;
	if (want_hook) m.on_instruction = [&](u32 pc) {
		instructions++;
		if (want_cov) {
			if (pc < 0x200000) cov_pc[pc / 4]++; else cov_other[pc]++;
			if (cov_prev != ~0u && pc != cov_prev) {
				auto rd = [&](u32 a) { return m.peek32(a); };
				const i960rec::decoded d = i960rec::decode(cov_prev, rd);
				const bool seq = pc == cov_prev + u32(d.len);
				const bool stat = d.has_target && pc == d.target;
				if (!seq && !stat) cov_edges.insert({ cov_prev, pc });
				else if (seq && d.k == i960rec::kind::call) {}   // (a call that came straight back: none)
			}
			cov_prev = pc;
		}
		if (want_thunks) {
			if (thunk_exit(prev_pc) && !thunk_stack.empty()) {
				thunk_returns.insert({ thunk_stack.back().first, thunk_stack.back().second, pc });
				thunk_stack.pop_back();
			}
			if (pc < 0x200000) {
				u32 op = m.peek32(pc);
				if ((op >> 24) == 0x85 && (op & 0x1000) && ((op >> 10) & 0xf) == 0xc && ((op >> 19) & 0x1f) == 14 && is_thunk(m.peek32(pc + 4)))
					thunk_stack.push_back({ pc, m.peek32(pc + 4) });
			}
			prev_pc = pc;
		}
		if (pc < 0x200000) {
			if (uint8_t w = watched[pc / 4]; w && !watches[w - 1].seen) {
				watches[w - 1].seen = true;
				m.on_milestone(string_format("%s at %08x", watches[w - 1].what, pc));
			}
		}
		if (tracing && trace) fprintf(trace, "%llu %08x\n", (unsigned long long)m.now(), pc);
	};

	FILE *copro = copro_path.empty() ? nullptr : fopen(copro_path.c_str(), "w");
	if (copro) m.on_copro = [&](char kind, u32 offset, u32 v) {
		if (kind == 'w') fprintf(copro, "w %08x %08x\n", offset, v);
		else fprintf(copro, "%08x\n", v);
	};
	FILE *tgp_trace = tgp_trace_path.empty() ? nullptr : fopen(tgp_trace_path.c_str(), "w");
	if (tgp_trace) m.tgp().on_instruction = [&](u16 pc) { if (tracing) fprintf(tgp_trace, "%llu %04x\n", (unsigned long long)m.tgp_now(), pc); };
	FILE *snd_trace = snd_trace_path.empty() ? nullptr : fopen(snd_trace_path.c_str(), "w");
	if (snd_trace) m.on_68k_instruction = [&](u32 pc) { if (tracing) fprintf(snd_trace, "%llu %06x\n", (unsigned long long)m.snd_cycles(), pc); };
	m.set_sound(sound);
	m.set_byte_link(byte_link);   // (the host's link to the sound board: not MAME's timing)
	m.set_render_scale(scale);
	m.set_aspect(aspect);
	if (culling >= 0 && !m.set_scenery_culling(culling)) fprintf(stderr, "--culling: not the program known\n");
	if (secret >= 0 && !m.set_secret_character(secret)) fprintf(stderr, "--secret: not the program known\n");
	m.set_backdrop_filter(backdrop_filter);
	if (!backgrounds.empty() && !load_backgrounds(m, backgrounds)) fprintf(stderr, "--backgrounds: nothing in %s\n", backgrounds.c_str());
	m.set_volumes(music / 100.0f, effects / 100.0f);
	m.set_level(level);
	m.m_keep_raw = !raw_path.empty();
	m.reset();
	if (version >= 0) {
		for (int i = 0; i < 120 && m.assignment(model2a::game_assignment::version) < 0; i++) m.run_frame();
		const bool changed = m.set_assignment(model2a::game_assignment::version, version);
		fprintf(stderr, "version %d%s\n", m.assignment(model2a::game_assignment::version), changed ? " (set)" : "");
		m.reset();
	}
	if (free_play) {
		// the first boot writes the settings block; free play is then read at the next boot
		for (int i = 0; i < 120 && !m.force_free_play() && !m.free_play(); i++) m.run_frame();
		fprintf(stderr, m.free_play() ? "free play set\n" : "free play: no settings block\n");
		m.reset();
	}
	bool ready_seen = false;
	std::vector<int16_t> wav;
	FILE *scsp_log = scsp_log_path.empty() ? nullptr : fopen(scsp_log_path.c_str(), "w");
	uint64_t scsp_log_next = 0;
	if (scsp_log) {
		m.on_uart_tx = [&](u8 b) { fprintf(scsp_log, "%.4f uart %02x\n", m.scsp_samples() / 44100.0, b); };
		m.scsp().m_probe = true;
		m.on_68k_scsp_write = [&](u32 o, u16 d, u16 mask) {   // a key-on (KYONEX): who did it
			if (o < 0x400 && (o & 0x1f) == 0 && (d & mask & 0x1000)) {
				m68000_device &c = m.snd_cpu();
				fprintf(scsp_log, "%.4f kyonex pc %06x d0-7", m.scsp_samples() / 44100.0, c.pc());
				for (int i = 0; i < 8; i++) fprintf(scsp_log, " %08x", c.reg(i));
				fprintf(scsp_log, " a0-6");
				for (int i = 8; i < 15; i++) fprintf(scsp_log, " %08x", c.reg(i));
				fprintf(scsp_log, "\n");
			}
		};
		m.scsp().on_key_on = [&](const scsp_device::key_on &k) {
			fprintf(scsp_log, "%.4f keyon %s slot %2d sa %05x lsa %04x lea %04x %s lp %d tl %3d disdl %d dipan %2d isel %2d imxl %d sdir %d oct %d fns %03x ar %d d1r %d d2r %d rr %d dl %d hold %d krs %d lpslnk %d\n",
				k.sample / 44100.0, m.scsp().slot_group(k.slot) ? "effects" : "music", k.slot, k.sa, k.lsa, k.lea, k.pcm8b ? "8b" : "16", k.lpctl, k.tl, k.disdl, k.dipan, k.isel, k.imxl, k.sdir, k.oct, k.fns, k.ar, k.d1r, k.d2r, k.rr, k.dl, k.eghold, k.krs, k.lpslnk);
		};
	}
	// --voice-log: each voice (key-on) with what the sound program started it for and its energy
	struct voice { double t0, t1; int slot, entry, note, group; u32 sa; int tl, disdl, imxl, isel, efsdl, lpctl, rr; double direct, dsp; };
	std::vector<voice> voices;
	int voice_now[32], pend_entry[32], pend_note[32];
	std::fill(std::begin(voice_now), std::end(voice_now), -1);
	std::fill(std::begin(pend_entry), std::end(pend_entry), -2);
	std::fill(std::begin(pend_note), std::end(pend_note), -1);
	const bool voice_log = !voice_log_path.empty() && !scsp_log;
	if (voice_log) {
		m.scsp().m_probe = true;
		m.on_68k_scsp_write = [&](u32 o, u16, u16) {
			m68000_device &c = m.snd_cpu();
			if (c.pc() == 0x600a96 && o < 0x400 && (o & 0x1f) == 0) {
				const u32 a0 = c.reg(8);
				pend_entry[o >> 5] = a0 >= 0x4000 && a0 < 0x4200 ? int((a0 - 0x4000) >> 4) : -1;
				pend_note[o >> 5] = int(c.reg(2) & 0xff);
			}
		};
		m.scsp().on_key_on = [&](const scsp_device::key_on &k) {
			const double t = k.sample / 44100.0;
			if (voice_now[k.slot] >= 0) voices[voice_now[k.slot]].t1 = t;
			voice_now[k.slot] = int(voices.size());
			voices.push_back(voice{ t, -1, k.slot, pend_entry[k.slot], pend_note[k.slot], m.scsp().slot_group(k.slot), k.sa, k.tl, k.disdl, k.imxl, k.isel, 0, k.lpctl, k.rr, 0, 0 });
			pend_entry[k.slot] = -2;   // -2: started without passing the sound program's voice start
		};
	}
	auto t0 = std::chrono::steady_clock::now();
	for (uint64_t f = 0; f < frames; f++) {
		tracing = (trace || tgp_trace || snd_trace) && (trace_frames.empty() || trace_frames.count(f));
		try {
			m.run_frame();
		} catch (const std::exception &e) {
			fprintf(stderr, "%s stopped: %s\n", stamp().c_str(), e.what());
			fprintf(stderr, "  ip %08x pip %08x pc %08x ac %08x\n", m.cpu().ip(), m.cpu().pip(), m.cpu().pc_reg(), m.cpu().reg(I960_AC));
			return 1;
		}
		// the frame just drawn is frame f (MAME's frame_number before it counts it)
		if (!ready_seen && (m.peek32(0x00500068) & 0x80000000)) {
			ready_seen = true;
			m.on_milestone("runtime-ready flag 0x500068[31] set");
		}
		if (frames_log)
			fprintf(stderr, "F %llu ip %08x r13 %08x r14 %08x g0 %08x irq %llu/%llu/%llu/%llu uart %u work %016llx\n", (unsigned long long)f, m.cpu().ip(), m.cpu().reg(13), m.cpu().reg(14), m.cpu().reg(16),
					(unsigned long long)m.m_irq_count[0], (unsigned long long)m.m_irq_count[1], (unsigned long long)m.m_irq_count[2],
					(unsigned long long)m.m_irq_count[3], m.m_uart_bytes, (unsigned long long)fnv(m.workram().data(), m.workram().size()));
		auto [shot_first, shot_last] = script.shots.equal_range(f);
		for (auto it = shot_first; it != shot_last; ++it) {
			const bitmap_rgb32 &b = m.render(!only_2d);
			std::string path = out + "/" + it->second + ".png";
			write_png(path.c_str(), b.data(), b.width(), b.height());
		}
		if (scsp_log && m.scsp_samples() >= scsp_log_next) {   // energies every 1/4 s
			scsp_device &c = m.scsp();
			fprintf(scsp_log, "%.4f energy efreg %.3g efsdl", m.scsp_samples() / 44100.0, c.m_probe_efreg);
			for (int i = 0; i < 16; i++) fprintf(scsp_log, " %d", c.m_probe_efsdl[i]);
			fprintf(scsp_log, " |");
			for (int i = 0; i < 32; i++) fprintf(scsp_log, " %d:%.3g/%.3g", i, c.m_probe_direct[i], c.m_probe_dsp_in[i]);
			fprintf(scsp_log, "\n");
			std::fill(std::begin(c.m_probe_direct), std::end(c.m_probe_direct), 0.0);
			std::fill(std::begin(c.m_probe_dsp_in), std::end(c.m_probe_dsp_in), 0.0);
			c.m_probe_efreg = 0;
			scsp_log_next = m.scsp_samples() + 44100 / 4;
		}
		if (voice_log) {
			scsp_device &c = m.scsp();
			for (int i = 0; i < 32; i++) {
				if (voice_now[i] >= 0) { voices[voice_now[i]].direct += c.m_probe_direct[i]; voices[voice_now[i]].dsp += c.m_probe_dsp_in[i]; }
				c.m_probe_direct[i] = c.m_probe_dsp_in[i] = 0;
			}
		}
		if (!wav_path.empty()) wav.insert(wav.end(), m.m_audio_out.begin(), m.m_audio_out.end());
		m.m_audio_out.clear();
		uint8_t ports[input_script::N_PORTS];
		script.apply(f, ports);
		m.m_in0 = ports[input_script::IN0]; m.m_in1 = ports[input_script::IN1];
		m.m_in2 = ports[input_script::IN2]; m.m_sw = ports[input_script::SW];
		if (png_all || png.count(f)) {
			const bitmap_rgb32 &b = m.render(!only_2d);
			std::string path = string_format("%s/f%05d.png", out.c_str(), int(f));
			write_png(path.c_str(), b.data(), b.width(), b.height());
			if (layers) {
				std::vector<uint32_t> map(512 * 512);
				for (int l = 0; l < 4; l++) {
					m.tiles().render_map(l, map.data());
					path = string_format("%s/f%05d_layer%d.png", out.c_str(), int(f), l);
					write_png(path.c_str(), map.data(), 512, 512);
				}
			}
		}
		if (dump.count(f)) {
			auto save = [&](const char *what, const void *p, size_t n) {
				std::string path = string_format(strchr(what, '.') ? "%s/f%05d_%s" : "%s/f%05d_%s.bin", out.c_str(), int(f), what);
				FILE *o = fopen(path.c_str(), "wb");
				fwrite(p, 1, n, o);
				fclose(o);
			};
			save("workram", m.workram().data(), m.workram().size());
			save("tileram", m.tiles().tile_ram.data(), m.tiles().tile_ram.size() * 2);
			std::vector<u8> pal(0x4000);
			for (u32 i = 0; i < 0x4000; i += 4) { u32 v = m.peek32(0x01800000 + i); memcpy(&pal[i], &v, 4); }
			save("palram", pal.data(), pal.size());
			std::vector<u8> xlat(0xc000);
			for (u32 i = 0; i < 0xc000; i += 4) { u32 v = m.peek32(0x01810000 + i); memcpy(&xlat[i], &v, 4); }
			save("colorxlat", xlat.data(), xlat.size());
			save("backup", m.backup_ram().data(), m.backup_ram().size());
			std::vector<u8> buf(0x20000);
			for (u32 i = 0; i < 0x20000; i += 4) { u32 v = m.peek32(0x00900000 + i); memcpy(&buf[i], &v, 4); }
			save("bufferram", buf.data(), buf.size());
			save("eeprom", m.eeprom_cells(), 128);
			if (sound) {   // the sound board's RAM, in the 68000's byte order
				std::vector<u8> sr(m.soundram().size() * 2);
				for (size_t i = 0; i < m.soundram().size(); i++) { sr[2 * i] = u8(m.soundram()[i] >> 8); sr[2 * i + 1] = u8(m.soundram()[i]); }
				save("soundram", sr.data(), sr.size());
			}
			const i960_device &c = m.cpu();
			std::string regs = string_format("ip %08x pc %08x ac %08x", c.ip(), c.pc_reg(), c.reg(I960_AC));
			for (int i = 0; i < 16; i++) regs += string_format(" r%d %08x", i, c.reg(i));
			for (int i = 0; i < 15; i++) regs += string_format(" g%d %08x", i, c.reg(16 + i));
			regs += string_format(" fp %08x\n", c.reg(31));
			save("regs.txt", regs.data(), regs.size());
		}
		for (const poke &k : pokes)
			if (f >= k.from && f <= k.to) m.write_byte(k.addr, k.value);
		for (const knockout &k : knockouts)   // (after the dumps: the game sees it in frame f + 1)
			if (f >= k.from && f <= k.to && m.workram()[0x2b] == 9 && m.workram()[0x31] == 9) {
				const u32 block = k.player == 1 ? 0x510980 : 0x512980;
				if (int16_t(m.peek32(block + 0x1ac)) > 0 && !(m.peek32(block) & 0x20)) {
					m.write_word(block + 0x1ac, 0);
					m.write_dword(block, m.peek32(block) | 0x20);
					m.on_milestone(string_format("player %d knocked out", k.player));
				}
			}
	}
	double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	if (trace) fclose(trace);
	if (tgp_trace) fclose(tgp_trace);
	if (snd_trace) fclose(snd_trace);
	if (scsp_log) {   // the DSP's program as it stands at the end
		const SCSPDSP &d = m.scsp().dsp();
		for (int g = 0; g < 2; g++)
			fprintf(scsp_log, "egstate %s attack %.4g decay1 %.4g decay2 %.4g release %.4g\n", g ? "effects" : "music",
				m.scsp().m_probe_state[g][0], m.scsp().m_probe_state[g][1], m.scsp().m_probe_state[g][2], m.scsp().m_probe_state[g][3]);
		fprintf(scsp_log, "dsp RBL %u RBP %u laststep %d\n", d.RBL, d.RBP, d.LastStep);
		for (int i = 0; i < 64; i++) fprintf(scsp_log, "dsp coef %d %d\n", i, d.COEF[i]);
		for (int i = 0; i < 32; i++) fprintf(scsp_log, "dsp madrs %d %u\n", i, d.MADRS[i]);
		for (int i = 0; i < 128; i++) fprintf(scsp_log, "dsp mpro %d %04x %04x %04x %04x\n", i, d.MPRO[i * 4], d.MPRO[i * 4 + 1], d.MPRO[i * 4 + 2], d.MPRO[i * 4 + 3]);
		fclose(scsp_log);
	}
	if (voice_log) {
		FILE *o = fopen(voice_log_path.c_str(), "w");
		if (o) {
			fprintf(o, "t0 t1 slot entry note group sa tl disdl imxl isel direct dsp lpctl rr\n");
			for (const voice &v : voices)
				fprintf(o, "%.4f %.4f %d %d %d %d %05x %d %d %d %d %.6g %.6g %d %d\n", v.t0, v.t1 < 0 ? m.scsp_samples() / 44100.0 : v.t1, v.slot, v.entry, v.note, v.group, v.sa, v.tl, v.disdl, v.imxl, v.isel, v.direct, v.dsp, v.lpctl, v.rr);
			fclose(o);
		}
	}
	if (!wav_path.empty() && !write_wav(wav_path, wav, model2a::SOUND_RATE)) fprintf(stderr, "couldn't write %s\n", wav_path.c_str());
	if (!raw_path.empty()) {
		FILE *o = fopen(raw_path.c_str(), "wb");
		if (o) { fwrite(m.m_audio_raw.data(), sizeof(float), m.m_audio_raw.size(), o); fclose(o); }
		fprintf(stderr, "%zu samples of the SCSP's output (%.2f s) in %s\n", m.m_audio_raw.size() / 2, m.m_audio_raw.size() / 2 / double(model2a::SOUND_RATE), raw_path.c_str());
	}
	if (sound) fprintf(stderr, "68000: %llu cycles; SCSP: %llu samples; MIDI out %llu bits\n", (unsigned long long)m.snd_cycles(), (unsigned long long)m.scsp_samples(), (unsigned long long)m.m_midi_out_bits);
	if (copro) fclose(copro);
	if (want_cov) {
		FILE *o = fopen(cov_path.c_str(), "a");
		if (o) {
			for (u32 i = 0; i < cov_pc.size(); i++) if (cov_pc[i]) fprintf(o, "pc %06x %llu\n", i * 4, (unsigned long long)cov_pc[i]);
			for (auto &[a, n] : cov_other) fprintf(o, "pc %08x %llu\n", a, (unsigned long long)n);
			for (auto &[a, b] : cov_edges) fprintf(o, "edge %06x %06x\n", a, b);
			fclose(o);
		}
	}
	if (want_thunks) {
		FILE *o = fopen(thunks_path.c_str(), "w");
		for (auto [site, thunk, ret] : thunk_returns) fprintf(o, "%06x %06x %06x\n", site, thunk, ret);
		fclose(o);
		fprintf(stderr, "%zu thunk call sites seen\n", thunk_returns.size());
	}
	if (want_hook)
		fprintf(stderr, "%llu frames, %llu i960 instructions, %llu TGP instructions, %u UART bytes in %.2fs\n",
				(unsigned long long)frames, (unsigned long long)instructions, (unsigned long long)m.m_tgp_instructions, m.m_uart_bytes, s);
	else
		fprintf(stderr, "%llu frames (i960 %s), %llu TGP instructions, %u UART bytes in %.2fs\n", (unsigned long long)frames,
				!m.recompiled() ? "interpreted" : cpu_mode == CPU_CPP ? "recompiled C++" : "recompiled", (unsigned long long)m.m_tgp_instructions, m.m_uart_bytes, s);
	if (!m.m_unmapped.empty()) {
		fprintf(stderr, "unmapped accesses:");
		int n = 0;
		for (auto &[a, c] : m.m_unmapped) if (n++ < 24) fprintf(stderr, " %s%08x x%d", (a & 1) ? "w" : "r", a & ~1u, c);
		fprintf(stderr, "\n");
	}
	return 0;
}
