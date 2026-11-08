#include "vf2_netplay.h"
#include <cctype>
#include <algorithm>
#include <chrono>
#include <cstring>

namespace vf2net {

namespace {

// IN0's bits for each player (active low on the board)
constexpr uint8_t IN0_COIN[2] = { 0x01, 0x02 }, IN0_START[2] = { 0x10, 0x20 };

constexpr uint32_t BLOB_MAGIC = 0x42324656;          // "VF2B" (v0.1.16's: no random fighters)
constexpr uint32_t BLOB_MAGIC_RANDOM = 0x43324656;   // "VF2C" (a random fighters' match: the seed, the host's fighter)

} // namespace

uint16_t pack(uint8_t in0, uint8_t port, int p)
{
	return uint16_t(uint8_t(~port) | (in0 & IN0_START[p] ? 0 : START_BUTTON) | (in0 & IN0_COIN[p] ? 0 : COIN_BUTTON));
}

void unpack(const uint16_t input[2], model2a &b)
{
	uint8_t in0 = 0xff;   // (test and service never pressed)
	for (int p = 0; p < 2; p++) {
		if (input[p] & START_BUTTON) in0 &= uint8_t(~IN0_START[p]);
		if (input[p] & COIN_BUTTON) in0 &= uint8_t(~IN0_COIN[p]);
	}
	b.m_in0 = in0;
	b.m_in1 = uint8_t(~input[0]);
	b.m_in2 = uint8_t(~input[1]);
	b.m_sw = 0xff;
	b.set_debug_mode(false);   // (Arcade settings > Debug mode: never online)
}

std::string online_version(const std::string &b)
{
	// a release's name: v, three numbers, and maybe a word after them (-alpha, -online-preview), not git describe's
	// -<commits>-g<commit> (a build between releases)
	size_t i = 0;
	auto number = [&] { const size_t at = i; while (i < b.size() && isdigit((unsigned char)b[i])) i++; return i > at; };
	bool release = !b.empty() && b[i++] == 'v' && number() && i < b.size() && b[i++] == '.' && number() && i < b.size() && b[i++] == '.' && number();
	if (release && i < b.size()) {
		release = b[i] == '-' && i + 1 < b.size() && islower((unsigned char)b[i + 1]);
		for (size_t k = i + 1; release && k < b.size(); k++)
			release = islower((unsigned char)b[k]) || isdigit((unsigned char)b[k]) || b[k] == '-';
		// (a name's last parts "-<n>-g<hex>": git describe's between releases)
		const size_t g = b.rfind("-g");
		if (release && g != std::string::npos && g > i && g + 2 < b.size()) {
			size_t d = b.rfind('-', g - 1);
			const std::string n = b.substr(d + 1, g - d - 1), h = b.substr(g + 2);
			const bool num = !n.empty() && n.find_first_not_of("0123456789") == std::string::npos;
			const bool hex = h.find_first_not_of("0123456789abcdef") == std::string::npos;
			if (num && hex) release = false;
		}
	}
	return release ? NET_VERSION : b;
}

uint64_t content_hash(const model2a &b) { return b.rom_hash(); }

bool random_version(const std::string &v)
{
	const size_t n = strlen(RANDOM_TAG);
	return v.size() > n && v.compare(v.size() - n, n, RANDOM_TAG) == 0;
}

std::string pool_version(const std::string &online)
{
	return online.substr(0, online.find('+')) + MIX_TAG;
}

bool terms_of(const std::string &terms, const std::string &own, const std::string &pool, bool &random_fighters)
{
	random_fighters = false;
	if (terms == pool) { random_fighters = true; return true; }
	const std::string base = own.substr(0, own.find('+'));
	if (terms == base) return true;
	if (terms == base + RANDOM_TAG) { random_fighters = true; return true; }
	return false;
}

// [magic "VF2B"][backup RAM][EEPROM][secret][stage]
std::vector<uint8_t> start_blob(const model2a &b, int secret)
{
	std::vector<uint8_t> out(4);
	memcpy(out.data(), &BLOB_MAGIC, 4);
	// the host's backup RAM with Rounds to win (vs player) at ONLINE_VS_ROUNDS, in this copy only (the host's own setting
	// kept for offline): the guest plays the blob's as it is, so every match is first to 3, against older builds too
	std::vector<uint8_t> backup = b.backup_ram();
	model2a::set_vs_rounds_in(backup, ONLINE_VS_ROUNDS);
	out.insert(out.end(), backup.begin(), backup.end());
	const uint8_t *e = reinterpret_cast<const uint8_t *>(b.eeprom_cells());
	out.insert(out.end(), e, e + 64 * 2);
	out.push_back(uint8_t(secret));
	// the host's random place in the stages' list (stage_for): a fresh one each match
	const uint64_t t = uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()) * 2654435761u;
	out.push_back(uint8_t((t >> 24) % STAGES));
	return out;
}

