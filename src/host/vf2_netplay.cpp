#include "vf2_netplay.h"
#include <algorithm>
#include <cstring>

namespace vf2net {

namespace {

// IN0's bits for each player (active low on the board)
constexpr uint8_t IN0_COIN[2] = { 0x01, 0x02 }, IN0_START[2] = { 0x10, 0x20 };

constexpr uint32_t BLOB_MAGIC = 0x42324656;   // "VF2B"

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
}

uint64_t content_hash(const model2a &b) { return b.rom_hash(); }

std::vector<uint8_t> start_blob(const model2a &b, int secret)
{
	std::vector<uint8_t> out(4);
	memcpy(out.data(), &BLOB_MAGIC, 4);
	const std::vector<uint8_t> &backup = b.backup_ram();
	out.insert(out.end(), backup.begin(), backup.end());
	const uint8_t *e = reinterpret_cast<const uint8_t *>(b.eeprom_cells());
	out.insert(out.end(), e, e + 64 * 2);
	out.push_back(uint8_t(secret));
	return out;
}

// (a frame's bytes go at once, a guessed frame's as much as a certain one's, so no sound waits for the other
// side's input)
void Adapter::take_sound(std::vector<uint8_t> &out)
{
	sound_taken = true;
	out.clear();
	auto it = sound_sent.find(frame);
	const bool sent = it != sound_sent.end() && !it->second.empty();
	if (!pending.empty() && !sent) {
		out = pending;
		sound_sent[frame] = pending;
	} else if (it == sound_sent.end())
		sound_sent[frame] = {};
	pending.clear();
	while (!sound_sent.empty() && sound_sent.begin()->first + 64 < frame) sound_sent.erase(sound_sent.begin());
}

bool Adapter::begin(const std::vector<uint8_t> &blob)
{
	const size_t backup_n = board.backup_ram().size();
	uint32_t magic = 0;
	if (blob.size() != 4 + backup_n + 64 * 2 + 1) return false;
	memcpy(&magic, blob.data(), 4);
	if (magic != BLOB_MAGIC) return false;
	const int secret = blob.back();
	if (secret != 0 && secret != 9 && secret != 11 && secret != 12) return false;
	if (settle) settle();
	if (on_begin) on_begin();
	collecting = false;
	// the same game on both sides, whatever each plays offline: the program's patches (the culling widescreen
	// uses, the host's secret character, the WARNING shortened), the FREE PLAY notices hidden, the sound board
	// held and the byte link (they cut the CPUs' time differently)
	// (each only on the program known: the ROMs' hashes being the same, both sides patch alike)
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
	for (int i = 0; i < BOOT_FRAMES && !board.past_boot(); i++) {
		board.run_frame();
		if (booting && i % 60 == 59) booting();
	}
	board.on_uart_tx = [this](uint8_t b) { if (collecting) pending.push_back(b); };
	collecting = true;
	pending.clear();
	frame = 0;
	started_secret = secret;
	sound_sent.clear();
	checkpoints.clear();
	return true;
}

uint64_t Adapter::hash(const std::vector<uint8_t> &state)
{
	const size_t skip = std::min(state.size(), model2a::STATE_HEADER);
	return vfnet::hash64(state.data() + skip, state.size() - skip);
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
	sound_taken = false;
	if (run) run(resim);
	else board.run_frame();
	if (!sound_taken) {
		std::vector<uint8_t> out;
		take_sound(out);
		if (!out.empty() && sound_out) sound_out(out);
	}
	frame++;
	if (checkpoint_every && frame % checkpoint_every == 0) {
		board.save_state(check_buf);
		checkpoints[frame] = hash(check_buf);
	}
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
