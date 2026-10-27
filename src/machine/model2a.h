// license:BSD-3-Clause
// copyright-holders:R. Belmont, Olivier Galibert, ElSemi, Angelo Salese, Matthew Daniels, Dirk Best, Aaron Giles, smf, Robbbert
// Sega Model 2A-CRX as Virtua Fighter 2 uses it: the i960KB main CPU on MAME's memory map
// (sega/model2.cpp, model2a_crx_mem), the MB86234 TGP running its uploaded program, the IRQ
// controller and timers, the System 24 tile layers, the geometrizer and polygon renderer
// (model2_video), the 315-5649 I/O chip with its 93C46 EEPROM, the i8251 UART to the sound
// board, and the sound board: a 68000 (MAME's core) and the SCSP (scsp.h), the link reaching the
// SCSP's MIDI input bit by bit as on MAME (model2_scsp, model2_snd).
//
// The CPUs are scheduled as MAME's device_scheduler does it: timeslices up to the next timer,
// the running CPUs in turn, halts and FIFO stalls through synchronised input lines, time in
// attoseconds. So the i960 and the TGP interleave exactly as on MAME. The sound board's 68000
// runs, or is held halted as in the MAME reference runs for the CPU/video regression
// (tools/mame_snap.sh, set_sound(false)); the billboard's Z80 only runs NOPs (MAME without its
// ROM), kept in the schedule for the way it cuts timeslices short. The video timing is MAME's:
// 16 MHz pixels, 656 x 424 per frame, vblank from line 384.
#pragma once
#include "../core/i960_device.h"
#include "../core/mb86233.h"
#include "../rec/i960_native.h"
#include "../host/romset.h"
#include "mame_lite.h"
#include "s24tile.h"
#include "model2_video.h"
#include "parallel.h"
#include "scsp.h"
#include "state_io.h"
#include "../../third_party/mame/m68000/m68000.h"
#include "hud_names.h"
#include <array>
#include <functional>
#include <deque>
#include <map>
#include <vector>
#include <mutex>

class model2a : public mame_bus32
{
public:
	static constexpr uint64_t CYCLES_PER_LINE = 1025;                  // 656 pixels at 16 MHz
	static constexpr uint64_t LINES = 424, VBLANK_LINE = 384;
	static constexpr uint64_t CYCLES_PER_FRAME = CYCLES_PER_LINE * LINES;   // 434600
	static constexpr uint64_t VBLANK_CYCLES = CYCLES_PER_LINE * (LINES - VBLANK_LINE);
	static constexpr int WIDTH = 496, HEIGHT = 384;

	explicit model2a(romset::result &&roms);
	~model2a() override;

	void reset();
	// A cold start: every RAM as a board just switched on has it (zeros, backup RAM 0xff, the EEPROM's cells
	// 0xffff), the CPUs' registers and the board's cleared, nothing kept from a run before (reset() keeps work
	// RAM, the tile and texture RAMs, backup RAM and more, and the CPUs' resets leave some registers). Then
	// reset(). Netplay's start: two boards that ran differently before start the same.
	void power_on();

	// Save states (state_io.h): everything the game's run depends on, the i960's and the TGP's state, the
	// scheduler (its timers, the CPUs' clocks, the input lines' queues), the FIFOs, the board's registers and
	// all its RAMs (about 7 MB). Not the sound board: a state is whole only with it held (set_sound(false)) and
	// the byte link (set_byte_link(true)), as in a match, where nothing the sound board does reaches this
	// board. Nor the 3D's own state (its matrices, lists, pictures): the picture's alone, and it goes by the
	// widescreen setting (the game never reads it back; polygon_count_reads() says if it did). save_state
	// reuses the buffer's memory; sections, if given, are filled with where each part starts. load_state
	// restores one this build saved, and false (the board then half restored: reset it) if it isn't one.
	// Between frames only (after run_frame()).
	void save_state(std::vector<uint8_t> &out, std::vector<m2::StateSection> *sections = nullptr);
	bool load_state(const uint8_t *p, size_t n);
	static constexpr uint32_t STATE_MAGIC = 0x53324656, STATE_VERSION = 1;   // "VF2S"
	// The state's first bytes are for this board's saves alone (which board, since which reset, which save): two
	// boards in the same state have the same bytes after them, and only those are compared or hashed
	static constexpr size_t STATE_HEADER = 24;
	uint64_t polygon_count_reads() const { return m_video->m_polygon_count_reads; }
	// A hash of the ROMs' contents as loaded, before any patch (the handshake's: both players need the same)
	uint64_t rom_hash() const { return m_rom_hash; }

