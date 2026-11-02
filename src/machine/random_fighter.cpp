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
//     fighters each); 201cc04 a move at a round's end (the loser's, standing), and 201cb9c's four tables of the win
//     poses, each by fighter
// MUE and KOJAC are Lion's record with Akira's commands, and so free: player 1's random fighter is made as fighter
// 11, player 2's as 12, and picked for them as the select ends (the VS screen). The computer's random opponent is
// made on its own fighter's tables as its match loads (not one a player has picked).
//
// Colours: a side's texture set is loaded with the fighter's number (0x142bc: ldob 1b0(g7), g2), which picks the
// costume's colours (the skin, the clothes' main colour, the faces' textures: hair, belts and shoes are shared colours).
// That read takes block +1b4 instead (+08's first byte), and every record is given a copy of itself whose +08 names
// its own number: the cast's colours as they were. A random fighter's are its head's (its face, hair and skin as
// they are), its clothes' main colour and the textures' recoloured each game (colours), in a table of its own.
// Textures: each part in its own fighter's, in a texture set made for the random fighter (build_textures, and the
// Textures note below). Every byte changed is kept as it was (undo, and the texture ROM's tex_was): off, the ROMs as
// loaded.
#include "model2a.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <zlib.h>

namespace {

constexpr u32 RECORDS = 0x0200620c, FACE_LISTS = 0x02006278, COMMANDS = 0x0001d1fc, ATTACH = 0x00061cfc;
constexpr u32 FACE_SPAN = 0x020062a0, FACE_SPAN_N = 0x1a0;   // (where FACE_LISTS' lists are: a save state's)
// 658a4: each fighter number's list of what it wears that swings, drawn on its own (Akira's headband's tails and his
// belt's ends, Jacky's hair, Lau's plait, Kage's hood's tails, Shun's gourd...: 0x64220, by +1b0; 6590c an empty one,
// Dural's, MUE's and KOJAC's). Its ops, each a word and its arguments: 1 a swing's settings (8), 2 what the strands
// after it hang from and rest on (15), 3 (2), 4 a strand (5: its kind, the part it hangs from: a record's slot, 2 the
// head; kind 1 the one before's again), 5 a link of it (16: its models at +6..+8), 0 the end. The game makes each op's
// work in 0x800 bytes (0x644e8, the sizes at 640c0). A random fighter's: each part's owner's strands that hang from
// that part (its head's from its head, its torso's from its torso...), a list of its own (WORN_COPIES)
constexpr u32 WORN = 0x000658a4, WORN_NONE = 0x0006590c;
constexpr u32 WORN_ARGS[6] = { 0, 8, 15, 2, 5, 16 }, WORN_WORK[6] = { 4, 0x94, 0x7c, 0xc, 0x10, 0x30 };
constexpr u32 VOICES = 0x0201e44c, ROUND_MOVES = 0x0201cc04, ROUND_MOVES_AT = 0x0003b38c;
// 201cb9c: the winner's pose (0x3b278), four tables of a move by fighter number without the costume, 11 each (u16): by
// the round's time in thirds (or the button held as it ends: Guard the first, Punch the second, Kick the third) and a
// perfect round's. 11 and 12 read past their ends (the next table's Akira and Jacky: a random fighter's win was
// theirs). Relocated as the voices are: 13 each
constexpr u32 POSES = 0x0201cb9c, N_POSES = 4;
// 201e498: each voice's lengths (the win words', 9-12: a u16 by fighter number without the costume, Akira to Lion's
// 11), the round's end waiting for its winner's to end (+770, set at 0x1b0bc, waited on at 0xf8f0); 11 and 12 read
// past their ends (59560 frames, a round that never ends, for one of them). Relocated as the voices are: 13 each
constexpr u32 VOICE_LENGTHS = 0x0201e498, CAST_LENGTHS = 11;
// the energy bars' names, a picture each (player 1's side's 201e044, player 2's 201e070, 11 each: KOJAC's on player 2's
// side read past the end, and had no box), read by two ld's (their addresses' words)
constexpr u32 BAR_NAMES[2] = { 0x0201e044, 0x0201e070 }, BAR_NAMES_AT[2] = { 0x0002ccac, 0x0002ccbc };
constexpr int N_RECORDS = 26, N_FACE_LISTS = 10, N_VOICES = 19, HEADER = 0x50, BLOCK = 0x340;
// The ROM's empty space past the program (zeros): the base layer, then a place for each fighter number's
constexpr u32 FREE_AT = 0x1c0000, FREE_END = 0x1fef00;
constexpr u32 COPIES = FREE_AT, COLOURS = FREE_AT + 0x1000, VOICE_COPIES = FREE_AT + 0x2000;
constexpr u32 ROUND_COPY = FREE_AT + 0x2800, POSE_COPIES = FREE_AT + 0x2900, BAR_COPIES = FREE_AT + 0x2a00;   // (POSES': 0x20 each)
constexpr u32 LENGTH_COPIES = FREE_AT + 0x3000;   // (a voice's lengths, 0x20 each)
// The bookkeeping's matches by fighter (0x6d9f8, at a match's end): a 12 x 12 table in backup RAM (36a4: rows of 48
// bytes by player 1's +1b1, two u16 a column by player 2's), its row and column read as the fighters' numbers and times
// 48 (0x6daec: addo 31,17,r13; mulo r11,r13,r9; mulo r12,r13,r10). KOJAC's 12 past its end: player 2's random fighter
// added 65536 to a count of the play times' after it (38e4) at each 2P match's end. Now a branch to the same three
// with each number through a table first, 12 and on counted as 11 (MUE's, the arcade's unused), and back
constexpr u32 MATCHES_AT = 0x0006daec, MATCHES_STUB = FREE_AT + 0x3800, MATCHES_MAP = FREE_AT + 0x3900;
constexpr u32 branch(u32 at, u32 to) { return 0x08000000u | ((to - at) & 0x00fffffcu); }   // (b to)
constexpr u32 MATCHES_CODE[] = { 0x805af400, MATCHES_MAP, 0x80633400, MATCHES_MAP,   // (ldob MAP(r11),r11; ldob MAP(r12),r12)
	0x596c581f, 0x704b408b, 0x7053408c, branch(MATCHES_STUB + 0x1c, MATCHES_AT + 12) };  // (as they were; b back)
constexpr u32 MADE_AT = FREE_AT + 0x4000, MADE = 0x1000;
// A side's colours, loaded by the colour's number n (block +1b4, COLOUR_AT): seven rows of 16 (the faces' and details'
// textures) from 2105800 + n * 0x2a0 (0x754), and two ramps of 64, the skin and the main colour (the clothes, shaded),
// from 2101000 + n * 0x300 (0x8e0; and 0x7a0 two rows of 16 lighter from them); a second costume's from 2107780 and
// 0x2400 on (by the block's +1b0, the costume). Each channel a level, 116 black to 255, through the colour adjust's
// table (544000). Relocated: a table each, one entry a number (0-25 the cast's as read, then the random fighters'),
// the costumes' split gone; the reloads by the block's number (Shun's drinks, 0x4772c) by the colour's
constexpr u32 FLAT = 0x02105800, FLAT2 = 0x02107780, RAMPS = 0x02101000, FLAT_SIZE = 0x2a0, RAMP_SIZE = 0x300;
constexpr u32 FLAT_NEW = 0x1e0000, RAMP_NEW = 0x1e6000, COLOUR_AT = 0x000142bc;
constexpr u32 WORN_COPIES = 0x1ec000, WORN_COPY = 0x800;   // (a fighter number's worn list: WORN)
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
	{ 0x00047770, 0x8088e1b0, 0x8088e1b4 },
	{ MATCHES_AT, 0x596c581f, branch(MATCHES_AT, MATCHES_STUB) } };   // (the bookkeeping's matches: addo 31,17,r13 -> b)

// Textures. A fighter's are two sets (record +30 and the next number) in the archive at 2300000: [2300008] a table of
// descriptor tables (a count, then a word each texture: where it goes), [230000c] a table of sets by number (the
// descriptor table's number, then each texture's data), uploaded a texture at a time by a background task (0x4bd24)
// for the requests at 550168 (0x20 each; a side's two at 550188 and 550248, player 2's 5501a8 and 550268). Each
// texture a block of 256 x 256 (or less) holding many of the fighter's (its face, hair, clothes, belts, boots ...
// wherever its models' texture headers say), every fighter's in the same places on its side's sheet (player 1's on
// sheet 1, player 2's on sheet 0; their mipmaps on the right halves, a level on the other sheet in turn), with seven
// rows of colours of its own (the record's colour entry's, for the faces and details) and the skin and main colour:
// so a side held one fighter's, and a random fighter's parts drew in its head's. Now each part is drawn in its own
// fighter's: the side's sheet put together a 32 x 32 tile at a time, each tile from the fighter whose texture is there
// (the head's where no other's is), a texture where another's is moved to a free place (the part's models' headers
// moved with it), its blocks decoded by the game's own decompressor (tex_decoder) and written to texture RAM once the
// side's set is loaded; and the parts' colour rows put in rows of the random fighter's colour entry the head doesn't
// use (the headers' colour numbers moved with them)
constexpr u32 ARC_DESCS = 0x02300008, ARC_SETS = 0x0230000c, MODELS = 0x020e0004;
constexpr u32 ARC_DESCS_WAS = 0x02300420, ARC_SETS_WAS = 0x02300c5c;
constexpr int N_ARC_SETS = 0x56;
constexpr int TW = 24, TH = 32;   // (the fighters' part of a sheet, in tiles of 32 x 32: 768 x 1024)
constexpr u32 SET_REQUEST[2][2] = { { 0x550188, 0x550248 }, { 0x5501a8, 0x550268 } };   // (each side's set's and the next's uploads)
// Each character's textures drawn as it plays besides its parts' (its faces' expressions, what it wears, its moves'
// effects: found by playing each, both costumes; a row a word, tiles 0-23 its bits), kept as its head's
const u32 MISC_TILES[11][TH] = {
	{ 0x000f00, 0x000f00, 0x000f00, 0x000f00, 0x000f00, 0x000f00, 0x000f00, 0x000f00, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000cf, 0x0000f7, 0x0000f7, 0x0000c3, 0x0000c3, 0x3f0000, 0x3f0000, 0x3f0000, 0x3f0000, 0x00003f, 0x00003f, 0x000000, 0x000000, 0x380000, 0x780000, 0xfc0000, 0xfc0000 },   // Akira
	{ 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x000000, 0x000000, 0x000000, 0x000000, 0x3f0000, 0x3f0000, 0x3f0000, 0x3f0000, 0x00003f, 0x00003f, 0x000000, 0x000000, 0x380000, 0x780000, 0xfc0000, 0xfc0000 },   // Jacky
	{ 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x00003f, 0x00001f, 0x00000f, 0x3f000f, 0x3f000f, 0x3f0000, 0x3f0000, 0x00003f, 0x00003f, 0x000000, 0x000000, 0x380000, 0x780000, 0xfc0000, 0xfc0000 },   // Sarah
	{ 0x0f0000, 0x0f0000, 0x0f0000, 0x0f0000, 0x0f0000, 0x0f0000, 0x0f0000, 0x0f0000, 0x0000ff, 0x00c0ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000bf, 0x0000bf, 0x000083, 0x0000cf, 0x0000cf, 0x0000cf, 0x0000cf, 0x30000f, 0x30000f, 0x3f000f, 0x3f000f, 0x000000, 0x000000, 0x300000, 0x300000, 0x380000, 0x780000, 0xfc0000, 0xfc0000 },   // Kage
	{ 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x0000ff, 0x0000ff, 0x0000cf, 0x0000cf, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x3f0000, 0x3f0000, 0x3f0000, 0x3f0000, 0x00003f, 0x00003f, 0x000000, 0x000000, 0x380000, 0x780000, 0xfc0000, 0xfc0000 },   // Lau
	{ 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x3f00ff, 0x3f000f, 0x3f000f, 0x3f000f, 0x000000, 0x000000, 0x000000, 0x000000, 0x380000, 0x780000, 0xfc0000, 0xfc0000 },   // Jeffry
	{ 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x00001f, 0x00001f, 0x0000cf, 0x000003, 0x3f0030, 0x3f0030, 0x3f0030, 0x3f0030, 0x780040, 0x780040, 0x080000, 0x080000, 0x300000, 0x700000, 0xe00000, 0xe00000 },   // Pai
	{ 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x00003f, 0x00003f, 0x00003f, 0x3f0000, 0x3f0000, 0x3f0000, 0x3f0000, 0x000000, 0x000000, 0x000000, 0x000000, 0x380000, 0x780000, 0xfc0000, 0xfc0000 },   // Wolf
	{ 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x0003ff, 0x0003ff, 0x0000ff, 0x00f0ff, 0x0000f7, 0x0000f7, 0x0000f7, 0x0000f7, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x3f003f, 0x3f003f, 0x3f000f, 0x3f000f, 0x0000c0, 0x0000c0, 0x000000, 0x000200, 0x380000, 0x780000, 0xfc0000, 0xfc0000 },   // Shun
	{ 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000 },   // Dural
	{ 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x00000f, 0x00001f, 0x00003f, 0x00003f, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x0000ff, 0x00f0ff, 0x0030ff, 0x3f0000, 0x3f0000, 0x3f0000, 0x3f0000, 0x000000, 0x000000, 0x000000, 0x000000, 0x380000, 0x780000, 0xfc0000, 0xfc0000 },   // Lion
};

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
bool light_colour(const hsv &c) { float r, g, b; to_rgb(c, r, g, b); return 0.3f * r + 0.59f * g + 0.11f * b > 0.45f; }
// (kept: the main colour if skin, grey, black or white; in the textures, mostly faces, every warm colour too)
bool kept(const hsv &c) { return is_skin(c) || c.s <= 0.28f || c.v <= 0.12f; }
bool face_colour(const hsv &c) { return kept(c) || c.h <= 0.18f || c.h >= 0.93f; }
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

