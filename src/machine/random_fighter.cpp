// Cheats' random fighters: a fighter made of three of the cast, for a player (Player 1 / 2 fighter: Random, or a
// Seed's) or for the computer (Random opponents). VF1 Recomp's, for VF2's tables (found in Version 2.1's program).
//
// VF2 loads a fighter by its number (the player's block +1b0: Akira 0, Jacky 1, Sarah 2, Kage 3, Lau 4, Jeffry 5,
// Pai 6, Wolf 7, Shun 8, Dural 9, Lion 10, MUE 11, KOJAC 12, and 13 on for the second costumes; +1b1 the number
// without the costume) from tables indexed by it:
//   - 200620c, its record (one a costume): 0x50 bytes, after 0x340 of its own (the parts, the hands' poses):
//       +00 the parts (16 models: 0 hips, 1 waist, 2 head, 3-5 near arm and hand, 6-8 far arm and hand, 9 torso,
//           10-12 a leg, 13-15 the other), +04 the skeleton (15 joints), +08 16 words copied into the block at
//           +1b4 (none have one: the ROM's first), +14 the basic moves (stance, walks, guards, reactions: move
//           numbers), +18-2c sizes and weight, +30 its texture set, +34 and +44 the parts again (as drawn
//           further away), +38 the torso and waist (in place of parts 9 and 1) and the torso's swinging parts,
//           +3c the hands' poses (parts 5 and 8), +40 extras (Kage's, Pai's), +48 and +4c more of the body's
//   - 2006278's ten tables of faces (13 each, by +1b1): the head, part 2, drawn from its face's frames (blinks,
//     expressions); a costume's in the row's next column
//   - 1d1fc, its commands (13): a dispatch on the buttons pressed (the low byte: Punch 1, Kick 2, Guard 4 and
//     their sums; the high byte the situation), each to a program of checks ending in a move. Move numbers are the
//     whole game's (200d34c), so a program works for anyone
//   - 61cfc, what swings from the head (Akira's headband's ends, braids); 201e44c, the voices (18 kinds, 11
//     fighters each); 201cc04 a move at a round's end, and 201cbee a word, each by fighter
// MUE and KOJAC are Lion's record with Akira's commands, and so free: player 1's random fighter is made as fighter
// 11, player 2's as 12, and picked for them as the select ends (the VS screen). The computer's random opponent is
// made on its own fighter's tables as its match loads (not one a player has picked).
//
// Colours: a side's texture set is loaded with the fighter's number (0x142bc: ldob 1b0(g7), g2), which picks the
// costume's colours (the skin, the clothes' main colour, the faces' textures: hair, belts and shoes are shared colours).
// That read takes block +1b4 instead (+08's first byte), and every record is given a copy of itself whose +08 names
// its own number: the cast's colours as they were. A random fighter's are its head's (its face, hair and skin as
// they are), its clothes' main colour and the textures' recoloured each game (colours), in a table of its own.
// Every byte changed is kept as it was (undo): off, the ROMs as loaded.
#include "model2a.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <zlib.h>