	// Runs to the end of the next vblank, where MAME draws the screen. MAME's screen starts at
	// the top of vblank: vblank N begins at cycle (N + 1) * CYCLES_PER_FRAME, and frame N (as
	// MAME's frame_number counts) is drawn when it ends, VBLANK_CYCLES later.
	void run_frame();
	void run_until(uint64_t cycle);
	// The screen as MAME's screen_update draws it: tile layers, polygons, tile layers. 496 x 384.
	// Call it right after run_frame(), when MAME draws (render(false): the 2D layers only).
	// At a render scale above 1 the picture is that many times larger each way.
	const bitmap_rgb32 &render(bool polygons = true);
	// The same picture into `out` (rows `stride` pixels apart): a texture's memory, say.
	void render_into(u32 *out, size_t stride, bool polygons = true);
	// render_into(out, stride), finished on another thread while the board runs on (the host's
	// pipelining: the next frame run while this one is drawn). The 2D is drawn now; the 3D (the
	// deferred polygons) and the putting together go on meanwhile. render_wait() waits for it.
	// Whatever would change what it reads waits first: the RAMs the polygons are coloured from
	// changing, the next frame's polygons, a new size or shape, another picture. The same pixels.
	// (Render test mode, and a 4:3 screen in a wide picture, whose sides go by what's drawn, are
	// drawn at once.)
	void render_async(u32 *out, size_t stride);
	void render_wait();
	// The same picture in parts, for the host to put together on the GPU: the 2D at the arcade's
	// resolution, each of its pixels to go up as an s x s block (nearest sampling), and the 3D at
	// the render scale. In order, each over the last:
	//  - the backdrop (tile maps 2 and 3), opaque, PAD arcade pixels larger each way with its edge
	//    pixels carried on, put at (-bdx - PAD * s, -bdy - PAD * s), (w + 2 PAD) * s by
	//    (384 + 2 PAD) * s: smooth motion's move of it (the picture's pixels beyond its edges take
	//    the edge's, as render_into's do). With the background filter (filter), softened, to be
	//    enlarged with straight lines between its pixels' middles (linear sampling);
	//  - mid, the layers over it that go under the 3D (0: transparent);
	//  - the 3D (if has3d), into px3d (rows stride3d apart): alpha 255 where it drew, else 0;
	//    only its rows rows3d (the rest is clear: px3d's left as they were). px3d null: left
	//    where it was drawn, map3d (rows map_stride apart, good until the next picture); map3d
	//    null: drawn straight into px3d, not kept (smooth motion's pictures);
	//  - top, the layers over the 3D (0: transparent);
	//  - a 4:3 screen's sides in a wide picture: sides 1, plain colours (side_colour, left and
	//    right); 2, side_px (rows 2 * e * s apart: the left side's e * s columns, then the right's).
	// The 2D's rows are w apart (backdrop's w + 2 PAD), the board's, good until the next picture;
	// version changes when they do (smooth motion's pictures between frames share their frame's).
	// alpha < 0: render_into's picture, else render_smooth's. async: the 3D drawn alongside the next
	// frame, as render_async (render_wait() before px3d is used); a 4:3 screen in a wide picture is
	// drawn at once. False, nothing done: render test mode (its 3D is drawn between the 2D passes),
	// to be drawn by render_into.
	struct picture_parts {
		static constexpr int PAD = 32;
		int w = 0, s = 1;
		uint64_t version = 0;
		const u32 *backdrop = nullptr, *mid = nullptr, *top = nullptr;
		bool filter = false, has3d = false;
		const u32 *map3d = nullptr;
		size_t map_stride = 0;
		int rows3d[2] = {};
		int bdx = 0, bdy = 0;
		int sides = 0;
		u32 side_colour[2] = {};
		const u32 *side_px = nullptr;
		bool gpu3d = false;   // the 3D drawn by the host's gpu3d (has3d: something to show), not in px3d
	};
	// The 3D on the GPU (the host's, src/gpu/gpu3d.h): given `gpu`, the 3D isn't drawn on the CPU
	// but handed over: gpu(video, picture) with the RAMs as that picture's frame left them (its
	// list: video.export_picture(picture)); the RAMs as they are again once it returns. Its
	// drawing goes on alongside, whether async or not. False from gpu: drawn on the CPU after all,
	// into px3d (gpu3d false). Not for a 4:3 screen in a wide picture (its sides go by the 3D's
	// edge columns): drawn on the CPU, or with gpu_only nothing at all (render_parts false: the
	// host draws the picture whole). also_cpu: the CPU's 3D into px3d as well (checking the GPU's).
	using gpu3d_hook = std::function<bool(model2_video &, int)>;
	bool render_parts(float alpha, u32 *px3d, size_t stride3d, bool async, picture_parts &parts,
		const gpu3d_hook *gpu = nullptr, bool gpu_only = false, bool also_cpu = false);
	// Smooth motion (model2_smooth.cpp): each frame's 3D kept, so that a picture can be drawn for
	// any moment between the last two frames (the picture a frame behind the game). The host's:
	// off (vf2run) nothing is kept and nothing changes. render_smooth draws the moment `alpha`
	// (0-1) of the way from the frame before the newest to the newest (1: the newest, as
	// render_into draws it), at once or alongside the next frame as render_async; its 2D is drawn
	// once a frame (the backdrop moved to the moment). smooth_cut: blend nothing from before the
	// newest frame shown (a scene change held, a cut).
	void set_smooth(bool on) { render_wait(); m_video->set_smooth(on); }
	bool smooth() const { return m_video->smooth(); }
	void render_smooth(float alpha, u32 *out, size_t stride, bool async);
	const bitmap_rgb32 &render_smooth(float alpha) { render_smooth(alpha, &m_screen.pix(0), size_t(m_screen.width()), false); return m_screen; }
	void smooth_cut() { m_video->smooth_cut(); }
	const int *smooth_stats() const { return m_video->m_smooth_stats; }   // the last one's objects: unmatched, jumped, blended
	// The last picture drawn, as it can be drawn again later at another size or shape without
	// running the game (a picture on show while paused, after a change of the display settings):
	// its 2D's frame (and that frame's 2D, kept once the game has run on: a picture held by scene
	// sync), its 3D's lists and moment, the backdrop's move. Needs the lists kept (set_smooth).
	struct picture_desc;
	std::shared_ptr<const picture_desc> last_picture() const { return m_last_pic; }
	const bitmap_rgb32 &redraw(const picture_desc &pic) { redraw_into(pic, &m_screen.pix(0), size_t(m_screen.width())); return m_screen; }
	void redraw_into(const picture_desc &pic, u32 *out, size_t stride);
	// the backdrop's place a moment `t` (frames) shows, against where it is drawn for the newest
	// frame (scroll units, before rounding): 0 none known, 1 from the game's own sum (the camera's
	// heading and pitch it scrolls by, two frames behind), 2 a line through the scroll it set
	int backdrop_move(double t, double &dh, double &dv) const;
	double render_job_ms() const { return m_job_ms; }   // the last one's time on its thread
	// work counted for the host's per-picture log (VF2_PICTURE_LOG): taken and cleared by the host
	struct work_stats { double deferred_ms = 0; int deferred_n = 0, ram_changes = 0; };
	work_stats take_stats() { const work_stats w = m_stats; m_stats = {}; return w; }
	double moment_3d() const { return m_moment3d; }   // the last picture's 3D: its frame (and the way to the next), -1 none
	uint64_t render_waited_ns() const { return m_job_waited; }   // time spent waiting for them, in all
	// The polygons in the 3D the picture shows (0: none), and in the 3D drawn `ago` (0-2) frames
	// before the newest
	int frame_polygons() const { return m_video->polygons_shown(); }
	// The video board, for the GPU renderer's tools (model2_video::export_shown, owner_map_shown)
	model2_video &video() { render_wait(); return *m_video; }
	int polygons_drawn(int ago) const { return m_video->polygons_drawn(ago); }
	// The 3D shown 0-2 frames after MAME shows it (model2_video::set_delay; 0: MAME's screen).
	// Every frame's 3D must then be set up (set_draw_3d(true)): a frame's is shown later.
	void set_3d_delay(int frames, bool cut = true) { render_wait(); m_video->set_delay(frames, cut); }
	// Smooth motion's pictures a further 0-1 frames back (model2_video::set_shift)
	void set_3d_shift(float frames) { render_wait(); m_video->set_shift(frames); }
	// The delay with its fraction now (smooth motion gives some up at a new shot: model2_smooth.cpp)
	float delay_3d() { render_wait(); return m_video->delay_exact(); }
	// The newest frame's 3D is a new shot (a camera cut), as smooth motion's pairing sees it (with
	// the lists kept, the host's; else false)
	bool newest_3d_cut() { return m_video->smooth() && m_video->newest_cut(); }
	// The 3D drawn only for the frames shown (model2_video::defer_drawing): the same pictures.
	void defer_3d(bool on) { render_wait(); m_video->defer_drawing(on); }
	// The 3D drawn at `scale` (1-8) times the arcade's resolution; the 2D layers, drawn at the
	// arcade's, are enlarged to it pixel for pixel (each a square block: as sharp as they are).
	// 1 is MAME's picture, exactly.
	void set_render_scale(int scale);
	int render_scale() const { return m_scale; }
	// Widescreen: the picture `aspect` wide (width / height, 4:3 to 32:9; the arcade's 496 x 384
	// fill a 4:3 tube, so its pixels are a little wide). Columns are added on each side
	// (side_columns()), the arcade's picture in the middle. In a fight (wide_frame()) the 3D sees
	// further to the sides (the same vertical view), the stage's backdrop panorama goes on and the
	// scenery culling is widened (set_scenery_culling); the HUD and text stay in the middle. Other
	// screens keep 4:3, their sides their edge's colour when it's one colour, else black
	// (fill_sides). 4:3 is MAME's screen.
	void set_aspect(float aspect);
	float aspect() const { return m_aspect; }
	int side_columns() const { return m_ext; }              // arcade pixels added on each side
	int width() const { return WIDTH + 2 * m_ext; }         // the picture's width in arcade pixels
	bool wide_frame() const;
	// The game's own culling of the stages' scenery (model2a.cpp): which of the objects around a
	// stage it draws goes by the camera's heading, in eighths of a turn (some in sixteenths), from
	// tables made for the 4:3 view, so wider the sides would lose objects. 0: the game's own;
	// 1: each heading's objects with its neighbours' (still a few lost at the sides at 32:9: a
	// sail, the edges of floors); 2: every object the game would draw from where the camera is,
	// whatever its heading. set_aspect() sets 2 for widescreen (in fights checked against no
	// culling at all: no later frames, the polygon budget's peak 4463 of 5000), 0 for 4:3. False
	// (and the game's own) if the program isn't the one known.
	bool set_scenery_culling(int mode);
	int scenery_culling() const { return m_culling; }
	// Antialiasing's Low: the 3D's edges smoothed after it's drawn, at the render scale (FXAA's
	// subpixel blend without its search along an edge, as vf1-recomp's): only the 3D's pixels and
	// those beside it change, blended with the 2D beneath; the 2D over the 3D (the HUD) stays sharp.
	// The same picture drawn whole and in parts. Off: the pictures as before.
	void set_edge_aa(bool on) { render_wait(); m_edge_aa = on; }
	bool edge_aa() const { return m_edge_aa; }
	// Painted sides for a 4:3 screen in widescreen: "select" (the player select) is the one known.
	// The picture (ARGB) is the screen at any size, its height the screen's 384 lines and the
	// arcade's 4:3 in its middle; only its sides are used (the game draws the middle), where
	// opaque. Their brightness follows the game's: its edge columns against the picture's beside
	// them, so fades carry over. False if the name isn't one known.
	static std::vector<std::string> screen_names();
	bool load_screen(const std::string &name, std::vector<u32> argb, int w, int h);
	// A fingerprint of the backdrop map (tile map 2's rows that hold still): which 4:3 screen is up
	u32 backdrop_print() const;
	// Drawing the polygons at every vblank, as MAME does (off: faster, for runs without pictures)
	void set_draw_3d(bool on) { m_draw_3d = on; }
	uint64_t frame_number() const { return m_frame_number; }
	// The i960's time in its cycles (MAME's totalcycles for it), and the TGP's in its.
	uint64_t now() const;
	uint64_t tgp_now() const;