// [magic "VF2C"][backup RAM][EEPROM][secret][stage][seed: 4][the host's fighter's length][its letters]
std::vector<uint8_t> start_blob_random(const model2a &b, int secret, const std::string &host_fighter, uint32_t seed)
{
	std::vector<uint8_t> out = start_blob(b, secret);
	memcpy(out.data(), &BLOB_MAGIC_RANDOM, 4);
	for (int k = 0; k < 4; k++) out.push_back(uint8_t(seed >> (8 * k)));
	const std::string f = host_fighter.substr(0, vfnet::MAX_EXTRA);
	out.push_back(uint8_t(f.size()));
	out.insert(out.end(), f.begin(), f.end());
	return out;
}

namespace { constexpr uint32_t WATCH_TAIL = 0x57324656; }   // "VF2W"

std::vector<uint8_t> watch_blob(const std::vector<uint8_t> &blob, const std::string &guest_extra)
{
	std::vector<uint8_t> out = blob;
	const std::string x = guest_extra.substr(0, vfnet::MAX_EXTRA);
	out.insert(out.end(), x.begin(), x.end());
	out.push_back(uint8_t(x.size()));
	for (int k = 0; k < 4; k++) out.push_back(uint8_t(WATCH_TAIL >> (8 * k)));
	return out;
}

bool watch_blob_split(const std::vector<uint8_t> &w, std::vector<uint8_t> &blob, std::string &guest_extra)
{
	uint32_t tail = 0;
	if (w.size() < 5) return false;
	memcpy(&tail, w.data() + w.size() - 4, 4);
	const size_t n = w[w.size() - 5];
	if (tail != WATCH_TAIL || n > size_t(vfnet::MAX_EXTRA) || w.size() < 5 + n) return false;
	const size_t at = w.size() - 5 - n;
	guest_extra.assign(reinterpret_cast<const char *>(w.data()) + at, n);
	for (char c : guest_extra) if (c < 0x21 || c > 0x7e) return false;   // (printable, as the hello's)
	blob.assign(w.begin(), w.begin() + ptrdiff_t(at));
	return true;
}

std::string fighter_extra(const std::string &setting, model2a &b)
{
	if (setting.empty()) return "";
	if (setting == "*") return "R";
	std::string n;
	for (char ch : setting) if (ch != ' ') n += char(toupper(uint8_t(ch)));
	const auto &names = b.random_fighter_names();
	const auto it = names.find(n);
	return it == names.end() ? "R" : "S" + std::to_string(it->second);   // (a name the book hasn't: Random, as offline)
}

bool fighter_setting(const std::string &x, model2a &b, std::string &setting)
{
	setting.clear();
	if (x.empty()) return true;
	if (x == "R") { setting = "*"; return true; }
	const bool digits = x.size() >= 2 && x.size() <= 6 && x[0] == 'S' && x.find_first_not_of("0123456789", 1) == std::string::npos;
	if (!digits) return false;
	const long v = strtol(x.c_str() + 1, nullptr, 10);
	if (v < 1 || size_t(v) > b.random_fighter_names().size()) return false;
	setting = x;   // (a named fighter by its place: set_players)
	return true;
}

