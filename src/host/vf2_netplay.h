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
#include <deque>
#include <functional>
#include <map>
#include <string>
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
// Every release plays the last one online (tools/compat/compat.sh in the private repo says COMPATIBLE first): what's
// new for a match goes behind an option that adds to the online version (RANDOM_TAG below), the version itself as the
// release's before it while the option's off.
inline constexpr const char *NET_VERSION = "v0.1.16";
// Online > Settings > Your fighter Random or Seed (Cheats' random fighters online): the online version with this after
// it, so the lobby, Random and the handshake meet only others who have a random fighter too, and such a match plays the
// random fighters' protocol (start_blob_random's blob, each fighter in the hello, their state in the save states).
// Normal: the plain version, the match exactly as v0.1.16's (start_blob's "VF2B" blob, no fighter in the hello, the
// same save states), so it plays v0.1.16. A change to the random fighters' match (the book, its order, the blob) is
// a new tag ("+random2"), the plain version untouched
inline constexpr const char *RANDOM_TAG = "+random";
bool random_version(const std::string &online_version);   // (the version has RANDOM_TAG: the random fighters' protocol)
// Random and room matches across Your fighter (vf3-recomp #25's, for VF2): a build that has this sends the lobby, besides
// its own online version, a pool (the version's base with MIX_TAG) and the flags of an older version's it can play that
// version's way ("random"). The lobby answers with the match's terms ("as"): the pool's, the random fighters' protocol
// whatever each chose (each player's own fighter in it: Normal is "" there, the select as ever), or one side's own
// online version, played as a build of that version plays it (a plain one: Normal fighters, v0.1.16's match). The
// handshake compares the terms. A change to the random fighters' match is a new tag here too ("+mix2")
inline constexpr const char *MIX_TAG = "+mix1";
std::string pool_version(const std::string &online);
// (the terms' match: random_fighters for the pool's or a RANDOM_TAG version's; false: not one this build plays)
bool terms_of(const std::string &terms, const std::string &own, const std::string &pool, bool &random_fighters);
// The online version a build plays as: NET_VERSION for a release (its version a release's name: v0.1.16, or with a
// word after it), else the build's own (git describe's between releases, a bare commit, "dev"): a test build, which
// may carry a change to the game, plays only the same build
std::string online_version(const std::string &build);

// A hash of the ROMs' contents as loaded (before any patch)
uint64_t content_hash(const model2a &board);
// Every online match first to 3 rounds: the host's blob's copy of its backup RAM has Rounds to win (vs player) at this
// (model2a::set_vs_rounds_in, its CRC with it), the host's own setting untouched. A guest plays the blob's settings as
// they are (Adapter::begin), so a guest of any version plays it, in step; an older host's blob has its own setting
inline constexpr int ONLINE_VS_ROUNDS = 3;
// The host's starting blob ("VF2B", v0.1.16's layout): its backup RAM (Rounds to win (vs player) ONLINE_VS_ROUNDS) and
// EEPROM, its secret character (0: none; Cheats' choice) and its place in the stages' list (a fresh one each call: STAGE)
std::vector<uint8_t> start_blob(const model2a &board, int secret = 0);
// A random fighters' match's ("VF2C", only between two RANDOM_TAG versions): start_blob's, then the match's random seed
// (Cheats' random fighters made alike on both sides: Random's draws, the colours) and the host's fighter (player 1's:
// fighter_extra)
std::vector<uint8_t> start_blob_random(const model2a &board, int secret, const std::string &host_fighter, uint32_t seed);

// A watched match's start (src/net/watch.h, the host's to the lobby): the session's blob, then the guest's fighter
// (player 2's Identity::extra, which only the guest's hello carries) and its length, then "VF2W", so a watcher plays
// both fighters as the players do. Every new host's, a random fighters' match or not (the plain one: no letters)
std::vector<uint8_t> watch_blob(const std::vector<uint8_t> &blob, const std::string &guest_extra);
bool watch_blob_split(const std::vector<uint8_t> &watched, std::vector<uint8_t> &blob, std::string &guest_extra);

