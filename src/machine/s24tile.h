// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
// Sega System 24 tilemap chip (315-5242 family) as Model 1 and Model 2 use it for their 2D
// layers: four 64x64 maps of 8x8 4bpp tiles, per-layer scroll, line scroll and the 128-pixel
// window mask. Ported from MAME's segaic24.cpp (BSD-3-Clause, copyright-holders: Olivier
// Galibert), by way of vf1-recomp; draw_sides draws past the arcade's edges for widescreen.
#pragma once
#include "mame_lite.h"
#include <map>

class segas24_tile
{
public:
	enum { DRAW_OPAQUE = 0x10000 };

	segas24_tile() : char_ram(0x80000 / 2), tile_ram(0x10000 / 2) {}

	uint16_t tile_r(offs_t o) const { return tile_ram[o & 0x7fff]; }
	uint16_t char_r(offs_t o) const { return char_ram[o & 0x3ffff]; }
	uint32_t tile_code(uint16_t entry) const { return entry & tile_mask; }
	// (the maps' entries and the tiles written since the last update(), decoded again then)
	void tile_w(offs_t o, uint16_t d, uint16_t m)
	{
		o &= 0x7fff;
		const uint16_t v = (tile_ram[o] & ~m) | (d & m);
		if (o < 0x4000 && v != tile_ram[o]) { m_entry_dirty[o] = 1; m_entries_dirty = true; }
		tile_ram[o] = v;
	}
	void char_w(offs_t o, uint16_t d, uint16_t m)
	{
		o &= 0x3ffff;
		const uint16_t v = (char_ram[o] & ~m) | (d & m);
		if (v != char_ram[o]) { m_char_dirty[o >> 4] = 1; m_chars_dirty = true; char_version++; }
		char_ram[o] = v;
	}
	uint64_t char_version = 0;          // (counts the tile graphics' writes: a copy of them is still good while it's the same)
	void mark_dirty() { m_dirty = true; }
	// (VF2's player select, drawing only: a player's cursor frame on the fighters' icons not drawn, bit 0 player 1's
	// (tiles af80-af8d on map 0, rows 34-44), bit 1 player 2's (af8e-af9b): a random player's, whose slot isn't its own)
	void hide_frames(uint8_t h) { if (h != m_hide_frames) { m_hide_frames = h; m_dirty = true; } }
	uint8_t hidden_frames() const { return m_hide_frames; }
	// (drawing only: map entries drawn as other tiles, entry index -> its tile word: VF2's ranking's random fighters'
	// names)
	void substitute(std::map<uint16_t, uint16_t> &&s)
	{
		if (s == m_subst) return;
		for (const auto &[e, v] : m_subst) if (e < 0x4000) { m_entry_dirty[e] = 1; m_entries_dirty = true; }
		m_subst = std::move(s);
		for (const auto &[e, v] : m_subst) if (e < 0x4000) { m_entry_dirty[e] = 1; m_entries_dirty = true; }
	}   // (the RAMs swapped from outside: decode again)
	// n words from o of the tile RAM (maps true: the maps' entries) or of the tile graphics, written from
	// outside (a save state's pages put back): decoded again at the next update()
	void restored(bool maps, offs_t o, size_t n)
	{
		if (maps) {
			for (size_t i = o; i < o + n && i < 0x4000; i++) m_entry_dirty[i] = 1;
			m_entries_dirty = true;
		} else {
			for (size_t i = o; i < o + n && i < char_ram.size(); i += 16) m_char_dirty[i >> 4] = 1;
			m_chars_dirty = true;
			char_version++;
		}
	}

	// One map as a 512x512 picture (after update()), for exporting the artwork.
	void render_map(int l, uint32_t *out) const
	{
		for (size_t i = 0; i < size_t(512) * 512; i++) out[i] = m_pens[pix[l][i] & 0x1fff];
	}