	i960_device &cpu() { return m_cpu; }
	// The i960's code: recompiled to native code from the program, with the address map (the
	// build's, src/rec/vf2_map.cpp, or a file's), when the board is made (the default: VF2_INTERP
	// in the environment for the interpreter); or the recompiled C++ this build was made with
	// (use_generated: false when it has none: `make rec`'s tools only); or MAME's interpreter
	// (use_interpreter). The same state, cycle for cycle. A patch to the program (shorten_warning,
	// set_scenery_culling) makes the native code again; the C++'s functions made from bytes the
	// program no longer has are left to the interpreter.
	bool use_native(const std::string &map_path = "", i960rec::native_stats *stats = nullptr);
	bool use_generated();
	const i960rec::native_stats &native_stats() const { return m_native_stats; }   // the last use_native's
	void use_interpreter();
	i960rec::runtime *recompiled() { return m_rec.get(); }
	mb86233_device &tgp();

	// Memory, as the i960 sees it (no side effects for RAM; devices are read as the CPU would).
	u32 peek32(u32 a) { return read_dword(a); }
	const std::vector<u8> &workram() const { return m_workram; }
	const segas24_tile &tiles() const { return m_tiles; }
	// Online play's names under the energy bars (hud_names.h), in the 2D picture only, and their flags: "" none
	void set_hud_names(const std::string names[2], const m2hud::Flag *flags[2] = nullptr)
	{
		std::lock_guard<std::mutex> lk(m_hud_mx);
		for (int p = 0; p < 2; p++) { m_hud_names[p] = names[p]; m_hud_flags[p] = flags && flags[p] ? *flags[p] : m2hud::Flag{}; }
	}
	// where each player's flag goes (hud_names: its place left for the host's sharper one), in the picture's pixels
	// (width() by HEIGHT): its left, and the box's top; false for none in the picture last made
	bool hud_flag_at(int p, int &x, int &y)
	{
		std::lock_guard<std::mutex> lk(m_hud_mx);
		x = m_hud_flag_at[p][0];
		y = m_hud_flag_at[p][1];
		return x >= 0 && !m_hud_names[p].empty();
	}
	const std::vector<u8> &backup_ram() const { return m_backup; }
	const u16 *eeprom_cells() const { return m_eeprom.cells; }
	// The board's NVRAM, to keep between runs: backup RAM (16 KB) and the 93C46's 64 cells.
	std::vector<u8> &backup_ram() { return m_backup; }
	u16 *eeprom_cells() { return m_eeprom.cells; }
	// VF2 keeps its test-mode settings in backup RAM (not the EEPROM), in a block at 0x3300 that
	// the game writes on its first boot. Sets COIN/CREDIT SETTING #26, FREE PLAY; true if it
	// changed something (the game reads the settings at boot: reset() after it). False too when
	// the block isn't there yet.
	bool force_free_play();
	bool free_play() const;
	// The coin/credit setting itself (0-based: 0, a coin a credit, is the factory's; 25 is FREE
	// PLAY), -1 when there are no settings yet; set_ is false when nothing changed.
	static constexpr int COIN_FACTORY = 0, COIN_FREE_PLAY = 0x19;
	int coin_setting() const;
	bool set_coin_setting(int v);
	// The test mode's GAME ASSIGNMENT, in the same block (0x3340-0x335c, with a CRC-16 of its own
	// at 0x3302), read as the game starts: reset() after a change. -1 when the game hasn't written
	// its settings yet; set_ is false when nothing changed. Difficulty: 0 EASY to 3 HARDEST (with
	// the energy and ring width that go with it, as the test mode sets them); rounds to win 2-5;
	// the switches 0 off, 1 on. version: 0 NORMAL, 1 VERSION 2.1. vs_finish: 1 ends the game for
	// both players after a versus match (0: the winner plays on).
	enum class game_assignment { match_1p, match_vs, difficulty, advertise_sound, continues, version, vs_finish };
	int assignment(game_assignment a) const;
	bool set_assignment(game_assignment a, int v);
	// The ring's width (1300-1600) whatever the difficulty, or 0 for the arcade's (by difficulty:
	// 1600 on EASY down to 1300 on HARDEST, the test mode's STAGE WIDTH). Written into the
	// settings (the game takes it from there as it starts); false when there are none yet.
	bool set_ring_width(int width);
	// Blanks the FREE PLAY notices (the attract's, bottom left, and the fight's FREEPLAY, top
	// right between PUSH START BUTTON): for the PC way of playing, always free play. Checked
	// against the data ROM; returns how many it found.
	int hide_free_play_text();
	void show_free_play_text();   // (puts back what hide_free_play_text changed: the arcade way)
	// The game's mode (0x50002b): 0x11 in its test mode, 3 in the attract, 9 in a fight
	u8 game_mode() const { return m_workram[0x2b]; }
	// The stage backdrop above 1x: softened and smoothly enlarged, or its pixels as blocks (the
	// arcade's, enlarged). Only in a fight (wide_frame()); the rest of the 2D stays sharp.
	void set_backdrop_filter(bool smooth) { m_backdrop_smooth = smooth; }
	bool backdrop_filter() const { return m_backdrop_smooth; }
	// The attract's SEGA logo is up (it sets bit 16 of the flags at 0x500068 every frame it
	// runs; clear again once the attract moves on): the first time, at about frame 728 from
	// power-on, the boot is over. Past the boot, the game's mode (game_mode()) is 3 or more.
	bool past_boot() const { return m_workram[0x6a] & 1; }
	// Work RAM survives reset(): after one that follows a run, this clears past_boot() and the
	// game's mode, as at power-on (the game sets them again as it boots).
	void forget_boot() { m_workram[0x6a] &= u8(~1); m_workram[0x2b] = 0; }
	// Replaces n bytes of the i960's program at addr with `to`, if the bytes from check to
	// check + check_n (around them) have the CRC-32 `crc`: VF2's program as it's known, only.
	bool patch_program(u32 addr, const u8 *to, size_t n, u32 check, size_t check_n, u32 crc);
	// The boot's WARNING screen waits 640 frames (11 s): this makes it 1. For the window only:
	// the frames after it are no longer MAME's. False if the program isn't the one known. Before
	// the boot reaches it (frame 85).
	bool shorten_warning();
	void restore_warning();   // (shorten_warning undone)
	// Secret characters: the player select's Akira (either player's) is the fighter with this
	// number instead: 9 Dural, 11 MUE and 12 KOJAC (Lion's body in Akira's moves, from the debug
	// menu), 0 Akira himself. The select shows the fighter's portrait with Akira's name and
	// profile. Takes effect as a cursor next moves onto him: set before the game starts (the
	// menu restarts it). False for another number, or if the program isn't the one known.
	bool set_secret_character(int fighter);