std::string fighter_words(const std::string &x, model2a &b)
{
	if (x == "R") return "RANDOM";
	if (x.size() > 1 && x[0] == 'S') {
		const uint32_t place = uint32_t(strtoul(x.c_str() + 1, nullptr, 10));
		for (const auto &[name, at] : b.random_fighter_names())
			if (at == place) return name;
		return "SEED " + x.substr(1);
	}
	return "";
}

void set_players(model2a &b, const std::string fighters[2])
{
	for (int p = 0; p < 2; p++)
		if (fighters[p].size() > 1 && fighters[p][0] == 'S') b.set_random_player_place(p, uint32_t(strtoul(fighters[p].c_str() + 1, nullptr, 10)));
		else b.set_random_player(p, fighters[p]);
}

std::vector<uint8_t> Adapter::host_blob(int secret)
{
	if (!random_fighters) return start_blob(board, secret);
	uint32_t seed = uint32_t(uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()) * 2654435761u) ^ 0x9e3779b9u;
	if (!seed) seed = 1;
	return start_blob_random(board, secret, own_extra, seed);
}

// (a frame's bytes go at once, a guessed frame's as much as a certain one's, so no sound waits for the other
// side's input; whole messages, see the header)
namespace {
// MIDI bytes into whole messages: status, running status, an unfinished message carried in `part`
void midi_messages(const std::vector<uint8_t> &bytes, uint8_t &status, std::vector<uint8_t> &part, std::vector<std::vector<uint8_t>> &out)
{
	for (const uint8_t x : bytes) {
		if (x >= 0xf8) { out.push_back({ x }); continue; }   // (a timing byte: alone, wherever it comes)
		if (x & 0x80) {   // (a message's start: one cut short before it is dropped)
			part.assign(1, x);
			status = x >= 0xf0 ? 0 : x;   // (a system message ends running status)
			if (!midi_data_bytes(x)) { out.push_back(part); part.clear(); }
			continue;
		}
		if (part.empty()) {
			if (!status) { out.push_back({ x }); continue; }   // (data with no status known: as it came)
			part.assign(1, status);   // (running status: another of the last kind)
		}
		part.push_back(x);
		if (int(part.size()) == 1 + midi_data_bytes(part[0])) { out.push_back(part); part.clear(); }
	}
}
} // namespace

void Adapter::take_sound(std::vector<uint8_t> &out)
{
	sound_taken = true;
	out.clear();
	sound_frame &f = sound_frames[frame];
	uint8_t status = f.status;
	std::vector<uint8_t> part = f.part;
	std::vector<std::vector<uint8_t>> msgs;
	midi_messages(pending, status, part, msgs);
	pending.clear();
	if (sound_log) {   // (this run's messages, whole: the frame's, once it's confirmed, for a snapshot)
		while (!logged.empty() && logged.back().first >= frame) logged.pop_back();   // (run again: from here, as it is now)
		std::vector<uint8_t> all;
		for (const auto &m : msgs) all.insert(all.end(), m.begin(), m.end());
		logged.push_back({ frame, std::move(all) });
		while (logged.size() > SOUND_TAIL + 64) logged.pop_front();
	}
	sound_frame &next = sound_frames[frame + 1];   // (where the next frame's bytes start: this run's, the latest)
	next.status = status;
	next.part = std::move(part);
	// the messages not sent for this frame yet (those sent that it doesn't have now can't be taken back: a wrong guess's
	// sound stays)
	std::vector<bool> matched(f.sent.size(), false);
	for (auto &m : msgs) {
		size_t k = 0;
		while (k < f.sent.size() && (matched[k] || f.sent[k] != m)) k++;
		if (k < f.sent.size()) { matched[k] = true; continue; }
		out.insert(out.end(), m.begin(), m.end());
		f.sent.push_back(std::move(m));
		matched.push_back(true);
	}
	while (!sound_frames.empty() && sound_frames.begin()->first + 64 < frame) sound_frames.erase(sound_frames.begin());
}