namespace {

constexpr u32 RECORDS = 0x0200620c, FACE_LISTS = 0x02006278, COMMANDS = 0x0001d1fc, ATTACH = 0x00061cfc;
constexpr u32 VOICES = 0x0201e44c, ROUND_MOVES = 0x0201cc04, ROUND_MOVES_AT = 0x0003b38c, WORDS_PTR = 0x0201cba8;
// the energy bars' names, a picture each (player 1's side's 201e044, player 2's 201e070, 11 each: KOJAC's on player 2's
// side read past the end, and had no box), read by two ld's (their addresses' words)
constexpr u32 BAR_NAMES[2] = { 0x0201e044, 0x0201e070 }, BAR_NAMES_AT[2] = { 0x0002ccac, 0x0002ccbc };
constexpr int N_RECORDS = 26, N_FACE_LISTS = 10, N_VOICES = 19, HEADER = 0x50, BLOCK = 0x340;
// The ROM's empty space past the program (zeros): the base layer, then a place for each fighter number's
constexpr u32 FREE_AT = 0x1c0000, FREE_END = 0x1fef00;
constexpr u32 COPIES = FREE_AT, COLOURS = FREE_AT + 0x1000, VOICE_COPIES = FREE_AT + 0x2000;
constexpr u32 ROUND_COPY = FREE_AT + 0x2800, WORDS_COPY = FREE_AT + 0x2900, BAR_COPIES = FREE_AT + 0x2a00;
constexpr u32 MADE_AT = FREE_AT + 0x4000, MADE = 0x1000;
// A side's colours, loaded by the colour's number n (block +1b4, COLOUR_AT): seven rows of 16 (the faces' and details'
// textures) from 2105800 + n * 0x2a0 (0x754), and two ramps of 64, the skin and the main colour (the clothes, shaded),
// from 2101000 + n * 0x300 (0x8e0; and 0x7a0 two rows of 16 lighter from them); a second costume's from 2107780 and
// 0x2400 on (by the block's +1b0, the costume). Each channel a level, 116 black to 255, through the colour adjust's
// table (544000). Relocated: a table each, one entry a number (0-25 the cast's as read, then the random fighters'),
// the costumes' split gone; the reloads by the block's number (Shun's drinks, 0x4772c) by the colour's
constexpr u32 FLAT = 0x02105800, FLAT2 = 0x02107780, RAMPS = 0x02101000, FLAT_SIZE = 0x2a0, RAMP_SIZE = 0x300;
constexpr u32 FLAT_NEW = 0x1e0000, RAMP_NEW = 0x1e6000, COLOUR_AT = 0x000142bc;
constexpr u32 COLOUR_NUMBER[3] = { 26, 27, 28 };   // (player 1's random fighter's, player 2's, the computer's)
struct patch { u32 at, was, now; };
constexpr patch PATCHES[] = {
	{ 0x000142bc, 0x8095e1b0, 0x8095e1b4 },        // (the texture set's colours: ldob 0x1b0(g7),g2 -> 0x1b4)
	{ 0x00000778, FLAT, FLAT_NEW },                 // (lda 2105800,g3)
	{ 0x00000784, 0x316ba014, 0x08000014 },        // (cmpobg 13,r14: a second costume's table -> b, never)
	{ 0x000007ac, RAMPS + 0x20, RAMP_NEW + 0x20 },  // (lda 2101020,g3: the lighter rows)
	{ 0x000007c4, RAMPS + 0x1a0, RAMP_NEW + 0x1a0 },
	{ 0x00000900, 0x316ba00c, 0x0800000c },        // (the ramps' cmpobg 13,r14 -> b)
	{ 0x00000980, RAMPS, RAMP_NEW },                // (lda 2101000(r3),r7)
	{ 0x00000c2c, 0x8088e1b0, 0x8088e1b4 },        // (Shun's drinks' reload: ldob 0x1b0(r3),g1 -> 0x1b4)
	{ 0x00000c5c, 0x8088e1b0, 0x8088e1b4 },
	{ 0x0004775c, 0x8088e1b0, 0x8088e1b4 },        // (both sides' ramps again, 0x4772c)
	{ 0x00047770, 0x8088e1b0, 0x8088e1b4 } };

// the colour levels as 0-1, and as hue, saturation, value
float level(u16 v) { return std::clamp((float(v) - 116.f) / 139.f, 0.f, 1.f); }
u16 raw(float x) { return u16(std::lround(116.f + std::clamp(x, 0.f, 1.f) * 139.f)); }
struct hsv { float h = 0, s = 0, v = 0; };
hsv to_hsv(float r, float g, float b)
{
	const float mx = std::max({ r, g, b }), mn = std::min({ r, g, b }), d = mx - mn;
	hsv c{ 0, mx > 0 ? d / mx : 0, mx };
	if (d > 0) { c.h = mx == r ? (g - b) / d : mx == g ? 2 + (b - r) / d : 4 + (r - g) / d; c.h /= 6; if (c.h < 0) c.h += 1; }
	return c;
}
void to_rgb(const hsv &c, float &r, float &g, float &b)
{
	const float h = std::fmod(c.h + 1, 1.f), x = c.v * c.s * (1 - std::fabs(std::fmod(h * 6, 2.f) - 1)), m = c.v - c.v * c.s, k = c.v * c.s;
	r = g = b = 0;
	switch (int(h * 6) % 6) { case 0: r = k, g = x; break; case 1: r = x, g = k; break; case 2: g = k, b = x; break; case 3: g = x, b = k; break; case 4: r = x, b = k; break; default: r = k, b = x; }
	r += m; g += m; b += m;
}
// (VF1's: skin, eyes and teeth (white or light grey), a mouth's full red)
bool is_skin(const hsv &c) { return c.h > 0.015f && c.h < 0.13f && c.s > 0.15f && c.s < 0.75f && c.v > 0.45f; }
// the clothes' themes, VF1's: a turn round the colour wheel most; black and white, black and gold the rare ones
enum { THEME_TURN, THEME_BW, THEME_BLACK_GOLD, THEME_COOL, THEME_WARM, THEME_OPPOSITE, THEME_NEON, THEME_PASTEL, THEMES };
// (the program and its tables as known: a CRC of the spans this reads and changes)
constexpr u32 KNOWN_CRC = 0xc4b50cfa;
// the cast random fighters are made of (by number: Akira to Shun, and Lion), and Dural, whose looks are anyone's now and then
const int CAST[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10 };
constexpr int DURAL = 9;
const char *const NAME[13] = { "AKIRA", "JACKY", "SARAH", "KAGE", "LAU", "JEFFRY", "PAI", "WOLF", "SHUN", "DURAL", "LION", "MUE", "KOJAC" };
// the looks, in groups (a pair of limbs as one), each from one fighter: the torso (with its skeleton, size, colours
// and textures: its record), the upper arms, the forearms, the hands, the thighs, the shins, the feet
const std::vector<std::vector<int>> GROUPS = { { 0, 1, 9 }, { 3, 6 }, { 4, 7 }, { 5, 8 }, { 10, 13 }, { 11, 14 }, { 12, 15 } };
constexpr int G_TORSO = 0, G_HANDS = 3;

} // namespace

class random_fighters
{
public:
	explicit random_fighters(model2a &m);
	model2a &m;
	std::vector<u8> &prog, &data;
	std::vector<u8> prog0, data0;   // (the ROMs' bytes as loaded, the part read: tables and records)
	bool known = false, on = false, base_on = false;
	int mode[2] = { 0, 0 };         // (each player's: 0 the select as ever, 1 random, 2 a seed's)
	u32 seed[2] = { 0, 0 };
	bool cpu = false;
	std::map<u32, u8> undo;         // (each byte changed, by its address as the i960 sees it: as it was)
	std::map<std::string, u32> names;
	std::string name_of[N_RECORDS];  // (each fighter number's random fighter, "" none)
	int head_of[N_RECORDS] = {};     // (... and its head's fighter, without the costume)
	u32 rng = 0;
	bool picking[2] = { false, false };
	int cpu_made[2] = { -1, -1 };   // (the fighter number each side's computer opponent was made random on)
	bool at_select[2] = { false, false }, fighting[2] = { false, false }, from_select = false;   // (who's playing: frame)

	// the ROMs: an address as the i960 sees it
	u8 *at(u32 a) { return a < 0x200000 ? &prog[a] : &data[a - 0x02000000]; }
	const u8 *at0(u32 a, u32 n) const   // (as loaded: the part kept, else zeros)
	{
		static const u8 none[4] = {};
		if (a + n <= prog0.size()) return &prog0[a];
		if (a >= 0x02000000 && a - 0x02000000 + n <= data0.size()) return &data0[a - 0x02000000];
		return none;
	}
	u32 rd0(u32 a) const { u32 v; memcpy(&v, at0(a, 4), 4); return v; }
	u16 rd0_16(u32 a) const { u16 v; memcpy(&v, at0(a, 2), 2); return v; }
	u16 rd16(u32 a) { u16 v; memcpy(&v, at(a), 2); return v; }   // (the ROMs as they are: the colours, never changed)
	void put8(u32 a, u8 v) { u8 &b = *at(a); if (b == v) return; undo.emplace(a, b); b = v; }
	void put32(u32 a, u32 v) { for (int k = 0; k < 4; k++) put8(a + k, u8(v >> (8 * k))); }
	void put16(u32 a, u16 v) { put8(a, u8(v)); put8(a + 1, u8(v >> 8)); }

	u32 next(u32 n) { u32 &x = rng; if (!x) x = 1; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return n ? x % n : 0; }

