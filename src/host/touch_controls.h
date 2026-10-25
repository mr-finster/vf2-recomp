// The on-screen controls (a touch screen: the phone): a d-pad, guard, punch and kick, Start and a
// Menu button. Drawn over the game's picture in the renderer's own coordinates, so the logical
// presentation letterboxes them with it whatever the picture's shape, and read into the same input
// bits the keys and pads set (main.cpp's read_inputs). The host decides when they are shown
// (On-screen controls: Off, when no pad is held, or On) and draws them itself.
#pragma once
#include <SDL3/SDL.h>
#include <cmath>
#include <cstring>

struct touch_controls
{
	// what a finger holds (the host turns these into the board's bits: PL_*), and the two
	// one-frame edges it takes (the board's Start, and the menu)
	enum action { NONE = -1, UP, DOWN, LEFT, RIGHT, GUARD, PUNCH, KICK, START, MENU, ACTIONS };
	static constexpr int MAX_FINGERS = 10;

	// The overlay's boxes, in the renderer's coordinates: the d-pad's four arms, guard, punch and
	// kick (circles), Start and Menu. The picture is 384 lines tall wherever it is shown, so a
	// unit here is one of the arcade's own pixels and the controls keep their share of the screen.
	struct box { SDL_FRect r; int action; bool circle; const char *label; };
	static int boxes(SDL_Renderer *ren, box *out)
	{
		int lw = 0, lh = 0, ow = 0, oh = 0;
		SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
		SDL_GetRenderLogicalPresentation(ren, &lw, &lh, &mode);
		SDL_GetRenderOutputSize(ren, &ow, &oh);
		const bool logical = mode != SDL_LOGICAL_PRESENTATION_DISABLED && lw && lh;
		const float W = float(logical ? lw : ow), H = float(logical ? lh : oh);
		const float u = H / 384.0f;
		const float cy = H - 74 * u, R = 32 * u, arm = 46 * u, reach = 46 * u, padx = 80 * u;
		int n = 0;
		const SDL_FRect pad[4] = {
			{ padx - arm / 2, cy - reach - arm / 2, arm, arm },   // up
			{ padx - arm / 2, cy + reach - arm / 2, arm, arm },   // down
			{ padx - reach - arm / 2, cy - arm / 2, arm, arm },   // left
			{ padx + reach - arm / 2, cy - arm / 2, arm, arm },   // right
		};
		for (int i = 0; i < 4; i++) out[n++] = { pad[i], UP + i, false, nullptr };
		const struct { float x, y; int action; const char *label; } buttons[] = {
			{ W - 220 * u, cy + 26 * u, GUARD, "G" },
			{ W - 150 * u, cy - 4 * u, PUNCH, "P" },
			{ W - 80 * u, cy + 26 * u, KICK, "K" },
		};
		for (const auto &t : buttons) out[n++] = { { t.x - R, t.y - R, 2 * R, 2 * R }, t.action, true, t.label };
		out[n++] = { { W - 190 * u, 30 * u, 84 * u, 40 * u }, START, false, "Start" };
		out[n++] = { { W - 96 * u, 30 * u, 84 * u, 40 * u }, MENU, false, "Menu" };
		return n;
	}

	// A finger touching, moving or lifting, as SDL reports it: held over an overlay button it
	// presses it; anywhere else it holds nothing (the finger is still tracked, so a move back
	// onto a button presses it). `down` is false for a lift, or for the system taking it away.
	void finger(SDL_Renderer *ren, SDL_Window *win, const SDL_TouchFingerEvent &f, bool down)
	{
		int slot = -1;
		for (int i = 0; i < MAX_FINGERS; i++)
			if (m_fingers[i].used && m_fingers[i].id == f.fingerID) { slot = i; break; }
		if (!down) { if (slot >= 0) set(slot, 0, false, NONE); return; }
		if (slot < 0)
			for (int i = 0; i < MAX_FINGERS; i++)
				if (!m_fingers[i].used) { slot = i; break; }
		if (slot < 0) return;   // (more fingers at once than we track: the rest hold nothing)
		int ww = 0, wh = 0;
		SDL_GetWindowSize(win, &ww, &wh);   // (a touch's x, y are of the window's points, 0..1)
		float x = 0, y = 0;
		if (!SDL_RenderCoordinatesFromWindow(ren, f.x * float(ww), f.y * float(wh), &x, &y)) return;
		set(slot, f.fingerID, true, at(ren, x, y));
	}

	// Every finger let go: the menu opened, or the app went to the background (no lift is coming)
	void release() { for (int i = 0; i < MAX_FINGERS; i++) set(i, 0, false, NONE); m_start = m_menu = false; }