bool Adapter::begin(const std::vector<uint8_t> &blob)
{
	const size_t backup_n = board.backup_ram().size();
	// v0.1.16's blob ("VF2B": backup RAM, EEPROM, secret, stage), and a random fighters' match's after it ("VF2C": the
	// seed, the host's fighter)
	const size_t BASE = 4 + backup_n + 64 * 2 + 2, FIXED = BASE + 4 + 1;
	uint32_t magic = 0, seed = 0;
	if (random_fighters ? blob.size() < FIXED || blob.size() != FIXED + blob[FIXED - 1] : blob.size() != BASE) return false;
	memcpy(&magic, blob.data(), 4);
	if (magic != (random_fighters ? BLOB_MAGIC_RANDOM : BLOB_MAGIC)) return false;
	const int secret = blob[BASE - 2];
	stage_offset = blob[BASE - 1] % STAGES;
	if (secret != 0 && secret != 9 && secret != 11 && secret != 12) return false;
	// each player's fighter (Cheats' random fighters): the host's (player 1's) in its blob, the guest's in its hello
	fighters[0].clear();
	fighters[1].clear();
	extras[0].clear();
	extras[1].clear();
	if (random_fighters) {
		memcpy(&seed, blob.data() + BASE, 4);
		const std::string host_x(reinterpret_cast<const char *>(blob.data()) + FIXED, blob[FIXED - 1]);
		if (!fighter_setting(host_x, board, fighters[0]) || !fighter_setting(hosting ? their_extra : own_extra, board, fighters[1])) return false;
		extras[0] = host_x;
		extras[1] = hosting ? their_extra : own_extra;
	}
	const bool random = !fighters[0].empty() || !fighters[1].empty();
	if (settle) settle();
	if (on_begin) on_begin();
	board.set_debug_mode(false);   // (Arcade settings > Debug mode: never online, the match's boot neither)
	collecting = false;
	// the same game on both sides, whatever each plays offline: the program's patches (the culling widescreen
	// uses, the host's secret character, the WARNING shortened), the FREE PLAY notices hidden, the sound board
	// held and the byte link (they cut the CPUs' time differently)
	// (each only on the program known: the ROMs' hashes being the same, both sides patch alike)
	// (Cheats' random fighters off, the ROMs as loaded, and everything they knew forgotten: on again below from the
	// cold start, only when a player has one)
	board.random_fighters_forget();
	board.set_scenery_culling(MATCH_CULLING);
	board.set_secret_character(secret);
	board.shorten_warning();
	board.hide_free_play_text();
	if (program_changed) program_changed();
	board.set_sound(false);
	board.set_byte_link(true);
	// a cold start on both (work RAM and more survive a reset: a side that had played would start out of step),
	// from the host's backup RAM and EEPROM
	auto cold = [&](const uint8_t *backup, const uint8_t *eeprom) {
		board.power_on();
		memcpy(board.backup_ram().data(), backup, backup_n);
		memcpy(board.eeprom_cells(), eeprom, 64 * 2);
	};
	cold(blob.data() + 4, blob.data() + 4 + backup_n);
	// a board never set up writes its settings as it starts (the host's first game ever): wait for them
	for (int i = 0; i < 300 && board.coin_setting() < 0; i++) {
		board.run_frame();
		if (booting && i % 60 == 59) booting();
	}
	if (board.coin_setting() < 0 || board.assignment(model2a::game_assignment::vs_finish) < 0) return false;
	// free play, and the winner of a versus playing on (the arcade's: the loser presses Start for a rematch)
	board.set_coin_setting(model2a::COIN_FREE_PLAY);
	board.set_assignment(model2a::game_assignment::vs_finish, 0);
	const std::vector<uint8_t> backup = board.backup_ram();
	std::vector<uint8_t> eeprom(64 * 2);
	memcpy(eeprom.data(), board.eeprom_cells(), eeprom.size());
	cold(backup.data(), eeprom.data());
	// the players' random fighters (Cheats'), on only now, from the cold start's RAM, every random choice from the
	// host's seed: the same on both sides (Random opponents offline only: no computer's opponent is anyone's online)
	if (random) {
		board.set_random_seed(seed);
		set_players(board, fighters);
		board.set_random_opponents(false);
		if (!board.apply_random_fighters()) return false;
		board.random_fighters_decode();   // (every texture one may take, now: none decoded mid-match, a frame of 100 ms on one side)
		if (program_changed) program_changed();
	}
	// (the boot's bytes too, from this cold start, a frame's at a time: a sound board made for the match, alone, needs
	// its set-up, and booting as this board boots, as the arcade's does, to take it: all at once on its first frame,
	// its program not yet started, they were lost, and the character select had no music or sounds)
	board.on_uart_tx = [this](uint8_t b) { if (collecting) pending.push_back(b); };
	collecting = true;
	pending.clear();
	boot_sound.clear();
	for (int i = 0; i < BOOT_FRAMES && !board.past_boot(); i++) {
		board.run_frame();
		boot_sound.push_back(std::move(pending));
		pending.clear();
		if (booting && i % 60 == 59) booting();
	}
	frame = 0;
	if (const char *d = getenv("VF2_DUMP_BEGIN")) {   // (tests: the state the match starts from, and its parts)
		std::vector<uint8_t> st;
		std::vector<m2::StateSection> sec;
		board.save_state(st, &sec);
		if (FILE *f = fopen(d, "wb")) { fwrite(st.data(), 1, st.size(), f); fclose(f); }
		if (FILE *f = fopen((std::string(d) + ".txt").c_str(), "w")) { for (auto &x : sec) fprintf(f, "%s %zu\n", x.name, x.offset); fclose(f); }
	}
	started_secret = secret;
	sound_frames.clear();
	{   // (the boot's frames' bytes as whole messages, one cut at a frame's end with the frame that ends it, as VF1's;
		// the match's first frame's bytes start where they ended, so an unfinished one goes once, with them)
		sound_frame &f0 = sound_frames[0];
		for (auto &b : boot_sound) {
			std::vector<std::vector<uint8_t>> msgs;
			midi_messages(b, f0.status, f0.part, msgs);
			b.clear();
			for (auto &m : msgs) b.insert(b.end(), m.begin(), m.end());
		}
	}
	sound_starts = dropped = false;
	checkpoints.clear();
	return true;
}