	// a fighter number's record (as loaded)
	u32 record(int f) const { return rd0(RECORDS + 4 * u32(f)); }
	int size_of(int f) const   // (its torso's length, the skeleton's second joint: 0 small (Pai, Sarah), 1, 2 big (Wolf, Jeffry))
	{
		float x; const u32 v = rd0(rd0(record(f) + 4) + 12); memcpy(&x, &v, 4);
		return x < 0.34f ? 0 : x > 0.385f ? 2 : 1;
	}
	// each base fighter's commands: the dispatch's keys and where each goes (read once)
	std::vector<std::pair<u16, u32>> disp[13];
	const std::vector<std::pair<u16, u32>> &dispatch(int base) const { return disp[base]; }

	struct plan {
		int three[3] = {};            // (the three, by base number: the first's stance, moves' basics and voice)
		int rec[7] = {};               // (each group's record: fighter number with its costume)
		int head = 0;                  // (the head's base number)
		std::map<u8, int> giver;       // (each button group's commands: which of the three)
		std::string name;
		int share = 0;                 // (the most of its looks one character's, in %)
		bool same_body = false;        // (its head and torso one character's)
	};
	plan make_plan(u32 seed);
	static std::string blend_name(const std::map<std::string, int> &made_of);
	void build(int f, const plan &p);   // (fighter number f made the plan's fighter)
	void own(int f);                    // (... and its own again)
	void base_layer();
	void off();
	void frame();
	void code_changed();   // (the program's patched instructions: made again)
	u32 colour_rng = 0;    // (the colours' draw: the clock's, new each game; not the seed's)
	u32 next_colour(u32 n);
	void colours(u32 n, int giver);   // (colour number n: the giver's colours, recoloured)
	u32 seed_for(int pl);
	u32 random_named();
};

void model2a::random_deleter::operator()(random_fighters *r) const { delete r; }

random_fighters &model2a::randoms()
{
	if (!m_random) m_random.reset(new random_fighters(*this));
	return *m_random;
}

random_fighters::random_fighters(model2a &board)
	: m(board), prog(board.m_roms.regions[romset::MAINCPU]), data(board.m_roms.regions[romset::MAIN_DATA])
{
	// (made before any of this changes them: the secret character's patches don't touch what's read here)
	if (prog.size() < 0x200000 || data.size() < 0x20000) return;
	prog0.assign(prog.begin(), prog.begin() + 0x80000);
	prog0.resize(0x200000, 0);
	data0.assign(data.begin(), data.begin() + 0x20000);
	static const std::pair<u32, u32> SPANS[] = { { RECORDS, 4 * N_RECORDS }, { FACE_LISTS, 4 * N_FACE_LISTS }, { COMMANDS, 4 * 13 },
		{ ATTACH, 4 * N_RECORDS }, { VOICES, 4 * N_VOICES }, { COLOUR_AT - 8, 16 }, { ROUND_MOVES_AT - 4, 12 }, { WORDS_PTR - 12, 16 },
		{ ROUND_MOVES, 4 * 11 }, { BAR_NAMES_AT[0] - 4, 4 }, { BAR_NAMES_AT[1] - 4, 4 }, { BAR_NAMES[0], 4 * 22 } };
	u32 crc = 0;
	for (const auto &[a, n] : SPANS) crc = u32(::crc32(crc, a < 0x200000 ? &prog[a] : &data[a - 0x02000000], uInt(n)));
	known = crc == KNOWN_CRC && std::all_of(std::begin(PATCHES), std::end(PATCHES), [&](const patch &k) { return rd0(k.at) == k.was; }) && std::all_of(prog.begin() + FREE_AT, prog.begin() + FREE_END, [](u8 b) { return b == 0; });
	for (int b = 0; b < 13; b++)
		for (u32 a = rd0(COMMANDS + 4 * u32(b)); a + 7 <= prog0.size() && prog0[a] == 0x0e && disp[b].size() < 200; a += 7)
			disp[b].push_back({ u16(prog0[a + 1] | prog0[a + 2] << 8), rd0(a + 3) });
	if (getenv("VF2_RANDLOG")) fprintf(stderr, "random fighters: tables crc %08x%s\n", crc, known ? "" : " (not the program known)");   // TESTONLY
}

// One random fighter's makeup, from its seed alone: the same on every machine
random_fighters::plan random_fighters::make_plan(u32 s)
{
	plan p;
	rng = s;
	const int n_cast = int(std::size(CAST));
	for (int i = 0; i < 3; i++)
		do p.three[i] = CAST[next(u32(n_cast))]; while ((i > 0 && p.three[i] == p.three[0]) || (i == 2 && p.three[2] == p.three[1]));
	auto costume = [&](int base) { return base + (next(2) ? 13 : 0); };
	// who gives each group of its looks: each of the three at least once; one in four anyone's (never Dural's: her
	// limbs are chrome, dark in another's colours; the group's owner's then, the draws as they were)
	int owner[7];
	for (int &o : owner) o = int(next(3));
	for (int who = 0; who < 3; who++)
		if (std::find(std::begin(owner), std::end(owner), who) == std::end(owner)) {
			int i;
			do i = int(next(7)); while (std::count(std::begin(owner), std::end(owner), owner[i]) < 2);
			owner[i] = who;
		}
	std::map<std::string, int> made_of, look;   // (its name's weights, and its looks' by whose they are)
	for (int g = 0; g < 7; g++) {
		const bool anyone = g != G_TORSO && next(4) == 0;
		int base = p.three[owner[g]];
		if (anyone && next(12) != 0) base = CAST[next(u32(n_cast))];
		p.rec[g] = costume(base);
		if (!anyone) made_of[NAME[base]] += g == G_TORSO ? 2 : 1;
		look[NAME[base]] += g == G_TORSO ? 3 : 1;
	}
	// (arms on a torso of another size: the upper arms were inside a big chest, or out in the air: the torso's own)
	if (size_of(p.rec[G_TORSO]) != size_of(p.rec[1]) || size_of(p.rec[G_TORSO]) != size_of(p.rec[2]))
		for (int g = 1; g <= G_HANDS; g++) p.rec[g] = p.rec[G_TORSO];
	// the head: the first's, one in five anyone's (Dural's one in twelve of those)
	const bool other = next(5) == 0;
	p.head = other ? (next(12) == 0 ? DURAL : CAST[next(u32(n_cast))]) : p.three[0];
	made_of[NAME[p.three[0]]] += 4;
	look[NAME[p.head]] += 4;
	// its commands, a button group at a time (the dispatch's low byte), each from one of the three: those that
	// differ between them dealt round the three in turn, in a random order; the rest the first's
	std::vector<std::pair<u16, u32>> d[3];
	for (int i = 0; i < 3; i++) d[i] = dispatch(p.three[i]);
	std::vector<u8> groups;
	for (int i = 0; i < 3; i++)
		for (const auto &[k, a] : d[i])
			if (std::find(groups.begin(), groups.end(), u8(k)) == groups.end()) groups.push_back(u8(k));
	std::sort(groups.begin(), groups.end());
	std::vector<u8> differ;
	for (u8 g : groups) {
		std::vector<u32> where[3];
		for (int i = 0; i < 3; i++)
			for (const auto &[k, a] : d[i]) if (u8(k) == g) where[i].push_back(a);
		if (where[0] != where[1] || where[0] != where[2]) differ.push_back(g); else p.giver[g] = 0;
	}
	for (size_t i = differ.size(); i > 1; i--) std::swap(differ[i - 1], differ[next(u32(i))]);
	const int first = int(next(3));
	for (size_t i = 0; i < differ.size(); i++) {
		p.giver[differ[i]] = int((first + i) % 3);
		made_of[NAME[p.three[p.giver[differ[i]]]]] += 1;
	}
	p.name = blend_name(made_of);
	int total = 0, most = 0;
	for (const auto &[n, w] : look) { total += w; most = std::max(most, w); }
	p.share = total ? most * 100 / total : 100;
	p.same_body = p.head == p.rec[G_TORSO] % 13;
	return p;
}