// The game's own texture decompressor (0x4c180, 0x4cb64, 0x4cd18: the uploader's, from 0x4be68), run on an i960 of
// its own over the ROMs, a work RAM of its own and nothing else: a texture block's data as the game would write it
// to texture RAM at its place in a set (player 1's: level 0 on sheet 1, each mipmap level on the other sheet in
// turn), each 16-bit unit (2 x 2 texels) written kept. A random fighter's textures are put together from them
// (random_fighters::apply_textures), so the uploader never loads more than a fighter's set
class tex_decoder : public mame_bus32
{
public:
	struct unit { u8 sheet; u32 at; u16 v; };   // (a write: sheet, its 16-bit unit's index, the value)
	tex_decoder(const std::vector<u8> &prog, const std::vector<u8> &data) : m_prog(prog), m_data(data), m_ram(0x100000) {}
	// a block's data (its word in a set's list) placed at x, y: false if it didn't finish
	bool decode(u32 data_at, int x, int y, std::vector<unit> &out)
	{
		if (!m_cpu) m_cpu = std::make_unique<i960_device>(*this);
		std::fill(m_ram.begin(), m_ram.end(), 0);
		m_out = &out;
		out.clear();
		put(0x550008, 1000);   // (the time it may take each go: plenty)
		put(0x55c2f4, 0);      // (the request's flags: player 1's, the whole block, its mipmaps)
		const u32 stub = 0x5ff000, done = stub + 16;
		auto call = [](u32 at, u32 to) { return 0x09000000u | ((to - at) & 0x00fffffcu); };
		const bool raw = rd32(data_at) != 0;   // (the uploader's choice: 0x4be78)
		put(stub, call(stub, 0x4d16c));
		put(stub + 4, call(stub + 4, raw ? 0x4c9dc : 0x4c180));
		put(stub + 8, raw ? 0x08000008u : call(stub + 8, 0x4cb64));
		put(stub + 12, raw ? 0x08000004u : call(stub + 12, 0x4cd18));
		put(done, 0x08000000);   // (b .)
		m_cpu->reset();
		m_cpu->set_reg(I960_G3, data_at + 4);
		m_cpu->set_reg(I960_G6, u32(y));
		m_cpu->set_reg(I960_G7, u32(x));
		m_cpu->set_reg(I960_G8, 0);
		m_cpu->set_reg(I960_FP, 0x5fe000);
		m_cpu->set_reg(I960_SP, 0x5fe040);
		m_cpu->set_reg(I960_PFP, 0);
		m_cpu->set_reg(I960_IP, stub);
		for (int i = 0; i < 4000 && m_cpu->ip() != done; i++) m_cpu->run(100000);
		m_out = nullptr;
		return m_cpu->ip() == done;
	}
	u8 read_byte(offs_t a) override { const u8 *p = ptr(a); return p ? *p : 0; }
	u16 read_word(offs_t a) override { return u16(read_byte(a) | read_byte(a + 1) << 8); }
	u32 read_dword(offs_t a) override
	{
		if (a == 0xf00008 || a == 0xf0000c) return 0xfffff;   // (the timer: never late, nothing to wait for)
		return rd32(a);
	}
	void write_byte(offs_t a, u8 v) override { if (a >= 0x500000 && a < 0x600000) m_ram[a - 0x500000] = v; }
	void write_word(offs_t a, u16 v) override
	{
		if (a >= 0x12000000 && a < 0x12800000) { tex(a, v); return; }
		write_byte(a, u8(v)); write_byte(a + 1, u8(v >> 8));
	}
	void write_dword(offs_t a, u32 v) override
	{
		if (a >= 0x12000000 && a < 0x12800000) { tex(a, u16(v)); return; }
		for (int k = 0; k < 4; k++) write_byte(a + u32(k), u8(v >> (8 * k)));
	}
	u16 flags(offs_t a) override
	{
		const bool burst = a < 0x200000 || (a >= 0x500000 && a < 0x600000) || (a >= 0x02000000 && a < 0x04000000) || (a >= 0x12000000 && a < 0x12820000);
		return burst ? i960_cpu_device::BURST : 0;
	}
private:
	const std::vector<u8> &m_prog, &m_data;
	std::vector<u8> m_ram;
	std::unique_ptr<i960_device> m_cpu;
	std::vector<unit> *m_out = nullptr;
	const u8 *ptr(offs_t a) const
	{
		if (a < m_prog.size()) return &m_prog[a];
		if (a >= 0x02000000 && a - 0x02000000 < m_data.size()) return &m_data[a - 0x02000000];
		if (a >= 0x500000 && a < 0x600000) return &m_ram[a - 0x500000];
		return nullptr;
	}
	u32 rd32(offs_t a) const { u32 v = 0; for (int k = 0; k < 4; k++) if (const u8 *p = ptr(a + u32(k))) v |= u32(*p) << (8 * k); return v; }
	void put(u32 a, u32 v) { for (int k = 0; k < 4; k++) m_ram[a - 0x500000 + u32(k)] = u8(v >> (8 * k)); }
	void tex(offs_t a, u16 v)
	{
		const bool sheet1 = a >= 0x12400000;
		if (m_out) m_out->push_back({ u8(sheet1), ((a - (sheet1 ? 0x12400000 : 0x12000000)) & 0x1fffff) >> 2, v });
	}
};

} // namespace

class random_fighters
{
public:
	explicit random_fighters(model2a &m);
	void snapshot();       // (the ROMs' bytes as they are now: what's read, known or not, each fighter's dispatch)
	bool stale = false;    // (forget's: everything forgotten, the ROMs read again at the next use)
	void forget();
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
	u32 select_at = 0;   // (the frame number of a select's first frame after it: its picks' pace, select_inputs)
	// The round-1 build (model2_video's m_build_parts): each fighter number's models' groups (0 feet ... 8 head), a
	// random fighter's as made (build), a cast one's as it is; the frame after a match's intro began (its round state
	// 2 or 3), -1 none; the build's sides as given to the video (each side's fighter, its first frame)
	std::map<u32, u8> part_groups[N_RECORDS];
	void groups_of(const int slot_rec[16], int hands, int torso, std::map<u32, u8> &out) const;
	const std::map<u32, u8> &groups(int f);
	int64_t intro_at = -1;
	int hair_hue[N_RECORDS];   // (each fighter number's random fighter's hair hue, in 1/1000ths: build; -1 none)
	bool build_side[2] = { false, false };   // (this intro's: the side's fighter built)
	std::string built[2];                    // (each side's random fighter built since the last select: not again)
	int build_fighter[2] = { -1, -1 };
	int64_t build_start[2] = { -1, -1 };
	void build_frame(int game);
	void build_stop();
	// The select's object for a player (game 7, as frame() reads it): its offset in work RAM, -1 none; its flags
	int select_object(int pl) const;
	u32 select_flags(int pl) const;
	bool select_in(int pl) const { const u32 f = select_flags(pl); return (f >> 31) && (f & 8); }
	bool select_hidden() const;
	void select_inputs(u8 &in1, u8 &in2);
	int select_slot(int base) const;   // (the select's slot that sets fighter base, -1 none)
	void select_cover();
	void attract_frame();
	u32 scene_ptr[2] = { 0, 0 };   // (the attract intro's scene pointers as last seen: attract_frame; in the save states)
	u64 hide_until[2] = { 0, 0 };   // (drawing only: a random player's select portrait hidden till this frame)
	void attract_scenes();   // (the attract intro's scenes' fighters: the random sides' carriers)

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
	// a voice's length for fighter number b (without the costume; none past the cast's, VOICE_LENGTHS)
	u16 voice_length(int s, int b) const { const u32 l = rd0(VOICE_LENGTHS + 4 * u32(s)); return l && b >= 0 && u32(b) < CAST_LENGTHS ? rd0_16(l + 2 * u32(b)) : 0; }
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
		int three[3] = {};            // (the three, by base number: the first's stance, moves' basics; -1 a name of two's third)
		int rec[7] = {};               // (each group's record: fighter number with its costume)
		int head = 0;                  // (the head's base number)
		int voice = 0;                 // (whose voice, by base number)
		bool head_costume = false;     // (TESTONLY, VF2_PLAN: its head in its second costume)
		std::map<u8, int> giver;       // (make_numeric's: each button group's commands, which of the three)
		int moves[8] = {};             // (the recipe's: each move group's owner, of the three: P, K, P+K, P+G, K+G, P+K+G, throw, other)
		std::vector<std::pair<u16, int>> keys;   // (each command key, whose: of the three)
		std::string name;
		u32 place = 0;                 // (make_plan's: its place in the book, from 1)
		int share = 0;                 // (the most of its looks one character's, in %)
		bool same_body = false;        // (its head and torso one character's)
	};
	plan make_numeric(u32 seed);       // (a numeric seed's fighter: VF2's own names)
	plan make_plan(u32 place);         // (the book's fighter at place (from 1): its recipe's)
	static int move_group(u16 key);
	// the book (the recipe spec's, shared with VF1 and VF3 Recomp: a name the same fighter in every game that has it):
	// the carried book's names (VF1's, random_book.inc), then VF2's own with Lion or Shun
	struct entry { std::string name; int three[3]; int head, voice; std::string book_head; };
	std::vector<entry> entries;
	static std::string blend_name(const std::map<std::string, int> &made_of);
	void build(int f, const plan &p, int side);   // (fighter number f made the plan's fighter, for a side)
	void own(int f);                    // (... and its own again)
	void base_layer();
	void off();
	void frame();
	void code_changed();   // (the program's patched instructions: made again)
	u32 colour_rng = 0;    // (the colours' draw: the clock's, new each game; not the seed's)
	// Online (set_random_seed): Random's draws and the colours from the match's seed, never the clock; and each
	// player's fighter as made (its place, the colours' draw before it: a save state's, made again from them)
	bool seeded = false;
	u32 draw_rng = 0;
	struct made_key { u32 place = 0, colour = 0; bool operator==(const made_key &) const = default; } made[2];
	u32 next_colour(u32 n);
	void colours(u32 n, int giver);   // (colour number n: the giver's colours, recoloured)
	int col_theme = THEME_TURN;       // (... its theme and turn, for the rows put in after)
	float col_turn = 0;
	bool col_plain = false;           // (TESTONLY: VF2_NOCOLOUR, the colours as given)
	hsv themed(const hsv &c, bool light) const;
	void flat_copy(u32 n, u32 row, u32 from, u32 from_row);   // (a row of colours, recoloured as colours' are)

	// Textures (each side's; see the Textures note)
	struct tex_side {
		int fighter = -1;                          // (the fighter number they're made for: its models' headers moved while it's on the side)
		struct src { int ch = -1, dx = 0, dy = 0; bool operator==(const src &o) const { return ch == o.ch && dx == o.dx && dy == o.dy; } };
		std::vector<src> tiles;                    // (each tile: whose texels, from how far back; ch -1 the game's)
		bool applied = false, busy = false;        // (written to texture RAM; the side's set loaded since)
		std::map<u32, u16> rewrite;                // (the models' texture headers moved: texture ROM u16 index, its value)
		bool rewritten = false;
		u32 head_set = 0;                          // (its head's texture set)
	};
	tex_side tex[2];
	std::vector<u8> &texrom;
	std::map<u32, u16> tex_was;   // (each texture ROM u16 changed, as loaded)
	u16 trd(u32 i) const { u16 v = 0; if (2 * size_t(i) + 2 <= texrom.size()) memcpy(&v, &texrom[2 * size_t(i)], 2); return v; }
	u16 trd0(u32 i) const { const auto it = tex_was.find(i); return it != tex_was.end() ? it->second : trd(i); }
	void twr(u32 i, u16 v) { if (2 * size_t(i) + 2 > texrom.size() || trd(i) == v) return; tex_was.emplace(i, trd(i)); memcpy(&texrom[2 * size_t(i)], &v, 2); }
	u32 rdl(u32 a) { if (a >= 0x02000000 ? a - 0x02000000 + 4 > data.size() : a + 4 > prog.size()) return 0; u32 v; memcpy(&v, at(a), 4); return v; }   // (as they are)
	void build_textures(int f, const plan &p, int side, const int slot_rec[16], int giver, u32 n);
	// what's worn that swings (WORN): a list's ops (where, which, the part its strand hangs from: -1 none), a fighter's
	// list (the strands of each part's owner's that hang from that part, in the work the cast's lists take at most)
	struct worn_op { u32 at, op; int slot; };
	bool worn_ops(u32 q, std::vector<worn_op> &ops) const;
	std::vector<u32> worn_list(const int slot_rec[16]) const;
	std::vector<u32> worn_models(int rec, const std::vector<int> &slots) const;   // (its strands' from those parts)
	// A record's portrait hair (+48: a word in a table by fighter at 20061b8, 11 then 10 for the second costumes; drawn
	// with its head on the select's portrait, not in a fight; none for Kage and Dural): the model, 0 none
	u32 hair_model(int rec) const { const u32 q = rd0(record(rec) + 0x48); const u32 m = q ? rd0(q) : 0; return m < 0x2000 ? m : 0; }
	u32 worn_limit = 0;
	void set_rewrites(int side, bool on);
	void apply_textures(int side);
	void texture_set_now(int f, int side);   // (its record's texture set: the side's as it is, if a fighter's)
	struct blk { int x, y, w, h; u32 data; };
	std::map<int, std::vector<blk>> blocks;   // (each character's set's blocks: where they go, their size, their data)
	const std::vector<blk> &blocks_of(int ch);
	void decode_all();   // (every character's set's blocks decoded now: none left to decode as a match goes)
	std::unique_ptr<tex_decoder> decoder;
	std::map<u32, std::vector<tex_decoder::unit>> decoded;   // (each block's units as the game writes them, by its data)
	u32 seed_for(int pl);
	u32 random_named();
	// save states (model2a::random_state): the ROM's bytes they change as a match goes, and what they know
	void save(m2::StateWriter &io);
	void load(m2::StateReader &io);
};

void model2a::random_deleter::operator()(random_fighters *r) const { delete r; }

random_fighters &model2a::randoms()
{
	if (!m_random) m_random.reset(new random_fighters(*this));
	if (m_random->stale) m_random->snapshot();
	return *m_random;
}

random_fighters::random_fighters(model2a &board)
	: m(board), prog(board.m_roms.regions[romset::MAINCPU]), data(board.m_roms.regions[romset::MAIN_DATA]), texrom(board.m_roms.regions[romset::TEXTURES])
{
	for (int &h : hair_hue) h = -1;
	snapshot();
}

// Everything forgotten (off first): the next use reads the ROMs as they are then (online: the match's patches on, alike
// on both sides) and makes the book again. The textures' decoded blocks kept: the ROMs' own, which nothing changes
void random_fighters::forget()
{
	off();
	on = false;
	mode[0] = mode[1] = 0;
	seed[0] = seed[1] = 0;
	cpu = false;
	names.clear();
	entries.clear();
	for (int &h : head_of) h = 0;
	rng = colour_rng = draw_rng = 0;
	seeded = false;
	at_select[0] = at_select[1] = fighting[0] = fighting[1] = from_select = false;
	select_at = 0;
	scene_ptr[0] = scene_ptr[1] = 0;
	col_theme = THEME_TURN;
	col_turn = 0;
	col_plain = false;
	stale = true;
}