uint64_t Adapter::hash(const std::vector<uint8_t> &state)
{
	const size_t skip = std::min(state.size(), model2a::STATE_HEADER);
	return vfnet::hash64(state.data() + skip, state.size() - skip);
}

uint32_t Adapter::snap_format() const
{
	const std::string k = std::string("vf2|") + NET_VERSION + "|" + std::to_string(model2a::STATE_VERSION) + "|" + std::to_string(board.backup_ram().size()) + "|" +
	                      std::to_string(board.workram().size()) + "|" + std::to_string(sizeof(void *));
	uint32_t h = 2166136261u;
	for (const char c : k) h = (h ^ uint8_t(c)) * 16777619u;
	return h ? h : 1;
}

// (the extra: "VF2T", the first frame, how many, each frame's bytes (u16 length, them), then where the frame's bytes'
// messages stand: the running status and an unfinished message's bytes (u8 length, them))
void Adapter::snap_extra(uint32_t at, std::vector<uint8_t> &out)
{
	out.clear();
	std::vector<const std::pair<uint32_t, std::vector<uint8_t>> *> v;
	for (const auto &e : logged)
		if (e.first < at && e.first + SOUND_TAIL >= at) v.push_back(&e);
	while (!v.empty() && v.front()->first + v.size() != at) v.erase(v.begin());   // (one frame after another up to it: none missing)
	auto u32 = [&](uint32_t x) { for (int k = 0; k < 4; k++) out.push_back(uint8_t(x >> (8 * k))); };
	u32(TAIL_MAGIC);
	u32(v.empty() ? at : v.front()->first);
	u32(uint32_t(v.size()));
	for (const auto *e : v) {
		const size_t n = std::min<size_t>(e->second.size(), 65535);
		out.push_back(uint8_t(n));
		out.push_back(uint8_t(n >> 8));
		out.insert(out.end(), e->second.begin(), e->second.begin() + ptrdiff_t(n));
	}
	const auto f = sound_frames.find(at);
	const uint8_t status = f != sound_frames.end() ? f->second.status : 0;
	const std::vector<uint8_t> part = f != sound_frames.end() ? f->second.part : std::vector<uint8_t>();
	out.push_back(status);
	out.push_back(uint8_t(std::min<size_t>(part.size(), 255)));
	out.insert(out.end(), part.begin(), part.begin() + ptrdiff_t(std::min<size_t>(part.size(), 255)));
}