std::string random_fighters::blend_name(const std::map<std::string, int> &made_of)
{
	std::vector<std::pair<int, std::string>> by;
	for (const auto &[n, k] : made_of) by.push_back({ -k, n });
	std::sort(by.begin(), by.end());
	if (by.empty()) return "???";
	// each name's syllables
	static const std::map<std::string, std::vector<std::string>> SYL = {
		{ "AKIRA", { "A", "KI", "RA" } }, { "JACKY", { "JAC", "KY" } }, { "SARAH", { "SA", "RAH" } }, { "KAGE", { "KA", "GE" } },
		{ "LAU", { "LAU" } }, { "JEFFRY", { "JEF", "FRY" } }, { "PAI", { "PAI" } }, { "WOLF", { "WOLF" } }, { "SHUN", { "SHUN" } },
		{ "DURAL", { "DU", "RAL" } }, { "LION", { "LI", "ON" } } };
	auto syl = [&](const std::string &n) { auto it = SYL.find(n); return it != SYL.end() ? it->second : std::vector<std::string>{ n }; };
	auto join = [](std::string a, std::string b) { if (!a.empty() && !b.empty() && a.back() == b.front()) b.erase(0, 1); return a + b; };
	const std::string &a = by[0].second;
	if (by.size() == 1) return a;
	const auto sa = syl(a), sb = syl(by[1].second);
	const std::string start = sa.size() >= 3 ? sa[0] + sa[1] : sa[0];   // (the heaviest's start)
	if (by.size() == 2) {
		std::string rest;
		for (size_t i = sb.size() > 1 ? 1 : 0; i < sb.size(); i++) rest += sb[i];
		return join(start, rest);
	}
	std::string mid = sb[0];
	for (const auto &x : sb) if (x.size() >= 2) { mid = x; break; }
	const std::string end = syl(by[2].second).back();
	std::string n = join(join(start, mid), end);
	return n.size() > 10 ? join(start, end) : n;
}

// The base layer, while any random fighter is on: every record a copy of itself naming its own colours (+08), the
// colours' read moved to it, and the voices, the round's end moves and the words a table of 13 (MUE's and KOJAC's
// places read past the end of 11: what they read kept)
void random_fighters::base_layer()
{
	if (base_on) return;
	base_on = true;
	for (int f = 0; f < N_RECORDS; f++) {
		const u32 copy = COPIES + HEADER * u32(f), colour = COLOURS + 0x40 * u32(f);
		for (u32 k = 0; k < HEADER; k += 4) put32(copy + k, rd0(record(f) + k));
		put32(copy + 8, colour);
		for (u32 k = 0; k < 0x40; k += 4) put32(colour + k, rd0(k));   // (the ROM's first 64 bytes, as every record copied)
		put8(colour, u8(f));
		put32(RECORDS + 4 * u32(f), copy);
	}
	for (int s = 1; s < N_VOICES; s++) {
		const u32 was = rd0(VOICES + 4 * u32(s));
		if (!was) continue;
		const u32 copy = VOICE_COPIES + 0x34 * u32(s - 1);
		for (u32 k = 0; k < 13; k++) put32(copy + 4 * k, rd0(was + 4 * k));
		put32(VOICES + 4 * u32(s), copy);
	}
	for (u32 k = 0; k < 13; k++) put32(ROUND_COPY + 4 * k, rd0(ROUND_MOVES + 4 * k));
	put32(ROUND_MOVES_AT + 4, ROUND_COPY);
	const u32 words = rd0(WORDS_PTR);
	for (u32 k = 0; k < 13; k++) put16(WORDS_COPY + 2 * k, rd0_16(words + 2 * k));
	put32(WORDS_PTR, WORDS_COPY);
	for (int side = 0; side < 2; side++) {
		const u32 copy = BAR_COPIES + 0x40 * u32(side);
		for (u32 k = 0; k < 13; k++) put32(copy + 4 * k, rd0(BAR_NAMES[side] + 4 * k));
		put32(BAR_NAMES_AT[side], copy);
	}
	// (the colours: a table each, an entry a number, as the loader read them; the patches)
	for (u32 n = 0; n < N_RECORDS; n++) {
		const u32 flat = n < 13 ? FLAT + FLAT_SIZE * n : FLAT2 + FLAT_SIZE * (n - 13), ramp = RAMPS + RAMP_SIZE * (n < 13 ? n : n - 1);
		for (u32 k = 0; k < FLAT_SIZE; k += 2) put16(FLAT_NEW + FLAT_SIZE * n + k, rd16(flat + k));
		for (u32 k = 0; k < RAMP_SIZE; k += 2) put16(RAMP_NEW + RAMP_SIZE * n + k, rd16(ramp + k));
	}
	for (const patch &k : PATCHES) put32(k.at, k.now);
	code_changed();
}