	// Map entries (w x h, as the tile RAM holds them) as a picture, 8 pixels per tile; after update().
	void render_entries(const uint16_t *e, int w, int h, uint32_t *out) const
	{
		for (int ty = 0; ty < h; ty++)
			for (int tx = 0; tx < w; tx++) {
				uint16_t val = e[ty * w + tx];
				uint32_t code = val & tile_mask, color = (val >> 7) & 0xff;
				const uint16_t *c = &char_ram[(code * 16) & 0x3ffff];
				for (int y = 0; y < 8; y++) {
					uint32_t row = (uint32_t(c[y * 2]) << 16) | c[y * 2 + 1];
					for (int x = 0; x < 8; x++)
						out[size_t(ty * 8 + y) * (w * 8) + tx * 8 + x] = m_pens[(color * 16 + ((row >> (28 - 4 * x)) & 0xf)) & 0x1fff];
				}
			}
	}

	// Decodes the four maps into 512x512 pen and flag planes; call once per frame before draw().
	// The planes hold pen numbers, not colours (a new palette changes nothing here): only the map
	// entries written since, and those showing a tile written since, are decoded again (as MAME's
	// tilemaps mark their tiles dirty).
	void update(const uint32_t *pens)
	{
		m_pens = pens;
		if (m_dirty || pix[0].empty()) {
			m_dirty = false;
			for (int l = 0; l < 4; l++) { pix[l].resize(512 * 512); flags[l].resize(512 * 512); }
			for (int e = 0; e < 0x4000; e++) decode_entry(e);
		} else {
			if (m_chars_dirty)
				for (int e = 0; e < 0x4000; e++)
					if (m_char_dirty[tile_ram[e] & tile_mask]) m_entry_dirty[e] = 1;
			if (m_entries_dirty || m_chars_dirty)
				for (int e = 0; e < 0x4000; e++)
					if (m_entry_dirty[e]) decode_entry(e);
		}
		std::fill(std::begin(m_entry_dirty), std::end(m_entry_dirty), 0);
		std::fill(std::begin(m_char_dirty), std::end(m_char_dirty), 0);
		m_entries_dirty = m_chars_dirty = false;
	}