std::pair<size_t, size_t> Adapter::snap_state_bytes() const
{
	// (the memories, then the small parts (the CPUs, the scheduler, the FIFOs and queues: tens of KB at most), then the frame)
	const size_t fixed = board.fixed_state_size();
	return { fixed + 4, fixed + (256u << 10) };
}

bool Adapter::load_snap(const std::vector<uint8_t> &state, const std::vector<uint8_t> &extra, uint32_t snap_frame)
{
	const auto [least, most] = snap_state_bytes();
	if (state.size() < least || state.size() > most) return false;
	// (a match with Cheats' random fighters: none taken. Their part of the state is the ROM's bytes they write as a
	// match goes, records and tables of addresses into the ROM that their code follows as its own; from another
	// process that code would follow anything. The watcher catches up from the match's start, as without snapshots)
	if (board.random_fighters_on()) return false;
	{   // (refused, the game as it was: its own state back, every part of it, not half another's)
		std::vector<uint8_t> was;
		save(was);
		if (!load(state) || frame != snap_frame) {
			load(was);
			return false;
		}
	}
	sound_frames.clear();
	tail.clear();
	dropped = false;
	// (the host's sound bytes before it, if they read whole; else none: the music from its next cue)
	size_t at = 0;
	auto need = [&](size_t n) { return extra.size() - at >= n; };
	auto u32 = [&] { uint32_t x = 0; for (int k = 0; k < 4; k++) x |= uint32_t(extra[at++]) << (8 * k); return x; };
	if (!need(12) || u32() != TAIL_MAGIC) return true;
	const uint32_t first = u32(), n = u32();
	if (n > SOUND_TAIL || uint64_t(first) + n != frame) return true;
	std::vector<std::vector<uint8_t>> got;
	for (uint32_t i = 0; i < n; i++) {
		if (!need(2)) return true;
		const size_t len = extra[at] | size_t(extra[at + 1]) << 8;
		at += 2;
		if (!need(len)) return true;
		got.emplace_back(extra.begin() + ptrdiff_t(at), extra.begin() + ptrdiff_t(at + len));
		at += len;
	}
	if (!need(2)) return true;
	const uint8_t status = extra[at], pl = extra[at + 1];
	at += 2;
	if (!need(pl)) return true;
	sound_frame &sf = sound_frames[frame];
	sf.status = status >= 0x80 && status < 0xf0 ? status : 0;
	sf.part.assign(extra.begin() + ptrdiff_t(at), extra.begin() + ptrdiff_t(at + pl));
	tail = std::move(got);
	return true;
}

void Adapter::save(std::vector<uint8_t> &state)
{
	if (settle) settle();
	board.save_state(state);
	// (the frame number with it: a load puts the adapter back at that frame)
	const size_t n = state.size();
	state.resize(n + 4);
	memcpy(state.data() + n, &frame, 4);
}

bool Adapter::load(const std::vector<uint8_t> &state)
{
	if (state.size() < 4) return false;
	if (settle) settle();
	memcpy(&frame, state.data() + state.size() - 4, 4);
	pending.clear();
	return board.load_state(state.data(), state.size() - 4);
}