void random_fighters::code_changed()
{
	if (!m.m_rec) return;
	m.m_rec->code_changed(ROUND_MOVES_AT, 8);
	for (u32 a : BAR_NAMES_AT) m.m_rec->code_changed(a - 4, 8);
	for (const patch &k : PATCHES) m.m_rec->code_changed(k.at & ~7u, 8);   // (an instruction's word, or an lda's address)
}

void random_fighters::off()
{
	const bool code = undo.count(COLOUR_AT) > 0;
	for (const auto &[a, b] : undo) *at(a) = b;
	undo.clear();
	if (code) code_changed();
	base_on = false;
	for (auto &n : name_of) n.clear();
	picking[0] = picking[1] = false;
	cpu_made[0] = cpu_made[1] = -1;
	{ std::lock_guard<std::mutex> lk(m.m_hud_mx); m.m_random_hud[0].clear(); m.m_random_hud[1].clear(); }
}

// A fighter number's tables its own again (the base layer's)
void random_fighters::own(int f)
{
	const int b = f % 13;
	put32(RECORDS + 4 * u32(f), COPIES + HEADER * u32(f));
	if (f >= 11 && f <= 12) put32(RECORDS + 4 * u32(f + 13), COPIES + HEADER * u32(f + 13));
	for (int t = 0; t < N_FACE_LISTS; t++) put32(rd0(FACE_LISTS + 4 * u32(t)) + 4 * u32(b), rd0(rd0(FACE_LISTS + 4 * u32(t)) + 4 * u32(b)));
	put32(COMMANDS + 4 * u32(b), rd0(COMMANDS + 4 * u32(b)));
	put32(ATTACH + 4 * u32(b), rd0(ATTACH + 4 * u32(b)));
	put32(ATTACH + 4 * u32(b + 13), rd0(ATTACH + 4 * u32(b + 13)));
	for (int s = 1; s < N_VOICES; s++)
		if (const u32 was = rd0(VOICES + 4 * u32(s))) put32(VOICE_COPIES + 0x34 * u32(s - 1) + 4 * u32(b), rd0(was + 4 * u32(b)));
	put32(ROUND_COPY + 4 * u32(b), rd0(ROUND_MOVES + 4 * u32(b)));
	put16(WORDS_COPY + 2 * u32(b), rd0_16(rd0(WORDS_PTR) + 2 * u32(b)));
	for (int side = 0; side < 2; side++) put32(BAR_COPIES + 0x40 * u32(side) + 4 * u32(b), rd0(BAR_NAMES[side] + 4 * u32(b)));
	name_of[f].clear();
	if (f >= 11 && f <= 12) name_of[f + 13].clear();
}

// Fighter number f made the plan's fighter: its record (in the place for f), and f's entries in the tables by
// fighter (by its number without the costume: the computer's opponent's both costumes)
void random_fighters::build(int f, const plan &p)
{
	const int b = f % 13, torso = p.rec[G_TORSO];
	const u32 at_ = MADE_AT + MADE * u32(f), hdr = at_, colour = at_ + 0x50, blk = at_ + 0x100, far34 = at_ + 0x440, far44 = at_ + 0x480, cmds = at_ + 0x500;
	// the record: the torso's (its skeleton, sizes and weight, torso and its swinging parts, the rest of its body's),
	// its own parts' list, the head's texture set, colours of its own
	const u32 t_rec = record(torso);
	for (u32 k = 0; k < HEADER; k += 4) put32(hdr + k, rd0(t_rec + k));
	for (u32 k = 0; k < BLOCK; k += 4) put32(blk + k, rd0(rd0(t_rec) + k));
	put32(hdr + 0x00, blk);
	put32(hdr + 0x08, colour);
	for (u32 k = 0; k < 0x40; k += 4) put32(colour + k, rd0(k));
	// (its colours its head's: the face's skin all over, the hair and the face as they are; Dural's her own)
	const u32 n = COLOUR_NUMBER[f == 11 ? 0 : f == 12 ? 1 : 2];
	colours(n, p.head + (f >= 13 ? 13 : 0));
	put8(colour, u8(n));
	int slot_rec[16];
	for (int g = 0; g < 7; g++) for (int s : GROUPS[g]) slot_rec[s] = p.rec[g];
	slot_rec[2] = p.head + (f >= 13 && p.head != DURAL ? 13 : 0);
	for (int s = 0; s < 16; s++) {
		const u32 r = record(slot_rec[s]);
		put32(blk + 4 * u32(s), rd0(rd0(r) + 4 * u32(s)));
		put32(far34 + 4 * u32(s), rd0(r + 0x34) ? rd0(rd0(r + 0x34) + 4 * u32(s)) : 0);
		put32(far44 + 4 * u32(s), rd0(r + 0x44) ? rd0(rd0(r + 0x44) + 4 * u32(s)) : 0);
	}
	put32(hdr + 0x34, far34);
	put32(hdr + 0x44, far44);
	put32(hdr + 0x14, rd0(record(p.three[0]) + 0x14));   // (the first's basic moves: its stance and walk)
	put32(hdr + 0x30, rd0(record(slot_rec[2]) + 0x30));   // (the head's texture set: its hair and face's textures; the others' textured parts take them)
	put32(hdr + 0x3c, rd0(record(p.rec[G_HANDS]) + 0x3c));
	put32(hdr + 0x40, rd0(record(slot_rec[2]) + 0x40));
	put32(RECORDS + 4 * u32(f), hdr);
	if (f >= 11 && f <= 12) put32(RECORDS + 4 * u32(f + 13), hdr);
	// the head: its faces (Dural has none: her head is part 2) and what swings from it
	for (int t = 0; t < N_FACE_LISTS; t++) {
		const u32 list = rd0(FACE_LISTS + 4 * u32(t));
		put32(list + 4 * u32(b), rd0(list + 4 * u32(p.head)));
	}
	put32(ATTACH + 4 * u32(b), rd0(ATTACH + 4 * u32(p.head)));
	put32(ATTACH + 4 * u32(b + 13), rd0(ATTACH + 4 * u32(p.head + 13)));
	// the commands: a dispatch of its own, each key from its button group's giver (else the first of the three
	// that has it)
	std::vector<std::pair<u16, u32>> d[3];
	for (int i = 0; i < 3; i++) d[i] = dispatch(p.three[i]);
	std::vector<u16> keys;
	for (int i = 0; i < 3; i++)
		for (const auto &[k, a] : d[i]) if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
	auto where = [&](int i, u16 k) -> u32 { for (const auto &[kk, a] : d[i]) if (kk == k) return a; return 0; };
	u32 w = cmds;
	for (u16 k : keys) {
		const auto g = p.giver.find(u8(k));
		u32 a = g != p.giver.end() ? where(g->second, k) : 0;
		for (int i = 0; i < 3 && !a; i++) a = where(i, k);
		if (!a || w + 8 > MADE_AT + MADE * u32(f + 1)) continue;
		put8(w, 0x0e); put8(w + 1, u8(k)); put8(w + 2, u8(k >> 8)); put32(w + 3, a);
		w += 7;
	}
	put8(w, 0x10);
	put32(COMMANDS + 4 * u32(b), cmds);
	// the first's voice, and its move at a round's end; the torso's word
	for (int s = 1; s < N_VOICES; s++)
		if (const u32 was = rd0(VOICES + 4 * u32(s))) put32(VOICE_COPIES + 0x34 * u32(s - 1) + 4 * u32(b), rd0(was + 4 * u32(p.three[0])));
	put32(ROUND_COPY + 4 * u32(b), rd0(ROUND_MOVES + 4 * u32(p.three[0])));
	put16(WORDS_COPY + 2 * u32(b), rd0_16(rd0(WORDS_PTR) + 2 * u32(torso % 13)));
	// (its energy bar's name: the first's picture, the box the name is drawn in over it, hud_names)
	for (int side = 0; side < 2; side++) put32(BAR_COPIES + 0x40 * u32(side) + 4 * u32(b), rd0(BAR_NAMES[side] + 4 * u32(p.three[0])));
	name_of[f] = p.name;
	head_of[f] = p.head;
	if (f >= 11 && f <= 12) name_of[f + 13] = p.name;
	if (getenv("VF2_RANDLOG")) {   // TESTONLY
		fprintf(stderr, "MADE %d %s: %s+%s+%s head %s torso %d arms %d/%d hands %d legs %d/%d/%d keys %zu share %d%%\n", f, p.name.c_str(),
			NAME[p.three[0]], NAME[p.three[1]], NAME[p.three[2]], NAME[p.head], p.rec[0], p.rec[1], p.rec[2], p.rec[3], p.rec[4], p.rec[5], p.rec[6],
			keys.size(), p.share);
		for (const auto &[g, i] : p.giver) fprintf(stderr, "  buttons %02x: %s\n", g, NAME[p.three[i]]);
	}
}