	// Milestones (TGP upload, IRQs, first scheduler dispatch...) as they happen, to stderr.
	std::function<void(const std::string &)> on_milestone;
	// The i960's traffic with the TGP: 'w' for a write to the function port / FIFO (with the
	// offset from 0x880000), 'r' for a value read back from the TGP's output FIFO.
	std::function<void(char, u32, u32)> on_copro;
	// Before each i960 instruction (tracing; slows the run down).
	std::function<void(u32 pc)> on_instruction;
	// Before each 68000 instruction (tracing)
	std::function<void(u32 pc)> on_68k_instruction;
	std::map<u32, int> m_unmapped;   // address (dword) -> count of accesses nobody answers

	// mame_bus32: the i960's program space
	u8 read_byte(offs_t a) override;
	u16 read_word(offs_t a) override;
	u32 read_dword(offs_t a) override;
	void write_byte(offs_t a, u8 v) override;
	void write_word(offs_t a, u16 v) override;
	void write_dword(offs_t a, u32 v) override;
	u16 flags(offs_t a) override;

	// Inputs, active low as on the cabinet (IN0: coins, service, start; IN1/IN2: players)
	u8 m_in0 = 0xff, m_in1 = 0xff, m_in2 = 0xff, m_sw = 0xff;

	// The sound board's output since the host last took it: stereo pairs at SOUND_RATE (the
	// SCSP's 22.5792 MHz / 512). Empty while the board is held (set_sound(false)).
	static constexpr int SOUND_RATE = 44100;
	std::vector<int16_t> m_audio_out;
	// The sound board running (68000, SCSP, the link both ways), or held halted as in the MAME
	// reference runs for the CPU/video regression (the default). Takes effect at reset().
	void set_sound(bool on) { m_sound_on = on; }
	bool sound() const { return m_sound_on; }
	// A board for its sound alone (online play's: the match's board runs with its sound held, the same on both
	// sides, and this one plays what it sends): only the 68000 and the SCSP run (the i960, the TGP and the Z80
	// never do), and the bytes for the sound board come from sound_bytes, a MIDI byte's time apart (320 us), as
	// the UART would send them. From the next reset() (with set_sound(true)).
	void set_sound_only(bool on) { m_sound_only = on; }
	void sound_bytes(const uint8_t *b, size_t n) { m_snd_queue.insert(m_snd_queue.end(), b, b + n); }
	size_t sound_bytes_waiting() const { return m_snd_queue.size(); }
	// The link to the sound board a byte at a time (for the host): each byte the UART sends reaches
	// the sound board's MIDI input when its stop bit would be read there, and the UART's status
	// changes when the bit-level transmitter would change it, without running the UART's 500 kHz
	// clock (a million timer events a second). The same bytes at the same moments; the CPUs' slices
	// of time are cut differently, so the game's timing can differ by microseconds. Off (the
	// default, vf2run): MAME's bit-level link, for the comparisons. Takes effect at reset().
	void set_byte_link(bool on) { m_byte_link = on; }
	// Separate volumes (gains, 1 = as the game mixes them) for the music and for the effects and
	// voices (voice_group in the .cpp says how VF2's sound program tells them apart). Not the
	// board's: a host's mixer. Left at 1, the sound is MAME's sample for sample.
	void set_volumes(float music, float effects);
	int voice_group() const;   // the group of the voice the 68000 is starting: 0 music, 1 effects
	// The host's overall level: a gain on m_audio_out, with a peak limiter so a loud moment never
	// clips. Left at 1, nothing is applied (vf2run's --wav stays MAME's).
	void set_level(float gain) { m_level = gain; }
	// The SCSP's own samples (stereo, -1..1, before its output gain), kept when m_keep_raw is set:
	// what MAME's Lua sound hook sees, for comparisons.
	bool m_keep_raw = false;
	std::vector<float> m_audio_raw;
	uint64_t m_midi_out_bits = 0;   // the SCSP's MIDI output (to the main board), bits seen