	bool held(int a) const { return a >= 0 && m_held[a] > 0; }
	// Start and Menu are taken once: by the frame that acts on them
	bool take_start() { const bool e = m_start; m_start = false; return e; }
	bool take_menu() { const bool e = m_menu; m_menu = false; return e; }

	// The overlay, over the picture: white and dim, a held button brighter
	void draw(SDL_Renderer *ren) const
	{
		box b[ACTIONS];
		const int n = boxes(ren, b);
		SDL_BlendMode was = SDL_BLENDMODE_NONE;
		SDL_GetRenderDrawBlendMode(ren, &was);
		SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
		pad(ren, b, n);   // (the four arms as one plus: no seams, one shade while a way is held)
		for (int i = 0; i < n; i++) {
			if (b[i].action <= RIGHT) continue;   // the d-pad's own
			const SDL_FRect &r = b[i].r;
			const bool on = held(b[i].action);
			if (b[i].circle) {
				disc(ren, r, on ? 0.45f : 0.16f);
				ring(ren, r, on ? 1.0f : 0.55f);
			} else {
				SDL_SetRenderDrawColor(ren, 255, 255, 255, on ? 120 : 45);
				SDL_RenderFillRect(ren, &r);
				SDL_SetRenderDrawColor(ren, 255, 255, 255, on ? 255 : 140);
				SDL_RenderRect(ren, &r);
			}
			if (b[i].label) {
				SDL_SetRenderDrawColor(ren, 255, 255, 255, on ? 255 : 190);
				const float lw = 8.0f * float(strlen(b[i].label));
				SDL_RenderDebugText(ren, r.x + (r.w - lw) / 2.0f, r.y + (r.h - 8.0f) / 2.0f, b[i].label);
			}
		}
		SDL_SetRenderDrawBlendMode(ren, was);
	}

private:
	// (a finger's ID needn't be non-zero: Android's is the pointer's, from 0 up, so "used" says)
	struct finger_state { SDL_FingerID id = 0; int action = NONE; bool used = false; };
	finger_state m_fingers[MAX_FINGERS];
	int m_held[ACTIONS] = {};
	bool m_start = false, m_menu = false;

	// A finger's action changes: the old one let go, the new one held (the edges when a hold
	// begins, so two fingers on one button are still one press)
	void set(int slot, SDL_FingerID id, bool used, int a)
	{
		const int was = m_fingers[slot].action;
		if (used && m_fingers[slot].used && was == a) { m_fingers[slot].id = id; return; }   // (a move within one button)
		if (was >= 0) m_held[was]--;
		m_fingers[slot].id = id;
		m_fingers[slot].used = used;
		m_fingers[slot].action = a;
		if (a >= 0 && ++m_held[a] == 1) {
			if (a == START) m_start = true;
			if (a == MENU) m_menu = true;
		}
	}