u32 random_fighters::next_colour(u32 n)
{
	u32 &x = colour_rng;
	if (!x && getenv("VF2_COLOUR_SEED")) x = u32(atoi(getenv("VF2_COLOUR_SEED")));   // TESTONLY
	if (!x) x = u32(std::chrono::steady_clock::now().time_since_epoch().count()) * 2654435761u ^ 0x85ebca6bu;
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	return n ? x % n : 0;
}

// A random fighter's colours, new each game (VF1's random_fighter_colour and its renderer's, on VF2's colour table):
// the giver's, its skin ramp as it is; its clothes' main colour (the other ramp, a colour shaded in 64) turned round
// the colour wheel, or given a theme (cool, warm, two opposite colours, neon, pastel, black and white, black and
// gold), its brightest level put in it and every level moved with it, so its shading stays; the textures' cool colours
// each the same way (their warm ones are faces); skin, greys, black and white as they are; a dark yellow or
// brown a turn makes (olive, khaki: muddy) lifted to a clean gold or orange
void random_fighters::colours(u32 n, int giver)
{
	static const int WEIGHT[THEMES] = { 6, 1, 1, 4, 4, 4, 3, 3 };
	int w = int(next_colour(26)), theme = 0;
	while (theme < THEMES - 1 && w >= WEIGHT[theme]) w -= WEIGHT[theme++];
	float turn = float(next_colour(1000)) / 1000.f;
	if (const char *t = getenv("VF2_THEME")) theme = std::clamp(atoi(t), 0, THEMES - 1);   // TESTONLY
	if (const char *t = getenv("VF2_OUTFIT")) turn = float(atof(t));                     // TESTONLY
	auto themed = [&](const hsv &c, bool light) {
		hsv t = c;
		const float h0 = c.h;
		switch (theme) {
		case THEME_BW: t.s = 0; t.v = light ? 0.95f : 0.12f; break;
		case THEME_BLACK_GOLD: if (!light) { t.s = 0; t.v = 0.12f; } else { t.h = 0.125f; t.s = 0.75f; t.v = 0.92f; } break;
		case THEME_COOL: t.h = 0.47f + 0.3f * h0; t.s = std::max(c.s, 0.6f); break;                       // (teal, blue, violet)
		case THEME_WARM: t.h = std::fmod(0.97f + 0.17f * h0, 1.f); t.s = std::max(c.s, 0.65f); t.v = std::max(c.v, 0.45f); break;   // (red, orange, gold)
		case THEME_OPPOSITE: t.h = std::fmod(turn + (h0 < 0.5f ? 0 : 0.5f), 1.f); t.s = std::max(c.s, 0.65f); break;
		case THEME_NEON: t.h = std::fmod(h0 + turn, 1.f); t.s = 1; t.v = std::max(c.v, 0.9f); break;
		case THEME_PASTEL: t.h = std::fmod(h0 + turn, 1.f); t.s = 0.32f; t.v = std::max(c.v, 0.85f); break;
		default:
			t.h = std::fmod(h0 + turn, 1.f);
			if (t.h > 0.07f && t.h < 0.28f && c.v < 0.7f) { t.v = std::min(1.f, c.v * 0.4f + 0.6f); t.s = std::max(c.s, 0.7f); }
		}
		return t;
	};
	auto light_of = [](const hsv &c) { float r, g, b; to_rgb(c, r, g, b); return 0.3f * r + 0.59f * g + 0.11f * b > 0.45f; };
	// (kept: the main colour if skin, grey, black or white; in the textures, which are mostly faces, every warm colour
	// too (skin in all its shades, lips, cheeks, beards, browns and golds): only their cool ones (greens, blues and
	// purples: Pai's patterns, Wolf's bands) are clothes for sure)
	auto kept = [](const hsv &c) { return is_skin(c) || c.s <= 0.28f || c.v <= 0.12f; };
	auto face = [&](const hsv &c) { return kept(c) || c.h <= 0.18f || c.h >= 0.93f; };
	const u32 sf = FLAT_NEW + FLAT_SIZE * u32(giver), sr = RAMP_NEW + RAMP_SIZE * u32(giver);
	const u32 df = FLAT_NEW + FLAT_SIZE * n, dr = RAMP_NEW + RAMP_SIZE * n;
	auto get = [&](u32 a, u32 step) { return to_hsv(level(rd16(a)), level(rd16(a + step)), level(rd16(a + 2 * step))); };
	auto put = [&](u32 a, u32 step, const hsv &c) { float r, g, b; to_rgb(c, r, g, b); put16(a, raw(r)); put16(a + step, raw(g)); put16(a + 2 * step, raw(b)); };
	auto copy = [&](u32 a, u32 step, u32 from) { for (u32 k = 0; k < 3; k++) put16(a + k * step, rd16(from + k * step)); };
	for (u32 k = 0; k < 0x180; k += 2) put16(dr + k, rd16(sr + k));   // (the skin)
	hsv lv[64];
	int top = 0;
	for (int i = 0; i < 64; i++) { lv[i] = get(sr + 0x180 + 2 * u32(i), 0x80); if (lv[i].v > lv[top].v) top = i; }
	const hsv ref = lv[top];
	int lit = 0;   // (light or dark, for black and white or gold: by its levels' brightness on the whole, not its brightest)
	for (const hsv &c : lv) lit += light_of(c);
	const hsv t = themed(ref, lit > 32);
	for (int i = 0; i < 64; i++) {
		const u32 a = dr + 0x180 + 2 * u32(i);
		if (kept(ref)) { copy(a, 0x80, sr + 0x180 + 2 * u32(i)); continue; }
		hsv c = t;
		c.s = std::clamp(ref.s > 0 ? t.s * lv[i].s / ref.s : t.s, 0.f, 1.f);
		c.v = std::clamp(lv[i].v * t.v / ref.v, 0.f, 1.f);
		put(a, 0x80, c);
	}
	for (u32 row = 0; row < 7; row++)
		for (u32 i = 0; i < 16; i++) {
			const u32 a = row * 0x60 + 2 * i;
			const hsv c = get(sf + a, 0x20);
			if (face(c)) copy(df + a, 0x20, sf + a); else put(df + a, 0x20, themed(c, light_of(c)));
		}
	if (getenv("VF2_RANDLOG")) fprintf(stderr, "COLOURS %u from %d: theme %d turn %.3f, main h%.2f s%.2f v%.2f -> h%.2f s%.2f v%.2f\n", n, giver, theme, turn, ref.h, ref.s, ref.v, t.h, t.s, t.v);   // TESTONLY
}