	// the sound board, for traces
	uint64_t snd_cycles() const;   // the 68000's cycles since power-on
	uint64_t scsp_samples() const { return m_scsp->samples_made(); }
	scsp_device &scsp() { return *m_scsp; }   // (made again at each reset())
	std::function<void(u8)> on_uart_tx;      // each byte the main board sends the sound board
	std::function<void(u32, u16, u16)> on_68k_scsp_write;   // the 68000's writes to the SCSP (offset, data, mask)
	m68000_device &snd_cpu() { return *m_68k; }
	const std::vector<u16> &soundram() const { return m_soundram; }

	// counters for the runner's report
	uint64_t m_irq_count[4] = {};
	uint32_t m_tgp_upload_words = 0;
	uint64_t m_tgp_instructions = 0;
	uint32_t m_uart_bytes = 0;

private:
	struct tgp_bus;
	friend struct tgp_bus;

	// Save states (save_state): the field list, run both ways; the device memories copied by the 4 KB pages
	// written since (each page's save number when last written). The RAMs the recompiled code writes directly,
	// and buffer RAM (the TGP's too), are copied whole.
	template <class IO> void state(IO &io);
	enum { P_TILE, P_CHAR, P_PAL, P_XLAT, P_FB_A, P_FB_B, P_TEX0, P_TEX1, P_LUMA, PAGED };
	static constexpr size_t PAGE = 4096;
	std::vector<uint32_t> m_page_gen[PAGED];
	uint32_t m_snap_id = 0, m_snap_epoch = 0, m_snap_gen = 1;
	uint64_t m_rom_hash = 0;
	void touch(int r, size_t byte) { m_page_gen[r][byte / PAGE] = m_snap_gen; }
	u8 *paged_base(int r);   // (where each is now: swap_2d swaps the vectors)
	size_t paged_bytes(int r) const;
	size_t fixed_state_size() const;
	void setup_pages();