// A player's fighter as the handshake carries it (Identity::extra), VF1's: "" the select as ever, "R" a random fighter
// (a new one at every select), "S<place>" a named fighter's (its place in the book, model2a::random_fighter_names: the
// same on any machine with the ROM and this online version; a change to the book, or its order, changes RANDOM_TAG). From Cheats' setting (model2a::set_random_player's: "",
// "*" or a name; a name the book hasn't plays as Random, as offline), and back (false for one that isn't any of them)
std::string fighter_extra(const std::string &random_player_setting, model2a &board);
bool fighter_setting(const std::string &extra, model2a &board, std::string &setting);
void set_players(model2a &board, const std::string fighters[2]);
// A fighter as a word under a player's name (the VS reveal): "" Normal, "RANDOM", a named fighter's name
std::string fighter_words(const std::string &extra, model2a &board);

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

// The select in lockstep (Session::set_lockstep) or not: off, the select played on guesses, as VF3's every public
// release. On, it waited for the other side's inputs in the select, and after blips of its byte mid-match: VF3's
// v0.1.11-alpha check (28 Oct, 180 ms through a relay) dropped fights to 40 fps where guessing held 57.5. The inputs
// are the same either way (each side's own choice): the game, and the online version, unchanged
constexpr bool SELECT_LOCKSTEP = false;

// The fight's stage: work RAM 0x64 (0-9 the ten fighters' stages, 11 Dural's), which the game sets at each select
// from its count of fights (0x5b: Start sets it to 0, each select moves it on by one, 10 is Dural's stage and the
// next is 0 again) and the scene tools force (VF2_STAGE). A cold start counts from 0: every match's first fight was
// on the same stage. A match's count starts at the host's random place (the blob's last byte), set once, through
// the first select after Start (Adapter::advance), the count and the stage both; the game counts on from there.
// (Set through every select, each a frame after the game had moved the count on, a later select's stage had the
// textures the game had already started loading for its own: one stage's shapes in another's textures, which put
// themselves right over a few seconds.) The first select's are loaded later, so its setting holds. Its count is
// 0 from Start, and no one has won yet (STREAK, the champion's wins: 0 only there; the count is 0 again after
// Dural's stage, the game's own). A 2P select only (VERSUS: 1 there, 0 in a 1P game's, whose count is the CPU's
// ladder). Checked over a 30,000-frame versus of 8 fights in vf2net: 3 (the place), 4 ... 9, Dural's; in step
constexpr uint32_t STAGE = 0x64, FIGHTS = 0x5b, STREAK = 0xa2, VERSUS = 0x56;
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