// Random's: one of the named fighters (the first seed of each name), so its name, typed as a seed, is that same
// fighter; Seed's, its own. (Random's draw from the clock: a new one each game)
u32 random_fighters::seed_for(int pl) { return mode[pl] == 2 ? seed[pl] : random_named(); }

u32 random_fighters::random_named()
{
	const auto &all = m.random_fighter_names();
	if (all.empty()) return 1;
	static u32 clock = u32(std::chrono::steady_clock::now().time_since_epoch().count()) ^ 0x9e3779b9u;
	clock ^= clock << 13; clock ^= clock >> 17; clock ^= clock << 5;
	auto it = all.begin();
	std::advance(it, clock % u32(all.size()));
	return it->second;
}

// After each frame: at the select, a random player's fighter made as they start picking, and theirs from the VS
// screen on (their block's +1b0 and +1b1: the carrier's number); the computer's random opponent made as its match
// loads; the names under the energy bars
void random_fighters::frame()
{
	auto &ram = m.m_workram;
	auto rd_ram = [&](u32 o) { u32 v; memcpy(&v, &ram[o], 4); return v; };
	auto block = [&](int pl) -> int { const u32 b = rd_ram(0x804 + 4 * u32(pl)); return b >= 0x500000 && b - 0x500000 + 0x200 < ram.size() ? int(b - 0x500000) : -1; };
	const u8 game = ram[0x2b];
	// (the select's objects, player 1's at [50083c], player 2's at [500840]: +4 the side, the first word's bit 31
	// on, 3 in (kept after the select), 1 picking (clear once picked))
	for (int pl = 0; pl < 2; pl++) {
		const u32 o = rd_ram(pl ? 0x840 : 0x83c);
		const bool there = game == 7 && o >= 0x500000 && o - 0x500000 + 8 < ram.size() && ram[o - 0x500000 + 4] == pl;
		const u32 flags = there ? rd_ram(o - 0x500000) : 0;
		const bool in = (flags >> 31) && (flags & 8), now_picking = in && (flags & 2);
		if (game == 7) at_select[pl] = (flags & 8) != 0;   // (bit 31 goes on the select's last frame, 3 stays)
		if (!mode[pl]) { picking[pl] = false; continue; }
		const int carrier = 11 + pl;
		if (now_picking && !picking[pl]) { build(carrier, make_plan(seed_for(pl))); }
		picking[pl] = now_picking;
		// (while the select's up, the fighter the select has: a picking player's cursor's (its portrait, colours and
		// name as ever); one already playing, at a challenger's select, the random fighter's head's (the portrait its
		// face, in the textures loaded for the fight, its head's). The random fighter's from the VS screen (8) on, as
		// the fight is about to load it)
		const int bl = block(pl);
		if (bl < 0) continue;
		if ((flags & 8) && ram[bl + 0x1b0] == carrier) ram[bl + 0x1b0] = ram[bl + 0x1b1] = u8(head_of[carrier]);
		if (game == 8 && from_select && at_select[pl]) ram[bl + 0x1b0] = ram[bl + 0x1b1] = u8(carrier);
	}
	// Who's playing: work RAM 4c in a fight (2 both, 1 player 2 alone, 0 player 1 alone; set as it starts), and
	// at the VS screen (8) the select's just over, else the last fight's
	if (game == 7) from_select = true;
	if (game == 9) { from_select = false; for (int pl = 0; pl < 2; pl++) fighting[pl] = ram[0x4c] == 2 || ram[0x4c] == (pl ? 1 : 0); }
	const bool match = game >= 7 && game <= 9;
	for (int pl = 0; pl < 2; pl++) {
		const bool human = match && (game == 8 && from_select ? at_select[pl] : game == 7 ? at_select[pl] : fighting[pl]);
		const int bl = block(pl);
		// Random opponents: the computer's fighter made random on its own number at the VS screen (8), or on a match's
		// first frame (round state 31 0: the next opponent of a 1P run, its number just set), as its match is about to
		// load it (the next frame), never once it has (half one and half the other); not one a player has, nor the
		// number of a fighter a player is (a mirror match's other costume: the tables by fighter are both costumes')
		if (!cpu || human || bl < 0) {
			if (cpu_made[pl] >= 0 && (!match || human)) { own(cpu_made[pl]); cpu_made[pl] = -1; }
			continue;
		}
		if (game != 8 && !(game == 9 && ram[0x31] == 0)) continue;
		const int f = ram[bl + 0x1b0];
		if (f >= N_RECORDS || f == cpu_made[pl]) continue;
		if (cpu_made[pl] >= 0) { own(cpu_made[pl]); cpu_made[pl] = -1; }
		const int other = bl >= 0 && block(pl ^ 1) >= 0 ? ram[block(pl ^ 1) + 0x1b1] : -1;
		if (f % 13 >= 11 || f % 13 == other % 13) continue;
		build(f, make_plan(random_named()));
		cpu_made[pl] = f;
	}
	std::string hud[2];
	if (game == 9)
		for (int pl = 0; pl < 2; pl++)
			if (const int bl = block(pl); bl >= 0 && ram[bl + 0x1b0] < N_RECORDS) hud[pl] = name_of[ram[bl + 0x1b0]];
	std::lock_guard<std::mutex> lk(m.m_hud_mx);
	m.m_random_hud[0] = hud[0];
	m.m_random_hud[1] = hud[1];
}