void Adapter::advance(const uint16_t input[2], bool resim)
{
	unpack(input, board);
	// random stages, as VF3's: a versus's first select after Start, its count of fights (0 from Start) put at the
	// host's place in the list, the stage with it; the game counts on from there (from the board's RAM and the blob
	// alone: the same on both sides)
	{
		auto &ram = const_cast<std::vector<u8> &>(board.workram());
		if (board.game_mode() == 7 && ram[VERSUS] == 1 && ram[FIGHTS] == 0 && ram[STREAK] == 0 && stage_offset)
			ram[FIGHTS] = ram[STAGE] = u8(stage_offset);
	}
	if (sound_starts) {   // (a watcher's first frame)
		sound_starts = false;
		if (sound_begin) sound_begin();
	}
	if (!tail.empty()) {   // (a watcher's first frame after a snapshot: the host's frames before it, for the sound board unheard)
		if (sound_unheard)
			for (const auto &b : tail) sound_unheard(b);
		tail.clear();
	}
	sound_taken = false;
	if (run) run(resim);
	else board.run_frame();
	if (!sound_taken) {
		std::vector<uint8_t> out;
		take_sound(out);
		if (mute) {   // (a watcher catching up: unheard, in time)
			if (sound_unheard) sound_unheard(out);
			else dropped = dropped || !out.empty();
		} else if (!out.empty() && sound_out) {
			if (dropped)   // (bytes dropped before these: from a status byte, their first data bytes no message's start)
				out.erase(out.begin(), std::find_if(out.begin(), out.end(), [](uint8_t x) { return x >= 0x80; }));
			dropped = false;
			if (!out.empty()) sound_out(out);
		}
	}
	frame++;
	if (checkpoint_every && frame % checkpoint_every == 0) {
		board.save_state(check_buf);
		checkpoints[frame] = hash(check_buf);
	}
}

void UnheardQueue::look(const std::vector<uint8_t> &b, bool is_frame)
{
	for (const uint8_t x : b) {
		if (x >= 0xf8) continue;
		if (x & 0x80) { st = x; need = midi_data_bytes(x); got = 0; continue; }
		if (!need) { need = midi_data_bytes(st); got = 0; }   // (running status)
		if (!need) continue;
		if (got++ == 0) d1 = x;
		if (--need == 0 && st == MUSIC_CUE[0] && d1 == MUSIC_CUE[1]) cue = is_frame ? frames_in : frames_in + 1;
	}
}

void UnheardQueue::heard(const std::vector<uint8_t> &b)
{
	const uint8_t st0 = st;
	look(b, false);
	q.push_back({ Piece::heard, b, 0, st0, std::move(carry_next) });
	carry_next.clear();
	if (strip) strip_front(q.size() - 1);
}

void UnheardQueue::frame(const std::vector<uint8_t> &b)
{
	const uint8_t st0 = st;
	look(b, true);
	q.push_back({ Piece::frame, b, frames_in++, st0, std::move(carry_next) });
	carry_next.clear();
	timed++;
	if (strip) strip_front(q.size() - 1);
	// past most_timed frames waiting (not counting those round the last cue), the oldest of the others let go: their
	// notes (sounds that would have ended by now), but not the rest, which go with the next one (carry)
	auto kept = [&](const Piece &p) { return cue != UINT64_MAX && p.n + before_cue >= cue && p.n < cue + cue_timed; };
	size_t kept_n = 0;
	for (const Piece &p : q) kept_n += p.kind == Piece::frame && kept(p);
	while (timed - kept_n > most_timed) {
		size_t i = 0;
		while (i < q.size() && (q[i].kind != Piece::frame || kept(q[i]))) i++;
		if (i == q.size()) break;
		std::vector<uint8_t> c = std::move(q[i].carry);
		carry(c, q[i].bytes, q[i].st0);
		q.erase(q.begin() + ptrdiff_t(i));
		timed--;
		dropped++;
		strip = true;
		if (i < q.size()) {
			carry(c, q[i].carry, 0);
			q[i].carry = std::move(c);
			strip_front(i);
		} else {
			carry(c, carry_next, 0);
			carry_next = std::move(c);
		}
	}
}