void random_fighters::snapshot()
{
	stale = false;
	known = false;
	worn_limit = 0;
	for (auto &d : disp) d.clear();
	// (made before any of this changes them: the secret character's patches don't touch what's read here)
	if (prog.size() < 0x200000 || data.size() < 0x20000) return;
	prog0.assign(prog.begin(), prog.begin() + 0x80000);
	prog0.resize(0x200000, 0);
	data0.assign(data.begin(), data.begin() + 0x20000);
	static const std::pair<u32, u32> SPANS[] = { { RECORDS, 4 * N_RECORDS }, { FACE_LISTS, 4 * N_FACE_LISTS }, { COMMANDS, 4 * 13 },
		{ ATTACH, 4 * N_RECORDS }, { VOICES, 4 * N_VOICES }, { COLOUR_AT - 8, 16 }, { ROUND_MOVES_AT - 4, 12 }, { POSES, 4 * N_POSES },
		{ ROUND_MOVES, 4 * 11 }, { BAR_NAMES_AT[0] - 4, 4 }, { BAR_NAMES_AT[1] - 4, 4 }, { BAR_NAMES[0], 4 * 22 } };
	u32 crc = 0;
	for (const auto &[a, n] : SPANS) crc = u32(::crc32(crc, a < 0x200000 ? &prog[a] : &data[a - 0x02000000], uInt(n)));
	known = crc == KNOWN_CRC && std::all_of(std::begin(PATCHES), std::end(PATCHES), [&](const patch &k) { return rd0(k.at) == k.was; }) && std::all_of(prog.begin() + FREE_AT, prog.begin() + FREE_END, [](u8 b) { return b == 0; })
		&& data.size() >= 0x2400000 && texrom.size() >= 0x1000000 && rdl(ARC_DESCS) == ARC_DESCS_WAS && rdl(ARC_SETS) == ARC_SETS_WAS
		&& rd0(WORN + 4 * 11) == WORN_NONE && rd0(WORN + 4 * 12) == WORN_NONE
		&& rd0(VOICE_LENGTHS) == 0 && rd0(VOICE_LENGTHS + 4 * 9) == 0x0201e828 && rd0(VOICE_LENGTHS + 4 * 12) == 0x0201e86a
		&& rd0(MATCHES_AT - 8) == 0x805ae1b1 && rd0(MATCHES_AT - 4) == 0x806321b1 && rd0(MATCHES_AT + 4) == MATCHES_CODE[5] && rd0(MATCHES_AT + 8) == MATCHES_CODE[6];
	for (u32 t = 0; t < N_FACE_LISTS && known; t++) known = rd0(FACE_LISTS + 4 * t) >= FACE_SPAN && rd0(FACE_LISTS + 4 * t) + 4 * 13 <= FACE_SPAN + FACE_SPAN_N;
	for (u32 g = 0; g < 26 && known; g++) {
		std::vector<worn_op> ops;
		known = worn_ops(rd0(WORN + 4 * g), ops);
		u32 w = WORN_WORK[0];
		for (const worn_op &o : ops) w += WORN_WORK[o.op];
		worn_limit = std::max(worn_limit, w);
	}
	known = known && worn_limit <= WORN_COPY;
	for (int b = 0; b < 13; b++)
		for (u32 a = rd0(COMMANDS + 4 * u32(b)); a + 7 <= prog0.size() && prog0[a] == 0x0e && disp[b].size() < 200; a += 7)
			disp[b].push_back({ u16(prog0[a + 1] | prog0[a + 2] << 8), rd0(a + 3) });
	if (getenv("VF2_RANDLOG")) fprintf(stderr, "random fighters: tables crc %08x%s\n", crc, known ? "" : " (not the program known)");   // TESTONLY
}

// One random fighter's makeup, from its seed alone: the same on every machine
random_fighters::plan random_fighters::make_numeric(u32 s)
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
	p.voice = p.three[0];
	return p;
}

// A command key's move group, the recipe spec's: 0 P, 1 K, 2 P+K, 3 P+G, 4 K+G, 5 P+K+G, 6 throw, 7 other (the
// dispatch's low byte the buttons: Punch 1, Kick 2, Guard 4; P+G a throw, as VF3 Recomp's, so 3 stays empty)
int random_fighters::move_group(u16 key)
{
	switch (key & 0xff) {
	case 1: return 0;
	case 2: return 1;
	case 3: return 2;
	case 5: return 6;
	case 6: return 4;
	case 7: return 5;
	default: return 7;
	}
}

