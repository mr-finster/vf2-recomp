// Desktop host: an SDL3 window with VF2's picture letterboxed in it, keyboard and gamepads for
// both players, pacing at the Model 2's 57.52 Hz (by the sound queue when there is sound, else
// the clock), the in-game menu (as vf1-recomp's), and the board's NVRAM (backup RAM and EEPROM)
// and the menu's settings kept in a data folder.
//
//   vf2 [vf2.zip] [--arcade] [--fullscreen | --window] [--no-sound] [--music N] [--effects N]
//       [--volume N] [--scale N] [--aspect fit|4:3|16:10|16:9|21:9] [--show-boot] [--no-scene-sync]
//       [--interp] [--roms <folder>]
//       [--host [--port N] | --join ADDRESS[:PORT]]   (online by address: a LAN or a VPN, as Online > Play by address)
//
// The game's i960 code is recompiled to native code from the player's ROM as it starts (src/rec,
// tens of milliseconds); --interp (or VF2_INTERP=1) runs it on MAME's interpreter instead, the
// reference it matches cycle for cycle.
//
// The game and the data folder (the saves, settings.ini, backgrounds/, Screenshots, log.txt):
// a zip named on the command line, with vf2_data beside it; else the player's own, roms/vf2.zip
// in their data folder, which is the program's own folder in portable mode (a file named
// portable.txt beside the program, or beside a Mac's .app: nothing is read or written anywhere
// else), else a "roms" folder's beside the program (the Steam Deck package keeps everything in
// its folder), else the source tree's roms/vf2.zip (from the current folder: vf2_data beside it,
// as before), else the system's (~/Library/Application Support, %APPDATA%, ~/.local/share:
// "Virtua Fighter 2 Recomp"). The player's own is asked for the first time (first_launch): a file
// picker for MAME's vf2.zip, checked, then copied into roms/. A ROM folder of the player's own
// (--roms <folder>, VF2_ROMS, or roms_folder = <folder> in settings.ini, in that order) is looked
// in first for vf2.zip, and never written to unless a set is picked while it has none. On a phone
// (Android) the player's own is the app's files folder, where the picker copies the zip; one put
// in the app's own folder on the shared storage instead (Android/data/<package>/files/roms, over
// USB or adb push) is played where it lies, no picker.
//
// The menu (Esc, or on a pad View tapped, L3 + R3 or the touchpad click) pauses the game and its sound: Display (render scale,
// antialiasing, the 3D's renderer: CPU, GPU or Auto, picture shape, background and screen filters, the frame rate, fullscreen,
// the on-screen controls), Sound
// (the overall, music and effects volumes), Game (PC or arcade play style, reset), Arcade
// settings (the game's own, from its test mode: a change restarts the game as the menu closes;
// and the service menu), Controls (each player's pad, keys and pad buttons) and Quit. Its choices
// are kept in settings.ini in the NVRAM folder ("key = value"), written when the menu closes;
// the command line wins over them.
//
// --music and --effects (0-100) set the volumes of the music and of the effects and voices. VF2
// plays its music through the SCSP's effect DSP (a stereo reverb) with many voices at once, and
// its hits and voices mostly dry. In the game's own mix (--music 100 --effects 100) the hits, the
// announcer and the crowd sit about level with the music's bed (MAME's SCSP, whose mirror of the
// DSP's MADRS wiped VF2's reverb, made the music a near-mono smear 8-11 dB louder: see
// src/machine/scsp.h). The default, music 50 and effects 100, puts them about 6 dB over it, as
// the earlier music 20 did over the louder smear, at the same overall loudness.
// The night stage's thunder goes with the music, as in the game (at the defaults about 2 dB under
// a typical hit, as before). With the volumes apart, a leveller evens out the effects
// (src/machine/scsp.h: a KO's last hit or a roar, 12 dB over the music's bed at the defaults
// without it, comes down to 8, a few dB over a jab).
// --volume (0-100, default 100) is the overall level. The board's output peaks well under full
// scale, more so with the music down, so the host raises it by LEVEL (at 100), with a limiter
// that keeps the loudest moments from clipping.
//
// The default is the PC way of playing: free play (the game's own coin setting #26, written into
// its settings in backup RAM) with its FREE PLAY notices blanked, so Start just starts, and no
// boot to sit through: the window opens on the attract's SEGA logo about half a second after
// launch, not 13 s later. The boot runs unseen and unheard at full speed, with its WARNING
// screen's 11 s countdown cut to one frame (model2a::shorten_warning: checked against the
// program); its frames still count (VF2_QUIT_AFTER). --arcade (or Game > Play style: Arcade)
// leaves the settings as they are (coins: 5 and 6) and shows the arcade's boot, as do
// --show-boot and VF2_SCRIPT (whose frame numbers are MAME's).
//
// --scale N (1-6) draws the 3D at N times the arcade's 496 x 384; 1 is the arcade's picture,
// exactly. The 2D (text, the HUD, the backdrops) is drawn at the arcade's resolution and enlarged
// with it, each pixel a square block: as sharp as the arcade's (Display > Background filter:
// Smooth softens and smoothly enlarges a fight's stage backdrop instead). Until one is chosen,
// the scale fits the window as it first opens (the scale whose picture is at least as tall as
// the window's, 4 at most); F5 steps through 1 to 6. The player's scale and antialiasing stay as
// set (the governor lowers them only in tests, VF2_GOVERN_CUTS; a 3D left to the CPU with the
// setting not CPU is drawn at 1x for the run, and says so).
//
// --aspect sets the picture's shape; F6 steps through them. The default, fit, follows the window's
// shape (between 4:3 and 80:9: five 16:9 screens side by side); the others keep theirs, with black bars. 4:3 is the arcade's
// screen. Wider, the fights (the attract's demos too) see more to the sides: the 3D's view opens
// out (the same vertical view, Hor+), the stage's backdrop panorama carries on and the scenery
// the game leaves out by its 4:3 view is drawn; the HUD and text stay in the middle 4:3. The other
// screens (title, select, versus, the ending) stay 4:3, between bars of their edge's colour when
// it's one colour (the SEGA logo's grey, a fade's), else black (model2a::set_aspect); the player
// select's sides are painted (assets/backgrounds/select.png; a select.png or .jpg in the NVRAM
// folder's backgrounds/ wins: the picture at any size, 384 lines high at 1x, the 4:3 in its
// middle, which is never drawn).
//
// Scene changes are shown clean (scene_sync.h): the 3D with the 2D backdrop it was set for (VF2
// sets the backdrop two frames after MAME draws the 3D: away from the fight itself the 3D is shown
// two frames late), and a new scene once it's whole. --no-scene-sync shows every frame as MAME
// draws it.
//
// Keys:  player 1: arrows, Z guard, X punch, C kick, 1 start
//        player 2: I J K L, B guard, N punch, M kick, 2 start (both remapped in Controls)
//        5 / 6 coins, F2 test, 9 service, Esc the menu, P pause, F3 reset, F4 free camera, F5 render scale,
//        F6 picture shape, F9 frame timings, F11 (or F, Alt+Enter) fullscreen, F12 a screenshot (Screenshots in
//        the NVRAM folder), Tab held: as fast as it goes
// Pads:  d-pad or left stick, X guard, A punch, B kick, Y guard+punch, RB punch+kick,
//        LB guard+kick, RT all three, Start (with --arcade: starts only with a credit, as the
//        cabinet's start button); Menu (View tapped by default), L3 + R3 or the touchpad click: the
//        menu (View held + Start too); a Coin key or button for each player (none by default; View
//        tapped is a coin only when it's set as Coin); View + LB: render scale;
//        View + RB: picture shape; View + R3: free camera. Pads come and go as they're plugged in; each player's pad and
//        buttons are chosen in Controls.
// Free camera (F4, View + R3, or Display > Free camera; never saved): the picture seen from a camera flown around the
//        fight, the game going on beneath it (P pauses it, the camera still flying), the players' own inputs kept
//        from the game meanwhile; the picture only (model2_video's freecam_*: never what the game reads). WASD and
//        the mouse (captured), E or Space up, Q or Ctrl down, Shift x4, Alt x0.25, the wheel the speed, the arrows
//        look too, R back to the game's camera; on a pad the left stick moves, the right looks, LT and RT down and
//        up, L3 held x4, LB and RB the speed, Y back to the game's camera. Offline and while watching a match, not
//        while playing one (it goes off as one starts). VF2 has no camera of its own to read: the game's camera's
//        move is worked out from the stage's pieces that sat still, and the free camera stays put in the world.
// Sticks: a joystick SDL doesn't know as a pad (a DirectInput arcade stick) is given a fighting stick's layout
//        (give_stick_layout); Controls > Player > Set up buttons makes one from its own presses (settings.ini's
//        pad_mapping); a gamecontrollerdb.txt in the data folder or beside the program is read too.
// Touch: on a touch screen (Android) the on-screen controls are drawn over the picture: a d-pad,
//        Guard, Punch and Kick, Start and a Menu button, into the same input bits as the keys and
//        pads (player 1). Display > On-screen controls: Off, Auto (while the player has no pad),
//        or On. Android's Back button opens the menu too (SDL_ANDROID_TRAP_BACK_BUTTON keeps it
//        from closing the game); the menu itself is tapped (SDL's own touch to mouse).
// Sound: on Android it's SDL's own AAudio stream of the SCSP's own 44100 Hz 16-bit stereo (SDL
//        resamples it to whatever the device runs at, usually 48000), marked as a game's (the
//        stream role hint: AAUDIO_USAGE_GAME). It pauses with the app going to the background, and
//        follows the phone's output as it changes (headphones plugged, Bluetooth).
//
// Testing without a display:
//   VF2_HIDDEN=1         no window on screen, no sound out (the board still runs)
//   VF2_INTERP=1         the i960 on MAME's interpreter (as --interp)
//   VF2_QUIT_AFTER=N     quit after N game frames, writing build/host_last.png (the frame) and
//                        build/host_window.png (the window as drawn); such runs save nothing
//   VF2_SCRIPT=file      inputs from an input script (src/host/input_script.h), frame by frame
//                        from the boot; its shots go to build/host_<name>.png
//   VF2_WINDOW=WxH       the window's size in points (a hidden one as large as a player's screen)
//   VF2_DATA=dir         the data folder (for the player's own game, the one holding roms/vf2.zip)
//   VF2_PACE_LOG=1       the pacing every 60 frames (time, sound queued), the boot's time, and
//                        the time each frame takes to run and draw, and scene sync's state (every 2 s)
//   VF2_GOVERN=1         the render scale governor in a test run too (else only on screen)
//   VF2_TEST_LOAD=ms:s   each pass ms longer a frame (at 3x; by the scale's square) for the
//                        first s seconds on show (the governor's tests: a slow start); VF2_SCREEN_HZ=n: the governor's screen as n Hz
//   VF2_NO_VSYNC=1       vsync off, as a driver's settings can force it (smooth pictures then 120 a second)
//   VF2_NO_EMU_THREAD=1  every frame run on the main thread (else, with smooth motion, the next one
//                        runs on the game's own thread during the present's wait for vsync)
//   VF2_THREADS=N        threads drawing the 3D (default: the cores less 1, 12 at most)
//   VF2_KEYS=k@F,...     presses key k (an SDL key name: F5, F6, F11, Escape...) when game frame F
//                        is reached, e.g. VF2_KEYS=F5@300,F11@400; its window as drawn 3 frames
//                        later goes to build/host_window_F.png
//   VF2_MENU_KEYS=k,...  presses these keys (esc, up, down, left, right, enter, or SDL key names)
//                        6 passes of the loop apart from pass 30: for the menu, which stops the
//                        game's frames
//   VF2_HOST_ROOM=1      hosts a room code's match at pass 60 (the code in log.txt), as Online > Create room
//   VF2_JOIN_CODE=XXXXXX joins that room at pass 60 (VF_LOBBY: another lobby); VF2_RANDOM=1 Quick match
//   VF2_WATCH_CODE=XXXXXX watches that match at pass 60, as Online > a room > Watch
//   VF2_HOST_PUBLIC=1    VF2_HOST_ROOM's room listed in Rooms (its matches may be watched by its code either way)
//   VF2_TEST_FREECAM=... the free camera at game frames, e.g. on@600,yaw@620=180,pitch@640=-30,move@660=0,2,-5,
//                        pause@700,yaw@720=90,shot@730=name,off@800: on, off, reset; a turn (degrees: yaw to the right,
//                        pitch up) or a move (forward, right, up; in rings' widths) at once; pause, resume (P);
//                        shot: the picture on show then to build/host_fc_<name>.png. The steps' clock is the game's
//                        frames, and a pass of the loop for each pass while the game's stopped (paused)
//   VF2_TEST_FREECAM_PATH=f the free camera flown along the path in f (free_cam_path.h: keys, orbits round the
//                        fighters, pause and unpause, on the same clock as VF2_TEST_FREECAM's), for footage with
//                        VF2_RECORD (a recording goes on through a pause: a picture each pass, the sound silent
//                        meanwhile). VF2_TEST_FREECAM_PATH_LOG=1: every 30 ticks where the game's camera is round the
//                        fighters, as a path's "rel" key has it (for writing one)
//   VF2_RECORD=dir       a recording (a trailer's footage): from game frame VF2_RECORD_FROM (default 0; before it as
//                        fast as it goes, its pictures not drawn) to VF2_RECORD_TO, 60 pictures a second by the
//                        game's clock, not the real one (smooth motion's in-betweens), the window's whole picture to
//                        dir/video.mp4 (by ffmpeg) and the sound to dir/sound.wav, in step (through a pause too: a
//                        picture each pass, silence for its time). Use with VF2_HIDDEN and VF2_WINDOW; the game's
//                        thread is off meanwhile. VF2_RECORD_HZ=n: n pictures a second instead
//   VF2_FREECAM_LOG=1    the free camera's working out: the world's up as it comes on, each frame's camera move (the
//                        still pieces it's from), its place as it's flown
//   VF2_SHOT_AT=n,...    the window as drawn at these passes, to build/host_window_at_N.png
//   VF2_TEST_SAVES=1     a test run (VF2_QUIT_AFTER) saves the NVRAM and settings.ini all the same
//   VF2_KO=P@a[-b]       knocks player P out in every round from game frame a (to b), as vf2run's
//                        --ko: with the scripted game, KOs, replays, the winner and the next match
//   VF2_PICTURE_LOG=f    a line a pass: where each picture's time went (see picture_log)
//   VF2_3D=cpu|gpu|auto  the 3D's renderer (Display > 3D renderer: see gpu3d_state), VF2_GPU3D_FAST=1
//                        its fast quality; VF2_GPU3D_CHECK=n every n-th GPU picture checked against the
//                        CPU's: its 3D, and the picture as put together for the screen against the
//                        CPU's way's (read at once, or every other one copied by the renderer and read
//                        some passes later; half of them drawn without the CPU's 3D alongside), the
//                        differing ones to VF2_GPU3D_CHECK_DIR; VF2_GPU3D_SLOW=n the GPU's drawing
//                        done n times over (a slow GPU); VF2_TEST_GPU_LOAD=n the renderer's own GPU
//                        work made slow (n extra large draws a pass);
//                        VF2_AUTO_RECHECK=pass,... Auto's check again (VF2_AUTO_ANYTIME=1: at once)
//   VF2_STAGE=n          the fights' stage (0-11), as the scene tools force it
//   VF2_FLICKER=dir      each new picture shown read back against the two before it: pixels that
//                        change and change back, per picture in dir/flicker.txt (VF2_FLICKER_FROM=frame,
//                        VF2_FLICKER_MIN=pixels: the worst saved from then)
//   VF2_PAD_KEYS=...     virtual pads: "plug@N" attaches one at pass N, "unplug@N" takes the last
//                        away, "P:buttons@N[/H]" holds pad P's buttons (SDL names joined by '+',
//                        lefttrigger and righttrigger too) from pass N for H passes (default 4)
//   VF2_TEST_VJOY=...    a virtual stick SDL doesn't know (10 buttons, a hat, 2 axes), pressed at these passes of
//                        the loop: b3@200 its button 3, h1@260 its hat (1 up, 2 right, 4 down, 8 left), +0@300 or
//                        -1@300 an axis to one end (each 8 passes), q@900 quit (a run that saves), p@100 plugged
//                        in then (not at the start); "1": no presses. Player 1's controls logged as they change
//   VF2_TEST_RAM_LOG=f   the game's work RAM hashed every 30 game frames to f (a display setting changes no frame)
//   VF2_TEST_CHIME_WAV=f the match found chime's sound (the game's coin, made at the start) to f, a .wav
#include "version.h"      // VF2_VERSION, VF2_DATE (made by the Makefile from git)
#include "../machine/model2a.h"
#include "../machine/parallel.h"
#include "romset.h"
#include "png.h"
#include "input_script.h"
#include "free_cam_path.h"
#include "scene_sync.h"
#include "backgrounds.h"
#include "gpu_picture.h"
#include "../gpu/gpu3d.h"
#include "menu.h"
#include "crt_colours.h"
#include "online.h"
#include "links.h"
#include "text.h"
#include "chime.h"
#include "coin_sound.h"
#include <future>
#include "flags.h"
#include "controls.h"
#include "touch_controls.h"
#include "sysinfo.h"
#include <SDL3/SDL.h>
#ifdef __APPLE__
#include <pthread.h>
#endif
#if defined(_WIN32) || defined(__ANDROID__)
#include <SDL3/SDL_main.h>   // WinMain on Windows; the SDLActivity's entry point on Android
#endif
#ifdef _WIN32
// A laptop with two graphics chips (NVIDIA Optimus, AMD switchable graphics): the game on the faster one, not the one
// in the processor, whatever Windows would pick. The drivers look for these two in the program's exports.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif
#include <algorithm>
#include <atomic>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

// Every message box goes through here: a test run (VF2_HIDDEN) never shows one (nor a file picker),
// the message goes to stderr (log.txt without a console) instead.
// The volumes an online match's own sound board plays at (the overall, the music, the effects: 0-100 each, 8 bits
// apiece): set by the Sound page, taken by the match sound's thread before each frame (the match's board is the
// other one, unheard)
static std::atomic<uint32_t> g_match_volumes{ 0 };
static uint32_t pack_volumes(int volume, int music, int effects) { return uint32_t(volume) | uint32_t(music) << 8 | uint32_t(effects) << 16; }

static bool message_box(SDL_MessageBoxFlags flags, const char *title, const std::string &msg)
{
	if (getenv("VF2_HIDDEN")) { fprintf(stderr, "(hidden: no dialog) %s\n", msg.c_str()); return false; }
	return SDL_ShowSimpleMessageBox(flags, title, msg.c_str(), nullptr);
}

namespace {

constexpr double FRAME_HZ = 16000000.0 / (656.0 * 424.0);   // 57.52 Hz, MAME's Model 2 screen
constexpr float LEVEL = 2.8f;   // --volume 100: +9 dB over the board's output
constexpr int MAX_SCALE = 6, DEFAULT_MAX_SCALE = 4;
#if defined(__ANDROID__)
// Android ships the menu, its font and the painted sides inside the APK, where only SDL's own
// reading reaches them (the game's, and RmlUi's, is plain fopen). Copy the tree once into the data
// folder and use that: <data>/assets.
// SDL names an APK asset the way the APK's own tree does, *inside* its assets folder: the menu is
// "ui/menu.rml" while the zip entry is "assets/ui/menu.rml". With the "assets/" prefix SDL would
// name a folder of the data folder instead — and SDL_EnumerateDirectory maps a relative path to
// <files>/ first, so it would list that (empty) folder and never look in the APK at all. So: no
// prefix, and never the name of the folder being copied into.
static const char *APK_ASSET_DIRS[] = { "ui", "fonts", "backgrounds" };

namespace {

// A step of the copy that failed, with the message the failing call left (SDL sets none on some).
// The copy's own message (below) tells only that the whole thing failed: which of the steps did is
// what these lines are for — a phone's logcat is of the other end (the app's own log.txt).
void asset_failed(const char *what, const std::string &path, const char *why)
{
	fprintf(stderr, "assets: %s %s: %s\n", what, path.c_str(), why ? why : "SDL set no error");
}

struct asset_copy { std::string from, to; bool ok = true; int entries = 0; };
bool copy_asset_dir(const std::string &from, const std::string &to);

// SDL guarantees an enumerated dirname ends with a separator ("ui/", SDL_filesystem.h:332): joining
// with a slash of our own makes "ui//menu.rml", which a filesystem forgives but the APK's
// AAssetManager takes literally — it isn't the zip's "ui/menu.rml", so the read failed and nothing
// was ever copied. Join to exactly one separator.
std::string asset_join(const char *dir, const char *name)
{
	std::string d = dir ? dir : "";
	if (!d.empty() && d.back() != '/') d += "/";
	return d + (name ? name : "");
}

SDL_EnumerationResult copy_asset_entry(void *ud, const char *dir, const char *name)
{
	auto *c = static_cast<asset_copy *>(ud);
	const std::string src = asset_join(dir, name), dst = asset_join(c->to.c_str(), name);
	c->entries++;
	SDL_PathInfo info;
	if (!SDL_GetPathInfo(src.c_str(), &info)) { asset_failed("stat", src, SDL_GetError()); c->ok = false; return SDL_ENUM_CONTINUE; }
	if (info.type == SDL_PATHTYPE_DIRECTORY) {
		if (!copy_asset_dir(src, dst)) c->ok = false;
		return SDL_ENUM_CONTINUE;
	}
	size_t n = 0;
	void *d = SDL_LoadFile(src.c_str(), &n);
	if (!d) { asset_failed("read", src, SDL_GetError()); c->ok = false; return SDL_ENUM_CONTINUE; }
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(dst).parent_path(), ec);
	if (FILE *f = fopen(dst.c_str(), "wb")) {
		const bool wrote = fwrite(d, 1, n, f) == n;
		const bool closed = fclose(f) == 0;
		if (!wrote || !closed) { asset_failed("write", dst, "the write came up short"); c->ok = false; }
	} else { asset_failed("write", dst, strerror(errno)); c->ok = false; }
	SDL_free(d);
	return SDL_ENUM_CONTINUE;
}

bool copy_asset_dir(const std::string &from, const std::string &to)
{
	std::error_code ec;
	std::filesystem::create_directories(to, ec);
	asset_copy c{ from, to };
	// (a folder the APK hasn't got is a failure, not an empty copy that reports success; and a
	// listing that came out empty is the data folder's own folder, not the APK's)
	if (!SDL_EnumerateDirectory(from.c_str(), copy_asset_entry, &c)) { asset_failed("listing", from, SDL_GetError()); c.ok = false; }
	else if (!c.entries) { asset_failed("listing", from, "empty: the data folder's own, not the APK's?"); c.ok = false; }
	return c.ok;
}

// The APK's assets into <data>/assets: the whole tree, and again when the game itself changed (a
// new APK's menu, fonts or painted sides). The stamp is the version the copy was made of. True when
// the copy is in place afterwards; it does the talking itself.
bool copy_apk_assets(const std::string &dir)
{
	std::error_code ec;
	const std::string want = std::string(VF2_VERSION) + " " + VF2_DATE, stamp = dir + "/.version";
	char have[128] = { 0 };
	if (FILE *f = fopen(stamp.c_str(), "rb")) {
		const size_t n = fread(have, 1, sizeof have - 1, f);
		fclose(f);
		if (n && want == have && std::filesystem::is_regular_file(dir + "/ui/menu.rml", ec))
			return true;   // (this version's copy is already there)
	}
	bool ok = true;
	for (const char *d : APK_ASSET_DIRS) ok = copy_asset_dir(d, dir + "/" + d) && ok;
	if (ok && !std::filesystem::is_regular_file(dir + "/ui/menu.rml", ec)) {   // (the menu's own file: what the copy is for)
		asset_failed("verify", dir + "/ui/menu.rml", "not there after the copy");
		ok = false;
	}
	if (ok)
		fprintf(stderr, "assets: the APK's ui/, fonts/ and painted sides copied into %s\n", dir.c_str());
	else
		fprintf(stderr, "assets: the APK's ui/, fonts/ or painted sides couldn't be copied into %s: the menu has no files and the sides stay unpainted (the steps that failed are above)\n", dir.c_str());
	if (ok)
		if (FILE *f = fopen(stamp.c_str(), "wb")) {   // (the stamp is a convenience: without it the copy is simply made again)
			if (fwrite(want.data(), 1, want.size(), f) != want.size())
				fprintf(stderr, "assets: the copy's stamp couldn't be written (%s)\n", stamp.c_str());
			fclose(f);
		}
	return ok;
}

} // namespace
#endif

// The menu's files and the shipped backgrounds: beside the program, two up from it (build/bin in
// the source tree), or the current folder's. Android: the APK's, copied beside the data (above).
std::string find_assets(const std::string &data)
{
#if defined(__ANDROID__)
	if (!data.empty()) {
		const std::string dir = data + "/assets";
		if (copy_apk_assets(dir)) return dir;
	}
#else
	(void)data;
#endif
	const char *base = SDL_GetBasePath();
	const std::string b = base ? base : "";
	for (const std::string &d : { b + "assets", b + "../../assets", std::string("assets") })
		if (FILE *f = fopen((d + "/ui/menu.rml").c_str(), "rb")) { fclose(f); return d; }
	return "assets";
}
// A folder as the player writes it: "~" (or "~/...") is their home folder; quotes around it are
// dropped; a relative one is taken from `base` (settings.ini's folder) or the current folder.
std::string player_folder(std::string f, const std::string &base = std::string())
{
	while (!f.empty() && (f.back() == ' ' || f.back() == '\t')) f.pop_back();
	if (f.size() >= 2 && (f[0] == '"' || f[0] == '\'') && f.back() == f[0]) f = f.substr(1, f.size() - 2);
	if (f.empty()) return f;
	if (f[0] == '~' && (f.size() == 1 || f[1] == '/' || f[1] == '\\')) {
		const char *h = getenv("HOME");
		if (!h || !*h) h = getenv("USERPROFILE");
		if (h && *h) f = std::string(h) + f.substr(1);
	}
	std::filesystem::path p(f);
	std::error_code ec;
	if (p.is_relative()) p = base.empty() ? std::filesystem::absolute(p, ec) : std::filesystem::path(base) / p;
	p = p.lexically_normal();
	if (!p.has_filename() && p.has_relative_path()) p = p.parent_path();   // (no separator at the end)
	return p.string();
}

// The program's folder, with a separator at the end: a Mac app's is the folder the .app is in (SDL's
// base path in an app is its Contents/Resources)
std::string program_folder()
{
	const char *base = SDL_GetBasePath();
	std::string b = base ? base : "";
	const std::string in_app = ".app/Contents/Resources/";
	if (b.size() > in_app.size() && b.compare(b.size() - in_app.size(), in_app.size(), in_app) == 0) {
		b.resize(b.size() - in_app.size());   // (".../Virtua Fighter 2 Recomp")
		b.resize(b.find_last_of('/') + 1);
	}
	return b;
}
// Portable mode, as the player asks for it: a file named portable.txt beside the program, or in the
// folder above it (its name as found, else null). Any contents; "portable" and Windows' portable.txt.txt (a New Text Document
// renamed with its extension hidden) count too.
const char *portable_marker(const std::string &folder)
{
	if (folder.empty()) return nullptr;
	// beside the program, or the folder above it: the download unzips into a folder of its own,
	// and a portable.txt put in the folder it was unzipped to ("the main folder") went unseen
	// (2026-10-16, a player's report). Looked for with SDL (UTF-8 names, as on a drive whose
	// folders have accents), not std::filesystem (Windows' code page there).
	std::string up = folder;
	while (!up.empty() && (up.back() == '/' || up.back() == '\\')) up.pop_back();
	const size_t cut = up.find_last_of("/\\");
	up = cut == std::string::npos ? std::string() : up.substr(0, cut + 1);
	static const char *const NAMES[] = { "portable.txt", "portable", "portable.txt.txt" };
	static const char *const BESIDE[] = { "portable.txt beside the program", "portable beside the program", "portable.txt.txt beside the program" };
	static const char *const ABOVE[] = { "portable.txt in the folder above", "portable in the folder above", "portable.txt.txt in the folder above" };
	for (int above = 0; above < 2; above++) {
		const std::string &dir = above ? up : folder;
		if (dir.empty()) continue;
		for (int k = 0; k < 3; k++) {
			SDL_PathInfo info;
			if (SDL_GetPathInfo((dir + NAMES[k]).c_str(), &info) && info.type == SDL_PATHTYPE_FILE) return above ? ABOVE[k] : BESIDE[k];
		}
	}
	return nullptr;
}
// The picture's shapes, as --aspect names them and F6 steps through them (vf1-recomp's): 0 follows
// the window's shape
const float ASPECTS[] = { 0.0f, 4.0f / 3.0f, 16.0f / 10.0f, 16.0f / 9.0f, 64.0f / 27.0f };
const char *const ASPECT_NAMES[] = { "fit", "4:3", "16:10", "16:9", "21:9" };
constexpr int N_ASPECTS = 5;
// The boot unseen: at most this many frames (a board that never reaches the attract, e.g. in its
// test mode, is shown as it is)
constexpr int BOOT_FRAMES = 1200;

// The board's input bits (active low on the board; set here when pressed)
enum : uint8_t {
	IN0_COIN1 = 0x01, IN0_COIN2 = 0x02, IN0_TEST = 0x04, IN0_SERVICE = 0x08,
	IN0_START1 = 0x10, IN0_START2 = 0x20,
	PL_PUNCH = 0x01, PL_KICK = 0x02, PL_GUARD = 0x04,
	PL_DOWN = 0x10, PL_UP = 0x20, PL_RIGHT = 0x40, PL_LEFT = 0x80,
};

// The cabinet's own buttons (the players' are in controls.h, set in the menu)
struct key_bind { SDL_Scancode key; int port; uint8_t bit; };
const key_bind SYS_KEYS[] = {
	{ SDL_SCANCODE_5, 0, IN0_COIN1 }, { SDL_SCANCODE_6, 0, IN0_COIN2 },
	{ SDL_SCANCODE_F2, 0, IN0_TEST }, { SDL_SCANCODE_9, 0, IN0_SERVICE },
};

// The PC way (the default; --arcade for the cabinet's): no boot, always free play with nothing
// said about it, and Start simply starts.
bool g_pc_mode = true;
bool g_match_board = false;   // (an online match's board: the host's settings and the match's program, none of this player's)
// Arcade settings > Stage select (issue #33): a versus's first fight after Start (a challenger in at the select, or
// both players in from Start) on the stage chosen, the game's own order on from it, as online's random stages: 1-9
// the versus order's stages after its first (0, the game's own), 10 Dural's. Its stage comes from the game's count of
// fights (vf2net::FIGHTS), which the select's first frame loads the stage's textures from (mode 7: online's -38 note),
// so the count and the stage (vf2net::STAGE) are set the frame before (mode 6), the game's first fight still to come:
// the count 0, no one has won (vf2net::STREAK), the 1P ladder's fight number (LADDER, from 1) 1. A 1P game's fights
// don't come from the count (the CPU's order, its stages theirs): there it's put back to 0 as the fight starts, the
// game as ever (a challenger later in it on the game's own stage). Offline only: never on an online match's board.
static void stage_select(model2a &m, int stage)
{
	constexpr uint32_t LADDER = 0x54;
	auto &ram = const_cast<std::vector<u8> &>(m.workram());
	static bool set = false;   // (the count set, for this fight: the game's own again in a 1P game)
	if (set && m.game_mode() == 9) {   // (the fight: a versus, or 1P)
		if (ram[vf2net::VERSUS] == 0 && ram[vf2net::FIGHTS] == u8(stage)) ram[vf2net::FIGHTS] = 0;
		set = false;
	}
	if (stage <= 0 || m.game_mode() != 6 || ram[vf2net::FIGHTS] != 0 || ram[vf2net::STREAK] != 0 || ram[LADDER] != 1) return;
	ram[vf2net::FIGHTS] = ram[vf2net::STAGE] = u8(stage);
	set = true;
	fprintf(stderr, "Arcade settings > Stage select: a versus from this select on stage %d%s\n", stage + 1, stage == vf2net::STAGES ? " (Dural's)" : "");
}

struct pad_state
{
	bool back_was = false, back_combo = false, start_was = false;
	bool menu_was = false, touch_was = false, sticks_was = false;   // the menu's other buttons (edges)
	bool start_quiet = false;   // Start held as the menu closed (it closed it): not the game's till let go
	int coin_frames = 0, start_delay = -1, start_frames = 0;
};

enum class hotkey { none, menu, scale, aspect };

// The game's test menu, from ours: the test button pressed for a moment once the game is past its
// boot, then, while the test menu is up, Down is the service button (move) and Start the test
// button (select). The test menu counts as up when the test button was pressed in the last 3 s
// and the game's mode has been 0x11 (its test mode) for half a second.
int g_test_frames = 0;
bool g_test_pending = false;     // the menu's Service menu: pressed once the game is past its boot (a restart first)
bool g_left_test_menu = false;   // set when it closes (the PC way then restarts past the boot)

// The pressed bits of IN0, IN1 and IN2 for the next frame, from the keyboard and the pads, and the
// hotkeys pressed on a pad
hotkey read_inputs(model2a &m, SDL_Gamepad *pads[2], pad_state ps[2], const player_controls ctl[2], uint8_t in[3],
				   touch_controls *overlay)
{
	static const uint8_t BITS[C_COUNT] = {
		PL_UP, PL_DOWN, PL_LEFT, PL_RIGHT, PL_GUARD, PL_PUNCH, PL_KICK,
		PL_GUARD | PL_PUNCH, PL_PUNCH | PL_KICK, PL_GUARD | PL_KICK, PL_GUARD | PL_PUNCH | PL_KICK, 0, 0 };
	in[0] = in[1] = in[2] = 0;
	const bool *ks = SDL_GetKeyboardState(nullptr);
	for (const auto &k : SYS_KEYS)
		if (ks[k.key]) in[k.port] |= k.bit;
	auto key = [&](SDL_Scancode k) { return k != SDL_SCANCODE_UNKNOWN && ks[k]; };
	auto keys = [&](const player_controls &c, int a) { return key(c.key[a]) || key(c.key2[a]); };   // (either of a control's keys)
	hotkey hk = hotkey::none;
	static bool key_start_was[2], key_coin_was[2], pad_coin_was[2], key_menu_was[2];
	for (int p = 0; p < 2; p++) {
		pad_state &st = ps[p];
		// Start is the cabinet's start button: in the arcade way it starts only with a credit (coins
		// come from the Coin binding or the 5 and 6 keys)
		auto press_start = [&] { st.start_delay = 0; };
		// the on-screen controls (a touch screen) are player 1's
		if (p == 0 && overlay) {
			static const struct { int at; uint8_t bit; } TOUCH_BITS[] = {
				{ touch_controls::UP, PL_UP }, { touch_controls::DOWN, PL_DOWN },
				{ touch_controls::LEFT, PL_LEFT }, { touch_controls::RIGHT, PL_RIGHT },
				{ touch_controls::GUARD, PL_GUARD }, { touch_controls::PUNCH, PL_PUNCH },
				{ touch_controls::KICK, PL_KICK } };
			for (const auto &t : TOUCH_BITS)
				if (overlay->held(t.at)) in[1] |= t.bit;
			if (overlay->take_start()) press_start();
			if (overlay->take_menu()) hk = hotkey::menu;
		}
		// a key set as Menu opens it too (Esc always does)
		const bool kmenu = keys(ctl[p], C_MENU);
		if (kmenu && !key_menu_was[p]) hk = hotkey::menu;
		key_menu_was[p] = kmenu;
		for (int a = 0; a < C_START; a++)
			if (keys(ctl[p], a)) in[1 + p] |= BITS[a];
		const bool kstart = keys(ctl[p], C_START);
		if (kstart && !key_start_was[p]) press_start();
		key_start_was[p] = kstart;
		// Start held reaches the game too, the arcade way, as the cabinet's button: a fighter chosen with
		// Start held is Dural (version 2.1's code). The PC way, a 6-frame press, as before v0.1.12: on a
		// pad Start confirms and closes the menu, and holding it turned a pick into Dural by accident
		bool start_held = kstart, start_now = kstart;
		// Coin (a cabinet's coin slot, bound in the menu): a coin for this player, the arcade way
		// (the PC way is free play: the game takes no coins)
		const bool kcoin = keys(ctl[p], C_COIN);
		if (kcoin && !key_coin_was[p] && !g_pc_mode) st.coin_frames = 6;
		key_coin_was[p] = kcoin;

		const uint8_t coin_bit = p ? IN0_COIN2 : IN0_COIN1, start_bit = p ? IN0_START2 : IN0_START1;
		if (SDL_Gamepad *g = pads[p]) {
			uint8_t &b = in[1 + p];
			auto btn = [&](SDL_GamepadButton x) { return SDL_GetGamepadButton(g, x); };
			const int lx = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
			const int dead = 12000;
			const bool back = btn(SDL_GAMEPAD_BUTTON_BACK);
			const bool start = ctl[p].pad_either(g, C_START);
			// Coin and Menu on View are its tap (below), so View + LB and the like stay hotkeys
			const int coin_code = ctl[p].pad[C_COIN], menu_code = ctl[p].pad[C_MENU];
			const bool pcoin = (coin_code != SDL_GAMEPAD_BUTTON_BACK && player_controls::pad_down(g, coin_code)) || player_controls::pad_down(g, ctl[p].pad2[C_COIN]);
			if (pcoin && !pad_coin_was[p] && !g_pc_mode) st.coin_frames = 6;
			pad_coin_was[p] = pcoin;
			const bool pmenu = (menu_code != SDL_GAMEPAD_BUTTON_BACK && player_controls::pad_down(g, menu_code)) || player_controls::pad_down(g, ctl[p].pad2[C_MENU]);
			if (pmenu && !st.menu_was && !back) hk = hotkey::menu;   // (View held: LB and RB are its hotkeys)
			st.menu_was = pmenu;
			// L3 + R3 and the touchpad click (a PlayStation pad's) always open the menu as well, so a
			// pad without a View button, or with Menu moved, can't be locked out of it
			const bool sticks = btn(SDL_GAMEPAD_BUTTON_LEFT_STICK) && btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK);
			if (sticks && !st.sticks_was) hk = hotkey::menu;
			st.sticks_was = sticks;
			bool touch_bound = false;
			for (int a = C_GUARD; a < C_COUNT; a++) touch_bound |= ctl[p].pad[a] == SDL_GAMEPAD_BUTTON_TOUCHPAD || ctl[p].pad2[a] == SDL_GAMEPAD_BUTTON_TOUCHPAD;
			const bool touch = !touch_bound && btn(SDL_GAMEPAD_BUTTON_TOUCHPAD);
			if (touch && !st.touch_was) hk = hotkey::menu;
			st.touch_was = touch;
			if (btn(SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -dead) b |= PL_UP;
			if (btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > dead) b |= PL_DOWN;
			if (btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -dead) b |= PL_LEFT;
			if (btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > dead) b |= PL_RIGHT;
			if (back) {
				// View held: hotkeys on Start, LB and RB, not moves
				const bool pstart = btn(SDL_GAMEPAD_BUTTON_START);
				const bool lb = btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), rb = btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
				if (pstart && !st.start_was) { hk = hotkey::menu; st.back_combo = true; }
				static bool lb_was[2], rb_was[2];
				if (lb && !lb_was[p]) { hk = hotkey::scale; st.back_combo = true; }
				if (btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) st.back_combo = true;   // (View + R3: the free camera's, read each pass)
				if (rb && !rb_was[p]) { hk = hotkey::aspect; st.back_combo = true; }
				lb_was[p] = lb; rb_was[p] = rb;
			} else {
				for (int a = C_GUARD; a < C_START; a++)
					if (ctl[p].pad_either(g, a)) b |= BITS[a];   // (not Start nor Coin)
				if (start && !st.start_was) press_start();
				start_held |= start;
				start_now |= start;
			}
			// View tapped on its own: whichever of Menu and Coin it's set as
			if (!back && st.back_was && !st.back_combo) {
				if (menu_code == SDL_GAMEPAD_BUTTON_BACK) hk = hotkey::menu;
				else if (coin_code == SDL_GAMEPAD_BUTTON_BACK && !g_pc_mode) st.coin_frames = 6;
			}
			if (!back) st.back_combo = false;
			st.back_was = back;
			st.start_was = back ? btn(SDL_GAMEPAD_BUTTON_START) : start;
		}
		if (st.start_quiet) {   // (the Start that closed the menu: nothing, till it's let go)
			if (!start_now) st.start_quiet = false;
			start_held = false;
			st.start_delay = -1;
		}
		if (st.coin_frames > 0) { in[0] |= coin_bit; st.coin_frames--; }
		if (st.start_delay >= 0 && st.start_delay-- == 0) st.start_frames = 6;
		if (st.start_frames > 0) { in[0] |= start_bit; st.start_frames--; }
		if (start_held && !g_pc_mode) in[0] |= start_bit;
	}
	static bool down_was, start_was, in_test;
	static int service_frames, test_ago = 1 << 20, test_mode_frames;
	const bool test_mode = m.game_mode() == 0x11;
	test_mode_frames = test_mode ? test_mode_frames + 1 : 0;
	if (!in_test && test_ago < 180 && test_mode_frames >= 30) in_test = true;
	if (in_test && !test_mode) { in_test = false; g_left_test_menu = true; }
	if (in_test) {
		const bool down = (in[1] | in[2]) & PL_DOWN, start = in[0] & (IN0_START1 | IN0_START2);
		if (down && !down_was) service_frames = 6;
		if (start && !start_was) g_test_frames = 6;
		down_was = down; start_was = start;
	}
	if (service_frames > 0) { in[0] |= IN0_SERVICE; service_frames--; }
	if (g_test_pending && m.game_mode() >= 3) { g_test_pending = false; g_test_frames = 10; }   // (past the boot)
	if (g_test_frames > 0) { in[0] |= IN0_TEST; g_test_frames--; }
	test_ago = (in[0] & IN0_TEST) ? 0 : test_ago + 1;
	static const bool vjoy_log = getenv("VF2_TEST_VJOY") != nullptr;   // (tests: player 1's controls as they change)
	if (vjoy_log) {
		static std::string was;
		std::string now;
		static const uint8_t B[] = { PL_UP, PL_DOWN, PL_LEFT, PL_RIGHT, PL_GUARD, PL_PUNCH, PL_KICK };
		static const char *const N[] = { "up", "down", "left", "right", "guard", "punch", "kick" };
		for (int i = 0; i < 7; i++) if (in[1] & B[i]) now += std::string(now.empty() ? "" : " ") + N[i];
		if (in[0] & IN0_START1) now += std::string(now.empty() ? "" : " ") + "start";
		if (now != was) fprintf(stderr, "test: player 1: %s\n", now.empty() ? "-" : now.c_str());
		was = now;
	}
	return hk;
}

// ---- sticks SDL doesn't know ----
// A joystick SDL has no gamepad mapping for (a DirectInput arcade stick: HORI's Virtua Stick, the Astro City Mini's)
// isn't a gamepad to SDL, and the game reads only gamepads. Each is given a fighting stick's layout by its GUID, a
// mapping as SDL's own database has them (buttons 0-7 as A B X Y LB RB LT RT, 8 View and 9 Start, the hat the d-pad,
// axes 0 and 1 the left stick), so it shows up as a pad and the rest (Controls' Pad and Pad buttons, the menu, online)
// is as for any pad. Controls > Player > Set up buttons makes one of its own from what's pressed, kept in settings.ini's
// pad_mapping lines. A gamecontrollerdb.txt in the data folder or beside the program is read first, and the player's own
// mappings after it: both win over the layout given here.
// The GUIDs given the stick's layout here (Any pad takes the pads SDL knows first, then these)
std::set<std::string> g_stick_guids;

std::string joystick_guid(SDL_JoystickID id)
{
	char buf[64];
	SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), buf, sizeof buf);
	return buf;
}

// a name as a mapping holds it (no commas: they part its fields)
std::string mapping_name(const char *n)
{
	std::string s = n && *n ? n : "Joystick";
	for (char &c : s) if (c == ',') c = ' ';
	return s;
}

// A joystick that isn't a gamepad given the stick's layout (true if it was). Not wheels, flight sticks and the like
// (an axis held at one end would hold a direction), nor one without buttons.
bool give_stick_layout(SDL_JoystickID id)
{
	static std::set<SDL_JoystickID> looked;   // (each joystick once: opening a DirectInput one is slow, and open_pads asks often)
	if (SDL_IsGamepad(id) || !looked.insert(id).second) return false;
	switch (SDL_GetJoystickTypeForID(id)) {
	case SDL_JOYSTICK_TYPE_WHEEL: case SDL_JOYSTICK_TYPE_FLIGHT_STICK: case SDL_JOYSTICK_TYPE_THROTTLE:
	case SDL_JOYSTICK_TYPE_GUITAR: case SDL_JOYSTICK_TYPE_DRUM_KIT: return false;
	default: break;
	}
	SDL_Joystick *j = SDL_OpenJoystick(id);
	if (!j) return false;
	const int nb = SDL_GetNumJoystickButtons(j), nh = SDL_GetNumJoystickHats(j), na = SDL_GetNumJoystickAxes(j);
	const std::string name = mapping_name(SDL_GetJoystickName(j)), guid = joystick_guid(id);
	// (axes 0 and 1 the left stick only if both rest in the middle: a slider or a trigger resting at one end would hold
	// a direction)
	bool centred = na >= 2;
	SDL_UpdateJoysticks();   // (a DirectInput device's axes read once: its initial state isn't always known yet)
	for (int a = 0; a < 2 && centred; a++) {
		Sint16 rest = 0;
		if (SDL_GetJoystickAxisInitialState(j, a, &rest) && std::abs(int(rest)) > 8000) centred = false;
		if (std::abs(int(SDL_GetJoystickAxis(j, a))) > 8000) centred = false;
	}
	SDL_CloseJoystick(j);
	// (a fighting stick: buttons, and a hat, six buttons, or four with a stick's two axes; anything less, as the HID
	// device a keyboard's lights or a media key can show up as, isn't made a pad, or it would take a player)
	if (nb <= 0 || (nh == 0 && nb < 6 && !(nb >= 4 && centred))) {
		if (nb > 0 || nh > 0) fprintf(stderr, "a joystick SDL doesn't know: %s (%s): %d buttons, %d hats, %d axes: not a stick, left alone\n", name.c_str(), guid.c_str(), nb, nh, na);
		return false;
	}
	static const char *const BUTTONS[] = { "a", "b", "x", "y", "leftshoulder", "rightshoulder", "lefttrigger", "righttrigger" };
	std::string m = guid + "," + name + ",";
	for (int b = 0; b < std::min(nb, 8); b++) m += std::string(BUTTONS[b]) + ":b" + std::to_string(b) + ",";
	if (nb >= 10) m += "back:b8,start:b9,";   // (most sticks' Select and Start, after their eight)
	else if (nb == 9) m += "start:b8,";
	if (nh > 0) m += "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,";
	if (centred) m += "leftx:a0,lefty:a1,";
	if (SDL_AddGamepadMapping(m.c_str()) < 0) {
		fprintf(stderr, "a joystick SDL doesn't know: %s (%s): couldn't give it a stick's layout: %s\n", name.c_str(), guid.c_str(), SDL_GetError());
		return false;
	}
	g_stick_guids.insert(guid);
	fprintf(stderr, "a joystick SDL doesn't know: %s (%s): given a stick's layout; set its buttons in Controls\n", name.c_str(), guid.c_str());
	return true;
}

void give_unknown_sticks()
{
	int n = 0;
	SDL_JoystickID *ids = SDL_GetJoysticks(&n);
	for (int i = 0; i < n; i++) give_stick_layout(ids[i]);
	SDL_free(ids);
}

// The player's gamecontrollerdb.txt (SDL's community database's format), in the data folder or beside the program,
// then their own mappings (Set up buttons'), before the pads are opened: so both win over a stick's layout.
void load_pad_mappings(const std::string &data, const std::string &program, const std::vector<std::string> &own)
{
	std::set<std::string> seen;
	for (std::string dir : { data, program }) {
		while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
		if (dir.empty()) continue;
		const std::string f = dir + "/gamecontrollerdb.txt";   // (as UTF-8, for SDL: not std::filesystem's, Windows' code page)
		if (!seen.insert(f).second) continue;
		SDL_PathInfo info;
		if (!SDL_GetPathInfo(f.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) continue;
		const int n = SDL_AddGamepadMappingsFromFile(f.c_str());
		if (n < 0) fprintf(stderr, "pads: couldn't read %s: %s\n", f.c_str(), SDL_GetError());
		else fprintf(stderr, "pads: %d mappings from %s\n", n, f.c_str());
	}
	for (const std::string &m : own)
		if (SDL_AddGamepadMapping(m.c_str()) < 0) fprintf(stderr, "pads: a pad_mapping in settings.ini wasn't taken (%s): %s\n", SDL_GetError(), m.c_str());
		else {
			const size_t a = m.find(','), b = m.find(',', a + 1);
			fprintf(stderr, "pads: the buttons set up in Controls for %s (%s)\n", m.substr(a + 1, b - a - 1).c_str(), m.substr(0, a).c_str());
		}
}

// A save is used only if it's the right size.
bool load_blob(const std::string &path, void *dst, size_t n)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) return false;
	std::vector<uint8_t> buf(n + 1);
	const size_t got = fread(buf.data(), 1, n + 1, f);
	fclose(f);
	if (got != n) return false;
	memcpy(dst, buf.data(), n);
	return true;
}

// Writes a file's data through to the disk before it replaces the old one: a rename alone can reach
// the disk first, and a power cut would then leave an empty file.
bool flush_to_disk(FILE *f)
{
	if (fflush(f) != 0) return false;
#ifdef _WIN32
	return _commit(_fileno(f)) == 0;
#else
	return fsync(fileno(f)) == 0;
#endif
}

bool replace_file(const std::string &path, const std::string &tmp)
{
	std::error_code ec;
	std::filesystem::rename(tmp, path, ec);
	if (ec) { std::filesystem::remove(tmp, ec); return false; }
	return true;
}

// Written beside the old file, flushed to the disk, then swapped in: a crash mid-save leaves the
// previous one whole.
bool save_blob(const std::string &path, const void *src, size_t n)
{
	const std::string tmp = path + ".tmp";
	FILE *f = fopen(tmp.c_str(), "wb");
	if (!f) return false;
	const bool ok = fwrite(src, 1, n, f) == n && flush_to_disk(f);
	if (fclose(f) != 0 || !ok) { std::error_code ec; std::filesystem::remove(tmp, ec); return false; }
	return replace_file(path, tmp);
}

// The GPU's start marker: written in place and flushed (no swap: a rename's not on the disk until the folder is, and
// a crash that takes the machine down in the driver would lose it).
bool write_marker(const std::string &path, const char *text)
{
	FILE *f = fopen(path.c_str(), "wb");
	if (!f) return false;
	const size_t n = strlen(text);
	const bool ok = fwrite(text, 1, n, f) == n && flush_to_disk(f);
	return fclose(f) == 0 && ok;
}

// The saves written off the game's thread: save_blob flushes each file to the disk (a crash mid-save
// keeps the old one), which on a hard disk waits tens of milliseconds a file, and the autosave ran
// every few seconds in play: a hitch each time (a reporter: "on an SSD barely noticeable, on a HDD
// heavy stutter even at 1x"). put() hands over a copy and returns at once; a writer thread saves
// it, the newest copy of a file winning if two wait. finish() writes what's left and stops.
struct bg_saver {
	std::mutex mx;
	std::condition_variable cv;
	std::map<std::string, std::vector<uint8_t>> jobs;
	bool stop = false;
	std::thread th{ [this] { run(); } };
	void put(const std::string &path, const void *src, size_t n) {
		{ std::lock_guard<std::mutex> l(mx); if (stop) return; jobs[path].assign(static_cast<const uint8_t *>(src), static_cast<const uint8_t *>(src) + n); }
		cv.notify_one();
	}
	void run() {
		std::unique_lock<std::mutex> l(mx);
		for (;;) {
			cv.wait(l, [&] { return stop || !jobs.empty(); });
			if (jobs.empty()) return;   // (stop, nothing left)
			auto todo = std::move(jobs);
			jobs.clear();
			l.unlock();
			for (auto &[path, bytes] : todo)
				if (!save_blob(path, bytes.data(), bytes.size())) fprintf(stderr, "couldn't save %s\n", path.c_str());
			l.lock();
		}
	}
	void finish() {
		{ std::lock_guard<std::mutex> l(mx); stop = true; }
		cv.notify_one();
		if (th.joinable()) th.join();
	}
	~bg_saver() { finish(); }
};

uint64_t fnv(const void *p, size_t n, uint64_t h = 1469598103934665603ull)
{
	for (size_t i = 0; i < n; i++) h = (h ^ static_cast<const uint8_t *>(p)[i]) * 1099511628211ull;
	return h;
}

// The menu's choices and the controls, kept in settings.ini in the NVRAM folder ("key = value").
// scale 0: none chosen yet (the one that fits the window).
struct settings
{
	int scale = 0, aspect = 0, volume = 100, music = 50, effects = 100, aa = 1, filter = 0, ring = 0;
	int crt_colours = 0, brightness = 100, gamma = 100;   // the picture's colours: an arcade monitor's (crt::PRESETS), the player's brightness and gamma (%)
	int pipelined_at = 0;
	// Display's own two rows for the gpu renderer: vsync, and how many frames it may keep in flight
	// (see apply_display_settings, where the second one is explained)
	int vsync = 1;
	bool hud = true;     // Display > HUD: the 2D over a fight (energy bars, timer, names...) drawn; off: the 3D and the backdrop only
	int frame_limit = -1;   // Display > Frame limit: the pictures a second at most, 30 60 120 144; 0 none, -1 the screen's refresh (as before with vsync)
	int on_screen = 1;   // the on-screen controls (a touch screen): 0 off, 1 while the player has no pad, 2 always
	int stage = -1;   // Arcade settings > Stage select: a versus's first stage after Start (1-9 the versus order's, 10 Dural's; 0, its first, the game's own), -1 the game's own
	int secret = 0;   // Cheats > Secret character: the fighter in Akira's place (model2a::set_secret_character), 0 none
	std::string random_player[2];   // Cheats: each player's fighter: "" the select as ever, "*" a random one each game, or a name's (model2a::set_random_player)
	std::string random_named[2];    // (each player's last name typed: Left / Right come back to it)
	bool random_cpu = false;        // Cheats: Random opponents, every computer opponent a random fighter
	bool debug_mode = false;        // Arcade settings: Debug mode, the arcade's own (model2a::set_debug_mode), offline only
	// settings.ini's version: 2 from v0.1.2 (none before), 3 from the GPU's failures forgotten once
	// (v0.1.15) or the phone's fullscreen, 4 from Rounds to win (vs player) 3 (v0.1.17), 5 the same
	// once more, for everyone (v0.1.18). Its upgrades, once, as it's read:
	//   2: the old default music volume, 20, to the new one, 50 (the music's stereo reverb mended:
	//      at 20 it's now too quiet); a 20 chosen later is kept (the file then says version 2)
	//   3: on a phone, fullscreen = 0 (the old default: the menu couldn't open on Android before
	//      this version, so the player never chose it) to 1, the new default; the GPU's failures
	//      saved by older builds forgotten once
	//   4: Rounds to win (vs player) 2 (the factory's, every player's until now) to 3, VF3's and the tournaments' (in the
	//      game's own settings, as the boot starts: vs_rounds_to_3); a 2 chosen later is kept
	//   5: the same once more, for everyone (2 Nov: online matches always first to 3, the offline setting 3 too): a 2
	//      kept in the NVRAM by an install whose settings.ini was then deleted had never moved. No settings.ini at all
	//      moves it too (the NVRAM may be an old one's); a 2 chosen after the move is kept (the file then says 5)
	static constexpr int VERSION = 5;   // (3: GPU failures saved by older builds forgotten once)
	int version = VERSION;
	bool upgraded = false;   // (an upgrade made as it was read: saved at once)
	bool vs_rounds_to_3 = true;   // (version 5's, for the boot: the game's settings aren't read yet; no settings.ini: on)
	// the 3D's renderer (Display > 3D renderer): 0 the CPU, 1 the GPU, 2 Auto (whichever is faster
	// here); the GPU's quality (0 exact, 1 fast); and Auto's choices so far on this machine, by
	// scale and antialiasing ("3x1:g 2x1:c": a start there starts on that one, then checks it)
	int renderer3d = 2, gpu_quality = 0;
	std::string auto3d;
	int graphics = 0;          // Windows: the GPU's driver, from the next start: 0 Auto (Vulkan first, Direct3D 12 if it won't start or draws no 3D), 1 Direct3D 12
	static constexpr const char *BUILD = VF2_VERSION " " VF2_DATE " " VF2_COMMIT;   // (what gpu3d_failed and auto3d were learnt by)
	std::string gpu3d_failed;   // SDL_GPU's drivers whose 3D failed here ("direct3d12 vulkan"): the next start tries the next
	std::string roms_folder;   // where the player keeps vf2.zip (roms_folder = ...: kept as written)
	int arcade_coin = -2;   // the arcade way's coin setting, kept while the PC way has FREE PLAY in its place (-1: none to give back; -2: none kept yet)   // the render scale this machine needed pipelining at (the governor's, kept: a start there starts pipelined)
	bool log = true;   // log.txt written (Game > Log file: read before the log's opened, so from the next start)
	std::string language = "auto";   // Game > Language: auto (the system's), or a language's code (tr::choice_key)
	OnlineSettings online;   // the Online page's
	// A phone has no window to speak of: start immersive (Display > Fullscreen turns it off, and
	// settings.ini keeps that choice). Elsewhere, a window the player can move and resize.
#if defined(__ANDROID__)
	bool fullscreen = true, timings = false, arcade = false, smooth_bg = false, show_fps = false, smooth = true;
	// SDL's gpu renderer keeps one frame in flight: every picture then waits for the one before it
	// to be drawn, so the GPU's own time for a picture is in the frame's. Cheap on a desktop, and
	// what the smooth pictures (up to 120 a second) can't afford on a phone: 2 lets the drawing get
	// ahead of the GPU again (Display > GPU frames in flight, which is where it's measured).
	int frames_in_flight = 2;
	// Android's performance hints (Display > Performance hints, ADPF): a session over the threads a
	// frame's work runs on, so the phone keeps their clocks up (sysinfo::perf_hints_start). On by
	// default; off to compare a run without it. Nothing elsewhere.
	int perf_hints = 1;
#else
	bool fullscreen = false, timings = false, arcade = false, smooth_bg = false, show_fps = false, smooth = true;
	int frames_in_flight = 1;   // (SDL's renderer's own: the shortest delay to the screen)
	int perf_hints = 0;   // (Android's performance hints: nothing here)
#endif
	int window_scale = 0;   // Display > Window size: the window k times the arcade's 384 lines high (4:3 and Fit 512 x k
	                        // wide, wider shapes theirs), each arcade line k of the screen's; 0 Free (as the player sizes it)
	// The window's place and size as last left in a window (VF3's issue #21), in the window's own units (points on a
	// Mac, as SDL_CreateWindow takes them); window_w 0: none yet. Maximised: the size under it kept, maximised again
	int window_w = 0, window_h = 0, window_x = 0, window_y = 0;
	bool window_max = false;
	// Fullscreen as a borderless window over the screen and a line past it (VF3's issue #7: AMD's driver on Windows
	// froze in its own, exclusive fullscreen): the way on Windows unless fullscreen_exclusive = 1 (Display > Fullscreen
	// mode); elsewhere the system's own fullscreen (a Mac's Spaces, the Deck's gamescope) unless fullscreen_window = 1
	bool fs_window = false, fs_exclusive = false;
	player_controls ctl[2] = { player_controls::defaults(0), player_controls::defaults(1) };
	std::vector<std::string> pad_mappings;   // pad_mapping lines: SDL mappings made in Controls > Set up buttons (one a pad)

	void load(const std::string &path)
	{
		FILE *f = fopen(path.c_str(), "r");
		if (!f) return;
		std::map<std::string, std::string> kv;
		char line[1100], key[64], val[1024];
		while (fgets(line, sizeof line, f))
			if (sscanf(line, " %63[a-z0-9_] = %1023[^\r\n]", key, val) == 2) {
				if (!strcmp(key, "pad_mapping")) pad_mappings.push_back(val);   // (several: one a pad)
				else kv[key] = val;
			}
		fclose(f);
		auto num = [&](const char *k, int &v, int lo, int hi) {
			auto it = kv.find(k);
			if (it != kv.end()) v = std::clamp(atoi(it->second.c_str()), lo, hi);
		};
		auto flag = [&](const char *k, bool &v) { int i = v; num(k, i, 0, 1); v = i; };
		num("scale", scale, 0, MAX_SCALE);
		if (auto it = kv.find("online_name"); it != kv.end()) online.name = online_menu::clean_name(it->second);
		if (auto it = kv.find("online_address"); it != kv.end()) online.address = online_menu::clean_address(it->second);
		if (auto it = kv.find("online_lobby"); it != kv.end() && (it->second.rfind("https://", 0) == 0 || it->second.rfind("http://", 0) == 0))
			online.lobby = it->second.substr(0, 200);   // (another lobby than the built-in one: tests, a server of one's own)
		num("online_delay", online.delay, -1, 10);
		num("online_port", online.port, 1024, 65535);
		num("online_connection", online.show_connection, 0, 2);
		flag("online_hide_ip", online.hide_ip);
		num("online_max_ping", online.max_ping, 0, 1000);
		flag("online_chime", online.chime);
		if (auto it = kv.find("online_fighter"); it != kv.end()) { std::string v = it->second; while (!v.empty() && isspace(uint8_t(v.back()))) v.pop_back(); online.fighter = v.substr(0, 16); }
		if (auto it = kv.find("online_room_name"); it != kv.end()) online.room_name = online_menu::clean_room(it->second);
		num("online_room_listed", online.room_who, 0, 2);
		num("online_rooms_show", online.rooms_show, 0, 2);
		flag("online_quick_listed", online.quick_listed);
		num("picture_shape", aspect, 0, N_ASPECTS - 1);
		num("volume", volume, 0, 100);
		num("antialiasing", aa, 0, 3);   // 1 off, 0 Low (the 3D's edges smoothed: older builds read 0 as off), 2 and 3 drawn larger
		num("music_volume", music, 0, 100);
		version = 0;
		num("settings_version", version, 0, 1000);
		const int file_version = version;   // (what the file said: the upgrades below look at it)
		if (version < 2 && music == 20) {
			music = 50; upgraded = true;
			fprintf(stderr, "settings: the music volume 20 (the old default) moved to 50, the new one\n");
		}
		vs_rounds_to_3 = version < 5;
		if (vs_rounds_to_3) upgraded = true;
		version = VERSION;
		num("effects_volume", effects, 0, 100);
		num("screen_filter", filter, 0, 2);
		num("crt_colours", crt_colours, 0, crt::N_PRESETS - 1);
		num("brightness", brightness, crt::BRIGHT_MIN, crt::BRIGHT_MAX);
		num("gamma", gamma, crt::GAMMA_MIN, crt::GAMMA_MAX);
		num("ring_size", ring, 0, 1600);
		num("pipelined_at", pipelined_at, 0, MAX_SCALE);
		num("vsync", vsync, 0, 1);
		num("frames_in_flight", frames_in_flight, 1, 3);
		num("perf_hints", perf_hints, 0, 1);
		num("on_screen_controls", on_screen, 0, 2);
		num("renderer_3d", renderer3d, 0, 2);
		num("gpu_quality", gpu_quality, 0, 1);
		num("graphics", graphics, 0, 1);
		if (auto it = kv.find("auto_3d"); it != kv.end()) auto3d = it->second;
		if (auto it = kv.find("gpu_3d_failed"); it != kv.end()) gpu3d_failed = it->second;
		// (older builds saved a driver as failed for any GPU error in play, a suspend or a reset: every
		// later start on the CPU. Forgotten once, each driver tried again from this build on)
		if (file_version < 3 && (!gpu3d_failed.empty() || !auto3d.empty())) {
			if (!gpu3d_failed.empty()) fprintf(stderr, "settings: the GPU drivers saved as failed (%s) tried again (an older build saved one failure in play for good)\n", gpu3d_failed.c_str());
			gpu3d_failed.clear(); auto3d.clear(); upgraded = true;
		}
		// (and what another build learnt (another version or commit): forgotten, so each new build tries the
		// GPU again, a driver updated since or a fix here. Remembered for ever, one bad start or one slow measure kept a
		// Windows player's 3D on the CPU at every start after, its cost growing with the scale: slow above 1x)
		std::string learnt_by;
		if (auto it = kv.find("gpu_learnt_build"); it != kv.end()) learnt_by = it->second;
		if ((!gpu3d_failed.empty() || !auto3d.empty()) && learnt_by != BUILD) {
			fprintf(stderr, "settings: the GPU's failures (%s) and Auto's choices (%s) learnt by %s forgotten: this build tries the GPU again\n",
			        gpu3d_failed.empty() ? "none" : gpu3d_failed.c_str(), auto3d.empty() ? "none" : auto3d.c_str(), learnt_by.empty() ? "an older build" : learnt_by.c_str());
			gpu3d_failed.clear(); auto3d.clear(); upgraded = true;
		}
		if (auto it = kv.find("roms_folder"); it != kv.end()) roms_folder = it->second;
		num("arcade_coins", arcade_coin, -2, 0x18);
		num("secret_character", secret, 0, 12);
		if (secret != 9 && secret != 11 && secret != 12) secret = 0;
		auto trimmed = [](std::string v) { while (!v.empty() && isspace(uint8_t(v.back()))) v.pop_back(); return v; };   // (a value edited by hand: "* ")
		for (int p = 0; p < 2; p++) {
			if (auto it = kv.find(p ? "random_p2" : "random_p1"); it != kv.end()) random_player[p] = trimmed(it->second) == "normal" ? std::string() : trimmed(it->second);
			if (auto it = kv.find(p ? "random_p2_name" : "random_p1_name"); it != kv.end()) random_named[p] = trimmed(it->second);
		}
		flag("random_opponents", random_cpu);
		flag("debug_mode", debug_mode);
		num("stage_select", stage, -1, vf2net::STAGES);
		if (stage == 0) stage = -1;   // (the versus order's first: the game's own)
		if (ring && (ring < 1300 || ring % 100)) ring = 0;   // 0: by difficulty
		flag("fullscreen", fullscreen);
		flag("fullscreen_window", fs_window);
		flag("fullscreen_exclusive", fs_exclusive);
#if defined(__ANDROID__)
		// (a phone's screen is the window: a file from before this version has the old default, 0,
		//  which the player couldn't have chosen — the menu opens for the first time in this one.
		//  Display > Fullscreen, and then a file of this version, turn it off for good.)
		if (file_version < 3 && !fullscreen) {
			fullscreen = true; upgraded = true;
			fprintf(stderr, "settings: fullscreen on (a phone's screen is the window)\n");
		}
#endif
		flag("frame_timings", timings);
		flag("log", log);
		if (auto it = kv.find("language"); it != kv.end()) {   // (one this build doesn't know: auto)
			tr::choice c;
			language = tr::choice_from(it->second, c) ? it->second : "auto";
		}
		flag("arcade_mode", arcade);
		flag("background_filter", smooth_bg);
		flag("show_fps", show_fps);
		flag("hud", hud);
		num("frame_limit", frame_limit, -1, 1000);
		if (frame_limit != -1 && frame_limit != 0 && frame_limit != 30 && frame_limit != 60 && frame_limit != 120 && frame_limit != 144) frame_limit = -1;
		num("window_scale", window_scale, 0, 16);
		num("window_w", window_w, 0, 65535);
		num("window_h", window_h, 0, 65535);
		num("window_x", window_x, -65535, 65535);
		num("window_y", window_y, -65535, 65535);
		flag("window_maximised", window_max);
		if (window_w < 64 || window_h < 48) window_w = window_h = 0;
		flag("smooth_motion", smooth);
		for (int p = 0; p < 2; p++) ctl[p].load(kv, p);
		// settings from before the Menu button: Arcade mode's View tapped was a coin. It stays one
		// (as Coin, so the menu is L3 + R3, the touchpad, View + Start or Esc), so a cabinet's coin
		// button keeps working after the update
		if (arcade && !kv.count("p1_pad_menu"))
			for (auto &c : ctl)
				if (c.pad[C_COIN] == PAD_NONE && c.pad[C_MENU] == SDL_GAMEPAD_BUTTON_BACK) {
					c.pad[C_COIN] = SDL_GAMEPAD_BUTTON_BACK;
					c.pad[C_MENU] = PAD_NONE;
				}
	}

	// False if it couldn't be written (the caller keeps it to save again; said once in the log)
	bool save(const std::string &path) const
	{
		const std::string tmp = path + ".tmp";
		std::error_code ec;
		if (const auto dir = std::filesystem::path(path).parent_path(); !dir.empty()) std::filesystem::create_directories(dir, ec);
		FILE *f = fopen(tmp.c_str(), "w");
		static bool said = false;
		if (!f) {
			if (!said) fprintf(stderr, "couldn't save the settings to %s: %s\n", path.c_str(), strerror(errno));
			said = true;
			return false;
		}
		fprintf(f, "# Virtua Fighter 2 settings (the in-game menu writes these)\n");
		fprintf(f, "settings_version = %d\n", version);
		fprintf(f, "scale = %d\npicture_shape = %d\nfullscreen = %d\nframe_timings = %d\nvolume = %d\narcade_mode = %d\n"
				"background_filter = %d\nmusic_volume = %d\neffects_volume = %d\nantialiasing = %d\nshow_fps = %d\nscreen_filter = %d\nring_size = %d\nsmooth_motion = %d\npipelined_at = %d\narcade_coins = %d\nvsync = %d\nframes_in_flight = %d\n", scale, aspect,
				int(fullscreen), int(timings), volume, int(arcade), int(smooth_bg), music, effects, aa, int(show_fps), filter, ring, int(smooth), pipelined_at, arcade_coin, vsync, frames_in_flight);
		if (!log) fprintf(f, "log = 0\n");
		fprintf(f, "language = %s\n", language.c_str());
		fprintf(f, "window_scale = %d\n", window_scale);
		fprintf(f, "hud = %d\nframe_limit = %d\n", int(hud), frame_limit);
		if (fs_window) fprintf(f, "fullscreen_window = 1\n");
		if (fs_exclusive) fprintf(f, "fullscreen_exclusive = 1\n");
		if (window_w > 0) fprintf(f, "window_w = %d\nwindow_h = %d\nwindow_x = %d\nwindow_y = %d\nwindow_maximised = %d\n", window_w, window_h, window_x, window_y, int(window_max));
		fprintf(f, "renderer_3d = %d\ngpu_quality = %d\nauto_3d = %s\n", renderer3d, gpu_quality, auto3d.c_str());
		fprintf(f, "perf_hints = %d\n", perf_hints);
		fprintf(f, "secret_character = %d\n", secret);
		for (int p = 0; p < 2; p++) {
			// (Normal written as "normal": an empty value isn't read back)
			fprintf(f, "random_p%d = %s\n", p + 1, random_player[p].empty() ? "normal" : random_player[p].c_str());
			if (!random_named[p].empty()) fprintf(f, "random_p%d_name = %s\n", p + 1, random_named[p].c_str());
		}
		if (random_cpu) fprintf(f, "random_opponents = 1\n");
		if (debug_mode) fprintf(f, "debug_mode = 1\n");
		if (stage >= 0) fprintf(f, "stage_select = %d\n", stage);
		fprintf(f, "crt_colours = %d\nbrightness = %d\ngamma = %d\n", crt_colours, brightness, gamma);
		fprintf(f, "on_screen_controls = %d\n", on_screen);
		if (graphics) fprintf(f, "graphics = %d\n", graphics);
		if (!gpu3d_failed.empty()) fprintf(f, "gpu_3d_failed = %s\n", gpu3d_failed.c_str());
		if (!gpu3d_failed.empty() || !auto3d.empty()) fprintf(f, "gpu_learnt_build = %s\n", BUILD);
		if (!roms_folder.empty()) fprintf(f, "roms_folder = %s\n", roms_folder.c_str());
		fprintf(f, "online_name = %s\nonline_delay = %d\nonline_connection = %d\nonline_port = %d\n", online.name.c_str(), online.delay,
				online.show_connection, online.port);
		if (!online.address.empty()) fprintf(f, "online_address = %s\n", online.address.c_str());
		if (!online.lobby.empty()) fprintf(f, "online_lobby = %s\n", online.lobby.c_str());
		fprintf(f, "online_hide_ip = %d\nonline_max_ping = %d\nonline_chime = %d\n", int(online.hide_ip), online.max_ping, int(online.chime));
		if (!online.fighter.empty()) fprintf(f, "online_fighter = %s\n", online.fighter.c_str());
		if (!online.room_name.empty()) fprintf(f, "online_room_name = %s\n", online.room_name.c_str());
		fprintf(f, "online_room_listed = %d\nonline_rooms_show = %d\nonline_quick_listed = %d\n", online.room_who, online.rooms_show, int(online.quick_listed));
		for (int p = 0; p < 2; p++) ctl[p].save(f, p);
		for (const std::string &m : pad_mappings) fprintf(f, "pad_mapping = %s\n", m.c_str());
		const bool flushed = flush_to_disk(f);
		if (fclose(f) == 0 && flushed && replace_file(path, tmp)) return true;
		std::filesystem::remove(tmp, ec);
		if (!said) fprintf(stderr, "couldn't save the settings to %s\n", path.c_str());
		said = true;
		return false;
	}
};

// The player's own game, the first time (as vf1-recomp asks for vf.zip): no complete set in their
// data folder's roms/ yet, so ask for MAME's vf2.zip with a file picker, check it, and copy it in.
// Only roms/vf2.zip is ever read, so a roms folder shared with other games can't get in the way.
// False if they cancel (or there's no way to pick a file: then they're told where to put it).
// The player's ROM folder (--roms, VF2_ROMS or settings.ini's roms_folder: `folder`) is looked in
// first and only read; without vf2.zip there, the data folder's roms/ next; a set chosen with the
// picker then is copied into the ROM folder (where the next start looks), not the data folder.
bool first_launch(const std::string &data, const std::string &folder, romset::result &out, std::string &first_link)
{
	namespace fs = std::filesystem;
	const fs::path own = fs::path(data) / "roms";
	const fs::path roms = folder.empty() ? own : fs::path(folder), zip = roms / "vf2.zip";
	std::error_code ec;
	std::string problem;
	if (!folder.empty()) {
		if (fs::is_regular_file(zip, ec)) fprintf(stderr, "the game from the ROM folder %s\n", roms.string().c_str());
		else {
			fprintf(stderr, "ROM folder: no vf2.zip in %s%s: looking in %s next\n", roms.string().c_str(),
					fs::is_directory(roms, ec) ? "" : " (no such folder)", own.string().c_str());
			if (fs::is_regular_file(own / "vf2.zip", ec)) {
				romset::result r = romset::load((own / "vf2.zip").string());
				if (r.ok) { fprintf(stderr, "the game from %s\n", own.string().c_str()); out = std::move(r); return true; }
				problem = r.error + "\n\n";
			}
		}
	}
	// where a picked set goes: the ROM folder, but never over the player's own set there (one that
	// failed the check goes on failing; the picked one in the data folder's roms, looked in next time)
	fs::path dest = roms;
	if (fs::is_regular_file(zip, ec)) {
		romset::result r = romset::load(zip.string());
		if (r.ok) { out = std::move(r); return true; }
		problem = r.error + "\n\n";
		if (!folder.empty()) {
			dest = own;
			if (fs::is_regular_file(own / "vf2.zip", ec)) {
				romset::result r2 = romset::load((own / "vf2.zip").string());
				if (r2.ok) { fprintf(stderr, "the ROM folder's set: %s; the game from %s\n", r.error.c_str(), own.string().c_str()); out = std::move(r2); return true; }
			}
		}
	}
	if (getenv("VF2_HIDDEN")) {   // (a test run: no dialogs, no file picker)
		fprintf(stderr, "%sno vf2.zip in %s\n", problem.c_str(), roms.string().c_str());
		return false;
	}
	SDL_Init(SDL_INIT_VIDEO);
	const char *title = "Virtua Fighter 2";
	const std::string where = tr::fill(tr::ROMS_COPIED_INTO, { { "folder", dest.string() } });
	std::string note = problem + tr::fill(tr::ROMS_CHOOSE, { { "where", where } });   // (problem: the set's own words, English)
	for (;;) {
		if (!message_box(SDL_MESSAGEBOX_INFORMATION, title, note)) return false;
		// (SDL may answer on another thread: done is set last, released, so failed and files are seen)
		struct pick { std::atomic<bool> done{ false }; bool failed = false; std::vector<std::string> files; } chosen;
		static std::string filter_name;   // (kept: the dialog reads it later)
		filter_name = tr::G_ZIP_FILTER;
		static SDL_DialogFileFilter filters[] = { { "", "zip" } };
		filters[0].name = filter_name.c_str();
		SDL_ShowOpenFileDialog([](void *ud, const char *const *list, int) {
			auto *c = static_cast<pick *>(ud);
			if (!list) c->failed = true;                  // no picker here (e.g. Steam Deck Game Mode)
			else for (; *list; list++) c->files.push_back(*list);
			c->done.store(true, std::memory_order_release);
		}, &chosen, nullptr, filters, 1, nullptr, false);
		while (!chosen.done.load(std::memory_order_acquire)) {
			SDL_Event e;
			while (SDL_PollEvent(&e))   // (a link that started the app, a Mac's open-URL event, kept for once the game's up)
				if (e.type == SDL_EVENT_DROP_FILE && e.drop.data && links::is_link(e.drop.data)) first_link = std::string(e.drop.data).substr(0, 64);
			SDL_Delay(10);
		}
		if (chosen.failed) {
			fs::create_directories(dest, ec);
			message_box(SDL_MESSAGEBOX_INFORMATION, title, tr::fill(tr::ROMS_NO_PICKER, { { "folder", dest.string() } }));
			return false;
		}
		if (chosen.files.empty()) return false;
		std::string f = chosen.files[0];
#if defined(__ANDROID__)
		// Android's picker hands back a content:// URI, which only SDL can read (not fopen): copy
		// the set through SDL into roms/vf2.zip, then read it back as a plain file.
		if (f.rfind("content://", 0) == 0) {
			fs::create_directories(roms, ec);
			size_t n = 0;
			void *d = SDL_LoadFile(f.c_str(), &n);
			if (!d) {
				note = tr::fill(tr::ROMS_NO_READ, { { "why", SDL_GetError() } });
				continue;
			}
			const bool wrote = save_blob(zip.string(), d, n);
			SDL_free(d);
			if (!wrote) {
				note = tr::fill(tr::ROMS_NO_COPY_HERE, { { "folder", roms.string() } });
				continue;
			}
			f = zip.string();
		}
#endif
		romset::result r = romset::load(f);
		if (!r.ok) {
			note = tr::fill(tr::ROMS_NOT_A_SET, { { "file", fs::path(f).filename().string() }, { "why", r.error }, { "where", where } });
			continue;
		}
		fs::create_directories(dest, ec);
		if (!fs::equivalent(f, dest / "vf2.zip", ec)) fs::copy_file(f, dest / "vf2.zip", fs::copy_options::overwrite_existing, ec);
		if (ec) {
			note = tr::fill(tr::ROMS_NO_COPY, { { "folder", dest.string() }, { "why", ec.message() } });
			continue;
		}
		out = std::move(r);
		return true;
	}
}

} // namespace

// settings.ini's log line, read before the settings are (the log's opened first): false for log = 0
static bool log_wanted(const std::string &path)
{
	bool on = true;
	if (FILE *f = fopen(path.c_str(), "r")) {
		char line[256];
		while (fgets(line, sizeof line, f)) {
			int v = 1;
			if (sscanf(line, " log = %d", &v) == 1) on = v != 0;
		}
		fclose(f);
	}
	return on;
}
#ifdef _WIN32
static const char *const NULL_DEVICE = "NUL";
#else
static const char *const NULL_DEVICE = "/dev/null";
#endif

int main(int argc, char **argv)
{
#ifdef __APPLE__
	// the game, its drawing and its sound on the performance cores: without it an M-series Mac can put
	// them on the efficiency cores (VF3's d5b1992)
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
	// test runs (VF2_HIDDEN): a background process on macOS, so no Dock icon appears for each one
	if (getenv("VF2_HIDDEN")) SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
	const auto launch_t = std::chrono::steady_clock::now();
	auto seconds_since = [](std::chrono::steady_clock::time_point t) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count(); };
	const bool pace_log = getenv("VF2_PACE_LOG") != nullptr;
	std::string zip;   // named on the command line
	// the command line wins over settings.ini (in the data folder)
	bool arg_arcade = false, sound = true, show_boot = false, no_scene_sync = false, arg_interp = false;
	int arg_music = -1, arg_effects = -1, arg_volume = -1, arg_scale = 0, arg_aspect = -1, arg_full = -1;
	std::string arg_roms;   // --roms <folder>: where the player's vf2.zip is (over VF2_ROMS and settings.ini's roms_folder)
	// Playing by address from the command line (a LAN, a VPN): --host waits for a player, --join ADDRESS joins one;
	// --port N the host's UDP port (7700); as Online > Play by address, started as the game is up
	bool arg_host = false;
	std::string arg_join;
	std::string arg_link;   // (a link from the Online page on the web: vf2recomp://vf2/join/CODE, links.h)
	int arg_port = 0;
	for (int i = 1; i < argc; i++) {
		const std::string a = argv[i];
		if (a == "--arcade") arg_arcade = true;
		else if (a == "--roms" && i + 1 < argc) arg_roms = argv[++i];
		else if (a.rfind("--roms=", 0) == 0) arg_roms = a.substr(7);
		else if (a == "--interp") arg_interp = true;   // MAME's i960 interpreter instead of the recompiled code
		else if (a == "--fullscreen") arg_full = 1;
		else if (a == "--window") arg_full = 0;
		else if (a == "--no-sound") sound = false;
		else if ((a == "--music" || a == "--effects") && i + 1 < argc) (a == "--music" ? arg_music : arg_effects) = std::clamp(atoi(argv[++i]), 0, 100);
		else if (a == "--volume" && i + 1 < argc) arg_volume = std::clamp(atoi(argv[++i]), 0, 100);
		else if (a == "--scale" && i + 1 < argc) arg_scale = std::clamp(atoi(argv[++i]), 1, MAX_SCALE);
		else if (a == "--aspect" && i + 1 < argc) {
			const std::string v = argv[++i];
			for (int k = 0; k < N_ASPECTS; k++) if (v == ASPECT_NAMES[k] || (k == 0 && v == "auto")) arg_aspect = k;
			if (arg_aspect < 0) { fprintf(stderr, "--aspect: fit, 4:3, 16:10, 16:9 or 21:9\n"); return 2; }
		}
		else if (a == "--show-boot") show_boot = true;
		else if (a == "--no-scene-sync") no_scene_sync = true;
		else if (a == "--host") arg_host = true;
		else if (a == "--join" && i + 1 < argc) arg_join = argv[++i];
		else if (a.rfind("--join=", 0) == 0) arg_join = a.substr(7);
		else if (a == "--port" && i + 1 < argc) arg_port = std::clamp(atoi(argv[++i]), 1024, 65535);
		else if (a.rfind("--port=", 0) == 0) arg_port = std::clamp(atoi(a.c_str() + 7), 1024, 65535);
		else if (links::is_link(a)) { arg_link = a.substr(0, 64); break; }   // (nothing after a link read: on Windows that would be its own text, past a quote in it)
		else if (a.rfind("--", 0) == 0) { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
		else if (a.rfind("-psn_", 0) == 0) {}   // (an old macOS's Finder adds its process number)
		else {
			// a zip named on the command line: one that's there (a stray word, say a value after an
			// option that takes none, used to be tried as the zip and its error shown in a dialog)
			std::error_code ec;
			if (!std::filesystem::is_regular_file(a, ec)) { fprintf(stderr, "%s: no such file (options start with --)\n", a.c_str()); return 2; }
			zip = a;
		}
	}
	const bool hidden = getenv("VF2_HIDDEN") != nullptr;
	const int64_t quit_after = getenv("VF2_QUIT_AFTER") ? atoll(getenv("VF2_QUIT_AFTER")) : -1;
	const bool test_saves = getenv("VF2_TEST_SAVES") != nullptr;   // tests: a test run saves the NVRAM and settings as a player's would
	input_script script;
	if (const char *sp = getenv("VF2_SCRIPT")) {
		std::string err;
		if (!script.load(sp, err)) { fprintf(stderr, "%s\n", err.c_str()); return 2; }
	}

	auto fail = [](const tr::line &what) {   // (the log in English, the box in the menus' language)
		fprintf(stderr, "%s\n", what.en().c_str());
		if (!getenv("VF2_HIDDEN")) message_box(SDL_MESSAGEBOX_ERROR, "Virtua Fighter 2", what.str());   // (a test run: stderr and log.txt only)
		return 1;
	};

	// The game and the data folder (see the top): it holds the saves, settings.ini, backgrounds/,
	// Screenshots and log.txt; the player's own also roms/vf2.zip
	std::error_code ec;
	std::string data = getenv("VF2_DATA") ? getenv("VF2_DATA") : "";
	bool players = false;
	const std::string home = program_folder();
	const char *portable = portable_marker(home);
	if (zip.empty()) {
		const char *base = SDL_GetBasePath();
		const std::string here = base ? base : "";
		if (portable) {   // portable mode: everything in the program's folder, nothing anywhere else
			if (data.empty()) data = home;
			players = true;
		} else if (!here.empty() && std::filesystem::is_directory(here + "roms", ec)) {   // a roms folder beside it (the Steam Deck's)
			if (data.empty()) data = here;
			players = true;
		} else if (arg_roms.empty() && !getenv("VF2_ROMS") && std::filesystem::is_regular_file("roms/vf2.zip", ec)) zip = "roms/vf2.zip";   // the source tree
		else {
			if (data.empty()) {
				char *pref = SDL_GetPrefPath(nullptr, "Virtua Fighter 2 Recomp");
				data = pref ? pref : ".";
				SDL_free(pref);
			}
			players = true;
		}
		// (no separator at the end, but for a drive's root)
		while (data.size() > 1 && (data.back() == '/' || data.back() == '\\') && data[data.size() - 2] != ':') data.pop_back();
	}
	if (data.empty()) data = (std::filesystem::path(zip).parent_path() / "vf2_data").string();
	if (data.empty()) data = "vf2_data";
	// a link with the game already running (Windows, Linux): handed to it, and this copy's done (before this one's
	// log.txt is opened: the running one's is left as it is)
	if (!arg_link.empty() && links::hand_over(data, arg_link)) { fprintf(stderr, "links: handed %s to the copy running\n", arg_link.c_str()); return 0; }
	// Without a console (started from the Finder, a launcher), the messages go to log.txt there,
	// for when something goes wrong.
	// settings.ini's log = 0 (Game > Log file): no log.txt written, the program's messages thrown away; read here,
	// before the log's opened (the settings themselves are read below)
	if (!log_wanted(data + "/settings.ini")) {
		if (!freopen(NULL_DEVICE, "w", stderr)) {}
	} else if (!isatty(fileno(stderr))) {
		std::filesystem::create_directories(data, ec);
		if (freopen((data + "/log.txt").c_str(), "w", stderr)) setvbuf(stderr, nullptr, _IONBF, 0);
	}
	sysinfo::tune();
	fprintf(stderr, "Virtua Fighter 2 Recomp %s (%s, online %s) on %s\n", VF2_VERSION, VF2_DATE, vf2net::online_version(VF2_VERSION).c_str(), sysinfo::describe().c_str());
	if (portable) fprintf(stderr, "portable (%s): everything in %s\n", portable, data.c_str());
	else if (home.find("/AppTranslocation/") != std::string::npos)   // (macOS runs a downloaded app from a hidden copy until it's moved)
		fprintf(stderr, "run from macOS's hidden copy of the app (App Translocation): a portable.txt beside it isn't seen until the app is moved with the Finder\n");
	// this one's the copy links go to, and this user's system told it opens them (not in tests)
	links::running(data);
	if (!getenv("VF2_HIDDEN") && !getenv("VF2_QUIT_AFTER")) { const std::string said = links::register_scheme(); if (!said.empty()) fprintf(stderr, "%s\n", said.c_str()); }
	const std::string settings_path = data + "/settings.ini";
	settings cfg;
	bool cfg_dirty = false;      // saved (as the menu closes, and at exit) only when the player changed something
	// Steam sets SteamDeck=1 on the Deck: fullscreen, its 16:10, and 2x (1,190 x 768), whatever
	// screen it's docked to: with the i960 recompiled, a Deck's fight at 2x is about 16 ms of its
	// 17.4 ms a frame, pipelined (a player's log: 3x holds 60 pictures a second too, but not as a
	// stage begins). The menu goes higher (the governor lowers it again if need be). settings.ini,
	// then the command line, win.
	if (getenv("SteamDeck")) { cfg.fullscreen = true; cfg.aspect = 2; cfg.scale = 2; }
	cfg.load(settings_path);
	// (the menus' language: Game > Language, Auto the system's; only what the menus and the game's own dialogs show
	// (choosing the ROM, a failed start), never the game, a match or a save)
	{
		tr::choice c;
		tr::choice_from(cfg.language, c);
		tr::set(tr::resolve(c));
		fprintf(stderr, "the menus' language: %s (Game > Language: %s)\n", tr::code(tr::current()), cfg.language.c_str());
	}
	if (cfg.upgraded) cfg.save(settings_path);   // (once: a 20 chosen later is the player's)
	if (arg_scale) cfg.scale = arg_scale;
	if (arg_aspect >= 0) cfg.aspect = arg_aspect;
	if (arg_full >= 0) cfg.fullscreen = arg_full;
	if (arg_arcade) cfg.arcade = true;
	if (arg_music >= 0) cfg.music = arg_music;
	if (arg_effects >= 0) cfg.effects = arg_effects;
	if (arg_volume >= 0) cfg.volume = arg_volume;
	g_pc_mode = !cfg.arcade;
#ifdef VF2_GPU3D_METAL
	{   // the GPU's 3D started while the game boots, if it'll be wanted (its shaders compiled meanwhile)
		const char *e = getenv("VF2_3D");
		if (e ? strcmp(e, "cpu") != 0 : cfg.renderer3d != 0) gpu3d_metal_prewarm();
	}
#endif
	const bool arcade_start = cfg.arcade;   // (the play style as the game started: the menu's change restarts it in the other)
	int aspect_i = cfg.aspect;
	const bool fullscreen = cfg.fullscreen;

	romset::result roms;
	if (players) {
		// the player's ROM folder: the command line's, else VF2_ROMS, else settings.ini's
		std::string roms_folder, from;
		if (!arg_roms.empty()) roms_folder = player_folder(arg_roms), from = "--roms";
		else if (const char *e = getenv("VF2_ROMS"); e && *e) roms_folder = player_folder(e), from = "VF2_ROMS";
		else if (!cfg.roms_folder.empty()) roms_folder = player_folder(cfg.roms_folder, data), from = "settings.ini";
#ifdef __ANDROID__
		else if (const char *ext = SDL_GetAndroidExternalStoragePath()) {
			// the phone's own folder, Android/data/<package>/files: a vf2.zip dropped there with
			// adb push or a file manager is played where it lies, without the first start's picker
			namespace fs = std::filesystem;
			std::error_code ec2;
			if (fs::is_regular_file(fs::path(ext) / "roms" / "vf2.zip", ec2))
				roms_folder = (fs::path(ext) / "roms").string(), from = "the phone's folder";
		}
#endif
		if (!roms_folder.empty()) fprintf(stderr, "ROM folder (%s): %s\n", from.c_str(), roms_folder.c_str());
		else fprintf(stderr, "ROM folder: %s\n", (std::filesystem::path(data) / "roms").string().c_str());
		std::string first_link;   // (a link that came while the ROM was being chosen: a Mac's, the app started by it)
		if (!first_launch(data, roms_folder, roms, first_link)) return hidden ? 1 : 0;   // (a test run without the game: an error)
		if (arg_link.empty() && !first_link.empty()) arg_link = first_link;
	} else {
		roms = romset::load(zip);
		if (!roms.ok) return fail(roms.error);
	}
	romset::result sound_roms = roms;   // (online play's sound board, made from them at the first match)
	// the match found chime: the game's coin sound, made by a sound board of its own on a thread of its own (about a
	// second of work, done long before any match)
	std::shared_future<std::vector<int16_t>> coin = std::async(std::launch::async, [&sound_roms] { return chime::coin_or_bell(coin_sound::make(sound_roms, LEVEL), coin_sound::RATE); }).share();
	if (const char *cw = getenv("VF2_TEST_CHIME_WAV")) {   // (tests: the match found chime's sound, the game's coin, to a .wav)
		const std::vector<int16_t> &pcm = coin.get();
		if (FILE *wf = fopen(cw, "wb")) {
			const uint32_t rate = coin_sound::RATE, br = rate * 4, db = uint32_t(pcm.size() * 2), sz = 36 + db, sixteen = 16;
			const uint16_t fmt = 1, ch = 2, al = 4, bits = 16;
			fwrite("RIFF", 1, 4, wf); fwrite(&sz, 4, 1, wf); fwrite("WAVEfmt ", 1, 8, wf); fwrite(&sixteen, 4, 1, wf); fwrite(&fmt, 2, 1, wf);
			fwrite(&ch, 2, 1, wf); fwrite(&rate, 4, 1, wf); fwrite(&br, 4, 1, wf); fwrite(&al, 2, 1, wf); fwrite(&bits, 2, 1, wf);
			fwrite("data", 1, 4, wf); fwrite(&db, 4, 1, wf); fwrite(pcm.data(), 2, pcm.size(), wf);
			fclose(wf);
		}
		fprintf(stderr, "(test) the match found chime: the game's coin sound, %.2f s\n", double(pcm.size() / 2) / coin_sound::RATE);
	}
	model2a m(std::move(roms));   // (recompiles the i960's program, unless VF2_INTERP)
	if (arg_interp) m.use_interpreter();
	if (m.recompiled()) {
		const i960rec::native_stats &st = m.native_stats();
		fprintf(stderr, "recompiled %zu functions (%zu instructions) to %zu KB of native code in %.0f ms\n", st.functions, st.instructions, st.code_bytes / 1024, st.ms);
	} else if (!arg_interp && !getenv("VF2_INTERP"))
		fprintf(stderr, "couldn't recompile the game (no executable memory?): running it on the slower interpreter instead\n");
	m.set_sound(sound);
	m.set_byte_link(true);   // the sound board's link a byte at a time (the same bytes, a lighter load)
	m.set_volumes(cfg.music / 100.0f, cfg.effects / 100.0f);
	m.set_level(LEVEL * cfg.volume / 100.0f);
	m.set_backdrop_filter(cfg.smooth_bg);
	m.set_hud_hidden(!cfg.hud);   // (Display > HUD: drawing only)
	m.set_smooth(true);   // the lists kept: smooth motion's pictures (cfg.smooth), and any picture drawn again (redraw)

	const std::string backup_path = data + "/backup1", eeprom_path = data + "/eeprom";
	// The arcade way's coin setting, which the PC way's free play replaces, kept in settings.ini
	// (arcade_coins: -1 none to give back, -2 none kept yet); earlier builds kept it in a file of
	// its own beside the backup, read once here
	if (cfg.arcade_coin == -2)
		if (FILE *f = fopen((data + "/arcade_coins").c_str(), "r")) {
			int v = -2;
			if (fscanf(f, "%d", &v) == 1 && v >= -1 && v < model2a::COIN_FREE_PLAY) { cfg.arcade_coin = v; cfg_dirty = true; }
			fclose(f);
		}
	// widescreen's painted sides: the shipped ones (assets/backgrounds), then the player's own, which win
	load_backgrounds(m, find_assets(data) + "/backgrounds");
	load_backgrounds(m, data + "/backgrounds");
	load_blob(backup_path, m.backup_ram().data(), m.backup_ram().size());
	load_blob(eeprom_path, m.eeprom_cells(), 64 * sizeof(u16));
	// the ranking's names for random fighters (model2a::ranking_frame), beside the backup (not in it: the backup is the
	// game's own, and an online match's start)
	const std::string rank_path = data + "/ranking_names.txt";
	if (FILE *f = fopen(rank_path.c_str(), "rb")) {
		std::string text;
		char buf[4096];
		for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
		fclose(f);
		m.set_ranking_names(text);
	}
	uint32_t rank_saved = m.ranking_names_version();
	// The backup as loaded: the autosave writes it once it differs (free play set at boot, the
	// game's first settings, an arcade setting changed in the first seconds).
	auto nvram_hash = [&] { return fnv(m.eeprom_cells(), 128, fnv(m.backup_ram().data(), m.backup_ram().size())); };
	uint64_t saved_hash = nvram_hash();
	bg_saver saver;   // (the autosave's writes, off the game's thread)
	bool nv_put = false;   // (the backup handed to it: the last save at quit writes it whatever, in case its write failed)
	auto save_nvram = [&](bool now) {
		if (quit_after >= 0 && !test_saves) return;   // test runs leave the saves alone
		if (g_match_board) return;   // (an online match's board holds the host's: this player's are given back after it)
		if (m.ranking_names_version() != rank_saved) {   // (the ranking's names: a few lines, written at once)
			std::filesystem::create_directories(data, ec);
			const std::string text = m.ranking_names();
			if (save_blob(rank_path, reinterpret_cast<const uint8_t *>(text.data()), text.size())) rank_saved = m.ranking_names_version();
		}
		const uint64_t h = nvram_hash();
		if (h == saved_hash && !(now && nv_put)) return;
		std::filesystem::create_directories(data, ec);
		if (!now) {
			nv_put = true;   // (play: handed to the writer; a failed write says so in the log, tried at the next change)
			saver.put(backup_path, m.backup_ram().data(), m.backup_ram().size());
			saver.put(eeprom_path, m.eeprom_cells(), 64 * sizeof(u16));
			saved_hash = h;
			return;
		}
		if (save_blob(backup_path, m.backup_ram().data(), m.backup_ram().size()) &&
			save_blob(eeprom_path, m.eeprom_cells(), 64 * sizeof(u16)))
			saved_hash = h;
		else fprintf(stderr, "couldn't save the NVRAM in %s\n", data.c_str());
	};
	// The PC way: the FREE PLAY notices blanked (always free play: nothing to say); the arcade way: shown
	auto apply_play_style = [&] {
		if (!g_pc_mode) m.show_free_play_text();
		else if (m.hide_free_play_text() != 2) fprintf(stderr, "not the data known: the FREE PLAY notices stay\n");
	};
	apply_play_style();

	// Boot. The settings as this host has them (the ring's width; in the PC way, free play) are put
	// into the game's own before it reads them; a board that has never run writes its settings in
	// its first frames, so those run first. Then, the PC way, the boot runs unseen up to the attract
	// (see the top).
	auto skip_boot = [&] { return g_pc_mode && !show_boot && !getenv("VF2_SCRIPT"); };
	if (skip_boot() && !m.shorten_warning())
		fprintf(stderr, "not the program known: the boot's WARNING runs in full (unseen)\n");
	uint64_t game_frame = 0;   // frames drawn since the boot (the script's frame numbers)
	auto boot = [&] {
		// (Arcade settings > Debug mode: never on in the boot's frames, which a restart that turned it off ran with it
		// on, from the frame before. The main loop turns it on again, offline, from the first frame after.)
		m.set_debug_mode(false);
		if (m.assignment(model2a::game_assignment::difficulty) < 0) {
			m.reset();
			for (int i = 0; i < 120 && m.assignment(model2a::game_assignment::difficulty) < 0; i++) m.run_frame();
			// A new player's game starts on Version 2.1 (the factory's is the original game, NORMAL):
			// only here, the board's first run, when the game has just written its settings; a
			// player's own choice after that (the menu's Version, the test mode) is kept. (The
			// MAME comparisons are vf2run's, from MAME's own factory settings, not this.)
			if (m.assignment(model2a::game_assignment::version) == 0) m.set_assignment(model2a::game_assignment::version, 1);
			// and two players' matches first to 3 rounds, as VF3's and SEGA's tournaments (the factory's is 2); the
			// player's choice after that kept
			m.set_assignment(model2a::game_assignment::match_vs, 3);
		}
		// (settings.ini from before version 5, or none: a 2, the old default, to 3 once, as a new player's; settings.ini
		// written, so a 2 chosen after stays)
		if (cfg.vs_rounds_to_3) {
			cfg.vs_rounds_to_3 = false;
			cfg_dirty = true;
			if (m.assignment(model2a::game_assignment::match_vs) == 2 && m.set_assignment(model2a::game_assignment::match_vs, 3)) {
				fprintf(stderr, "settings: Rounds to win (vs player) 2 (the old default) moved to 3, the new one\n");
				save_nvram(false);
			}
		}
		m.set_ring_width(cfg.ring);
		// Cheats' fighters (random_fighter.cpp): each player's; a name no seed gives now plays as Random. A player's
		// random fighter is made in MUE's or KOJAC's place, so the Secret character's MUE and KOJAC are off meanwhile
		for (int p = 0; p < 2; p++)
			if (!m.set_random_player(p, cfg.random_player[p])) {
				if (cfg.random_named[p] == cfg.random_player[p]) cfg.random_named[p].clear();
				cfg.random_player[p] = "*";
				m.set_random_player(p, "*");
				cfg_dirty = true;
			}
		m.set_random_opponents(cfg.random_cpu);
		const bool random_players = !cfg.random_player[0].empty() || !cfg.random_player[1].empty();
		const int secret = random_players && (cfg.secret == 11 || cfg.secret == 12) ? 0 : cfg.secret;
		if (!m.set_secret_character(secret)) fprintf(stderr, "secret character: not the program known\n");
		if (!m.apply_random_fighters()) fprintf(stderr, "random fighters: not the program known\n");
		// The PC way plays free: the arcade's coin setting is kept (settings.ini's arcade_coins) before
		// FREE PLAY replaces it, and the arcade way gives it back. Free play the PC way set before it
		// was kept (earlier builds) gives back the factory's (a coin a credit); FREE PLAY the player
		// chose in the arcade's test mode stays.
		if (const int coin = m.coin_setting(); coin >= 0) {
			int keep = cfg.arcade_coin;
			if (g_pc_mode) {
				if (coin != model2a::COIN_FREE_PLAY) keep = coin;
				else if (keep == -2) keep = model2a::COIN_FACTORY;
				m.force_free_play();
			} else {
				if (coin == model2a::COIN_FREE_PLAY && keep != -1) m.set_coin_setting(keep >= 0 ? keep : model2a::COIN_FACTORY);
				keep = -1;
			}
			if (keep != cfg.arcade_coin) { cfg.arcade_coin = keep; cfg_dirty = true; }
			if (g_pc_mode && !m.free_play()) fprintf(stderr, "the PC way: couldn't set FREE PLAY in the game's settings (coin setting %d)\n", m.coin_setting());
		}
		m.reset();
		m.forget_boot();
		m.m_audio_out.clear();
		game_frame = 0;
		if (!skip_boot()) return;
		while (!m.past_boot() && game_frame < BOOT_FRAMES) {
			m.run_frame();
			m.m_audio_out.clear();
			game_frame++;
		}
	};
	const auto boot_t = std::chrono::steady_clock::now();
	boot();
	if (pace_log)
		fprintf(stderr, "boot: %llu frames in %.3f s%s\n", (unsigned long long)game_frame, seconds_since(boot_t), skip_boot() ? ", unseen" : "");

#ifdef _WIN32
	// Windows only lets the program in front hand over the foreground: started from another program,
	// the game could open behind it with the keys going there. SDL's raise can force it.
	SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");
#endif
	if (getenv("VF2_PAD_KEYS") || getenv("VF2_TEST_VJOY")) SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");   // (tests: the window may not have the focus)
#ifdef __ANDROID__
	// Android's Back button: kept by the app (the key comes to us as AC_BACK), so it opens the menu
	// instead of closing the game (the SDLActivity's onBackPressed asks for this hint by name)
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif
	if (!SDL_Init(SDL_INIT_VIDEO)) return fail(tr::line(tr::FAIL_VIDEO, { { "why", SDL_GetError() } }));
	if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) fprintf(stderr, "no gamepads: %s\n", SDL_GetError());
	// VF2_TEST_VJOY (tests): a virtual stick SDL doesn't know (10 buttons, a hat, 2 axes), pressed as it says (below)
	SDL_Joystick *test_vjoy = nullptr;
	auto attach_vjoy = [&] {
		SDL_VirtualJoystickDesc d;
		SDL_INIT_INTERFACE(&d);
		d.type = SDL_JOYSTICK_TYPE_UNKNOWN;
		d.vendor_id = 0x1234;
		d.product_id = 0x5678;
		d.naxes = 2;
		d.nbuttons = 10;
		d.nhats = 1;
		d.name = "VF2 Test Stick";
		const SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
		test_vjoy = id ? SDL_OpenJoystick(id) : nullptr;
		fprintf(stderr, "test: a virtual stick: %s, %s\n", id ? joystick_guid(id).c_str() : SDL_GetError(),
		        id && SDL_IsGamepad(id) ? "a gamepad to SDL" : "not a gamepad to SDL");
	};
	if (const char *v = getenv("VF2_TEST_VJOY"); v && !strchr(v, 'p')) attach_vjoy();   // (p@N: plugged in at pass N instead)
	load_pad_mappings(data, program_folder(), cfg.pad_mappings);
	give_unknown_sticks();
	if (sound && !SDL_InitSubSystem(SDL_INIT_AUDIO)) fprintf(stderr, "no sound: %s\n", SDL_GetError());
	int win_w = 1366, win_h = 768;   // 16:9 (within 90% of a smaller screen)
	// testing: VF2_WINDOW=WxH, the window's size in points (a hidden one as large as a player's screen)
	const bool fixed_window = getenv("VF2_WINDOW") && sscanf(getenv("VF2_WINDOW"), "%dx%d", &win_w, &win_h) == 2;
	bool win_placed = false;   // (the window put where it was last left; not on a phone, whose window is the screen)
	int win_x = 0, win_y = 0;
	if (!fixed_window) {
		SDL_Rect usable;
		if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w > 0 && usable.h > 0) {
			const float k = std::min({ 1.0f, usable.w * 0.9f / win_w, usable.h * 0.9f / win_h });
			win_w = int(win_w * k); win_h = int(win_h * k);
		}
#if !defined(__ANDROID__)
		// The window as last left (settings.ini's window_w ...): there if it's still all on a connected screen's usable
		// area; else at that size (no larger than the screen) in the middle of the screen it was most on, or the primary
		if (cfg.window_w > 0) {
			int n = 0;
			SDL_DisplayID *ds = SDL_GetDisplays(&n);
			const SDL_Rect r{ cfg.window_x, cfg.window_y, cfg.window_w, cfg.window_h };
			SDL_Rect best{ 0, 0, 0, 0 };
			long long best_area = -1;
			for (int i = 0; ds && i < n; i++) {
				SDL_Rect u, in;
				if (!SDL_GetDisplayUsableBounds(ds[i], &u) || u.w <= 0 || u.h <= 0) continue;
				const long long a = SDL_GetRectIntersection(&r, &u, &in) ? (long long)in.w * in.h : 0;
				if (r.x >= u.x && r.y >= u.y && r.x + r.w <= u.x + u.w && r.y + r.h <= u.y + u.h) { best = u; best_area = LLONG_MAX; break; }
				if (a > best_area || (best_area <= 0 && ds[i] == SDL_GetPrimaryDisplay())) { best = u; best_area = a; }
			}
			SDL_free(ds);
			if (best.w > 0) {
				win_w = std::min(cfg.window_w, best.w);
				win_h = std::min(cfg.window_h, best.h);
				if (best_area == LLONG_MAX) { win_x = cfg.window_x; win_y = cfg.window_y; }
				else { win_x = best.x + (best.w - win_w) / 2; win_y = best.y + (best.h - win_h) / 2; }
				win_placed = true;
				fprintf(stderr, "the window as last left: %d x %d at %d, %d%s%s\n", win_w, win_h, win_x, win_y,
				        best_area == LLONG_MAX ? "" : " (centred: not all on a screen now)", cfg.window_max ? ", maximised" : "");
			}
		}
#endif
	}
	// hidden until it has the game's first picture to show
	// A phone's window is the screen, and it can't be resized. SDL_WINDOW_RESIZABLE there also has
	// SDL ask Android for "any orientation, as the player locked the rotation" (SDLActivity's
	// setOrientationBis: a resizable window with no orientation hint is SCREEN_ORIENTATION_FULL_USER)
	// instead of the landscape the manifest asks for: a session on a phone held with the rotation
	// locked came up portrait, 1220 x 2421 in the log, its picture a 4:3 band down the middle.
	// Fullscreen's way (Config's fs_window): a borderless window over the screen and a line past it, so no driver
	// takes it for a game's own fullscreen (VF3's issue #7: AMD's Vulkan on Windows froze there, every frame waiting),
	// the window's place, size and maximised kept for the way back; or the system's own (SDL_WINDOW_FULLSCREEN)
#ifdef _WIN32
	static constexpr bool FS_BORDERLESS_DEFAULT = true;
#else
	static constexpr bool FS_BORDERLESS_DEFAULT = false;   // (a Mac's green button and Spaces, the Deck's gamescope)
#endif
	auto fs_borderless = [&] { return !cfg.fs_exclusive && (FS_BORDERLESS_DEFAULT || cfg.fs_window); };
	SDL_PropertiesID wp = SDL_CreateProperties();
	SDL_SetStringProperty(wp, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Virtua Fighter 2");
	SDL_SetNumberProperty(wp, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, win_w);
	SDL_SetNumberProperty(wp, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, win_h);
	if (win_placed) {
		SDL_SetNumberProperty(wp, SDL_PROP_WINDOW_CREATE_X_NUMBER, win_x);
		SDL_SetNumberProperty(wp, SDL_PROP_WINDOW_CREATE_Y_NUMBER, win_y);
	}
	SDL_SetNumberProperty(wp, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,
#if defined(__ANDROID__)
		SDL_WINDOW_HIGH_PIXEL_DENSITY | (fullscreen ? SDL_WINDOW_FULLSCREEN : 0) | SDL_WINDOW_HIDDEN);
#else
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (fullscreen && !fs_borderless() ? SDL_WINDOW_FULLSCREEN : 0) |
		(win_placed && cfg.window_max ? SDL_WINDOW_MAXIMIZED : 0) | SDL_WINDOW_HIDDEN);
#endif
	SDL_Window *win = SDL_CreateWindowWithProperties(wp);
	bool fs_win_on = false, fs_win_max = false;
	SDL_Rect fs_win_was{ 0, 0, 0, 0 };
	auto is_fullscreen = [&] { return fs_win_on || (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0; };
	SDL_DestroyProperties(wp);
	if (!win) return fail(tr::line(tr::FAIL_WINDOW, { { "why", SDL_GetError() } }));
	SDL_HideCursor();
	int focus_checks = 600;   // about 5 s: going fullscreen on a Mac moves to a new Space, which takes a while
	SDL_Renderer *ren = nullptr;
	// Why the 3D is on the CPU when the setting isn't CPU: said on screen, not only in the log (a player whose GPU
	// couldn't be used just saw a slow game). cpu_why_new: not said yet.
	// (its words, tr: why, what to do, and the scale cut for the run said with it: cpu_notice)
	std::string cpu_why;          // (why, in English: empty none said)
	tr::line cpu_why_line, cpu_todo;
	bool cpu_why_new = false, cpu_why_cut = false;
	bool gpu_crashed = false;   // (the last start closed while starting the GPU: the menu's row says so until a choice there)
	bool gpu_unavailable = false;   // (the gpu renderer won't start here, or isn't tried (CRT Emudriver): the row's not "from the next start")
	// (why: what happened, in a player's words; then what to do. None with a renderer chosen by hand, SDL_RENDER_DRIVER)
#ifdef __APPLE__
	static const tr::text UPDATE = tr::CPU_TODO_UPDATE_MAC;
#else
	static const tr::text UPDATE = tr::CPU_TODO_UPDATE;
#endif
	// (every start tries the GPU again, a failure remembered only for Windows' order of drivers: only a crashed start
	// waits for the player)
	static const tr::text RETRY = tr::CPU_TODO_RETRY;
	auto on_cpu_because = [&](tr::text why, tr::text what_to_do) {
		if (cpu_why.empty() && !getenv("SDL_RENDER_DRIVER")) { cpu_why_line = why; cpu_todo = what_to_do; cpu_why = cpu_why_line.en(); cpu_why_new = true; }
	};
	auto cpu_notice = [&] { return tr::line(cpu_why_cut ? tr::CPU_NOTICE_CUT : tr::CPU_NOTICE, { { "why", cpu_why_line }, { "todo", cpu_todo } }); };
	// (the GPU start's marker kept through the first 15 s of play, its shaders, first pictures and checks too, a driver
	// that closes the game there caught as well as one in its start; taken away then, or on quitting)
	std::string marker_live;
	uint64_t marker_until = 0;
	auto marker_done = [&](bool now) {
		if (marker_live.empty() || (!now && SDL_GetTicks() < marker_until)) return;
		SDL_RemovePath(marker_live.c_str());
		marker_live.clear();
	};
#ifdef VF2_GPU3D_SDL
	// The 3D on the GPU wanted (Auto or GPU, the setting or VF2_3D): SDL's "gpu" renderer (Direct3D
	// 12 on Windows, Vulkan on Linux and the Deck), whose device the 3D's compute shares, its
	// pictures its textures. Else, or if it can't start here, SDL's usual one (and the CPU's 3D).
	// (SDL_RENDER_DRIVER, SDL's own, chooses over this.)
	{
		const char *e = getenv("VF2_3D");
		// (2026-10-15, vf2-recomp #6: an HD 7800 with CRT Emudriver closed inside the gpu renderer's start,
		// no message, every start since v0.1.2. A marker is left while it starts: there next time, it
		// crashed then, so SDL's usual renderer and the CPU's 3D, said so. Deleting gpu-start.crashed
		// tries again.)
		const std::string marker = data + "/gpu-start.crashed";
		// (and a driver known to crash there isn't tried at all: CRT Emudriver, the modified AMD driver
		// arcade cabinets use, names itself in the card's name)
		const std::string cards = sysinfo::gpu_names();
		const bool known_bad = cards.find("CRT Emudriver") != std::string::npos;
		if (known_bad && (e ? strcmp(e, "cpu") != 0 : cfg.renderer3d != 0))
			{ gpu_unavailable = true; fprintf(stderr, "SDL's gpu renderer: not tried with CRT Emudriver (its start crashes there): SDL's usual renderer, the CPU's 3D\n"); on_cpu_because(tr::CPU_WHY_EMUDRIVER, tr::CPU_TODO_CHOOSE_CPU); }
		// (the marker holds the build that left it: another build's, a driver or a fix since, the GPU tried again)
		std::string marker_build;
		bool marker_there = false;
		if (SDL_GetPathInfo(marker.c_str(), nullptr)) {
			marker_there = true;
			size_t n = 0;
			if (void *d = SDL_LoadFile(marker.c_str(), &n)) { marker_build.assign(static_cast<const char *>(d), n); SDL_free(d); }
		}
		bool crashed = known_bad || (marker_there && marker_build == settings::BUILD);
		auto failed_before = [&](const char *d) { return (" " + cfg.gpu3d_failed + " ").find(std::string(" ") + d + " ") != std::string::npos; };
#ifdef _WIN32
		// (Windows: the driver that start was on passed over for this build and the other tried now, not the CPU: a
		// Vulkan driver that closes the game goes to Direct3D 12 by itself, and the other way round. Both: the CPU.)
		if (crashed && !known_bad && !getenv("SDL_GPU_DRIVER")) {
			const bool card = !cards.empty() && !(cards.rfind("Microsoft", 0) == 0 && cards.find(", ") == std::string::npos);
			const bool was_vk = (failed_before("direct3d12") || (card && cfg.graphics == 0)) && !failed_before("vulkan");
			const char *drv = was_vk ? "vulkan" : "direct3d12", *other = was_vk ? "direct3d12" : "vulkan";
			if (!failed_before(other)) {
				fprintf(stderr, "SDL's gpu renderer: the last start closed while starting it on %s: %s this start\n", drv, other);
				if (!failed_before(drv)) cfg.gpu3d_failed += (cfg.gpu3d_failed.empty() ? "" : " ") + std::string(drv);
				cfg.save(settings_path);   // (now: should the other close the game too, the next start knows both did)
				crashed = false;
				marker_there = false;   // (said)
			}
		}
#endif
		if (crashed && !known_bad && (e ? strcmp(e, "cpu") != 0 : cfg.renderer3d != 0) && !getenv("SDL_RENDER_DRIVER"))
			{ fprintf(stderr, "SDL's gpu renderer: the last start closed while starting it (%s): SDL's usual renderer, the CPU's 3D (a new build tries it again)\n", marker.c_str()); gpu_crashed = !getenv("SDL_RENDER_DRIVER"); on_cpu_because(tr::CPU_WHY_CLOSED, RETRY); }
		else if (marker_there && !known_bad && !crashed && (e ? strcmp(e, "cpu") != 0 : cfg.renderer3d != 0) && !getenv("SDL_RENDER_DRIVER"))
			fprintf(stderr, "SDL's gpu renderer: %s closed while starting it: this build tries it again\n", marker_build.empty() ? "an older build" : marker_build.c_str());
		if (!crashed && (e ? strcmp(e, "cpu") != 0 : cfg.renderer3d != 0) && !getenv("SDL_RENDER_DRIVER")) {
			write_marker(marker, settings::BUILD);   // (flushed to the disk: a crash that takes the machine down keeps it)
			const auto t0 = std::chrono::steady_clock::now();
			// (a driver whose 3D failed its check here before is passed over: Direct3D 12's for Vulkan,
			// SDL's other on Windows (some older cards draw nothing on one and are fine on the other);
			// SDL_GPU_DRIVER, SDL's own, chooses over this)
			// (vf2-recomp #19: an RX 9070 on Direct3D 12 took 15 s to start and ~29 ms to take each
			// picture, 30 a second at any scale; on Vulkan 6x ran at the screen's rate. A Radeon's
			// Vulkan first then, and since #21 and #22 (NVIDIA: ~100 ms a 3D picture on Direct3D 12,
			// ~5-10 on Vulkan) any card's on Windows, not Microsoft's software one; Direct3D 12 if
			// Vulkan won't start or draws no 3D, remembered, or if Display > Graphics driver says so)
#ifdef _WIN32
			const bool card = !cards.empty() && !(cards.rfind("Microsoft", 0) == 0 && cards.find(", ") == std::string::npos);
#else
			const bool card = false;
#endif
			const bool vk_first = card && cfg.graphics == 0 && !failed_before("vulkan") && !getenv("SDL_GPU_DRIVER");
			const bool try_vulkan = (failed_before("direct3d12") || vk_first) && !failed_before("vulkan") && !getenv("SDL_GPU_DRIVER");
			if (try_vulkan) {
				fprintf(stderr, "SDL's gpu renderer: Vulkan (%s)\n", failed_before("direct3d12") ? "the 3D failed on Direct3D 12 here before" : "Windows: Vulkan first");
				SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");
			}
			ren = SDL_CreateRenderer(win, "gpu");
			if (!ren && try_vulkan) {   // (no Vulkan here: Direct3D 12 again, the 3D on the CPU)
				fprintf(stderr, "SDL's gpu renderer: Vulkan won't start (%s): Direct3D 12\n", SDL_GetError());
				cfg.gpu3d_failed += " vulkan";
				cfg_dirty = true;
				SDL_ResetHint(SDL_HINT_GPU_DRIVER);
				ren = SDL_CreateRenderer(win, "gpu");
			}
			// A first look, before anything is drawn: the made-up test scene on the renderer's device
			// and on the CPU, and if the GPU's comes back empty where the CPU's is drawn (vf2-recomp #3:
			// a GTX 970 drew nothing on Direct3D 12), the renderer made again on Vulkan at once, not
			// from the next start. (Only that case: the full check, pixel for pixel, comes later as
			// before.) 1 drawn, 0 empty, -1 not looked at.
			auto draws_3d = [&](SDL_Renderer *r) -> int {
				SDL_GPUDevice *gd = r ? SDL_GetGPURendererDevice(r) : nullptr;
				std::unique_ptr<gpu3d> dev = gd ? gpu3d_create_sdlgpu(gd) : nullptr;
				if (!dev) return -1;
				model2_video &v = m.video();
				int w = 0, h = 0;
				v.render_size(w, h);
				if (w <= 0 || h <= 0) return -1;
				v.bench_make(1500);
				std::vector<gpu3d_poly> list;
				v.bench_export(list);
				gpu3d_rams rams;
				v.gpu_rams(rams);
				dev->set_rams(rams);
				gpu3d_frame f;
				f.w = w; f.h = h; f.scale = std::max(1, h / 384);
				if (!dev->submit(list.data(), list.size(), f, -1) || !dev->finish()) return 0;
				std::vector<uint32_t> gp(size_t(w) * h, 0);
				if (!dev->read_picture(gp.data(), size_t(w))) return -1;
				v.bench_cpu(0, 1);
				const bitmap_rgb32 &c = v.bench_picture();
				if (c.width() != w || c.height() != h) return -1;
				long long cpu_drawn = 0, gpu_drawn = 0;
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++) { cpu_drawn += (c.pix(y, x) & 0xffffff) != 0; gpu_drawn += (gp[size_t(y) * w + x] & 0xffffff) != 0; }
				const long long px = (long long)w * h;
				return cpu_drawn > px / 2 && gpu_drawn < px / 100 ? 0 : 1;
			};
			if (ren && !getenv("SDL_GPU_DRIVER") && !getenv("VF2_GPU3D_NO_PROBE") && !(failed_before("direct3d12") && failed_before("vulkan"))) {
				SDL_GPUDevice *gd = SDL_GetGPURendererDevice(ren);
				const char *drv = gd ? SDL_GetGPUDeviceDriver(gd) : nullptr;
				if (drv && !strcmp(drv, "vulkan") && vk_first && !failed_before("direct3d12") && draws_3d(ren) == 0) {
					// (Vulkan chosen first draws nothing: Direct3D 12, as before #19)
					fprintf(stderr, "SDL's gpu renderer: Vulkan draws no 3D here: Direct3D 12 instead\n");
					cfg.gpu3d_failed += (cfg.gpu3d_failed.empty() ? "" : " ") + std::string("vulkan");
					cfg_dirty = true;
					SDL_DestroyRenderer(ren);
					SDL_ResetHint(SDL_HINT_GPU_DRIVER);
					ren = SDL_CreateRenderer(win, "gpu");
				} else if (drv && !strcmp(drv, "direct3d12") && draws_3d(ren) == 0) {
					fprintf(stderr, "SDL's gpu renderer: Direct3D 12 draws no 3D here: Vulkan instead\n");
					if (!failed_before("direct3d12")) { cfg.gpu3d_failed += (cfg.gpu3d_failed.empty() ? "" : " ") + std::string("direct3d12"); cfg_dirty = true; }
					SDL_DestroyRenderer(ren);
					SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");
					ren = SDL_CreateRenderer(win, "gpu");
					const int vk = ren ? draws_3d(ren) : -1;
					if (!ren || vk == 0) {   // (no Vulkan, or no 3D on it either: Direct3D 12 for the 2D, the 3D on the CPU)
						fprintf(stderr, "SDL's gpu renderer: Vulkan %s: Direct3D 12, the 3D on the CPU\n", ren ? "draws no 3D either" : "won't start");
						if (ren) SDL_DestroyRenderer(ren);
						if (!failed_before("vulkan")) { cfg.gpu3d_failed += " vulkan"; cfg_dirty = true; }
						SDL_ResetHint(SDL_HINT_GPU_DRIVER);
						ren = SDL_CreateRenderer(win, "gpu");
					} else fprintf(stderr, "SDL's gpu renderer: Vulkan draws the 3D here\n");
				}
			}
			marker_live = marker; marker_until = SDL_GetTicks() + 15000;
			if (!ren) { fprintf(stderr, "SDL's gpu renderer: %s (SDL's usual renderer, the CPU's 3D)\n", SDL_GetError()); on_cpu_because(tr::CPU_WHY_RENDERER, UPDATE); gpu_unavailable = true; }
			else fprintf(stderr, "SDL's gpu renderer started in %.0f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
		}
	}
#endif
	if (!ren) ren = SDL_CreateRenderer(win, nullptr);
	if (!ren) return fail(tr::line(tr::FAIL_GRAPHICS, { { "why", SDL_GetError() } }));
	// Display's two settings for the renderer in use (the menu's rows call this too):
	//
	// vsync: on, the picture is shown in step with the screen (no tearing, and every picture waits
	// for the screen's next refresh); off, no waiting at all.
	//
	// frames in flight: SDL's own gpu renderer makes this 1 the moment it starts, for the shortest
	// delay between a finger and the screen. What that costs is invisible on a desktop and heavy on
	// a phone: every SDL_RenderPresent waits for the picture before it to have been drawn, so the
	// drawing and the GPU's work are one step after another instead of alongside, and the GPU's own
	// time for a picture lands in the frame's (a picture shown still drawing's fence is what the log
	// prints as "a picture N ms on the GPU"). 2 or 3 let the drawing get ahead of the GPU: the same
	// pictures, more of them a second, one or two frames more delay. Whatever's asked for here, the
	// log's render state line and the 10 s pace lines are where it's seen.
	auto apply_display_settings = [&] {
		const int want_vsync = getenv("VF2_NO_VSYNC") ? 0 : cfg.vsync;   // (VF2_NO_VSYNC=1: tests, as a driver forcing it off)
		if (!SDL_SetRenderVSync(ren, want_vsync)) fprintf(stderr, "vsync %d: %s\n", want_vsync, SDL_GetError());
		const char *rname = SDL_GetRendererName(ren);
		SDL_GPUDevice *gd = rname && !strcmp(rname, "gpu") ? SDL_GetGPURendererDevice(ren) : nullptr;
		if (gd && !SDL_SetGPUAllowedFramesInFlight(gd, Uint32(cfg.frames_in_flight)))
			fprintf(stderr, "the GPU's frames in flight (%d): %s\n", cfg.frames_in_flight, SDL_GetError());
		// What the driver was asked for and which driver it is. vsync off asks for mailbox first,
		// then immediate, and only then the screen's own pace (SDL_render_gpu.c's ChoosePresentMode)
		// - which of the three the phone's window accepts is not otherwise visible, and mailbox is
		// what the phone's own numbers said pay for a high picture rate. The driver's name, version
		// and info are worth a line of their own: Android's graphics drivers arrive with system
		// updates, and Arm's own advice is tied to driver releases.
		if (gd) {
			const bool mailbox = SDL_WindowSupportsGPUPresentMode(gd, win, SDL_GPU_PRESENTMODE_MAILBOX);
			const bool immediate = SDL_WindowSupportsGPUPresentMode(gd, win, SDL_GPU_PRESENTMODE_IMMEDIATE);
			const char *mode = want_vsync ? "vsync" : mailbox ? "mailbox" : immediate ? "immediate" : "vsync";
			SDL_PropertiesID pr = SDL_GetGPUDeviceProperties(gd);
			const char *dn = SDL_GetStringProperty(pr, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, nullptr);
			const char *dv = SDL_GetStringProperty(pr, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, nullptr);
			const char *di = SDL_GetStringProperty(pr, SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING, nullptr);
			std::string drv = std::string(dn ? dn : "?") + " " + (dv ? dv : "?");
			if (di && *di) drv += " (" + std::string(di) + ")";
			fprintf(stderr, "the GPU's present mode: %s (vsync %s; mailbox%s, immediate%s); the driver: %s\n",
				mode, want_vsync ? "on" : "off", mailbox ? "" : " no", immediate ? "" : " no", drv.c_str());
		}
	};
	apply_display_settings();
	// Android's performance hints (sysinfo::perf_hints_*, ADPF): a session over the threads a frame's
	// work runs on, with the frame's own 17.4 ms as its target, so the phone's scheduler keeps their
	// cores - and the GPU's clocks - up instead of taking the game for a program in the background
	// (MediaTek's PowerHAL is said to; the Android counterpart of tune()'s Windows power throttling).
	// The work each pass really took is reported back further down the loop. Display > Performance
	// hints turns it off to be compared, and VF2_NO_PERF_HINTS=1 does the same for a run.
	bool perf_hints_up = false;
	auto apply_perf_hints = [&] {
		sysinfo::perf_hints_stop();
		perf_hints_up = false;
#if defined(__ANDROID__)
		if (cfg.perf_hints && !getenv("VF2_NO_PERF_HINTS"))
			perf_hints_up = sysinfo::perf_hints_start(row_pool::get().worker_tids(), int64_t(1e9 / FRAME_HZ));
#endif
	};
	// The picture's shape: the chosen one, or the window's (fit: between 4:3 and 80:9)
	auto window_aspect = [&] {
		int w = 0, h = 0;
		SDL_GetWindowSizeInPixels(win, &w, &h);
		if ((SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) && (SDL_GetWindowFlags(win) & SDL_WINDOW_HIDDEN))   // (it takes the screen's size once shown)
			if (const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay())) { w = dm->w; h = dm->h; }
		return h > 0 ? std::clamp(float(w) / float(h), 4.0f / 3.0f, 80.0f / 9.0f) : 16.0f / 9.0f;
	};
	auto apply_aspect = [&] {   // true if the picture's shape changed
		const float a = ASPECTS[aspect_i] > 0 ? ASPECTS[aspect_i] : window_aspect();
		if (std::fabs(a - m.aspect()) < 0.001f) return false;
		const int ext = m.side_columns();
		m.set_aspect(a);
		if (g_match_board) m.set_scenery_culling(vf2net::MATCH_CULLING);   // (a match plays the same program on both sides, whatever the shape)
		return m.side_columns() != ext;
	};
	apply_aspect();
	// 496 x 384 fills the cabinet's 4:3 tube: shown as 512 x 384 (wider, 384 * its width / 372),
	// letterboxed in the window
	auto present_shape = [&] {
		SDL_SetRenderLogicalPresentation(ren, int(std::lround(384.0 * m.width() / 372.0)), 384, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	};
	present_shape();

	// The render scale: the player's, else the one whose picture is at least as tall as the
	// window's (4:3 in it), at least 2x and within DEFAULT_MAX_SCALE, less on a CPU with few threads
	// (3x from 6, 4x from 8: the 3D is drawn on the CPU). Antialiasing draws larger and averages down:
	// within 8x the arcade's size in all (at 32:9 that's already 10,576 x 3,072).
	int scale = cfg.scale, aa = cfg.aa;   // as in use (the governor may lower them for this run)
	if (!scale) {
		int pw = 0, ph = 0;
		SDL_GetWindowSizeInPixels(win, &pw, &ph);
		if (fullscreen) {   // (the window takes the screen's size once it's shown)
			if (const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay())) {
				pw = int(dm->w * dm->pixel_density); ph = int(dm->h * dm->pixel_density);
			}
		}
		const int tall = std::min(double(ph), pw * 372.0 / m.width());
		const int cores = int(std::thread::hardware_concurrency());
		const int most = cores >= 8 ? DEFAULT_MAX_SCALE : cores >= 6 ? 3 : 2;
		scale = std::clamp(int(std::ceil(tall / double(model2a::HEIGHT) - 0.1)), 2, std::max(2, most));
	}
	// The picture drawn at most as wide as 32:9 at 8x (10,576 pixels: 80:9's 3,306 at 8x would be 26,448, and
	// its buffers 1.5 GB): wider shapes at a lower scale and antialiasing, the player's own kept for narrower
	auto scale_most = [&] { return std::max(1, (model2a::WIDTH + 2 * 413) * 8 / m.width()); };
	int scale_wanted = scale;   // (the scale asked for, the player's or the governor's, before the width's limit)
	scale = std::min(scale, scale_most());
	auto aa_fit = [&](int s, int a) { return std::max(1, std::min({ a, 8 / s, scale_most() / std::max(1, s) })); };   // (0, Low: at the scale, the 3D's edges smoothed)
	m.set_render_scale(scale * aa_fit(scale, aa));
	m.set_edge_aa(aa == 0);
	auto aa_now = [&] { return m.render_scale() / std::max(1, scale); };   // the antialiasing in use
	// The picture's texture, the render scale's size (antialiasing averaged into it): a card that
	// can't make one that large gets a lower scale.
	SDL_Texture *tex = nullptr;
	char title_base[96] = "";
	// Pipelining, when running a frame and then drawing it no longer fit in a frame's time (see the
	// governor): each picture is drawn alongside the next frame (model2a::render_async) into a
	// second texture, held locked meanwhile, which goes up a pass later in place of the first.
	// The same pictures, a frame later.
	const int pipeline_force = getenv("VF2_PIPELINE") ? atoi(getenv("VF2_PIPELINE")) : -1;   // tests: 0 never, 1 always
	bool pipelined = pipeline_force == 1;
	SDL_Texture *tex_next = nullptr;   // (made when first needed, at tex's size)
	bool async_pending = false;
	// The 2D on the GPU (gpu_picture.h): the board's picture in parts, put together by the renderer
	// in its target texture, on show in place of `tex` (gpu.shown); the same pixels (the
	// background filter's smooth enlargement within a level or two). From 2x: at 1x putting the
	// picture together is cheaper than handing the GPU its 3D (on an M4: 1.1 ms a picture on the
	// CPU, 1.3 on the GPU; 2x even; at 4x the GPU's 5-9% quicker, with 5-9% less CPU). Not with
	// antialiasing (drawn larger, then averaged on the CPU), nor on SDL's software renderer or an
	// OpenGL drawn on the CPU (their blending is slower than the board's own: in the Steam Runtime
	// container, Mesa's llvmpipe, 2x went from 86 pictures a second to 25-43), nor in render test
	// mode (its 3D goes between the 2D's passes): those are drawn on the CPU as before. VF2_GPU: 0
	// never, 1 at any scale and even on the software renderer. The 3D's texture is held locked while the board draws: smooth motion's 3D
	// goes straight into it, a frame's own is copied in from where it was drawn (on every core);
	// VF2_GPU_3D=update: the renderer copies it in instead. VF2_GPU_CHECK=n: every n-th picture read
	// back and checked (the log).
	bool deck = false;
	gpu_picture gpu_pic(ren);
	struct { bool on = false, any_scale = false, shown = false, pending = false; model2a::picture_parts parts; } gpu;
	bool soft_render = false;   // (the renderer is the CPU's: SDL's software one, llvmpipe, SwiftShader. Auto measures there)
	{
		const char *g = getenv("VF2_GPU");
		const char *name = SDL_GetRendererName(ren);
		// (OpenGL drawn on the CPU, Mesa's llvmpipe say, as in a container: as the software renderer)
		const char *gl = nullptr;
		if (name && !strncmp(name, "opengl", 6))
			if (auto get = reinterpret_cast<const unsigned char *(*)(unsigned)>(SDL_GL_GetProcAddress("glGetString")))
				gl = reinterpret_cast<const char *>(get(0x1F01));   // GL_RENDERER
		if (gl) fprintf(stderr, "OpenGL: %s\n", gl);
		// (SDL's gpu renderer: the device's name, as GL_RENDERER: Mesa's llvmpipe is Vulkan drawn on the CPU)
		if (SDL_GPUDevice *gd = name && !strcmp(name, "gpu") ? SDL_GetGPURendererDevice(ren) : nullptr) {
			gl = SDL_GetStringProperty(SDL_GetGPUDeviceProperties(gd), SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr);
			fprintf(stderr, "SDL_GPU: %s, %s\n", SDL_GetGPUDeviceDriver(gd), gl ? gl : "?");
		}
		deck = getenv("SteamDeck") || (gl && (strstr(gl, "vangogh") || strstr(gl, "VANGOGH")));   // (a Steam Deck's GPU, Steam or not)
		const bool soft_gl = gl && (strstr(gl, "llvmpipe") || strstr(gl, "softpipe") || strstr(gl, "Software") || strstr(gl, "SwiftShader"));
		soft_render = (name && !strcmp(name, "software")) || soft_gl;
		gpu.on = g ? atoi(g) != 0 : !(name && !strcmp(name, "software")) && !soft_gl;
		gpu.any_scale = g && atoi(g) == 1;
		if (const char *c = getenv("VF2_GPU_CHECK")) gpu_pic.check_every = std::max(0, atoi(c));
		fprintf(stderr, "renderer %s: the 2D %s\n", name ? name : "?", !gpu.on ? "put together on the CPU" : gpu.any_scale ? "put together on the GPU" : "put together on the GPU from 2x");
	}
	// Pipelined from the first frame where it will be wanted: on a Steam Deck (a fight at 2x is
	// about 16 ms of its 17.4 a frame, and switched on in the first fight the pictures dipped for a
	// moment there), and at the scale a run before needed it at (the player's change of scale
	// forgets that). A frame's delay, and no picture is lost to the switch.
	auto pipeline_wanted = [&] { return pipeline_force == 1 || (pipeline_force < 0 && (deck || (cfg.pipelined_at && cfg.pipelined_at == scale))); };
	pipelined = pipeline_wanted();
	const bool gpu_lock3d = !(getenv("VF2_GPU_3D") && !strcmp(getenv("VF2_GPU_3D"), "update"));
	// A texture is held locked across a present (pipelining: drawn into alongside the next frame)
	// only on renderers whose textures can't vanish meanwhile. Direct3D 11 and 12 and Vulkan free
	// them all on a device reset, and Direct3D 9 makes them again on a reset (a window's new size):
	// there a picture drawn alongside goes into memory of the host's, sent up when it's done.
	bool lock_across = false;
	if (const char *n = SDL_GetRendererName(ren))
		for (const char *ok : { "metal", "opengl", "opengles2", "software" }) lock_across |= !strcmp(n, ok);
	// The board draws straight into a locked texture only on those same renderers, where a lock
	// hands out memory of the host's own (Metal's shared memory, SDL's buffer for OpenGL, the
	// software renderer's surface). Direct3D 11 makes a new staging texture at each lock (a
	// fresh allocation, its pages faulted in by the drawing threads, in memory the CPU writes
	// through uncached on most cards), Direct3D 12 and Vulkan a new upload buffer: drawing a
	// picture there, smooth motion's 3D or the whole picture on the CPU's way (antialiasing Low
	// reading it back), went at a fraction of its speed, the more so the higher the scale
	// (vf2-recomp #3: an i9-14900KF short of pictures from 2x). There, as vf1-recomp always does,
	// the picture is drawn in the host's memory, then copied into the texture on every core
	// (send_rows: on Direct3D 11 into the texture itself, mapped, d3d11_map.h). The same pixels
	// either way. VF2_DRAW_LOCKED=0 or 1 (tests) chooses.
	bool draw_locked = lock_across;
	if (const char *d = getenv("VF2_DRAW_LOCKED")) draw_locked = atoi(d) != 0;
	std::vector<uint32_t> async_buf;   // (else: the picture drawn alongside, until it's sent up)
	bool async_in_buf = false;
	auto gpu_free = [&] { gpu_pic.free(); gpu.shown = false; };
	// (drawn alongside where textures can vanish, or anywhere the board doesn't draw into a
	// locked texture: the 3D into the host's memory, not left where the board drew it, which its
	// next frame may draw over before it's sent up)
	std::vector<u32> gpu_buf;
	bool gpu_in_buf = false;
	auto gpu_lock = [&](u32 *&px, size_t &stride, bool across) {
		px = nullptr; stride = 0;
		gpu_in_buf = false;
		if ((across && !lock_across) || (!draw_locked && gpu_lock3d)) {
			const size_t W = size_t(m.width()) * m.render_scale();
			if (gpu_buf.size() != W * model2a::HEIGHT * m.render_scale()) gpu_buf.assign(W * model2a::HEIGHT * m.render_scale(), 0);
			px = gpu_buf.data(); stride = W; gpu_in_buf = true;
		} else if (gpu_lock3d) gpu_pic.lock(m.width(), m.render_scale(), px, stride);
	};
	auto gpu_unlock = [&] { gpu_pic.unlock(); gpu_in_buf = false; };
	auto gpu_put = [&](model2a::picture_parts &p) -> bool {
		if (gpu_in_buf) {   // (the 3D drawn into the host's memory: sent up from there)
			p.map3d = gpu_buf.data(); p.map_stride = size_t(p.w) * p.s;
			gpu_in_buf = false;
		}
		if (!gpu_pic.put(p)) return false;   // (no textures that size here: checked before drawing, gpu_pic.ready)
		gpu.shown = true;
		return true;
	};
	// ---- The 3D on the GPU (src/gpu/gpu3d.h; Display > 3D renderer) ----
	// The board hands the GPU its 3D's list (model2a::render_parts' gpu3d hook) in place of drawing
	// it on the CPU; the GPU draws it into one of three textures of the renderer's (a shared surface:
	// nothing copied back), which the 2D's parts are put together around (gpu_picture), as the
	// CPU's 3D is. The same pixels as the CPU's (gpu3d_quality::exact), with smooth motion, the wide
	// pictures' sides and every scale; antialiasing 2x and 3x draw it larger and average it down
	// on the GPU (each block's share drawn its alpha, put over the 2D premultiplied: the CPU's
	// averaging of the whole picture, where the 2D beneath is one colour across a block, as it is
	// but at a moved backdrop's edges), Low smooths its edges there as the CPU's does (edge_smooth's
	// sums, the 2D beneath from the parts). A 4:3 screen in a wide picture (its sides go by the 3D's
	// edges) and render test mode are drawn on the CPU. Metal only for now (Macs).
	//
	// Auto (the choice of the faster here): before the first picture, unseen, both draw a made-up
	// scene the size of a fight's (model2_video::bench_make) at the scale and antialiasing in use,
	// a few times each (the GPU's first ones its warm-up: its shaders and buffers made then, not in
	// play); the GPU is taken if it's clearly quicker (under 85% of the CPU's time; once on it, it
	// stays while it's no slower). Checked again, on the game's own scene, when the scale,
	// antialiasing or shape change and when the governor finds the pictures short, but only at a
	// moment nothing moves on screen (the menu, paused, a scene change held by scene sync), where a
	// switch can't be seen either. What it found is kept per scale and antialiasing (settings.ini's
	// auto_3d), so a later start begins there (and checks it again). Any GPU error: the CPU from
	// then on, for this run.
	//   VF2_3D=cpu|gpu|auto  the renderer, over the setting; VF2_GPU3D_FAST=1 the fast quality
	//   VF2_GPU3D_SLOW=n     tests: the GPU's drawing done n times over (a slow GPU)
	//   VF2_GPU3D_CHECK=n    every n-th GPU picture also drawn on the CPU and compared (the log)
	//   VF2_AUTO_RECHECK=p,..  Auto checked again from these passes on (at the next quiet moment;
	//                        VF2_AUTO_ANYTIME=1: at once, wherever it is: tests of its numbers)
	//   VF2_GPU3D_TEST_DIFF=n  tests: the GPU's picture checked made to differ in n pixels (-1: all)
	enum { R3D_CPU, R3D_GPU, R3D_AUTO };
	// SDL_GPU's check of its pictures against the CPU's: how many differing pixels are rounding
	// (some graphics cards' drivers, NVIDIA's on Direct3D 12, put pixels a hair off), the GPU kept.
	// Judged by how wrong: colours within GPU_ROUNDING_COLOUR anywhere are rounding; beyond that,
	// up to 1% of the picture's pixels, and up to 5% drawn on one and not the other (edges a hair
	// apart). When the player chose the GPU (not Auto): 10% and 25%, only a picture mostly wrong
	// (a GPU drawing nothing, or garbage) going to the CPU.
	constexpr long long GPU_ROUNDING_PX = 64;
	constexpr int GPU_ROUNDING_COLOUR = 8;
	constexpr double GPU_WRONG_COLOUR = 0.01, GPU_WRONG_COVER = 0.05, GPU_WRONG_COLOUR_CHOSEN = 0.10, GPU_WRONG_COVER_CHOSEN = 0.25;
	static const char *R3D_NAMES[] = { "CPU", "GPU", "Auto" };
	constexpr int G3_TARGETS = 5;   // (see gpu3d_state::tex)
	struct gpu3d_state {
		std::unique_ptr<gpu3d> dev;
		int mode = R3D_CPU;         // the player's choice (or VF2_3D's)
		bool on = false;            // drawing on the GPU now
		bool failed = false;        // an error: the CPU for this run
		bool forced_mode = false;
		bool tried = false;         // the GPU's renderer started (dev, or none here)
		bool next_start = false;    // none now, but there will be at the next start (SDL_GPU: the renderer's chosen as the game starts)
		int verify = 0;             // SDL_GPU: its pictures still to be checked against the CPU's (1 the test scene's, 2 the game's own)
		// The GPU's pictures go into G3_TARGETS targets in turn, read by the renderer (SDL's own
		// queue, which Metal doesn't order against ours) when it puts the picture together. The
		// renderer may still have up to three frames on the way; pipelined, a picture is put together
		// a pass after it's drawn: five, so a target is never drawn into again before its frame is done.
		SDL_Texture *tex[G3_TARGETS] = {};
		int w = 0, h = 0, next = 0, pending_t = -1, pending_ss = 1;
		bool pending = false;       // a picture drawn alongside the next frame (pipelined)
		std::vector<gpu3d_poly> list;
		gpu3d_rams rams;
		gpu3d_frame frame;
		// Auto
		struct bench {
			bool active = false, real = false, ready = false;   // (ready: its turns done, the choice to make)
			double last_turn = 0;
			int i = 0, reps = 3, scale = 0, aa = 0, parts = 1, band = 0;
			double cur_c = 0;
			double best_g = 1e30, best_c = 1e30, gpu_only = 1e30, took = 0;
			std::string why;
			gpu3d_frame f;
		} b;                             // a check under way
		double cpu_ms = 0, gpu_ms = 0;   // the last check's (ms a picture)
		double gpu_only_ms = 0;          // (of which the GPU's own time)
		const char *checked_on = "";     // its scene
		bool recheck = false;
		std::string recheck_why;
		uint64_t last_check = 0;
		std::vector<int64_t> recheck_at;
		bool anytime = getenv("VF2_AUTO_ANYTIME") != nullptr;   // (tests: checked at once, not at a quiet moment)
		bool want_real = false;   // the start's check was of the test scene: once more on a fight's
		// in play: the GPU's pictures, its time and the CPU's setting them up
		double sum_gpu = 0, sum_cpu = 0; uint64_t pictures = 0;
		uint64_t unwaited = 0;   // (shown still drawing: the renderer after it on the GPU, the CPU not waiting)
		// VF2_GPU3D_CHECK
		int check_every = 0;
		uint64_t checked = 0, check_px = 0, check_diff = 0, check_bad = 0;
		int check_max = 0;
		struct shown_count { uint64_t checked = 0, px = 0, diff = 0, bad = 0; int most = 0, saved = 0; } shown[2];   // (as put together for the screen: read back at once, later)
		int ref_w = 0, ref_h = 0, late_w = 0, late_h = 0, late_wait = 0;
		bool late_smooth = false;
		SDL_Texture *late = nullptr;
		std::vector<uint32_t> late_ref;
		bool ref_pending = false, ref_smooth = false;
		std::vector<uint32_t> ref;
		std::vector<u32> check_cpu, check_gpu;
	} g3;
	{
		const char *e = getenv("VF2_3D");
		g3.mode = cfg.renderer3d;
		if (e) { g3.forced_mode = true; g3.mode = !strcmp(e, "gpu") ? R3D_GPU : !strcmp(e, "auto") ? R3D_AUTO : R3D_CPU; }
		if (const char *c = getenv("VF2_GPU3D_CHECK")) g3.check_every = std::max(0, atoi(c));
		if (const char *r = getenv("VF2_AUTO_RECHECK"))
			for (const char *q = r; *q; ) { g3.recheck_at.push_back(atoll(q)); while (*q && *q != ',') q++; if (*q) q++; }
	}
	// the GPU's renderer started (its shaders compiled) when first wanted: not at all for the CPU's
	auto g3_start = [&] {
		if (g3.dev || g3.tried) return;
		g3.tried = true;
		const auto t0 = std::chrono::steady_clock::now();
		const char *rn = SDL_GetRendererName(ren);
#ifdef VF2_GPU3D_METAL
		if (rn && !strcmp(rn, "metal"))
			if (void *layer = SDL_GetRenderMetalLayer(ren)) g3.dev = gpu3d_create_metal_layer(layer);
#endif
#ifdef VF2_GPU3D_SDL
		// (on SDL's gpu renderer's device, its pictures checked against the CPU's before they're
		// trusted: the start's test scene, then the game's own; any difference, the CPU's 3D)
		if (rn && !strcmp(rn, "gpu")) {
			if (SDL_GPUDevice *gd = SDL_GetGPURendererDevice(ren)) g3.dev = gpu3d_create_sdlgpu(gd);
			g3.verify = g3.dev ? 2 : 0;
		} else if (!getenv("SDL_RENDER_DRIVER") && !gpu_unavailable) g3.next_start = true;
#endif
		if (g3.dev) {
			g3.dev->set_quality(cfg.gpu_quality || getenv("VF2_GPU3D_FAST") ? gpu3d_quality::fast : gpu3d_quality::exact);
			if (const char *sl = getenv("VF2_GPU3D_SLOW")) g3.dev->test_repeat = std::max(1, atoi(sl));
		}
		fprintf(stderr, "the 3D on the GPU: %s (%.0f ms to start)\n", g3.dev ? g3.dev->name() : g3.next_start ? "from the next start (the CPU's 3D)" : "no GPU renderer for it here (the CPU's 3D)",
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
#if defined(VF2_GPU3D_METAL) || defined(VF2_GPU3D_SDL)
		if (!g3.dev && !g3.next_start && g3.mode != R3D_CPU) on_cpu_because(tr::CPU_WHY_NO_RENDERER, UPDATE);
#endif
	};
	fprintf(stderr, "the 3D: %s%s\n", R3D_NAMES[g3.mode], g3.forced_mode ? " (VF2_3D)" : "");
	if (g3.mode != R3D_CPU) g3_start();
	auto g3_free = [&] {
		if (g3.dev) g3.dev->finish();
		for (SDL_Texture *&t : g3.tex) if (t) { SDL_DestroyTexture(t); t = nullptr; }
		g3.w = g3.h = 0;
		g3.pending = false;
	};
	// (keep: the driver it failed on saved, the next start tries SDL's other. Only for its start's check
	// and its textures: a picture failing in play (a suspend and resume, a reset) is the CPU for this
	// run only; saved, one bad moment on a Deck, Vulkan its only driver, put every later start on the CPU)
	// The 3D gone to the CPU in play: the scale the player chose for the GPU, 3x with 2x antialiasing, was 0.5 pictures
	// a second there (vf3-recomp #26's device lost). 1x without antialiasing for this run (cpu_cut, at the pass's end,
	// said with the notice); the settings saved stay the player's (a graphics setting changed puts them back). Drawing
	// only: the game's the same
	bool cpu_cut = false;
	auto g3_fail = [&](const char *why, bool keep = true) {
		fprintf(stderr, "the 3D on the GPU: %s: on the CPU for this run%s\n", why, keep ? " (the driver noted for Windows' order)" : "");
		cpu_cut = true;
		// (its start's check or its textures: kept for this build, the menu tries again; a picture failing in play, a
		// sleep or a GPU reset: for this run only)
		bool other_next = false;   // (Windows: the other driver not tried yet, so the next start tries it by itself)
		auto say = [&] {
			if (g3.mode == R3D_CPU) return;
			if (keep && strstr(why, "no targets that size")) on_cpu_because(tr::CPU_WHY_TOO_LARGE, tr::CPU_TODO_LOWER_SCALE);
			else if (keep) on_cpu_because(strstr(why, "aren't the CPU's") ? tr::CPU_WHY_WRONG : tr::CPU_WHY_FAILED,
			                              other_next ? tr::CPU_TODO_OTHER_DRIVER : UPDATE);
			else on_cpu_because(tr::CPU_WHY_STOPPED, tr::CPU_TODO_NEXT_START);
		};
		g3.failed = true; g3.on = false;
		g3_free();
#ifdef VF2_GPU3D_SDL
		// the driver it failed on, kept: the next start tries SDL's other (Direct3D 12, then Vulkan)
		if (!keep || strstr(why, "no targets that size")) { say(); return; }   // (too large for any driver: none blamed)
		if (SDL_GPUDevice *d = static_cast<SDL_GPUDevice *>(SDL_GetPointerProperty(SDL_GetRendererProperties(ren), SDL_PROP_RENDERER_GPU_DEVICE_POINTER, nullptr)))
			if (const char *drv = SDL_GetGPUDeviceDriver(d); drv && (" " + cfg.gpu3d_failed + " ").find(std::string(" ") + drv + " ") == std::string::npos) {
				cfg.gpu3d_failed += (cfg.gpu3d_failed.empty() ? "" : " ") + std::string(drv);
				cfg_dirty = true;
				if (!strcmp(drv, "direct3d12") && cfg.gpu3d_failed.find("vulkan") == std::string::npos)
					fprintf(stderr, "the 3D on the GPU: the next start tries Vulkan\n");
#ifdef _WIN32
				const char *other = !strcmp(drv, "vulkan") ? "direct3d12" : !strcmp(drv, "direct3d12") ? "vulkan" : nullptr;
				other_next = other && (" " + cfg.gpu3d_failed + " ").find(std::string(" ") + other + " ") == std::string::npos && !getenv("SDL_GPU_DRIVER");
#endif
			}
#endif
		say();
	};
	// the targets, the picture's size (false: none here, the CPU's 3D)
	auto g3_targets = [&](int w, int h) -> bool {
		if (g3.w == w && g3.h == h && g3.tex[0]) return true;
		g3_free();
		if (!g3.dev->make_targets(w, h, G3_TARGETS)) { g3_fail("no targets that size"); return false; }
		for (int i = 0; i < G3_TARGETS; i++) {
			SDL_PropertiesID pr = SDL_CreateProperties();
			SDL_SetNumberProperty(pr, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, g3.dev->targets_rgba() ? SDL_PIXELFORMAT_ABGR8888 : SDL_PIXELFORMAT_ARGB8888);
			SDL_SetNumberProperty(pr, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
			SDL_SetNumberProperty(pr, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
			SDL_SetNumberProperty(pr, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
#ifdef VF2_GPU3D_SDL
			SDL_SetPointerProperty(pr, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, g3.dev->target_handle(i));
#else
			SDL_SetPointerProperty(pr, SDL_PROP_TEXTURE_CREATE_METAL_PIXELBUFFER_POINTER, g3.dev->target_handle(i));
#endif
			g3.tex[i] = SDL_CreateTextureWithProperties(ren, pr);
			SDL_DestroyProperties(pr);
			if (!g3.tex[i]) { fprintf(stderr, "the 3D on the GPU: %s\n", SDL_GetError()); g3_fail("the renderer won't show its targets"); return false; }
			SDL_SetTextureBlendMode(g3.tex[i], SDL_BLENDMODE_BLEND_PREMULTIPLIED);
			SDL_SetTextureScaleMode(g3.tex[i], SDL_SCALEMODE_NEAREST);
		}
		g3.w = w; g3.h = h;
		return true;
	};
	// a picture's list handed to the GPU (render_parts' hook: the RAMs as its frame left them)
	auto g3_submit = [&](model2_video &v, int pic, int target) -> bool {
		v.export_picture(pic, g3.list);
		v.gpu_rams(g3.rams);
		g3.dev->set_rams(g3.rams);
		const model2a::picture_parts &p = gpu.parts;
		gpu3d_frame &f = g3.frame;
		f.w = m.width() * scale; f.h = model2a::HEIGHT * scale;
		f.scale = scale; f.ss = aa_now();
		f.edge = aa == 0 && f.ss == 1;
		f.backdrop = p.backdrop; f.mid = p.mid; f.w2d = p.w; f.pad = model2a::picture_parts::PAD;
		f.bdx = p.bdx; f.bdy = p.bdy; f.filter = p.filter; f.version2d = p.version;
		if (!g3.dev->submit(g3.list.data(), g3.list.size(), f, target)) { g3_fail("a picture couldn't be drawn", false); return false; }
		return true;
	};
	// the GPU's picture finished (its timings counted)
	auto g3_finish = [&]() -> bool {
		if (!g3.dev || !g3.dev->busy()) return !g3.failed;
		double gms = 0, cms = 0;
		if (!g3.dev->finish(&gms, &cms)) { g3_fail("a picture failed on the GPU", false); return false; }
		g3.sum_gpu += gms; g3.sum_cpu += cms; g3.pictures++;
		return true;
	};
	// the GPU's picture about to be shown: not waited for where the renderer comes after it on the same
	// queue (see gpu3d::settle); its timings counted when it had finished, else at the next submit's
	auto g3_settle = [&]() -> bool {
		if (!g3.dev || !g3.dev->busy()) return !g3.failed;
		double gms = 0, cms = 0;
		if (!g3.dev->settle(&gms, &cms)) { g3_fail("a picture failed on the GPU", false); return false; }
		if (gms >= 0) { g3.sum_gpu += gms; g3.sum_cpu += cms; g3.pictures++; } else g3.unwaited++;
		return true;
	};
	// Auto's check: the CPU's and the GPU's time for a picture (ms: the GPU's from handing it over
	// to finished, the quickest after two to warm up; the CPU's on every core, the quickest), of the
	// game's own scene (the picture shown) if it has a fight's polygons, else the made-up one. Its
	// pictures go nowhere the game shows. In turns (g3_bench_step: one picture each way, a few ms),
	// so that a check in play is spread over the quiet passes; a moment of other work on the machine
	// falls on both alike.
	auto g3_bench_begin = [&](const char *why, bool real, int parts) {
		model2_video &v = m.video();   // (waits for a picture drawn alongside)
		int pic = real ? v.shown_index() : -1;
		if (pic >= 0 && v.polygons_shown() < 400) pic = -1;
		if (pic >= 0) v.bench_take(pic); else v.bench_make(1500);
		g3.checked_on = pic >= 0 ? "the game's scene" : "a test scene";
		v.bench_export(g3.list);
		v.gpu_rams(g3.rams);
		g3.dev->set_rams(g3.rams);
		auto &b = g3.b;
		b = {};
		b.active = true; b.why = why; b.real = real; b.scale = scale; b.aa = aa; b.parts = std::clamp(parts, 1, 32);
		b.f.w = m.width() * scale; b.f.h = model2a::HEIGHT * scale; b.f.scale = scale; b.f.ss = aa_now();
	};
	// (a turn: the GPU's picture, then the CPU's a band of rows at a time, b.parts of them, their
	// times added up: each step a millisecond or so, for a pass with time to spare)
	auto g3_bench_step = [&]() -> bool {   // true: done (or given up: b.active false)
		auto &b = g3.b;
		if (b.scale != scale || b.aa != aa || b.f.w != m.width() * scale) { const std::string why = b.why; g3_bench_begin(why.c_str(), b.real, b.parts); }   // (a new size meanwhile: again)
		model2_video &v = m.video();
		using clk = std::chrono::steady_clock;
		const auto t0 = clk::now();
		if (b.band == 0) {
			double gms = 0;
			if (!g3.dev->submit(g3.list.data(), g3.list.size(), b.f, -1) || !g3.dev->finish(&gms)) { b.active = false; g3_fail("a test picture failed"); return true; }
			if (b.i >= 2) b.best_g = std::min(b.best_g, std::chrono::duration<double, std::milli>(clk::now() - t0).count()), b.gpu_only = std::min(b.gpu_only, gms);
			b.cur_c = 0;
			if (b.i == 0) { b.i++; b.took += std::chrono::duration<double, std::milli>(clk::now() - t0).count(); return false; }   // (the first: the GPU's warm-up alone)
			b.band = 1;
			if (b.parts > 1) { b.took += std::chrono::duration<double, std::milli>(clk::now() - t0).count(); return false; }
		}
		b.cur_c += v.bench_cpu(b.band - 1, b.parts);
		b.took += std::chrono::duration<double, std::milli>(clk::now() - t0).count();
		if (b.band++ < b.parts) return false;
		b.band = 0;
		b.best_c = std::min(b.best_c, b.cur_c);
		return ++b.i >= b.reps + 2;
	};
	// Auto's choice kept per scale and antialiasing (settings.ini's auto_3d: "3x1:g 2x0:c")
	auto auto_key = [&] { return std::to_string(scale) + "x" + std::to_string(aa) + ":"; };
	auto auto_kept = [&]() -> int {   // 1 the GPU, 0 the CPU, -1 none kept
		const std::string k = auto_key();
		for (size_t at = 0; (at = cfg.auto3d.find(k, at)) != std::string::npos; at++)
			if (at == 0 || cfg.auto3d[at - 1] == ' ') return at + k.size() < cfg.auto3d.size() && cfg.auto3d[at + k.size()] == 'g';
		return -1;
	};
	auto auto_keep = [&](bool gpu_wins) {
		const std::string k = auto_key();
		std::string out;
		for (size_t a = 0; a < cfg.auto3d.size();) {
			size_t b = cfg.auto3d.find(' ', a);
			if (b == std::string::npos) b = cfg.auto3d.size();
			const std::string tok = cfg.auto3d.substr(a, b - a);
			if (!tok.empty() && tok.compare(0, k.size(), k)) out += (out.empty() ? "" : " ") + tok;
			a = b + 1;
		}
		out += (out.empty() ? "" : " ") + k + (gpu_wins ? "g" : "c");
		if (out != cfg.auto3d) { cfg.auto3d = out; cfg_dirty = true; }
	};
	// The renderer for the scale and antialiasing now: the player's choice, or Auto's check (the
	// GPU's warm-up too, whichever is chosen): its numbers in, the choice made
	// SDL_GPU: the check's scene drawn once more each way, at once, and compared (render scale,
	// before the 2D), pixel for pixel; a difference: the CPU's 3D from now on. Once on the start's
	// test scene, once on the game's own (its textures).
	auto g3_verify = [&]() -> bool {
		auto &b = g3.b;
		model2_video &v = m.video();
		int w = 0, h = 0;
		v.render_size(w, h);
		std::vector<uint32_t> gp(size_t(w) * h);
		long long cover = 0;   // (of the differing pixels, drawn on one and not the other: an edge's)
		int most = 0;          // (the largest difference in a colour channel, of those drawn on both)
		long long big = 0;     // (of those drawn on both, the ones a colour channel is off by more than rounding)
		auto differ = [&]() -> long long {
			if (!g3.dev->read_picture(gp.data(), size_t(w))) return -1;
			const bitmap_rgb32 &c = v.bench_picture();
			if (c.width() != w || c.height() != h) return -1;
			// tests: VF2_GPU3D_TEST_DIFF=n, the GPU's picture made to differ in n pixels (-1: all of it garbage)
			if (const char *t = getenv("VF2_GPU3D_TEST_DIFF")) {
				const long long k = atoll(t);
				for (size_t i = 0; i < gp.size() && (k < 0 || (long long)i < k); i++) gp[(i * 7919) % gp.size()] ^= 0x00102030u;
			}
			long long n = 0;
			for (int y = 0; y < h; y++) {
				const u32 *r = &c.pix(y);
				const uint32_t *g = gp.data() + size_t(y) * w;
				for (int x = 0; x < w; x++) {
					if (!((r[x] == 0) != (g[x] == 0) || ((r[x] ^ g[x]) & 0xffffff))) continue;
					n++;
					if ((r[x] == 0) != (g[x] == 0)) { cover++; continue; }
					int off = 0;
					for (int sh = 0; sh < 24; sh += 8) off = std::max(off, std::abs(int((r[x] >> sh) & 255) - int((g[x] >> sh) & 255)));
					most = std::max(most, off);
					if (off > GPU_ROUNDING_COLOUR) big++;
				}
			}
			return n;
		};
		// (both drawn again now, from the check's list: the game's own pictures go through g3.list too,
		// and its RAMs may have moved since the check's turns)
		v.bench_export(g3.list);
		v.gpu_rams(g3.rams);
		g3.dev->set_rams(g3.rams);
		if (!g3.dev->submit(g3.list.data(), g3.list.size(), b.f, -1) || !g3.dev->finish()) { g3_fail("a test picture failed"); return false; }
		v.bench_cpu(0, 1);
		const long long d = differ();
		const bool game = !strcmp(g3.checked_on, "the game's scene");
		if (d < 0) { g3_fail("its picture couldn't be read back"); return false; }
		if (d != 0) {
			// A few pixels apart is rounding (a graphics card's driver doing a sum or an edge a hair
			// differently), not a broken GPU: the GPU's kept. Beyond that the CPU's 3D from now on;
			// when the player chose the GPU (not Auto), only for a picture that's mostly wrong.
			// Judged by how wrong, not how many: colours off by a little, or edges a hair apart,
			// anywhere, are a driver's rounding; only a picture plainly wrong (much of it drawn on one
			// and not the other, as a GPU drawing nothing; or colours well off) goes to the CPU.
			const long long px = (long long)w * h;
			const bool chosen = g3.mode == R3D_GPU;
			const long long ok = std::max(GPU_ROUNDING_PX, (long long)(px * (chosen ? GPU_WRONG_COLOUR_CHOSEN : GPU_WRONG_COLOUR)));
			const long long ok_cover = std::max(GPU_ROUNDING_PX, (long long)(px * (chosen ? GPU_WRONG_COVER_CHOSEN : GPU_WRONG_COVER)));
			if (big <= ok && cover <= ok_cover) {
				fprintf(stderr, "the 3D on the GPU: its picture of %s: %lld of %d x %d pixels differ by rounding (%lld at an edge, %lld by more than %d, colours by %d at most; up to %lld and %lld kept): keeping the GPU\n",
						g3.checked_on, d, w, h, cover, big, GPU_ROUNDING_COLOUR, most, ok_cover, ok);
				if (game) g3.verify = 0;
				return true;
			}
			fprintf(stderr, "the 3D on the GPU: its picture of %s isn't the CPU's (%lld of %d x %d pixels differ, %lld drawn on one only, %lld by more than %d, colours by %d at most; over the %lld and %lld kept)\n",
					g3.checked_on, d, w, h, cover, big, GPU_ROUNDING_COLOUR, most, ok_cover, ok);
			g3_fail("its pictures aren't the CPU's");
			return false;
		}
		fprintf(stderr, "the 3D on the GPU: its picture of %s is the CPU's, pixel for pixel (%d x %d)\n", g3.checked_on, w, h);
		if (game) g3.verify = 0;
		return true;
	};
	auto g3_bench_done = [&] {
		auto &b = g3.b;
		if (g3.verify && !g3_verify()) { b.active = false; g3.recheck = false; return; }
		b.active = false;
		g3.recheck = false;
		g3.cpu_ms = b.best_c; g3.gpu_ms = b.best_g; g3.gpu_only_ms = b.gpu_only;
		const bool was = g3.on;
		bool on = true;
		if (g3.mode == R3D_AUTO) {
			// the GPU unless it's clearly slower in a way that matters: over 1.5x the CPU's time and
			// over 14 ms (0.8 of the frame's 17.4: the GPU draws alongside the next frame, pipelined,
			// so only near a whole frame is the frame at risk); back to it within 1.3x or under 12 ms.
			// (It was 8 ms, half the frame: a Radeon 780M at 6x (vf2-recomp #10) takes ~9.5 ms on any
			// scene, its pixels' passes a floor, and a light scene checked after the menu put the 3D
			// on the CPU (4.8 ms there), which then took ~28 ms on a fight's and dropped to 30 fps.)
			// (now, as VF1's since 18 Oct: the GPU whenever it works, its check of the pictures passed; the
			// CPU after a GPU error. VF2_AUTO_MEASURE=1: tests, the old choice by the two times)
			const double c = g3.cpu_ms, g = g3.gpu_ms;
			static const bool measure = getenv("VF2_AUTO_MEASURE") != nullptr;
			// (a GPU drawn on the CPU, llvmpipe in a container or a VM, measured as before: it's several times slower)
			on = (!measure && !soft_render) || (was || !g3.last_check ? !(g > 1.5 * c && g > 14.0) : (g <= 1.3 * c || g < 12.0));
			if (!on && soft_render) on_cpu_because(tr::CPU_WHY_NO_DRIVER, tr::CPU_TODO_INSTALL);
			auto_keep(on);
		}
		g3.on = on;
		g3.last_check = SDL_GetTicksNS();
		if (g3.verify) { g3.recheck = g3.want_real = true; g3.recheck_why = "the game's own scene"; }   // (SDL_GPU: not yet checked on it)
		fprintf(stderr, "the 3D (%s, %s): a picture %.2f ms on the CPU (%d threads), %.2f on the GPU (%.2f its own; %s, %s, %dx, antialiasing %s): on the %s%s; the check took %.0f ms in all\n",
				R3D_NAMES[g3.mode], b.why.c_str(), g3.cpu_ms, row_pool::get().threads(), g3.gpu_ms, g3.gpu_only_ms, g3.dev->quality() == gpu3d_quality::exact ? "exact" : "fast", g3.checked_on, scale,
				aa == 0 ? "Low" : aa_now() > 1 ? (aa_now() == 2 ? "2x" : "3x") : "off", on ? "GPU" : "CPU", on == was ? "" : " (switched)", b.took);
	};
	// ... all at once (the start, the menu: nothing moves on screen)
	auto g3_decide = [&](const char *why, bool real) {
		g3.recheck = false;
		g3.b.active = false;
		if (!g3.dev || g3.failed || g3.mode == R3D_CPU) {
			if (g3.on || !g3.last_check) fprintf(stderr, "the 3D (%s, %s): on the CPU (%s)\n", R3D_NAMES[g3.mode], why,
				g3.mode == R3D_CPU ? "the player's setting" : g3.failed ? "the GPU failed" : "no GPU 3D here");
			g3.on = false; g3.last_check = SDL_GetTicksNS();
			return;
		}
		g3_bench_begin(why, real, 1);
		while (!g3_bench_step()) {}
		if (g3.b.active) g3_bench_done();
	};
	std::function<void()> g3_check_shown;   // (VF2_GPU3D_CHECK: below)
	auto finish_async = [&](bool show) -> bool {   // the picture being drawn: finished, and up next (or dropped)
		if (!async_pending) return false;
		m.render_wait();
		async_pending = false;
		if (g3.pending) {   // (the 3D on the GPU: finished, put together now)
			g3.pending = gpu.pending = false;
			const bool ok = show ? g3_settle() : g3_finish();
			if (!show || !ok) { gpu_unlock(); return false; }
			if (!gpu_pic.put(gpu.parts, gpu.parts.has3d ? g3.tex[g3.pending_t] : nullptr, g3.pending_ss)) return false;
			g3_check_shown();
			gpu.shown = true;
			return true;
		}
		if (gpu.pending) {   // (the 2D on the GPU: the 3D drawn, put together now)
			gpu.pending = false;
			if (!show) { gpu_unlock(); return false; }
			return gpu_put(gpu.parts);   // (from the host's memory, if it was drawn there)
		}
		if (async_in_buf) { async_in_buf = false; if (show) send_rows(tex_next, nullptr, async_buf.data(), size_t(m.width()) * m.render_scale()); }
		else SDL_UnlockTexture(tex_next);
		if (!show) return false;
		std::swap(tex, tex_next);
		gpu.shown = false;
		return true;
	};
	std::string pass_note, gov_note;   // (VF2_PICTURE_LOG's)
	struct knockout { int player = 0; uint64_t from = 0, to = UINT64_MAX; } test_ko;   // VF2_KO
	if (const char *k = getenv("VF2_KO")) {
		char *e;
		test_ko.player = int(strtol(k, &e, 10));
		if (*e == '@') { test_ko.from = strtoull(e + 1, &e, 10); if (*e == '-') test_ko.to = strtoull(e + 1, &e, 10); }
	}
	auto make_texture = [&]() -> bool {
		pass_note += "texture,";
		finish_async(false);   // (a new size: what's on show is drawn again)
		gpu_free();
		g3_free();
		if (tex_next) SDL_DestroyTexture(tex_next);
		tex_next = nullptr;
		if (tex) SDL_DestroyTexture(tex);
		for (;;) {
			const int w = m.width() * scale, h = model2a::HEIGHT * scale;
			tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
			if (tex || scale == 1) break;
			fprintf(stderr, "a %d x %d picture is too large here (%s): trying %dx\n", w, h, SDL_GetError(), scale - 1);
			scale--;
			m.set_render_scale(scale * aa_fit(scale, aa));
		}
		if (!tex) return false;
		SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
		SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
		if (aspect_i) snprintf(title_base, sizeof title_base, "Virtua Fighter 2  %dx  %s", scale, ASPECT_NAMES[aspect_i]);
		else snprintf(title_base, sizeof title_base, "Virtua Fighter 2  %dx  fit (%.2f:1)", scale, m.width() / 372.0);
		SDL_SetWindowTitle(win, title_base);
		return true;
	};
	if (!make_texture()) {   // (the GPU's 3D let go first, as at the end: before the renderer whose device it may share)
		const tr::line why(tr::FAIL_PICTURE, { { "why", SDL_GetError() } });
		g3_free();
		g3.dev.reset();
		return fail(why);
	}
	// Antialiasing: each aa x aa block of the picture drawn averaged into one pixel of what's shown.
	// Red and blue added together in one word (0x00ff00ff lanes), green in another.
	auto aa_into = [&](const bitmap_rgb32 &src_bm, uint32_t *dst, size_t stride) {
		const int k = aa_now(), w = src_bm.width() / k, h = src_bm.height() / k, sw = src_bm.width();
		const uint32_t *src = src_bm.data();
		parallel_rows(h, [&](int y0, int y1) {
			for (int y = y0; y < y1; y++) {
				uint32_t *d = dst + size_t(y) * stride;
				const uint32_t *r0 = src + size_t(y * k) * sw;
				if (k == 2) {
					const uint32_t *r1 = r0 + sw;
					for (int x = 0; x < w; x++) {
						const uint32_t a0 = r0[2 * x], a1 = r0[2 * x + 1], b0 = r1[2 * x], b1 = r1[2 * x + 1];
						const uint32_t rb = (a0 & 0xff00ff) + (a1 & 0xff00ff) + (b0 & 0xff00ff) + (b1 & 0xff00ff);
						const uint32_t g = (a0 & 0xff00) + (a1 & 0xff00) + (b0 & 0xff00) + (b1 & 0xff00);
						d[x] = 0xff000000 | ((rb >> 2) & 0xff00ff) | ((g >> 2) & 0xff00);
					}
				} else {
					for (int x = 0; x < w; x++) {
						uint32_t r = 0, g = 0, b = 0;
						for (int j = 0; j < k; j++) {
							const uint32_t *p = r0 + size_t(j) * sw + x * k;
							for (int i = 0; i < k; i++) { r += (p[i] >> 16) & 0xff; g += (p[i] >> 8) & 0xff; b += p[i] & 0xff; }
						}
						const uint32_t n = k * k;
						d[x] = 0xff000000 | (r / n) << 16 | (g / n) << 8 | (b / n);
					}
				}
			}
		});
	};
	// a picture (at the render scale) into the texture
	// VF2_GPU3D_CHECK's second look: the picture as put together for the screen (the GPU's 3D, the
	// 2D around it) against the one the CPU's way shows for the same moment (render / render_smooth,
	// antialiased as upload_picture does): what the player sees, not only the 3D
	// (the CPU's picture for the moment, taken when the GPU's is handed over: pipelined, it's put
	// together a pass later, the game a frame on by then)
	auto g3_check_ref = [&](bool smooth_now, float alpha) {
		const bitmap_rgb32 &b = smooth_now ? m.render_smooth(alpha) : m.render(true);
		const int ss = aa_now();
		g3.ref_w = b.width() / ss; g3.ref_h = b.height() / ss;
		g3.ref.resize(size_t(g3.ref_w) * g3.ref_h);
		if (ss == 1) for (int y = 0; y < g3.ref_h; y++) memcpy(&g3.ref[size_t(y) * g3.ref_w], &b.pix(y), size_t(g3.ref_w) * 4);
		else aa_into(b, g3.ref.data(), size_t(g3.ref_w));
		g3.ref_smooth = smooth_now;
		g3.ref_pending = true;
	};
	// (a picture read back and set against the CPU's: tag "as shown" or "as shown, read later")
	auto g3_compare = [&](SDL_Texture *t, const std::vector<uint32_t> &want, int w, int h, bool smooth, bool late) {
		SDL_SetRenderTarget(ren, t);
		SDL_Surface *shot = SDL_RenderReadPixels(ren, nullptr);
		SDL_SetRenderTarget(ren, nullptr);
		if (!shot) return;
		SDL_Surface *x = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_XRGB8888);
		SDL_DestroySurface(shot);
		if (!x) return;
		uint64_t diff = 0;
		int most = 0;
		auto &k = g3.shown[late];
		if (x->w == w && x->h == h)
			for (int y = 0; y < h; y++) {
				const uint32_t *g = (const uint32_t *)((const uint8_t *)x->pixels + size_t(y) * x->pitch);
				for (int i = 0; i < w; i++) {
					const uint32_t a = g[i], c = want[size_t(y) * w + i];
					const int d = std::max({ std::abs(int(a >> 16 & 255) - int(c >> 16 & 255)), std::abs(int(a >> 8 & 255) - int(c >> 8 & 255)), std::abs(int(a & 255) - int(c & 255)) });
					if (d > 2) {   // (the 2D averaged on the GPU: within a level or two)
						if (k.bad < 3 && diff == 0)
							fprintf(stderr, "gpu3d check: picture %llu as shown%s: at %d,%d the CPU's %06x, the GPU's %06x (scale %d, antialiasing %d, smooth %d, pipelined %d)\n", (unsigned long long)g3.pictures, late ? ", read later" : "", i, y, c & 0xffffff, a & 0xffffff, scale, aa_now(), int(smooth), int(pipelined));
						diff++;
						most = std::max(most, d);
					}
				}
			}
		else diff = uint64_t(w) * h;
		if (diff > 50 && k.saved < 4 && getenv("VF2_GPU3D_CHECK_DIR")) {
			k.saved++;
			const std::string base = std::string(getenv("VF2_GPU3D_CHECK_DIR")) + (late ? "/late" : "/shown") + std::to_string(g3.pictures);
			write_png((base + "_cpu.png").c_str(), want.data(), w, h);
			std::vector<uint32_t> got(size_t(w) * h);
			for (int y = 0; y < h && x->w == w; y++) memcpy(&got[size_t(y) * w], (const uint8_t *)x->pixels + size_t(y) * x->pitch, size_t(w) * 4);
			write_png((base + "_gpu.png").c_str(), got.data(), w, h);
		}
		SDL_DestroySurface(x);
		k.checked++; k.px += uint64_t(w) * h; k.diff += diff; k.bad += diff > 50; k.most = std::max(k.most, most);
	};
	// Reading the picture back at once makes the renderer finish it there and then; every other
	// one is copied (by the renderer, in its turn) and read back some passes later instead, as the
	// screen got it: a target of the GPU's drawn again before the renderer had used it shows there
	g3_check_shown = [&] {
		if (!g3.ref_pending) return;
		g3.ref_pending = false;
		if ((g3.shown[0].checked + g3.shown[1].checked + (g3.late_wait > 0)) % 2 == 0) { g3_compare(gpu_pic.target(), g3.ref, g3.ref_w, g3.ref_h, g3.ref_smooth, false); return; }
		if (g3.late_wait > 0) return;
		float tw = 0, th = 0;
		SDL_GetTextureSize(gpu_pic.target(), &tw, &th);
		if (g3.late && (g3.late->w != int(tw) || g3.late->h != int(th))) { SDL_DestroyTexture(g3.late); g3.late = nullptr; }
		if (!g3.late) g3.late = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, int(tw), int(th));
		if (!g3.late) return;
		SDL_BlendMode bm = SDL_BLENDMODE_NONE;
		SDL_GetTextureBlendMode(gpu_pic.target(), &bm);
		SDL_SetTextureBlendMode(gpu_pic.target(), SDL_BLENDMODE_NONE);
		SDL_SetRenderTarget(ren, g3.late);
		SDL_RenderTexture(ren, gpu_pic.target(), nullptr, nullptr);
		SDL_SetRenderTarget(ren, nullptr);
		SDL_SetTextureBlendMode(gpu_pic.target(), bm);
		g3.late_ref = g3.ref; g3.late_w = g3.ref_w; g3.late_h = g3.ref_h; g3.late_smooth = g3.ref_smooth;
		g3.late_wait = 6;
	};
	auto upload_picture = [&](const bitmap_rgb32 &b) {
		void *px;
		int pitch;
		if (aa_now() > 1 && SDL_LockTexture(tex, nullptr, &px, &pitch)) {
			aa_into(b, static_cast<uint32_t *>(px), size_t(pitch) / 4);
			SDL_UnlockTexture(tex);
		} else if (aa_now() == 1) send_rows(tex, nullptr, b.data(), size_t(b.width()));
		gpu.shown = false;
	};
	// the picture as shown (antialiased), for a screenshot
	std::shared_ptr<const model2a::picture_desc> shown_pic, pending_pic;   // (see redraw_shown)
	auto shown_pixels = [&](std::vector<uint32_t> &out, int &w, int &h) {
		const bitmap_rgb32 &b = shown_pic ? m.redraw(*shown_pic) : m.render(true);
		w = b.width() / aa_now(); h = b.height() / aa_now();
		out.resize(size_t(w) * h);
		if (aa_now() == 1) memcpy(out.data(), b.data(), out.size() * 4);
		else aa_into(b, out.data(), size_t(w));
	};
	// Screen filters: a picture of the monitor's glass multiplied over the game's, at the window's own
	// pixels (made again when the picture's size changes). Scanlines darkens between the arcade's 384
	// lines; CRT adds an aperture grille (red, green and blue stripes) and slightly darker corners.
	SDL_Texture *glass = nullptr;
	int glass_w = 0, glass_h = 0, glass_kind = 0;
	// CRT colours, brightness and gamma (crt_colours.h): the game's picture drawn through them as it goes to the window
	crt::Colours crt_paint;
	bool crt_on = false;   // (any of them set: the menu says when this renderer can't)
	auto draw_filter = [&] {
		SDL_FRect pic;
		if (!cfg.filter || !SDL_GetRenderLogicalPresentationRect(ren, &pic)) return;
		// on whole pixels, one of the glass's to each (as VF3's, vf3-recomp #7): the picture's edges can fall between
		// them (a borderless fullscreen's 1920 x 1081, 4:3: 1441.33 wide from 239.33), and the glass stretched by a
		// fraction, sampled to the nearest, drew one column twice in the middle (a line down the screen)
		const float x0 = std::round(pic.x), y0 = std::round(pic.y);
		const SDL_FRect r{ x0, y0, std::round(pic.x + pic.w) - x0, std::round(pic.y + pic.h) - y0 };
		const int w = int(r.w), h = int(r.h);
		if (w < 1 || h < 1) return;
		if (!glass || w != glass_w || h != glass_h || cfg.filter != glass_kind) {
			if (glass) SDL_DestroyTexture(glass);
			glass = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
			if (!glass) return;
			glass_w = w; glass_h = h; glass_kind = cfg.filter;
			const bool crt = cfg.filter == 2;
			// the lines want a couple of pixels each to show: they fade in from 1.5 to 3 per line
			const float per_line = h / 384.0f;
			const float depth = (crt ? 0.45f : 0.6f) * std::clamp((per_line - 1.5f) / 1.5f, 0.0f, 1.0f);
			// The lines are a whole number of screen pixels apart: the picture's lines to the nearest,
			// so every line looks the same. Sampled at a fraction of a line apart (the Steam Deck's
			// 800-pixel screen: 2.08 pixels a line), the pattern beat into uneven bands every dozen
			// lines. At a whole number of pixels a line this is the picture's own lines, as before;
			// two pixels a line: a bright row and a dark one.
			const int period = std::max(1, int(std::lround(per_line)));
			std::vector<float> row(h);
			for (int y = 0; y < h; y++) {
				const float f = period == 2 ? float(y % 2) * 0.5f : (float(y % period) + 0.5f) / float(period);
				row[y] = 1 - depth * (1 - std::sin(f * 3.14159265f));   // 1 mid-line, darker at its edges
			}
			auto glass_at = [&](int x, int y, float rgb[3]) {
				rgb[0] = rgb[1] = rgb[2] = row[y];
				if (!crt) return;
				for (int c = 0; c < 3; c++) if (c != x % 3) rgb[c] *= 0.78f;   // the grille: one colour per pixel column
				const float dx = (x + 0.5f) / w * 2 - 1, dy = (y + 0.5f) / h * 2 - 1;
				const float d = std::min(1.0f, (dx * dx + dy * dy) / 2);
				for (int c = 0; c < 3; c++) rgb[c] *= 1 - 0.22f * d * d;      // the corners
			};
			// Multiplying only darkens: drawn as twice the product instead (dst * glass * 2), the glass
			// also brightens the middle of each line, so the picture keeps about its brightness.
			const SDL_BlendMode times2 = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_SRC_COLOR,
				SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
			const bool doubled = SDL_SetTextureBlendMode(glass, times2);
			if (!doubled) SDL_SetTextureBlendMode(glass, SDL_BLENDMODE_MOD);   // a renderer without it: darker
			float gain = 1;
			if (doubled) {
				double sum = 0; int n = 0;
				for (int y = 0; y < h; y += 3)
					for (int x = 0; x < w; x += 7) { float v[3]; glass_at(x, y, v); sum += v[0] + v[1] + v[2]; n += 3; }
				gain = std::min(1.4f, float(n / sum));
			}
			const float k = 255 * gain / (doubled ? 2 : 1);
			std::vector<uint32_t> px(size_t(w) * h);
			parallel_rows(h, [&](int y0, int y1) {   // on every core: a 4K window's is ~20 ms on one
				for (int y = y0; y < y1; y++)
					for (int x = 0; x < w; x++) {
						float v[3];
						glass_at(x, y, v);
						uint32_t c[3];
						for (int i = 0; i < 3; i++) c[i] = uint32_t(std::min(255.0f, v[i] * k + 0.5f));
						px[size_t(y) * w + x] = 0xff000000u | c[0] << 16 | c[1] << 8 | c[2];
					}
			}, 4);
			SDL_UpdateTexture(glass, nullptr, px.data(), w * 4);
			SDL_SetTextureScaleMode(glass, SDL_SCALEMODE_NEAREST);
		}
		// over the picture, under the menu: in the window's own pixels (the logical presentation off for it)
		int lw = 0, lh = 0;
		SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
		SDL_GetRenderLogicalPresentation(ren, &lw, &lh, &mode);
		SDL_SetRenderLogicalPresentation(ren, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
		SDL_RenderTexture(ren, glass, nullptr, &r);
		SDL_SetRenderLogicalPresentation(ren, lw, lh, mode);
	};

	SDL_AudioStream *audio = nullptr;
	SDL_AudioStream *chime_stream = nullptr;   // (the match found chime's: opened the first time)
	if (sound) {
#ifdef __ANDROID__
		// AAudio takes this hint as the stream's usage, AAUDIO_USAGE_GAME: without it the sound is
		// declared as the system's default for media, which is where Android's own effects (an
		// equaliser, ducking) and its game mode look. SDL's own other backends default to "Game".
		SDL_SetHint(SDL_HINT_AUDIO_DEVICE_STREAM_ROLE, "Game");
#endif
		SDL_AudioSpec spec{ SDL_AUDIO_S16, 2, model2a::SOUND_RATE };
		audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
		if (audio) {
			if (hidden) SDL_SetAudioStreamGain(audio, 0.0f);   // tests: silent
			SDL_ResumeAudioStreamDevice(audio);
		} else fprintf(stderr, "no audio: %s\n", SDL_GetError());
	}
	// Pacing: the game's frames by the clock, one every 1/57.52 s, evenly, so that each is shown.
	// The sound card's clock is the one that counts: the frame clock is nudged (1% at most) to
	// keep about QUEUED frames of sound queued on average, and a queue running low gets its frames
	// at once. Up to 8 frames late are caught up (a pause of 140 ms loses no game time); later,
	// the clock starts again from now. (Running frames whenever the queue was under 3 frames
	// followed the device's pulls, a frame's sound or more at a time: frames came in pairs and,
	// one picture a pass, a quarter of them were never shown.) Without sound, the clock alone.
	const int frame_bytes = int(model2a::SOUND_RATE / FRAME_HZ) * 4;
	constexpr double QUEUED = 3.5;

	SDL_Gamepad *pads[2] = { nullptr, nullptr };
	bool pads_swapped = false;           // two pads that look alike (same GUID): swapped by hand
	auto guid_of = [](SDL_JoystickID id) {
		char g[64];
		SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), g, sizeof g);
		return std::string(g);
	};
	// each player's pad: the one chosen (by its GUID) if it is there, else the next free one
	// ("auto"), or none
	auto open_pads = [&] {
		for (auto &pd : pads) if (pd) { SDL_CloseGamepad(pd); pd = nullptr; }
		give_unknown_sticks();   // (a joystick that came without its own event)
		int n = 0;
		SDL_JoystickID *ids = SDL_GetGamepads(&n);
		std::vector<bool> used(n, false);
		// first the pads chosen by name, then "auto" players take the free ones in order, then a
		// player whose chosen pad isn't there (or shows up differently: a pad in another mode
		// has another GUID) takes the next free one too. The free ones: the pads SDL knows first,
		// then the joysticks given a stick's layout here (one that isn't a pad at all, as a
		// keyboard's lights can be, doesn't take player 1 from a real pad)
		for (int pass = 0; pass < 3; pass++)
			for (int p = 0; p < 2; p++) {
				const std::string &c = cfg.ctl[p].pad_choice;
				if (pads[p] || c == "none") continue;
				const bool chosen = c != "auto";
				if ((pass == 0 && !chosen) || (pass == 1 && chosen)) continue;
				for (int stick = 0; stick < (pass == 0 ? 1 : 2) && !pads[p]; stick++)
					for (int i = 0; i < n && !pads[p]; i++)
						if (!used[i] && (pass != 0 ? g_stick_guids.count(guid_of(ids[i])) == size_t(stick) : guid_of(ids[i]) == c)) {
							pads[p] = SDL_OpenGamepad(ids[i]);
							used[i] = true;
						}
			}
		SDL_free(ids);
		if (pads_swapped && pads[0] && pads[1]) std::swap(pads[0], pads[1]);
		if (!pads[0] && !pads[1]) {   // (said, for a log from a player whose pad does nothing)
			int nj = 0;
			SDL_free(SDL_GetJoysticks(&nj));
			fprintf(stderr, "pads: none (%d gamepads, %d joysticks seen)\n", n, nj);
		}
	};
	open_pads();

	auto write_window = [&](const char *path) {
		int lw, lh;
		SDL_RendererLogicalPresentation mode;
		SDL_GetRenderLogicalPresentation(ren, &lw, &lh, &mode);
		SDL_SetRenderLogicalPresentation(ren, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
		if (SDL_Surface *shot = SDL_RenderReadPixels(ren, nullptr)) {
			if (SDL_Surface *x = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_XRGB8888)) {
				std::vector<uint32_t> px(size_t(x->w) * x->h);
				for (int y = 0; y < x->h; y++) memcpy(&px[size_t(y) * x->w], (uint8_t *)x->pixels + y * x->pitch, x->w * 4);
				write_png(path, px.data(), x->w, x->h);
				SDL_DestroySurface(x);
			}
			SDL_DestroySurface(shot);
		}
		SDL_SetRenderLogicalPresentation(ren, lw, lh, mode);
	};

	bool running = true, paused = false;
	const uint64_t start_ns = SDL_GetTicksNS();
	double frame_clock = 0;       // game frames due since the loop began (the paced clock)
	uint64_t frames_run = 0, clock_t = start_ns;
	double queue_avg = QUEUED * frame_bytes;   // the sound queued, averaged over about half a second
	uint64_t next_save = start_ns + 3000000000ull;
	bool new_picture = true;
	// the 3D's renderer, before the first picture (unseen): Auto starts on what it found here before
	// at this scale and antialiasing, and checks it; the GPU warmed up either way
	if (g3.dev && g3.mode != R3D_CPU) {
		g3.on = g3.mode == R3D_GPU || auto_kept() == 1;
		g3_decide("the start", false);
		if (g3.on) g3_targets(m.width() * scale, model2a::HEIGHT * scale);
		// (the test scene is like a fight, not one: checked again on the game's own once one's on show)
		if ((g3.mode == R3D_AUTO || g3.verify) && !g3.failed) { g3.recheck = g3.want_real = true; g3.recheck_why = "the game's own scene"; }
	}
	const bitmap_rgb32 *picture = &m.render(true);
	// The picture on show, as it can be drawn again (model2a::redraw): a change of scale, shape or
	// filter draws that same picture (the same moment, the same frame's 2D) at the new size, and
	// never runs the game. pending_pic: the one being drawn alongside (pipelining).
	shown_pic = m.last_picture();
	auto show_drawn = [&](const bitmap_rgb32 &b) { picture = &b; new_picture = true; shown_pic = m.last_picture(); };
	auto redraw_shown = [&] {
		picture = shown_pic ? &m.redraw(*shown_pic) : &m.render(true);
		new_picture = true;
	};
	auto restart_clock = [&] { clock_t = SDL_GetTicksNS(); frame_clock = double(frames_run); };

	// The load governor (a computer that can't keep up at this render scale): when, over a
	// 2-second window, the game runs at under 56.5 frames a second (of its 57.52) or under 55 of
	// them are shown, or when, for three windows in a row, running the game, drawing it and
	// uploading the picture (not the wait for the screen) take over 90% of the time, or the sound
	// runs dry while they take over 60%: antialiasing goes off, then the scale goes down a step.
	// Not while the window is out of sight. It never goes back up; the player's own change starts
	// it afresh. For this run: what's saved is the player's choice. Not in test runs (VF2_HIDDEN,
	// VF2_QUIT_AFTER) unless VF2_GOVERN is set.
	const bool govern = (!hidden && quit_after < 0) || getenv("VF2_GOVERN");
	// (as VF1's since 18 Oct: no cuts. The player's render scale, antialiasing and smooth motion
	// always; the governor only turns pipelining on. Its cuts lowered a fast computer whose first
	// seconds were busy, or whose 3D Auto had put on the CPU, to 1x for the run (vf2-recomp #19).
	// VF2_GOVERN_CUTS=1: tests, as before)
	const bool govern_cuts = getenv("VF2_GOVERN_CUTS") != nullptr;
	struct load_window { uint64_t since = 0, work = 0, frames = 0, starved = 0, worst = 0; int over = 0; bool first = true; int slow = 0; } load;
	// the ordinary passes of the window (see below, at the end of a pass): time, work, frames,
	// pictures, the sound running dry; gov_quiet: passes still to skip after a scene change's
	struct gov_window { uint64_t time = 0, work = 0, frames = 0, pictures = 0, starved = 0; } gov;
	int gov_quiet = 0;
	// Pipelining's own look, quicker than the governor's 2 s windows: half a second of ordinary
	// frames over 85% of their time switches it on (on a Steam Deck at 2x a fight was 16.2 ms a
	// frame of its 17.4 against the menus' far less: judged by the 2 s windows, after the scene
	// change's settling, it came 2-3 s into the first fight, which showed 40 pictures a second
	// until then). It costs a frame's delay and nothing else, so it's not held back at the start.
	struct { uint64_t work = 0, frames = 0; } pipe_look;
	// The governor's start: nothing judged in the first 10 s the game is on show (the first shader
	// and texture work, the OS and Steam settling: on a Steam Deck the first half minute is far
	// busier than the rest, and judged then the governor went from 3x down to 1x and stayed). And it
	// gives back what it took: after 10 s of calm (well under the load it lowers at), the scale or
	// antialiasing it lowered goes back up a step towards the player's, unless a step back up was
	// lowered again in this run.
	float test_screen_hz = 0;   // VF2_SCREEN_HZ=n: tests, the governor's screen as n Hz
	float screen_hz_now = 0;    // (the screen's refresh as the last 2 s window found it: Auto's spare passes)
	int vsync_now = -1;         // (the renderer's vsync as it reports it, looked at with the refresh: -1 not yet)
	uint64_t gov_from = UINT64_MAX;
	double test_load_ms = 0, test_load_s = 0;   // VF2_TEST_LOAD=ms:s (tests: a slow start)
	if (const char *tl = getenv("VF2_TEST_LOAD")) sscanf(tl, "%lf:%lf", &test_load_ms, &test_load_s);
	if (const char *hz = getenv("VF2_SCREEN_HZ")) test_screen_hz = float(atof(hz));
	int gov_calm = 0;
	bool gov_raised = false;
	int gov_calm_need = 5;   // calm windows (2 s) before a step back up: doubled each time one doesn't hold, to 30
	int player_scale = scale, player_aa = aa;   // the scale and antialiasing the governor goes back up to (the player's)
	// VF2_PACE_LOG's (and F9's) breakdown of a 2 s window: time in each step, passes, pictures, the sound queue
	// every 10 s, in the log whatever the settings (for a player's log from a machine we can't run):
	// frames and pictures a second, the load, and a picture's time in each step
	struct pace_ten { double secs = 0, frames = 0, pictures = 0, work = 0, emu = 0, draw = 0, upload = 0, present = 0, passes = 0, ahead = 0; } ten;   // (ahead: frames run during the present)
	struct pace_window { uint64_t emu = 0, waited = 0, waited_after = 0, draw = 0, upload = 0, blit = 0, present = 0, delay = 0, passes = 0, pictures = 0, held = 0; int qmin = 1 << 30, qmax = 0; } pace;
	bool timings = cfg.timings;
	// Smooth motion (Display > Smooth motion): besides each frame's picture, pictures for the
	// moments between frames, at most 120 a second (60 once the governor limits them): a budget,
	// not a minimum gap, which would skip a 120 Hz screen's refreshes that come a little early.
	bool smooth_limit = false;
	auto smooth_on = [&] { return cfg.smooth; };
	double smooth_budget = 1;
	uint64_t smooth_budget_t = SDL_GetTicksNS();
	bool last_shown = true;   // the newest frame is shown (not held by scene sync)
	load.since = SDL_GetTicksNS();
	scene_sync sync;      // clean scene changes (scene_sync.h): which picture to show
	sync.enabled = !no_scene_sync;
	m.defer_3d(sync.enabled);   // (every frame's 3D set up, drawn only if it's shown)

	// ---- the menu (Esc, or View on a pad) ----
	game_menu menu;
	// Online play's flags on the HUD, beside each player's name (m2hud::draw leaves their place in the box): the
	// countries' flags (assets/ui/flags.png, 48 x 36 each, as they are) drawn over the game's picture there, 12 x 9 of
	// the arcade's pixels in the middle of the letters' rows, its edges on the window's own pixels (sharp at any
	// size), after the picture and before the screen filter; as VF1's
	SDL_Surface *flag_atlas = nullptr;
	SDL_Texture *flag_tex[2] = { nullptr, nullptr };
	std::string hud_country[2], flag_made[2];   // (each player's, as the lobby has it: "" none; and the one each texture holds)
	auto hud_flag = [&](int p) -> SDL_Texture * {
		const int i = flags::index_of(hud_country[p]);
		if (i < 0) return nullptr;
		if (flag_tex[p] && flag_made[p] == hud_country[p]) return flag_tex[p];
		if (!flag_atlas)
			if (SDL_Surface *f = SDL_LoadSurface((find_assets(data) + "/ui/flags.png").c_str())) {
				flag_atlas = SDL_ConvertSurface(f, SDL_PIXELFORMAT_ABGR8888);
				SDL_DestroySurface(f);
			}
		if (!flag_atlas) return nullptr;
		const int x0 = i % flags::COLS * flags::CELL_W + 1, y0 = i / flags::COLS * flags::CELL_H + 1;
		if (x0 + flags::W > flag_atlas->w || y0 + flags::H > flag_atlas->h) return nullptr;
		if (flag_tex[p]) SDL_DestroyTexture(flag_tex[p]);
		flag_tex[p] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, flags::W, flags::H);
		if (!flag_tex[p]) return nullptr;
		SDL_UpdateTexture(flag_tex[p], nullptr, static_cast<const uint8_t *>(flag_atlas->pixels) + size_t(y0) * size_t(flag_atlas->pitch) + size_t(x0) * 4, flag_atlas->pitch);
		SDL_SetTextureScaleMode(flag_tex[p], SDL_SCALEMODE_LINEAR);
		SDL_SetTextureBlendMode(flag_tex[p], SDL_BLENDMODE_BLEND);
		flag_made[p] = hud_country[p];
		return flag_tex[p];
	};
	auto draw_hud_flags = [&] {
		if (hud_country[0].empty() && hud_country[1].empty()) return;
		int lw = 0, lh = 0;
		SDL_RendererLogicalPresentation mode;
		SDL_FRect out;
		SDL_GetRenderLogicalPresentation(ren, &lw, &lh, &mode);
		if (lw <= 0 || lh <= 0 || !SDL_GetRenderLogicalPresentationRect(ren, &out) || out.w <= 0) return;
		// (the arcade's pixels to the logical size's, and the logical size's to the window's: each edge put on a whole
		// window pixel, so the flag's never blurred into the box around it)
		const float sx = float(lw) / float(m.width()), sy = float(lh) / float(model2a::HEIGHT), wx = out.w / float(lw), wy = out.h / float(lh);
		auto snap_x = [&](float v) { return (std::round(out.x + v * sx * wx) - out.x) / wx; };
		auto snap_y = [&](float v) { return (std::round(out.y + v * sy * wy) - out.y) / wy; };
		for (int p = 0; p < 2; p++) {
			int x = 0, y = 0;
			if (!m.hud_flag_at(p, x, y)) continue;
			if (SDL_Texture *t = hud_flag(p)) {
				const float l = snap_x(float(x)), r = snap_x(float(x) + 12), top = snap_y(float(y) + 3.5f), bottom = snap_y(float(y) + 12.5f);   // (12 x 9, in the middle of the letters' rows)
				const SDL_FRect dst{ l, top, r - l, bottom - top };
				SDL_RenderTexture(ren, t, nullptr, &dst);
			}
		}
	};
	if (!menu.init(win, ren, find_assets(data)))
		fprintf(stderr, "menu: couldn't load assets/ui (the game runs without it)\n");
	menu.set_footer(std::string("Virtua Fighter 2 Recomp ") + VF2_VERSION + (*VF2_DATE ? std::string(" · ") + VF2_DATE : ""));
	// The render state in use, to the log: at the start, at each change of scale, shape or
	// antialiasing (the player's or the governor's), each step the governor takes, and as the menu
	// closes (a fresh start and a run after a change can be told apart from the log alone)
	auto log_state = [&](const char *why) {
		int vsync = 0, pw = 0, ph = 0;
		SDL_GetRenderVSync(ren, &vsync);
		SDL_GetWindowSizeInPixels(win, &pw, &ph);
		float hz = 0;
		if (const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win))) hz = dm->refresh_rate;
		const char *rn = SDL_GetRendererName(ren);
		const bool gpu2d = gpu.on && aa_now() == 1 && (scale >= 2 || gpu.any_scale);
		const i960rec::runtime *rec = m.recompiled();
		char r3d[160];
		if (!g3.dev || g3.mode == R3D_CPU) snprintf(r3d, sizeof r3d, "the CPU (%s%s)", R3D_NAMES[g3.mode], g3.dev ? "" : ", no GPU renderer");
		else snprintf(r3d, sizeof r3d, "the %s (%s%s, %s; last check CPU %.2f ms, GPU %.2f)", g3.on ? "GPU" : "CPU", R3D_NAMES[g3.mode], g3.failed ? ", the GPU failed" : "",
					  g3.dev->quality() == gpu3d_quality::exact ? "exact" : "fast", g3.cpu_ms, g3.gpu_ms);
		const std::string hints = sysinfo::perf_hints_available() ? std::string(", performance hints ") + (perf_hints_up ? "on" : "off") : std::string();
		fprintf(stderr, "render state (%s): %dx (drawn at %dx), antialiasing %s, %s %.2f:1, pipelined %s, the 3D on %s, the 2D on the %s (%s), drawn %s, smooth motion %s, governor %s (over %d, short %d), %d threads, vsync %d, frames in flight %d%s, screen %.2f Hz, window %d x %d, i960 %s\n",
				why, scale, m.render_scale(), aa == 0 ? "Low" : aa_now() > 1 ? (aa_now() == 2 ? "2x" : aa_now() == 3 ? "3x" : "on") : "off", ASPECT_NAMES[aspect_i], m.width() / 372.0,
				pipelined ? "yes" : "no", r3d, gpu2d || g3.on ? "GPU" : "CPU", rn ? rn : "?", draw_locked ? "into the texture" : "in memory, then sent up", !cfg.smooth ? "off" : smooth_limit ? "on (60 a second)" : "on",
				govern_cuts ? "on" : govern ? "pipelining only" : "off", load.over, load.slow, row_pool::get().threads(), vsync, cfg.frames_in_flight, hints.c_str(), hz, pw, ph,
				!rec ? "interpreted" : rec->nentry ? "native" : "recompiled C++");
	};
	// a new render scale or antialiasing: a new picture size
	auto set_scale = [&](int v, bool by_player = true) {
		v = std::clamp(v, 1, MAX_SCALE);
		scale_wanted = v;
		scale = std::min(v, scale_most());
		if (by_player) {
			cfg.scale = v; aa = cfg.aa; cfg_dirty = true;
			load = load_window{ SDL_GetTicksNS() }; gov = gov_window{};   // the player's own choice starts the governor afresh
			if (cfg.pipelined_at != scale) cfg.pipelined_at = 0;   // (what a run needed at another scale says nothing here)
			pipelined = pipeline_wanted();            // (and the pipelining, and smooth motion's limit)
			smooth_limit = false;
			gov_calm = 0; gov_raised = false; gov_calm_need = 5;
		}
		m.set_render_scale(scale * aa_fit(scale, aa));
		m.set_edge_aa(aa == 0);
		if (!make_texture()) { running = false; return; }
		if (by_player) { player_scale = v; player_aa = aa; }
		if (g3.mode == R3D_AUTO && (soft_render || getenv("VF2_AUTO_MEASURE"))) { g3.recheck = true; g3.want_real = false; g3.recheck_why = by_player ? "the player's scale or antialiasing" : "the governor's scale or antialiasing"; }   // (Auto measures no more: the GPU)
		redraw_shown();   // the picture on show, at the new size
		log_state(by_player ? "the player's scale or antialiasing" : "the governor's");
	};
	// a new shape: a new picture size (the 3D drawn from the next frame on)
	auto reshape = [&] {
		// the scale within the width's limit: lowered for a wider shape, back up to the player's (or the
		// governor's) for a narrower one
		scale = std::min(scale_wanted, scale_most());
		if (m.render_scale() != scale * aa_fit(scale, aa)) { m.set_render_scale(scale * aa_fit(scale, aa)); m.set_edge_aa(aa == 0); }
		if (!make_texture()) { running = false; return; }
		present_shape();
		redraw_shown();   // the picture on show, at the new shape (the 3D's lists set up again for the width)
		load = load_window{ SDL_GetTicksNS() }; gov = gov_window{};
		if (g3.mode == R3D_AUTO && (soft_render || getenv("VF2_AUTO_MEASURE"))) { g3.recheck = true; g3.want_real = false; g3.recheck_why = "a new shape"; }
		pipelined = pipeline_wanted();
		smooth_limit = false;
		log_state("a new shape");
	};
	// The player's own change of a setting the governor looks at (scale, antialiasing, smooth
	// motion, shape, fullscreen): every cut it made lifted and the governor started afresh. (Only
	// the changed setting's own was: smooth motion held to 60 stayed when the scale went down, and
	// a lowered scale stayed when the antialiasing changed, until 10 calm seconds judged against
	// the old settings, which might never come.)
	auto gov_fresh = [&] {
		if (scale != player_scale || aa != player_aa) { aa = player_aa; set_scale(player_scale); }   // (by the player: all of it afresh)
		load = load_window{ SDL_GetTicksNS() }; gov = gov_window{};
		smooth_limit = false;
		gov_calm = 0; gov_raised = false; gov_calm_need = 5;
	};
	// Display > Window size: the window k times the arcade's 384 lines high, in the window's own pixels (a Mac's
	// Retina window is sized in points: the pixel density's share of them), its width the picture shape's at that
	// height (4:3 and Fit: 512 x k, the 496 x 384 picture on its 4:3 tube; wider shapes the picture's own width, its
	// 384 * width / 372, times k), so the picture fills it and each arcade line is k of the screen's; only the sizes
	// that fit the screen's usable area. In fullscreen it waits for the window. Resized by hand: Free again. Not on a
	// phone (its window is the screen).
	int win_expect_w = 0, win_expect_h = 0;   // (the window's pixels as Window size made them)
	int win_set_w = 0, win_set_h = 0;         // (and its points)
	bool window_moved = false;                // (moved, resized, maximised: remembered after the pass's events)
	bool window_rescale = false;              // (made again after this pass's events: out of the OS's fullscreen, or
	                                          // moved to a screen of another pixel density)
	auto window_target = [&](int k, int &w, int &h) {
		h = 384 * k;
		w = aspect_i <= 1 ? 512 * k : int(std::lround(384.0 * m.width() / 372.0)) * k;
	};
	auto window_fits = [&](int k) {
		int w, h;
		window_target(k, w, h);
		const float d = std::max(SDL_GetWindowPixelDensity(win), 0.25f);
		SDL_Rect usable;
		if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(win), &usable) || usable.w <= 0 || usable.h <= 0) return k == 1;
		int top = 0, left = 0, bottom = 0, right = 0;
		if (!SDL_GetWindowBordersSize(win, &top, &left, &bottom, &right)) top = left = bottom = right = 0;
		return w / d + left + right <= usable.w && h / d + top + bottom <= usable.h;
	};
	auto window_scale_most = [&] {
		int k = 0;
		while (k < 16 && window_fits(k + 1)) k++;
		return k;
	};
	auto apply_window_scale = [&] {
#if defined(__ANDROID__)
		return;
#endif
		if (cfg.window_scale <= 0 || is_fullscreen() || fixed_window) return;
		const int most = window_scale_most();
		if (cfg.window_scale > most) {
			fprintf(stderr, "window size %dx: larger than the screen, %s\n", cfg.window_scale, most ? "the largest that fits" : "Free");
			cfg.window_scale = most;
			cfg_dirty = true;
			if (!most) return;
		}
		int w, h;
		window_target(cfg.window_scale, w, h);
		if (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) { SDL_RestoreWindow(win); SDL_SyncWindow(win); }
		const float d = std::max(SDL_GetWindowPixelDensity(win), 0.25f);
		int pw = int(std::lround(w / d)), ph = int(std::lround(h / d));
		SDL_SetWindowSize(win, pw, ph);
		SDL_SyncWindow(win);
		int gw = 0, gh = 0;
		SDL_GetWindowSizeInPixels(win, &gw, &gh);
		if ((gw != w || gh != h) && gw > 0 && gh > 0) {   // (a fractional density's rounding: a point more or less)
			pw += gw < w ? 1 : gw > w ? -1 : 0;
			ph += gh < h ? 1 : gh > h ? -1 : 0;
			SDL_SetWindowSize(win, pw, ph);
			SDL_SyncWindow(win);
			SDL_GetWindowSizeInPixels(win, &gw, &gh);
		}
		win_expect_w = gw; win_expect_h = gh;
		win_set_w = pw; win_set_h = ph;
		SDL_FlushEvent(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);   // (its own sizes on the way: not the player's resize)
		fprintf(stderr, "window size %dx: %d x %d pixels (%d x %d points, density %.2f)%s\n", cfg.window_scale, gw, gh, pw, ph, d,
		        gw == w && gh == h ? "" : ": not the size asked");
		if (!aspect_i && apply_aspect()) reshape();   // (Fit: the picture's shape the window's new one)
	};
	// the window's place and size kept in the settings as the player leaves them, in a window only (not fullscreen;
	// maximised: said so, the size under it kept); not for VF2_WINDOW's tests, nor a phone
	auto remember_window = [&] {
#if defined(__ANDROID__)
		return;
#endif
		if (fixed_window || is_fullscreen()) return;
		const SDL_WindowFlags fl = SDL_GetWindowFlags(win);
		if (fl & SDL_WINDOW_MINIMIZED) return;
		const bool mx = (fl & SDL_WINDOW_MAXIMIZED) != 0;
		int w = cfg.window_w, h = cfg.window_h, x = cfg.window_x, y = cfg.window_y;
		if (!mx || w <= 0) { SDL_GetWindowSize(win, &w, &h); SDL_GetWindowPosition(win, &x, &y); }   // (maximised from the first: its size then)
		if (w <= 0 || h <= 0) return;
		if (w == cfg.window_w && h == cfg.window_h && x == cfg.window_x && y == cfg.window_y && mx == cfg.window_max) return;
		cfg.window_w = w; cfg.window_h = h; cfg.window_x = x; cfg.window_y = y; cfg.window_max = mx;
		cfg_dirty = true;
	};
	auto set_window_scale = [&](int k) { cfg.window_scale = std::max(k, 0); cfg_dirty = true; apply_window_scale(); };
	auto set_shape = [&](int v) {
		aspect_i = cfg.aspect = v; cfg_dirty = true;
		apply_aspect();
		reshape();   // (the title too)
		apply_window_scale();   // (Window size: the new shape's width)
		gov_fresh();
	};
	auto set_fullscreen = [&](bool on) {
		cfg.fullscreen = on; cfg_dirty = true;
		SDL_Rect b;
		if (on && !fs_win_on && fs_borderless() && SDL_GetDisplayBounds(SDL_GetDisplayForWindow(win), &b)) {
			if (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) { SDL_SetWindowFullscreen(win, false); SDL_SyncWindow(win); }
			fs_win_max = (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) != 0;
			if (fs_win_max) { SDL_RestoreWindow(win); SDL_SyncWindow(win); }   // (the size under it, for the way back)
			SDL_GetWindowPosition(win, &fs_win_was.x, &fs_win_was.y);
			SDL_GetWindowSize(win, &fs_win_was.w, &fs_win_was.h);
			SDL_SetWindowBordered(win, false);
			SDL_SetWindowPosition(win, b.x, b.y);
			SDL_SetWindowSize(win, b.w, b.h + 1);
			fs_win_on = true;
			fprintf(stderr, "fullscreen: a borderless window, %d x %d%s\n", b.w, b.h + 1,
			        cfg.fs_window ? " (fullscreen_window)" : " (fullscreen_exclusive = 1 for the driver's own)");
		} else if (!on && fs_win_on) {
			SDL_SetWindowBordered(win, true);
			if (fs_win_was.w > 0) { SDL_SetWindowSize(win, fs_win_was.w, fs_win_was.h); SDL_SetWindowPosition(win, fs_win_was.x, fs_win_was.y); }
			if (fs_win_max) SDL_MaximizeWindow(win);
			fs_win_on = false;
		} else if (!fs_win_on) SDL_SetWindowFullscreen(win, on);
		SDL_SyncWindow(win);     // macOS animates into fullscreen: wait for it to finish
		if (!on) apply_window_scale();
		gov_fresh();
	};
	// borderless fullscreen from the start: the window (made at its remembered place and size) over the screen
	if (cfg.fullscreen && fs_borderless()) { const bool d = cfg_dirty; set_fullscreen(true); cfg_dirty = d; }
	apply_window_scale();   // (the saved Window size from the start; VF2_WINDOW's tests: the size they ask)
	// The game on a thread of its own while the screen waits (VF3's job_start and job_wait, VF1's
	// game thread): with smooth motion, a frame that will be due by the next refresh is run during
	// the present's wait for vsync, its inputs set as the frame before it ended (as ever); what
	// follows it (scene sync, its sound, the next frame's inputs, the hotkeys) as the present
	// returns, and its picture drawn at the next pass, as if it had run there. Meanwhile nothing
	// else touches the board: the present only, a picture drawn alongside (pipelined) going on as
	// it does beside any frame. The same frames, the same pictures. VF2_NO_EMU_THREAD=1: off.
	const bool emu_threaded = getenv("VF2_NO_EMU_THREAD") == nullptr && getenv("VF2_RECORD") == nullptr;   // (a recording: its frames by its own clock)
	// VF2_RECORD: the recording (see the top)
	struct {
		std::string dir;
		uint64_t from = 0, to = UINT64_MAX, n = 0, base = 0, stopped_n = 0;   // game frames; n: the pictures recorded (stopped_n of them while the game stood still); base: frames_run then
		bool on = false;
		FILE *video = nullptr, *wav = nullptr;
		uint32_t wav_bytes = 0;
		uint64_t silent = 0;
		int w = 0, h = 0, hz = 60;
	} rec;
	if (const char *d = getenv("VF2_RECORD")) {
		rec.dir = d;
		std::error_code rec_ec;
		std::filesystem::create_directories(rec.dir, rec_ec);
		if (const char *f = getenv("VF2_RECORD_FROM")) rec.from = strtoull(f, nullptr, 10);
		if (const char *f = getenv("VF2_RECORD_TO")) rec.to = strtoull(f, nullptr, 10);
		if (const char *f = getenv("VF2_RECORD_HZ")) rec.hz = std::clamp(atoi(f), 1, 240);
	}
	auto wav_header = [&] {
		const uint32_t rate = model2a::SOUND_RATE, br = rate * 4, sz = 36 + rec.wav_bytes, sixteen = 16;
		const uint16_t fmt = 1, ch = 2, align = 4, bits = 16;
		fseek(rec.wav, 0, SEEK_SET);
		fwrite("RIFF", 1, 4, rec.wav); fwrite(&sz, 4, 1, rec.wav); fwrite("WAVEfmt ", 1, 8, rec.wav);
		fwrite(&sixteen, 4, 1, rec.wav); fwrite(&fmt, 2, 1, rec.wav); fwrite(&ch, 2, 1, rec.wav);
		fwrite(&rate, 4, 1, rec.wav); fwrite(&br, 4, 1, rec.wav); fwrite(&align, 2, 1, rec.wav); fwrite(&bits, 2, 1, rec.wav);
		fwrite("data", 1, 4, rec.wav); fwrite(&rec.wav_bytes, 4, 1, rec.wav);
		fseek(rec.wav, 0, SEEK_END);
	};
	auto rec_stop = [&] {
		if (rec.video) pclose(rec.video);
		if (rec.wav) { wav_header(); fclose(rec.wav); }
		if (rec.on) fprintf(stderr, "record: %llu pictures, %.2f s, to %s\n", (unsigned long long)rec.n, rec.n / double(rec.hz), rec.dir.c_str());
		rec.video = rec.wav = nullptr;
		rec.on = false;
	};
	fprintf(stderr, "the game's thread: %s\n", emu_threaded ? "on (with smooth motion, the next frame run during the present's wait)" : "off (VF2_NO_EMU_THREAD)");
	background_job emu_job;
	struct { bool carried = false, into = false, held = false; } ahead;   // (that frame's picture, for the next pass)
	// Reset (the menu, F3): the menu closes, a paused game unpauses, and the screen goes black at
	// once while the boot runs unseen, not the last picture held.
	// the arcade settings as their page opened (and the ring): see arcade_page
	std::optional<std::array<int, 12>> assign_before;
	// Arcade settings > Debug mode changed: the board powered on before the restart, its backup RAM and EEPROM kept (as
	// the netplay adapter's cold start). A restart keeps the work RAM, where the debug mode's bit (14 of 0x508000) and
	// what it wrote stay: the game then as from a start, exactly
	auto cold_start = [&] {
		if (g_match_board) return;
		finish_async(false);
		const std::vector<u8> backup = m.backup_ram();
		u16 eeprom[64];
		memcpy(eeprom, m.eeprom_cells(), sizeof eeprom);
		m.power_on();
		m.backup_ram() = backup;
		memcpy(m.eeprom_cells(), eeprom, sizeof eeprom);
	};
	auto reset_game = [&] {
		// (never on a match's board: the host's settings booted, the same on both sides; end_match gives this player's
		// back first)
		if (g_match_board) { fprintf(stderr, "(no restart: an online match's board)\n"); return; }
		finish_async(false);
		ahead = {};   // (a frame run during the last present: its picture not drawn)
		shown_pic.reset();
		assign_before.reset();   // a restart applies them
		menu.close();
		paused = false;
		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
		SDL_RenderClear(ren);
		SDL_RenderPresent(ren);
		boot();
		g_test_frames = 0;
		save_nvram(false);
		fprintf(stderr, "the game restarted\n");
		std::vector<uint32_t> black(size_t(m.width()) * scale * model2a::HEIGHT * scale, 0xff000000);
		SDL_UpdateTexture(tex, nullptr, black.data(), m.width() * scale * 4);
		gpu.shown = false;
		new_picture = false;
		restart_clock();
	};
	// the settings as the player chose them (not what the governor lowered them to for now)
	auto save_settings = [&] {
		cfg.aspect = aspect_i;
		cfg.timings = timings;
		return cfg.save(settings_path);
	};
	auto on_off = [](bool b) { return std::string(b ? tr::ON : tr::OFF); };
	auto wrap = [](int v, int n) { return (v % n + n) % n; };
	// The on-screen controls (a touch screen): a d-pad, Guard, Punch and Kick, Start and a Menu
	// button over the picture (src/host/touch_controls.h). Auto shows them while player 1 has no
	// pad; On always; never over the menu (its own touch UI is there). Fingers are followed in the
	// event loop, where the renderer is at hand to place them (finger's x, y are of the window).
	touch_controls touch;
	bool touch_present = false;   // (a touch screen here: asked of SDL once, and a finger means one)
	{
		int n = 0;
		SDL_TouchID *devs = SDL_GetTouchDevices(&n);
		touch_present = n > 0;
		SDL_free(devs);
	}
	auto touch_shown = [&] {
		return !menu.is_open() && (cfg.on_screen == 2 || (cfg.on_screen == 1 && touch_present && !pads[0]));
	};
	// (Display > Picture's row for them: on Android, or with a touch screen here; and while they're On, so they can
	// always be turned off. A saved Auto without a touch screen shows nothing, as ever)
	auto on_screen_row = [&] {
#if defined(__ANDROID__)
		return true;
#else
		return touch_present || cfg.on_screen == 2;
#endif
	};
	auto back_item = [&] { return menu_item{ tr::BACK, tr::BACK_HELP, {}, {}, [&] { menu.pop(); } }; };
	// Game > Language: the setting, and the menus made again in the language at once (the page it's on kept)
	std::function<void()> menus_relabelled;   // (set below, with the pages)
	auto language_choice = [&] { tr::choice c; tr::choice_from(cfg.language, c); return c; };
	auto set_language = [&](tr::choice c) {
		cfg.language = tr::choice_key(c);
		cfg_dirty = true;
		tr::set(tr::resolve(c));
		fprintf(stderr, "Game > Language: %s (the menus in %s)\n", cfg.language.c_str(), tr::code(tr::current()));
		menu.set_language();
		if (menus_relabelled) menus_relabelled();
	};
	auto x_n = [](int n) { return tr::fill(tr::N_X, { { "n", std::to_string(n) } }); };
	// The free camera (see the top): on; the menu's choice (taken as the menu closes); the speed (the wheel's, LB and
	// RB's); kept off while an online match is being played (fc_blocked: set once online's made, below)
	bool freecam = false, fc_menu = false;
	double fc_speed = 1;
	std::function<bool()> fc_blocked = [] { return false; };
	std::function<void(bool, bool)> set_freecam = [](bool, bool) {};
	// Display's rows in three pages of their own, each a row of Display with what it's set to in a few words (a page
	// is 14 rows at most: menu.rcss): Picture, Quality, Screen filter and colours. Each row as it was.
	auto picture_page = [&] {
		menu_page p{ tr::D_PICTURE, "", {} };
		p.items.push_back({ tr::D_SHAPE, tr::D_SHAPE_HELP,
			[&] { return std::string(aspect_i ? ASPECT_NAMES[aspect_i] : tr::D_SHAPE_FIT.c_str()); },
			[&](int d) { set_shape(wrap(aspect_i + d, N_ASPECTS)); } });
#if !defined(__ANDROID__)
		p.items.push_back({ tr::D_WINDOW_SIZE, tr::D_WINDOW_SIZE_HELP,
			[&] {
				if (cfg.window_scale <= 0) return std::string(tr::D_FREE);
				int w, h;
				window_target(cfg.window_scale, w, h);
				return tr::fill(is_fullscreen() ? tr::D_WINDOW_N_IN_WINDOW : tr::D_WINDOW_N, { { "n", std::to_string(cfg.window_scale) }, { "w", std::to_string(w) }, { "h", std::to_string(h) } });
			},
			[&](int d) { set_window_scale(wrap(cfg.window_scale + d, window_scale_most() + 1)); } });
#endif
		p.items.push_back({ tr::D_FULLSCREEN, tr::D_FULLSCREEN_HELP,
			[&] { return on_off(is_fullscreen()); },
			[&](int) { set_fullscreen(!is_fullscreen()); } });
#ifdef _WIN32
		p.items.push_back({ tr::D_FS_MODE, tr::D_FS_MODE_HELP,
			[&] { return std::string(fs_borderless() ? tr::D_BORDERLESS : tr::D_STANDARD); },
			[&](int) {
				const bool was = is_fullscreen();
				if (was) set_fullscreen(false);
				cfg.fs_exclusive = fs_borderless(); cfg_dirty = true;
				if (was) set_fullscreen(true);
				menu.refresh();
			} });
#endif
		p.items.push_back({ tr::D_HUD, tr::D_HUD_HELP,
			[&] { return on_off(cfg.hud); },
			[&](int) { cfg.hud = !cfg.hud; cfg_dirty = true; m.set_hud_hidden(!cfg.hud); redraw_shown(); } });
		p.items.push_back({ tr::D_SHOW_FPS, tr::D_SHOW_FPS_HELP,
			[&] { return on_off(cfg.show_fps); },
			[&](int) { cfg.show_fps = !cfg.show_fps; cfg_dirty = true; if (!cfg.show_fps) menu.set_fps(""); } });
		if (on_screen_row())
			p.items.push_back({ tr::D_ON_SCREEN, tr::D_ON_SCREEN_HELP,
				[&] { return std::string(cfg.on_screen == 0 ? tr::OFF : cfg.on_screen == 2 ? tr::ON : tr::D_ON_SCREEN_AUTO); },
				[&](int d) { cfg.on_screen = wrap(cfg.on_screen + d, 3); cfg_dirty = true; if (!touch_shown()) touch.release(); } });
		p.items.push_back(back_item());
		return p;
	};
	auto quality_page = [&] {
		menu_page p{ tr::D_QUALITY, "", {} };
		p.items.push_back({ tr::D_SCALE, tr::D_SCALE_HELP,
			[&] { return scale < scale_wanted ? tr::fill(tr::D_SCALE_NARROWER, { { "n", std::to_string(scale) }, { "want", std::to_string(scale_wanted) } }) : x_n(scale); },
			[&](int d) { set_scale(std::clamp(scale_wanted + d, 1, MAX_SCALE)); } });
		p.items.push_back({ tr::D_AA, tr::D_AA_HELP,
			[&] { return aa == 0 ? std::string(tr::D_AA_LOW) : aa_now() == 1 ? std::string(aa > 1 ? tr::D_AA_OFF_TOO_LARGE : tr::OFF) : x_n(aa_now()); },
			[&](int d) {
				static const int ORDER[] = { 1, 0, 2, 3 };   // Off, Low, 2x, 3x (as vf1-recomp)
				int i = 0;
				while (i < 3 && ORDER[i] != cfg.aa) i++;
				cfg.aa = ORDER[std::clamp(i + d, 0, 3)]; aa = cfg.aa; set_scale(player_scale);   // (a scale the governor lowered: the player's again)
			} });
		p.items.push_back({ tr::D_RENDERER, tr::D_RENDERER_HELP,
			[&] {
				const tr::text names[] = { tr::R3D_CPU, tr::R3D_GPU, tr::AUTO };   // (R3D_NAMES' order: the log's English)
#if !defined(VF2_GPU3D_METAL) && !defined(VF2_GPU3D_SDL)
				return std::string(tr::D_R3D_NO_BUILD);
#endif
				if (!g3.dev && gpu_crashed) return std::string(tr::D_R3D_CRASHED);
				if (!g3.dev && g3.next_start && g3.mode != R3D_CPU) return tr::fill(tr::D_R3D_NEXT_START, { { "what", names[g3.mode] } });
				if (!g3.dev) return std::string(g3.mode == R3D_CPU ? tr::R3D_CPU : g3.tried ? tr::D_R3D_NO_GPU : names[g3.mode]);
				if (g3.mode == R3D_AUTO) return tr::fill(tr::AUTO_IS, { { "what", g3.on ? tr::R3D_GPU : tr::R3D_CPU } });
				return std::string(g3.failed && g3.mode == R3D_GPU ? tr::D_R3D_FAILED : names[g3.mode]);
			},
			[&](int d) {
#if !defined(VF2_GPU3D_METAL) && !defined(VF2_GPU3D_SDL)
				return;   // (no GPU's 3D: the setting kept as it is)
#endif
				// (after a crashed start the first press only tries the GPU again, the setting kept: one press on to CPU
				// would have left it there)
				if (!gpu_crashed) {
					static const int ORDER[] = { R3D_AUTO, R3D_CPU, R3D_GPU };
					int i = 0;
					while (i < 2 && ORDER[i] != g3.mode) i++;
					g3.mode = cfg.renderer3d = ORDER[wrap(i + d, 3)]; cfg_dirty = true;
				}
				cfg.gpu3d_failed.clear();   // (the player's choice: every GPU driver tried again from the next start, a crashed start too)
				cfg.auto3d.clear();   // (and Auto's choices made again)
				cfg_dirty = true;
				SDL_RemovePath((data + "/gpu-start.crashed").c_str());
				gpu_crashed = false;
				menu.toast("");   // (its advice followed: the notice taken down)
				finish_async(false);
				if (g3.mode != R3D_CPU) g3_start();
				g3_decide("the player's choice", true);   // (the menu's open: nothing moves)
				redraw_shown();
				log_state("the player's 3D renderer");
			} });
#ifdef _WIN32
		p.items.push_back({ tr::D_DRIVER, tr::D_DRIVER_HELP,
			[&] { return std::string(cfg.graphics ? "Direct3D 12" : tr::D_DRIVER_AUTO.c_str()); },
			[&](int) {
				cfg.graphics = !cfg.graphics; cfg_dirty = true;
				SDL_RemovePath((data + "/gpu-start.crashed").c_str());   // (another driver: a crashed start tried again with it)
				gpu_crashed = false;
				cfg.gpu3d_failed.clear();   // (and the drivers' failures forgotten: they'd choose the order over this)
				cfg.auto3d.clear();
				menu.toast("");
			} });
#endif
		if (g3.dev && !strcmp(g3.dev->name(), "metal")) p.items.push_back({ tr::D_GPU_QUALITY, tr::D_GPU_QUALITY_HELP,
			[&] { return std::string(cfg.gpu_quality ? tr::D_FAST : tr::D_EXACT); },
			[&](int) {
				cfg.gpu_quality = !cfg.gpu_quality; cfg_dirty = true;
				if (!g3.dev) return;
				finish_async(false);
				g3.dev->set_quality(cfg.gpu_quality ? gpu3d_quality::fast : gpu3d_quality::exact);
				g3_decide("the player's GPU quality", true);
				redraw_shown();
			} });
		p.items.push_back({ tr::D_BG_FILTER, tr::D_BG_FILTER_HELP,
			[&] { return std::string(cfg.smooth_bg ? tr::D_SMOOTH : tr::OFF); },
			[&](int) {
				cfg.smooth_bg = !cfg.smooth_bg; cfg_dirty = true;
				m.set_backdrop_filter(cfg.smooth_bg);
				redraw_shown();
			} });
		p.items.push_back({ tr::D_SMOOTH_MOTION, tr::D_SMOOTH_MOTION_HELP,
			[&] { return on_off(cfg.smooth); },
			[&](int) { cfg.smooth = !cfg.smooth; cfg_dirty = true; gov_fresh(); } });   // (the picture on show stays)
		{
			int now = 0;
			if (SDL_GetRenderVSync(ren, &now))
				p.items.push_back({ tr::D_VSYNC, tr::D_VSYNC_HELP,
					[&] { int v = 0; SDL_GetRenderVSync(ren, &v); return on_off(v != 0); },
					[&](int) { cfg.vsync = !cfg.vsync; cfg_dirty = true; apply_display_settings(); } });
		}
		p.items.push_back({ tr::D_FRAME_LIMIT, tr::D_FRAME_LIMIT_HELP,
			[&] {
				if (cfg.frame_limit == 0) return std::string(tr::OFF);
				if (cfg.frame_limit < 0) {
					const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win));
					return tr::fill(tr::D_FRAME_LIMIT_SCREEN, { { "n", std::to_string(int(std::lround(dm && dm->refresh_rate > 0 ? dm->refresh_rate : 60.0f))) } });
				}
				return std::to_string(cfg.frame_limit);
			},
			[&](int d) {
				static const int ORDER[] = { 0, 30, 60, 120, 144, -1 };
				int i = 0;
				while (i < 5 && ORDER[i] != cfg.frame_limit) i++;
				cfg.frame_limit = ORDER[wrap(i + d, 6)];
				cfg_dirty = true;
				fprintf(stderr, "Display > Frame limit: %d (0 none, -1 the screen's)\n", cfg.frame_limit);
			} });
		if (const char *renderer_now = SDL_GetRendererName(ren); renderer_now && !strcmp(renderer_now, "gpu"))
			p.items.push_back({ tr::D_FRAMES_IN_FLIGHT, tr::D_FRAMES_IN_FLIGHT_HELP,
				[&] { return std::to_string(cfg.frames_in_flight); },
				[&](int d) { cfg.frames_in_flight = std::clamp(cfg.frames_in_flight + d, 1, 3); cfg_dirty = true; apply_display_settings(); } });
#if defined(__ANDROID__)
		if (sysinfo::perf_hints_available())
			p.items.push_back({ tr::D_PERF_HINTS, tr::D_PERF_HINTS_HELP,
				[&] { return on_off(cfg.perf_hints); },
				[&](int) { cfg.perf_hints = !cfg.perf_hints; cfg_dirty = true; apply_perf_hints(); } });
#endif
		p.items.push_back(back_item());
		return p;
	};
	auto screen_page = [&] {
		menu_page p{ tr::D_SCREEN, "", {} };
		p.items.push_back({ tr::D_SCREEN_FILTER, tr::D_SCREEN_FILTER_HELP,
			[&] { const tr::text names[] = { tr::OFF, tr::D_SCANLINES, tr::D_CRT }; return std::string(names[cfg.filter]); },
			[&](int d) { cfg.filter = wrap(cfg.filter + d, 3); cfg_dirty = true; } });
		p.items.push_back({ tr::D_CRT_COLOURS, tr::D_CRT_COLOURS_HELP,
			[&] {
				const tr::text names[] = { tr::OFF, tr::CRT_SEGA, tr::CRT_SONY, tr::CRT_NTSC_JP, tr::CRT_NTSC_US, tr::CRT_PAL };
				static_assert(sizeof names / sizeof names[0] == crt::N_PRESETS, "a name for each of crt::PRESETS");
				const std::string n = names[cfg.crt_colours];
				return n;
			},
			[&](int d) { cfg.crt_colours = wrap(cfg.crt_colours + d, crt::N_PRESETS); cfg_dirty = true; } });
		p.items.push_back({ tr::D_BRIGHTNESS, tr::D_BRIGHTNESS_HELP,
			[&] { return tr::fill(tr::N_PERCENT, { { "n", std::to_string(cfg.brightness) } }); },
			[&](int d) { cfg.brightness = std::clamp(cfg.brightness + 5 * d, crt::BRIGHT_MIN, crt::BRIGHT_MAX); cfg_dirty = true; } });
		p.items.push_back({ tr::D_GAMMA, tr::D_GAMMA_HELP,
			[&] { return tr::fill(tr::N_PERCENT, { { "n", std::to_string(cfg.gamma) } }); },
			[&](int d) { cfg.gamma = std::clamp(cfg.gamma + 5 * d, crt::GAMMA_MIN, crt::GAMMA_MAX); cfg_dirty = true; } });
		// (a renderer that can't paint them: said under each of the three, their values as set)
		for (size_t i = 1; i < p.items.size(); i++)
			p.items[i].below = [&] { return crt_on && crt_paint.failed() ? std::string(tr::D_NOT_WITH_RENDERER) : std::string(); };
		p.items.push_back(back_item());
		return p;
	};
	// (a list as a sentence has it, in the language: the pages' helps name their rows, the summaries their parts)
	auto listed = [](std::initializer_list<std::string> parts) {
		std::string out;
		for (const std::string &x : parts) if (!x.empty()) out = out.empty() ? x : tr::fill(tr::LIST_PAIR, { { "a", out }, { "b", x } });
		return out;
	};
	auto display_page = [&] {
		menu_page p{ tr::P_DISPLAY, "", {} };
		// Picture: its shape, and fullscreen or the window's size ("16:9, Fullscreen", "4:3, Window 2x")
		p.items.push_back({ tr::D_PICTURE,
			listed({ tr::D_SHAPE,
#if !defined(__ANDROID__)
				tr::D_WINDOW_SIZE,
#endif
				tr::D_FULLSCREEN,
#ifdef _WIN32
				tr::D_FS_MODE,
#endif
				tr::D_HUD, tr::D_SHOW_FPS, on_screen_row() ? std::string(tr::D_ON_SCREEN) : std::string() }),
			[&] {
				return listed({ std::string(aspect_i ? ASPECT_NAMES[aspect_i] : tr::D_SUM_FIT.c_str()),
				                is_fullscreen() ? std::string(tr::D_FULLSCREEN)
				                : cfg.window_scale > 0 ? tr::fill(tr::D_SUM_WINDOW_N, { { "n", std::to_string(cfg.window_scale) } }) : std::string(tr::D_SUM_WINDOW) });
			}, {}, [&] { menu.push(picture_page()); } });
		// Quality: the render scale, the antialiasing in use if any, and the 3D renderer drawing ("3x, AA 2x, GPU")
		p.items.push_back({ tr::D_QUALITY,
			listed({ tr::D_SCALE, tr::D_AA, tr::D_RENDERER,
#ifdef _WIN32
				tr::D_DRIVER,
#endif
				g3.dev && !strcmp(g3.dev->name(), "metal") ? std::string(tr::D_GPU_QUALITY) : std::string(),
				tr::D_BG_FILTER, tr::D_SMOOTH_MOTION, tr::D_VSYNC, tr::D_FRAME_LIMIT,
				SDL_GetRendererName(ren) && !strcmp(SDL_GetRendererName(ren), "gpu") ? std::string(tr::D_FRAMES_IN_FLIGHT) : std::string(),
#if defined(__ANDROID__)
				sysinfo::perf_hints_available() ? std::string(tr::D_PERF_HINTS) : std::string(),
#endif
				}),
			[&] {
				const int k = aa_now();
				return listed({ x_n(scale),
				                aa == 0 ? tr::fill(tr::D_SUM_AA, { { "n", std::string(tr::D_AA_LOW) } }) : k > 1 ? tr::fill(tr::D_SUM_AA, { { "n", x_n(k) } }) : std::string(),
				                std::string(g3.on ? tr::R3D_GPU : tr::R3D_CPU) });
			}, {}, [&] { menu.push(quality_page()); } });
		// Screen filter and colours: the filter, and the CRT colours if on ("Scanlines", "Off, CRT colours")
		p.items.push_back({ tr::D_SCREEN, listed({ tr::D_SCREEN_FILTER, tr::D_CRT_COLOURS, tr::D_BRIGHTNESS, tr::D_GAMMA }),
			[&] {
				const tr::text names[] = { tr::OFF, tr::D_SCANLINES, tr::D_CRT };
				return listed({ std::string(names[cfg.filter]), cfg.crt_colours ? std::string(tr::D_CRT_COLOURS) : std::string() });
			}, {}, [&] { menu.push(screen_page()); } });
		p.items.push_back({ tr::D_FREECAM, tr::D_FREECAM_HELP,
			[&] { return fc_blocked() ? std::string(tr::D_FREECAM_NOT_ONLINE) : on_off(fc_menu); },
			[&](int) { if (fc_blocked()) menu.toast(tr::FREECAM_NOT_ONLINE); else fc_menu = !fc_menu; } });   // (on or off as the menu closes; in a match of the player's own, the toast)
		p.items.push_back(back_item());
		return p;
	};
	auto sound_page = [&] {
		menu_page p{ tr::P_SOUND, "", {} };
		p.items.push_back({ tr::S_VOLUME, tr::S_VOLUME_HELP,
			[&] { return tr::fill(tr::N_PERCENT, { { "n", std::to_string(cfg.volume) } }); },
			[&](int d) {
				cfg.volume = std::clamp(cfg.volume + 10 * d, 0, 100); cfg_dirty = true;
				m.set_level(LEVEL * cfg.volume / 100.0f);
				g_match_volumes = pack_volumes(cfg.volume, cfg.music, cfg.effects);
			} });
		p.items.push_back({ tr::S_MUSIC, tr::S_MUSIC_HELP,
			[&] { return tr::fill(tr::N_PERCENT, { { "n", std::to_string(cfg.music) } }); },
			[&](int d) {
				cfg.music = std::clamp(cfg.music + 10 * d, 0, 100); cfg_dirty = true;
				m.set_volumes(cfg.music / 100.0f, cfg.effects / 100.0f);
				g_match_volumes = pack_volumes(cfg.volume, cfg.music, cfg.effects);
			} });
		p.items.push_back({ tr::S_EFFECTS, tr::S_EFFECTS_HELP,
			[&] { return tr::fill(tr::N_PERCENT, { { "n", std::to_string(cfg.effects) } }); },
			[&](int d) {
				cfg.effects = std::clamp(cfg.effects + 10 * d, 0, 100); cfg_dirty = true;
				m.set_volumes(cfg.music / 100.0f, cfg.effects / 100.0f);
				g_match_volumes = pack_volumes(cfg.volume, cfg.music, cfg.effects);
			} });
		p.items.push_back(back_item());
		return p;
	};
	auto game_page = [&] {
		menu_page p{ tr::P_GAME, "", {} };
		p.items.push_back({ tr::G_LANGUAGE, tr::G_LANGUAGE_HELP,
			[&] {
				const tr::choice c = language_choice();
				return c.automatic() ? tr::fill(tr::LANG_AUTO_IS, { { "lang", tr::info(tr::system()).name } }) : tr::choice_name(c);
			},
			[&](int d) { set_language(tr::step(language_choice(), d)); } });
		p.items.push_back({ tr::G_PLAY_STYLE, tr::G_PLAY_STYLE_HELP,
			[&] { return std::string(cfg.arcade ? tr::G_ARCADE : tr::G_PC); },
			[&](int) {
				cfg.arcade = !cfg.arcade; g_pc_mode = !cfg.arcade; cfg_dirty = true;
				apply_play_style();
				reset_game();   // (boot gives the coin setting to the style now in use)
			} });
		p.items.push_back({ tr::G_RESET, tr::G_RESET_HELP,
			{}, {}, [&] { reset_game(); } });
		p.items.push_back({ tr::G_LOG, tr::G_LOG_HELP,
			[&] { return on_off(cfg.log); }, [&](int) { cfg.log = !cfg.log; cfg_dirty = true; }, [&] { cfg.log = !cfg.log; cfg_dirty = true; } });
		p.items.push_back(back_item());
		return p;
	};
	// ---- the arcade's own settings (test mode's GAME ASSIGNMENT): the game reads them as it
	// starts, so closing the menu after changing one restarts it (only if something is different) ----
	using ga = model2a::game_assignment;
	constexpr int N_ASSIGN = 7;
	static_assert(std::tuple_size_v<decltype(assign_before)::value_type> == N_ASSIGN + 5);
	auto assignments_now = [&] {
		std::array<int, N_ASSIGN + 5> v;
		for (int i = 0; i < N_ASSIGN; i++) v[i] = m.assignment(ga(i));
		v[N_ASSIGN] = cfg.ring;
		v[N_ASSIGN + 1] = cfg.secret;   // (Cheats: the select sets the fighter as it opens, so it restarts too)
		v[N_ASSIGN + 2] = cfg.random_cpu;   // (and the random fighters: each player's, Random opponents)
		v[N_ASSIGN + 3] = int(std::hash<std::string>{}(cfg.random_player[0] + "/" + cfg.random_player[1]));
		v[N_ASSIGN + 4] = cfg.debug_mode;   // (Debug mode: off, the game as ever from its start)
		return v;
	};
	auto arcade_page = [&] {
		if (!assign_before) assign_before = assignments_now();
		menu_page p{ tr::P_ARCADE, "", {} };   // always in view
		auto choice = [&](tr::text label, tr::text help, ga a, std::function<std::string(int)> name) {
			p.items.push_back({ label, help,
				[&m, a, name] { int v = m.assignment(a); return v < 0 ? std::string("-") : name(v); },
				[&m, a](int d) { int v = m.assignment(a); if (v >= 0) m.set_assignment(a, v + d); } });
		};
		static const tr::text DIFFICULTY[] = { tr::A_EASY, tr::A_NORMAL, tr::A_HARD, tr::A_HARDEST };
		choice(tr::A_DIFFICULTY, tr::A_DIFFICULTY_HELP, ga::difficulty, [](int v) { return std::string(DIFFICULTY[v]); });
		static const int RINGS[] = { 0, 1300, 1400, 1500, 1600 };   // the arcade's widths (STAGE WIDTH)
		static const tr::text RING_NAMES[] = { tr::A_RING_DEFAULT, tr::A_RING_SMALL, tr::A_RING_MEDIUM, tr::A_RING_LARGE, tr::A_RING_XL };
		p.items.push_back({ tr::A_RING, tr::A_RING_HELP,
			[&] { return std::string(RING_NAMES[std::find(std::begin(RINGS), std::end(RINGS), cfg.ring) - std::begin(RINGS)]); },
			[&](int d) {
				int i = int(std::find(std::begin(RINGS), std::end(RINGS), cfg.ring) - std::begin(RINGS));
				cfg.ring = RINGS[std::clamp(i + d, 0, int(std::size(RINGS)) - 1)];
				cfg_dirty = true;
				m.set_ring_width(cfg.ring);
			} });
		choice(tr::A_ROUNDS_CPU, tr::A_ROUNDS_CPU_HELP, ga::match_1p, [](int v) { return std::to_string(v); });
		choice(tr::A_ROUNDS_VS, tr::A_ROUNDS_VS_HELP, ga::match_vs, [](int v) { return std::to_string(v); });
		choice(tr::A_CONTINUE, tr::A_CONTINUE_HELP, ga::continues, [&](int v) { return on_off(v); });
		choice(tr::A_DEMO_SOUND, tr::A_DEMO_SOUND_HELP, ga::advertise_sound, [&](int v) { return on_off(v); });
		choice(tr::A_AFTER_VS, tr::A_AFTER_VS_HELP, ga::vs_finish, [](int v) { return std::string(v ? tr::A_BOTH_STOP : tr::A_WINNER_ON); });
		choice(tr::A_VERSION, tr::A_VERSION_HELP, ga::version, [](int v) { return v ? std::string("2.1") : std::string(tr::A_ORIGINAL); });
		// (Stage select, issue #33: offline only, an online match's board never; no restart, from the next Start)
		p.items.push_back({ tr::A_STAGE_SELECT, tr::A_STAGE_SELECT_HELP,
			[&] { return cfg.stage < 0 ? std::string(tr::OFF) : cfg.stage == vf2net::STAGES ? std::string(tr::A_STAGE_DURAL) : tr::fill(tr::A_STAGE_N, { { "n", std::to_string(cfg.stage + 1) } }); },
			[&](int d) { const int v = std::clamp(cfg.stage + d, -1, vf2net::STAGES); cfg.stage = v == 0 ? (d > 0 ? 1 : -1) : v; cfg_dirty = true; } });   // (Stage 1 is Off's)
		// (Debug mode: Cheats' before, settings.ini's debug_mode key as it was; offline only, never on a match's board)
		p.items.push_back({ tr::A_DEBUG, tr::A_DEBUG_HELP,
			[&] { return on_off(cfg.debug_mode); },
			[&](int) { cfg.debug_mode = !cfg.debug_mode; cfg_dirty = true; } });   // (applied as the game restarts)
		p.items.push_back({ tr::A_SERVICE, tr::A_SERVICE_HELP,
			{}, {}, [&] { menu.close(); paused = false; g_test_pending = true; } });
		p.items.push_back(back_item());
		return p;
	};
	// ---- cheats: each player's fighter (random fighters), Random opponents, the secret characters (in Akira's place) ----
	// Each player's fighter's row state: typing a seed in it, what was typed, a mistake; and where the page put the
	// rows (to make it again with the same one selected)
	struct seed_entry { bool typing = false, pending = false; std::string typed; tr::line error; };   // (pending: Seed chosen, none typed yet: plays as Random)
	std::shared_ptr<seed_entry> seed_st[2] = { std::make_shared<seed_entry>(), std::make_shared<seed_entry>() };
	int fighter_row[2] = { 0, 0 }, fighter_sub[2] = { -1, -1 };
	std::function<menu_page()> cheats_page;
	// (out here: the rows' functions outlive the page's making)
	auto kind_of = [&](int pl) { const std::string &v = cfg.random_player[pl]; return v.empty() ? 0 : v == "*" ? 1 : 2; };
	static const tr::text KINDS[] = { tr::FIGHTER_NORMAL, tr::FIGHTER_RANDOM, tr::FIGHTER_SEED };
	auto rebuild = [&](int pl, bool sub) { menu_page pg = cheats_page(); menu.replace(std::move(pg), sub && fighter_sub[pl] >= 0 ? fighter_sub[pl] : fighter_row[pl]); };
	cheats_page = [&] {
		if (!assign_before) assign_before = assignments_now();
		menu_page p{ tr::P_CHEATS_PAGE, "", {} };
		auto heading = [&](tr::text t) { menu_item h{ t, "", {}, {}, {}, true }; h.heading = true; p.items.push_back(h); };
		heading(tr::C_FIGHTERS);
		// Each player's fighter (Normal, Random, Seed), and under it, when it's Seed, a row of its own for the seed
		// (typed right in it, a mistake said under it). Anything but Normal is picked for that player at the select
		for (int pl = 0; pl < 2; pl++) {
			auto st = seed_st[pl];
			auto entry = std::make_shared<std::function<bool(const SDL_Event &)>>();
			auto set_kind = [&, pl](const std::string &v) { cfg.random_player[pl] = v; if (v != "" && v != "*") cfg.random_named[pl] = v; cfg_dirty = true; };
			auto start_typing = [&, pl, st, entry] {
				st->typing = true; st->typed.clear(); st->error.clear();
				rebuild(pl, true);
				menu.capture(tr::SEED_HINT, *entry, [&, pl, st] { st->typing = false; st->error.clear(); rebuild(pl, false); });
			};
			*entry = [&, pl, st, set_kind](const SDL_Event &e) {
				bool enter = false;
				if (e.type == SDL_EVENT_KEY_DOWN) {
					const SDL_Scancode sc = e.key.scancode;
					SDL_Keycode k = e.key.key;   // (the letter on the key, the keyboard's layout: AZERTY's A is the scancode's Q)
					if (k == SDLK_UNKNOWN && sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) k = SDLK_A + (sc - SDL_SCANCODE_A);   // (none given: by its place)
					if (k >= SDLK_A && k <= SDLK_Z) { if (st->typed.size() < 12) st->typed += char('A' + (k - SDLK_A)); }
					else if (sc == SDL_SCANCODE_BACKSPACE) { if (!st->typed.empty()) st->typed.pop_back(); }
					else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) enter = true;
					else return false;
				} else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
					std::string &t = st->typed;
					auto turn = [&](int d) { if (t.empty()) t.push_back('A'); else t.back() = char('A' + ((t.back() - 'A' + d) % 26 + 26) % 26); };
					switch (e.gbutton.button) {
					case SDL_GAMEPAD_BUTTON_EAST: st->typing = false; st->error.clear(); rebuild(pl, false); return true;   // (B: as it was)
					case SDL_GAMEPAD_BUTTON_SOUTH: enter = true; break;
					case SDL_GAMEPAD_BUTTON_DPAD_UP: turn(+1); break;
					case SDL_GAMEPAD_BUTTON_DPAD_DOWN: turn(-1); break;
					case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: if (t.size() < 12) t.push_back(t.empty() ? 'A' : t.back()); break;
					case SDL_GAMEPAD_BUTTON_DPAD_LEFT: if (!t.empty()) t.pop_back(); break;
					default: return false;
					}
				} else
					return false;
				st->error.clear();
				if (enter) {
					if (st->typed.empty()) st->error = tr::SEED_TYPE_ONE;
					else if (!m.random_fighter_names().count(st->typed)) st->error = tr::line(tr::SEED_NONE, { { "seed", st->typed } });
					else { set_kind(st->typed); st->typing = st->pending = false; rebuild(pl, true); return true; }
				}
				menu.refresh();
				return false;
			};
			fighter_row[pl] = int(p.items.size());
			p.items.push_back({ pl ? tr::C_P2_FIGHTER : tr::C_P1_FIGHTER, tr::C_FIGHTER_HELP,
				[&, pl, st] { return std::string(KINDS[st->typing || st->pending ? 2 : kind_of(pl)]); },
				[&, pl, st, set_kind](int d) {
					const int k = wrap((st->pending ? 2 : kind_of(pl)) + d, 3);
					st->typing = st->pending = false; st->error.clear();
					if (k == 0) set_kind("");
					else if (k == 1) set_kind("*");
					else if (!cfg.random_named[pl].empty() && m.random_fighter_names().count(cfg.random_named[pl])) set_kind(cfg.random_named[pl]);
					else { st->pending = true; set_kind("*"); }   // (no seed yet: Random until one is typed in its row)
					rebuild(pl, false);
				},
				{}, false,
				[&, pl, st, set_kind] { st->typing = st->pending = false; set_kind(""); rebuild(pl, false); } });
			fighter_sub[pl] = -1;
			if ((st->typing || st->pending ? 2 : kind_of(pl)) == 2) {
				fighter_sub[pl] = int(p.items.size());
				menu_item it{ tr::FIGHTER_SEED, tr::C_SEED_HELP,
					[&, pl, st] { return st->typing ? st->typed + "_" : st->pending ? std::string(tr::SEED_PENDING) : cfg.random_player[pl]; },
					{}, start_typing };
				it.below = [st] { return st->typing ? st->error.str() : std::string(); };
				it.sub = true;
				p.items.push_back(it);
			}
		}
		p.items.push_back({ tr::C_RANDOM_CPU, tr::C_RANDOM_CPU_HELP,
			[&] { return on_off(cfg.random_cpu); },
			[&](int) { cfg.random_cpu = !cfg.random_cpu; cfg_dirty = true; } });   // (applied as the game restarts)
		heading(tr::C_SECRET_HEAD);
		static const int SECRET[] = { 0, 9, 11, 12 };
		static const char *SECRET_NAMES[] = { "Off", "Dural", "MUE", "KOJAC" };   // (the fighters' names as they are; Off the language's)
		p.items.push_back({ tr::C_SECRET, tr::C_SECRET_HELP,
			[&] { const auto i = std::find(std::begin(SECRET), std::end(SECRET), cfg.secret) - std::begin(SECRET); return i ? std::string(SECRET_NAMES[i]) : std::string(tr::OFF); },
			[&](int d) {
				const int i = int(std::find(std::begin(SECRET), std::end(SECRET), cfg.secret) - std::begin(SECRET));
				const int n = int(std::size(SECRET));
				cfg.secret = SECRET[(i + d + n) % n];
				cfg_dirty = true;   // (applied as the game restarts: boot)
			} });
		p.items.push_back(back_item());
		return p;
	};
	// ---- controls: per player, keys and pad buttons, and which pad ----
	// The controls' names in the language on (CONTROL_NAMES: the log's and the settings' English), and a pad button's
	auto control_name = [](int a) -> std::string {
		static const tr::text names[C_COUNT] = { tr::CTL_UP, tr::CTL_DOWN, tr::CTL_LEFT, tr::CTL_RIGHT, tr::CTL_GUARD, tr::CTL_PUNCH, tr::CTL_KICK,
			tr::CTL_GUARD_PUNCH, tr::CTL_PUNCH_KICK, tr::CTL_GUARD_KICK, tr::CTL_ALL_THREE, tr::CTL_START, tr::CTL_COIN, tr::CTL_MENU };
		return a >= 0 && a < C_COUNT ? std::string(names[a]) : std::string();
	};
	auto pad_label = [](int code) -> std::string {   // (the buttons' printed names as they are: A, B, LB, Start, View)
		switch (code) {
		case SDL_GAMEPAD_BUTTON_LEFT_STICK: return tr::PAD_LEFT_STICK;
		case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return tr::PAD_RIGHT_STICK;
		case SDL_GAMEPAD_BUTTON_TOUCHPAD: return tr::PAD_TOUCHPAD;
		case SDL_GAMEPAD_BUTTON_DPAD_UP: return tr::PAD_DPAD_UP;
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return tr::PAD_DPAD_DOWN;
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return tr::PAD_DPAD_LEFT;
		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return tr::PAD_DPAD_RIGHT;
		default: return player_controls::pad_label(code);
		}
	};
	auto key_name = [](SDL_Scancode k) {
		if (!k) return std::string("-");
		const char *n = SDL_GetScancodeName(k);   // (the keys' own names, as they're printed: not translated)
		return n && *n ? std::string(n) : tr::fill(tr::CTL_KEY_N, { { "n", std::to_string(int(k)) } });
	};
	auto pad_name = [&](int p) {
		const std::string &c = cfg.ctl[p].pad_choice;
		const char *nm = pads[p] ? SDL_GetGamepadName(pads[p]) : nullptr;
		std::string now = pads[p] ? (nm && *nm ? nm : tr::CTL_GAMEPAD.c_str()) : "";
		if (c == "none") return std::string(tr::CTL_NONE);
		if (c == "auto") return now.empty() ? std::string(tr::CTL_ANY_PAD_NONE) : tr::fill(tr::CTL_ANY_PAD, { { "pad", now } });
		return now.empty() ? std::string(tr::CTL_CHOSEN_PAD_GONE) : now;
	};
	// the pad choices: any free pad, each connected pad, none
	auto cycle_pad = [&](int p, int d) {
		std::vector<std::string> opts = { "auto" };
		int n = 0;
		SDL_JoystickID *ids = SDL_GetGamepads(&n);
		for (int i = 0; i < n; i++) opts.push_back(guid_of(ids[i]));
		SDL_free(ids);
		opts.push_back("none");
		auto it = std::find(opts.begin(), opts.end(), cfg.ctl[p].pad_choice);
		int i = it == opts.end() ? 0 : int(it - opts.begin());
		cfg.ctl[p].pad_choice = opts[wrap(i + d, int(opts.size()))];
		// a pad chosen by one player is no longer the other's
		if (cfg.ctl[p].pad_choice != "auto" && cfg.ctl[p].pad_choice != "none" && cfg.ctl[1 - p].pad_choice == cfg.ctl[p].pad_choice)
			cfg.ctl[1 - p].pad_choice = "auto";
		cfg_dirty = true;
		open_pads();
	};
	// (slot 0 a control's key or button, 1 its second: Controls > Second keys, Second pad buttons)
	auto key_of = [&](int p, int slot, int a) -> SDL_Scancode & { return slot ? cfg.ctl[p].key2[a] : cfg.ctl[p].key[a]; };
	auto pad_of = [&](int p, int slot, int a) -> int & { return slot ? cfg.ctl[p].pad2[a] : cfg.ctl[p].pad[a]; };
	// A player's keys: the directions, the buttons, Start, Coin and Menu; the four combinations (Guard + Punch and
	// the rest) on a page of their own from it, Button combinations (14 rows and Back were more than a page's 14)
	std::function<menu_page(int, int, bool)> keys_page = [&](int p, int slot, bool combos) {
		menu_page pg{ combos ? tr::CTL_COMBOS : slot ? (p ? tr::P_P2_KEYBOARD2 : tr::P_P1_KEYBOARD2) : (p ? tr::P_P2_KEYBOARD : tr::P_P1_KEYBOARD), "", {} };
		for (int a = 0; a < C_COUNT; a++) {
			const bool combo = a >= C_GUARD_PUNCH && a <= C_ALL_THREE;
			if (!combos && a == C_GUARD_PUNCH)
				pg.items.push_back({ tr::CTL_COMBOS, tr::fill(tr::CTL_COMBOS_HELP, { { "list", listed({ control_name(C_GUARD_PUNCH), control_name(C_PUNCH_KICK), control_name(C_GUARD_KICK), control_name(C_ALL_THREE) }) } }),
					{}, {}, [&, p, slot] { menu.push(keys_page(p, slot, true)); } });
			if (combo != combos) continue;
			pg.items.push_back({ control_name(a), a == C_COIN ? tr::CTL_KEY_HELP_COIN : a == C_MENU ? tr::CTL_KEY_HELP_MENU : tr::CTL_KEY_HELP,
				[&, p, slot, a] { return key_name(key_of(p, slot, a)); }, {},
				[&, p, slot, a] {
					menu.capture(tr::fill(p ? tr::CTL_PRESS_KEY_P2 : tr::CTL_PRESS_KEY_P1, { { "control", control_name(a) } }),
						[&, p, slot, a](const SDL_Event &e) {
							if (e.type != SDL_EVENT_KEY_DOWN) return false;
							const SDL_Scancode k = e.key.scancode == SDL_SCANCODE_BACKSPACE || e.key.scancode == SDL_SCANCODE_DELETE ? SDL_SCANCODE_UNKNOWN : e.key.scancode;
							// the game's own keys (pause, reset, the menu's shortcuts, the cabinet's coin and
							// test buttons) stay theirs: wait for another key
							static const SDL_Scancode RESERVED[] = { SDL_SCANCODE_P, SDL_SCANCODE_TAB, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
								SDL_SCANCODE_F5, SDL_SCANCODE_F6, SDL_SCANCODE_F9, SDL_SCANCODE_F11, SDL_SCANCODE_F12 };
							for (SDL_Scancode r : RESERVED) if (k == r) return false;
							for (const auto &sk : SYS_KEYS) if (k == sk.key) return false;
							// a key does one thing: take it off whatever had it
							if (k) for (auto &c : cfg.ctl) { for (auto &x : c.key) if (x == k) x = SDL_SCANCODE_UNKNOWN; for (auto &x : c.key2) if (x == k) x = SDL_SCANCODE_UNKNOWN; }   // (first or second)
							key_of(p, slot, a) = k;
							cfg_dirty = true;
							return true;
						});
				}, false, [&, p, slot, a] { key_of(p, slot, a) = SDL_SCANCODE_UNKNOWN; cfg_dirty = true; } });
		}
		pg.items.push_back(back_item());
		return pg;
	};
	auto pad_buttons_page = [&](int p, int slot) {
		menu_page pg{ slot ? (p ? tr::P_P2_PAD2 : tr::P_P1_PAD2) : (p ? tr::P_P2_PAD : tr::P_P1_PAD), "", {} };
		for (int a = C_GUARD; a < C_COUNT; a++)
			pg.items.push_back({ control_name(a), a == C_COIN ? tr::CTL_PADB_HELP_COIN : a == C_MENU ? tr::CTL_PADB_HELP_MENU : tr::CTL_PADB_HELP,
				[&, p, slot, a] { return pad_label(pad_of(p, slot, a)); }, {},
				[&, p, slot, a] {
					menu.capture(tr::fill(a >= C_COIN && !slot ? (p ? tr::CTL_PRESS_PAD_P2 : tr::CTL_PRESS_PAD_P1) : (p ? tr::CTL_PRESS_PAD_VIEW_P2 : tr::CTL_PRESS_PAD_VIEW_P1), { { "control", control_name(a) } }),
						[&, p, slot, a](const SDL_Event &e) {
							int code = PAD_NONE;
							if (e.type == SDL_EVENT_KEY_DOWN) {
								if (e.key.scancode != SDL_SCANCODE_DELETE && e.key.scancode != SDL_SCANCODE_BACKSPACE) return false;
								pad_of(p, slot, a) = PAD_NONE;
								cfg_dirty = true;
								return true;
							}
							if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
								// View: keep it (View is for the menu's hotkeys, not moves), unless it's for Coin or Menu
								if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK && (a < C_COIN || slot)) return true;   // (a second button never takes View)
								code = e.gbutton.button;
							} else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e.gaxis.value > 16000 &&
									   (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))
								code = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? PAD_LT : PAD_RT;
							else return false;
							for (int b = C_GUARD; b < C_COUNT; b++) { if (cfg.ctl[p].pad[b] == code) cfg.ctl[p].pad[b] = PAD_NONE; if (cfg.ctl[p].pad2[b] == code) cfg.ctl[p].pad2[b] = PAD_NONE; }   // (first or second)
							pad_of(p, slot, a) = code;
							cfg_dirty = true;
							return true;
						});
				}, false, [&, p, slot, a] { pad_of(p, slot, a) = PAD_NONE; cfg_dirty = true; } });
		pg.items.push_back(back_item());
		return pg;
	};
	// Set up buttons: each of the game's controls asked for in turn, read from the pad's own joystick (its buttons,
	// hats and axes as they are, not through its mapping), and a mapping made of them for its GUID (as SDL's database's):
	// the directions the d-pad, Guard Punch Kick X A B (the Pad buttons' defaults, set back to them) and Start, Menu on
	// View (the menu's own button, tapped), the buttons left over Y RB LB RT (the combinations' defaults: Guard + Punch,
	// Punch + Kick, Guard + Kick, all three) and the rest, a pad's left stick kept where it rests in the middle and
	// wasn't pressed for something else. Kept in settings.ini (pad_mapping), so a DirectInput stick's own layout,
	// whatever it is, works from then on. Esc stops; Delete skips one (a stick without it); 10 s with nothing pressed
	// stops too.
	struct setup_step { const char *name, *target; int action; };
	static const setup_step SETUP_STEPS[] = {
		{ "Up", "dpup", -1 }, { "Down", "dpdown", -1 }, { "Left", "dpleft", -1 }, { "Right", "dpright", -1 },
		{ "Guard", "x", C_GUARD }, { "Punch", "a", C_PUNCH }, { "Kick", "b", C_KICK },
		{ "Start", "start", C_START }, { "Menu (View or Select; Start again if it has none)", "back", -1 } };
	constexpr int N_SETUP = int(sizeof SETUP_STEPS / sizeof SETUP_STEPS[0]);
	constexpr int SETUP_START = N_SETUP - 2, SETUP_FIRST_BUTTON = 4;   // (Start's step, Menu after it; Guard's the first button's)
	static_assert(N_SETUP == 9, "SETUP_START, SETUP_FIRST_BUTTON and the Pad buttons' CODE follow SETUP_STEPS");
	struct {
		int player = -1;            // the player whose pad's being set up (-1: none)
		SDL_Joystick *joy = nullptr;
		int step = 0;
		bool wait_release = true;   // (the press before, or the button that chose Set up buttons, let go first)
		uint64_t since = 0;         // (SDL_GetTicks: the prompt's, for its time out)
		bool skip = false;          // (Delete: this one skipped)
		std::vector<std::string> got;   // each prompt's input, as a mapping has it ("b3", "h0.1", "-a1"; empty: skipped)
		std::vector<Sint16> rest;   // the axes as they were at the start (a trigger's rest is one end)
		std::string orig;           // the pad's mapping before (what of it isn't pressed for something else is kept)
	} setup;
	auto setup_end = [&] {
		if (setup.joy) SDL_CloseJoystick(setup.joy);
		setup.joy = nullptr;
		setup.player = -1;
	};
	auto setup_prompt = [&] {
		const int p = setup.player;
		static const tr::text SETUP_NAMES[] = { tr::CTL_UP, tr::CTL_DOWN, tr::CTL_LEFT, tr::CTL_RIGHT, tr::CTL_GUARD, tr::CTL_PUNCH, tr::CTL_KICK,
			tr::CTL_START, tr::SETUP_MENU_STEP };
		static_assert(sizeof SETUP_NAMES / sizeof SETUP_NAMES[0] == N_SETUP, "a name for each of SETUP_STEPS");
		menu.capture(tr::fill(tr::SETUP_PRESS, { { "button", SETUP_NAMES[setup.step] }, { "player", std::to_string(p + 1) }, { "n", std::to_string(setup.step + 1) }, { "of", std::to_string(N_SETUP) } }),
			[&](const SDL_Event &e) {   // (the pad's own events are swallowed meanwhile: what counts is read below)
				if (e.type == SDL_EVENT_KEY_DOWN && (e.key.scancode == SDL_SCANCODE_DELETE || e.key.scancode == SDL_SCANCODE_BACKSPACE)) setup.skip = true;
				return false;
			},
			[&] { setup_end(); fprintf(stderr, "set up buttons: stopped\n"); });
	};
	auto setup_start = [&](int p) {
		if (!pads[p]) { menu.toast(tr::SETUP_NO_PAD); return; }
		setup_end();
		setup.joy = SDL_OpenJoystick(SDL_GetGamepadID(pads[p]));   // (a reference of its own: pads are opened again as they come and go)
		if (!setup.joy) { menu.toast(tr::SETUP_UNREADABLE); return; }
		setup.player = p;
		setup.step = 0;
		setup.wait_release = true;
		setup.skip = false;
		setup.since = SDL_GetTicks();
		setup.got.clear();
		setup.rest.clear();
		if (char *om = SDL_GetGamepadMapping(pads[p])) { setup.orig = om; SDL_free(om); } else setup.orig.clear();
		for (int a = 0; a < SDL_GetNumJoystickAxes(setup.joy); a++) setup.rest.push_back(SDL_GetJoystickAxis(setup.joy, a));
		fprintf(stderr, "set up buttons: player %d, %s (%s): %d buttons, %d hats, %d axes\n", p + 1, SDL_GetJoystickName(setup.joy),
		        joystick_guid(SDL_GetJoystickID(setup.joy)).c_str(), SDL_GetNumJoystickButtons(setup.joy), SDL_GetNumJoystickHats(setup.joy),
		        SDL_GetNumJoystickAxes(setup.joy));
		setup_prompt();
	};
	auto forget_mapping = [&](const std::string &guid) {   // (settings.ini's pad_mapping for that GUID: true if there was one)
		auto &all = cfg.pad_mappings;
		const size_t was = all.size();
		all.erase(std::remove_if(all.begin(), all.end(), [&](const std::string &o) { return o.compare(0, o.find(','), guid) == 0; }), all.end());
		return all.size() != was;
	};
	auto setup_finish = [&] {
		SDL_Joystick *j = setup.joy;
		const int p = setup.player;
		const std::string guid = joystick_guid(SDL_GetJoystickID(j));
		std::string m = guid + "," + mapping_name(SDL_GetJoystickName(j)) + ",";
		std::set<std::string> targets, sources;   // (what's mapped, and from what: an axis without its sign)
		auto source = [](std::string v) {   // (an axis's sign and SDL's ~ for one turned round off)
			if (v.size() > 1 && (v[0] == '+' || v[0] == '-')) v.erase(0, 1);
			if (!v.empty() && v.back() == '~') v.pop_back();
			return v;
		};
		auto add = [&](const std::string &t, const std::string &v) { m += t + ":" + v + ","; targets.insert(t); sources.insert(source(v)); };
		for (size_t i = 0; i < setup.got.size(); i++)
			if (!setup.got[i].empty()) add(SETUP_STEPS[i].target, setup.got[i]);
		// the pad's own mapping where it doesn't clash: a pad's triggers, sticks and the rest kept as they were
		for (size_t at = setup.orig.find(',', setup.orig.find(',') + 1); at != std::string::npos && at + 1 < setup.orig.size();) {
			const size_t end = setup.orig.find(',', at + 1);
			const std::string f = setup.orig.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
			at = end;
			const size_t colon = f.find(':');
			if (colon == std::string::npos || f.compare(0, colon, "platform") == 0 || f.compare(0, colon, "crc") == 0) continue;
			const std::string t = f.substr(0, colon), v = f.substr(colon + 1);
			if (!targets.count(t) && !sources.count(source(v))) add(t, v);
		}
		if (!targets.count("leftx") && !sources.count("a0") && !sources.count("a1") && setup.rest.size() >= 2 &&
		    std::abs(int(setup.rest[0])) < 8000 && std::abs(int(setup.rest[1])) < 8000) {
			add("leftx", "a0");
			add("lefty", "a1");
		}
		// (the buttons left over: the combinations' Pad buttons first, Y RB LB RT as player_controls::defaults has them)
		static const char *const EXTRA[] = { "y", "rightshoulder", "leftshoulder", "righttrigger", "lefttrigger", "misc1", "paddle1", "paddle2", "paddle3", "paddle4" };
		size_t x = 0;
		for (int b = 0; b < SDL_GetNumJoystickButtons(j); b++) {
			if (sources.count("b" + std::to_string(b))) continue;
			while (x < std::size(EXTRA) && targets.count(EXTRA[x])) x++;
			if (x == std::size(EXTRA)) break;
			add(EXTRA[x], "b" + std::to_string(b));
		}
		setup_end();
		menu.end_capture();
		// (a mapping given before ours with more say, as Steam's SDL_GAMECONTROLLERCONFIG, keeps its place: not taken)
		const int added = SDL_AddGamepadMapping(m.c_str());
		char *now = added < 0 ? nullptr : SDL_GetGamepadMappingForGUID(SDL_StringToGUID(guid.c_str()));
		const std::string body = m.substr(guid.size() + 1, m.size() - guid.size() - 2);   // (the name and the fields, no last comma)
		const bool taken = now && std::string(now).find(body) != std::string::npos;
		if (now) SDL_free(now);
		if (!taken) {
			fprintf(stderr, "set up buttons: the mapping wasn't taken (%s): %s\n", added < 0 ? SDL_GetError() : "another, from Steam or SDL_GAMECONTROLLERCONFIG, decides", m.c_str());
			menu.toast(added < 0 ? tr::SETUP_FAILED : tr::SETUP_STEAM);
			return;
		}
		fprintf(stderr, "set up buttons: %s\n", m.c_str());
		g_stick_guids.erase(guid);   // (the player's own now: an Any pad like any other)
		forget_mapping(guid);
		cfg.pad_mappings.push_back(m);
		// the Pad buttons back on what was pressed for them (each code taken from whatever else had it), for this player
		// and the other if theirs is the same kind of pad (a mapping is a GUID's: two identical sticks share it)
		static const int CODE[] = { SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_START };
		for (int q = 0; q < 2; q++) {
			if (q != p && !(pads[q] && joystick_guid(SDL_GetGamepadID(pads[q])) == guid)) continue;
			for (int i = SETUP_FIRST_BUTTON; i <= SETUP_START && i < int(setup.got.size()); i++) {
				if (setup.got[i].empty()) continue;
				const int code = CODE[i - SETUP_FIRST_BUTTON];
				for (int b = C_GUARD; b < C_COUNT; b++) { if (cfg.ctl[q].pad[b] == code) cfg.ctl[q].pad[b] = PAD_NONE; if (cfg.ctl[q].pad2[b] == code) cfg.ctl[q].pad2[b] = PAD_NONE; }
				cfg.ctl[q].pad[SETUP_STEPS[i].action] = code;
			}
		}
		cfg_dirty = true;
		menu.refresh();
		menu.toast(pads[1 - p] && joystick_guid(SDL_GetGamepadID(pads[1 - p])) == guid
		           ? tr::fill(tr::SETUP_DONE_BOTH, { { "player", std::to_string(p + 1) }, { "other", std::to_string(2 - p) } })
		           : tr::fill(tr::SETUP_DONE, { { "player", std::to_string(p + 1) } }));
	};
	// each pass of the loop while it's on: the joystick's state read (SDL updates it as the events are polled)
	auto setup_poll = [&] {
		if (setup.player < 0) return;
		SDL_Joystick *j = setup.joy;
		if (!menu.capturing() || !SDL_JoystickConnected(j)) {
			const bool gone = menu.capturing();
			setup_end();
			if (gone) { menu.end_capture(); menu.toast(tr::SETUP_DISCONNECTED); }
			return;
		}
		const int nb = SDL_GetNumJoystickButtons(j), nh = SDL_GetNumJoystickHats(j), na = std::min(SDL_GetNumJoystickAxes(j), int(setup.rest.size()));
		std::string in;   // the first input pressed now, as a mapping has it
		bool any = false;
		for (int b = 0; b < nb; b++)
			if (SDL_GetJoystickButton(j, b)) { any = true; if (in.empty()) in = "b" + std::to_string(b); }
		for (int h = 0; h < nh; h++) {
			const Uint8 v = SDL_GetJoystickHat(j, h);
			if (v == SDL_HAT_CENTERED) continue;
			any = true;
			if (in.empty() && (v == SDL_HAT_UP || v == SDL_HAT_RIGHT || v == SDL_HAT_DOWN || v == SDL_HAT_LEFT))   // (not a diagonal)
				in = "h" + std::to_string(h) + "." + std::to_string(v);
		}
		for (int a = 0; a < na; a++) {
			const int v = SDL_GetJoystickAxis(j, a), d = v - setup.rest[a];
			if (std::abs(d) > 8000) any = true;
			if (in.empty() && std::abs(d) > 16000) in = (d < 0 ? "-a" : "+a") + std::to_string(a);
		}
		const uint64_t now = SDL_GetTicks();
		if (setup.wait_release) {
			if (!any) { setup.wait_release = false; setup.since = now; }
			else if (now - setup.since > 10000) {   // (a switch that reads as held, or an axis off its rest: never let go)
				setup_end();
				menu.end_capture();
				menu.toast(tr::SETUP_HELD);
				fprintf(stderr, "set up buttons: something held for 10 s\n");
			}
			return;
		}
		// (Start again on Menu, its last: skipped, for a pad without one and a player without a keyboard)
		const bool start_again = setup.step > SETUP_START && !in.empty() && in == setup.got[SETUP_START];
		if (setup.skip || start_again) { setup.skip = false; in.clear(); }
		else if (in.empty() || std::find(setup.got.begin(), setup.got.end(), in) != setup.got.end()) {   // (nothing new pressed)
			if (now - setup.since > 10000) {
				if (setup.step > SETUP_START) {   // (Start's had: what was pressed kept, Menu left as it was)
					fprintf(stderr, "set up buttons: Menu not pressed for 10 s: kept as it was\n");
					setup.got.push_back("");
					setup_finish();
					return;
				}
				setup_end();
				menu.end_capture();
				menu.toast(tr::SETUP_TIMEOUT);
				fprintf(stderr, "set up buttons: nothing pressed for 10 s\n");
			}
			return;
		}
		fprintf(stderr, "set up buttons: %s is %s\n", SETUP_STEPS[setup.step].name, in.empty() ? "skipped" : in.c_str());
		setup.got.push_back(in);
		setup.wait_release = true;
		setup.since = now;
		if (++setup.step == N_SETUP) setup_finish();
		else setup_prompt();
	};
	auto player_page = [&](int p) {
		menu_page pg{ p ? tr::P_PLAYER_2 : tr::P_PLAYER_1, "", {} };
		pg.items.push_back({ tr::CTL_PAD, tr::CTL_PAD_HELP,
							 [&, p] { return pad_name(p); }, [&, p](int d) { cycle_pad(p, d); } });
		pg.items.push_back({ tr::CTL_KEYBOARD, tr::CTL_KEYBOARD_HELP, {}, {}, [&, p] { menu.push(keys_page(p, 0, false)); } });
		pg.items.push_back({ tr::CTL_KEYBOARD2, tr::CTL_KEYBOARD2_HELP, {}, {}, [&, p] { menu.push(keys_page(p, 1, false)); } });
		pg.items.push_back({ tr::CTL_PAD_BUTTONS, tr::CTL_PAD_BUTTONS_HELP,
							 {}, {}, [&, p] { menu.push(pad_buttons_page(p, 0)); } });
		pg.items.push_back({ tr::CTL_PAD_BUTTONS2, tr::CTL_PAD_BUTTONS2_HELP,
							 {}, {}, [&, p] { menu.push(pad_buttons_page(p, 1)); } });
		pg.items.push_back({ tr::CTL_SETUP, tr::CTL_SETUP_HELP,
							 {}, {}, [&, p] { setup_start(p); } });
		pg.items.push_back({ tr::CTL_RESET, tr::CTL_RESET_HELP, {}, {},
							 [&, p] {
								 // (a pad's set-up buttons forgotten: SDL keeps a mapping for the run, so its own is back next start)
								 // (not while the other player's pad is the same kind: the mapping's theirs too)
								 if (pads[p]) {
									 const std::string g = joystick_guid(SDL_GetGamepadID(pads[p]));
									 if (pads[1 - p] && joystick_guid(SDL_GetGamepadID(pads[1 - p])) == g)
										 menu.toast(tr::CTL_RESET_SAME_KIND);
									 else if (forget_mapping(g))
										 menu.toast(tr::CTL_RESET_FORGOTTEN);
								 }
								 cfg.ctl[p] = player_controls::defaults(p);
								 // the default keys may be the other player's now: take them off there
								 for (auto k : cfg.ctl[p].key)
									 if (k) { for (auto &x : cfg.ctl[1 - p].key) if (x == k) x = SDL_SCANCODE_UNKNOWN; for (auto &x : cfg.ctl[1 - p].key2) if (x == k) x = SDL_SCANCODE_UNKNOWN; }
								 cfg_dirty = true;
								 open_pads();
							 } });
		pg.items.push_back(back_item());
		return pg;
	};
	auto controls_page = [&] {
		menu_page pg{ tr::P_CONTROLS, "", {} };
		pg.items.push_back({ tr::P_PLAYER_1, tr::CTL_P1_HELP, [&] { return pad_name(0); }, {},
							 [&] { menu.push(player_page(0)); } });
		pg.items.push_back({ tr::P_PLAYER_2, tr::CTL_P2_HELP, [&] { return pad_name(1); }, {},
							 [&] { menu.push(player_page(1)); } });
		pg.items.push_back({ tr::CTL_SWAP, tr::CTL_SWAP_HELP, {}, {},
							 [&] {
								 auto choice = [&](int p) { return pads[p] ? guid_of(SDL_GetGamepadID(pads[p])) : std::string("none"); };
								 std::string a = choice(0), b = choice(1);
								 if (a == b) {          // alike (same model, Steam Input): swap who holds which
									 pads_swapped = !pads_swapped;
									 std::swap(pads[0], pads[1]);
									 return;
								 }
								 cfg.ctl[0].pad_choice = b;
								 cfg.ctl[1].pad_choice = a;
								 cfg_dirty = true;
								 open_pads();
							 } });
		pg.items.push_back(back_item());
		return pg;
	};
	// ---- Online play (online.h): matches through the session; in a match the board holds the host's NVRAM and
	// settings, and this player's own wait here ----
	static Online online(m);
	std::vector<uint8_t> own_backup;
	uint16_t own_eeprom[64] = {};
	bool match_on = false, resimmed = false;
	std::string hud_player[2];   // (a match's: the players' names for the HUD, each shown while they're in the game)
	int hud_in_shown = -1;
	bool menu_closed_elsewhere = false;   // (the menu closed by a match starting: its closing's bookkeeping still owed)
	// The match's sound: a sound board alone (model2a::set_sound_only) on a thread of its own, to the real clock,
	// fed the bytes the match's board sends its (held: the same on both sides); the queue kept topped up whatever
	// the frames do (held back for the other side, slowed to keep in time), so it never drops out or changes pitch
	struct match_sound_t {
		std::unique_ptr<model2a> board;
		std::thread th;
		std::mutex mx;
		std::vector<uint8_t> in;
		std::atomic<bool> quit{ false };
		std::atomic<uint64_t> frames{ 0 }, bytes_in{ 0 };   // (the log's: what it played)
		// (a watcher's frames caught up unseen, for the board to run through unheard as fast as it can, in time while it
		// keeps up (vf2net::UnheardQueue, bounded): after in's first unheard_at bytes; while there are some, put's go
		// after them (heard), so the bytes keep their order)
		vf2net::UnheardQueue unheard = [] { vf2net::UnheardQueue q; q.most_timed = vf2net::UNHEARD_CATCH_UP; return q; }();   // (none let go catching up: vf2_netplay.h)
		size_t unheard_at = 0;
		std::atomic<bool> unheard_on{ false };
		// (a watcher catching up, unseen: the board runs through what it's given unheard, and nothing more, not on to the
		// real clock meanwhile; heard again at the live point, from where those frames left it: Online::watch_hushed)
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
			if (!th.joinable()) return;   // (no board playing: none to run them through)
			std::lock_guard<std::mutex> lk(mx);
			if (!unheard_on) unheard_at = in.size();
			unheard.frame(b);
			unheard_on = true;
		}
		size_t unheard_waiting()   // (frames caught up the board has still to run through: the catch-up waits past UNHEARD_AHEAD)
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
				fprintf(stderr, "online: the match's sound board played %llu frames, %llu bytes from the game; %llu frames caught up let go\n", (unsigned long long)frames.load(), (unsigned long long)bytes_in.load(), (unsigned long long)unheard.dropped);
			}
			quit = false;
			frames = bytes_in = 0;
		}
	} msound;
	auto match_sound_start = [&] {
		msound.stop();
		if (!audio || !sound) return;
		if (!msound.board) {
			msound.board = std::make_unique<model2a>(romset::result(sound_roms));
			msound.board->set_draw_3d(false);
		}
		model2a *bp = msound.board.get();
		model2a &b = *bp;
		b.set_sound(true);
		b.set_sound_only(true);
		b.set_byte_link(true);
		g_match_volumes = pack_volumes(cfg.volume, cfg.music, cfg.effects);
		b.reset();
		{ std::lock_guard<std::mutex> lk(msound.mx); msound.in.clear(); msound.unheard.clear(); msound.unheard_on = false; }
		SDL_ClearAudioStream(audio);
		const int target = int(frame_bytes * QUEUED);
		msound.th = std::thread([&, target, bp, boot = online.adapter.boot_sound] {
			model2a &b = *bp;
			std::vector<uint8_t> bytes;
			uint32_t volumes = ~0u;   // (the volumes it plays at: the Sound page's, as they change mid-match)
			auto take_volumes = [&] {
				const uint32_t v = g_match_volumes;
				if (v == volumes) return;
				volumes = v;
				b.set_volumes(float((v >> 8) & 0xff) / 100.0f, float((v >> 16) & 0xff) / 100.0f);
				b.set_level(LEVEL * float(v & 0xff) / 100.0f);
			};
			take_volumes();
			// (booted as the match's board booted, a frame for each of its, unheard: its program started and set up
			// before the match's first frame, as the arcade's, so a Start pressed at once is heard, the select's
			// music after it)
			for (const std::vector<uint8_t> &f : boot) {
				if (msound.quit) break;
				b.sound_bytes(f.data(), f.size());
				b.run_frame();
				b.m_audio_out.clear();
			}
			while (!msound.quit) {
				if (msound.unheard_on) {   // (a watcher's frames caught up unseen: run through unheard, the bytes put before them first)
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
					continue;
				}
				if (msound.held) { SDL_DelayNS(1000000); continue; }   // (catching up: no sound to the real clock, none sped up)
				if (SDL_GetAudioStreamQueued(audio) >= target) { SDL_DelayNS(1000000); continue; }
				{ std::lock_guard<std::mutex> lk(msound.mx); bytes.swap(msound.in); }
				take_volumes();
				b.sound_bytes(bytes.data(), bytes.size());
				bytes.clear();
				b.run_frame();
				msound.frames++;
				static FILE *wav = [] { const char *w = getenv("VF2_MATCH_SOUND"); return w ? fopen(w, "wb") : nullptr; }();   // (tests: the raw 16-bit stereo it played)
				if (wav && !b.m_audio_out.empty()) { fwrite(b.m_audio_out.data(), 2, b.m_audio_out.size(), wav); fflush(wav); }
				if (!b.m_audio_out.empty()) SDL_PutAudioStreamData(audio, b.m_audio_out.data(), int(b.m_audio_out.size() * sizeof(int16_t)));
				b.m_audio_out.clear();
			}
		});
	};
	online.adapter.run = [&](bool resim) {
		m.set_draw_3d(!resim);   // (a frame run again after a rollback: the board alone, no picture)
		m.run_frame();
		m.set_draw_3d(true);
		if (resim) resimmed = true;
		else sysinfo::note_frame();
	};
	online.adapter.settle = [&] { m.render_wait(); };
	online.adapter.booting = [] { SDL_PumpEvents(); };   // (the boot's seconds: the system doesn't take the game for hung)
	online.adapter.on_begin = [&] {
		finish_async(false);
		ahead = {};
		if (!g_match_board) {   // (this player's own, given back after the match)
			own_backup = m.backup_ram();
			memcpy(own_eeprom, m.eeprom_cells(), sizeof own_eeprom);
			g_match_board = true;
		}
		paused = false;
		shown_pic.reset();
		g_test_frames = 0;
	};
	online.adapter.sound_out = [&](const std::vector<uint8_t> &b) { msound.put(b); };
	// (a watcher's: its sound board started as its first frame runs, not at its first picture, so the frames it catches
	// up unseen run through it unheard, in time (Adapter::sound_unheard): the stage's music on when the picture's up)
	bool watch_sound_started = false;
	online.adapter.sound_begin = [&] { match_sound_start(); watch_sound_started = true; };
	online.adapter.sound_unheard = [&](const std::vector<uint8_t> &b) { msound.put_unheard(b); };
	online.adapter.sound_unheard_waiting = [&] { return msound.unheard_waiting(); };
	// The identity for the handshake: the online version, with vf2net::RANDOM_TAG after it for a random fighter (Online >
	// Settings > Your fighter Random or Seed: such players meet only each other); Normal the plain version, as v0.1.16's
	bool online_ready = false;   // (made: again after Your fighter changes between Normal and a random fighter)
	auto online_identity = [&] {
		if (online_ready) return;   // (the ROMs' hash for the handshake, once; again after the fighter's kind changed)
		finish_async(false);
		const bool random = !cfg.online.fighter.empty();
		online.identity = { "vf2", vf2net::online_version(VF2_VERSION) + (random ? vf2net::RANDOM_TAG : ""), vf2net::content_hash(m), "" };   // (the online version, not the release's)
		// (Random and rooms across Your fighter: the pool, each player's own fighter; and an older build's "+random")
		online.pool = vf2net::pool_version(vf2net::online_version(VF2_VERSION));
		online.accepts = { std::string(vf2net::RANDOM_TAG).substr(1) };
		online_ready = true;
	};
	auto online_page = [&] {
		online_identity();
		cfg.online.seed_name = cfg.random_named[0];   // (the Cheats page's seed name, offered as a fighter)
		cfg.online.seed_ok = [&](const std::string &n) { return m.random_fighter_names().count(n) > 0; };
		return online_menu::page(online, cfg.online, menu, [&] {
			cfg_dirty = true;
			if (cfg.online.fighter.empty() == vf2net::random_version(online.identity.build)) { online_ready = false; online_identity(); }   // (Your fighter: at once)
		});
	};
	int online_at = 5;   // (the main page's Online row, as main_page last made it: where a match's end opens the Online page)
	auto online_row = [&] { return online_at; };
	auto main_page = [&] {
		menu_page p{ "", "", {} };
		if (online.playing() || g_match_board) {   // (the match goes on under the menu; or it's starting, on its board)
			p.items.push_back({ tr::M_RESUME, tr::M_RESUME_MATCH_HELP, {}, {}, [&] { menu.close(); } });
			online_menu::match_rows(online, cfg.online, menu, [&] { cfg_dirty = true; }, p.items);
			p.items.push_back({ tr::P_DISPLAY, tr::M_DISPLAY_HELP, {}, {}, [&] { menu.push(display_page()); } });
			p.items.push_back({ tr::P_SOUND, tr::M_SOUND_HELP, {}, {}, [&] { menu.push(sound_page()); } });
			p.items.push_back({ tr::P_CONTROLS, tr::M_CONTROLS_MATCH_HELP, {}, {}, [&] { menu.push(controls_page()); } });
			return p;
		}
		p.items.push_back({ tr::M_RESUME, tr::M_RESUME_HELP, {}, {}, [&] { menu.close(); } });
		p.items.push_back({ tr::P_DISPLAY, tr::M_DISPLAY_HELP, {}, {}, [&] { menu.push(display_page()); } });
		p.items.push_back({ tr::P_SOUND, tr::M_SOUND_HELP, {}, {}, [&] { menu.push(sound_page()); } });
		p.items.push_back({ tr::P_GAME, tr::M_GAME_HELP, {}, {}, [&] { menu.push(game_page()); } });
		p.items.push_back({ tr::M_ARCADE, tr::M_ARCADE_HELP, {}, {}, [&] { menu.push(arcade_page()); } });
		online_at = int(p.items.size());
		p.items.push_back({ tr::P_ONLINE, tr::M_ONLINE_HELP, {}, {}, [&] { menu.push(online_page()); } });
		p.items.push_back({ tr::P_CONTROLS, tr::M_CONTROLS_HELP, {}, {}, [&] { menu.push(controls_page()); } });
		p.items.push_back({ tr::P_CHEATS, tr::M_CHEATS_HELP, {}, {}, [&] { menu.push(cheats_page()); } });
		p.items.push_back({ tr::M_QUIT, tr::M_QUIT_HELP, {}, {}, [&] { running = false; } });
		return p;
	};
	// the menu pauses the game and its sound: however it opened (Esc, the pad's View, the frames'
	// hotkey) or closed, the sound device follows at once (its queue doesn't drain meanwhile)
	bool sound_paused = false;
	// Android sends these as the app leaves and comes back (WILL_ENTER_BACKGROUND, then
	// DID_ENTER_FOREGROUND): nothing runs while it's away, and the clock is forgotten so the
	// frames it missed aren't run at once on return.
	bool suspended = false;
	auto sync_menu_sound = [&] {
		const bool pause = menu.is_open() && !online.playing();   // (a match goes on, its sound too)
		if (!audio || pause == sound_paused) return;
		sound_paused = pause;
		if (sound_paused) SDL_PauseAudioStreamDevice(audio); else SDL_ResumeAudioStreamDevice(audio);
	};
	auto menu_title = [&] { return std::string(online.watching() ? tr::TITLE_WATCHING : online.playing() ? tr::TITLE_ONLINE_MATCH : tr::TITLE_PAUSED); };
	menus_relabelled = [&] {   // (Game > Language changed: the title, the main page and Game made again in it, Language selected)
		const int row = menu.selected();
		menu.set_title(menu_title());
		menu_page root = main_page();
		int game_row = 0;
		for (size_t i = 0; i < root.items.size(); i++) if (root.items[i].label == tr::P_GAME.c_str()) game_row = int(i);
		menu.open(std::move(root));
		menu.select_row(game_row);
		menu.push(game_page());
		menu.select_row(row);
	};
	// (tests: VF2_TEST_FREECAM_PATH, the free camera flown along a path: free_cam_path.h; at the clock of the picture
	// being set up, fc_path_clock: the hooks' clock, between two frames as far as the picture is)
	freecam::Path fc_path;
	double fc_path_clock = 0;
	if (const char *pf = getenv("VF2_TEST_FREECAM_PATH")) {
		if (!fc_path.load(pf)) fprintf(stderr, "(test) the free camera's path: %s\n", fc_path.error.c_str());
		else {
			fprintf(stderr, "(test) the free camera's path: %zu keys, %zu orbits, %zu events\n", fc_path.keys.size(), fc_path.orbits.size(), fc_path.events.size());
			m.set_freecam_pilot([&](model2_video::freecam_pilot_state &st) { fc_path.fly(st, fc_path_clock); });
		}
	}
	fc_blocked = [&] { return online.playing() && !online.watching(); };
	set_freecam = [&](bool on, bool pad) {
		if (on && fc_blocked()) { menu.toast(tr::FREECAM_NOT_ONLINE); fc_menu = false; return; }
		fc_menu = on;
		if (on == freecam) return;
		freecam = on;
		m.set_freecam(on);
		fc_speed = 1;
		if (on && !fc_path.no_toast) menu.toast(pad ? tr::FREECAM_ON_PAD : tr::FREECAM_ON_KEYS);
		if (paused && !on && shown_pic) redraw_shown();   // (paused: the picture on show from the game's camera again)
		fprintf(stderr, "free camera: %s\n", on ? "on" : "off");
	};
	auto open_menu = [&] {
		fc_menu = freecam;
		touch.release();
		finish_async(false);
		menu.set_title(menu_title());
		if (menu.ready() && !menu.is_open()) menu.open(main_page());
		sync_menu_sound();
	};
	pad_state pstate[2];
	int shot = 0;   // F12's screenshots: the next number to try

	// A step that couldn't run (the other side behind, or keeping in time) takes no input: what was pressed for it
	// goes with the next one tried, so a tap of Start or Coin then isn't lost
	uint16_t step_carry = 0;
	constexpr uint16_t ONE_SHOT = vf2net::START_BUTTON | vf2net::COIN_BUTTON;
	Online::Phase online_was = Online::Phase::off;
	std::string online_code_was;   // (the room code: its page made again when the lobby gives it)
	uint64_t online_refresh_t = 0;
	auto end_match = [&] {   // back to playing on its own, from the boot, with this player's NVRAM and settings
		finish_async(false);
		const bool reopen = match_on || g_match_board;   // (a match, or one that started and never got going)
		if (match_on) fprintf(stderr, "online: the match ended at frame %llu; back to playing alone\n", (unsigned long long)game_frame);
		match_on = false;
		msound.stop();
		{ const std::string none[2]; m.set_hud_names(none); hud_country[0].clear(); hud_country[1].clear(); }
		if (audio) SDL_ClearAudioStream(audio);
		menu.set_notice("");
		menu.set_banner("");
		menu.hide_versus();
		menu.set_net("", 0);
		step_carry = 0;
		if (g_match_board) {
			memcpy(m.backup_ram().data(), own_backup.data(), std::min(own_backup.size(), m.backup_ram().size()));
			memcpy(m.eeprom_cells(), own_eeprom, sizeof own_eeprom);
			g_match_board = false;
		}
		// this player's own again: the sound board running, the culling for their picture's shape, the FREE PLAY
		// notices as their play style has them (the secret character and the ring: boot's)
		m.set_sound(sound);
		m.set_scenery_culling(m.side_columns() ? 2 : 0);
		if (!skip_boot()) m.restore_warning();   // (the match's shortened WARNING: whole again for a boot that's seen)
		apply_play_style();
		menu.set_title(tr::TITLE_PAUSED);
		reset_game();
		// back on the Online page: what ended the match, and the same again a press away
		if (reopen && menu.ready()) {
			if (menu.is_open()) menu.close();
			menu.open(main_page());
			menu.select_row(online_row());
			menu.push(online_page());
		}
		sync_menu_sound();
	};
	// The button this player presses for Start, as their pad or keyboard has it (online, Player 1's controls)
	auto start_button = [&]() -> std::string {
		const int b = cfg.ctl[0].pad[C_START];
		if (pads[0] && b != PAD_NONE) {
			if (b != SDL_GAMEPAD_BUTTON_START) {
				const char *bn = b == PAD_LT ? "LT" : b == PAD_RT ? "RT" : SDL_GetGamepadStringForButton(SDL_GamepadButton(b));
				std::string n = bn ? bn : "START";
				for (char &ch : n) ch = char(toupper(uint8_t(ch)));
				return n;
			}
			const char *nm = SDL_GetGamepadName(pads[0]);
			if (nm && strstr(nm, "Steam Deck")) return "MENU";
			switch (SDL_GetGamepadType(pads[0])) {
			case SDL_GAMEPAD_TYPE_PS3: case SDL_GAMEPAD_TYPE_PS4: case SDL_GAMEPAD_TYPE_PS5: return "OPTIONS";
			case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
			case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return "+";
			case SDL_GAMEPAD_TYPE_XBOXONE: return "MENU";
			default: return "START";
			}
		}
		std::string k = key_name(cfg.ctl[0].key[C_START]);
		for (char &ch : k) ch = char(toupper(uint8_t(ch)));
		return k;
	};
	auto online_pass = [&] {
		online.poll(double(SDL_GetTicksNS()) / 1e6);
		msound.held = online.watch_hushed();
		Online::Notice n;
		while (online.next_notice(n)) {
			if (n.kind == Online::Notice::joined || n.kind == Online::Notice::watching) {
				const bool watching = n.kind == Online::Notice::watching;
				if (cfg.online.chime && !watching) chime::play(chime_stream, win, audio && !hidden ? float(cfg.volume) / 100.0f : 0.0f, coin.get(), coin_sound::RATE);
				match_on = true;
				if (!watching || !watch_sound_started) match_sound_start();   // (a watcher's going since its first frame)
				watch_sound_started = false;
				// the players' names under the energy bars (the names the two sides were sent; a watched match's, as its
				// host sent them)
				for (int p = 0; p < 2; p++) {
					hud_player[p] = online.player_name(p);
					hud_country[p] = online.player_country(p);
				}
				hud_in_shown = -1;   // (set on the HUD below, for the players in the game)
				game_frame = vf2net::BOOT_FRAMES;
				assign_before.reset();   // (the board's arcade settings are the host's now: no restart for them)
				if (menu.is_open()) { menu.close(); menu_closed_elsewhere = true; }
				restart_clock();
				sync_menu_sound();
				// who's playing who, over the title screen for a moment (the host is player 1)
				const std::string host = online.player_name(0), guest = online.player_name(1);
				const int ms = online.ping();
				// (with each one's fighter under the name: Random, a seed's, Cheats' random fighters)
				// (RANDOM and a seed's number in the language on; a seed's own name as it is)
				auto fighter_said = [&](int pl) -> std::string {
					const std::string x = online.player_extra(pl), w = vf2net::fighter_words(x, m);
					if (x == "R") return tr::VS_FIGHTER_RANDOM;
					if (w.rfind("SEED ", 0) == 0) return tr::fill(tr::VS_FIGHTER_SEED, { { "n", w.substr(5) } });
					return w;
				};
				const std::string ft[2] = { fighter_said(0), fighter_said(1) };
				if (watching)
					menu.show_versus({ tr::VS_PLAYER_1, host, tr::VS_PLAYER_2, guest, tr::WATCHING_BEHIND, tr::WATCHING_HOW_TO_STOP, false, online.player_country(0), online.player_country(1), ft[0], ft[1] });
				else
					menu.show_versus({ tr::VS_PLAYER_1_HOST, host, tr::VS_PLAYER_2, guest,
					                   ms > 0 ? tr::fill(online.via_relay() ? tr::VS_RELAY_MS : tr::VS_DIRECT_MS, { { "ms", std::to_string(ms) } }) : std::string(online.via_relay() ? tr::VS_RELAY : tr::VS_DIRECT),
					                   online.slow_local() ? tr::VS_MAC_LOCAL : tr::VS_BOTH_PRESS_START, online.via_relay(),
					                   online.player_country(0), online.player_country(1), ft[0], ft[1] });
			}
			fprintf(stderr, "online: %s\n", Online::log_masked(n.text.en()).c_str());
		}
		if (match_on) {   // (each player's name and flag on the HUD only while they're in the game: a side the CPU has, the game's own)
			const int in = online.players_in_game();
			if (in != hud_in_shown) {
				hud_in_shown = in;
				fprintf(stderr, "online: the HUD's names: %s\n", in == 3 ? "both players'" : in == 1 ? "player 1's (player 2 the CPU's)" : in == 2 ? "player 2's (player 1 the CPU's)" : "the game's own (no one in the game yet)");
				std::string names[2];
				m2hud::Flag f[2];   // (their places only: the host draws them, draw_hud_flags)
				for (int p = 0; p < 2; p++) {
					if (!(in >> p & 1)) continue;
					names[p] = hud_player[p];
					if (flags::index_of(hud_country[p]) >= 0) { f[p].w = flags::W / 4; f[p].h = flags::H / 4; }
				}
				const m2hud::Flag *fl[2] = { &f[0], &f[1] };
				m.set_hud_names(names, fl);
			}
		}
		if ((match_on && !online.playing()) || (g_match_board && !online.playing() && !online.starting())) end_match();   // (ended, left, or never got going)
		static bool said_interrupted = false;
		const bool interrupted = online.playing() && online.interrupted() && (said_interrupted || (online.hold_left() <= 6 && !menu.versus_showing()));
		said_interrupted = interrupted;
		// who's to press Start: at the match's start until both have, and after a match the loser, for a rematch
		int wait = online.playing() && !menu.is_open() && !menu.versus_showing() && !interrupted ? online.start_wait() : 0;
		static uint64_t other_out_since = 0;
		if (wait != 4) other_out_since = 0;
		else if (!other_out_since) other_out_since = SDL_GetTicks();
		else if (SDL_GetTicks() - other_out_since > 8000) wait = 0;
		static int said_wait = 0;
		if (wait != said_wait && online.playing()) {
			static const char *what[5] = { "no one", "this player (the match's start)", "the other (the match's start)", "this player (a rematch)", "the other (a rematch)" };
			fprintf(stderr, "online: to press Start: %s\n", what[wait]);
		}
		said_wait = wait;
		const bool press = wait == 1 || wait == 3;
		// (a room's host, someone in its line: the guest about to make way for them, said for the moment before)
		if (online.making_way()) menu.set_banner(tr::BANNER_NEXT_IN_LINE, tr::fill(tr::BANNER_MAKES_WAY, { { "name", online.opponent() } }));
		else menu.set_banner(press ? std::string(tr::BANNER_PRESS) : wait ? tr::fill(tr::BANNER_WAITING_FOR, { { "name", online.opponent() } }) : "",
		                     wait == 2 ? std::string(tr::BANNER_TO_PRESS_START) : wait == 3 ? std::string(tr::BANNER_FOR_REMATCH) : wait == 4 ? std::string(tr::BANNER_TO_PRESS_REMATCH) : "", press ? start_button() : "");
		if (interrupted)
			menu.set_notice(tr::fill(tr::BANNER_WAITING_FOR, { { "name", online.opponent() } }), tr::fill(tr::NOTICE_DROPPED, { { "n", std::to_string(online.hold_left()) } }),
			                tr::NOTICE_CARRIES_ON);
		else if (online.starting()) menu.set_notice(tr::NOTICE_STARTING, online.status());
		else if (online.watch_between()) menu.set_notice(tr::MATCH_OVER, online.watch_status());   // (a watched match over: not its last picture, still)
		else if (online.watch_between()) menu.set_notice(tr::MATCH_OVER, online.watch_status());   // (a watched match over: not its last picture, still)
		else menu.set_notice("");
		std::string line;
		if (cfg.online.show_connection && online.waiting() && !online.starting())   // (playing alone meanwhile: still waiting, and where)
			line = online.phase() == Online::Phase::hosting && online.by_code() && !online.is_random()
				? (online.room_code().empty() ? std::string(tr::LINE_HOSTING_A_ROOM) : tr::fill(tr::LINE_HOSTING_ROOM, { { "code", online.room_code() } })) + "   " + online.status()
				: online.phase() == Online::Phase::hosting && !online.by_code()
				? tr::fill(tr::LINE_HOSTING_AT, { { "address", online.local_address().empty() ? std::string(tr::LINE_NO_NETWORK) : online.local_address() } }) + "   " + online.status()
				: online.status();
		static game_menu::net_cells cells;
		static uint64_t cells_at = 0;
		static bool had_cells = false;
		if (!online.playing() || (!cfg.online.show_connection && !online.watching()) || menu.versus_showing()) {
			had_cells = false;
			menu.set_net(menu.versus_showing() ? std::string() : line, 0);
		} else if (!had_cells || SDL_GetTicks() - cells_at >= 250 || online.interrupted() != cells.interrupted) {
			had_cells = online.connection(cells, cfg.online.show_connection == 2 && !online.watching());
			cells_at = SDL_GetTicks();
			if (had_cells) menu.set_net(cells);
			else menu.set_net(std::string(), 0);
		}
		// the Online page as things change: its rows made again with the phase, its status lines a few times a second
		if (menu.is_open() && (online.phase() != online_was || online.room_code() != online_code_was) && menu.top_name() == tr::P_ONLINE.c_str() && !menu.capturing())
			menu.replace(online_page(), -1);
		else if (menu.is_open() && online.phase() != online_was && !menu.capturing())
			menu.refresh();
		else if (menu.is_open() && online.playing() && !menu.capturing() && SDL_GetTicks() - online_refresh_t > 500) {
			online_refresh_t = SDL_GetTicks();
			menu.refresh();
		}
		else if (menu.is_open() && online.waiting() && SDL_GetTicks() - online_refresh_t > 250) {
			online_refresh_t = SDL_GetTicks();
			static std::string said;
			if (const std::string now = online.status(); now != said) { said = now; menu.refresh(); }
		}
		else if (menu.is_open() && online.phase() == Online::Phase::off && menu.top_name() == tr::P_ONLINE.c_str() && !menu.capturing())
			// (the Online screen, the room browser: the rooms asked for every 15 s and the page made again as they change,
			// the room selected kept; how many play at random, as the lobby says. Not a match's end, nor a try that failed)
			online_menu::keep_up(online, cfg.online, menu, online_page);
		online_was = online.phase();
		if (!menu.capturing()) online_code_was = online.room_code();
	};

	// ---- test hooks ----
	std::vector<std::pair<SDL_Scancode, uint64_t>> test_keys;   // VF2_KEYS
	if (const char *k = getenv("VF2_KEYS"))
		for (std::string all = std::string(k) + ","; !all.empty(); all.erase(0, all.find(',') + 1)) {
			const std::string t = all.substr(0, all.find(','));
			const size_t at = t.find('@');
			if (at != std::string::npos)
				test_keys.push_back({ SDL_GetScancodeFromName(t.substr(0, at).c_str()), strtoull(t.c_str() + at + 1, nullptr, 10) });
		}
	uint64_t window_shot = 0;   // the frame of a VF2_KEYS press whose window to write, 3 frames on
	std::vector<std::string> menu_keys;   // VF2_MENU_KEYS
	if (const char *k = getenv("VF2_MENU_KEYS"))
		for (std::string t, all = std::string(k) + ","; !all.empty(); all.erase(0, all.find(',') + 1))
			if (!(t = all.substr(0, all.find(','))).empty()) menu_keys.push_back(t);
	// VF2_FLICKER=dir: each new picture on show read back and set against the two before it (pixels
	// that change and change back: a flicker), counted in dir/flicker.txt, the worst saved
	const int test_stage = getenv("VF2_STAGE") ? atoi(getenv("VF2_STAGE")) : -1;
	const char *flicker_dir = getenv("VF2_FLICKER");
	struct { FILE *log = nullptr; std::vector<u32> prev1, prev2; int w = 0, h = 0, n = 0, saved = 0; uint64_t total = 0, pics = 0, worst = 0, save_from = 0, save_min = 200; } flicker;
	if (const char *f = getenv("VF2_FLICKER_MIN")) flicker.save_min = strtoull(f, nullptr, 10);
	if (const char *f = getenv("VF2_FLICKER_FROM")) flicker.save_from = strtoull(f, nullptr, 10);
	if (flicker_dir) flicker.log = fopen((std::string(flicker_dir) + "/flicker.txt").c_str(), "w");
	if (flicker_dir && !flicker.log) flicker_dir = nullptr;
	std::vector<int64_t> shots_at;        // VF2_SHOT_AT
	if (const char *k = getenv("VF2_SHOT_AT"))
		for (std::string all = std::string(k) + ","; !all.empty(); all.erase(0, all.find(',') + 1))
			if (all.find(',') > 0) shots_at.push_back(atoll(all.c_str()));
	// VF2_TEST_VJOY=b3@200,h1@260,+0@300,q@900,...: the virtual stick's button 3, its hat up (1, 2 right, 4 down,
	// 8 left), axis 0 to its + end held from those passes of the loop (8 passes); q: quit then (a run that saves)
	struct vjoy_press { char what; int n; int64_t at; };
	std::vector<vjoy_press> vjoy_presses;
	if (const char *k = getenv("VF2_TEST_VJOY"))
		for (std::string all = std::string(k) + ","; !all.empty(); all.erase(0, all.find(',') + 1)) {
			const std::string t = all.substr(0, all.find(','));
			const size_t at = t.find('@');
			if (t.size() < 2 || at == std::string::npos) continue;
			vjoy_presses.push_back({ t[0], atoi(t.c_str() + 1), atoll(t.c_str() + at + 1) });
		}
	// VF2_PAD_KEYS: virtual pads (SDL's), plugged and pressed at passes
	struct pad_event { int64_t at; int pad; std::vector<std::string> buttons; int hold; };
	std::vector<pad_event> pad_events;
	std::vector<std::pair<SDL_JoystickID, SDL_Joystick *>> vpads;
	std::vector<std::unique_ptr<std::string>> vpad_names;   // (each kept while its pad is plugged in)
	if (const char *k = getenv("VF2_PAD_KEYS"))
		for (std::string all = std::string(k) + ","; !all.empty(); all.erase(0, all.find(',') + 1)) {
			const std::string t = all.substr(0, all.find(','));
			const size_t at = t.find('@');
			if (at == std::string::npos) continue;
			pad_event ev{ atoll(t.c_str() + at + 1), -1, {}, 4 };
			if (const size_t sl = t.find('/', at); sl != std::string::npos) ev.hold = atoi(t.c_str() + sl + 1);
			const std::string what = t.substr(0, at);
			if (what == "plug" || what == "unplug") ev.buttons = { what };
			else if (const size_t c = what.find(':'); c != std::string::npos) {
				ev.pad = atoi(what.c_str()) - 1;
				for (std::string b, rest = what.substr(c + 1) + "+"; !rest.empty(); rest.erase(0, rest.find('+') + 1))
					if (!(b = rest.substr(0, rest.find('+'))).empty()) ev.buttons.push_back(b);
			}
			pad_events.push_back(ev);
		}
	auto pad_hooks = [&](int64_t pass) {
		for (const pad_event &ev : pad_events) {
			if (ev.buttons.size() == 1 && ev.buttons[0] == "plug" && ev.at == pass) {
				SDL_VirtualJoystickDesc d;
				SDL_INIT_INTERFACE(&d);
				d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
				d.naxes = SDL_GAMEPAD_AXIS_COUNT;
				d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
				d.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
				d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
				vpad_names.push_back(std::make_unique<std::string>("Virtual pad " + std::to_string(vpads.size() + 1)));
				const std::string &name = *vpad_names.back();
				d.name = name.c_str();
				d.product_id = Uint16(0x100 + vpads.size());   // (each its own GUID)
				const SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
				vpads.push_back({ id, id ? SDL_OpenJoystick(id) : nullptr });
				fprintf(stderr, "test: %s plugged in at pass %lld\n", name.c_str(), (long long)pass);
			} else if (ev.buttons.size() == 1 && ev.buttons[0] == "unplug" && ev.at == pass && !vpads.empty()) {
				if (vpads.back().second) SDL_CloseJoystick(vpads.back().second);
				SDL_DetachVirtualJoystick(vpads.back().first);
				vpads.pop_back();
				fprintf(stderr, "test: a virtual pad unplugged at pass %lld\n", (long long)pass);
			} else if (ev.pad >= 0 && ev.pad < int(vpads.size()) && vpads[ev.pad].second && (pass == ev.at || pass == ev.at + ev.hold)) {
				const bool down = pass == ev.at;
				for (const std::string &b : ev.buttons) {
					if (b == "lefttrigger" || b == "righttrigger")
						SDL_SetJoystickVirtualAxis(vpads[ev.pad].second, b == "lefttrigger" ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, down ? 32767 : -32768);
					else
						SDL_SetJoystickVirtualButton(vpads[ev.pad].second, SDL_GetGamepadButtonFromString(b.c_str()), down);
				}
			}
		}
	};
	if (std::any_of(pad_events.begin(), pad_events.end(), [](const pad_event &ev) { return ev.at <= 0; })) {
		pad_hooks(0);   // the pads plugged in from the start
		SDL_UpdateJoysticks();
		open_pads();
	}

	bool shown = false;   // the window: shown with its first picture
	// The renderer's textures gone (Direct3D 11/12 and Vulkan after a device reset: every handle
	// still there, the texture behind it not; Direct3D 9's render targets after a reset): every
	// texture let go of and made again, whatever is on show (playing, paused, the menu, a picture
	// drawn on the CPU or the GPU, one being drawn alongside), and the picture on show drawn again
	// as it was. VF2_RESET_AT=pass,...: tests, as if the device had been reset at those passes.
	auto textures_lost = [&](bool device) {
		if (device) {
			crt_paint.lost();
			for (auto &t : flag_tex) if (t) { SDL_DestroyTexture(t); t = nullptr; }   // (made again when next drawn)
		}
		fprintf(stderr, "the renderer's %s reset: its textures made again\n", device ? "device" : "targets");
		if (async_pending) {   // (its texture may be gone: nothing unlocked into it, the picture dropped)
			m.render_wait();
			if (g3.dev) g3.dev->finish();
			async_pending = gpu.pending = async_in_buf = g3.pending = false;
		}
		gpu_pic.lost();
		g3_free();
		gpu.shown = false;
		if (glass) { SDL_DestroyTexture(glass); glass = nullptr; glass_w = glass_h = 0; }
		menu.textures_lost();
		if (!make_texture()) { running = false; return; }
		redraw_shown();
	};
	std::vector<int64_t> resets_at;
	if (const char *r = getenv("VF2_RESET_AT"))
		for (const char *q = r; *q; ) { resets_at.push_back(atoll(q)); while (*q && *q != ',') q++; if (*q) q++; }
	// VF2_PICTURE_LOG=file: a line a pass of the loop (tests: where time goes around scene changes):
	// the pass, game frame, frames run, the picture (new, smooth at a moment, held by scene sync,
	// or the same one again), and ms running, waiting for the drawing, drawing, sending up, putting
	// on screen, the pass in all; the 3D drawn early for a change of its RAMs (deferred pictures);
	// the RAM changes; scene sync's delay; pipelining; the scale; and what else happened (pass_note:
	// textures made, the governor's steps).
	FILE *picture_log = getenv("VF2_PICTURE_LOG") ? fopen(getenv("VF2_PICTURE_LOG"), "w") : nullptr;
	if (picture_log) fprintf(picture_log, "pass frame due picture alpha run wait draw upload blit present pass_ms deferred_ms deferred_n ram_changes delay pipelined scale moment3d notes polygons\n");
	int64_t passes = 0;   // passes of the loop (the test hooks')
	int fps_shown = 0;    // new pictures shown (Show FPS), counted over half a second
	bool hold_on = false; // the last frame run held back by scene sync on purpose (counted as shown)
	uint64_t fps_since = SDL_GetTicksNS();
	apply_perf_hints();
#if defined(__ANDROID__)
	if (cfg.perf_hints && !getenv("VF2_NO_PERF_HINTS")) {
		if (perf_hints_up)
			fprintf(stderr, "performance hints: a session on %d threads, %.2f ms a frame\n", row_pool::get().threads(), 1000.0 / FRAME_HZ);
		else if (sysinfo::perf_hints_available())
			fprintf(stderr, "performance hints: the session wouldn't start\n");
		else
			fprintf(stderr, "performance hints: none this phone has (Android 13 and up)\n");
	}
#endif
	// Fullscreen: F11, Alt+Enter, or F (F only while no player has it as a control; the menu takes
	// keys first, so a key being bound there isn't one)
	auto fullscreen_key = [&](const SDL_KeyboardEvent &k) {
		if (k.scancode == SDL_SCANCODE_RETURN && (k.mod & SDL_KMOD_ALT)) return true;
		if (k.scancode != SDL_SCANCODE_F || (k.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) return false;
		for (const auto &c : cfg.ctl)
			for (int a = 0; a < C_COUNT; a++) if (c.key[a] == SDL_SCANCODE_F || c.key2[a] == SDL_SCANCODE_F) return false;   // (first or second)
		return true;
	};
	// The free camera each pass: flown by the time passed (the keys held, the mouse's moves the events gathered, the
	// pads), View + R3 on any pad turning it on and off, off as an online match is played; the mouse captured while
	// it's flown (not with the menu open, nor the window out of focus). And VF2_TEST_FREECAM's steps.
	float fc_mouse[2] = { 0, 0 };
	uint64_t fc_t = SDL_GetTicksNS(), fc_clock_stopped = 0;
	struct fc_step { uint64_t at; std::string what, arg; };
	std::vector<fc_step> fc_steps;
	if (const char *t = getenv("VF2_TEST_FREECAM")) {
		std::string all = t;
		for (size_t i = 0; i < all.size();) {
			size_t j = all.find(',', i);
			// (a move's own commas: its three numbers)
			std::string item = all.substr(i, j == std::string::npos ? std::string::npos : j - i);
			if (item.rfind("move@", 0) == 0) for (int k = 0; k < 2 && j != std::string::npos; k++) j = all.find(',', j + 1);
			item = all.substr(i, j == std::string::npos ? std::string::npos : j - i);
			const size_t at = item.find('@'), eq = item.find('=');
			if (at != std::string::npos)
				fc_steps.push_back({ uint64_t(atoll(item.c_str() + at + 1)), item.substr(0, at), eq == std::string::npos ? "" : item.substr(eq + 1) });
			if (j == std::string::npos) break;
			i = j + 1;
		}
	}
	auto freecam_pass = [&] {
		const uint64_t now = SDL_GetTicksNS();
		const double dt = std::clamp(double(now - fc_t) / 1e9, 0.0, 0.1);
		fc_t = now;
		static bool combo_was[2], y_was[2], lb_was[2], rb_was[2];
		for (int p = 0; p < 2; p++) {
			SDL_Gamepad *g = pads[p];
			const bool combo = g && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_BACK) && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
			if (combo && !combo_was[p] && !menu.is_open()) set_freecam(!freecam, true);
			combo_was[p] = combo;
		}
		if (freecam && fc_blocked()) set_freecam(false, false);   // (an online match being played: off)
		const bool fly = freecam && !menu.is_open() && !suspended;
		const bool focus = !hidden && SDL_GetKeyboardFocus() == win;
		if (SDL_GetWindowRelativeMouseMode(win) != (fly && focus)) SDL_SetWindowRelativeMouseMode(win, fly && focus);
		if (fly) {
			constexpr double MOUSE = 0.12, ARROWS = 90, STICK = 120;   // (degrees a pixel; a second)
			double fwd = 0, right = 0, up = 0, mul = 1;
			double yaw = fc_mouse[0] * MOUSE, pitch = -fc_mouse[1] * MOUSE;
			if (focus) {
				const bool *ks = SDL_GetKeyboardState(nullptr);
				fwd += ks[SDL_SCANCODE_W] - ks[SDL_SCANCODE_S];
				right += ks[SDL_SCANCODE_D] - ks[SDL_SCANCODE_A];
				up += (ks[SDL_SCANCODE_E] || ks[SDL_SCANCODE_SPACE]) - (ks[SDL_SCANCODE_Q] || ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL]);
				if (ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT]) mul *= 4;
				if (ks[SDL_SCANCODE_LALT] || ks[SDL_SCANCODE_RALT]) mul *= 0.25;
				yaw += ARROWS * dt * (ks[SDL_SCANCODE_RIGHT] - ks[SDL_SCANCODE_LEFT]);
				pitch += ARROWS * dt * (ks[SDL_SCANCODE_UP] - ks[SDL_SCANCODE_DOWN]);
			}
			for (int p = 0; p < 2; p++) {
				SDL_Gamepad *g = pads[p];
				if (!g || (p == 1 && pads[1] == pads[0])) continue;
				auto axis = [&](SDL_GamepadAxis a) {   // (a dead zone of 0.15, then squared)
					const double v = std::clamp(SDL_GetGamepadAxis(g, a) / 32767.0, -1.0, 1.0), m = (std::fabs(v) - 0.15) / 0.85;
					return m <= 0 ? 0.0 : std::copysign(m * m, v);
				};
				auto btn = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(g, b); };
				fwd -= axis(SDL_GAMEPAD_AXIS_LEFTY);
				right += axis(SDL_GAMEPAD_AXIS_LEFTX);
				up += axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) - axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
				yaw += STICK * dt * axis(SDL_GAMEPAD_AXIS_RIGHTX);
				pitch -= STICK * dt * axis(SDL_GAMEPAD_AXIS_RIGHTY);
				if (btn(SDL_GAMEPAD_BUTTON_LEFT_STICK)) mul *= 4;
				const bool lb = btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), rb = btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), y = btn(SDL_GAMEPAD_BUTTON_NORTH);
				if (lb && !lb_was[p]) fc_speed = std::max(fc_speed / 1.25, 1.0 / 16);
				if (rb && !rb_was[p]) fc_speed = std::min(fc_speed * 1.25, 16.0);
				if (y && !y_was[p]) m.freecam_reset();
				lb_was[p] = lb; rb_was[p] = rb; y_was[p] = y;
			}
			fwd = std::clamp(fwd, -1.0, 1.0); right = std::clamp(right, -1.0, 1.0); up = std::clamp(up, -1.0, 1.0);
			// (a ring's width in about 3 s at the speed's start)
			const double step = m.freecam_unit() / 3 * fc_speed * mul * dt;
			if (fwd || right || up || yaw || pitch) m.freecam_fly(float(fwd * step), float(right * step), float(up * step), float(yaw), float(pitch));
		}
		fc_mouse[0] = fc_mouse[1] = 0;
		// (tests)
		const bool stopped_now = suspended || (!online.playing() && (paused || menu.is_open() || online.starting()));
		if (stopped_now) fc_clock_stopped++;
		const uint64_t clock = game_frame + fc_clock_stopped;
		if (fc_path.loaded) {
			m.freecam_fighters();   // (where the fighters are, for the path's picture: read only)
			for (freecam::Path::Event &e : fc_path.events) {
				if (e.done || double(clock) < e.t) continue;
				e.done = true;
				fprintf(stderr, "(test) the free camera's path: %s at %llu (frame %llu)\n", e.what.c_str(), (unsigned long long)clock, (unsigned long long)game_frame);
				if (e.what == "on") set_freecam(true, false);
				else if (e.what == "off") set_freecam(false, false);
				else if (e.what == "pause") paused = true;
				else if (e.what == "unpause") paused = false;
				else if (e.what == "quit") running = false;
			}
		}
		while (!fc_steps.empty() && clock >= fc_steps.front().at) {
			const fc_step st = fc_steps.front();
			fc_steps.erase(fc_steps.begin());
			const double v = atof(st.arg.c_str()), u = m.freecam_unit();
			fprintf(stderr, "test: free camera: %s %s at frame %llu (clock %llu)\n", st.what.c_str(), st.arg.c_str(), (unsigned long long)game_frame, (unsigned long long)clock);
			if (st.what == "on") set_freecam(true, false);
			else if (st.what == "off") set_freecam(false, false);
			else if (st.what == "reset") m.freecam_reset();
			else if (st.what == "yaw") m.freecam_fly(0, 0, 0, float(v), 0);
			else if (st.what == "pitch") m.freecam_fly(0, 0, 0, 0, float(v));
			else if (st.what == "move") {
				double f = 0, r = 0, up = 0;
				sscanf(st.arg.c_str(), "%lf,%lf,%lf", &f, &r, &up);
				m.freecam_fly(float(f * u), float(r * u), float(up * u), 0, 0);
			} else if (st.what == "pause") paused = true;
			else if (st.what == "resume") paused = false;
			else if (st.what == "shot") {
				std::vector<uint32_t> px;
				int w = 0, h = 0;
				shown_pixels(px, w, h);
				write_png(("build/host_fc_" + st.arg + ".png").c_str(), px.data(), w, h);
			}
		}
	};
	while (running) {
		const int64_t pass = passes++;
		marker_done(false);
		// (tests: a room hosted, or joined with its code, or a random opponent looked for, as the menu would, once the
		// game's past its start)
		if ((arg_host || !arg_join.empty()) && pass == 60) {   // (--host, --join: by address, the Online screen showing it)
			online_page();   // (the identity, as opening the Online page sets it)
			OnlineSettings os = cfg.online;
			if (os.name.empty()) os.name = "PLAYER";   // (no name asked for: the command line's)
			if (arg_port) os.port = arg_port;
			if (!arg_join.empty()) os.address = cfg.online.address = online_menu::clean_address(arg_join);   // (the Online screen says where)
			tr::line err;
			if (!(arg_host ? online.host(os, err) : online.join(os, err))) fprintf(stderr, "online: %s: %s\n", arg_host ? "--host" : "--join", err.en().c_str());
			else fprintf(stderr, "online: %s from the command line\n", arg_host ? "hosting by address" : ("joining " + os.address).c_str());
			if (menu.ready()) {
				if (menu.is_open()) menu.close();
				menu.open(main_page());
				menu.select_row(online_row());
				menu.push(online_page());
			}
		}
		static const char *test_host_room = getenv("VF2_HOST_ROOM"), *test_join_code = getenv("VF2_JOIN_CODE"), *test_random = getenv("VF2_RANDOM");
		// a link (the command line, a Mac's open-URL event, or handed over by another copy): once the game's up, into
		// the room or the match, as Online > Enter code (only the link's own form: links::parse)
		if (pass % 60 == 0) { std::string handed; if (links::take(data, handed)) arg_link = handed; }
		if (!arg_link.empty() && pass >= 60 && menu.ready()) {
			links::Link l;
			const std::string url = arg_link;
			arg_link.clear();
			if (!links::parse(url, l)) fprintf(stderr, "links: not a link this game takes: %s\n", Online::log_masked(url.substr(0, 40)).c_str());
			else if (const tr::line no = online_menu::link_refused(online); !no.empty()) { menu.toast(no.str()); fprintf(stderr, "links: %s %s: not now (%s)\n", l.action.c_str(), l.code.c_str(), no.en().c_str()); }
			else {
				online_page();   // (the identity, as opening the Online page sets it)
				tr::line err;
				const bool ok = online_menu::follow_link(online, cfg.online, menu, [&] { cfg_dirty = true; }, l.action, l.code, [&] {
					if (menu.is_open()) menu.close();
					menu.open(main_page());
					menu.select_row(online_row());
					menu.push(online_page());
				}, err);
				fprintf(stderr, "links: %s %s%s%s\n", l.action.c_str(), l.code.c_str(), ok ? "" : (": " + err.en()).c_str(), ok && cfg.online.name.empty() && l.action != "watch" ? " (a name asked for first)" : "");
				SDL_RaiseWindow(win);
			}
		}
		static const char *test_watch = getenv("VF2_WATCH_CODE");
		if ((test_host_room || test_join_code || test_random || test_watch) && pass == 60) {
			online_page();   // (the identity, as opening the Online page sets it)
			tr::line err;
			if (getenv("VF2_HOST_PUBLIC")) cfg.online.room_who = 2;
			if (!(test_watch ? online.watch(cfg.online, test_watch, err) : test_random ? online.find_random(cfg.online, err)
			      : test_join_code ? online.join_room(cfg.online, test_join_code, err) : online.host_room(cfg.online, err)))
				fprintf(stderr, "online: (test) %s\n", err.en().c_str());
		}
		online_pass();
		const pace_window pace0 = pace;
		bool pass_cut = false;
		uint64_t pass_work = 0, pass_starved = 0;
		const uint64_t pass_t0 = SDL_GetTicksNS();
		pass_note = gov_note;   // (the governor's steps come at the end of a pass: told with the next)
		gov_note.clear();
		for (auto &[key, at] : test_keys)
			if (at && game_frame >= at) {
				SDL_Event k{};
				k.type = SDL_EVENT_KEY_DOWN;
				k.key.scancode = key;
				k.key.down = true;
				SDL_PushEvent(&k);
				window_shot = at;
				at = 0;
			}
		if (pass >= 30 && (pass - 30) % 6 == 0 && size_t((pass - 30) / 6) < menu_keys.size()) {
			static const std::map<std::string, SDL_Scancode> names = {
				{ "esc", SDL_SCANCODE_ESCAPE }, { "up", SDL_SCANCODE_UP }, { "down", SDL_SCANCODE_DOWN },
				{ "left", SDL_SCANCODE_LEFT }, { "right", SDL_SCANCODE_RIGHT }, { "enter", SDL_SCANCODE_RETURN } };
			const std::string &name = menu_keys[(pass - 30) / 6];
			auto it = names.find(name);
			const SDL_Scancode sc = it != names.end() ? it->second : SDL_GetScancodeFromName(name.c_str());
			if (sc != SDL_SCANCODE_UNKNOWN) {
				SDL_Event k{};
				k.type = SDL_EVENT_KEY_DOWN;
				k.key.scancode = sc;
				k.key.down = true;
				SDL_PushEvent(&k);
			}
		}
		if (pass > 0) pad_hooks(pass);
		for (const auto &t : vjoy_presses) {   // VF2_TEST_VJOY: each press held 8 passes
			const bool on = pass >= t.at && pass < t.at + 8;
			if (pass != t.at && pass != t.at + 8) continue;
			if (!test_vjoy && t.what != 'p' && t.what != 'q') continue;
			if (t.what == 'b') SDL_SetJoystickVirtualButton(test_vjoy, t.n, on);
			else if (t.what == 'h') SDL_SetJoystickVirtualHat(test_vjoy, 0, on ? Uint8(t.n) : SDL_HAT_CENTERED);
			else if (t.what == '+' || t.what == '-') SDL_SetJoystickVirtualAxis(test_vjoy, t.n, on ? (t.what == '+' ? 32767 : -32768) : 0);
			else if (t.what == 'q' && on) { SDL_Event q{}; q.type = SDL_EVENT_QUIT; SDL_PushEvent(&q); }
			else if (t.what == 'p' && on && !test_vjoy) attach_vjoy();
			if (on) fprintf(stderr, "test: the virtual stick: %c%d at pass %lld\n", t.what, t.n, (long long)pass);
		}
		for (int64_t at : resets_at)
			if (at == pass) { SDL_Event r{}; r.type = SDL_EVENT_RENDER_DEVICE_RESET; SDL_PushEvent(&r); }
		if (static const char *rs = getenv("VF2_TEST_RESIZE"); rs) {   // (tests: the window resized as by hand, "WxH@pass", "WxH+X+Y@pass")
			int rw = 0, rh = 0, rx = 0, ry = 0;
			long long at = 0;
			if (sscanf(rs, "%dx%d+%d+%d@%lld", &rw, &rh, &rx, &ry, &at) == 5 && pass == at) { SDL_SetWindowSize(win, rw, rh); SDL_SetWindowPosition(win, rx, ry); SDL_SyncWindow(win); }
			else if (sscanf(rs, "%dx%d@%lld", &rw, &rh, &at) == 3 && pass == at) { SDL_SetWindowSize(win, rw, rh); SDL_SyncWindow(win); }
		}
		if (getenv("VF2_TEST_WINDOW_LOG") && pass % 20 == 0) {   // (tests: the window's pixels and Window size)
			int pw = 0, ph = 0, x = 0, y = 0, lw = 0, lh = 0;
			SDL_RendererLogicalPresentation mode;
			SDL_GetWindowSizeInPixels(win, &pw, &ph);
			SDL_GetWindowPosition(win, &x, &y);
			SDL_GetRenderLogicalPresentation(ren, &lw, &lh, &mode);
			fprintf(stderr, "test: pass %lld: the window %d x %d pixels at %d, %d, window_scale %d, the picture %d x %d\n",
			        (long long)pass, pw, ph, x, y, cfg.window_scale, lw, lh);
		}
		if (window_rescale) { window_rescale = false; apply_window_scale(); menu.refresh(); }   // (the last pass's events asked)
		if (window_moved) { window_moved = false; remember_window(); }
		SDL_Event e;
		const bool menu_was_open = menu.is_open();
		while (SDL_PollEvent(&e)) {
			// (a Mac's open-URL event, as SDL gives it: a link from the web, the game running or just started by it)
			if (e.type == SDL_EVENT_DROP_FILE && e.drop.data && links::is_link(e.drop.data)) arg_link = std::string(e.drop.data).substr(0, 64);
			if (e.type == SDL_EVENT_JOYSTICK_ADDED) give_unknown_sticks();   // (a gamepad's added event follows for one given a layout)
			if (e.type == SDL_EVENT_WINDOW_MOVED || e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_MAXIMIZED ||
				e.type == SDL_EVENT_WINDOW_RESTORED)
				window_moved = true;
			// (fullscreen by the system's own way, a Mac's green button: remembered as the menu's is)
			if ((e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN || e.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) && !fs_win_on &&
				cfg.fullscreen != (e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN)) {
				cfg.fullscreen = e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
				cfg_dirty = true;
			}
			if (e.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN && cfg.window_scale > 0) window_rescale = true;
			// Window size: the window resized by hand (a corner dragged, maximised): Free again
			if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && cfg.window_scale > 0 && !window_rescale && !is_fullscreen() &&
				(e.window.data1 != win_expect_w || e.window.data2 != win_expect_h)) {
				int pw = 0, ph = 0;
				SDL_GetWindowSize(win, &pw, &ph);
				if (pw == win_set_w && ph == win_set_h) window_rescale = true;   // (the same points: another density)
				else {
					fprintf(stderr, "window size: resized to %d x %d pixels: Free\n", e.window.data1, e.window.data2);
					cfg.window_scale = 0;
					cfg_dirty = true;
					menu.refresh();
				}
			}
			if (e.type == SDL_EVENT_QUIT) running = false;
			else if (e.type == SDL_EVENT_WILL_ENTER_BACKGROUND) {   // (Android: nothing runs in the background)
				suspended = true;
				touch.release();   // (a lift may not come: the app is going away)
				if (audio && !sound_paused) SDL_PauseAudioStreamDevice(audio);
			}
			else if (e.type == SDL_EVENT_DID_ENTER_FOREGROUND) {
				suspended = false;
				restart_clock();
				if (audio && !sound_paused) SDL_ResumeAudioStreamDevice(audio);
			}
			else if (e.type == SDL_EVENT_RENDER_DEVICE_RESET || e.type == SDL_EVENT_RENDER_TARGETS_RESET) textures_lost(e.type == SDL_EVENT_RENDER_DEVICE_RESET);
			else if (e.type == SDL_EVENT_RENDER_DEVICE_LOST) { fprintf(stderr, "the graphics device was lost: closing\n"); running = false; }
			else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_AC_BACK && !e.key.repeat) {
				// Android's Back: the menu, not the app (before the menu, which takes every key);
				// a page up at a time, like Esc (and the on-screen controls' Menu button opens it)
				if (menu.is_open()) menu.pop(); else open_menu();
			}
			// (F11 and Alt+Enter with the menu open too, unless it's waiting for a key: Alt+Enter was its Enter)
			else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && menu.is_open() && !menu.capturing()
					 && (e.key.scancode == SDL_SCANCODE_F11 || (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT))))
				set_fullscreen(!is_fullscreen());
			else if (menu.handle_event(e)) continue;
			else if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION ||
					 e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) {
				touch_present = true;
				if (touch_shown())
					touch.finger(ren, win, e.tfinger, e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION);
			}
			else if (e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_GAMEPAD_REMOVED) {
				open_pads();
				menu.refresh();
				if (!touch_shown()) touch.release();   // (Auto: a pad arriving takes them away, fingers and all)
			}
			else if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && !aspect_i && apply_aspect()) reshape();
			else if (e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN || e.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
				menu.refresh();
				if (SDL_GetKeyboardFocus() != win) SDL_RaiseWindow(win);   // (the move to a new Space can lose it)
			}
			else if (e.type == SDL_EVENT_MOUSE_MOTION && freecam && SDL_GetWindowRelativeMouseMode(win)) { fc_mouse[0] += e.motion.xrel; fc_mouse[1] += e.motion.yrel; }
			else if (e.type == SDL_EVENT_MOUSE_WHEEL && freecam) fc_speed = std::clamp(fc_speed * std::pow(1.25, double(e.wheel.y)), 1.0 / 16, 16.0);
			else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
				if (e.key.scancode == SDL_SCANCODE_ESCAPE) open_menu();
				else if (e.key.scancode == SDL_SCANCODE_F4) set_freecam(!freecam, false);
				else if (e.key.scancode == SDL_SCANCODE_R && freecam) m.freecam_reset();
				else if (e.key.scancode == SDL_SCANCODE_P && !online.playing()) paused = !paused;   // (a match never pauses)
				else if (e.key.scancode == SDL_SCANCODE_F9) {
					timings = !timings; cfg_dirty = true;
					if (!timings) SDL_SetWindowTitle(win, title_base);
				}
				else if (e.key.scancode == SDL_SCANCODE_F3 && !online.playing()) reset_game();   // (never in a match)
				else if (e.key.scancode == SDL_SCANCODE_F5) set_scale(scale_wanted % MAX_SCALE + 1);
				else if (e.key.scancode == SDL_SCANCODE_F6) set_shape((aspect_i + 1) % N_ASPECTS);
				else if (e.key.scancode == SDL_SCANCODE_F11 || fullscreen_key(e.key)) set_fullscreen(!is_fullscreen());
				else if (e.key.scancode == SDL_SCANCODE_F12) {
					// F12: the picture as drawn, into Screenshots in the NVRAM folder (numbered, never overwritten)
					const std::string shots = data + "/Screenshots";
					std::filesystem::create_directories(shots, ec);
					char name[64];
					do snprintf(name, sizeof name, "/vf2_%04d.png", ++shot);
					while (std::filesystem::exists(shots + name, ec) && shot < 9999);
					std::vector<uint32_t> px;
					int w = 0, h = 0;
					shown_pixels(px, w, h);
					write_png((shots + name).c_str(), px.data(), w, h);
				}
			}
		}
		setup_poll();
		sync_menu_sound();
		freecam_pass();
		if ((menu.is_open() != menu_was_open && !menu.is_open()) || std::exchange(menu_closed_elsewhere, false)) {
			if (fc_menu != freecam) set_freecam(fc_menu, false);   // (Display > Free camera's choice)
			// (not a match just left from the menu: Leave closes it before end_match has shown MATCH OVER, which it then didn't)
			if (online.phase() == Online::Phase::off && !match_on) { online.set_last_match({}); online.forget_failure(); }   // (MATCH OVER, COULDN'T CONNECT: just after, not for ever)
			log_state("the menu closed");
			// settings changed in the menu are saved as it closes (not only at quit: a crash or a
			// force quit would lose them); test runs save nothing
			if (cfg_dirty && (quit_after < 0 || test_saves)) {
				if (save_settings()) cfg_dirty = false;   // (else tried again within 3 s)
			}
			// the arcade settings are read as the game starts: restart it if one is different
			if (assign_before) {
				const auto now = assignments_now();
				const bool changed = *assign_before != now, cold = (*assign_before)[N_ASSIGN + 4] != now[N_ASSIGN + 4];
				assign_before.reset();
				if (cold) cold_start();   // (Debug mode: a start's game, exactly)
				if (changed) reset_game();
			}
			pstate[0] = pstate[1] = pad_state{};   // the button that closed it isn't a move
			pstate[0].back_was = pstate[1].back_was = true;
			pstate[0].back_combo = pstate[1].back_combo = true;
			pstate[0].start_was = pstate[1].start_was = true;
			pstate[0].start_quiet = pstate[1].start_quiet = true;   // (a Start key or button held as it closed)
			for (pad_state &s : pstate) s.menu_was = s.touch_was = s.sticks_was = true;
			restart_clock();
		}
		// (a match goes on under the menu; one being set up holds the board, which is the match's now;
		// a phone's app in the background stops it all, the clock forgotten on return)
		const bool stopped = suspended || (!online.playing() && (paused || menu.is_open() || online.starting()));
		int due = 0;
		bool draw_into_texture = false;   // the newest frame, drawn straight into the texture after the frames
		bool held = false;                // the newest frame held back by scene sync
		const bool ff = !stopped && !online.playing() && SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_TAB];   // Tab held: flat out (never in a match)
		const uint64_t work_t0 = SDL_GetTicksNS();
		bool sound_full = false;   // over 300 ms queued: no frames run this pass
		if (stopped) restart_clock();
		else if (!rec.dir.empty()) {
			// the recording: before it starts, frames as fast as they go (no pictures); then 60 pictures a second by the
			// game's clock
			if (!rec.on && game_frame >= rec.from) {
				rec.on = true;
				rec.from = game_frame;
				rec.base = frames_run;
				int ww = 0, wh = 0;
				SDL_GetCurrentRenderOutputSize(ren, &ww, &wh);
				rec.w = ww & ~1; rec.h = wh & ~1;
				const std::string cmd = "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s " + std::to_string(rec.w) + "x" + std::to_string(rec.h) +
				                        " -r " + std::to_string(rec.hz) + " -i - -c:v libx264 -preset slow -crf 12 -pix_fmt yuv420p '" + rec.dir + "/video.mp4'";
				rec.video = popen(cmd.c_str(), "w");
				rec.wav = fopen((rec.dir + "/sound.wav").c_str(), "wb");
				if (rec.wav) wav_header();
				fprintf(stderr, "record: from frame %llu, %dx%d\n", (unsigned long long)rec.from, rec.w, rec.h);
			}
			if (!rec.on) { due = 8; frame_clock = double(frames_run + due); }
			else {
				frame_clock = double(rec.base) + double(rec.n - rec.stopped_n + 1) * FRAME_HZ / double(rec.hz);
				due = std::max(0, int(int64_t(std::floor(frame_clock)) - int64_t(frames_run)));
			}
			frames_run += due;
		}
		else {
			// how many frames to run now
			const uint64_t now_ns = SDL_GetTicksNS();
			const double dt = double(now_ns - clock_t) / 1e9;
			clock_t = now_ns;
			double rate = FRAME_HZ;
			int low = 0;   // frames the sound queue needs at once
			if (audio && m.sound() && !online.playing()) {   // (a match's sound: its own thread's, to the real clock)
				const int q = SDL_GetAudioStreamQueued(audio);
				// a sound device that stopped taking sound without going away, or takes it slower than
				// the 1% trim can follow (Wine's on a Mac: about 5%): the queue would grow without end,
				// the sound ever further behind the picture. Over 300 ms queued the game waits for the
				// sound, so the two stay together (as vf1-recomp)
				if (q > frame_bytes * int(FRAME_HZ * 0.3)) {
					static int told = 0;
					if (told++ == 0) fprintf(stderr, "sound: %d ms queued (the device is slow or stalled): the game waits for it\n", int(q * 1000.0 / (frame_bytes * FRAME_HZ)));
					sound_full = true;
				}
				queue_avg += (q - queue_avg) * std::min(1.0, dt / 0.5);
				rate *= 1 - std::clamp((queue_avg / frame_bytes - QUEUED) * 0.005, -0.01, 0.01);
				if (q < frame_bytes * 3 / 2) low = int(std::ceil(QUEUED - double(q) / frame_bytes));
			}
			rate *= 1 - online.slow_down();   // (a match: ahead of the other side, a little slower till it's caught up)
			frame_clock += dt * rate;
			due = int(int64_t(std::floor(frame_clock)) - int64_t(frames_run));
			if (due > 8) due = 1, frame_clock = double(frames_run + 1);    // fell behind: don't race
			due = std::max(due, low);
			if (due > 0) frame_clock = std::max(frame_clock, double(frames_run + due));
			due = std::max(due, 0);
			// After a long pass (a stall, a scene change's work), not a burst of frames back to back
			// to win the time back: at most two a pass, unless the sound needs more now (low); the
			// clock slips the rest, and the pictures carry on at the screen's rate from where it is
			if (!ff && due > 2 && due > low) {
				const int keep = std::max(2, low);
				frame_clock -= due - keep;
				due = keep;
				pass_note += "slip,";
			}
			if (ff) { due = 8; frame_clock = double(frames_run + due); }
			else if (sound_full) { due = 0; frame_clock = std::min(frame_clock, double(frames_run) + 1); }   // (the clock held at the next frame)
			frames_run += due;
		}
		// a frame run on the board: here, or on the game's thread during the present (see emu_job)
		auto run_one = [&](bool last) {
			m.set_draw_3d(last || sync.enabled);
			if (test_stage >= 0 && (m.game_mode() == 7 || m.game_mode() == 8))   // VF2_STAGE (tests: the fight's stage)
				const_cast<std::vector<u8> &>(m.workram())[0x64] = u8(test_stage);
			if (!g_match_board) stage_select(m, cfg.stage);   // Arcade settings > Stage select (offline only)
			m.run_frame();
			sysinfo::note_frame();   // (on the thread that ran it)
			m.set_draw_3d(true);
		};
		// what follows a frame, here: scene sync, the picture to draw (into, held_now), the next frame's
		// inputs, its sound, the hotkeys (false: no more frames this pass, the menu opened or a restart)
		auto frame_done = [&](bool last, bool &into, bool &held_now) -> bool {
			if (test_ko.player && game_frame >= test_ko.from && game_frame <= test_ko.to && m.workram()[0x2b] == 9 && m.workram()[0x31] == 9) {
				const u32 block = test_ko.player == 1 ? 0x510980 : 0x512980;   // (as vf2run's --ko)
				if (int16_t(m.peek32(block + 0x1ac)) > 0 && !(m.peek32(block) & 0x20)) {
					m.write_word(block + 0x1ac, 0);
					m.write_dword(block, m.peek32(block) | 0x20);
					pass_note += "ko,";
				}
			}
			if (m.newest_3d_cut()) pass_cut = true;
			const bool show = sync.show(m);   // (every frame: it follows the scene)
			if (show && !last_shown) m.smooth_cut();   // (a hold over: the new scene isn't blended from the old)
			last_shown = show;
			held_now = last && !show;
			hold_on = held_now;
			if (last && script.shots.count(game_frame)) {   // a shot to write: as a bitmap
				const uint64_t td = SDL_GetTicksNS();
				show_drawn(m.render(true));
				pace.draw += SDL_GetTicksNS() - td;
				into = false;
			} else if (last) {
				into = show;
				// (held: a picture made meanwhile, at a new size, still goes up; pipelined too, as this
				// frame's goes up only at the next pass: else the new size's texture, still empty, shows)
				if (show && !pipelined) new_picture = false;
			}
			load.frames++;
			// the inputs the game reads during the next frame: the script's, then the player's
			uint8_t ports[input_script::N_PORTS];
			script.apply(game_frame, ports);
			uint8_t live[3];
			const hotkey hk = read_inputs(m, pads, pstate, cfg.ctl, live, touch_shown() ? &touch : nullptr);
			if (freecam) live[0] = live[1] = live[2] = 0;   // (the free camera's: the players' own inputs kept from the game)
			m.m_in0 = ports[input_script::IN0] & ~live[0];
			m.m_in1 = ports[input_script::IN1] & ~live[1];
			m.m_in2 = ports[input_script::IN2] & ~live[2];
			m.m_sw = ports[input_script::SW];
			auto [shot_first, shot_last] = script.shots.equal_range(game_frame);
			for (auto it = shot_first; it != shot_last; ++it)
				write_png(("build/host_" + it->second + ".png").c_str(), picture->data(), picture->width(), picture->height());
			if (rec.wav && rec.on && !m.m_audio_out.empty()) {   // (a recording's: to its file)
				fwrite(m.m_audio_out.data(), sizeof(int16_t), m.m_audio_out.size(), rec.wav);
				rec.wav_bytes += uint32_t(m.m_audio_out.size() * sizeof(int16_t));
			}
			if (audio && !ff && !m.m_audio_out.empty() && rec.dir.empty()) {
				SDL_PutAudioStreamData(audio, m.m_audio_out.data(), int(m.m_audio_out.size() * sizeof(int16_t)));
			}
			m.m_audio_out.clear();
			game_frame++;
			// (tests: the game's work RAM hashed every 30 frames, with the polygon counts it read, VF2_TEST_RAM_LOG=file: the game the same whatever the
			// display's settings, Display > HUD's, V-Sync's, the frame limit's)
			if (static FILE *rl = getenv("VF2_TEST_RAM_LOG") ? fopen(getenv("VF2_TEST_RAM_LOG"), "w") : nullptr; rl && game_frame % 30 == 0) {
				fprintf(rl, "%llu %016llx %016llx\n", (unsigned long long)game_frame, (unsigned long long)fnv(m.workram().data(), m.workram().size()),
					(unsigned long long)m.video().m_polygon_count_sum);   // (and every polygon count the game read from the 3D so far)
				fflush(rl);
			}
			if (pace_log && game_frame % 60 == 0)
				fprintf(stderr, "frame %llu at %.3f s, queued %d bytes, made %zu\n", (unsigned long long)game_frame, double(SDL_GetTicksNS() - start_ns) / 1e9, audio ? SDL_GetAudioStreamQueued(audio) : -1, m.m_audio_out.size());
			if (quit_after >= 0 && int64_t(game_frame) >= quit_after) running = false;
			switch (hk) {
			case hotkey::menu: open_menu(); break;
			case hotkey::scale: set_scale(scale_wanted % MAX_SCALE + 1); break;   // (as F5: 1x to 6x)
			case hotkey::aspect: set_shape((aspect_i + 1) % N_ASPECTS); break;
			case hotkey::none: break;
			}
			// leaving the service menu: the PC way restarts at once, past the boot
			if (g_left_test_menu) {
				g_left_test_menu = false;
				if (g_pc_mode) { reset_game(); into = held_now = false; return false; }
				m.set_ring_width(cfg.ring);   // (the test mode sets the ring by the difficulty)
			}
			if (menu.is_open()) { into = show; held_now = !show; return false; }   // (this frame is the one on show)
			return true;
		};
		m.set_debug_mode(cfg.debug_mode && !online.playing() && !g_match_board);   // (Arcade settings > Debug mode: offline only)
		if (online.playing()) {
			// A match: each frame through the session, with this player's input (either player's controls play this
			// player's fighter; nothing while the menu's open); a frame that can't run yet (the other side behind, or
			// keeping in time) stays due, and the picture stays
			for (int i = 0; i < due && running && online.playing(); i++) {
				uint8_t ports[input_script::N_PORTS];
				script.apply(game_frame, ports);   // (tests: an input script's players as this player's)
				uint8_t live[3];
				const hotkey hk = read_inputs(m, pads, pstate, cfg.ctl, live, touch_shown() ? &touch : nullptr);
				const uint8_t in0 = uint8_t(ports[input_script::IN0] & ~live[0]), in1 = uint8_t(ports[input_script::IN1] & ~live[1]), in2 = uint8_t(ports[input_script::IN2] & ~live[2]);
				const uint16_t bits = menu.is_open() || freecam ? 0 : uint16_t(vf2net::pack(in0, in1, 0) | vf2net::pack(in0, in2, 1) | step_carry);
				online.poll(double(SDL_GetTicksNS()) / 1e6);
				if (hk == hotkey::menu) open_menu();
				const uint64_t te = SDL_GetTicksNS();
				if (!online.step(bits)) {
					step_carry = uint16_t(bits & ONE_SHOT);
					frames_run -= uint64_t(due - i);
					frame_clock = std::min(frame_clock, double(frames_run) + 1.0);
					break;
				}
				step_carry = 0;
				pace.emu += SDL_GetTicksNS() - te;
				if (std::exchange(resimmed, false)) m.smooth_cut();   // (a rollback: the frames before aren't the ones shown)
				draw_into_texture = true;
				held = false;
				m.m_audio_out.clear();
				load.frames++;
				game_frame++;
				if (quit_after >= 0 && int64_t(game_frame) >= quit_after) running = false;   // (tests)
			}
			due = 0;
		}
		// Display > Frame limit: the pictures a second at most. With vsync, one at or over the screen's rate is a picture
		// each refresh (the budget a little over the screen's: by the budget alone a refresh a hair early found it a hair
		// short, and that refresh's picture was lost); the governor's 60 for now. Only the pictures: the frames go by
		// the clock (and online, the session's) whatever it is. Under the game's own 57.5 (30), a frame's picture
		// waits for the budget too (the frame run, its 3D drawn with the next shown, as when the machine's behind)
		const double screen_rate = screen_hz_now > 0 ? double(screen_hz_now) : 60.0;
		double picture_cap = cfg.frame_limit == 0 ? 1e6 : cfg.frame_limit < 0 ? screen_rate : double(cfg.frame_limit);
		if (vsync_now != 0 && picture_cap >= screen_rate * 0.95) picture_cap = std::max(picture_cap, screen_rate + 1.0);
		if (smooth_limit) picture_cap = std::min(picture_cap, 60.0);
		const bool capped_low = picture_cap < FRAME_HZ * 0.95;
		const bool frame_picture = (!capped_low || smooth_budget >= 1 || stopped) && (rec.dir.empty() || rec.on || pass % 60 == 0);   // (a recording not started yet: a picture only now and then)
		if (online.playing() && !frame_picture) draw_into_texture = false;   // (a match's frames too, by the budget: as VF3's)
		// the frame run during the last present: its picture drawn now, as if it had run here
		const bool took_ahead = ahead.carried;   // (counted as a frame run in this pass, below)
		if (took_ahead) { draw_into_texture = ahead.into; held = ahead.held; ahead.carried = false; }
		for (int i = 0; i < due && running; i++) {
			// Behind (several frames to run at once), only the last is drawn: the others wouldn't be
			// seen. (The 3D of a frame not drawn is drawn with the next, as MAME draws it; with scene
			// sync every frame's 3D is set up, as it may be shown two frames on, and drawn if it is.)
			const bool last = (i == due - 1 && frame_picture) || script.shots.count(game_frame) ||
				(quit_after >= 0 && int64_t(game_frame) + 1 >= quit_after);
			if (audio && !ff && SDL_GetAudioStreamQueued(audio) == 0 && shown) load.starved++, pass_starved++;
			const uint64_t te = SDL_GetTicksNS(), waited = m.render_waited_ns();
			run_one(last);
			pace.emu += SDL_GetTicksNS() - te;
			pace.waited += m.render_waited_ns() - waited;   // (pipelined: the last picture still being drawn)
			if (!frame_done(last, draw_into_texture, held)) break;
		}
		if (audio) { const int q = SDL_GetAudioStreamQueued(audio); pace.qmin = std::min(pace.qmin, q); pace.qmax = std::max(pace.qmax, q); }
		// Auto's check again, a turn at a time (a picture each way) where it can't be seen: at a
		// moment nothing moves on screen (the menu, paused, a scene change held), or in a pass with
		// time to spare (no frame run, and the turn, as long as the last, well within the screen's
		// refresh: a picture between frames still goes up in time). A switch only at a quiet moment.
		for (int64_t at : g3.recheck_at) if (at == pass) { g3.recheck = true; g3.recheck_why = "a test (VF2_AUTO_RECHECK)"; }
		const bool quiet = stopped || held || (hold_on && due == 0 && !took_ahead);
		if (g3.recheck && (g3.mode == R3D_AUTO || (g3.verify && g3.mode == R3D_GPU)) && g3.dev && !g3.failed && (!g3.want_real || m.polygons_drawn(0) >= 400)) {
			const double refresh_ms = 1000.0 / (hidden || !screen_hz_now ? 120.0 : std::min(240.0f, screen_hz_now));
			const double turn = g3.b.active && g3.b.last_turn > 0 ? g3.b.last_turn : std::max(1.0, g3.gpu_ms);
			const bool spare = !stopped && due == 0 && !took_ahead && !async_pending && turn > 0 && double(SDL_GetTicksNS() - pass_t0) / 1e6 + turn < 0.4 * refresh_ms;
			if (quiet || spare || g3.anytime || g3.verify) {   // (SDL_GPU's check of its pictures: whenever, a turn a pass)
				const bool was = g3.on;
				const uint64_t tc = SDL_GetTicksNS();
				// (in bands of about a millisecond of the CPU's, by the last check's time)
				if (!g3.b.active) g3_bench_begin(g3.recheck_why.c_str(), true, stopped ? 1 : int(std::ceil(std::max(g3.cpu_ms, 1.0) / 1.0)));
				if (!g3.b.ready) {
					bool done = g3.b.active && g3_bench_step();
					while (stopped && g3.b.active && !done) done = g3_bench_step();   // (the menu: all of it)
					if (done && g3.b.active) g3.b.ready = true;
					g3.b.last_turn = double(SDL_GetTicksNS() - tc) / 1e6;
					pass_note += quiet ? "auto3d-quiet," : "auto3d-spare,";
					pace.draw += SDL_GetTicksNS() - tc;
				}
				// the choice, at a quiet moment (or at once if it's the same)
				if (g3.b.ready && g3.b.active) {
					const bool gpu_wins = g3.on ? g3.b.best_g <= g3.b.best_c : g3.b.best_g < 0.85 * g3.b.best_c;
					if (quiet || gpu_wins == g3.on || g3.anytime || g3.mode != R3D_AUTO) { g3.want_real = false; g3_bench_done(); pass_note += "auto3d-done,"; }
				}
				if (was != g3.on) {
					if (!g3.on) g3_free();
					load = load_window{ SDL_GetTicksNS() }; gov = gov_window{};
					log_state("Auto's check");
				}
			}
		}
		bool new_shown = false;   // a new picture of the game went up (Show FPS)
		if (async_pending) {   // the last pass's picture, drawn meanwhile
			const uint64_t tu = SDL_GetTicksNS(), waited = m.render_waited_ns();
			if (finish_async(true)) {
				shown_pic = pending_pic;
				const uint64_t w = m.render_waited_ns() - waited;
				pace.draw += uint64_t(m.render_job_ms() * 1e6);
				pace.waited_after += w;
				pace.upload += SDL_GetTicksNS() - tu - w;
				pace.pictures++;
				new_shown = true;
			}
		}
		// smooth motion: this moment's picture, between the last two frames (in place of the newest
		// frame's own), when a frame ran or the budget allows
		bool smooth_now = false;
		float alpha = 1;
		{
			const uint64_t bt = SDL_GetTicksNS();
			// (the frame limit's budget, picture_cap above: with vsync and the screen's rate a picture each refresh,
			// as before at any rate; with vsync off, forced so in a driver's settings, nothing paces the passes to
			// the screen: the limit's number then)
			smooth_budget = std::min(2.0, smooth_budget + double(bt - smooth_budget_t) * picture_cap / 1e9);
			smooth_budget_t = bt;
			const bool unseen = !hidden && (SDL_GetWindowFlags(win) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED));
			if (smooth_on() && !stopped && !ff && last_shown && !held && !unseen && (draw_into_texture || rec.on || (due == 0 && !took_ahead && smooth_budget >= 1))) {
				smooth_now = true;
				smooth_budget = std::max(0.0, smooth_budget - 1);
				alpha = float(std::clamp(frame_clock - double(frames_run), 0.0, 1.0));
				draw_into_texture = true;
			} else if (draw_into_texture && capped_low) smooth_budget = std::max(0.0, smooth_budget - 1);   // (a frame's own picture, within the limit)
		}
		// (tests: the free camera's path, at this picture's time: the hooks' clock, between two frames as far as a smooth
		// picture is; a paused game's, a tick a pass)
		fc_path_clock = double(game_frame + fc_clock_stopped) + (smooth_now ? double(alpha) - 1 : 0.0);
		// the 3D on the GPU (see gpu3d_state), the 2D's parts put together around it (gpu_picture)
		if (draw_into_texture && g3.on && !g3.failed && g3_targets(m.width() * scale, model2a::HEIGHT * scale) && gpu_pic.ready(m.width(), scale)) {
			const uint64_t td = SDL_GetTicksNS();
			const float a = smooth_now ? alpha : -1.0f;
			const int ss = aa_now();
			// (a picture checked: the CPU's 3D too, into memory of the host's. A 4:3 screen in a wide
			// picture, which the GPU leaves to the CPU, is left in the CPU's map: put() sends it up)
			const bool check = g3.check_every && (g3.pictures + 1) % uint64_t(g3.check_every) == 0;
			// (every other checked picture drawn on the GPU alone, as one not checked is, then checked
			// as shown only: drawing the CPU's 3D alongside mustn't be what makes them the same)
			const bool check3d = check && (g3.pictures + 1) / uint64_t(g3.check_every) % 2 == 0;
			u32 *px3d = nullptr; size_t stride3d = 0;
			if (check3d) {
				const size_t W = size_t(m.width()) * m.render_scale();
				g3.check_cpu.assign(W * model2a::HEIGHT * m.render_scale(), 0);
				px3d = g3.check_cpu.data(); stride3d = W;
			}
			const int target = g3.next;
			const model2a::gpu3d_hook hook = [&](model2_video &v, int pic) { return g3_submit(v, pic, target); };
			if (m.render_parts(a, px3d, stride3d, false, gpu.parts, &hook, ss > 1 || check3d, check3d)) {
				if (gpu.parts.gpu3d && check) g3_check_ref(smooth_now, alpha);
				if (gpu.parts.gpu3d) {
					if (gpu.parts.has3d) g3.next = (g3.next + 1) % G3_TARGETS;
					if (check3d && gpu.parts.has3d && g3_finish()) {   // the GPU's against the CPU's (averaged as the GPU does)
						const int w = m.width() * scale, h = model2a::HEIGHT * scale, W = m.width() * m.render_scale();
						g3.check_gpu.assign(size_t(w) * h, 0);
						if (g3.dev->read_target(target, g3.check_gpu.data(), size_t(w))) {
							uint64_t diff = 0;
							for (int y = 0; y < h; y++)
								for (int x = 0; x < w; x++) {
									u32 want;
									if (ss == 1) want = g3.check_cpu[size_t(y) * W + x];
									else {
										u32 r = 0, g = 0, b = 0, n = 0;
										for (int j = 0; j < ss; j++)
											for (int i = 0; i < ss; i++)
												if (const u32 c = g3.check_cpu[size_t(y * ss + j) * W + x * ss + i]) r += (c >> 16) & 255, g += (c >> 8) & 255, b += c & 255, n++;
										const u32 k = u32(ss * ss);
										want = n ? (u32(std::lround(n * 255.0 / k)) << 24) | (r / k) << 16 | (g / k) << 8 | (b / k) : 0;
									}
									const u32 got = g3.check_gpu[size_t(y) * w + x];
									if (got != want) {
										diff++;
										int e = 0;
										for (int sh = 0; sh < 32; sh += 8) e = std::max(e, std::abs(int((got >> sh) & 255) - int((want >> sh) & 255)));
										g3.check_max = std::max(g3.check_max, e);
										if (g3.check_bad < 5 && diff == 1)
											fprintf(stderr, "gpu3d check: picture %llu: at %d,%d the CPU's %08x, the GPU's %08x (scale %d, antialiasing %d, smooth %d)\n", (unsigned long long)g3.pictures, x, y, want, got, scale, ss, int(smooth_now));
									}
								}
							g3.checked++; g3.check_px += uint64_t(w) * h; g3.check_diff += diff; g3.check_bad += diff != 0;
						}
					}
					if (pipelined) {   // put together a pass later (the GPU drawing meanwhile)
						gpu_pic.upload_2d(gpu.parts, ss);   // (now, while the 2D is the board's)
						pending_pic = m.last_picture();
						async_pending = gpu.pending = g3.pending = true;
						g3.pending_t = target; g3.pending_ss = ss;
						draw_into_texture = false;
						pace.draw += SDL_GetTicksNS() - td;
					} else {
						const uint64_t te = SDL_GetTicksNS();
						pace.draw += te - td;
						gpu_pic.upload_2d(gpu.parts, ss);   // (the 2D sent up while the GPU draws the 3D)
						if (g3_settle() && gpu_pic.put(gpu.parts, gpu.parts.has3d ? g3.tex[target] : nullptr, ss)) {
							g3_check_shown();
							gpu.shown = true;
							shown_pic = m.last_picture();
							pace.pictures++;
							new_shown = true;
							draw_into_texture = false;
						}
						pace.upload += SDL_GetTicksNS() - te;
					}
				} else if (ss == 1 && !check3d) {   // (drawn on the CPU: a 4:3 screen in a wide picture)
					const uint64_t te = SDL_GetTicksNS();
					pace.draw += te - td;
					if (gpu_put(gpu.parts)) {
						shown_pic = m.last_picture();
						pace.pictures++;
						new_shown = true;
						draw_into_texture = false;
					}
					pace.upload += SDL_GetTicksNS() - te;
				}
			}
		}
		if (draw_into_texture && gpu.on && aa_now() == 1 && (scale >= 2 || gpu.any_scale) && gpu_pic.ready(m.width(), m.render_scale())) {   // the 2D on the GPU (see gpu_picture)
			const uint64_t td = SDL_GetTicksNS();
			const float a = smooth_now ? alpha : -1.0f;
			u32 *px3d; size_t stride3d;
			gpu_lock(px3d, stride3d, pipelined);
			if (pipelined && m.render_parts(a, px3d, stride3d, true, gpu.parts)) {   // the 3D drawn alongside the next frame
				gpu_pic.upload_2d(gpu.parts);   // (now, while the 2D is the board's)
				pending_pic = m.last_picture();
				async_pending = gpu.pending = true;
				pace.draw += SDL_GetTicksNS() - td;
				draw_into_texture = false;
			} else if (!pipelined && m.render_parts(a, px3d, stride3d, false, gpu.parts)) {
				const uint64_t te = SDL_GetTicksNS();
				pace.draw += te - td;
				if (gpu_put(gpu.parts)) {
					shown_pic = m.last_picture();
					pace.pictures++;
					new_shown = true;
					draw_into_texture = false;
				}
				pace.upload += SDL_GetTicksNS() - te;
			}
			if (draw_into_texture) gpu_unlock();   // (not drawn this way: render test mode)
		}
		if (draw_into_texture && pipelined && aa_now() == 1 && !tex_next) {   // the second texture, as the first
			float tw = 0, th = 0;
			SDL_GetTextureSize(tex, &tw, &th);
			if ((tex_next = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, int(tw), int(th)))) {
				SDL_SetTextureBlendMode(tex_next, SDL_BLENDMODE_NONE);
				SDL_SetTextureScaleMode(tex_next, SDL_SCALEMODE_LINEAR);
			}
		}
		void *next_px = nullptr;
		int next_pitch = 0;
		if (draw_into_texture && pipelined && aa_now() == 1 && tex_next && (!lock_across || !draw_locked)) {   // (into the host's memory: see lock_across, draw_locked)
			const size_t n = size_t(m.width()) * m.render_scale() * model2a::HEIGHT * m.render_scale();
			if (async_buf.size() != n) async_buf.assign(n, 0);
			next_px = async_buf.data(); next_pitch = m.width() * m.render_scale() * 4;
			async_in_buf = true;
		}
		if (draw_into_texture && pipelined && aa_now() == 1 && tex_next && (async_in_buf || SDL_LockTexture(tex_next, nullptr, &next_px, &next_pitch))) {
			const uint64_t td = SDL_GetTicksNS();   // drawn alongside the next frame, straight into the texture
			if (smooth_now) m.render_smooth(alpha, static_cast<uint32_t *>(next_px), size_t(next_pitch) / 4, true);
			else m.render_async(static_cast<uint32_t *>(next_px), size_t(next_pitch) / 4);
			pending_pic = m.last_picture();
			async_pending = true;
			pace.draw += SDL_GetTicksNS() - td;
		} else if (draw_into_texture) {   // (not a copy of a 12 MB picture at 4x: drawn where it goes, where that's the host's memory)
			void *px;
			int pitch;
			const uint64_t tu = SDL_GetTicksNS();
			if (aa_now() == 1 && draw_locked && SDL_LockTexture(tex, nullptr, &px, &pitch)) {
				const uint64_t td = SDL_GetTicksNS();
				if (smooth_now) m.render_smooth(alpha, static_cast<uint32_t *>(px), size_t(pitch) / 4, false);
				else m.render_into(static_cast<uint32_t *>(px), size_t(pitch) / 4, true);
				shown_pic = m.last_picture();
				const uint64_t te = SDL_GetTicksNS();
				SDL_UnlockTexture(tex);
				gpu.shown = false;
				pace.draw += te - td;
				pace.upload += (td - tu) + (SDL_GetTicksNS() - te);
				pace.pictures++;
				new_shown = true;
			} else {
				const uint64_t td = SDL_GetTicksNS();
				show_drawn(smooth_now ? m.render_smooth(alpha) : m.render(true));
				pace.draw += SDL_GetTicksNS() - td;
			}
		}
		if (held && !new_picture) {   // the last picture stays up, unless this one is the game's black curtain
			const bitmap_rgb32 &b = m.render(true);
			if (scene_sync::is_blank(b)) show_drawn(b);
			else pace.pictures++, pace.held++;   // (a picture held on purpose isn't one missed)
		}
		if (freecam && stopped && !menu.is_open() && !suspended && shown_pic) redraw_shown();   // (paused: the free camera still flies, a picture each pass)
		if (new_picture) {
			const uint64_t tu = SDL_GetTicksNS();
			upload_picture(*picture);
			pace.upload += SDL_GetTicksNS() - tu;
			pace.pictures++;
			new_picture = false;
			new_shown = !stopped;
		}
		if (due || took_ahead || smooth_now) {
			if (test_load_ms > 0 && gov_from != UINT64_MAX && SDL_GetTicksNS() < gov_from - 10000000000ull + uint64_t(test_load_s * 1e9))
				for (const uint64_t until = SDL_GetTicksNS() + uint64_t(test_load_ms * 1e6 * std::max(due, 1) * scale * scale / 9); SDL_GetTicksNS() < until;) {}   // VF2_TEST_LOAD
			const uint64_t w = SDL_GetTicksNS() - work_t0;
			load.work += w;
			pass_work = w;
			if (due) load.worst = std::max(load.worst, w / due);
			if (perf_hints_up && due) sysinfo::perf_hints_report(int64_t(w / due));   // (the frame's own work, ADPF)
		}
		// the FPS (Display > Show FPS): new pictures shown, counted over half a second
		// (a picture held on purpose, a scene change held till it's whole or a cut's first frame,
		// counts as shown: the screen kept its pace, and counting it as missed showed a drop to
		// 80-odd at every scene change)
		if (new_shown || (hold_on && !stopped && !menu.is_open())) fps_shown++;
		// (the 3D on the CPU with the setting not CPU: why, on screen once; not over online play: after it, and one up as
		// a match is joined is taken down and said again after)
		if (cpu_cut) {
			cpu_cut = false;
			if (scale > 1 || aa != 1) {
				aa = 1;
				set_scale(1, false);
				fprintf(stderr, "the 3D on the CPU: drawn at 1x without antialiasing for this run (the settings kept)\n");
				if (cpu_why_new) cpu_why_cut = true;   // (said with the notice)
			}
		}
		if (online.phase() != Online::Phase::off && !cpu_why.empty() && menu.toast_showing()) { menu.toast(""); cpu_why_new = true; }
		if (cpu_why_new && online.phase() == Online::Phase::off) {
			cpu_why_new = false;
			menu.toast(cpu_notice().str(), 12);
			fprintf(stderr, "the notice: %s\n", cpu_notice().en().c_str());   // (in the log too: what the player was told)
		}
		if (const uint64_t now = SDL_GetTicksNS(); now - fps_since >= 500000000) {
			menu.set_fps(cfg.show_fps ? std::to_string(int(std::lround(fps_shown * 1e9 / double(now - fps_since)))) + " FPS" : "");
			fps_shown = 0;
			fps_since = now;
		}
		const uint64_t tb = SDL_GetTicksNS();
		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
		SDL_RenderClear(ren);
		crt_on = cfg.crt_colours || cfg.brightness != 100 || cfg.gamma != 100;
		// (Cheats' random fighters, everyone at the select random: the select they're picked at (a moment) isn't shown,
		// who they got a surprise as the fight comes in)
		const bool select_hidden = m.random_select_hidden();
		if (!select_hidden)
			crt_paint.draw(ren, gpu.shown ? gpu_pic.target() : tex, nullptr, cfg.crt_colours, cfg.brightness, cfg.gamma);   // (CRT colours, brightness, gamma: the picture only)
		if (!select_hidden) draw_hud_flags();
		draw_filter();
		if (touch_shown()) touch.draw(ren);
		menu.render(true);   // (the FPS too: set_fps above leaves it empty while Show FPS is off)
		pace.blit += SDL_GetTicksNS() - tb;
		if (!running && gpu_pic.check_every) gpu_pic.report();
		if (!running && g3.check_every)
			fprintf(stderr, "gpu3d check: %llu of %llu GPU pictures checked against the CPU's, %llu with differences, %llu of %llu pixels differ (largest %d)\n",
					(unsigned long long)g3.checked, (unsigned long long)g3.pictures, (unsigned long long)g3.check_bad, (unsigned long long)g3.check_diff, (unsigned long long)g3.check_px, g3.check_max);
		if (static const int sdl_load = getenv("VF2_TEST_GPU_LOAD") ? atoi(getenv("VF2_TEST_GPU_LOAD")) : 0; sdl_load > 0 && gpu.shown) {   // tests: the renderer's GPU work made slow (its frames queue up behind)
			static SDL_Texture *scratch = nullptr;
			if (!scratch) scratch = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 7680, 4320);
			SDL_SetRenderTarget(ren, scratch);
			for (int i = 0; i < sdl_load; i++) SDL_RenderTexture(ren, gpu_pic.target(), nullptr, nullptr);
			SDL_SetRenderTarget(ren, nullptr);
		}
		if (g3.late_wait > 0 && --g3.late_wait == 0) g3_compare(g3.late, g3.late_ref, g3.late_w, g3.late_h, g3.late_smooth, true);
		if (!running && g3.check_every)
			for (int l = 0; l < 2; l++)
				fprintf(stderr, "gpu3d check: %llu pictures as shown%s checked against the CPU's way, %llu differ (over 50 pixels by over 2), %llu of %llu pixels in all (largest %d)\n", (unsigned long long)g3.shown[l].checked,
						l ? " (read some passes later)" : " (read at once)", (unsigned long long)g3.shown[l].bad, (unsigned long long)g3.shown[l].diff, (unsigned long long)g3.shown[l].px, g3.shown[l].most);
		if (!running && quit_after >= 0) {
			picture = shown_pic ? &m.redraw(*shown_pic) : &m.render(true);   // (the picture on show)
			write_png("build/host_last.png", picture->data(), picture->width(), picture->height());
			write_window("build/host_window.png");
		}
		if (window_shot && (due || took_ahead) && game_frame >= window_shot + 3) {   // (a new shape's texture filled by then)
			write_window(("build/host_window_" + std::to_string(window_shot) + ".png").c_str());
			window_shot = 0;
		}
		if (flicker_dir && new_shown && !stopped) {   // VF2_FLICKER: the picture on show against the two before it
			int lw, lh; SDL_RendererLogicalPresentation mode;
			SDL_GetRenderLogicalPresentation(ren, &lw, &lh, &mode);
			SDL_SetRenderLogicalPresentation(ren, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
			if (SDL_Surface *shot = SDL_RenderReadPixels(ren, nullptr)) {
				if (SDL_Surface *x = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_XRGB8888)) {
					const int fw = x->w, fh = x->h;
					std::vector<u32> cur(size_t(fw) * fh);
					for (int y = 0; y < fh; y++) memcpy(&cur[size_t(y) * fw], (uint8_t *)x->pixels + y * x->pitch, size_t(fw) * 4);
					auto d = [](u32 a, u32 b) { return std::abs(int(a >> 16 & 255) - int(b >> 16 & 255)) + std::abs(int(a >> 8 & 255) - int(b >> 8 & 255)) + std::abs(int(a & 255) - int(b & 255)); };
					auto &fl = flicker;
					if (fl.w == fw && fl.h == fh && fl.n >= 2) {
						// a pixel that went somewhere and came back: A, B, A (B far from both)
						uint64_t toggles = 0;
						for (size_t i = 0; i < cur.size(); i++)
							if (d(cur[i], fl.prev2[i]) < 24 && d(cur[i], fl.prev1[i]) > 96 && d(fl.prev1[i], fl.prev2[i]) > 96) toggles++;
						fl.total += toggles; fl.pics++;
						if (toggles > fl.worst) fl.worst = toggles;
						fprintf(fl.log, "%lld %llu %llu\n", (long long)pass, (unsigned long long)game_frame, (unsigned long long)toggles);
						if (toggles >= fl.save_min && fl.saved < 12 && game_frame >= fl.save_from) {
							fl.saved++;
							const std::string b = std::string(flicker_dir) + "/t" + std::to_string(pass);
							write_png((b + "_a.png").c_str(), fl.prev2.data(), fw, fh);
							write_png((b + "_b.png").c_str(), fl.prev1.data(), fw, fh);
							write_png((b + "_c.png").c_str(), cur.data(), fw, fh);
						}
					}
					if (fl.w != fw || fl.h != fh) fl.n = 0;
					fl.w = fw; fl.h = fh;
					fl.prev2.swap(fl.prev1); fl.prev1.swap(cur); fl.n++;
					SDL_DestroySurface(x);
				}
				SDL_DestroySurface(shot);
			}
			SDL_SetRenderLogicalPresentation(ren, lw, lh, mode);
		}
		if (!running && flicker_dir) fprintf(stderr, "flicker: %llu pictures, %llu pixels went and came back (%.1f a picture), the most in one %llu\n",
				(unsigned long long)flicker.pics, (unsigned long long)flicker.total, flicker.pics ? double(flicker.total) / flicker.pics : 0.0, (unsigned long long)flicker.worst);
		for (int64_t at : shots_at)
			if (at == pass) write_window(("build/host_window_at_" + std::to_string(at) + ".png").c_str());
		// the next frame, if it will be due by the next refresh, run on the game's thread while this
		// present waits (see emu_job): not for a frame whose picture the test run writes, nor its last
		bool ran_ahead = false;
		uint64_t ahead_ns = 0, ahead_waited = 0;
		if (emu_threaded && running && shown && smooth_on() && !paused && !menu.is_open() && !ff && !sound_full && !online.playing() && !capped_low &&
			!script.shots.count(game_frame) && !(quit_after >= 0 && int64_t(game_frame) + 1 >= quit_after) &&
			frame_clock + FRAME_HZ / (hidden || screen_hz_now <= 0 ? 120.0 : double(screen_hz_now)) >= double(frames_run + 1)) {
			if (audio && SDL_GetAudioStreamQueued(audio) == 0) load.starved++, pass_starved++;
			frames_run++;
			ran_ahead = true;
			emu_job.start([&] {
				const uint64_t te = SDL_GetTicksNS(), waited = m.render_waited_ns();
				run_one(true);
				ahead_ns = SDL_GetTicksNS() - te;
				ahead_waited = m.render_waited_ns() - waited;
			});
		}
		if (rec.on && rec.video) {   // VF2_RECORD: the window's whole picture, a picture each pass
			int lw, lh;
			SDL_RendererLogicalPresentation mode;
			SDL_GetRenderLogicalPresentation(ren, &lw, &lh, &mode);
			SDL_SetRenderLogicalPresentation(ren, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
			if (SDL_Surface *sf = SDL_RenderReadPixels(ren, nullptr)) {
				if (SDL_Surface *c = SDL_ConvertSurface(sf, SDL_PIXELFORMAT_ABGR8888)) {
					for (int y = 0; y < rec.h && y < c->h; y++)
						fwrite(static_cast<uint8_t *>(c->pixels) + size_t(y) * c->pitch, 4, size_t(rec.w), rec.video);
					SDL_DestroySurface(c);
				}
				SDL_DestroySurface(sf);
			}
			SDL_SetRenderLogicalPresentation(ren, lw, lh, mode);
			rec.n++;
			if (stopped) {   // (a paused picture: the game's clock stands still, the sound silent for its time, in step)
				rec.stopped_n++;
				if (rec.wav) {
					const uint64_t want = rec.stopped_n * uint64_t(model2a::SOUND_RATE) / uint64_t(rec.hz);
					const std::vector<int16_t> zero(size_t(want - rec.silent) * 2, 0);
					fwrite(zero.data(), sizeof(int16_t), zero.size(), rec.wav);
					rec.wav_bytes += uint32_t(zero.size() * sizeof(int16_t));
					rec.silent = want;
				}
			}
			if (rec.n % (uint64_t(rec.hz) * 10) == 0) fprintf(stderr, "record: %.0f s\n", rec.n / double(rec.hz));
			if (game_frame >= rec.to) running = false;
		}
		const uint64_t tp = SDL_GetTicksNS();
		SDL_RenderPresent(ren);
		pace.present += SDL_GetTicksNS() - tp;
		pace.passes++;
		if (!shown) {
			shown = true;
			if (!hidden) {
				SDL_ShowWindow(win);
				SDL_RaiseWindow(win);
				SDL_RenderPresent(ren);   // (again, now that it's on screen)
			}
			if (pace_log) fprintf(stderr, "first picture %.3f s after launch\n", seconds_since(launch_t));
			gov_from = SDL_GetTicksNS() + 10000000000ull;
			log_state("the start");
		}
		{   // at most a pass every 2 ms: a hidden or minimized window's present may not wait
			static uint64_t last_pass = SDL_GetTicksNS();
			const uint64_t t = SDL_GetTicksNS();
			if (!hidden && t - last_pass < 2000000) SDL_DelayNS(2000000 - (t - last_pass));
			last_pass = SDL_GetTicksNS();
			pace.delay += last_pass - t;
		}
		if (hidden) {   // a hidden window may not wait for the screen: pace the passes as 120 Hz
			static uint64_t next = SDL_GetTicksNS();
			next += 8333333;
			const uint64_t t = SDL_GetTicksNS();
			if (next > t) SDL_DelayNS(next - t); else next = t;
		}
		if (ran_ahead) {   // the frame run meanwhile: what follows it, here (its time counted as the pass's work)
			emu_job.wait();
			pace.emu += ahead_ns;
			pace.waited += ahead_waited;
			load.work += ahead_ns;
			pass_work += ahead_ns;
			load.worst = std::max(load.worst, ahead_ns);
			ten.ahead++;
			ahead.carried = true;
			frame_done(true, ahead.into, ahead.held);
		}
		// Started from a terminal, the window can open behind it or without the keyboard (the keys go
		// to the terminal): it asks for it over about 5 s, and the log says whether it got it.
		if (focus_checks > 0 && !hidden && shown) {
			const bool ours = SDL_GetKeyboardFocus() == win;
			if (--focus_checks % 30 == 0 && !ours) SDL_RaiseWindow(win);
			if (focus_checks == 0 || ours) {
				static bool told = false;
				if (!told) { told = true; fprintf(stderr, "keyboard: %s\n", ours ? "ours" : "not ours (click the window)"); }
				if (ours) focus_checks = 0;
			}
		}
		const model2a::work_stats ws = m.take_stats();
		// The governor's view (gov_window): only ordinary passes count, not a scene change's (a
		// picture held by scene sync, a camera cut in the frames run, a burst of texture uploads, a
		// new texture, frames caught up), nor stopped ones: their one-off work isn't the load
		{
			const bool disturbed = stopped || held || pass_cut || ws.ram_changes > 2000 || due > 2 || !pass_note.empty();
			if (disturbed) gov_quiet = 30;   // (and the half second after it: the scene settling)
			else if (gov_quiet > 0) gov_quiet--;
			else {
				gov.time += SDL_GetTicksNS() - pass_t0;
				gov.work += pass_work;
				gov.frames += uint64_t(due + ran_ahead);
				gov.pictures += new_shown;
				gov.starved += pass_starved;
				pipe_look.work += pass_work;
				pipe_look.frames += uint64_t(due + ran_ahead);
				if (pipe_look.frames >= 30) {
					const double cost = double(pipe_look.work) / 1e6 / double(pipe_look.frames);
					if (govern && pipeline_force < 0 && !pipelined && aa_now() == 1 && shown && cost > 0.85 * 1000.0 / FRAME_HZ) {
						pipelined = true;
						if (cfg.pipelined_at != scale) { cfg.pipelined_at = scale; cfg_dirty = true; }
						gov_note += "gov:pipelined,";
						fprintf(stderr, "a frame's run and draw take %.1f ms of its %.1f (half a second): each picture drawn alongside the next frame from now on\n", cost, 1000.0 / FRAME_HZ);
						log_state("the governor's");
					}
					pipe_look = {};
				}
			}
		}
		if (picture_log) {
			auto d = [&](uint64_t a, uint64_t b) { return double(a - b) / 1e6; };
			const char *kind = new_shown ? (smooth_now ? "smooth" : "new") : held ? "held" : "same";
			fprintf(picture_log, "%lld %llu %d %s %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %d %d %d %d %d %.3f %s %d\n", (long long)pass, (unsigned long long)game_frame, due, kind,
					smooth_now ? alpha : 1.0f, d(pace.emu, pace0.emu), d(pace.waited, pace0.waited), d(pace.draw, pace0.draw), d(pace.upload, pace0.upload),
					d(pace.blit, pace0.blit), d(pace.present, pace0.present), d(SDL_GetTicksNS(), pass_t0), ws.deferred_ms, ws.deferred_n, ws.ram_changes,
					sync.delay(), int(pipelined), scale, new_shown ? m.moment_3d() : -2.0, pass_note.empty() ? "-" : pass_note.c_str(), m.frame_polygons());
		}
		if (const uint64_t t = SDL_GetTicksNS(); t - load.since >= 2000000000ull) {
			const double busy = double(load.work) / double(t - load.since);
			const double secs = double(t - load.since) / 1e9;
			if (pace_log) {
				float hz = 0;
				if (const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win))) hz = dm->refresh_rate;
				fprintf(stderr, "scale %dx: %.2f ms a frame (worst %.2f), busy %.0f%%, sound ran dry %llu times\n", scale,
						load.frames ? load.work / 1e6 / load.frames : 0.0, load.worst / 1e6, busy * 100, (unsigned long long)load.starved);
				fprintf(stderr, "  %.2f frames/s, %.1f passes/s, %.1f pictures/s (screen %.2f Hz); per second: run %.0f ms (waiting for the drawing %.0f, then %.0f after), draw %.0f%s, upload %.0f, blit %.0f, present %.0f, delay %.0f; queued %d-%d bytes; scene sync: 3D %d frames late, %llu held\n",
						load.frames / secs, pace.passes / secs, pace.pictures / secs, hz, pace.emu / 1e6 / secs, pace.waited / 1e6 / secs, pace.waited_after / 1e6 / secs, pace.draw / 1e6 / secs, pipelined ? " (alongside)" : "",
						pace.upload / 1e6 / secs, pace.blit / 1e6 / secs, pace.present / 1e6 / secs, pace.delay / 1e6 / secs, pace.qmin, pace.qmax, sync.delay(), (unsigned long long)pace.held);
			}
			// F9: the timings in the title bar (and the log) every 2 s
			if (timings && !stopped) {
				char t2[220];
				snprintf(t2, sizeof t2, "Virtua Fighter 2  %dx  %.1f frames/s  %.1f shown/s  load %.0f%%  game %.1f ms/frame (worst %.1f)  sound dropouts %llu",
						 scale, load.frames / secs, pace.pictures / secs, busy * 100, load.frames ? load.work / 1e6 / load.frames : 0.0, load.worst / 1e6,
						 (unsigned long long)load.starved);
				SDL_SetWindowTitle(win, t2);
				fprintf(stderr, "%s\n", t2);
			}
			// (the first window has the start: the sound queue filling, the first frames)
			const double fps = load.frames / secs, shown_fps = pace.pictures / secs;
			const bool unseen = !hidden && (SDL_GetWindowFlags(win) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED));
			// short of pictures: under 55 a second, or with smooth motion under 90% of what it aims
			// for (the screen's refresh, 120 at most, or 60 once limited); and only while busy (a dip
			// with time to spare, as when a stage loads, isn't the load)
			double enough = 55;
			float screen_hz = hidden ? 120 : 60;
			if (const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win))) if (!hidden && dm->refresh_rate > 0) screen_hz = dm->refresh_rate;
			if (test_screen_hz > 0) screen_hz = test_screen_hz;
			screen_hz_now = screen_hz;
			{   // (the log says when it's on or off)
				int vs = 0;
				const int on = SDL_GetRenderVSync(ren, &vs) && vs != 0;
				if (on != vsync_now) fprintf(stderr, "vsync: %s\n", on ? "on" : "off (pictures by Display > Frame limit, not one each refresh)");
				vsync_now = on;
			}
			if (smooth_on()) enough = std::max(55.0, std::min<double>(screen_hz, smooth_limit ? 60 : 120) * 0.9);
			enough = std::min(enough, picture_cap * 0.9);   // (Display > Frame limit: no more asked for than it lets through)
			// Judged on the window's ordinary passes only (gov), and only when they're most of it: a
			// window taken up by scene changes says nothing either way (its streaks stay as they were)
			const double gsecs = double(gov.time) / 1e9;
			// A machine that can't draw a picture a frame runs two frames a pass and slips the rest of
			// the clock (pass_note's "slip"): each of those passes counts as a scene change's, so its
			// ordinary passes never filled half a window and nothing was judged (a player at 2x with
			// antialiasing 2x: 28 pictures a second at 97% busy for minutes, the governor untouched).
			// So a whole window, nothing held in it, over 90% busy and short of pictures, is judged
			// too, as vf1-recomp judges its windows: short.
			const bool steady_short = !load.first && !unseen && !stopped && t >= gov_from && pace.held == 0 && busy > 0.9 && shown_fps < enough;
			const bool judged = (!load.first && !unseen && !stopped && gsecs >= secs * 0.5 && t >= gov_from) || steady_short;
			const double gfps = gsecs > 0 ? gov.frames / gsecs : 0, gshown = gsecs > 0 ? gov.pictures / gsecs : 0, gbusy = gsecs > 0 ? double(gov.work) / double(gov.time) : 0;
			const bool slow = steady_short || (judged && (gfps < 56.5 || gshown < enough) && gbusy > 0.6);
			const bool over = judged && (gbusy > 0.9 || (gov.starved > 0 && gbusy > 0.6));
			// Auto: pictures short, the other renderer checked at the next quiet moment (at most every 20 s)
			if (g3.mode == R3D_AUTO && (soft_render || getenv("VF2_AUTO_MEASURE")) && g3.dev && !g3.failed && !g3.recheck && (slow || over) && t - g3.last_check > 20000000000ull) {
				g3.recheck = true; g3.recheck_why = "the pictures short";
			}
			// Pipelining: on when running a frame and drawing its pictures takes over 85% of a frame's
			// time (each picture then goes up a pass later: a frame, or with smooth motion a refresh of
			// the screen), before the governor lowers anything. Like the
			// governor's changes it stays for this run; the player's own change of scale, antialiasing
			// or shape starts it afresh.
			// (the cost of a frame: running it, and drawing its pictures)
			const double frame_cost = gov.frames ? double(gov.work) / 1e6 / gov.frames : 0.0;   // (ordinary passes': their run and draw)
			bool stepped = false;
			// (short of pictures in two windows in a row: one with a stage loading in it can be short)
			const int slow_windows = slow ? load.slow + 1 : 0;
			if (govern && pipeline_force < 0 && !pipelined && judged && aa_now() == 1 &&
				frame_cost > 0.85 * 1000.0 / FRAME_HZ) {
				pipelined = stepped = true;
				if (cfg.pipelined_at != scale) { cfg.pipelined_at = scale; cfg_dirty = true; }
				gov_note += "gov:pipelined,";
				fprintf(stderr, "a frame's run and draw take %.1f ms of its %.1f: each picture drawn alongside the next frame from now on\n", frame_cost, 1000.0 / FRAME_HZ);
				log_state("the governor's");
			}
			// then, before antialiasing and the scale, smooth motion's pictures limited to 60 a second
			// (not on a screen of 60 Hz or less: a picture for each refresh is no more than 60 a second
			// there, and a budget of 60 a second would leave some refreshes without one)
			if (govern_cuts && !stepped && smooth_on() && !smooth_limit && screen_hz > 65 && judged && (slow_windows >= 3 || (over && load.over + 1 >= 3))) {
				smooth_limit = stepped = true;
				if (gov_raised) { gov_calm_need = std::min(30, gov_calm_need * 2); gov_raised = false; }   // (lifted, then short again: the next lift waits longer, never for good)
				gov_note += "gov:smooth60,";
				fprintf(stderr, "%.1f frames a second, %.1f shown, busy %.0f%% of the time: smooth pictures limited to 60 a second\n", fps, shown_fps, busy * 100);
				log_state("the governor's");
			}
			if (judged) load = load_window{ t, 0, 0, 0, 0, over && !stepped ? load.over + 1 : 0, stopped, stepped ? 0 : slow_windows };
			else load = load_window{ t, 0, 0, 0, 0, load.over, stopped, load.slow };   // (not judged: the streaks as they were)
			gov = gov_window{};
			// (six seconds of ordinary play short of pictures, or eight over-busy: a lasting load)
			if (govern_cuts && !stepped && judged && (load.slow >= 3 || load.over >= 4) && (aa_now() > 1 || aa == 0 || scale > 1)) {
				if (aa_now() > 1 || aa == 0) {
					fprintf(stderr, "%.1f frames a second, %.1f shown, busy %.0f%% of the time: antialiasing off\n", fps, shown_fps, busy * 100);
					aa = 1;
					set_scale(scale, false);
				} else {
					fprintf(stderr, "%.1f frames a second, %.1f shown, busy %.0f%% of the time: render scale lowered to %dx\n",
							fps, shown_fps, busy * 100, scale - 1);
					set_scale(scale - 1, false);
				}
				gov_note += "gov:lowered,";
				load.over = load.slow = 0;
				gov_calm = 0;
				if (gov_raised) { gov_calm_need = std::min(30, gov_calm_need * 2); gov_raised = false; }   // (a step back up that didn't hold: the next waits longer)
			}
			// (calm: the frames and pictures all there, busy under 55% of the time)
			const bool calm = judged && !slow && !over && gbusy < 0.55 && gfps >= 56.5 && gshown >= enough;
			gov_calm = !judged ? gov_calm : calm ? gov_calm + 1 : 0;
			// (2026-10-14, vf2-recomp #3: an RTX 5080 at 6x was limited to 60 by a slow first few seconds, then
			// sat at 40% busy for minutes, still at 60: a limit is lifted again once things are calm, the
			// smooth pictures' first, and a limit that comes straight back stays)
			if (govern_cuts && !stepped && gov_calm >= gov_calm_need && smooth_limit && scale == player_scale && aa == player_aa) {
				fprintf(stderr, "%.1f frames a second, %.1f shown, busy %.0f%% of the time: smooth pictures no longer limited to 60\n", fps, shown_fps, busy * 100);
				smooth_limit = false;
				gov_note += "gov:smooth-all,";
				gov_raised = true;
				gov_calm = 0;
				load.over = load.slow = 0;
				log_state("the governor's");
			} else
			if (govern_cuts && !stepped && gov_calm >= gov_calm_need && (scale < std::min(player_scale, scale_most()) || aa != player_aa)) {
				if (scale < std::min(player_scale, scale_most())) {
					fprintf(stderr, "%.1f frames a second, %.1f shown, busy %.0f%% of the time: render scale back up to %dx\n", fps, shown_fps, busy * 100, scale + 1);
					set_scale(scale + 1, false);
				} else {
					fprintf(stderr, "%.1f frames a second, %.1f shown, busy %.0f%% of the time: antialiasing back on\n", fps, shown_fps, busy * 100);
					aa = player_aa;
					set_scale(scale, false);
				}
				gov_note += "gov:raised,";
				gov_raised = true;
				gov_calm = 0;
				load.over = load.slow = 0;
			}
			// a 2 s window short of pictures, in the log with its own timings (where a 10 s line's dip came from)
			if (!stopped && !unseen && shown && pace.pictures / secs < 0.9 * std::min(screen_hz, 120.0f) && pace.pictures > 0) {
				const double pics = double(pace.pictures), passes = std::max<double>(1, pace.passes);
				fprintf(stderr, "short: %.1f frames/s, %.1f pictures/s, busy %.0f%%; a picture: run %.2f ms (waiting for the drawing %.2f), draw %.2f%s, upload %.2f; a pass: present %.2f ms; held %llu; %s\n",
						fps, pics / secs, busy * 100, pace.emu / 1e6 / pics, pace.waited / 1e6 / pics, pace.draw / 1e6 / pics, pipelined ? " (alongside)" : "", pace.upload / 1e6 / pics,
						pace.present / 1e6 / passes, (unsigned long long)pace.held, gov_note.empty() ? "-" : gov_note.c_str());
			}
			if (!stopped) {
				ten.secs += secs; ten.frames += fps * secs; ten.pictures += pace.pictures; ten.work += busy * secs * 1000;
				ten.emu += pace.emu / 1e6; ten.draw += pace.draw / 1e6; ten.upload += pace.upload / 1e6; ten.present += pace.present / 1e6; ten.passes += pace.passes;
			}
			if (ten.secs >= 10) {
				const double pics = std::max(1.0, ten.pictures), passes = std::max(1.0, ten.passes);
				char thr[64] = ", the game's thread off";   // (on: its share of the frames)
				if (emu_threaded) snprintf(thr, sizeof thr, ", %.0f%% of the frames run during the present", 100.0 * ten.ahead / std::max(1.0, ten.frames));
				fprintf(stderr, "pace: %.1f frames/s, %.1f pictures/s (screen %.2f Hz), busy %.0f%%; a picture: run %.2f ms, draw %.2f%s, upload %.2f; a pass: present %.2f ms; %dx%s%s\n",
						ten.frames / ten.secs, ten.pictures / ten.secs, screen_hz, ten.work / (ten.secs * 10), ten.emu / pics, ten.draw / pics, pipelined ? " (alongside)" : "",
						ten.upload / pics, ten.present / passes, scale, !smooth_on() ? ", smooth off" : "", thr);
				if (const std::string where = sysinfo::placement(); !where.empty()) fprintf(stderr, "  %s\n", where.c_str());
				if (const std::string clocks = sysinfo::cpu_frequencies(); !clocks.empty()) fprintf(stderr, "  the cores at: %s\n", clocks.c_str());
				if (const std::string gpu_clock = sysinfo::gpu_frequencies(); !gpu_clock.empty()) fprintf(stderr, "  the GPU at: %s\n", gpu_clock.c_str());
				ten = pace_ten{};
				static uint64_t g3_pics0 = 0, g3_unw0 = 0;
				static double g3_gpu0 = 0, g3_cpu0 = 0;
				if (g3.pictures > g3_pics0) {
					const double n = double(g3.pictures - g3_pics0);
					fprintf(stderr, "the 3D on the GPU: %.0f pictures, a picture %.2f ms on the GPU, %.2f handing it over", n, (g3.sum_gpu - g3_gpu0) / n, (g3.sum_cpu - g3_cpu0) / n);
					if (g3.unwaited > g3_unw0) fprintf(stderr, "; %llu more shown still drawing (not waited for)", (unsigned long long)(g3.unwaited - g3_unw0));
					fprintf(stderr, "\n");
				} else if (g3.unwaited > g3_unw0) fprintf(stderr, "the 3D on the GPU: %llu pictures shown still drawing (not waited for)\n", (unsigned long long)(g3.unwaited - g3_unw0));
				g3_pics0 = g3.pictures; g3_gpu0 = g3.sum_gpu; g3_cpu0 = g3.sum_cpu; g3_unw0 = g3.unwaited;
			}
			pace = pace_window{};
		}
		if (SDL_GetTicksNS() >= next_save) {
			next_save = SDL_GetTicksNS() + 3000000000ull;
			// a setting changed outside the menu (F5, F6, a pad's View + LB or RB): saved within 3 s,
			// not only at quit (Steam can end the game without one)
			if (cfg_dirty && !menu.is_open() && (quit_after < 0 || test_saves)) { if (save_settings()) cfg_dirty = false; }
			// the backup within a minute of changing (every 3 s before: the arcade's bookkeeping
			// changes it in play, so it was written that often; and at quit as ever)
			static uint64_t next_nvram = SDL_GetTicksNS() + 60000000000ull;
			if (SDL_GetTicksNS() >= next_nvram) { next_nvram = SDL_GetTicksNS() + 60000000000ull; save_nvram(false); }
		}
	}
	finish_async(false);
	msound.stop();
	online.stop();   // (closed in a match: the other side is told at once, not after a silence)
	Online::flush(1500);   // (and the lobby told: a room closed, a search left)
	rec_stop();
	links::stopped(data);
	if (g_match_board) {   // (this player's own NVRAM, not the host's, saved)
		memcpy(m.backup_ram().data(), own_backup.data(), std::min(own_backup.size(), m.backup_ram().size()));
		memcpy(m.eeprom_cells(), own_eeprom, sizeof own_eeprom);
		g_match_board = false;
	}
	saver.finish();   // (an autosave still being written, done before the last save: the same files)
	save_nvram(true);
	remember_window();
	if (cfg_dirty && (quit_after < 0 || test_saves)) save_settings();
	for (auto &[id, j] : vpads) { if (j) SDL_CloseJoystick(j); SDL_DetachVirtualJoystick(id); }
	if (chime_stream) SDL_DestroyAudioStream(chime_stream);
	crt_paint.release();
	g3_free();
	g3.dev.reset();   // (the GPU's 3D before the renderer whose device it may share)
	marker_done(true);
	SDL_Quit();
	return 0;
}