	void draw(bitmap_rgb32 &bitmap, const rectangle &cliprect, int layer, int lpri, int flags_in)
	{
		uint16_t hscr = tile_ram[0x5000 + (layer >> 1)];
		uint16_t vscr = tile_ram[0x5004 + (layer >> 1)];
		uint16_t ctrl = tile_ram[0x5004 + ((layer >> 1) & 2)];
		const uint16_t *mask = &tile_ram[layer & 4 ? 0x6800 : 0x6000];
		uint16_t tpri = layer & 1;
		layer >>= 1;

		if (vscr & 0x8000)
			return;

		if (ctrl & 0x6000) {
			// Window/scroll split modes, drawn through the generic scrolled path.
			if (layer & 1)
				return;
			int sy = vscr & 0x1ff;
			if (hscr & 0x8000) {
				const uint16_t *hscrtb = &tile_ram[0x4000 + 0x200 * layer];
				for (int y = cliprect.min_y; y <= cliprect.max_y; y++) {
					rectangle c = cliprect; c.min_y = c.max_y = y;
					uint16_t h = hscrtb[y] & 0x1ff;
					int l1 = layer;
					if (((ctrl & 0x6000) >> 13) == 1) {
						uint16_t v = (-vscr) & 0x1ff;
						if (!((-vscr) & 0x200)) l1 ^= 1;
						if (y >= v) l1 ^= 1;
						draw_scrolled(bitmap, c, l1, -h, sy, tpri, flags_in);
					} else {
						rectangle c1 = c, c2 = c;
						if (c1.max_x >= h) c1.max_x = h - 1;
						if (c2.min_x < h) c2.min_x = h;
						if (!(hscrtb[y] & 0x200)) l1 ^= 1;
						draw_scrolled(bitmap, c1, l1, -h, sy, tpri, flags_in);
						draw_scrolled(bitmap, c2, l1 ^ 1, -h, sy, tpri, flags_in);
					}
				}
			} else {
				int sx = -(hscr & 0x1ff);
				rectangle c1 = cliprect, c2 = cliprect;
				if (((ctrl & 0x6000) >> 13) == 1) {
					uint16_t v = (-vscr) & 0x1ff;
					if (c1.max_y >= v) c1.max_y = v - 1;
					if (c2.min_y < v) c2.min_y = v;
					if (!((-vscr) & 0x200)) layer ^= 1;
				} else {
					uint16_t h = hscr & 0x1ff;
					if (c1.max_x >= h) c1.max_x = h - 1;
					if (c2.min_x < h) c2.min_x = h;
					if (!(hscr & 0x200)) layer ^= 1;
				}
				draw_scrolled(bitmap, c1, layer, sx, sy, tpri, flags_in);
				draw_scrolled(bitmap, c2, layer ^ 1, sx, sy, tpri, flags_in);
			}
			return;
		}

		int win = layer & 1;
		if (hscr & 0x8000) {
			const uint16_t *hscrtb = &tile_ram[0x4000 + 0x200 * layer];
			vscr &= 0x1ff;
			for (int y = 0; y < 384; y++) {
				uint16_t h = (-hscrtb[y]) & 0x1ff;
				if (h + 496 <= 512)
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, h, vscr, 0, y, 496, y + 1);
				else {
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, h, vscr, 0, y, 512 - h, y + 1);
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, 0, vscr, 512 - h, y, 496, y + 1);
				}
				vscr = (vscr + 1) & 0x1ff;
			}
		} else {
			hscr = (-hscr) & 0x1ff;
			vscr = (+vscr) & 0x1ff;
			if (hscr + 496 <= 512) {
				if (vscr + 384 <= 512)
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, hscr, vscr, 0, 0, 496, 384);
				else {
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, hscr, vscr, 0, 0, 496, 512 - vscr);
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, hscr, 0, 0, 512 - vscr, 496, 384);
				}
			} else {
				if (vscr + 384 <= 512) {
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, hscr, vscr, 0, 0, 512 - hscr, 384);
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, 0, vscr, 512 - hscr, 0, 496, 384);
				} else {
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, hscr, vscr, 0, 0, 512 - hscr, 512 - vscr);
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, 0, vscr, 512 - hscr, 0, 496, 512 - vscr);
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, hscr, 0, 0, 512 - vscr, 512 - hscr, 384);
					draw_rect(layer, bitmap, mask, tpri, flags_in, win, 0, 0, 512 - hscr, 512 - vscr, 496, 384);
				}
			}
		}
	}

	std::vector<uint16_t> char_ram, tile_ram;
	int m_ox = 0;   // destination x of the arcade's column 0 (widescreen draws centre the picture)

	// Widescreen: a panorama a layer's map holds part of. `entries` is `cols` x `rows` map entries
	// (the map's rows from 0), `cols` * 8 pixels around; the map holds 512 of them, at the same x
	// modulo 512. For the map's tile row r (rows can scroll apart, and lines too: a line scroll),
	// at[r] is the panorama's x at the screen's left edge on a line where the map's is x0[r];
	// another line of the row is that moved as its own scroll is; at[r] -1: not from the panorama.
	struct panorama { const uint16_t *entries = nullptr; int cols = 0, rows = 0; int16_t at[64] = {}, x0[64] = {}; };

	// Where a layer takes screen line y from, as draw() does: the map (in the split modes map 2 or
	// 3; for the horizontal split, at the screen's left edge), its line, and its x at the screen's
	// column 0 (the map wraps). False if the layer draws nothing.
	bool line_source(int layer, int y, int &map, int &my, int &x0) const
	{
		const int l = layer >> 1;
		const uint16_t hscr = tile_ram[0x5000 + l], vscr = tile_ram[0x5004 + l];
		const uint16_t ctrl = tile_ram[0x5004 + (l & 2)];
		const uint16_t *hscrtb = &tile_ram[0x4000 + 0x200 * l];
		if (vscr & 0x8000) return false;
		map = l;
		if (ctrl & 0x6000) {
			if (l & 1) return false;   // (the split modes draw maps 2 and 3 as map 2's layer)
			const int h = (hscr & 0x8000) ? hscrtb[y] & 0x1ff : hscr & 0x1ff;
			x0 = (-h) & 511;
			my = (y + (vscr & 0x1ff)) & 511;
			if ((ctrl & 0x6000) >> 13 == 1) {
				const int v = (-vscr) & 0x1ff;
				if (!((-vscr) & 0x200)) map ^= 1;
				if (y >= v) map ^= 1;
			} else if (!(((hscr & 0x8000) ? hscrtb[y] : hscr) & 0x200)) map ^= 1;
		} else {
			x0 = (hscr & 0x8000) ? (-hscrtb[y]) & 0x1ff : (-hscr) & 0x1ff;
			my = (vscr + y) & 0x1ff;
		}
		return true;
	}

	// Widescreen: a layer's pixels beyond the arcade's 496 columns, `ext` on each side (the
	// arcade's column 0 at m_ox), as draw() would take them from the map, the map wrapping; where
	// `pan` covers map 2's rows, from the panorama instead (the map holds only what's in view). And
	// from the panorama, the 8 columns inside each edge: the map's tile column across the edge is
	// often a frame behind (the game copies it in as it comes into view: at 4:3 a few pixels at
	// the screen's edge, wider in the middle of the picture).
	void draw_sides(bitmap_rgb32 &bitmap, int layer, int flags_in, int ext, const panorama *pan)
	{
		constexpr int INNER = 8;
		const int l = layer >> 1;
		const uint16_t hscr = tile_ram[0x5000 + l];
		const uint16_t ctrl = tile_ram[0x5004 + (l & 2)];
		const uint16_t *mask = &tile_ram[layer & 4 ? 0x6800 : 0x6000];
		const uint8_t want = uint8_t((layer & 1) | 0x10);
		const bool opaque = flags_in & DRAW_OPAQUE;
		const bool split = ctrl & 0x6000, hsplit = split && (ctrl & 0x6000) >> 13 != 1;
		const int hs = (hscr & 0x8000) ? -1 : hscr & 0x1ff;
		for (int y = 0; y < 384 && y < bitmap.height(); y++) {
			int m0, my, x0;
			if (!line_source(layer, y, m0, my, x0)) return;
			uint32_t *row = &bitmap.pix(y);
			const int hy = hs >= 0 ? hs : tile_ram[0x4000 + 0x200 * l + y] & 0x1ff;
			for (int side = 0; side < 2; side++)
				for (int x = side ? 496 - INNER : -ext; x < (side ? 496 + ext : INNER); x++) {
					const bool inside = x >= 0 && x < 496;
					int m = m0;
					if (hsplit && x >= hy) m ^= 1;   // (the horizontal split: the other map from column h)
					const int mx = (x0 + x) & 511;
					if (!split) {
						const int dx = std::clamp(x, 0, 495);
						uint16_t mk = mask[y * 4 + (dx >> 7)];
						if (l & 1) mk = ~mk;
						if (mk & (0x8000 >> ((dx & 127) >> 3))) continue;
					}
					uint16_t pen;
					uint8_t flag;
					const int r = my >> 3;
					const bool from_pan = pan && m == 2 && r < pan->rows && pan->at[r] >= 0;
					if (inside && !from_pan) continue;   // (the game's own, as drawn)
					if (from_pan) {
						const int w = pan->cols * 8;
						const int at = pan->at[r] + ((x0 - pan->x0[r] + 256) & 511) - 256;
						const int px = (((at + x) % w) + w) % w;
						const uint16_t e = pan->entries[size_t(my >> 3) * pan->cols + (px >> 3)];
						const uint16_t *c = &char_ram[(size_t(e & tile_mask) * 16 + (my & 7) * 2) & 0x3ffff];
						const uint32_t bits = (uint32_t(c[0]) << 16) | c[1];
						const uint8_t p = (bits >> (28 - 4 * (px & 7))) & 0xf;
						pen = uint16_t(((e >> 7) & 0xff) * 16 + p);
						flag = p ? uint8_t(0x10 | ((e & 0x8000) ? 1 : 0)) : 0;
					} else {
						pen = pix[m][size_t(my) * 512 + mx];
						flag = flags[m][size_t(my) * 512 + mx];
					}
					if (opaque || flag == want) row[m_ox + x] = m_pens[pen & 0x1fff];
				}
		}
	}

	// The free camera: a backdrop layer (map 2 or 3) drawn as the game's camera turned by the free camera's yaw and
	// pitch would see it, `dx` and `dy` pixels along (all of the picture's columns, -ext to 496 + ext): each line as
	// draw() takes it (line_source; a line past the screen's as the screen's edge line, its map's rows carrying on),
	// map 2's from the panorama where it has the row (`pan`, as draw_sides: all the way round), else from the map
	// (wrapping: only what's in the game's view is right there). Map 2 holds the backdrop from its row 0 down; past
	// its rows, its edge row faded over FADE lines to that row's average colour, not the map's other end. Not for
	// the side-by-side split (can_shift false: drawn as it is).
	bool can_shift(int layer) const
	{
		const uint16_t ctrl = tile_ram[0x5004 + ((layer >> 1) & 2)];
		return !(ctrl & 0x6000) || (ctrl & 0x6000) >> 13 == 1;
	}
	void draw_shifted(bitmap_rgb32 &bitmap, int layer, int flags_in, int ext, const panorama *pan, int dx, int dy)
	{
		constexpr int FADE = 48;
		const int l = layer >> 1;
		const uint16_t ctrl = tile_ram[0x5004 + (l & 2)];
		const bool split = ctrl & 0x6000;
		const uint16_t *mask = &tile_ram[layer & 4 ? 0x6800 : 0x6000];
		const uint8_t want = uint8_t((layer & 1) | 0x10);
		const bool opaque = flags_in & DRAW_OPAQUE;
		struct line { int m, my, x0, yc; };
		line top, bottom;
		if (!line_source(layer, 0, top.m, top.my, top.x0) || !line_source(layer, 383, bottom.m, bottom.my, bottom.x0)) return;
		auto line_at = [&](int y) {   // (y any line, as if the screen went on)
			line L;
			L.yc = std::clamp(y, 0, 383);
			line_source(layer, L.yc, L.m, L.my, L.x0);
			L.my = (L.my + (y - L.yc)) & 511;
			return L;
		};
		// (a panorama row not on the screen: placed as the nearest one that is; the rows scroll together)
		int pan_row[64];
		for (int r = 0; r < 64; r++) {
			pan_row[r] = -1;
			if (!pan || r >= pan->rows) continue;
			for (int d = 0; d < 64 && pan_row[r] < 0; d++) {
				if (r - d >= 0 && pan->at[r - d] >= 0) pan_row[r] = r - d;
				else if (r + d < pan->rows && pan->at[r + d] >= 0) pan_row[r] = r + d;
			}
		}
		auto sample = [&](const line &L, int x, uint16_t &pen, uint8_t &flag) {
			const int my = L.my, x0 = L.x0, m = L.m;
			const int r = my >> 3, pr = m == 2 ? pan_row[r] : -1;
			if (pr >= 0) {
				const int w = pan->cols * 8;
				const int at = pan->at[pr] + ((x0 - pan->x0[pr] + 256) & 511) - 256;
				const int px = (((at + x) % w) + w) % w;
				const uint16_t e = pan->entries[size_t(r) * pan->cols + (px >> 3)];
				const uint16_t *c = &char_ram[(size_t(e & tile_mask) * 16 + (my & 7) * 2) & 0x3ffff];
				const uint32_t bits = (uint32_t(c[0]) << 16) | c[1];
				const uint8_t p = (bits >> (28 - 4 * (px & 7))) & 0xf;
				pen = uint16_t(((e >> 7) & 0xff) * 16 + p);
				flag = p ? uint8_t(0x10 | ((e & 0x8000) ? 1 : 0)) : 0;
			} else {
				const int mx = (x0 + x) & 511;
				pen = pix[m][size_t(my) * 512 + mx];
				flag = flags[m][size_t(my) * 512 + mx];
			}
		};
		// the lines map 2's rows reach, from the screen's edges out (another map at an edge, the sky's filler: any)
		const int top_known = top.m == 2 ? -top.my : -(1 << 20), bottom_known = bottom.m == 2 ? 384 + (511 - bottom.my) : 1 << 20;
		auto edge_colour = [&](int y) {   // the average of a line (its opaque pixels)
			uint64_t r = 0, g = 0, b = 0, n = 0;
			const line L = line_at(y);
			for (int x = -ext; x < 496 + ext; x += 2) {
				uint16_t pen; uint8_t flag;
				sample(L, x, pen, flag);
				if (!opaque && flag != want) continue;
				const uint32_t c = m_pens[pen & 0x1fff];
				r += c >> 16 & 0xff; g += c >> 8 & 0xff; b += c & 0xff; n++;
			}
			return n ? uint32_t(0xff000000 | (r / n) << 16 | (g / n) << 8 | (b / n)) : 0u;
		};
		uint32_t top_c = 0, bottom_c = 0;
		if (dy < top_known) top_c = edge_colour(top_known);
		if (383 + dy >= bottom_known) bottom_c = edge_colour(bottom_known - 1);
		for (int y = 0; y < 384 && y < bitmap.height(); y++) {
			uint32_t *row = &bitmap.pix(y);
			int ys = y + dy, fade = 0;
			uint32_t to = 0;
			if (ys < top_known) { fade = top_known - ys; ys = top_known; to = top_c; }
			else if (ys >= bottom_known) { fade = ys - bottom_known + 1; ys = bottom_known - 1; to = bottom_c; }
			const uint32_t t = uint32_t(std::min(fade, FADE) * 256 / FADE);
			const line L = line_at(ys);
			for (int x = -ext; x < 496 + ext; x++) {
				if (!split) {   // (the window mask, as draw_sides: by the screen's line and column)
					const int mx = std::clamp(x, 0, 495);
					uint16_t mk = mask[L.yc * 4 + (mx >> 7)];
					if (l & 1) mk = ~mk;
					if (mk & (0x8000 >> ((mx & 127) >> 3))) continue;
				}
				uint16_t pen; uint8_t flag;
				sample(L, x + dx, pen, flag);
				if (!opaque && flag != want) continue;
				uint32_t c = m_pens[pen & 0x1fff];
				if (t && to) c = 0xff000000 | ((((c & 0xff00ff) * (256 - t) + (to & 0xff00ff) * t) >> 8) & 0xff00ff) | ((((c & 0xff00) * (256 - t) + (to & 0xff00) * t) >> 8) & 0xff00);
				row[m_ox + x] = c;
			}
		}
	}