	romset::result m_roms;
	const std::vector<u8> &m_rom, &m_data;
	i960_device m_cpu;
	std::unique_ptr<i960rec::runtime> m_rec;
	i960rec::native_stats m_native_stats;
	void map_plain_pages(i960rec::runtime &r);
	std::unique_ptr<tgp_bus> m_tgp;

	// memory
	std::vector<u8> m_ram200, m_workram, m_bufferram, m_backup, m_cpuctl;
	std::vector<u16> m_palram, m_colorxlat, m_fbvram_a, m_fbvram_b;
	std::vector<u32> m_tex0, m_tex1;
	std::vector<u8> m_lumaram, m_m2comm_share;
	segas24_tile m_tiles;
	std::mutex m_hud_mx;   // (the names are set by the host while a picture's drawn alongside)
	std::string m_hud_names[2];
	m2hud::Flag m_hud_flags[2];
	int m_hud_flag_at[2][2] = { { -1, -1 }, { -1, -1 } };

	// ---- MAME's scheduler (emu/schedule.cpp, diexec.cpp) for the i960 and the TGP ----
	using atime = unsigned __int128;   // attoseconds since power-on
	static constexpr atime AS_PER_SEC = 1000000000000000000ull;
	// in MAME's device order; the 68000 is held halted (as the reference runs it) and the
	// billboard's Z80 runs its blank ROM: NOPs, which it executes in steps of 2 cycles
	enum { CPU_I960, CPU_TGP, CPU_68K, CPU_Z80, N_CPUS };
	enum { SUSPEND_HALT = 1, SUSPEND_RESET = 2 };
	enum line_id {
		L_IRQ0, L_IRQ1, L_IRQ2, L_IRQ3, L_I960_HALT, L_TGP_HALT, L_TGP_RESET,
		L_68K_IRQ0,                            // + level: the 68000's interrupt lines (MAME's 0-7)
		N_LINES = L_68K_IRQ0 + 8
	};
	enum timer_id {
		T_UART,                                // the UART's 500 kHz clock: a toggle every 1 us
		T_VBLANK_START, T_VBLANK_END,
		T_TIMER0, T_TIMER1, T_TIMER2, T_TIMER3,
		T_IRQ_DELAY,                           // irq_mask_delayed_update
		T_FIN_EMPTY, T_FIN_FULL, T_FOUT_EMPTY, T_FOUT_FULL,   // the FIFOs' sync timers
		T_SCSP0,                               // + the SCSP's timers (scsp_device::TIMER_A...)
		T_SOUND_UPDATE = T_SCSP0 + int(scsp_device::N_TIMERS),     // the sound manager's 50 Hz update
		T_LINE0,                               // + line: an input line's synchronize()
		N_TIMERS = T_LINE0 + N_LINES
	};
	struct timer { atime expire = 0, start = 0, period = 0; uint64_t seq = 0; bool enabled = false; u32 param = 0; };
	struct cpu_sched
	{
		atime local = 0;
		atime period;          // attoseconds per cycle, as MAME computes it
		uint64_t cycles = 0;   // local / period (local only ever moves by whole cycles)
		int divshift = 0;
		u32 divisor = 0;
		u32 suspend = 0, nextsuspend = 0;
		bool eat = false, nexteat = false;
		void set_rate(uint64_t cycles_per_second);
	};
	timer m_timer[N_TIMERS];
	uint64_t m_timer_seq = 0;
	cpu_sched m_sched[N_CPUS];
	int m_order[N_CPUS] = { CPU_I960, CPU_TGP, CPU_68K, CPU_Z80 };
	bool m_suspend_changes = false;
	atime m_base = 0;
	int m_executing = -1;
	bool m_in_callback = false;
	atime m_callback_time = 0;
	std::vector<int> m_line_queue[N_LINES];
	uint64_t m_frame_number = 0;
	uint64_t m_tgp_cycle_base = 0;   // MAME counts the TGP's cycles from its last reset
	atime time() const;
	mutable int m_head = -1;          // the first timer to fire (cached; -1: work it out again)
	int timer_head() const;
	void timer_disable(int id);
	void timer_adjust(int id, atime delay, u32 param = 0, atime period = 0);
	int m_callback_timer = -1;        // the timer whose callback is running, and whether it changed it
	bool m_callback_modified = false;
	void abort_timeslice();
	void set_input_line(int line, int state);
	void apply_line(int line);
	void suspend(int cpu, u32 reason);
	void resume(int cpu, u32 reason);
	void apply_suspend_changes();
	void timeslice();
	void timer_fired(int id);

	// MAME's generic_fifo_u32_device, 8 deep, with model2_tgp_state's callbacks
	struct gen_fifo
	{
		std::deque<u32> values, extra;
		bool empty_triggered = false, full_triggered = false;
		bool is_full() const { return values.size() >= 8; }
	};
	gen_fifo m_fifo_in, m_fifo_out;    // i960 -> TGP, TGP -> i960
	u32 fifo_pop(bool out);
	void fifo_push(bool out, u32 v);
	void fifo_clear(bool out);
	void fifo_sync_empty(bool out);
	void fifo_sync_full(bool out);

	// IRQ controller and timers (MAME's model2_state)
	u32 m_intreq = 0, m_intena = 0;
	u32 m_timervals[4], m_timerorig[4];
	bool m_timerrun[4] = {};
	void irq_update();
	void timer_fire(int n);