// ---- model2a's ----

bool model2a::set_random_player(int pl, const std::string &setting)
{
	if (pl < 0 || pl > 1) return false;
	std::string n;
	for (char ch : setting) if (ch != ' ') n += char(toupper(u8(ch)));
	random_fighters &r = randoms();
	if (n.empty() || n == "*") { r.mode[pl] = n.empty() ? 0 : 1; r.seed[pl] = 0; return true; }
	const auto &all = random_fighter_names();
	const auto it = all.find(n);
	if (it == all.end()) return false;
	r.mode[pl] = 2;
	r.seed[pl] = it->second;
	return true;
}

void model2a::set_random_opponents(bool on) { randoms().cpu = on; }

bool model2a::random_fighters_on() const { return m_random && m_random->on; }

std::string model2a::random_fighter_name(int pl)
{
	if (!m_random || pl < 0 || pl > 1) return "";
	std::lock_guard<std::mutex> lk(m_hud_mx);
	return m_random_hud[pl];
}

std::string model2a::random_fighter_makeup(const std::string &name)
{
	const auto &all = random_fighter_names();
	const auto it = all.find(name);
	if (it == all.end()) return "";
	random_fighters &r = randoms();
	const u32 was = r.rng;
	const random_fighters::plan p = r.make_plan(it->second);
	r.rng = was;
	auto who = [](int rec) { std::string n = NAME[rec % 13]; for (size_t i = 1; i < n.size(); i++) n[i] = char(tolower(u8(n[i]))); return n; };
	auto list = [&](std::initializer_list<int> recs) {
		std::string out;
		for (int rec : recs) if (out.find(who(rec)) == std::string::npos) out += (out.empty() ? "" : ", ") + who(rec);
		return out;
	};
	std::string moves;
	static const char *const BUTTON[8] = { "none", "P", "K", "P+K", "G", "P+G", "K+G", "P+K+G" };
	for (int i = 0; i < 3; i++) {
		std::string mine;
		for (const auto &[g, giver] : p.giver) if (giver == i) mine += std::string(mine.empty() ? "" : " ") + (g < 8 ? BUTTON[g] : "other");
		moves += (i ? "; " : "") + who(p.three[i]) + (mine.empty() ? "" : " " + mine);
	}
	return who(p.head) + "|" + list({ p.rec[0] }) + "|" + list({ p.rec[1], p.rec[2] }) + "|" + list({ p.rec[3] }) + "|" +
		list({ p.rec[4], p.rec[5], p.rec[6] }) + "|" + moves + "|" + who(p.three[0]);
}

void model2a::random_frame()
{
	if (m_random && m_random->on) m_random->frame();
}

const std::map<std::string, u32> &model2a::random_fighter_names()
{
	random_fighters &r = randoms();
	if (!r.names.empty() || !r.known) return r.names;
	const u32 was = r.rng;
	// (only a fighter that looks its own: not one character's face on its own torso, not over 60% of its looks
	// one character's; the first seed for each name)
	for (u32 s = 1; s <= RANDOM_SEEDS; s++) {
		const random_fighters::plan p = r.make_plan(s);
		if (p.same_body || p.share > 60) continue;
		r.names.emplace(p.name, s);
	}
	r.rng = was;
	if (getenv("VF2_RANDLOG")) fprintf(stderr, "random fighters: %zu names\n", r.names.size());   // TESTONLY
	return r.names;
}

bool model2a::apply_random_fighters()
{
	render_wait();
	random_fighters &r = randoms();
	const bool any = r.mode[0] || r.mode[1] || r.cpu;
	r.on = any && r.known;
	if (!r.on) { r.off(); return !any; }
	random_fighter_names();   // (made now, as the game boots unseen: a quarter of a second, a hitch at a select)
	// (staying on: the fighters made undone, the base layer kept, so the program isn't made again)
	if (!r.base_on) { r.off(); r.base_layer(); }
	for (int f = 0; f < N_RECORDS; f++) if (!r.name_of[f].empty()) r.own(f);
	r.picking[0] = r.picking[1] = false;
	r.cpu_made[0] = r.cpu_made[1] = -1;
	r.at_select[0] = r.at_select[1] = r.fighting[0] = r.fighting[1] = r.from_select = false;
	// (each player's fighter made now too, for a match that starts without a select: a seed's as it plays)
	for (int pl = 0; pl < 2; pl++)
		if (r.mode[pl]) r.build(11 + pl, r.make_plan(r.seed_for(pl)));
	return true;
}

void model2a::random_fighters_off()
{
	if (!m_random) return;
	render_wait();
	m_random->off();
	m_random->on = false;
	m_random->mode[0] = m_random->mode[1] = 0;
	m_random->cpu = false;
}
