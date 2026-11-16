#include "hud_names.h"
#include "s24tile.h"
#include <algorithm>

namespace m2hud {

namespace {

// The font: VF3 Recomp's HUD font (7 x 10, this project's own), lit in VF2's colours
struct Glyph { char c; const char *rows[10]; };
const Glyph FONT[] = {
	{ 'A', { ".#####.", "###.###", "###.###", "###.###", "###.###", "#######", "###.###", "###.###", "###.###", "###.###" } },
	{ 'B', { "######.", "###.###", "###.###", "###.###", "######.", "###.###", "###.###", "###.###", "###.###", "######." } },
	{ 'C', { ".######", "###....", "###....", "###....", "###....", "###....", "###....", "###....", "###....", ".######" } },
	{ 'D', { "######.", "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", "######." } },
	{ 'E', { "#######", "###....", "###....", "###....", "######.", "###....", "###....", "###....", "###....", "#######" } },
	{ 'F', { "#######", "###....", "###....", "###....", "######.", "###....", "###....", "###....", "###....", "###...." } },
	{ 'G', { ".######", "###....", "###....", "###....", "###.###", "###.###", "###.###", "###.###", "###.###", ".######" } },
	{ 'H', { "###.###", "###.###", "###.###", "###.###", "#######", "###.###", "###.###", "###.###", "###.###", "###.###" } },
	{ 'I', { ".#####.", "..###..", "..###..", "..###..", "..###..", "..###..", "..###..", "..###..", "..###..", ".#####." } },
	{ 'J', { "....###", "....###", "....###", "....###", "....###", "....###", "###.###", "###.###", "###.###", ".#####." } },
	{ 'K', { "###.###", "###.###", "###.##.", "###.##.", "#####..", "#####..", "###.##.", "###.###", "###.###", "###.###" } },
	{ 'L', { "###....", "###....", "###....", "###....", "###....", "###....", "###....", "###....", "###....", "#######" } },
	{ 'M', { "##...##", "###.###", "#######", "#######", "##.#.##", "##...##", "##...##", "##...##", "##...##", "##...##" } },
	{ 'N', { "###..##", "####.##", "####.##", "#######", "#######", "##.####", "##.####", "##..###", "##..###", "##...##" } },
	{ 'O', { ".#####.", "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", ".#####." } },
	{ 'P', { "######.", "###.###", "###.###", "###.###", "###.###", "######.", "###....", "###....", "###....", "###...." } },
	{ 'Q', { ".#####.", "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", "###.##.", ".####.#" } },
	{ 'R', { "######.", "###.###", "###.###", "###.###", "###.###", "######.", "###.##.", "###.###", "###.###", "###.###" } },
	{ 'S', { ".######", "###....", "###....", "###....", ".#####.", "....###", "....###", "....###", "....###", "######." } },
	{ 'T', { "#######", "..###..", "..###..", "..###..", "..###..", "..###..", "..###..", "..###..", "..###..", "..###.." } },
	{ 'U', { "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", ".#####." } },
	{ 'V', { "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", ".##.##.", ".#####.", "..###.." } },
	{ 'W', { "##...##", "##...##", "##...##", "##...##", "##...##", "##.#.##", "#######", "#######", "###.###", "##...##" } },
	{ 'X', { "###.###", "###.###", ".##.##.", ".#####.", "..###..", "..###..", ".#####.", ".##.##.", "###.###", "###.###" } },
	{ 'Y', { "###.###", "###.###", "###.###", "###.###", ".#####.", "..###..", "..###..", "..###..", "..###..", "..###.." } },
	{ 'Z', { "#######", "....###", "....###", "...###.", "..###..", ".###...", "###....", "###....", "###....", "#######" } },
	{ '0', { ".#####.", "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", "###.###", ".#####." } },
	{ '1', { "..###..", ".####..", "..###..", "..###..", "..###..", "..###..", "..###..", "..###..", "..###..", ".#####." } },
	{ '2', { ".#####.", "###.###", "....###", "....###", "...###.", "..###..", ".###...", "###....", "###....", "#######" } },
	{ '3', { "######.", "....###", "....###", "....###", ".#####.", "....###", "....###", "....###", "....###", "######." } },
	{ '4', { "###.###", "###.###", "###.###", "###.###", "#######", "....###", "....###", "....###", "....###", "....###" } },
	{ '5', { "#######", "###....", "###....", "###....", "######.", "....###", "....###", "....###", "....###", "######." } },
	{ '6', { ".######", "###....", "###....", "###....", "######.", "###.###", "###.###", "###.###", "###.###", ".#####." } },
	{ '7', { "#######", "....###", "....###", "....###", "...###.", "..###..", "..###..", "..###..", "..###..", "..###.." } },
	{ '8', { ".#####.", "###.###", "###.###", "###.###", ".#####.", "###.###", "###.###", "###.###", "###.###", ".#####." } },
	{ '9', { ".#####.", "###.###", "###.###", "###.###", "###.###", ".######", "....###", "....###", "....###", "######." } },
	{ '.', { ".......", ".......", ".......", ".......", ".......", ".......", ".......", ".......", "..###..", "..###.." } },
	{ '-', { ".......", ".......", ".......", ".......", ".#####.", ".#####.", ".......", ".......", ".......", "......." } },
	{ '!', { "..###..", "..###..", "..###..", "..###..", "..###..", "..###..", "..###..", ".......", "..###..", "..###.." } },
	{ '?', { ".#####.", "###.###", "....###", "....###", "...###.", "..###..", "..###..", ".......", "..###..", "..###.." } },
};
constexpr int GW = 7, GH = 10, PITCH = 8, SPACE = 4;
// VF2's name letters: orange, a stroke's right-hand pixel a darker orange (its shadow side), its top-left one a light
// grey (as the game's own names are lit), outlined in black, 0xAARRGGBB
constexpr uint32_t LIT = 0xFFB88200, SHADOW = 0xFFA03A00, SHINE = 0xFFB8B8B8, OUTLINE = 0xFF000000;

const Glyph *glyph(char c)
{
	for (const Glyph &g : FONT)
		if (g.c == c) return &g;
	return nullptr;
}

int text_width(const std::string &s)
{
	int w = 0;
	for (char c : s) w += c == ' ' ? SPACE : PITCH;
	return w > 0 ? w - (PITCH - GW) : 0;
}

uint32_t code_at(const segas24_tile &t, int row, int col) { return t.tile_code(t.tile_r(uint32_t(row * 64 + col))); }   // (map 0)
bool blank(uint32_t code) { return code == 0 || code == 0x20; }

// a round marker: two tiles side by side, on both rows
bool marker_at(const segas24_tile &t, int c)
{
	if (c < 0 || c + 1 >= 64) return false;
	for (int row = 6; row <= 7; row++) {
		const uint32_t a = code_at(t, row, c);
		if (blank(a) || code_at(t, row, c + 1) != a + 1) return false;
	}
	return true;
}

// the fight's HUD: at least two markers a side, out from the timer (player 1's from column 22 leftwards, player 2's
// from 36 rightwards): where they end, the names' room does
bool fight_hud(const segas24_tile &t, int &p1_first, int &p2_last)
{
	for (int c : { 22, 24, 36, 38 })
		if (!marker_at(t, c)) return false;
	p1_first = 22;
	while (p1_first > 22 - 2 * 4 && marker_at(t, p1_first - 2)) p1_first -= 2;
	p2_last = 39;
	while (p2_last < 39 + 2 * 3 && marker_at(t, p2_last + 1)) p2_last += 2;
	return true;
}

// a column of the box with no letter in it: only the frame's black, see-through and the line under it
bool plain_column(const bitmap_rgb32 &top, int x, int y0, int y1)
{
	for (int y = y0 + 2; y <= y1 - 2; y++) {
		const uint32_t c = top.pix(y, x);
		if (c != 0 && c != 0xFF000000u) return false;
	}
	return true;
}

// each letter outlined in black first, as the game's own are (a pixel round every stroke's, corners too), so it reads
// over the checker; then the letters over the outlines. x: where the next letter goes, after
void letters(bitmap_rgb32 &top, const std::string &t, int &x, int ty)
{
	int ox2 = x;
	for (char ch : t) {
		if (ch == ' ') { ox2 += SPACE; continue; }
		if (const Glyph *g = glyph(ch))
			for (int r = 0; r < GH; r++)
				for (int k = 0; k < GW; k++) {
					if (g->rows[r][k] != '#') continue;
					for (int dy = -1; dy <= 1; dy++)
						for (int dx = -1; dx <= 1; dx++) {
							const int yy = ty + r + dy, xx = ox2 + k + dx;
							if (yy >= 0 && yy < top.height() && xx >= 0 && xx < top.width()) top.pix(yy, xx) = OUTLINE;
						}
				}
		ox2 += PITCH;
	}
	for (char ch : t) {
		if (ch == ' ') { x += SPACE; continue; }
		if (const Glyph *g = glyph(ch))
			for (int r = 0; r < GH; r++)
				for (int k = 0; k < GW; k++) {
					if (g->rows[r][k] != '#') continue;
					const bool right_edge = k + 1 >= GW || g->rows[r][k + 1] != '#';
					const bool shine = (r == 0 || g->rows[r - 1][k] != '#') && (k == 0 || g->rows[r][k - 1] != '#');
					const int yy = ty + r, xx = x + k;
					if (yy >= 0 && yy < top.height() && xx >= 0 && xx < top.width()) top.pix(yy, xx) = shine ? SHINE : right_edge ? SHADOW : LIT;
				}
		x += PITCH;
	}
}

} // namespace

bool champion_at(const segas24_tile &t, int side, int &c0, int &c1)
{
	// CHAMPION's top row as the game's map has it (its letters' tiles, the palette and priority bits as they come)
	static constexpr uint16_t WORD[8] = { 0x893c, 0x8945, 0x891b, 0x894c, 0x893e, 0x8901, 0x891b, 0x894d };
	const int from = side ? 35 : 0, to = side ? 63 : 26;   // (either side of the timer's box, columns 27-34)
	for (int c = from; c + 7 <= to; c++) {
		bool is = true;
		for (int k = 0; k < 8 && is; k++) is = code_at(t, 1, c + k) == t.tile_code(WORD[k]);
		if (!is) continue;
		// the line to its end: the count and WIN or WINS after it, a column's space between words
		int end = c + 7;
		for (int x = end + 1; x <= to; x++) {
			const bool lit = !blank(code_at(t, 1, x)) || !blank(code_at(t, 2, x));
			if (lit) end = x;
			else if (x - end > 1) break;
		}
		c0 = c;
		c1 = end;
		return true;
	}
	return false;
}

bool challenger_at(const segas24_tile &t, int side, int &c0, int &c1)
{
	// only with CHAMPION over the other bar: then this side's rows 1 and 2 hold CHALLENGER and nothing else
	int o0, o1;
	if (!champion_at(t, side ^ 1, o0, o1)) return false;
	const int from = side ? 35 : 0, to = side ? 63 : 26;
	c0 = -1;
	for (int x = from; x <= to; x++)
		if (!blank(code_at(t, 1, x)) || !blank(code_at(t, 2, x))) { if (c0 < 0) c0 = x; c1 = x; }
	return c0 >= 0;
}

bool draw_streak(const segas24_tile &t, bitmap_rgb32 &top, int ox, int side, const std::string &text)
{
	int p1_first, p2_last;
	if (text.empty() || !fight_hud(t, p1_first, p2_last)) return false;
	// The game's own heading letters: each two tiles tall, its top and bottom halves' tiles as the game's table of
	// them has them (shared halves: 0's top is O's and A's, its bottom 6's and 8's), in CHAMPION's palette (0x89xx)
	struct Pair { char c; uint16_t top, bottom; };
	static constexpr Pair HEAD[] = {
		{ '0', 0x091b, 0x091c }, { '1', 0x091d, 0x091e }, { '2', 0x091f, 0x0920 }, { '3', 0x0921, 0x0922 }, { '4', 0x0923, 0x0924 },
		{ '5', 0x0925, 0x0926 }, { '6', 0x0927, 0x091c }, { '7', 0x0928, 0x0929 }, { '8', 0x092a, 0x091c }, { '9', 0x091b, 0x092b },
		{ 'I', 0x0901, 0x091e }, { 'N', 0x094d, 0x0946 }, { 'S', 0x0951, 0x0952 }, { 'W', 0x0956, 0x0957 },
	};
	auto pair = [&](char c) -> const Pair * { for (const Pair &q : HEAD) if (q.c == c) return &q; return nullptr; };
	// hard to the bar's outer end, rows 1 and 2: player 1's from column 2, player 2's to column 59 (the name boxes'
	// and the bars' outer edges), a letter a column
	const int y0 = 8, cols = int(text.size());
	const int x0 = ox + (side ? 60 - cols : 2) * 8;
	// only if the tiles are there (the HUD's font in the tile graphics, as in every fight): else the names' letters
	bool ok = true;
	for (char c : text) {
		if (c == ' ') continue;
		const Pair *q = pair(c);
		if (!q || t.tile_empty(q->top) || t.tile_empty(q->bottom)) { ok = false; break; }
	}
	if (!ok) {
		int x = side ? ox + 60 * 8 - text_width(text) : ox + 2 * 8;
		letters(top, text, x, y0 + 3);
		return true;
	}
	int x = x0;
	for (char c : text) {
		if (const Pair *q = c == ' ' ? nullptr : pair(c)) {
			t.draw_tile(top, x, y0, uint16_t(0x8000 | q->top));
			t.draw_tile(top, x, y0 + 8, uint16_t(0x8000 | q->bottom));
		}
		x += 8;   // (a space a column, as the game's)
	}
	return true;
}

bool draw(const segas24_tile &tiles, bitmap_rgb32 &top, int ox, const std::string names[2], const Flag *flags[2], int flag_at[2][2])
{
	if (flag_at) for (int p = 0; p < 2; p++) flag_at[p][0] = flag_at[p][1] = -1;
	int p1_first = 0, p2_last = 0;
	if (!fight_hud(tiles, p1_first, p2_last)) return false;
	constexpr int Y0 = 6 * 8, Y1 = Y0 + 15;
	// each game's box (player 1's in columns 2-7, player 2's in 54-59: its ends, the frame's) and its checker: two plain
	// columns (its two phases) next to each other, the first after the left end's frame. A name whose letters leave no
	// two plain columns (JEFFRY: the J's hook against the frame, black between the letters) has the checker made as the
	// game's is: black above, the light line below, between them black and see-through by turns, its frame a pixel
	int box0[2] = { -1, -1 }, box1[2] = { -1, -1 }, plain[2] = { -1, -1 };
	uint32_t col[2][2][16];
	for (int p = 0; p < 2; p++) {
		const int x_from = ox + (p ? 54 : 2) * 8, x_to = x_from + 6 * 8 - 1;
		for (int x = x_from; x <= x_to; x++)
			for (int y = Y0; y <= Y1; y++)
				if (top.pix(y, x)) { if (box0[p] < 0) box0[p] = x; box1[p] = x; break; }
		if (box0[p] < 0 || box1[p] - box0[p] < 12) { box0[p] = -1; continue; }
		for (int x = box0[p] + 1; x + 1 < box1[p]; x++)
			if (plain_column(top, x, Y0, Y1) && plain_column(top, x + 1, Y0, Y1) && top.pix(Y0 + 4, x) != top.pix(Y0 + 4, x + 1)) { plain[p] = x; break; }
		if (plain[p] >= 0)
			for (int y = 0; y < 16; y++) { col[p][0][y] = top.pix(Y0 + y, plain[p]); col[p][1][y] = top.pix(Y0 + y, plain[p] + 1); }
		else {
			plain[p] = box0[p] + 1;
			const uint32_t line = top.pix(Y1, (box0[p] + box1[p]) / 2);
			for (int k = 0; k < 2; k++)
				for (int y = 0; y < 16; y++)
					col[p][k][y] = y == 0 ? 0xFF000000u : y == 15 ? line : (plain[p] + k + Y0 + y) & 1 ? 0 : 0xFF000000u;
		}
	}
	for (int p = 0; p < 2; p++) {
		if (names[p].empty() || box0[p] < 0) continue;
		const int bx0 = box0[p], bx1 = box1[p], fill = plain[p];
		const int left = fill - bx0, right = std::max(1, left);   // (the frame's columns at each end)
		uint32_t edge_l[8][16], edge_r[8][16];
		for (int y = 0; y < 16; y++) {
			for (int k = 0; k < left && k < 8; k++) edge_l[k][y] = top.pix(Y0 + y, bx0 + k);
			for (int k = 0; k < right && k < 8; k++) edge_r[k][y] = top.pix(Y0 + y, bx1 - right + 1 + k);
		}
		// the name (and its flag, out from the middle) and the box it needs, short of the round markers: closer together
		// if it's tight (a long name, the most rounds), the name alone only if even that won't fit
		const int tw = text_width(names[p]);
		const Flag *fl = flags && flags[p] && flags[p]->w > 0 && flags[p]->h <= 12 ? flags[p] : nullptr;
		const int limit = p ? ox + (p2_last + 1) * 8 + 2 : ox + p1_first * 8 - 3;   // (the box never into the markers)
		int gap = 3, pad = 3, inner = 0, need = 0, nx0 = 0, nx1 = 0;
		auto fit = [&] {
			inner = tw + (fl ? fl->w + gap : 0);
			need = std::max(bx1 - bx0 + 1, left + pad + inner + pad + right);
			nx0 = p ? bx1 - need + 1 : bx0;
			nx1 = p ? bx1 : bx0 + need - 1;
			return !fl || (p ? nx0 >= limit : nx1 <= limit);
		};
		if (!fit()) {
			gap = pad = 2;
			if (!fit()) { gap = pad = 3; fl = nullptr; fit(); }
		}
		nx0 = std::max(nx0, 0);
		nx1 = std::min(nx1, top.width() - 1);
		// the box made again: its left end, the checker in its phase across, its right end
		for (int x = nx0; x <= nx1; x++)
			for (int y = 0; y < 16; y++) {
				uint32_t c;
				if (x - nx0 < left) c = edge_l[x - nx0][y];
				else if (nx1 - x < right) c = edge_r[right - 1 - (nx1 - x)][y];
				else c = col[p][(x - fill) & 1][y];
				top.pix(Y0 + y, x) = c;
			}
		// the name in it: player 1's from the left, player 2's to the right
		const int ty = Y0 + 3;
		int x = p ? nx1 - right - pad - inner + 1 : nx0 + left + pad;
		auto put_flag = [&](int fx) {
			if (flag_at) { flag_at[p][0] = fx; flag_at[p][1] = Y0; }
			if (fl->px.size() < size_t(fl->w) * size_t(fl->h)) return;   // (the host's to draw)
			for (int y = 0; y < fl->h; y++)
				for (int k = 0; k < fl->w; k++) top.pix(ty + (GH - fl->h) / 2 + y, fx + k) = fl->px[size_t(y) * size_t(fl->w) + size_t(k)];
		};
		if (fl && !p) {   // (player 1's flag to the left of the name)
			put_flag(x);
			x += fl->w + gap;
		}
		letters(top, names[p], x, ty);
		if (fl && p) {   // (player 2's to the right of it)
			x += gap - (PITCH - GW);
			put_flag(x);
		}
	}
	return true;
}

} // namespace m2hud