	// coprocessor (TGP) glue
	u32 m_coproctl = 0, m_coprocnt = 0, m_geoctl = 0, m_geocnt = 0;
	u32 m_geo_write_start = 0;
	std::unique_ptr<model2_video> m_video;
	bool m_sound_on = false;
	bool m_sound_only = false;
	std::deque<uint8_t> m_snd_queue;   // (sound_only: the bytes to go to the sound board)
	atime m_snd_next = 0;              // (and when the next may)
	float m_volume[2] = { 1, 1 };
	float m_level = 1, m_limit = 1;   // set_level's gain; the limiter's gain now
	bool m_backdrop_smooth = false;
	std::vector<u32> m_bd_soft, m_bd_wide;   // the backdrop filter: the backdrop softened, then enlarged across
	void backdrop_smooth(int w, bool enlarge);
	std::vector<u32> m_bd_pad, m_side_px;    // render_parts': the backdrop padded; a 4:3 screen's sides
	int m_ring_width = 0;                   // set_ring_width's (0: by difficulty)
	bool m_draw_3d = true;   // the geometrizer and the 3D (m_geo_read_start_address is there)
	u32 m_videocontrol = 0, m_framenum = 0;
	bool m_render_test_mode = false, m_render_mode = false, m_render_unk = false;
	void copro_ctl1_w(u32 data, u32 mem_mask);
	void copro_fifo_w(u32 data);
	u32 copro_fifo_r();
	void copro_halt();
	void copro_boot();

	// 2D video
	std::array<uint32_t, 8192> m_pens{};
	bool m_palette_dirty = false;
	u8 m_gamma[256];
	// the screen; the tile layers: the backdrop (maps 2 and 3, opaque), the layers over it under the
	// 3D, the layers over the 3D
	bitmap_rgb32 m_screen, m_sys24, m_sys24_mid, m_sys24_top;
	u32 under_3d(int y, int x) const { const u32 p = m_sys24_mid.pix(y, x); return p ? p : m_sys24.pix(y, x); }
	void draw_picture(u32 *out, size_t stride, bool polygons, bool async, float alpha = -1, picture_parts *parts = nullptr,
		const gpu3d_hook *gpu = nullptr, bool also_cpu = false);
	// the frame's 2D state, kept when a picture of it is still held once the game runs on
	struct frame_2d {
		uint64_t frame = 0;
		bool kept = false;
		std::vector<u16> tile_ram, palram, colorxlat;
		std::shared_ptr<const std::vector<u16>> char_ram;
	};
	std::shared_ptr<frame_2d> m_cur_2d;               // the current frame's, while pictures of it are around
	std::shared_ptr<const std::vector<u16>> m_char_copy;   // the last copy of the tile graphics, and its version
	uint64_t m_char_copy_version = ~0ull;
	std::shared_ptr<const picture_desc> m_last_pic;
	void keep_2d();
	void swap_2d(frame_2d &f);   // the frame's 2D state in place of the live one (and back)
	void bd_pixels(int mode, double dh, double dv, int &dx, int &dy) const;
	void draw_2d(bool polygons, bool parts);
	bool m_2d_wide = false, m_2d_filter = false;   // what the last draw_2d drew
	uint64_t m_2d_version = 0;
	uint64_t m_2d_key[5] = { ~0ull };              // smooth motion: the frame, scale, width, filter and parts it was drawn for
	void compose(u32 *out, size_t stride, const bitmap_rgb32 *layer3d, int x0, int x1, bool smooth, u32 pen0, int bdx = 0, int bdy = 0);
	// Antialiasing's Low (set_edge_aa): the 3D's edges smoothed once it's on the picture beneath it
	bool m_edge_aa = false;
	std::vector<std::pair<u32, std::vector<u8>>> m_free_play_text;   // the data hide_free_play_text replaced
	std::vector<std::vector<std::pair<uint32_t, uint32_t>>> m_aa_changes;   // each band's blends (pixel, colour)
	u32 below_3d(int X, int Y, bool smooth, u32 pen0, int bdx, int bdy) const;
	void edge_smooth(u32 *px, size_t stride, const bitmap_rgb32 *layer3d, int x0, int x1, int r0, int r1, bool smooth, u32 pen0, int bdx, int bdy);
	// smooth motion: the backdrop's scroll each frame, and how far to move it for a moment
	struct scroll_sample { int64_t frame = -1; u16 h = 0, v = 0; };
	static constexpr int SCROLL_HIST = 16;
	scroll_sample m_scroll_hist[SCROLL_HIST];
	void record_scroll_input(uint64_t f);
	// the game's backdrop scroll's inputs each frame (smooth motion's backdrop): the camera's heading
	// and pitch as the scroll routine (0x24540) takes them, two frames behind, and what it wrote
	struct scroll_input { int64_t frame = -1; u16 heading = 0; int16_t pitch = 0; bool exact = false; };
	scroll_input m_scroll_in[SCROLL_HIST];
	u16 m_next_heading = 0; int16_t m_next_pitch = 0;
	// The 3D's RAMs changing: a deferred picture drawn, a picture being drawn finished, and with
	// smooth motion the change kept (m_ram_log)
	enum { RAM_XLAT, RAM_TEX0, RAM_TEX1, RAM_LUMA, RAM_PAL };
	void note_gpu_ram(int kind);
	void before_3d_ram_change(int kind, u32 offset, u32 old, u32 now);
	struct ram_change { uint64_t frame; u8 kind; u32 offset, old, now; };
	std::vector<ram_change> m_ram_log;   // the last frames' changes, oldest first
	void ram_log_apply(uint64_t frame, bool undo);
	bool m_job_reverts = false;          // the picture being drawn has those RAMs taken back: nothing may touch them
	void ram_guard() { if (m_job_reverts) render_wait(); }
	std::unique_ptr<background_job> m_job;   // render_async's
	work_stats m_stats;
	double m_moment3d = -1;
	bool m_job_active = false;
	double m_job_ms = 0;
	uint64_t m_job_waited = 0;
	int m_scale = 1;
	// widescreen
	float m_aspect = 4.0f / 3.0f;
	int m_ext = 0;
	void resize_screen();
	int m_culling = 0;
	// a 4:3 screen's sides in a wide picture (fill_sides)
	struct screen_art { std::string name; u32 print = 0; std::vector<u32> px; int w = 0, h = 0; };
	std::vector<screen_art> m_screens;
	std::vector<int> m_side_cols, m_side_seam;   // fill_sides: each screen column's picture column (-1: none), seam share
	// edge: the picture's columns beside the sides (the left's, then the right's: 384 each, the
	// picture's rows y * s). The right side's column X goes in out's column X - gap. colours: plain
	// colours put there, not drawn (returns 1); else drawn (2).
	int fill_sides(u32 *out, size_t stride, const u32 *edge, int gap = 0, u32 *colours = nullptr);
	void sides_into(u32 *out, size_t stride);   // fill_sides on a picture drawn whole
	void wall_sides(u32 *out, size_t stride, int gap);   // "TRY NEXT STAGE"'s
	std::vector<u8> m_culling_orig;   // the program's bytes the culling patches change, as they were
	struct panorama_record { int rows = 0; std::vector<u16> entries; };   // 256 columns x rows
	std::vector<panorama_record> m_panoramas;
	bool m_panoramas_read = false;
	int m_pan_last = -1;
	segas24_tile::panorama m_pan_view;
	void read_panoramas();
	const segas24_tile::panorama *find_panorama();
	void palette_w(offs_t offset, u16 data, u16 mem_mask);
	void set_pen(int i);