	// Which overlay button is at (x, y) in the renderer's coordinates (NONE: the picture itself)
	static int at(SDL_Renderer *ren, float x, float y)
	{
		box b[ACTIONS];
		const int n = boxes(ren, b);
		for (int i = n - 1; i >= 0; i--) {
			const SDL_FRect &r = b[i].r;
			if (b[i].circle) {
				const float dx = x - (r.x + r.w / 2), dy = y - (r.y + r.h / 2);
				if (dx * dx + dy * dy <= (r.w / 2) * (r.w / 2)) return b[i].action;
			} else if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
				return b[i].action;
			}
		}
		return NONE;
	}

	// A filled fan of triangles from (cx, cy) out to the points
	static void fan(SDL_Renderer *ren, float cx, float cy, const SDL_FPoint *p, int count, float alpha)
	{
		const SDL_FColor c = { 1, 1, 1, alpha };
		SDL_Vertex v[33];
		int idx[3 * 32];
		v[0].position = SDL_FPoint{ cx, cy }; v[0].color = c; v[0].tex_coord = SDL_FPoint{ 0, 0 };
		for (int i = 0; i < count; i++) {
			v[1 + i].position = p[i];
			v[1 + i].color = c;
			v[1 + i].tex_coord = SDL_FPoint{ 0, 0 };
		}
		for (int i = 0; i < count; i++) { idx[3 * i] = 0; idx[3 * i + 1] = 1 + i; idx[3 * i + 2] = 1 + (i + 1) % count; }
		SDL_RenderGeometry(ren, nullptr, v, count + 1, idx, count * 3);
	}
	// A round button: the disc from its centre, its rim drawn as a closed loop
	static void disc(SDL_Renderer *ren, const SDL_FRect &r, float alpha)
	{
		const float cx = r.x + r.w / 2, cy = r.y + r.h / 2, rad = r.w / 2;
		const int seg = 24;
		SDL_FPoint p[24];
		for (int i = 0; i < seg; i++) {
			const float a = 6.2831853f * float(i) / float(seg);
			p[i] = SDL_FPoint{ cx + rad * cosf(a), cy + rad * sinf(a) };
		}
		fan(ren, cx, cy, p, seg, alpha);
	}
	static void ring(SDL_Renderer *ren, const SDL_FRect &r, float alpha)
	{
		SDL_SetRenderDrawColor(ren, 255, 255, 255, Uint8(alpha * 255.0f));
		const float cx = r.x + r.w / 2, cy = r.y + r.h / 2, rad = r.w / 2;
		const int seg = 24;
		SDL_FPoint p[25];
		for (int i = 0; i <= seg; i++) {
			const float a = 6.2831853f * float(i) / float(seg);
			p[i] = SDL_FPoint{ cx + rad * cosf(a), cy + rad * sinf(a) };
		}
		SDL_RenderLines(ren, p, seg + 1);
	}
	// A triangle in an arm, pointing the way it goes
	static void arrow(SDL_Renderer *ren, int action, const SDL_FRect &r, float alpha)
	{
		const float s = 0.36f * r.w;
		const float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
		SDL_FPoint p[3];
		switch (action) {
		case UP:    p[0] = SDL_FPoint{ cx, cy - s }; p[1] = SDL_FPoint{ cx + s, cy + s }; p[2] = SDL_FPoint{ cx - s, cy + s }; break;
		case DOWN:  p[0] = SDL_FPoint{ cx, cy + s }; p[1] = SDL_FPoint{ cx - s, cy - s }; p[2] = SDL_FPoint{ cx + s, cy - s }; break;
		case LEFT:  p[0] = SDL_FPoint{ cx - s, cy }; p[1] = SDL_FPoint{ cx + s, cy + s }; p[2] = SDL_FPoint{ cx + s, cy - s }; break;
		default:    p[0] = SDL_FPoint{ cx + s, cy }; p[1] = SDL_FPoint{ cx - s, cy - s }; p[2] = SDL_FPoint{ cx - s, cy + s }; break;
		}
		const SDL_FColor c = { 1, 1, 1, alpha };
		SDL_Vertex v[3];
		for (int i = 0; i < 3; i++) { v[i].position = p[i]; v[i].color = c; v[i].tex_coord = SDL_FPoint{ 0, 0 }; }
		const int idx[3] = { 0, 1, 2 };
		SDL_RenderGeometry(ren, nullptr, v, 3, idx, 3);
	}
	static const SDL_FRect *find(const box *b, int n, int action)
	{
		for (int i = 0; i < n; i++) if (b[i].action == action) return &b[i].r;
		return nullptr;
	}
	// The d-pad: its four arms as one plus (so the joins don't show), the way held brighter
	void pad(SDL_Renderer *ren, const box *b, int n) const
	{
		const SDL_FRect *u = find(b, n, UP), *d = find(b, n, DOWN), *l = find(b, n, LEFT), *r = find(b, n, RIGHT);
		if (!u || !d || !l || !r) return;
		const float ht = u->h / 2, cx = u->x + u->w / 2, cy = l->y + l->h / 2, rr = r->x + r->w - cx;
		const SDL_FPoint q[12] = {
			{ cx - ht, cy - rr }, { cx + ht, cy - rr }, { cx + ht, cy - ht }, { cx + rr, cy - ht },
			{ cx + rr, cy + ht }, { cx + ht, cy + ht }, { cx + ht, cy + rr }, { cx - ht, cy + rr },
			{ cx - ht, cy + ht }, { cx - rr, cy + ht }, { cx - rr, cy - ht }, { cx - ht, cy - ht },
		};
		fan(ren, cx, cy, q, 12, 0.20f);
		// the way held lit from its tip in to the middle, as a d-pad's own would be
		for (int a = UP; a <= RIGHT; a++)
			if (held(a)) {
				SDL_FRect s = { cx - ht, cy - rr, 2 * ht, rr };                 // up
				if (a == DOWN) s.y = cy;
				else if (a == LEFT) { s.x = cx - rr; s.y = cy - ht; s.w = rr; s.h = 2 * ht; }
				else if (a == RIGHT) { s.x = cx; s.y = cy - ht; s.w = rr; s.h = 2 * ht; }
				SDL_SetRenderDrawColor(ren, 255, 255, 255, 70);
				SDL_RenderFillRect(ren, &s);
			}
		SDL_SetRenderDrawColor(ren, 255, 255, 255, 150);
		SDL_FPoint loop[13];
		for (int i = 0; i < 13; i++) loop[i] = q[i % 12];
		SDL_RenderLines(ren, loop, 13);
		for (int a = UP; a <= RIGHT; a++) arrow(ren, a, *find(b, n, a), held(a) ? 1.0f : 0.7f);
	}
};