// The book's fighter at place (from 1), from its recipe and its name alone (the same on any machine, and in VF3 Recomp
// for the names both have): the three as the book has them (the first's stance and basic moves), its head and voice;
// each move group's owner from its name (the recipe spec's: its FNV-1a hash, then xorshift32, each of the three once at
// least; a name of two, the third's the first's), then each part group's from the same stream on (each of the three once
// at least), and its costume; each command key to its group's owner, or the next of the three that has it
random_fighters::plan random_fighters::make_plan(u32 place)
{
	plan p;
	if (place < 1 || place > entries.size()) return p;
	const entry &e = entries[place - 1];
	p.name = e.name;
	p.place = place;
	for (int i = 0; i < 3; i++) p.three[i] = e.three[i];
	p.head = e.head;
	p.voice = e.voice;
	const int n = e.three[2] < 0 ? 2 : 3;
	uint32_t h = 2166136261u;
	for (char c : e.name) { h ^= uint8_t(c); h *= 16777619u; }
	uint32_t x = h ? h : 1;
	auto next_x = [&] { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; };
	int owner[8];
	for (int g = 0; g < 8; g++) owner[g] = int(next_x() % 3);
	for (int who = 0; who < 3; who++) {
		if (std::count(owner, owner + 8, who)) continue;
		for (;;) { const int g = int(next_x() % 8); if (std::count(owner, owner + 8, owner[g]) > 1) { owner[g] = who; break; } }
	}
	for (int g = 0; g < 8; g++) p.moves[g] = owner[g] % n;
	int part[7];
	for (int g = 0; g < 7; g++) part[g] = int(next_x() % 3);
	for (int who = 0; who < 3; who++) {
		if (std::count(part, part + 7, who)) continue;
		for (;;) { const int g = int(next_x() % 7); if (std::count(part, part + 7, part[g]) > 1) { part[g] = who; break; } }
	}
	for (int g = 0; g < 7; g++) p.rec[g] = p.three[part[g] % n] + (next_x() % 2 ? 13 : 0);
	// (arms on a torso of another size: the upper arms were inside a big chest, or out in the air: the torso's own)
	if (size_of(p.rec[G_TORSO]) != size_of(p.rec[1]) || size_of(p.rec[G_TORSO]) != size_of(p.rec[2]))
		for (int g = 1; g <= G_HANDS; g++) p.rec[g] = p.rec[G_TORSO];
	if (const char *o = getenv("VF2_PLAN")) {   // TESTONLY: "leader,head,torso,arms,forearms,hands,thighs,shins,feet" (records)
		int v[9];
		if (sscanf(o, "%d,%d,%d,%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]) == 9) {
			p.three[0] = v[0]; p.head = v[1] % 13; p.head_costume = v[1] >= 13;
			for (int g = 0; g < 7; g++) p.rec[g] = v[2 + g];
		}
	}
	// the keys: the first's, in its order, then the others' it hasn't
	std::vector<u16> keys;
	for (int who = 0; who < n; who++)
		for (const auto &[k, a] : dispatch(p.three[who]))
			if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
	for (u16 k : keys) {
		auto has = [&](int who) { for (const auto &[kk, a] : dispatch(p.three[who])) if (kk == k) return true; return false; };
		int who = p.moves[move_group(k)];
		for (int t = 0; t < n && !has(who); t++) who = (who + 1) % n;
		p.keys.push_back({ k, who });
	}
	std::map<int, int> look;
	look[p.head] += 4;
	for (int g = 0; g < 7; g++) look[p.rec[g] % 13] += g == G_TORSO ? 3 : 1;
	int total = 0, most = 0;
	for (const auto &[c, w] : look) { total += w; most = std::max(most, w); }
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
// colours' read moved to it, and the voices, the round's end moves and the win poses a table of 13 (MUE's and KOJAC's
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
	for (int s = 1; s < N_VOICES; s++) {
		if (!rd0(VOICE_LENGTHS + 4 * u32(s))) continue;
		const u32 copy = LENGTH_COPIES + 0x20 * u32(s);
		for (int k = 0; k < 13; k++) put16(copy + 2 * u32(k), voice_length(s, k));
		put32(VOICE_LENGTHS + 4 * u32(s), copy);
	}
	for (u32 k = 0; k < 13; k++) put32(ROUND_COPY + 4 * k, rd0(ROUND_MOVES + 4 * k));
	put32(ROUND_MOVES_AT + 4, ROUND_COPY);
	for (u32 t = 0; t < N_POSES; t++) {
		const u32 was = rd0(POSES + 4 * t);
		for (u32 k = 0; k < 13; k++) put16(POSE_COPIES + 0x20 * t + 2 * k, rd0_16(was + 2 * k));
		put32(POSES + 4 * t, POSE_COPIES + 0x20 * t);
	}
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
	for (u32 k = 0; k < std::size(MATCHES_CODE); k++) put32(MATCHES_STUB + 4 * k, MATCHES_CODE[k]);
	for (u32 k = 0; k < 256; k++) put8(MATCHES_MAP + k, u8(std::min(k, 11u)));
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
	for (const auto &[i, v] : tex_was) memcpy(&texrom[2 * size_t(i)], &v, 2);
	tex_was.clear();
	for (tex_side &t : tex) t = tex_side();
	if (code) code_changed();
	base_on = false;
	for (auto &n : name_of) n.clear();
	for (auto &g : part_groups) g.clear();
	for (int &h : hair_hue) h = -1;
	build_stop();
	picking[0] = picking[1] = false;
	cpu_made[0] = cpu_made[1] = -1;
	made[0] = made[1] = made_key{};
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
	put32(WORN + 4 * u32(f), rd0(WORN + 4 * u32(f)));
	if (f >= 11 && f <= 12) put32(WORN + 4 * u32(f + 13), rd0(WORN + 4 * u32(f + 13)));
	for (int s = 1; s < N_VOICES; s++)
		if (const u32 was = rd0(VOICES + 4 * u32(s))) put32(VOICE_COPIES + 0x34 * u32(s - 1) + 4 * u32(b), rd0(was + 4 * u32(b)));
	for (int s = 1; s < N_VOICES; s++)
		if (rd0(VOICE_LENGTHS + 4 * u32(s))) put16(LENGTH_COPIES + 0x20 * u32(s) + 2 * u32(b), voice_length(s, b));
	put32(ROUND_COPY + 4 * u32(b), rd0(ROUND_MOVES + 4 * u32(b)));
	for (u32 t = 0; t < N_POSES; t++) put16(POSE_COPIES + 0x20 * t + 2 * u32(b), rd0_16(rd0(POSES + 4 * t) + 2 * u32(b)));
	for (int side = 0; side < 2; side++) put32(BAR_COPIES + 0x40 * u32(side) + 4 * u32(b), rd0(BAR_NAMES[side] + 4 * u32(b)));
	name_of[f].clear();
	if (f >= 11 && f <= 12) name_of[f + 13].clear();
	part_groups[f].clear();
	hair_hue[f] = -1;
	if (f >= 11 && f <= 12) hair_hue[f + 13] = -1;
	for (int side = 0; side < 2; side++)
		if (tex[side].fighter == f) { set_rewrites(side, false); tex[side].fighter = -1; }
}

// Fighter number f made the plan's fighter: its record (in the place for f), and f's entries in the tables by
// fighter (by its number without the costume: the computer's opponent's both costumes)
void random_fighters::build(int f, const plan &p, int side)
{
	const int b = f % 13, torso = p.rec[G_TORSO];
	const u32 at_ = MADE_AT + MADE * u32(f), hdr = at_, colour = at_ + 0x50, blk = at_ + 0x100, far34 = at_ + 0x440, far44 = at_ + 0x480, cmds = at_ + 0x500;
	if (f == 11 || f == 12) made[f - 11] = { p.place, colour_rng };
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
	int giver = p.head + (f >= 13 || p.head_costume ? 13 : 0);
	if (getenv("VF2_GIVER")) giver = p.rec[G_TORSO];   // TESTONLY: the torso's colours (to compare a body with its own)
	colours(n, giver);
	// (its hair's hue: from the colours' draw as it stands (online, the match's seed's), not a draw of its own)
	hair_hue[f] = int(((colour_rng ^ 0x5bd1e995u) * 2654435761u >> 8) % 1000u);
	if (f >= 11 && f <= 12) hair_hue[f + 13] = hair_hue[f];
	put8(colour, u8(n));
	int slot_rec[16];
	for (int g = 0; g < 7; g++) for (int s : GROUPS[g]) slot_rec[s] = p.rec[g];
	slot_rec[2] = p.head + ((f >= 13 || p.head_costume) && p.head != DURAL ? 13 : 0);
	groups_of(slot_rec, p.rec[G_HANDS], torso, part_groups[f]);
	for (int s = 0; s < 16; s++) {
		const u32 r = record(slot_rec[s]);
		put32(blk + 4 * u32(s), rd0(rd0(r) + 4 * u32(s)));
		put32(far34 + 4 * u32(s), rd0(r + 0x34) ? rd0(rd0(r + 0x34) + 4 * u32(s)) : 0);
		put32(far44 + 4 * u32(s), rd0(r + 0x44) ? rd0(rd0(r + 0x44) + 4 * u32(s)) : 0);
	}
	put32(hdr + 0x34, far34);
	put32(hdr + 0x44, far44);
	put32(hdr + 0x14, rd0(record(p.three[0]) + 0x14));   // (the first's basic moves: its stance and walk)
	put32(hdr + 0x30, rd0(record(slot_rec[2]) + 0x30));   // (the head's texture set; one of its own, build_textures)
	// (both hands' poses, the hands': in the block, and +3c the first's)
	const bool old = getenv("VF2_TEXTURES_OLD") != nullptr;   // TESTONLY: as before (the head's set, the hands' first only)
	if (!old) for (u32 k = 0x40; k < 0x240; k += 4) put32(blk + k, rd0(rd0(record(p.rec[G_HANDS])) + k));
	put32(hdr + 0x3c, old ? rd0(record(p.rec[G_HANDS]) + 0x3c) : blk + 0x40);
	put32(hdr + 0x40, rd0(record(slot_rec[2]) + 0x40));
	put32(hdr + 0x48, rd0(record(slot_rec[2]) + 0x48));   // (its portrait's hair, hair_model: its head's, not its torso's)
	if (!old) build_textures(f, p, side, slot_rec, giver, n);
	put32(RECORDS + 4 * u32(f), hdr);
	if (f >= 11 && f <= 12) put32(RECORDS + 4 * u32(f + 13), hdr);
	// the head: its faces (Dural has none: her head is part 2) and what swings from it
	for (int t = 0; t < N_FACE_LISTS; t++) {
		const u32 list = rd0(FACE_LISTS + 4 * u32(t));
		put32(list + 4 * u32(b), rd0(list + 4 * u32(p.head)));
	}
	put32(ATTACH + 4 * u32(b), rd0(ATTACH + 4 * u32(p.head)));
	put32(ATTACH + 4 * u32(b + 13), rd0(ATTACH + 4 * u32(p.head + 13)));
	// (what it wears that swings: each part's owner's from that part, WORN)
	const std::vector<u32> worn = worn_list(slot_rec);
	const u32 worn_at = worn.size() > 1 ? WORN_COPIES + WORN_COPY * u32(f) : WORN_NONE;
	if (worn.size() > 1) for (size_t k = 0; k < worn.size(); k++) put32(worn_at + 4 * u32(k), worn[k]);
	put32(WORN + 4 * u32(f), worn_at);
	if (f >= 11 && f <= 12) put32(WORN + 4 * u32(f + 13), worn_at);
	// the commands: a dispatch of its own, each key to its giver's program (its move group's owner's, or the next of the
	// three that has it: make_plan)
	auto where = [&](int i, u16 k) -> u32 { for (const auto &[kk, a] : dispatch(p.three[i])) if (kk == k) return a; return 0; };
	u32 w = cmds;
	for (const auto &[k, who] : p.keys) {
		const u32 a = where(who, k);
		if (!a || w + 8 > MADE_AT + MADE * u32(f + 1)) continue;
		put8(w, 0x0e); put8(w + 1, u8(k)); put8(w + 2, u8(k >> 8)); put32(w + 3, a);
		w += 7;
	}
	put8(w, 0x10);
	put32(COMMANDS + 4 * u32(b), cmds);
	// its voice, the first's move at a round's end; the torso's win poses
	for (int s = 1; s < N_VOICES; s++)
		if (const u32 was = rd0(VOICES + 4 * u32(s))) put32(VOICE_COPIES + 0x34 * u32(s - 1) + 4 * u32(b), rd0(was + 4 * u32(p.voice)));
	for (int s = 1; s < N_VOICES; s++)   // (and their lengths: the round's end waits for the win words')
		if (rd0(VOICE_LENGTHS + 4 * u32(s))) put16(LENGTH_COPIES + 0x20 * u32(s) + 2 * u32(b), voice_length(s, p.voice));
	put32(ROUND_COPY + 4 * u32(b), rd0(ROUND_MOVES + 4 * u32(p.three[0])));
	for (u32 t = 0; t < N_POSES; t++) put16(POSE_COPIES + 0x20 * t + 2 * u32(b), rd0_16(rd0(POSES + 4 * t) + 2 * u32(torso % 13)));
	// (its energy bar's name: the first's picture, the box the name is drawn in over it, hud_names)
	for (int side = 0; side < 2; side++) put32(BAR_COPIES + 0x40 * u32(side) + 4 * u32(b), rd0(BAR_NAMES[side] + 4 * u32(p.three[0])));
	name_of[f] = p.name;
	head_of[f] = p.head;
	if (f >= 11 && f <= 12) name_of[f + 13] = p.name;
	if (getenv("VF2_RANDLOG")) {   // TESTONLY
		fprintf(stderr, "MADE %d %s: %s+%s+%s head %s voice %s torso %d arms %d/%d hands %d legs %d/%d/%d keys %zu share %d%%, moves", f, p.name.c_str(),
			NAME[p.three[0]], NAME[p.three[1]], p.three[2] >= 0 ? NAME[p.three[2]] : "-", NAME[p.head], NAME[p.voice], p.rec[0], p.rec[1], p.rec[2], p.rec[3],
			p.rec[4], p.rec[5], p.rec[6], p.keys.size(), p.share);
		for (int g = 0; g < 8; g++) fprintf(stderr, " %d", p.moves[g]);
		fprintf(stderr, "\n");
	}
}

u32 random_fighters::next_colour(u32 n)
{
	u32 &x = colour_rng;
	if (!x && seeded) x = 1;
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
hsv random_fighters::themed(const hsv &c, bool light) const
{
	hsv t = c;
	const float h0 = c.h, turn = col_turn;
	switch (col_theme) {
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
}

void random_fighters::colours(u32 n, int giver)
{
	static const int WEIGHT[THEMES] = { 6, 1, 1, 4, 4, 4, 3, 3 };
	int w = int(next_colour(26)), theme = 0;
	while (theme < THEMES - 1 && w >= WEIGHT[theme]) w -= WEIGHT[theme++];
	float turn = float(next_colour(1000)) / 1000.f;
	if (const char *t = getenv("VF2_THEME")) theme = std::clamp(atoi(t), 0, THEMES - 1);   // TESTONLY
	if (const char *t = getenv("VF2_OUTFIT")) turn = float(atof(t));                     // TESTONLY
	col_theme = theme;
	col_turn = turn;
	col_plain = getenv("VF2_NOCOLOUR") != nullptr;                                     // TESTONLY
	const u32 sr = RAMP_NEW + RAMP_SIZE * u32(giver), dr = RAMP_NEW + RAMP_SIZE * n;
	auto get = [&](u32 a, u32 step) { return to_hsv(level(rd16(a)), level(rd16(a + step)), level(rd16(a + 2 * step))); };
	auto put = [&](u32 a, u32 step, const hsv &c) { float r, g, b; to_rgb(c, r, g, b); put16(a, raw(r)); put16(a + step, raw(g)); put16(a + 2 * step, raw(b)); };
	auto copy = [&](u32 a, u32 step, u32 from) { for (u32 k = 0; k < 3; k++) put16(a + k * step, rd16(from + k * step)); };
	for (u32 k = 0; k < 0x180; k += 2) put16(dr + k, rd16(sr + k));   // (the skin)
	hsv lv[64];
	int top = 0;
	for (int i = 0; i < 64; i++) { lv[i] = get(sr + 0x180 + 2 * u32(i), 0x80); if (lv[i].v > lv[top].v) top = i; }
	const hsv ref = lv[top];
	int lit = 0;   // (light or dark, for black and white or gold: by its levels' brightness on the whole, not its brightest)
	for (const hsv &c : lv) lit += light_colour(c);
	const hsv t = themed(ref, lit > 32);
	for (int i = 0; i < 64; i++) {
		const u32 a = dr + 0x180 + 2 * u32(i);
		if (kept(ref) || col_plain) { copy(a, 0x80, sr + 0x180 + 2 * u32(i)); continue; }
		hsv c = t;
		c.s = std::clamp(ref.s > 0 ? t.s * lv[i].s / ref.s : t.s, 0.f, 1.f);
		c.v = std::clamp(lv[i].v * t.v / ref.v, 0.f, 1.f);
		put(a, 0x80, c);
	}
	for (u32 row = 0; row < 7; row++) flat_copy(n, row, u32(giver), row);
	if (getenv("VF2_RANDLOG")) fprintf(stderr, "COLOURS %u from %d: theme %d turn %.3f, main h%.2f s%.2f v%.2f -> h%.2f s%.2f v%.2f\n", n, giver, theme, turn, ref.h, ref.s, ref.v, t.h, t.s, t.v);   // TESTONLY
}

// A row of the textures' colours (16), from colour number from's row to n's: its cool colours turned as the clothes'
// main colour was (the warm ones are mostly faces: skin, lips, cheeks, beards, browns and golds; the cool ones, greens,
// blues and purples, Pai's patterns, Wolf's bands, clothes for sure)
void random_fighters::flat_copy(u32 n, u32 row, u32 from, u32 from_row)
{
	const u32 sf = FLAT_NEW + FLAT_SIZE * from + 0x60 * from_row, df = FLAT_NEW + FLAT_SIZE * n + 0x60 * row;
	for (u32 i = 0; i < 16; i++) {
		const u32 a = 2 * i;
		const hsv c = to_hsv(level(rd16(sf + a)), level(rd16(sf + a + 0x20)), level(rd16(sf + a + 0x40)));
		if (col_plain || face_colour(c)) { for (u32 k = 0; k < 3; k++) put16(df + a + 0x20 * k, rd16(sf + a + 0x20 * k)); continue; }
		float r, g, b;
		to_rgb(themed(c, light_colour(c)), r, g, b);
		put16(df + a, raw(r)); put16(df + a + 0x20, raw(g)); put16(df + a + 0x40, raw(b));
	}
}

// The textures of fighter number f, a random fighter on side side (see the Textures note): its head's set the base,
// each other fighter's parts' textures where they go in their own set if that's free (not the head's: its head, its
// faces, what it wears, MISC_TILES; nor another's), else in a free place, moved; their colour rows in rows of colour
// number n the head doesn't use. Each tile's texels' fighter and how far they're moved (written by apply_textures),
// and the headers to move (on while f is on the side: frame)
// A worn list's ops (WORN), each with the part its strand hangs from (a strand's kind 1: the one before's); false if
// it isn't one
bool random_fighters::worn_ops(u32 q, std::vector<worn_op> &ops) const
{
	ops.clear();
	int slot = -1;
	for (u32 a = q; a + 4 <= 0x80000 && ops.size() < 1000;) {
		const u32 op = rd0(a);
		if (op == 0) return true;
		if (op >= std::size(WORN_ARGS)) return false;
		if (op == 4 && rd0(a + 4) != 1) slot = int(rd0(a + 8));
		if (op >= 4 && (slot < 0 || slot >= 16)) return false;
		ops.push_back({ a, op, op >= 4 ? slot : -1 });
		a += 4 + 4 * WORN_ARGS[op];
	}
	return false;
}

// A fighter's worn list: of the head's, the torso's, the limbs' owners' lists in turn, each swing (its settings and
// what follows to the next) with the strands that hang from a part of that owner's, those with any, while the work
// they take is no more than the cast's lists take (the head's always is). The cast's own: as it was
std::vector<u32> random_fighters::worn_list(const int slot_rec[16]) const
{
	std::vector<u32> out;
	u32 work = WORN_WORK[0];
	std::vector<int> recs;
	for (int s : { 2, 0, 1, 9, 3, 6, 4, 7, 5, 8, 10, 13, 11, 14, 12, 15 })
		if (std::find(recs.begin(), recs.end(), slot_rec[s]) == recs.end()) recs.push_back(slot_rec[s]);
	std::vector<worn_op> ops;
	for (int rec : recs) {
		if (!worn_ops(rd0(WORN + 4 * u32(rec)), ops)) continue;
		for (size_t i = 0, j; i < ops.size(); i = j) {
			for (j = i + 1; j < ops.size() && ops[j].op != 1; j++) {}
			std::vector<u32> swing;
			u32 w = 0;
			bool any = false;
			for (size_t k = i; k < j; k++) {
				const worn_op &o = ops[k];
				if (o.slot >= 0 && slot_rec[o.slot] != rec) continue;
				any |= o.op == 4;
				for (u32 n = 0; n <= WORN_ARGS[o.op]; n++) swing.push_back(rd0(o.at + 4 * n));
				w += WORN_WORK[o.op];
			}
			if (any && work + w <= worn_limit && 4 * (out.size() + swing.size() + 1) <= WORN_COPY) {
				out.insert(out.end(), swing.begin(), swing.end());
				work += w;
			}
		}
	}
	out.push_back(0);
	return out;
}

std::vector<u32> random_fighters::worn_models(int rec, const std::vector<int> &slots) const
{
	std::vector<u32> ms;
	std::vector<worn_op> ops;
	if (worn_ops(rd0(WORN + 4 * u32(rec)), ops))
		for (const worn_op &o : ops)
			if (o.op == 5 && std::find(slots.begin(), slots.end(), o.slot) != slots.end())
				for (u32 n = 7; n <= 9; n++) if (const u32 m = rd0(o.at + 4 * n); m && m < 0x2000) ms.push_back(m);
	return ms;
}

void random_fighters::build_textures(int f, const plan &p, int side, const int slot_rec[16], int giver, u32 n)
{
	tex_side &t = tex[side];
	set_rewrites(side, false);
	t.rewrite.clear();
	t.fighter = f;
	const int H = p.head;   // (the head's character)
	auto inside = [](int tx, int ty) { return tx >= 0 && ty >= 0 && tx < TW && ty < TH && (tx >= 8 || ty >= 8); };
	// a model's texture headers on a side (u16 indexes into the texture ROM): from, count
	auto headers = [&](u32 model, int sd, u32 &from, u32 &count) {
		from = count = 0;
		if (!model || model >= 0x2000) return;
		const u32 w1 = rdl(MODELS + 16 * model + 4), w3 = rdl(MODELS + 16 * model + 12);
		count = w3 >> 16;
		from = w1 + 4 * count * u32(sd);
		if (count > 0x1000 || 2 * (size_t(from) + 4 * count) > texrom.size()) count = 0;
	};
	struct hdr { int x, y, w, h; u16 h1, h3; };
	auto decode = [&](u32 i, hdr &h) {
		const u16 h0 = trd0(i), h2 = trd0(i + 2);
		h = { 32 * (h2 & 0x3f), 32 * ((h2 >> 6) & 0x1f), 32 << (h0 & 7), 32 << ((h0 >> 3) & 7), trd0(i + 1), trd0(i + 3) };
		return h0 || h2;
	};
	// the models drawn of a record's group of parts (and of its head)
	auto group_models = [&](int rec, int g) {
		std::vector<u32> ms;
		const u32 r = record(rec);
		for (u32 o : { 0u, 0x34u, 0x44u })
			if (const u32 q = rd0(r + o)) for (int s : GROUPS[g]) ms.push_back(rd0(q + 4 * u32(s)));
		if (g == G_TORSO) if (const u32 q = rd0(r + 0x38)) for (u32 k = 0; k < 4; k++) ms.push_back(rd0(q + 4 * k));
		for (u32 mdl : worn_models(rec, GROUPS[g])) ms.push_back(mdl);   // (what swings from them)
		if (g == G_HANDS) {   // (the hands' poses, both hands', in the record's block)
			const u32 b = rd0(r);
			for (u32 k = 0x40; k < 0xc0; k += 4) ms.push_back(rd0(b + k));
			for (u32 k = 0x1c0; k < 0x240; k += 4) ms.push_back(rd0(b + k));
		}
		ms.erase(std::remove_if(ms.begin(), ms.end(), [](u32 m) { return !m || m >= 0x2000; }), ms.end());
		return ms;
	};
	auto head_models = [&](int rec) {
		std::vector<u32> ms;
		const u32 r = record(rec);
		for (u32 o : { 0u, 0x34u, 0x44u }) if (const u32 q = rd0(r + o)) ms.push_back(rd0(q + 8));
		return ms;
	};
	// each tile: the character whose textures it keeps (-1 free)
	std::vector<s8> who(TW * TH, -1);
	auto tiles = [&](const hdr &h, auto &&fn) {
		for (int ty = h.y / 32; ty < (h.y + h.h) / 32; ty++)
			for (int tx = h.x / 32; tx < (h.x + h.w) / 32; tx++) if (inside(tx, ty)) fn(tx, ty);
	};
	auto mark = [&](u32 model, int ch) {
		u32 from, count;
		headers(model, 0, from, count);
		for (u32 k = 0; k < count; k++) { hdr h; if (decode(from + 4 * k, h)) tiles(h, [&](int tx, int ty) { who[ty * TW + tx] = s8(ch); }); }
	};
	for (u32 mdl : head_models(slot_rec[2])) mark(mdl, H);
	if (const u32 hair = hair_model(slot_rec[2])) mark(hair, H);
	// (what swings from the head, WORN; and its shadow's pieces, ATTACH's 15 a costume)
	for (u32 mdl : worn_models(slot_rec[2], { 2 })) mark(mdl, H);
	for (int c : { H, H + 13 })
		if (const u32 q = rd0(ATTACH + 4 * u32(c)))
			for (u32 k = 0; k < 15; k++) if (const u32 m = rd0(q + 24 * k + 4); m && m < 0x2000) mark(m, H);
	if (H < int(std::size(MISC_TILES)))
		for (int ty = 0; ty < TH; ty++) for (int tx = 0; tx < TW; tx++) if (MISC_TILES[H][ty] >> tx & 1) who[ty * TW + tx] = s8(H);
	for (int g = 0; g < 7; g++) if (p.rec[g] % 13 == H) for (u32 mdl : group_models(p.rec[g], g)) mark(mdl, H);
	// the others' parts: their textures' rectangles, those that overlap as one
	struct comp { int ch, x, y, w, h, dx = 0, dy = 0; bool placed = false; };
	std::vector<comp> comps;
	for (int g = 0; g < 7; g++) {
		const int ch = p.rec[g] % 13;
		if (ch == H) continue;
		for (u32 mdl : group_models(p.rec[g], g)) {
			u32 from, count;
			headers(mdl, 0, from, count);
			for (u32 k = 0; k < count; k++) {
				hdr h;
				if (!decode(from + 4 * k, h)) continue;
				bool any = false;
				tiles(h, [&](int, int) { any = true; });
				if (!any) continue;   // (not the fighter's set: the shared black, ...)
				comp c{ ch, h.x, h.y, h.w, h.h };
				for (size_t i = 0; i < comps.size();) {   // (merged with any of its own it overlaps)
					const comp &o = comps[i];
					if (o.ch == ch && o.x < c.x + c.w && c.x < o.x + o.w && o.y < c.y + c.h && c.y < o.y + o.h) {
						const int x1 = std::max(c.x + c.w, o.x + o.w), y1 = std::max(c.y + c.h, o.y + o.h);
						c.x = std::min(c.x, o.x); c.y = std::min(c.y, o.y); c.w = x1 - c.x; c.h = y1 - c.y;
						comps.erase(comps.begin() + long(i));
						i = 0;
					} else i++;
				}
				comps.push_back(c);
			}
		}
	}
	auto each = [&](const comp &c, int dx, int dy, auto &&fn) {
		for (int ty = (c.y + dy) / 32; ty < (c.y + dy + c.h) / 32; ty++)
			for (int tx = (c.x + dx) / 32; tx < (c.x + dx + c.w) / 32; tx++) if (inside(tx, ty)) fn(tx, ty);
	};
	// where they go: their own places if free, in the parts' order; else the first free place they fit
	for (comp &c : comps) {
		bool free = true;
		each(c, 0, 0, [&](int tx, int ty) { free = free && who[ty * TW + tx] < 0; });
		if (!free) continue;
		each(c, 0, 0, [&](int tx, int ty) { who[ty * TW + tx] = s8(c.ch); });
		c.placed = true;
	}
	int moved = 0, lost = 0;
	std::vector<comp *> by_size;   // (the biggest first)
	for (comp &c : comps) if (!c.placed) by_size.push_back(&c);
	std::stable_sort(by_size.begin(), by_size.end(), [](const comp *a, const comp *b) { return a->w * a->h > b->w * b->h; });
	for (comp *cp : by_size) {
		comp &c = *cp;
		const int w = c.w / 32, h = c.h / 32;
		for (int ty = 0; ty + h <= TH && !c.placed; ty++)
			for (int tx = 0; tx + w <= TW && !c.placed; tx++) {
				const int dx = tx * 32 - c.x, dy = ty * 32 - c.y;
				bool ok = true;
				for (int j = 0; j < h && ok; j++)
					for (int i = 0; i < w && ok; i++) ok = inside(tx + i, ty + j) && who[(ty + j) * TW + tx + i] < 0;
				if (!ok) continue;
				c.dx = dx; c.dy = dy; c.placed = true;
				each(c, dx, dy, [&](int x, int y) { who[y * TW + x] = s8(c.ch); });
			}
		if (c.placed) moved++; else lost++;
	}
	// each tile's texels: whose, from how far (the head's everywhere no one else's are, as its set has them; its faces'
	// expressions uploaded over its eyes as the game goes on, the head's faces')
	t.tiles.assign(TW * TH, tex_side::src{});
	for (int ty = 0; ty < TH; ty++)
		for (int tx = 0; tx < TW; tx++)
			if (inside(tx, ty) && (who[ty * TW + tx] < 0 || who[ty * TW + tx] == H)) t.tiles[size_t(ty * TW + tx)] = { H, 0, 0 };
	for (const comp &c : comps) if (c.placed) each(c, c.dx, c.dy, [&](int tx, int ty) { t.tiles[size_t(ty * TW + tx)] = { c.ch, c.dx, c.dy }; });
	t.applied = false;
	t.busy = false;
	t.head_set = rd0(record(slot_rec[2]) + 0x30);
	texture_set_now(f, side);
	// The colours: each row a part uses, of its record's (the head's: the giver's, as colours made them), in its own
	// place if the head doesn't use it, else in one free; then the headers: a moved texture's place, a moved row's
	// colour number
	struct row_src { int rec = -1, row = 0; };
	row_src rows[7];
	const u32 cb0 = side ? 8 : 1;   // (the side's colour numbers for colour number n's seven rows)
	auto flat_row = [&](const hdr &h) { const u32 cb = (h.h3 >> 6) & 0x3ff; return (h.h1 & 0xff) == 1 && cb >= cb0 && cb < cb0 + 7 ? int(cb - cb0) : -1; };
	auto rows_of = [&](u32 model, auto &&fn) {
		u32 from, count;
		headers(model, side, from, count);
		for (u32 k = 0; k < count; k++) { hdr h; if (decode(from + 4 * k, h)) if (const int r = flat_row(h); r >= 0) fn(r); }
	};
	rows[0] = { giver, 0 };
	rows[1] = { giver, 1 };
	for (u32 mdl : head_models(slot_rec[2])) rows_of(mdl, [&](int r) { rows[r] = { giver, r }; });
	if (const u32 hair = hair_model(slot_rec[2])) rows_of(hair, [&](int r) { rows[r] = { giver, r }; });   // (its portrait's hair)
	for (int g = 0; g < 7; g++) if (p.rec[g] == giver) for (u32 mdl : group_models(p.rec[g], g)) rows_of(mdl, [&](int r) { rows[r] = { giver, r }; });
	auto same = [&](const row_src &a, int rec, int row) {
		for (u32 k = 0; k < 0x60; k += 2)
			if (rd16(FLAT_NEW + FLAT_SIZE * u32(a.rec) + 0x60 * u32(a.row) + k) != rd16(FLAT_NEW + FLAT_SIZE * u32(rec) + 0x60 * u32(row) + k)) return false;
		return true;
	};
	std::map<int, std::array<int, 7>> remap;   // (a record's rows: where they are)
	int unmatched = 0;
	for (int g = 0; g < 7; g++) {
		const int rec = p.rec[g];
		if (rec == giver) continue;
		auto &rm = remap.emplace(rec, std::array<int, 7>{ -1, -1, -1, -1, -1, -1, -1 }).first->second;
		for (u32 mdl : group_models(rec, g))
			rows_of(mdl, [&](int r) {
				if (rm[size_t(r)] >= 0) return;
				int to = -1;
				if (rows[r].rec < 0) { rows[r] = { rec, r }; to = r; }
				for (int k = 0; k < 7 && to < 0; k++) if (rows[k].rec >= 0 && same(rows[k], rec, r)) to = k;
				for (int k = 0; k < 7 && to < 0; k++) if (rows[k].rec < 0) { rows[k] = { rec, r }; to = k; }
				if (to < 0) { to = r; unmatched++; }
				rm[size_t(r)] = to;
			});
	}
	for (u32 r = 0; r < 7; r++) if (rows[r].rec >= 0 && rows[r].rec != giver) flat_copy(n, r, u32(rows[r].rec), u32(rows[r].row));
	FILE *log = getenv("VF2_TEXLOG") ? fopen(getenv("VF2_TEXLOG"), "a") : nullptr;   // TESTONLY
	if (log) {   // TESTONLY
		fprintf(log, "BUILD %d side %d head %d giver %d n %u\n", f, side, H, giver, n);
		for (u32 mdl : head_models(slot_rec[2])) {
			u32 from, count;
			headers(mdl, side, from, count);
			for (u32 k = 0; k < count; k++) fprintf(log, "HEAD %d %x %x %04x %04x %04x %04x\n", side, mdl, from + 4 * k, trd0(from + 4 * k), trd0(from + 4 * k + 2), trd0(from + 4 * k + 1), trd0(from + 4 * k + 3));
		}
	}
	for (int g = 0; g < 7; g++) {
		const int rec = p.rec[g], ch = rec % 13;
		if (rec == giver && ch == H) continue;   // (the head's own)
		std::array<int, 7> rm{ -1, -1, -1, -1, -1, -1, -1 };
		if (const auto it = remap.find(rec); it != remap.end()) rm = it->second;
		for (u32 mdl : group_models(rec, g)) {
			u32 from, count;
			headers(mdl, side, from, count);
			for (u32 k = 0; k < count; k++) {
				const u32 i = from + 4 * k;
				hdr h;
				if (!decode(i, h)) continue;
				u16 h2 = trd0(i + 2), h3 = trd0(i + 3);
				for (const comp &c : comps)
					if (c.ch == ch && ch != H && c.placed && (c.dx || c.dy) && h.x >= c.x && h.y >= c.y && h.x + h.w <= c.x + c.w && h.y + h.h <= c.y + c.h) {
						h2 = u16((h2 & ~0x7ff) | ((h.x + c.dx) / 32 & 0x3f) | ((h.y + c.dy) / 32 & 0x1f) << 6);
						break;
					}
				if (const int r = flat_row(h); r >= 0 && rm[size_t(r)] >= 0) h3 = u16((h3 & ~(0x3ff << 6)) | (cb0 + u32(rm[size_t(r)])) << 6);
				if (h2 != trd0(i + 2)) t.rewrite[i + 2] = h2;
				if (h3 != trd0(i + 3)) t.rewrite[i + 3] = h3;
				if (log) fprintf(log, "HDR %d %d %d %x %x %04x %04x %04x %04x %04x\n", side, rec, g, mdl, i, trd0(i), trd0(i + 2), h2, trd0(i + 3), h3);   // TESTONLY
			}
		}
	}
	if (log) fclose(log);   // TESTONLY
	if (getenv("VF2_RANDLOG")) {   // TESTONLY
		fprintf(stderr, "TEXTURES %d side %d: %zu textures of others' (%d moved, %d with no room), %zu headers changed, rows", f, side, comps.size(), moved, lost, t.rewrite.size());
		for (const row_src &r : rows) fprintf(stderr, " %d.%d", r.rec, r.row);
		fprintf(stderr, "%s\n", unmatched ? " (some rows unmatched)" : "");
	}
}

// The headers of a side's random fighter's parts moved (on), or as loaded
void random_fighters::set_rewrites(int side, bool on)
{
	tex_side &t = tex[side];
	if (t.rewritten == on) return;
	for (const auto &[i, v] : t.rewrite) twr(i, on ? v : trd0(i));
	t.rewritten = on;
}

// A character's set's blocks (its record's +30 and the next), as the archive has them
const std::vector<random_fighters::blk> &random_fighters::blocks_of(int ch)
{
	auto it = blocks.find(ch);
	if (it != blocks.end()) return it->second;
	std::vector<blk> &out = blocks[ch];
	const u32 first = rd0(record(ch) + 0x30);
	for (u32 sn = first; sn <= first + 1 && sn < u32(N_ARC_SETS); sn++) {
		const u32 l = rdl(ARC_SETS_WAS + 4 * sn), d = rdl(ARC_DESCS_WAS + 4 * rdl(l)), cnt = rdl(d);
		for (u32 k = 0; k < cnt && k < 64; k++) {
			const u32 g2 = rdl(d + 4 + 4 * k), dat = rdl(l + 4 + 4 * k), i = (g2 & 0xffff) >> 1;
			if (i >= 24 || dat < 0x02000000 || dat - 0x02000000 + 8 > data.size()) continue;
			const int y = rd0_16(0x4c120 + 4 * i) + int(g2 >> 24), x = rd0_16(0x4c122 + 4 * i) + int((g2 >> 16) & 0xff);
			out.push_back({ x, y, *at(dat + 5) + 1, *at(dat + 4) + 1, dat });
		}
	}
	return out;
}

void random_fighters::decode_all()
{
	if (!decoder) decoder = std::make_unique<tex_decoder>(prog, data);
	for (int ch = 0; ch < 11; ch++)
		for (const blk &b : blocks_of(ch))
			if (!decoded.count(b.data)) {
				std::vector<tex_decoder::unit> out;
				if (!decoder->decode(b.data, b.x, b.y, out)) out.clear();
				decoded.emplace(b.data, std::move(out));
			}
}

// The texture set a random fighter's record names: the one its side has (or is loading) if it's a fighter's, so
// nothing more is loaded (its tiles are all written over: apply_textures), else its head's
void random_fighters::texture_set_now(int f, int side)
{
	const auto &ram = m.m_workram;
	const u32 e = SET_REQUEST[side][0] - 0x500000, now = u32(ram[e] | ram[e + 1] << 8);
	u32 set = tex[side].head_set;
	for (int c = 0; c < 13; c++) if (now && now == rd0(record(c) + 0x30)) set = now;
	put32(MADE_AT + MADE * u32(f) + 0x30, set);
}

// A side's random fighter's textures written to texture RAM: each tile's from its fighter's set's blocks (decoded as
// the game would write them, level 0 and every mipmap), moved as far as its texture was
void random_fighters::apply_textures(int side)
{
	tex_side &t = tex[side];
	if (t.tiles.empty()) return;
	if (!decoder) decoder = std::make_unique<tex_decoder>(prog, data);
	std::vector<tex_side::src> srcs;
	for (const tex_side::src &sr : t.tiles) if (sr.ch >= 0 && std::find(srcs.begin(), srcs.end(), sr) == srcs.end()) srcs.push_back(sr);
	size_t written = 0;
	for (const tex_side::src &sr : srcs)
		for (const blk &b : blocks_of(sr.ch)) {
			bool any = false;   // (any of its tiles, moved, this source's)
			for (int ty = std::max(0, (b.y + sr.dy) / 32); ty < TH && ty * 32 < b.y + sr.dy + b.h && !any; ty++)
				for (int tx = std::max(0, (b.x + sr.dx) / 32); tx < TW && tx * 32 < b.x + sr.dx + b.w && !any; tx++) any = t.tiles[size_t(ty * TW + tx)] == sr;
			if (!any) continue;
			auto it = decoded.find(b.data);
			if (it == decoded.end()) {
				std::vector<tex_decoder::unit> out;
				if (!decoder->decode(b.data, b.x, b.y, out)) out.clear();
				it = decoded.emplace(b.data, std::move(out)).first;
			}
			for (const tex_decoder::unit &u : it->second) {
				const int px = int(u.at & 511) * 2, py = int(u.at >> 9) * 2;
				int level = 0, c = 0, x0 = px, y0 = py;
				if (py >= 1024) {   // (a mipmap: level n at 1024 - 2048 >> n across, 1536 + half that down, a half each)
					if (py < 1536) continue;
					level = 1;
					while (level < 10 && px >= 1024 - (2048 >> (level + 1))) level++;
					c = 1024 - (2048 >> level);
					x0 = (px - c) << level;
					y0 = (py - 1536 - c / 2) << level;
				}
				const int x = x0 + sr.dx, y = y0 + sr.dy;
				if (x < 0 || y < 0 || x / 32 >= TW || y / 32 >= TH || !(t.tiles[size_t(y / 32 * TW + x / 32)] == sr)) continue;
				const int nx = level ? c + ((x >> level) & ~1) : x, ny = level ? 1536 + c / 2 + ((y >> level) & ~1) : y;
				const bool sheet1 = (u.sheet != 0) != (side != 0);   // (decoded as player 1's; player 2's on the other sheets)
				m.write_dword((sheet1 ? 0x12400000u : 0x12000000u) + (u32(ny / 2) * 512 + u32(nx / 2)) * 4, u.v);
				written++;
			}
		}
	t.applied = true;
	t.busy = false;
	if (getenv("VF2_RANDLOG")) fprintf(stderr, "TEXTURES side %d written: %zu units, %zu blocks decoded so far\n", side, written, decoded.size());   // TESTONLY
}


// Random's: one of the named fighters (the first seed of each name), so its name, typed as a seed, is that same
// fighter; Seed's, its own. (Random's draw from the clock: a new one each game)
u32 random_fighters::seed_for(int pl) { return mode[pl] == 2 ? seed[pl] : random_named(); }

u32 random_fighters::random_named()
{
	m.random_fighter_names();
	if (entries.empty()) return 1;
	static u32 clock = u32(std::chrono::steady_clock::now().time_since_epoch().count()) ^ 0x9e3779b9u;
	u32 &x = seeded ? draw_rng : clock;   // (online: the match's seed's)
	if (!x) x = 1;
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	return 1 + x % u32(entries.size());   // (the whole book's)
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
	if (game == 7 && !from_select) select_at = u32(m.m_frame_number);   // (a select's first frame: select_inputs' pace from the next)
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
		if (now_picking && !picking[pl]) { build(carrier, make_plan(seed_for(pl)), pl); }
		picking[pl] = now_picking;
		// (while the select's up, the fighter the select has: a picking player's cursor's (its portrait, colours and
		// profile as ever: select_inputs puts it on its head's slot); one already playing, at a challenger's select, the
		// random fighter itself (the portrait its own head and hair, in its colours and textures, as in the fight). The
		// random fighter's from the VS screen (8) on, as the fight is about to load it. Its name under the portrait
		// covered (select_cover): the slot's isn't its own)
		const int bl = block(pl);
		if (bl < 0) continue;
		// (picked at a select that shows: the random fighter itself from now, its portrait its own head in its colours: its
		// side's textures and colours loaded again for it, as the game's own request (0x4b838: 3, the set, the side, the
		// colour number at 5502c0 or 5502d0, and 550000 set), its textures put together once they're in)
		if (game == 7 && (flags >> 31) && (flags & 0xa) == 8 && !name_of[carrier].empty() && ram[bl + 0x1b0] != carrier && !select_hidden()) {
			const u32 rec = rdl(RECORDS + 4 * u32(carrier)), side = ram[bl + 4] & 1, set = rdl(rec + 0x30), col = rdl(rec + 8);
			ram[bl + 0x1b0] = ram[bl + 0x1b1] = u8(carrier);
			for (u32 k = 0; k < 0x40; k++) ram[bl + 0x1b4 + k] = *at(col + k);
			memcpy(&ram[bl + 0x640], &set, 4);
			const u32 req = side ? 0x502d0 : 0x502c0, words[4] = { 3, set, side, ram[bl + 0x1b4] };
			memcpy(&ram[req], words, 16);
			const u32 one = 1;
			memcpy(&ram[0x50000], &one, 4);
		}
		if (game == 8 && from_select && at_select[pl]) { ram[bl + 0x1b0] = ram[bl + 0x1b1] = u8(carrier); texture_set_now(carrier, pl); }
	}
	if (game == 7) select_cover();
	{   // (a random player's portrait hidden till it's the random fighter's own, in its textures and colours: while it
		// picks, its cursor walking to its head's slot, and till its side's textures are in; and 6 frames on, the picture
		// behind the game's state. Drawing only, but for a Dural head's, below)
		bool hide[2] = { false, false };
		if (game == 7 && !select_hidden())
			for (int pl = 0; pl < 2; pl++) {
				const int o = select_object(pl), bl = block(pl);
				const u32 flags = select_flags(pl);
				if (!mode[pl] || o < 0 || bl < 0 || !(flags >> 31) || !(flags & 8)) continue;
				const tex_side &t = tex[ram[bl + 4] & 1];
				// (a Dural head: the select's portrait is a face list's (face list 7, 0x31c94), and Dural has none: the
				// carrier's, Lion's face drawn in her chrome, would show. None drawn)
				if ((flags & 2) || ram[bl + 0x1b0] != 11 + pl || !t.applied || t.busy) hide_until[pl] = m.m_frame_number + 6;
				// (a Dural head: its portrait the head the block draws (+48, the face lists' head: she has none, the one before's
				// stayed, Lion's), her chrome head, the record's part 2; and no hair (+768: the record's +48's, read as it loads))
				if (head_of[11 + pl] == DURAL && ram[bl + 0x1b0] == 11 + pl) {
					const u32 head = rdl(rdl(rdl(RECORDS + 4 * u32(11 + pl))) + 8), none = 0;
					if (memcmp(&ram[bl + 0x48], &head, 4)) memcpy(&ram[bl + 0x48], &head, 4);
					if (memcmp(&ram[bl + 0x768], &none, 4)) memcpy(&ram[bl + 0x768], &none, 4);
				}
				hide[pl] = m.m_frame_number <= hide_until[pl];
			}
		model2_video &v = *m.m_video;
		if (hide[0] != v.m_hide_portrait[0] || hide[1] != v.m_hide_portrait[1]) { m.render_wait(); v.m_hide_portrait[0] = hide[0]; v.m_hide_portrait[1] = hide[1]; }
		// (and a random player's cursor frame on the icons: its slot isn't its fighter's, as VF1 Recomp covers its own)
		u8 frames = 0;
		if (game == 7) for (int pl = 0; pl < 2; pl++) if (mode[pl] && select_in(pl)) frames |= u8(1 << pl);
		if (frames != m.m_tiles.hidden_frames()) { m.render_wait(); m.m_tiles.hide_frames(frames); }
	}
	if (game == 3) attract_frame();
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
		build(f, make_plan(random_named()), pl);
		cpu_made[pl] = f;
	}
	// (a side's random fighter's parts' texture headers moved while it's the fighter on that side: off at a select's
	// portrait, or another fighter's)
	bool on_side[2] = { false, false };
	for (int pl = 0; pl < 2; pl++)
		if (const int bl = block(pl); bl >= 0 && ram[bl + 4] < 2 && tex[ram[bl + 4]].fighter >= 0 && ram[bl + 0x1b0] == tex[ram[bl + 4]].fighter) on_side[ram[bl + 4]] = true;
	for (int side = 0; side < 2; side++) set_rewrites(side, on_side[side]);
	// (its textures written once the side's set is loaded, and again whenever it's loaded since: while it's the
	// fighter on that side)
	for (int side = 0; side < 2; side++) {
		tex_side &t = tex[side];
		if (t.tiles.empty()) continue;
		if (!on_side[side]) { t.applied = false; continue; }
		bool idle = true;
		for (u32 e : SET_REQUEST[side]) if (s16(ram[e - 0x500000 + 2] | ram[e - 0x500000 + 3] << 8) != 0) idle = false;
		if (!idle) t.busy = true;
		else if (!t.applied || t.busy) apply_textures(side);
	}
	build_frame(game);
	std::string hud[2];
	if (game == 9 || game == 3)   // (a fight; the attract's, its demo's bars)
		for (int pl = 0; pl < 2; pl++)
			if (const int bl = block(pl); bl >= 0 && ram[bl + 0x1b0] < N_RECORDS) hud[pl] = name_of[ram[bl + 0x1b0]];
	std::lock_guard<std::mutex> lk(m.m_hud_mx);
	m.m_random_hud[0] = hud[0];
	m.m_random_hud[1] = hud[1];
}

// The round-1 build (model2_video's): a fighter's models by group, feet up as VF1 Recomp's: 0 the feet, 1 the shins, 2
// the thighs, 3 the hips and waist, 4 the torso (and its other torsos, +38), 5 the upper arms, 6 the forearms, 7 the
// hands (and their poses), 8 the head; each part's at every distance (+34, +44), and what swings from it with it
namespace {
constexpr u8 SLOT_GROUP[16] = { 3, 3, 8, 5, 6, 7, 5, 6, 7, 4, 2, 1, 0, 2, 1, 0 };
constexpr int BUILD_DELAY = 3;   // (frames after a match's intro began: as the camera starts to sweep in)
}

void random_fighters::groups_of(const int slot_rec[16], int hands, int torso, std::map<u32, u8> &out) const
{
	out.clear();
	auto put = [&](u32 mdl, u8 g) { if (mdl && mdl < 0x2000) out.emplace(mdl, g); };
	for (int s = 0; s < 16; s++) {
		const u32 r = record(slot_rec[s]);
		for (u32 o : { 0u, 0x34u, 0x44u }) if (const u32 q = rd0(r + o)) put(rd0(q + 4 * u32(s)), SLOT_GROUP[s]);
		for (u32 mdl : worn_models(slot_rec[s], { s })) put(mdl, SLOT_GROUP[s]);
	}
	if (const u32 q = rd0(record(torso) + 0x38)) for (u32 k = 0; k < 4; k++) put(rd0(q + 4 * k), 4);
	put(hair_model(slot_rec[2]), 8);
	// (the head as the face lists draw it: their expressions' models (a blink, a pain face as it's hit), each a frame's,
	// in place of part 2; each list's entries chained (+10), +08 the frames: +02 their count, +04 a table of 16 bytes a
	// model (the costumes' at +0, +4, +8), from +08 each frame's index: 0x4b730)
	const int hb = slot_rec[2] % 13;
	for (int t = 0; t < N_FACE_LISTS && hb < 13; t++) {
		u32 e = rd0(rd0(FACE_LISTS + 4 * u32(t)) + 4 * u32(hb));
		for (int k = 0; k < 64 && e >= 0x02000000; k++) {
			const u32 fr = rd0(e + 8);
			if (fr >= 0x02000000) {
				const u32 n = std::min<u32>(rd0_16(fr + 2), 64), tab = rd0(fr + 4);
				for (u32 i = 0; i < n && tab >= 0x02000000; i++) {
					const u32 idx = *at0(fr + 8 + i, 1);
					for (u32 v = 0; v < 12; v += 4) put(rd0(tab + 16 * idx + v), 8);
				}
			}
			const u32 nx = rd0(e + 16);
			if (nx == e) break;
			e = nx;
		}
	}
	const u32 b = rd0(record(hands));
	for (u32 k = 0x40; k < 0xc0; k += 4) put(rd0(b + k), 7);
	for (u32 k = 0x1c0; k < 0x240; k += 4) put(rd0(b + k), 7);
}

const std::map<u32, u8> &random_fighters::groups(int f)
{
	if (part_groups[f].empty() && name_of[f].empty()) {   // (a cast fighter's: its own parts)
		int slot_rec[16];
		for (int &r : slot_rec) r = f;
		groups_of(slot_rec, f, f, part_groups[f]);
	}
	return part_groups[f];
}

// After each frame: a match's intro (round state 2 or 3, its first round's: the next rounds start at 4) starts the
// build of each side's random fighter, unless it's one built since the last select (a 1P run's next match: its new
// opponent's only); the fight's start (8), or anything but a fight, ends it (none half built)
void random_fighters::build_frame(int game)
{
	auto &ram = m.m_workram;
	const u8 round = ram[0x31];
	if (game == 7) built[0].clear(), built[1].clear();
	const bool was = intro_at >= 0;
	if (game == 9 && (round == 2 || round == 3)) intro_at = int64_t(m.m_frame_number);
	if (game != 9 || round >= 8) intro_at = -1;
	int want[2] = { -1, -1 };
	int64_t start[2] = { -1, -1 };
	if (intro_at >= 0)
		for (int pl = 0; pl < 2; pl++) {
			u32 b;
			memcpy(&b, &ram[0x804 + 4 * pl], 4);
			if (b < 0x500000 || b - 0x500000 + 0x200 >= ram.size()) continue;
			const int f = ram[b - 0x500000 + 0x1b0];
			if (f >= N_RECORDS) continue;
			want[pl] = f;
			if (!was) {   // (the intro's first frame: whose)
				build_side[pl] = !name_of[f].empty() && name_of[f] != built[pl];
				if (build_side[pl]) built[pl] = name_of[f];
			}
			if (build_side[pl]) start[pl] = intro_at + BUILD_DELAY;
		}
	// (and each side's fighter, a random one's hair in its own hue: in a match, at its select (the portraits) and in the
	// attract; not Dural's head, her chrome as it is)
	int hue[2] = { -1, -1 };
	if (intro_at < 0 && (game == 3 || (game >= 7 && game <= 9)))
		for (int pl = 0; pl < 2; pl++) {
			u32 b;
			memcpy(&b, &ram[0x804 + 4 * pl], 4);
			if (b < 0x500000 || b - 0x500000 + 0x200 >= ram.size()) continue;
			const int f = ram[b - 0x500000 + 0x1b0];
			if (f < N_RECORDS) want[pl] = f;
		}
	for (int pl = 0; pl < 2; pl++)
		if (want[pl] >= 0 && !name_of[want[pl]].empty() && head_of[want[pl]] != DURAL && head_of[want[pl]] != 8) hue[pl] = hair_hue[want[pl]] + (head_of[want[pl]] == 3 ? 1000 : 0);   // (Dural's chrome and Shun's white hair as they are)
	model2_video &v = *m.m_video;
	if (v.m_portraits != (game == 7)) { m.render_wait(); v.m_portraits = game == 7; }
	if (start[0] < 0 && start[1] < 0 && hue[0] < 0 && hue[1] < 0) { build_stop(); return; }
	if (want[0] == build_fighter[0] && want[1] == build_fighter[1] && start[0] == build_start[0] && start[1] == build_start[1]
		&& hue[0] == v.m_hair_hue[0] && hue[1] == v.m_hair_hue[1]) return;
	m.render_wait();
	v.m_build_parts.clear();
	for (int pl = 0; pl < 2; pl++) {
		build_fighter[pl] = want[pl];
		build_start[pl] = v.m_build_start[pl] = start[pl];
		v.m_hair_hue[pl] = int16_t(hue[pl]);
		if (want[pl] < 0) continue;
		for (const auto &[mdl, g] : groups(want[pl])) {
			const u32 oba = rdl(MODELS + 16 * mdl + 8), n = rdl(MODELS + 16 * mdl + 12) & 0xffff;
			if (!oba) continue;
			model2_video::build_part &bp = v.m_build_parts[oba];
			bp.sides |= u8(1 << pl);
			bp.group[pl] = g;
			bp.polys = u16(std::min<u32>(n, 0xffff));
		}
	}
	if (getenv("VF2_RANDLOG")) fprintf(stderr, "BUILD frame %llu: sides %d %d from %lld %lld, %zu models\n", (unsigned long long)m.m_frame_number, want[0], want[1], (long long)start[0], (long long)start[1], v.m_build_parts.size());   // TESTONLY
}

void random_fighters::build_stop()
{
	if (build_fighter[0] < 0 && build_fighter[1] < 0 && build_start[0] < 0 && build_start[1] < 0) return;
	m.render_wait();
	model2_video &v = *m.m_video;
	v.m_build_parts.clear();
	v.m_build_start[0] = v.m_build_start[1] = -1;
	v.m_hair_hue[0] = v.m_hair_hue[1] = -1;
	build_fighter[0] = build_fighter[1] = -1;
	build_start[0] = build_start[1] = -1;
}

// The select, VF1 Recomp's way: a random player's controls let go while it's up and its pick pressed for it, from the
// select's state and the frames since it began only (online, under rollback: the same on both sides). Everyone at it
// random (one player alone, or both, offline or online): the select hidden, the picks at once where the cursors are
// (who they got a surprise as the fight comes in). A player picking as ever beside a random one: the select shows,
// theirs as ever, the random one's cursor moved to its fighter's head's slot (the portrait its head's, as the select
// has it: Dural's none, the cursor stays) and picked at once. A player already in (a challenger's select) isn't
// picking, and isn't picked again. The select's object: +06 the cursor's slot (0-9, the slots' fighters 0x33e4c's, or
// the copy the Secret character's put there: the address in the select's first ldib, 32140)
namespace {
constexpr u32 SELECT_SLOTS_AT = 0x00032140, SELECT_SLOTS = 0x00033e4c, SELECT_COPY = 0x001fff00;
constexpr int N_SLOTS = 10;
constexpr u8 PICK = 0x01, RIGHT = 0x40, LEFT = 0x80;   // (Punch: the pick, the costume as the cursor's)
}

int random_fighters::select_object(int pl) const
{
	const auto &ram = m.m_workram;
	if (ram.size() < 0x900 || ram[0x2b] != 7) return -1;
	u32 o;
	memcpy(&o, &ram[pl ? 0x840 : 0x83c], 4);
	if (o < 0x500000 || o - 0x500000 + 8 >= ram.size() || ram[o - 0x500000 + 4] != pl) return -1;
	return int(o - 0x500000);
}

u32 random_fighters::select_flags(int pl) const
{
	const int o = select_object(pl);
	u32 f = 0;
	if (o >= 0) memcpy(&f, &m.m_workram[size_t(o)], 4);
	return f;
}

bool random_fighters::select_hidden() const
{
	if (!on || (!mode[0] && !mode[1])) return false;
	const bool in0 = select_in(0), in1 = select_in(1);
	return (in0 || in1) && (!in0 || mode[0]) && (!in1 || mode[1]);   // (everyone at it random)
}

// The select's names under the portraits (tile map 0, rows 30-32: player 1's box columns 8-20, player 2's 41-53, the
// name font's tiles, b000 its blank): a random player's covered with the blank, as VF1
// Recomp gilds its own over (the slot's name isn't the random fighter's). Tile RAM is the game's (in the save states:
// online, alike on both sides), the name drawn again as the select's next scene comes
void random_fighters::select_cover()
{
	if (!on || select_hidden()) return;
	for (int pl = 0; pl < 2; pl++) {
		if (!mode[pl] || !select_in(pl)) continue;
		const int x0 = pl ? 41 : 8;
		bool box = true;   // (the box as known: the name font's tiles, b0xx)
		for (int y = 30; y <= 32 && box; y++) for (int x = x0; x <= x0 + 12; x++) box = box && (m.m_tiles.tile_r(u32(y * 64 + x)) >> 8) == 0xb0;
		if (!box) continue;
		for (int y = 30; y <= 32; y++)
			for (int x = x0; x <= x0 + 12; x++)
				if (m.m_tiles.tile_r(u32(y * 64 + x)) != 0xb000) { m.render_wait(); m.m_tiles.tile_w(u32(y * 64 + x), 0xb000, 0xffff); }
	}
}

// The attract (game 3: its intro's scenes and its demo fight), VF1 Recomp's way: a side whose player has a random
// fighter (Random: a new one for each demo fight; a Seed's: that one), or every side with Random opponents, shows a
// random fighter in place of the cast member the attract puts there: the side's carrier (11, 12). The intro's scenes
// take theirs from two lists in the data ROM (attract_scenes: 16 bytes a scene, +0c the fighter, +0a its frame, 0
// after the last; read by 0x549e8 as the scene starts, its models and textures loaded from it at once), made the
// carrier's while the side is random; the demo fight's pair (0xbdc8: two of the cast at random) and the first scene
// after a boot (0xb1ac) are put in its block as the attract sets them (the frame after: the game asks for their
// textures two or three frames later, loading them by the carrier's record), the carrier made afresh then. From the
// game's state and the draws alone (online, the match's seed's: alike on both sides); none of it with no random fighter
namespace {
constexpr u32 SCENES[2] = { 0x0201f454, 0x0201f258 };   // (player 1's side's, player 2's)
constexpr u8 SCENE_CAST[2][20] = { { 8, 4, 4, 8, 8, 4, 4, 8, 8, 7, 7, 8, 8, 3, 3, 3, 8, 8, 1, 1 },
                                   { 6, 6, 6, 2, 2, 2, 5, 5, 5, 10, 10, 10, 10, 0, 0, 0 } };
constexpr int SCENE_N[2] = { 20, 16 };
}

void random_fighters::attract_scenes()
{
	for (int pl = 0; pl < 2; pl++) {
		auto was = [&](u32 a) { const auto it = undo.find(a); return it != undo.end() ? it->second : *at(a); };   // (as loaded)
		bool known = was(SCENES[pl] + 16 * u32(SCENE_N[pl]) + 0xa) == 0 && was(SCENES[pl] + 16 * u32(SCENE_N[pl]) + 0xb) == 0;
		for (int k = 0; k < SCENE_N[pl] && known; k++) known = was(SCENES[pl] + 16 * u32(k) + 0xc) == SCENE_CAST[pl][k];   // (the lists as known: their fighters, then an end)
		if (!known) continue;
		const bool random = mode[pl] || cpu;
		for (int k = 0; k < SCENE_N[pl]; k++) put8(SCENES[pl] + 16 * u32(k) + 0xc, random ? u8(11 + pl) : SCENE_CAST[pl][k]);
	}
}

void random_fighters::attract_frame()
{
	auto &ram = m.m_workram;
	for (int pl = 0; pl < 2; pl++) {
		if (!mode[pl] && !cpu) continue;
		u32 b;
		memcpy(&b, &ram[0x804 + 4 * pl], 4);
		if (b < 0x500000 || b - 0x500000 + 0x200 >= ram.size()) continue;
		const u32 bl = b - 0x500000;
		const int f = ram[bl + 0x1b0], carrier = 11 + pl;
		if (f >= N_RECORDS || f % 13 >= 11) continue;   // (the carrier already, or none the attract put)
		build(carrier, make_plan(mode[pl] ? seed_for(pl) : random_named()), pl);
		ram[bl + 0x1b0] = ram[bl + 0x1b1] = u8(carrier);
	}
	// Random opponents (a side whose player is on Normal): a new random fighter as each of the intro's scenes brings in
	// another of the cast on that side (the one before gone from the screen), as VF1 Recomp makes a record afresh once
	// it's off the screen. The scene just started, by the side's scene pointer (5001c8 player 1's, 5001cc player 2's:
	// the next scene's entry); the carrier made again and its block loaded again by the game's own loader (+0c, the
	// block's task: 0x13f08, run at its next turn: its models, textures and colours the new fighter's)
	for (int pl = 0; pl < 2; pl++) {
		u32 ptr;
		memcpy(&ptr, &ram[0x1c8 + 4 * pl], 4);
		if (ptr == scene_ptr[pl]) continue;
		scene_ptr[pl] = ptr;
		if (mode[pl] || !cpu || ptr < SCENES[pl] + 16 || (ptr - SCENES[pl]) % 16) continue;
		const u32 k = (ptr - SCENES[pl]) / 16 - 1;
		if (k >= u32(SCENE_N[pl]) || (k > 0 && SCENE_CAST[pl][k] == SCENE_CAST[pl][k - 1])) continue;
		u32 b;
		memcpy(&b, &ram[0x804 + 4 * pl], 4);
		if (b < 0x500000 || b - 0x500000 + 0x200 >= ram.size()) continue;
		const u32 bl = b - 0x500000, carrier = 11 + pl, loader = 0x13f08;
		if (ram[bl + 0x1b0] != carrier) continue;
		build(int(carrier), make_plan(random_named()), pl);
		memcpy(&ram[bl + 0xc], &loader, 4);
	}
}

int random_fighters::select_slot(int base) const
{
	u32 table;
	memcpy(&table, &prog[SELECT_SLOTS_AT], 4);
	if ((table != SELECT_SLOTS && table != SELECT_COPY) || table + N_SLOTS > prog.size()) return -1;
	for (int s = 0; s < N_SLOTS; s++) if (prog[table + u32(s)] == base) return s;
	return -1;
}

void random_fighters::select_inputs(u8 &in1, u8 &in2)
{
	if (!on || (!mode[0] && !mode[1]) || m.m_workram[0x2b] != 7) return;
	const int64_t f = int64_t(u32(m.m_frame_number) - select_at), ph = f % 40;
	u8 *const in[2] = { &in1, &in2 };
	const bool hidden = select_hidden();
	int k = 0;
	for (int pl = 0; pl < 2; pl++) {
		if (!mode[pl]) continue;
		const int at = k++ ? 30 : 10;   // (the first random player's pick at 10 of every 40 frames, the second's at 30)
		*in[pl] = 0xff;
		const int o = select_object(pl);
		const u32 flags = select_flags(pl);
		if (o < 0 || !(flags >> 31) || (flags & 0xa) != 0xa) continue;   // (in and picking)
		const int cursor = m.m_workram[size_t(o) + 6];
		const int head = hidden ? -1 : select_slot(head_of[11 + pl]);
		const int to = head < 0 ? cursor : head;
		if (cursor != to) { if (f % 8 < 3) *in[pl] &= u8(~(to > cursor ? RIGHT : LEFT)); continue; }
		if (ph >= at && ph < at + 6) *in[pl] &= u8(~PICK);
	}
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
	const random_fighters::plan p = r.make_plan(it->second);
	auto who = [](int rec) { std::string n = NAME[rec % 13]; for (size_t i = 1; i < n.size(); i++) n[i] = char(tolower(u8(n[i]))); return n; };
	auto list = [&](std::initializer_list<int> recs) {
		std::string out;
		for (int rec : recs) if (out.find(who(rec)) == std::string::npos) out += (out.empty() ? "" : ", ") + who(rec);
		return out;
	};
	std::string moves;
	static const char *const GROUP[8] = { "P", "K", "P+K", "P+G", "K+G", "P+K+G", "throws", "the rest" };
	for (int i = 0; i < 3 && p.three[i] >= 0; i++) {
		std::string mine;
		for (int g = 0; g < 8; g++) if (g != 3 && p.moves[g] == i) mine += std::string(mine.empty() ? "" : " ") + GROUP[g];
		moves += (i ? "; " : "") + who(p.three[i]) + (mine.empty() ? "" : " " + mine);
	}
	return who(p.head) + "|" + list({ p.rec[0] }) + "|" + list({ p.rec[1], p.rec[2], p.rec[3] }) + "|" +
		list({ p.rec[4], p.rec[5], p.rec[6] }) + "|" + moves + "|" + who(p.voice);
}

// The book as the recipe spec's table (name leader second third head voice, as carried: "-" none), or (owners) each
// name's as VF3 Recomp lists them: its head in this game, then its move groups' owners and its part groups' (0 the first)
std::vector<std::string> model2a::random_fighter_book(bool owners)
{
	random_fighter_names();
	random_fighters &r = randoms();
	std::vector<std::string> out;
	for (u32 place = 1; place <= r.entries.size(); place++) {
		const random_fighters::entry &e = r.entries[place - 1];
		std::string l = e.name;
		for (int i = 0; i < 3; i++) l += std::string(" ") + (e.three[i] >= 0 ? NAME[e.three[i]] : "-");
		if (!owners) { l += " " + e.book_head + " " + NAME[e.voice]; out.push_back(l); continue; }
		const random_fighters::plan p = r.make_plan(place);
		l += std::string(" ") + NAME[p.head] + " " + NAME[p.voice];
		for (int g = 0; g < 8; g++) l += " " + std::to_string(p.moves[g]);
		l += " |";
		for (int g = 0; g < 7; g++) {
			int k = 0;
			while (k < 2 && p.three[k] != p.rec[g] % 13) k++;
			l += " " + std::to_string(k);
		}
		out.push_back(l);
	}
	return out;
}

void model2a::random_frame()
{
	if (m_random && m_random->on) m_random->frame();
}

void model2a::random_select_inputs()
{
	if (m_random && m_random->on) m_random->select_inputs(m_in1, m_in2);
}

bool model2a::random_select_hidden() const { return m_random && m_random->select_hidden(); }

// The ranking (backup RAM 3100, and its working copy in work RAM 9c100: 16 bytes an entry, +0 the time, +4 the
// initials, +0a the fighter without the costume) names a random fighter MUE or KOJAC (its number, 11 or 12). Each such
// entry's random fighter is learnt as it comes in (the name of the fighter with that number then) and kept by its
// bytes; where the game draws the column (tile map 0: a ranking row three tiles high, the initials at columns 32-37,
// the fighter's name from 42, a letter two tiles across), its name is drawn instead, in the game's own letters
// (drawing only: segas24_tile::substitute). The letters' tiles as the ranking draws them (A-Z, those its names and
// initials use: every letter of the book's names)
namespace {
constexpr u32 RANK_BACKUP = 0x3100, RANK_WORK = 0x9c100, RANK_N = 20;
struct glyph { char c; u16 t[6]; };
constexpr glyph GLYPHS[] = {
	{ '.', { 0x8315, 0x8315, 0x8315, 0x8315, 0x8341, 0x8342 } }, { 'A', { 0x8300, 0x8301, 0x8307, 0x8308, 0x8310, 0x8310 } },
	{ 'C', { 0x8300, 0x8301, 0x830a, 0x830b, 0x8313, 0x8312 } }, { 'D', { 0x8302, 0x8301, 0x830a, 0x830a, 0x8311, 0x8312 } },
	{ 'E', { 0x8302, 0x8303, 0x8307, 0x830c, 0x8311, 0x8314 } }, { 'F', { 0x8302, 0x8304, 0x8307, 0x830d, 0x8310, 0x8315 } },
	{ 'G', { 0x8300, 0x8305, 0x830e, 0x830f, 0x8313, 0x8316 } }, { 'H', { 0x8306, 0x8306, 0x8307, 0x8308, 0x8310, 0x8310 } },
	{ 'I', { 0x8317, 0x8318, 0x831d, 0x831e, 0x8325, 0x8326 } }, { 'J', { 0x8315, 0x8306, 0x831f, 0x830a, 0x8313, 0x8312 } },
	{ 'K', { 0x8306, 0x8306, 0x8320, 0x8321, 0x8310, 0x8310 } }, { 'L', { 0x8306, 0x8315, 0x830a, 0x8315, 0x8311, 0x8314 } },
	{ 'M', { 0x8319, 0x831a, 0x830a, 0x830a, 0x8310, 0x8310 } }, { 'N', { 0x831b, 0x831c, 0x8322, 0x8323, 0x8310, 0x8310 } },
	{ 'O', { 0x8300, 0x8301, 0x830a, 0x830a, 0x8313, 0x8312 } }, { 'P', { 0x8302, 0x8301, 0x8307, 0x8324, 0x8310, 0x8315 } },
	{ 'R', { 0x8302, 0x8301, 0x8307, 0x8329, 0x8310, 0x8310 } }, { 'S', { 0x8300, 0x8305, 0x832a, 0x832b, 0x8313, 0x8312 } },
	{ 'T', { 0x8327, 0x8328, 0x831d, 0x831e, 0x8331, 0x8332 } }, { 'U', { 0x8306, 0x8306, 0x830a, 0x830a, 0x8313, 0x8312 } },
	{ 'W', { 0x8306, 0x8306, 0x830a, 0x830a, 0x8335, 0x8336 } }, { 'Y', { 0x8306, 0x8306, 0x833d, 0x833e, 0x8331, 0x8332 } },
	{ 'Z', { 0x8337, 0x8338, 0x833f, 0x8340, 0x8349, 0x834a } } };
const glyph *glyph_of(char c) { for (const glyph &g : GLYPHS) if (g.c == c) return &g; return nullptr; }
std::string rank_key(const u8 *e) { char b[24]; for (int k = 0; k < 11; k++) snprintf(b + 2 * k, 3, "%02x", e[k]); return b; }
}

void model2a::ranking_frame()
{
	if (m_backup.size() < RANK_BACKUP + 16 * RANK_N || m_workram.size() < RANK_WORK + 16 * RANK_N) return;
	// (learnt: a new entry's random fighter, while random fighters are on, by the entry's bytes: kept, as entries leave
	// the table only as better times come, and an online match's board holds another's)
	if (m_random && m_random->on)
		for (const u8 *table : { &m_backup[RANK_BACKUP], &m_workram[RANK_WORK] })
			for (u32 i = 0; i < RANK_N; i++) {
				const u8 *e = table + 16 * i;
				if (e[10] != 11 && e[10] != 12) continue;
				const std::string key = rank_key(e);
				const std::string &n = m_random->name_of[e[10]];
				if (n.empty() || m_rank_names.count(key)) continue;
				m_rank_names[key] = n;
				m_rank_version++;
			}
	// (shown: a row whose column spells MUE or KOJAC, its initials and fighter an entry's whose name is known)
	std::map<uint16_t, uint16_t> sub;
	auto at = [&](int x, int y) { return m_tiles.tile_r(u32(y * 64 + x)); };
	auto is = [&](int x, int y, char c) { const glyph *g = glyph_of(c); if (!g) return false; for (int k = 0; k < 6; k++) if (at(x + k % 2, y + k / 2) != g->t[k]) return false; return true; };
	auto spells = [&](int x, int y, const char *w) { for (int k = 0; w[k]; k++) if (!is(x + 2 * k, y, w[k])) return false; return true; };
	auto letter = [&](int x, int y) { for (const glyph &g : GLYPHS) { bool ok = true; for (int k = 0; k < 6 && ok; k++) ok = at(x + k % 2, y + k / 2) == g.t[k]; if (ok) return g.c; } return char(0); };
	if (!m_rank_names.empty() || (m_random && m_random->on))
		for (int y = 0; y + 2 < 64; y++)
			for (int x0 = 0; x0 + 6 <= 62; x0++) {
				const int f = spells(x0, y, "KOJAC") ? 12 : spells(x0, y, "MUE") ? 11 : 0;
				if (!f) continue;
				// (its entry: by the initials ten columns before, the ranking's (a letter the font lacks, a digit, any))
				char ini[3] = {};
				for (int k = 0; k < 3; k++) ini[k] = x0 >= 10 ? letter(x0 - 10 + 2 * k, y) : 0;
				std::string name;
				for (const u8 *table : { &m_workram[RANK_WORK], &m_backup[RANK_BACKUP] })
					for (u32 i = 0; i < RANK_N && name.empty(); i++) {
						const u8 *e = table + 16 * i;
						bool ok = e[10] == f;
						for (int k = 0; k < 3 && ok; k++) ok = !ini[k] || e[4 + k] == u8(ini[k]);
						if (!ok) continue;
						if (const auto it = m_rank_names.find(rank_key(e)); it != m_rank_names.end()) name = it->second;
					}
				// (none yet: the name entry's, its initials being typed in: the random fighter who played)
				if (name.empty() && m_random && m_random->on) name = m_random->name_of[f];
				if (name.empty()) continue;
				const int len = f == 12 ? 5 : 3;
				for (int k = 0; k < 10; k++) {   // (up to 10 letters, to column 61: the screen's edge)
					const glyph *g = k < int(name.size()) ? glyph_of(name[size_t(k)]) : nullptr;
					for (int j = 0; j < 6; j++) {
						const int x = x0 + 2 * k + j % 2, yy = y + j / 2;
						if (x < 62 && (g || k < len)) sub[uint16_t(yy * 64 + x)] = g ? g->t[j] : 0x8315;
					}
				}
				x0 += 2 * len - 1;
			}
	if (!sub.empty() || m_tiles_subst_on) { render_wait(); m_tiles_subst_on = !sub.empty(); m_tiles.substitute(std::move(sub)); }
}

std::string model2a::ranking_names() const
{
	std::string out;
	for (const auto &[k, n] : m_rank_names) out += k + " " + n + "\n";
	return out;
}

void model2a::set_ranking_names(const std::string &text)
{
	m_rank_names.clear();
	size_t at = 0;
	while (at < text.size()) {
		size_t e = text.find('\n', at);
		if (e == std::string::npos) e = text.size();
		const std::string line = text.substr(at, e - at);
		at = e + 1;
		const size_t sp = line.find(' ');
		if (sp != 22 || line.size() < 24) continue;
		std::string name;
		for (char c : line.substr(sp + 1)) if (glyph_of(c) && name.size() < 10) name += c;
		if (!name.empty()) m_rank_names[line.substr(0, sp)] = name;
	}
	m_rank_version++;
}

// The carried book (tools/carry_book.py: VF1 Recomp's seed book, its names' recipes)
static const char *const CARRIED =
#include "random_book.inc"
	;

// The book: the carried book's names (VF1's: their three, head and voice as VF1's book has them; a head not in VF2's
// cast, a cut character's, the first's), then each numeric seed's fighter whose three have Lion or Shun, a name to the
// first seed that makes it (only one that looks its own, as the seed makes it and as its recipe does: not one
// character's face on its own torso, not over 60% of its looks one character's), unless it's a carried name. A name's
// place in it (from 1) its seed
const std::map<std::string, u32> &model2a::random_fighter_names()
{
	random_fighters &r = randoms();
	if (!r.names.empty() || !r.known) return r.names;
	auto who = [](const std::string &n) { for (int f = 0; f <= 10; f++) if (n == NAME[f]) return f; return -1; };
	for (const char *l = CARRIED; *l;) {
		const char *e = strchr(l, '\n');
		const std::string line(l, e ? size_t(e - l) : strlen(l));
		l = e ? e + 1 : l + line.size();
		char f[6][24] = {};
		if (sscanf(line.c_str(), "%23s %23s %23s %23s %23s %23s", f[0], f[1], f[2], f[3], f[4], f[5]) != 6) continue;
		random_fighters::entry en{ f[0], { who(f[1]), who(f[2]), who(f[3]) }, who(f[4]), who(f[5]), f[4] };
		if (en.three[0] < 0 || en.three[0] == DURAL || en.three[1] < 0 || en.three[1] == DURAL || en.three[2] == DURAL || r.names.count(en.name)) continue;
		if (en.head < 0) en.head = en.three[0];   // (a cut character's: the first's)
		if (en.voice < 0 || en.voice == DURAL) en.voice = en.three[0];
		r.entries.push_back(en);
		r.names.emplace(en.name, u32(r.entries.size()));
	}
	const u32 was = r.rng;
	for (u32 s = 1; s <= RANDOM_SEEDS; s++) {
		const random_fighters::plan p = r.make_numeric(s);
		if (p.same_body || p.share > 60 || r.names.count(p.name)) continue;
		if (std::find(std::begin(p.three), std::end(p.three), 8) == std::end(p.three) && std::find(std::begin(p.three), std::end(p.three), 10) == std::end(p.three)) continue;
		r.entries.push_back({ p.name, { p.three[0], p.three[1], p.three[2] }, p.head, p.voice, NAME[p.head] });
		// (as its recipe makes it, too: its looks dealt from its name)
		const random_fighters::plan q = r.make_plan(u32(r.entries.size()));
		if (q.same_body || q.share > 60) { r.entries.pop_back(); continue; }
		r.names.emplace(p.name, u32(r.entries.size()));
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
	r.select_at = 0;
	r.scene_ptr[0] = r.scene_ptr[1] = 0;
	// (each player's fighter made now too, for a match that starts without a select: a seed's as it plays; with Random
	// opponents a side's random one for the attract, attract_frame)
	for (int pl = 0; pl < 2; pl++)
		if (r.mode[pl] || r.cpu) r.build(11 + pl, r.make_plan(r.mode[pl] ? r.seed_for(pl) : r.random_named()), pl);
	r.attract_scenes();
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
	m_random->seeded = false;
	m_random->draw_rng = m_random->colour_rng = 0;
}

void model2a::random_fighters_decode()
{
	if (m_random && m_random->on) m_random->decode_all();
}

void model2a::random_fighters_forget()
{
	if (!m_random) return;
	render_wait();
	m_random->forget();
}

void model2a::set_random_seed(uint32_t seed)
{
	random_fighters &r = randoms();
	auto mix = [](uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x ? x : 1u; };
	r.seeded = true;
	r.draw_rng = mix(seed);
	r.colour_rng = mix(seed ^ 0x5bd1e995u);
}

bool model2a::set_random_player_place(int pl, uint32_t place)
{
	if (pl < 0 || pl > 1) return false;
	random_fighters &r = randoms();
	if (place < 1 || place > random_fighter_names().size()) return false;
	r.mode[pl] = 2;
	r.seed[pl] = place;
	return true;
}

// Save states while they're on (online, on both sides alike): the ROM's bytes they change as a match goes (the empty
// ROM they're made in, and the tables by fighter number they point into it), and what they know (each player's
// fighter as made, who's picking, the draws, each side's textures' state). A load makes a player's fighter again
// if the state's is another than the one made now (its textures' plan, its name), then puts the bytes back
namespace {
struct rom_span { u32 at, n; };
constexpr rom_span STATE_SPANS[] = { { FREE_AT, FREE_END - FREE_AT }, { RECORDS, 4 * N_RECORDS }, { FACE_SPAN, FACE_SPAN_N },
	{ COMMANDS, 4 * 13 }, { ATTACH, 4 * N_RECORDS }, { WORN, 4 * N_RECORDS } };
constexpr u32 STATE_TAG = 0x52324656;   // "VF2R"
struct random_host_state {   // (u32s only: no padding in a state)
	u32 mode[2], seed[2], cpu, picking[2], cpu_made[2], at_select[2], fighting[2], from_select, head_of[N_RECORDS];
	u32 seeded, draw_rng, colour_rng, made[2][2], tex_fighter[2], tex_applied[2], tex_busy[2], tex_head_set[2], select_at, scene_ptr[2], hair_hue[N_RECORDS];
};
}

void random_fighters::save(m2::StateWriter &io)
{
	u32 tag = STATE_TAG;
	io.pod(tag);
	for (const rom_span &k : STATE_SPANS) io.bytes(at(k.at), k.n);
	random_host_state h{};
	for (int i = 0; i < 2; i++) {
		h.mode[i] = u32(mode[i]); h.seed[i] = seed[i]; h.picking[i] = picking[i]; h.cpu_made[i] = u32(cpu_made[i]);
		h.at_select[i] = at_select[i]; h.fighting[i] = fighting[i];
		h.made[i][0] = made[i].place; h.made[i][1] = made[i].colour;
		h.tex_fighter[i] = u32(tex[i].fighter); h.tex_applied[i] = tex[i].applied; h.tex_busy[i] = tex[i].busy; h.tex_head_set[i] = tex[i].head_set;
	}
	h.cpu = cpu; h.from_select = from_select;
	for (int f = 0; f < N_RECORDS; f++) { h.head_of[f] = u32(head_of[f]); h.hair_hue[f] = u32(hair_hue[f]); }
	h.seeded = seeded; h.draw_rng = draw_rng; h.colour_rng = colour_rng;
	h.select_at = select_at; h.scene_ptr[0] = scene_ptr[0]; h.scene_ptr[1] = scene_ptr[1];
	io.pod(h);
}

void random_fighters::load(m2::StateReader &io)
{
	u32 tag = 0;
	io.pod(tag);
	const u8 *src[std::size(STATE_SPANS)];
	for (size_t i = 0; i < std::size(STATE_SPANS); i++) src[i] = io.take(STATE_SPANS[i].n);
	random_host_state h{};
	io.pod(h);
	if (!io.ok || tag != STATE_TAG) { io.ok = false; return; }
	// (the players' fighters the state had: made again, from their places and colours' draws, if they're others now)
	for (int i = 0; i < 2; i++) {
		const made_key k{ h.made[i][0], h.made[i][1] };
		if (k == made[i]) continue;
		if (k.place) { colour_rng = k.colour; build(11 + i, make_plan(k.place), i); }
		else if (!name_of[11 + i].empty()) { own(11 + i); made[i] = made_key{}; }
	}
	for (size_t i = 0; i < std::size(STATE_SPANS); i++) {
		const u32 a = STATE_SPANS[i].at, n = STATE_SPANS[i].n;
		for (u32 k = 0; k < n; k += 64) {
			const u32 len = std::min(64u, n - k);
			if (memcmp(at(a + k), src[i] + k, len)) for (u32 j = 0; j < len; j++) put8(a + k + j, src[i][k + j]);
		}
	}
	for (int i = 0; i < 2; i++) {
		mode[i] = int(h.mode[i]); seed[i] = h.seed[i]; picking[i] = h.picking[i]; cpu_made[i] = int(h.cpu_made[i]);
		at_select[i] = h.at_select[i]; fighting[i] = h.fighting[i];
		tex[i].fighter = int(h.tex_fighter[i]); tex[i].applied = h.tex_applied[i]; tex[i].busy = h.tex_busy[i]; tex[i].head_set = h.tex_head_set[i];
	}
	cpu = h.cpu; from_select = h.from_select;
	for (int f = 0; f < N_RECORDS; f++) { head_of[f] = int(h.head_of[f]); hair_hue[f] = int(h.hair_hue[f]); }
	seeded = h.seeded; draw_rng = h.draw_rng; colour_rng = h.colour_rng;
	select_at = h.select_at; scene_ptr[0] = h.scene_ptr[0]; scene_ptr[1] = h.scene_ptr[1];
}

void model2a::random_state(m2::StateWriter &io) { m_random->save(io); }
void model2a::random_state(m2::StateReader &io) { m_random->load(io); }
