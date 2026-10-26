// VF2 in a netplay session (src/net): the board as the session's Game. What both sides start from (the host's
// backup RAM and EEPROM, so the same arcade settings and rankings), how they start (a cold start, the PC way on
// both: free play, the FREE PLAY notices hidden, the boot run unseen), each player's switches as an input word, and
// the bytes for the sound board under rollback. VF3's vf3_netplay, for VF2's board.
//
// In a match the board runs with its sound board held and the byte link (as MAME's reference runs it: the i960
// never hears back from the sound board, so it isn't part of what the two sides must agree on). The bytes the game
// sends the sound board go to a sound board of their own (take_sound), which plays to the real clock.
#pragma once
#include "../machine/model2a.h"
#include "../net/session.h"
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace vf2net {

constexpr int BOOT_FRAMES = 1200;   // the boot, unseen, up to the SEGA logo at most (with the WARNING shortened, ~100)

// A player's switches as the session's input word (the stick, Punch, Kick, Guard, Start and Coin; never test or
// service), from the board's ports (active low, as the cabinet's) and back. Start is the press the host made of
// it (the PC way's 6 frames), not a key held: the word is what the game gets.
constexpr uint16_t START_BUTTON = 256, COIN_BUTTON = 512;
uint16_t pack(uint8_t in0, uint8_t player_port, int player);
void unpack(const uint16_t input[2], model2a &board);

// The online version both sides compare in the handshake (with the ROMs' hash), apart from the release's: changed
// only with what a match depends on (the board, the recompiler, their compiler flags, this adapter, online.cpp's
// match settings, src/net's protocol), so a release that changes only the picture or the menus still plays the one
// before. Named after the release that brings it; VF2's first online release names it (docs/RELEASING-ALL.md).
inline constexpr const char *NET_VERSION = "vf2-online-1";
// The online version a build plays as: NET_VERSION for a release (its version a release's name: v0.1.16, or with a
// word after it), else the build's own (git describe's between releases, a bare commit, "dev"): a test build, which
// may carry a change to the game, plays only the same build
std::string online_version(const std::string &build);

// A hash of the ROMs' contents as loaded (before any patch)
uint64_t content_hash(const model2a &board);
// The host's starting blob: its backup RAM and EEPROM, its secret character (0: none; Cheats' choice) and its place in the
// stages' list (a fresh one each call: STAGE)
std::vector<uint8_t> start_blob(const model2a &board, int secret = 0);

// The settings a match plays with on both sides, whatever each has offline (they change the game's program or
// its timing): the scenery culling widescreen uses (so a 16:9 and a 4:3 player run the same game), the sound
// board held, the byte link, the WARNING screen shortened, the FREE PLAY notices hidden
constexpr int MATCH_CULLING = 2;

// The character select on screen: the game's mode (work RAM 0x2b, model2a::game_mode) 7 from the first frame after
// Start to the VS screen, the challenger's select too, and never elsewhere (3 the title and the attract, 4-6 Start's
// frames, 8 the VS screen, 9 the fights; held frame by frame over a 2P run and a challenger's way back in). A match
// plays it in lockstep (vfnet::Session::set_lockstep): no frame on a guess, so a face never flashes as a guess is
// put right. As VF3's.
inline int scene_byte(const model2a &board) { return board.game_mode(); }
inline bool in_select(const model2a &board) { return scene_byte(board) == 7; }

// The fight's stage: work RAM 0x64 (0-9 the ten fighters' stages, 10 and 11 Dural's), which the game sets at the
// select from its count of fights (0x5b) and the scene tools force (VF2_STAGE). A cold start counts from 0: every
// match's first fight was on the same stage. A match's stages go on from the host's random place (the blob's last
// byte), set through a versus's select and VS screen (Adapter::advance). The count is what's set, not the stage:
// the game loads the stage's textures from the count partway through the select, and the stage alone set gave
// one stage's shapes in another's textures (Wolf's fence black). Each one from the last fight's stage (LAST_STAGE,
// the same all through the select), so setting the count doesn't move it; a 2P select alone (VERSUS: 1 there, 0
// in a 1P game's, whose count is the CPU's ladder)
constexpr uint32_t STAGE = 0x64, FIGHTS = 0x5b, LAST_STAGE = 0x3058, VERSUS = 0x56;
constexpr int STAGES = 10;

// The score between the two players, read from the game's own RAM after each frame (as Fightcade's detectors do):
// each player's rounds won in the match on now (both back to 0 when it's over), and which players are playing (a
// side not playing is the CPU's). A match counts only if both were playing at each round it was won (not the
// winner's fights against the CPU after it, nor one a challenger broke into); each reading only once it's held 30
// frames, longer than any rollback, so a guessed frame never scores. VF2's places (tools/vf2detect.cpp, checked
// against the HUD's round markers over four matches and a challenger who joined and lost): work RAM 0x4f and 0x51,
// each player's rounds; 0x4c the players in a fight, 2 both, 1 player 2 alone, 0 player 1 alone (or no one: the
// game mode then says, 9 a fight).
struct Score
{
	int matches[2] = {}, rounds[2] = {};   // between the two players: matches won, and rounds won in them
	void frame(const model2a &board);
	int players() const { return playing; }          // (who's in the game, as held: bit 0 player 1, bit 1 player 2)
	uint8_t players_now() const { return now[2]; }   // (as the RAM has it this frame, not yet held: when one went out)
	int round_ends = 0;   // (each round's end, any players': a moment no one's in control, while the game shows who won)
	static constexpr uint32_t ROUNDS_P1 = 0x4f, ROUNDS_P2 = 0x51, PLAYING = 0x4c;   // (work RAM offsets)
	static constexpr bool known() { return ROUNDS_P1 && ROUNDS_P2 && PLAYING; }
	static uint8_t players_in(const model2a &board);   // (bit 0 player 1, bit 1 player 2, from PLAYING and the game mode)
private:
	uint8_t seen[2] = {}, now[3] = {}, playing = 0;
	int held = 0;
	bool versus = false;   // (the match on now: both playing at every round so far)
};

class Adapter : public vfnet::Game
{
public:
	explicit Adapter(model2a &board) : board(board) {}
	~Adapter() override { board.on_uart_tx = nullptr; }

	// Set by the host program:
	// one frame (the ports are set): resim, a frame run again (no picture, no sound needed). Default:
	// board.run_frame(). Whatever takes the frame's bytes for the sound board calls take_sound (else the adapter
	// does after the frame, and gives them to sound_out).
	std::function<void(bool resim)> run;
	std::function<void()> program_changed;     // the program was patched (the recompiled code was made again)
	std::function<void()> settle;              // before a save or load: anything running alongside the board waits
	std::function<void(const std::vector<uint8_t> &)> sound_out;   // bytes for the sound board
	std::function<void()> on_begin;            // a match's start, before the board is set up for it (keep the player's NVRAM and settings)
	std::function<void()> booting;             // now and then in the match's boot (seconds of frames: the window's events)

	bool begin(const std::vector<uint8_t> &blob) override;
	void save(std::vector<uint8_t> &state) override;
	bool load(const std::vector<uint8_t> &state) override;
	void advance(const uint16_t input[2], bool resim) override;
	// (the state's hash without its header: that's this board's own bookkeeping, not the game's)
	uint64_t hash(const std::vector<uint8_t> &state);
	size_t hash_skip() const override { return model2a::STATE_HEADER; }
	// The frame's bytes for the sound board to send now: all of them the first time a frame runs; run again after a
	// rollback, none if its bytes went already (the sound board can't take back what it played: a wrong guess's
	// sound stays), and if it sent none and has some now, those (late)
	void take_sound(std::vector<uint8_t> &out);
	// The match's boot's bytes for the sound board, a frame's each (begin): a sound board made for the match runs a
	// frame for each with them as it starts, as it would have booted alongside this board, before the match's own
	std::vector<std::vector<uint8_t>> boot_sound;
	int stage_offset = 0;   // (the host's place in the stages' list, from its blob)
	int secret() const { return started_secret; }   // (the match's secret character, from the host's blob)

	// tests: the state's hash after each frame a multiple of this, the last time it ran (0: none kept)
	uint32_t checkpoint_every = 0;
	std::map<uint32_t, uint64_t> checkpoints;

private:
	model2a &board;
	uint32_t frame = 0;   // the session's frame the board is at
	std::vector<uint8_t> pending;   // the bytes the game sent the sound board in the frame running
	bool collecting = false;
	// per frame: the bytes the sound board was sent for it (a frame run again sends only what wasn't)
	std::map<uint32_t, std::vector<uint8_t>> sound_sent;
	std::vector<uint8_t> check_buf;
	bool sound_taken = false;
	int started_secret = 0;
};

} // namespace vf2net