	// 315-5649 I/O and the 93C46 EEPROM (16-bit cells), MAME's eepromser
	u8 m_io_port[7], m_io_config = 0xff, m_io_mode = 0, m_io_analog = 0;
	bool m_ctrlmode = false;
	struct eeprom93c46
	{
		u16 cells[64];
		int cs = 0, clk = 0, di = 0;
		enum { IN_RESET, WAIT_FOR_START_BIT, WAIT_FOR_COMMAND, READING_DATA, WAIT_FOR_DATA, WAIT_FOR_COMPLETION } state = IN_RESET;
		bool locked = true;
		int bits = 0;
		u32 cmd_addr = 0, shift = 0, address = 0;
		enum { INVALID, READ, WRITE, ERASE, LOCK, UNLOCK, WRITEALL, ERASEALL } command = INVALID;
		uint64_t completion = 0, cs_rise = 0;
		void cs_w(int s, uint64_t now);
		void clk_w(int s, uint64_t now);
		int do_r(uint64_t now) const;
		void event(int ev, uint64_t now);
		void execute(uint64_t now);
		void execute_write(uint64_t now);
	} m_eeprom;
	u8 io_read(int offset);
	void io_write(int offset, u8 data);
	void eeprom_w(u8 data);
	u8 in0_r();

	// i8251 UART to the sound board: transmit side only, clocked as MAME's (500 kHz TxC)
	struct uart8251
	{
		enum { NEXT_MODE, NEXT_SYNC1, NEXT_SYNC2, NEXT_COMMAND } flags = NEXT_MODE;
		u8 status = 0x05, command = 0, mode = 0, tx_data = 0;
		int br_factor = 1;
		bool delayed_tx_en = false;
		int tx_bits_left = 0;          // bits of the current character still to shift out
		u16 tx_shift = 0;              // the character as sent: start bit, 8 data bits (LSB first), stop bit
		int txc_count = 0, txc = 0;    // TxC falling edges counted towards br_factor; the clock's level
		bool tx_enabled() const { return command & 1; }   // CTS held low by the board
		bool txrdy() const { return tx_enabled() && (status & 0x01); }
		bool rxrdy() const { return false; }
	} m_uart;
	void uart_write(int offset, u8 data);
	void uart_start_tx();
	void uart_clock_tick();
	void uart_transmit_clock();
	void uart_tx_ready_changed();
	void sound_ready();
	// set_byte_link: T_UART fires only when the transmitter would do something (a byte's arrival
	// at the sound board, the step after its stop bit, an idle step that changes something)
	bool m_byte_link = false, m_link_fast = false;   // asked for; on since reset()
	uint64_t m_link_mode_edge = 0;   // TxC's falling edge (2 us apart from time 0) the mode was written at
	atime m_link_t0 = 0;             // the current byte's start bit on the line
	bool m_link_delivered = false, m_link_in_step = false;
	u8 m_link_byte = 0;
	uint64_t link_edges(atime t) const;
	atime link_next_step(atime t) const;
	void link_schedule();
	void link_event();
	std::vector<u8> m_uart_log;

	// the sound board (MAME's model2_snd map, model2_scsp config)
	std::vector<u16> m_soundram;       // 512 KB shared by the 68000 and the SCSP, 16-bit words
	std::unique_ptr<scsp_device> m_scsp;
	struct snd_bus;
	std::unique_ptr<snd_bus> m_snd_bus;
	std::unique_ptr<m68000_device> m_68k;
	void sound_reset();
	void sound_update_tick();
	void uart_txd(int state);
	void take_audio();

	// M2COMM link board (no link)
	u8 m_m2comm_cn = 0, m_m2comm_fg = 0;

	// handlers by dword, for everything that isn't plain memory
	u32 dev_read(u32 a, u32 mem_mask);
	void dev_write(u32 a, u32 data, u32 mem_mask);
	void unmapped(u32 a, bool write);
	void milestone(const std::string &s) { if (on_milestone) on_milestone(s); }
};