// (a frame let go: its messages put after carry's, but its notes on (sounds over by now) and the music's cues (the last
// one's never let go: those before it are over too); then each once, its last: a control's last value, a note's last
// off, a channel's last program. s: the running status its bytes start with)
void UnheardQueue::carry(std::vector<uint8_t> &c, const std::vector<uint8_t> &bytes, uint8_t s)
{
	struct M { uint8_t b[3]; int n; };
	std::vector<M> ms;
	auto read = [&](const std::vector<uint8_t> &v, uint8_t s) {
		int need = 0;
		M m{};
		for (const uint8_t x : v) {
			if (x >= 0xf8) continue;
			if (x & 0x80) { s = x; need = 0; continue; }
			if (!need) {
				need = midi_data_bytes(s);
				if (!need) continue;
				m = { { s, 0, 0 }, 1 };
			}
			m.b[m.n++] = x;
			if (--need == 0) ms.push_back(m);
		}
	};
	read(c, 0);
	read(bytes, s);
	auto note = [](const M &m) { return (m.b[0] & 0xf0) == 0x90 && m.b[2] != 0; };
	auto key = [](const M &m) {   // (a note off, either way it's sent, as one; a program, a pressure, a bend by its channel)
		const uint8_t k = m.b[0] & 0xf0;
		const uint8_t st = k == 0x90 ? uint8_t(0x80 | (m.b[0] & 0x0f)) : m.b[0];
		return k >= 0xc0 ? int(st) << 8 : int(st) << 8 | m.b[1];
	};
	std::vector<M> out;
	std::vector<int> seen;
	for (size_t i = ms.size(); i-- > 0;) {
		const M &m = ms[i];
		if (note(m) || (m.b[0] == MUSIC_CUE[0] && m.b[1] == MUSIC_CUE[1])) continue;
		if (std::find(seen.begin(), seen.end(), key(m)) != seen.end()) continue;
		seen.push_back(key(m));
		out.push_back(m);
	}
	c.clear();
	for (size_t i = out.size(); i-- > 0;) c.insert(c.end(), out[i].b, out[i].b + out[i].n);
}

// (after bytes let go: the next ones from a status byte, a message's data without its status byte being taken for
// another's)
void UnheardQueue::strip_front(size_t i)
{
	for (; i < q.size() && strip; i++) {
		std::vector<uint8_t> &u = q[i].bytes;
		const auto st = std::find_if(u.begin(), u.end(), [](uint8_t x) { return x >= 0x80; });
		strip = st == u.end();
		u.erase(u.begin(), st);
	}
}

bool UnheardQueue::next(Piece &p)
{
	if (q.empty()) return false;
	p = std::move(q.front());
	q.pop_front();
	if (p.kind == Piece::frame) timed--;
	return true;
}

void UnheardQueue::clear()
{
	q.clear();
	timed = 0;
	frames_in = 0;
	cue = UINT64_MAX;
	st = d1 = 0;
	need = got = 0;
	strip = false;
	carry_next.clear();
	dropped = 0;
}

void run_unheard(model2a &b, const UnheardQueue::Piece &p)
{
	b.sound_bytes(p.carry.data(), p.carry.size());
	b.sound_bytes(p.bytes.data(), p.bytes.size());
	if (p.kind == UnheardQueue::Piece::heard) return;
	b.run_frame();
	b.m_audio_out.clear();
}

uint8_t Score::players_in(const model2a &board)
{
	const uint8_t v = board.workram()[PLAYING];
	if (v == 2) return 3;
	if (v == 1) return 2;
	return board.game_mode() == 9 ? 1 : 0;   // (player 1 alone, in a fight; else no one yet)
}

void Score::frame(const model2a &board)
{
	if (!known()) return;
	const std::vector<uint8_t> &ram = board.workram();
	const uint8_t r[3] = { ram[ROUNDS_P1], ram[ROUNDS_P2], players_in(board) };
	if (memcmp(r, now, 3) != 0) { memcpy(now, r, 3); held = 0; return; }
	if (++held != 30) return;   // (once, when it's held long enough)
	playing = r[2];
	for (int p = 0; p < 2; p++)
		if (r[p] == seen[p] + 1) {
			round_ends++;
			if (seen[0] + seen[1] == 0) versus = playing == 3;   // (the match's first round)
			if (playing != 3) versus = false;
			if (versus) rounds[p]++;
		}
	if (r[0] == 0 && r[1] == 0 && seen[0] + seen[1] > 0) {   // (the match over: whoever had more won it)
		if (versus && seen[0] != seen[1]) matches[seen[0] > seen[1] ? 0 : 1]++;
		versus = false;
	}
	seen[0] = r[0];
	seen[1] = r[1];
}

} // namespace vf2net