private:
	static constexpr uint32_t tile_mask = 0x3fff;
	std::vector<uint16_t> pix[4];
	std::vector<uint8_t> flags[4];
	const uint32_t *m_pens = nullptr;
	bool m_dirty = true;   // everything to decode again
	uint8_t m_entry_dirty[0x4000] = {}, m_char_dirty[0x4000] = {};
	bool m_entries_dirty = false, m_chars_dirty = false;

	uint8_t m_hide_frames = 0;   // (hide_frames)
	std::map<uint16_t, uint16_t> m_subst;   // (substitute)
	void decode_entry(int e)
	{
		const int l = e >> 12, ty = (e >> 6) & 63, tx = e & 63;
		auto &pm = pix[l];
		auto &fm = flags[l];
		uint16_t val = tile_ram[e];
		if (m_hide_frames && l == 0 && ty >= 34 && ty <= 44 && val >= 0xaf80 && val <= 0xaf9b && (m_hide_frames >> (val >= 0xaf8e ? 1 : 0) & 1)) val = 0x0020;
		if (!m_subst.empty()) if (const auto it = m_subst.find(uint16_t(e)); it != m_subst.end()) val = it->second;
		uint32_t code = val & tile_mask;
		uint32_t color = (val >> 7) & 0xff;
		uint8_t cat = (val & 0x8000) ? 1 : 0;
		const uint16_t *c = &char_ram[(code * 16) & 0x3ffff];
		for (int y = 0; y < 8; y++) {
			uint16_t w0 = c[y * 2], w1 = c[y * 2 + 1];
			uint32_t row = (uint32_t(w0) << 16) | w1;
			size_t base = size_t(ty * 8 + y) * 512 + tx * 8;
			for (int x = 0; x < 8; x++) {
				uint8_t p = (row >> (28 - 4 * x)) & 0xf;
				pm[base + x] = uint16_t(color * 16 + p);
				fm[base + x] = p ? uint8_t(0x10 | cat) : 0;
			}
		}
	}