// The match's sound thread's queue of a watcher's frames caught up unseen (Adapter::sound_unheard), for its sound
// board to run through unheard, in time, alongside the catch-up, as fast as it can (so the music a cue started is
// where the players' is), and the bytes sent heard while some wait (they go with the next). Bounded, as the thread can
// fall behind a long catch-up (a watcher come in an hour into a session of rematches: 200,000 frames): past
// UNHEARD_TIMED frames waiting, the oldest are let go (their notes, sounds long over; the rest, a control's last value
// and the like, go with the next), but for those round the music's last cue (MUSIC_CUE): the MUSIC_CUE_BEFORE before
// it (the scene's set-up, sent about 110 frames before a stage's song: without it, a stage's was silent; no more than
// UNHEARD_TIMED, the most kept as the cue comes) and the MUSIC_CUE_FRAMES from it. Bytes sent in a burst, out of time,
// instead lost the song (the sound program takes its time over a song's start and its set-up). So the board's at most
// 420 frames' work behind when the picture's up, under half a second's (VF2's sound board takes about 1 ms a frame),
// the heard sound after it; the music then from its song's start, not where the players are in it
constexpr size_t UNHEARD_TIMED = 180, MUSIC_CUE_BEFORE = 180, MUSIC_CUE_FRAMES = 60;
// A watcher catching up from the match's start lets none go: the catch-up waits while the sound board has more than
// UNHEARD_AHEAD frames to run (Adapter::sound_unheard_waiting; near the live point, while it has any), and the queue
// holds UNHEARD_CATCH_UP (more than that and a pass's frames, Spectator::Config::catch_up_frames). Letting frames go
// there (their notes dropped, the rest folded into the next) left the board in a state the game never made, out of step
// with the players' sound at every join point tried (tools/vf2watchsound): VF2 v0.1.20's watcher come 7 minutes late
// heard the music stuck on one note and no effects till the next round's song. The board now runs every frame a
// from-the-start watcher's does, in order; only a snapshot's tail (the frames before it, given at once) is still cut
// down to the queue's bound, as it always was
constexpr size_t UNHEARD_AHEAD = 120, UNHEARD_CATCH_UP = 1200;
// (a snapshot's sound bytes: the frames before it the host keeps, about 4 minutes, past a fight's song's cue; "VF2T")
constexpr size_t SOUND_TAIL = 14400;
constexpr uint32_t TAIL_MAGIC = 0x54324656;
constexpr uint8_t MUSIC_CUE[2] = { 0xad, 0x10 };   // (AD 10 nn: the song, from the bytes a match sends: one at each scene, the stage's as its fight starts)
class UnheardQueue
{
public:
	struct Piece
	{
		enum Kind : uint8_t { frame, heard };   // (a frame's bytes, in its time; heard ones, with the next)
		Kind kind = frame;
		std::vector<uint8_t> bytes;
		uint64_t n = 0;                // (a frame's: which, from the first)
		uint8_t st0 = 0;               // (the running status its bytes start with)
		std::vector<uint8_t> carry;    // (what's kept of frames let go just before it, to send first: UnheardQueue::carry)
	};
	void frame(const std::vector<uint8_t> &bytes);   // (a frame unheard: its bytes, or none)
	void heard(const std::vector<uint8_t> &bytes);
	bool next(Piece &p);                             // (the oldest, taken off)
	bool empty() const { return q.empty(); }
	size_t frames() const { return timed; }          // (the frames waiting)
	void clear();
	size_t most_timed = UNHEARD_TIMED, before_cue = MUSIC_CUE_BEFORE, cue_timed = MUSIC_CUE_FRAMES;   // (tests may change them)
	uint64_t dropped = 0;                                                                         // (frames let go)
private:
	std::deque<Piece> q;
	size_t timed = 0;
	uint64_t frames_in = 0, cue = UINT64_MAX;   // (the frames put in; the last cue's frame)
	uint8_t st = 0, d1 = 0;                     // (the MIDI read so far, for the cues)
	int need = 0, got = 0;
	bool strip = false;                // (frames let go just now: the next bytes from a status byte)
	std::vector<uint8_t> carry_next;   // (the carry for the next piece put in)
	void look(const std::vector<uint8_t> &bytes, bool is_frame);
	void strip_front(size_t i);
	static void carry(std::vector<uint8_t> &c, const std::vector<uint8_t> &bytes, uint8_t st);
};
// The data bytes after a MIDI status byte (none for one without, or for a byte that isn't a status byte)
inline int midi_data_bytes(uint8_t st) { const int k = st >> 4; return k == 0xc || k == 0xd ? 1 : k >= 0x8 && k <= 0xe ? 2 : 0; }
// A piece of the queue run through the sound board alone (the match's sound thread's), its output thrown away: a
// frame's bytes and a frame; heard bytes only put in, to go with the next
void run_unheard(model2a &board, const UnheardQueue::Piece &piece);

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
	// (a watcher catching up unseen: the frames' bytes for the sound board not played as they run, a burst, but given
	// to sound_unheard, a frame's at a time (none too), for the sound board to run through unheard, in time: they're
	// commands sent once (a stage's music is started by one); dropped, a watcher who came in mid-fight heard its
	// effects and voices but no music till the next cue. Only what's heard, never the game)
	bool mute = false;
	std::function<void(const std::vector<uint8_t> &)> sound_unheard;   // (none: they're dropped)
	// (and how many of them the sound board has still to run: a watcher catching up waits for it past
	// UNHEARD_AHEAD (Online::step), so none is let go (UnheardQueue's bound) and its board is a from-the-start
	// watcher's when the picture's up; 0 none waiting. None set: it doesn't wait)
	std::function<size_t()> sound_unheard_waiting;
	// (a watcher's: sound_begin called as its first frame runs, before its bytes go anywhere, for the host to start
	// the sound board (boot_sound's ready), so the frames it catches up unseen run through it. begin sets it off)
	bool sound_starts = false;
	std::function<void()> sound_begin;

	bool begin(const std::vector<uint8_t> &blob) override;
	void save(std::vector<uint8_t> &state) override;
	bool load(const std::vector<uint8_t> &state) override;
	void advance(const uint16_t input[2], bool resim) override;
	// (the state's hash without its header: that's this board's own bookkeeping, not the game's)
	uint64_t hash(const std::vector<uint8_t> &state);
	size_t hash_skip() const override { return model2a::STATE_HEADER; }
	// A watched match's snapshot (src/net/watch.h): this build's states (its online version, the state's version and
	// size); with it, the host's sound bytes of the frames before it (sound_log: each frame's whole messages as its last
	// run had them, the last SOUND_TAIL), and where its bytes' messages stood, so a watcher loading it runs them through
	// its sound board unheard as one catching up from the start would (the stage's song from its cue, UnheardQueue)
	uint32_t snap_format() const override;
	void snap_extra(uint32_t frame, std::vector<uint8_t> &out) override;
	// (a snapshot's state, any host's: refused unless the board would make it (model2a::state_sane, the reader's checks)
	// and its frame is the snapshot's; refused, the board and this as they were)
	bool load_snap(const std::vector<uint8_t> &state, const std::vector<uint8_t> &extra, uint32_t frame) override;
	std::pair<size_t, size_t> snap_state_bytes() const override;
	bool sound_log = false;   // (the host's, while its match may be watched)
	// The frame's bytes for the sound board to send now, whole MIDI messages only, each with its status byte (a
	// message the frame's bytes end in the middle of goes with the frame that finishes it): the first time a frame
	// runs, all of them; run again after a rollback, those it has that weren't sent for it (late). Issue #35: a frame
	// run again had sent nothing if its first run had sent anything (the right timeline's sounds and music cues lost),
	// and a message cut at a frame's end could meet another timeline's bytes (a status byte with another's data, data
	// bytes under another message's running status): measured in vf2net at 20 ms, delay 0, 20,000 frames, 2-7 cut
	// short and 1-2 lost a side; now none of either, only a wrong guess's own sounds extra (they can't be taken back)
	void take_sound(std::vector<uint8_t> &out);
	// The match's boot's bytes for the sound board, a frame's each, whole messages (begin): a sound board made for the match runs a
	// frame for each with them as it starts, as it would have booted alongside this board, before the match's own
	std::vector<std::vector<uint8_t>> boot_sound;
	int stage_offset = 0;   // (the host's place in the stages' list, from its blob)
	int secret() const { return started_secret; }   // (the match's secret character, from the host's blob)
	// The fighters (Online sets these before the session): whether the match is a random fighters' one (both sides'
	// online version RANDOM_TAG's: start_blob_random's blob; else v0.1.16's match, start_blob's, no fighters), this
	// side's choice (fighter_extra), and whether it hosts (player 1, its choice in its blob) or joins (player 2, its
	// choice in its hello)
	bool random_fighters = false;
	std::string own_extra;
	bool hosting = false;
	std::vector<uint8_t> host_blob(int secret = 0);   // (start_blob, or start_blob_random with this side's fighter and a fresh seed)
	void peer_extra(const std::string &extra) override { their_extra = extra; }
	std::string fighters[2];   // (the match's, as begin set them: "", "*" or "S<place>")
	std::string extras[2];     // (and as the handshake carries them, fighter_extra's: "", "R" or "S<place>")

	// tests: the state's hash after each frame a multiple of this, the last time it ran (0: none kept)
	uint32_t checkpoint_every = 0;
	std::map<uint32_t, uint64_t> checkpoints;

private:
	model2a &board;
	uint32_t frame = 0;   // the session's frame the board is at
	std::vector<uint8_t> pending;   // the bytes the game sent the sound board in the frame running
	bool collecting = false;
	// per frame (the last 64): where its bytes' messages stood as it started (the running status, an unfinished
	// message's bytes), from the frame before's last run, and the messages the sound board was sent for it
	struct sound_frame { uint8_t status = 0; std::vector<uint8_t> part; std::vector<std::vector<uint8_t>> sent; };
	std::map<uint32_t, sound_frame> sound_frames;
	std::vector<uint8_t> check_buf;
	bool sound_taken = false;
	bool dropped = false;   // (bytes for the sound board muted with no sound_unheard: none heard since)
	std::deque<std::pair<uint32_t, std::vector<uint8_t>>> logged;   // (sound_log's: frame, its messages)
	std::vector<std::vector<uint8_t>> tail;   // (a snapshot's sound bytes, for the sound board as its first frame after runs)
	int started_secret = 0;
	std::string their_extra;
};

} // namespace vf2net