public:
	// When set (a buffer the size of the destination), draw_rect also records where each pixel
	// it draws came from: map << 18 | y * 512 + x. For replacing the sky with a hi-res picture.
	uint32_t *m_coord = nullptr;
private:

	void draw_scrolled(bitmap_rgb32 &bm, const rectangle &c, int l, int scrollx, int scrolly, uint16_t tpri, int fl)
	{
		uint8_t want = uint8_t(tpri | 0x10);
		// c is in arcade pixels (0..495); the destination is offset by m_ox
		const int x0 = std::max({c.min_x, 0, -m_ox}), x1 = std::min({c.max_x, 495, bm.width() - 1 - m_ox});
		for (int y = std::max(c.min_y, 0); y <= std::min({c.max_y, 383, bm.height() - 1}); y++)
			for (int x = x0; x <= x1; x++) {
				size_t i = size_t((y + scrolly) & 511) * 512 + ((x + scrollx) & 511);
				if (flags[l][i] == want || (fl & DRAW_OPAQUE)) {
					bm.pix(y, x + m_ox) = m_pens[pix[l][i] & 0x1fff];
					if (m_coord) m_coord[size_t(y) * bm.width() + x + m_ox] = uint32_t(l) << 18 | uint32_t(i);
				}
			}
	}

	void draw_rect(int l, bitmap_rgb32 &dm, const uint16_t *mask, uint16_t tpri, int fl, int win,
				   int sx, int sy, int xx1, int yy1, int xx2, int yy2)
	{
		tpri |= 0x10;
		int dst_x = xx1;            // MAME takes the destination before the group shift
		mask += yy1 * 4;
		while (xx1 >= 128) { xx1 -= 128; xx2 -= 128; mask++; }
		for (int y = 0; y < yy2 - yy1; y++) {
			int dy = yy1 + y;
			if (dy >= dm.height()) break;
			const uint16_t *src = &pix[l][size_t((sy + y) & 511) * 512 + sx];
			const uint8_t *srct = &flags[l][size_t((sy + y) & 511) * 512 + sx];
			uint32_t *dst = &dm.pix(dy, dst_x + m_ox);
			uint32_t *crd = m_coord ? m_coord + size_t(dy) * dm.width() + dst_x + m_ox : nullptr;
			const uint16_t *mask1 = mask + y * 4;
			int llx = xx2, cur_x = xx1;
			while (llx > 0) {
				uint16_t m = *mask1++;
				if (win) m = ~m;
				int llx1 = llx >= 128 ? 128 : llx;
				for (int x = cur_x; x < llx1; x++) {
					if ((*srct == tpri || (fl & DRAW_OPAQUE)) && !(m & (0x8000 >> (x >> 3)))) {
						*dst = m_pens[*src & 0x1fff];
						if (crd) *crd = uint32_t(l) << 18 | uint32_t(src - pix[l].data());
					}
					src++; srct++; dst++;
					if (crd) crd++;
				}
				llx -= 128;
				cur_x = 0;
			}
		}
	}
};
