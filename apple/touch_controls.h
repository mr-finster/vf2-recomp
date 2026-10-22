// The on-screen controls (a touch screen: the phone): an eight-way joystick, guard, punch and kick, Start and a
// Menu button. Native Apple views and the SDL fallback share layout and digital input state.
// Controls use the whole window, independently of the game aspect ratio, and feed the same input
// bits as keys and pads (main.cpp's read_inputs). The host decides when they are shown
// (On-screen controls: Off, when no pad is held, or On) and draws them itself.
#pragma once
#include <SDL3/SDL.h>
#include <cmath>
#include <cstring>

struct touch_controls
{
	// what a finger holds (the host turns these into the board's bits: PL_*), and the two
	// one-frame edges it takes (the board's Start, and the menu)
	enum action { NONE = -1, UP, DOWN, LEFT, RIGHT, GUARD, PUNCH, KICK, START, MENU,
		GUARD_PUNCH, GUARD_KICK, GUARD_PUNCH_KICK, PUNCH_KICK, ACTIONS };
	static constexpr int MAX_FINGERS = 10;

	// The fallback uses a separate full-window coordinate space. Native views use
	// layout() directly in UIKit/AppKit points, without touching the renderer.
	struct screen_space {
		SDL_Renderer *ren;
		int width = 0, height = 0;
		SDL_RendererLogicalPresentation mode;
		bool changed;
		explicit screen_space(SDL_Renderer *r) : ren(r) {
			SDL_GetRenderLogicalPresentation(ren, &width, &height, &mode);
			int w = 0, h = 0;
			SDL_GetRenderOutputSize(ren, &w, &h);
			const int ui_width = h > 0 ? int(std::lround(384.0 * w / h)) : 512;
			changed = width != ui_width || height != 384 || mode != SDL_LOGICAL_PRESENTATION_STRETCH;
			if (changed) SDL_SetRenderLogicalPresentation(ren, ui_width, 384, SDL_LOGICAL_PRESENTATION_STRETCH);
		}
		~screen_space() { if (changed) SDL_SetRenderLogicalPresentation(ren, width, height, mode); }
		screen_space(const screen_space &) = delete;
		screen_space &operator=(const screen_space &) = delete;
	};
	struct box { SDL_FRect r; int action; bool circle; const char *label; };
	static int boxes(SDL_Renderer *ren, box *out, bool menu = false)
	{
		screen_space screen(ren);
		SDL_Rect safe;
		if (!SDL_GetRenderSafeArea(ren, &safe)) SDL_GetRenderViewport(ren, &safe);
		return layout({float(safe.x), float(safe.y), float(safe.w), float(safe.h)}, out, menu);
	}
	static int layout(const SDL_FRect &safe, box *out, bool menu = false)
	{
		const float W = float(safe.w), H = float(safe.h);
		const float ox = float(safe.x), oy = float(safe.y);
		const float u = H / 384.0f;
		const float cy = oy + H - 74 * u, R = 32 * u, arm = 46 * u, reach = 46 * u, padx = ox + 80 * u;
		int n = 0;
		const SDL_FRect pad[4] = {
			{ padx - arm / 2, cy - reach - arm / 2, arm, arm },   // up
			{ padx - arm / 2, cy + reach - arm / 2, arm, arm },   // down
			{ padx - reach - arm / 2, cy - arm / 2, arm, arm },   // left
			{ padx + reach - arm / 2, cy - arm / 2, arm, arm },   // right
		};
		for (int i = 0; i < 4; i++) out[n++] = { pad[i], UP + i, false, nullptr };
		const struct { float x, y; int action; const char *label; } buttons[] = {
			{ ox + W - 184 * u, cy + 26 * u, GUARD, "G" },
			{ ox + W - 114 * u, cy - 16 * u, PUNCH, "P" },
			{ ox + W - 44 * u, cy + 26 * u, KICK, "K" },
		};
		for (const auto &t : buttons) out[n++] = { { t.x - R, t.y - R, 2 * R, 2 * R }, t.action, true, t.label };
		out[n++] = { { ox + W - 88 * u, oy + 12 * u, 76 * u, 32 * u }, START, false, "Start" };
		out[n++] = { { ox + 12 * u, oy + 12 * u, 76 * u, 32 * u }, MENU, false, "Menu" };
		if (!menu) {
			// Smaller combination buttons surround G/P/K without overlapping them.
			const float r = 25 * u;
			const struct { float x, y; int action; const char *label; } combos[] = {
				{ ox + W - 184 * u, cy - 46 * u, GUARD_PUNCH, "G+P" },
				{ ox + W - 44 * u, cy - 46 * u, GUARD_KICK, "G+K" },
				{ ox + W - 114 * u, cy + 45 * u, GUARD_PUNCH_KICK, "G+P+K" },
				{ ox + W - 244 * u, cy + 45 * u, PUNCH_KICK, "P+K" },
			};
			for (const auto &t : combos) out[n++] = { { t.x - r, t.y - r, 2 * r, 2 * r }, t.action, true, t.label };
		}
		return n;
	}
	static SDL_FRect stick_bounds(SDL_Renderer *ren)
	{
		box b[ACTIONS];
		boxes(ren, b);
		return stick_bounds(b);
	}
	static SDL_FRect stick_bounds(const box *b)
	{
		return { b[LEFT].r.x, b[UP].r.y,
			b[RIGHT].r.x + b[RIGHT].r.w - b[LEFT].r.x,
			b[DOWN].r.y + b[DOWN].r.h - b[UP].r.y };
	}
	static bool contains_window_point(SDL_Renderer *ren, float wx, float wy, bool menu = false)
	{
		screen_space screen(ren);
		float x = 0, y = 0;
		return SDL_RenderCoordinatesFromWindow(ren, wx, wy, &x, &y) &&
			(inside_stick(stick_bounds(ren), x, y) || at(ren, x, y, menu) != NONE);
	}

	// A touch starting inside the stick's touch area owns it until lifted/canceled, including
	// drags outside its circle. Other fingers can press attack buttons independently.
	// The return value is a single navigation edge for menus; held() exposes both
	// direction bits for diagonals to the game.
	int finger(SDL_Renderer *ren, SDL_Window *win, const SDL_TouchFingerEvent &f, bool down, bool menu = false)
	{
		screen_space screen(ren);
		int ww = 0, wh = 0;
		SDL_GetWindowSize(win, &ww, &wh);
		float x = 0, y = 0;
		if (!SDL_RenderCoordinatesFromWindow(ren, f.x * float(ww), f.y * float(wh), &x, &y)) return NONE;
		box b[ACTIONS];
		const int n = boxes(ren, b, menu);
		return finger_at(f.fingerID, x, y, down, f.type == SDL_EVENT_FINGER_DOWN, b, n);
	}
	// Backend-independent touch entry point: coordinates and layout share native view points.
	int finger_at(SDL_FingerID id, float x, float y, bool down, bool beginning, const box *b, int n)
	{
		int slot = -1;
		for (int i = 0; i < MAX_FINGERS; i++)
			if (m_fingers[i].used && m_fingers[i].id == id) { slot = i; break; }
		if (!down) { if (slot >= 0) lift(slot); return NONE; }
		// After a menu/lifecycle reset, a still-held finger must lift before reusing it.
		if (slot < 0 && !beginning) return NONE;
		if (slot < 0)
			for (int i = 0; i < MAX_FINGERS; i++)
				if (!m_fingers[i].used) { slot = i; break; }
		if (slot < 0) return NONE;
		const SDL_FRect stick = stick_bounds(b);
		auto &finger = m_fingers[slot];
		if (!finger.used) {
			finger.used = true;
			finger.id = id;
			finger.stick = inside_stick(stick, x, y);
			if (finger.stick && m_stick < 0) m_stick = slot;
		}
		unsigned mask = 0;
		int action = NONE;
		if (finger.stick) {
			// A second thumb never takes over or adds an opposing direction.
			if (m_stick != slot) return NONE;
			const float radius = stick.w / 2;
			const float dx = (x - stick.x - radius) / radius;
			const float dy = (y - stick.y - radius) / radius;
			const float length = std::sqrt(dx * dx + dy * dy);
			m_stick_x = dx / (length > 1 ? length : 1);
			m_stick_y = dy / (length > 1 ? length : 1);
			if (length > 0.22f) {
				// Widen horizontal-only movement to +/-30 degrees to reduce accidental
				// jumping/crouching. Vertical-only sectors keep their +/-22.5-degree width.
				constexpr float horizontal_corner = 0.41421356f; // tan(22.5 degrees)
				constexpr float vertical_corner = 0.57735027f; // tan(30 degrees)
				if (std::fabs(dx) >= std::fabs(dy) * horizontal_corner) mask |= 1u << (dx < 0 ? LEFT : RIGHT);
				if (std::fabs(dy) > std::fabs(dx) * vertical_corner) mask |= 1u << (dy < 0 ? UP : DOWN);
				action = std::fabs(dx) > std::fabs(dy) ? (dx < 0 ? LEFT : RIGHT) : (dy < 0 ? UP : DOWN);
			}
		} else {
			action = at(b, n, x, y);
			if (action != NONE) mask = 1u << action;
			// Keep the button identity for its highlight, and reference-count the
			// component inputs alongside ordinary G/P/K touches.
			if (action == GUARD_PUNCH || action == GUARD_PUNCH_KICK) mask |= (1u << GUARD) | (1u << PUNCH);
			if (action == GUARD_KICK || action == GUARD_PUNCH_KICK) mask |= (1u << GUARD) | (1u << KICK);
			if (action == PUNCH_KICK) mask |= (1u << PUNCH) | (1u << KICK);
		}
		const bool pressed = action != NONE && action != finger.menu_action && (finger.stick || !held(action));
		finger.menu_action = action;
		set(slot, mask);
		++m_revision;
		return pressed ? action : NONE;
	}

	// No held inputs survive a menu transition or the app going to the background.
	void release()
	{
		for (int i = 0; i < MAX_FINGERS; i++) lift(i);
		m_start = m_menu = false;
	}

	unsigned revision() const { return m_revision; }
	bool stick_active() const { return m_stick >= 0; }
	SDL_FPoint stick_position() const { return {m_stick_x, m_stick_y}; }
	bool held(int a) const { return a >= 0 && a < ACTIONS && m_held[a] > 0; }
	// Start and Menu are taken once: by the frame that acts on them
	bool take_start() { const bool e = m_start; m_start = false; return e; }
	bool take_menu() { const bool e = m_menu; m_menu = false; return e; }

	// The overlay, over the picture: white and dim, a held button brighter
	void draw(SDL_Renderer *ren, bool menu = false) const
	{
		screen_space screen(ren);
		box b[ACTIONS];
		const int n = boxes(ren, b, menu);
		if (menu) {
			for (int i = 0; i < n; i++) {
				switch (b[i].action) {
				case GUARD: case MENU: b[i].label = "Back"; break;
				case PUNCH: b[i].label = "OK"; break;
				case KICK: b[i].label = "Clear"; break;
				case START: b[i].label = "Resume"; break;
				default: break;
				}
			}
		}
		SDL_BlendMode was = SDL_BLENDMODE_NONE;
		SDL_GetRenderDrawBlendMode(ren, &was);
		SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
		pad(ren);
		for (int i = 0; i < n; i++) {
			if (b[i].action <= RIGHT) continue;   // the joystick's direction anchors
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
	struct finger_state {
		SDL_FingerID id = 0;
		unsigned mask = 0;
		int menu_action = NONE;
		bool used = false, stick = false;
	};
	finger_state m_fingers[MAX_FINGERS];
	int m_held[ACTIONS] = {};
	bool m_start = false, m_menu = false;
	int m_stick = -1;
	float m_stick_x = 0, m_stick_y = 0;
	unsigned m_revision = 0;

	static bool inside_stick(const SDL_FRect &r, float x, float y)
	{
		// Expand touch capture only; drawing and movement still use the visible radius.
		constexpr float touch_scale = 1.3f;
		const float radius = r.w * touch_scale / 2;
		const float dx = x - r.x - r.w / 2, dy = y - r.y - r.h / 2;
		return dx * dx + dy * dy <= radius * radius;
	}
	void lift(int slot)
	{
		if (m_fingers[slot].used) ++m_revision;
		set(slot, 0);
		m_fingers[slot] = {};
		if (m_stick == slot) { m_stick = -1; m_stick_x = m_stick_y = 0; }
	}
	void set(int slot, unsigned mask)
	{
		const unsigned was = m_fingers[slot].mask;
		for (int a = 0; a < ACTIONS; a++) {
			const unsigned bit = 1u << a;
			if ((was & bit) && !(mask & bit)) --m_held[a];
			if (!(was & bit) && (mask & bit) && ++m_held[a] == 1) {
				if (a == START) m_start = true;
				if (a == MENU) m_menu = true;
			}
		}
		m_fingers[slot].mask = mask;
	}

	// Which overlay button is at (x, y) in the renderer's coordinates (NONE: the picture itself)
	static int at(SDL_Renderer *ren, float x, float y, bool menu)
	{
		box b[ACTIONS];
		const int n = boxes(ren, b, menu);
		return at(b, n, x, y);
	}
public:
	static bool contains(const box *b, int n, float x, float y)
	{
		return inside_stick(stick_bounds(b), x, y) || at(b, n, x, y) != NONE;
	}
private:
	static int at(const box *b, int n, float x, float y)
	{
		for (int i = n - 1; i >= 0; i--) {
			if (b[i].action <= RIGHT) continue;
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
	static constexpr int CIRCLE_SEGMENTS = 64;
	static void fan(SDL_Renderer *ren, float cx, float cy, const SDL_FPoint *p, int count, float alpha, float gray = 1.0f)
	{
		const SDL_FColor c = { gray, gray, gray, alpha };
		SDL_Vertex v[CIRCLE_SEGMENTS + 1];
		int idx[3 * CIRCLE_SEGMENTS];
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
	static void disc(SDL_Renderer *ren, const SDL_FRect &r, float alpha, float gray = 1.0f)
	{
		const float cx = r.x + r.w / 2, cy = r.y + r.h / 2, rad = r.w / 2;
		const int seg = CIRCLE_SEGMENTS;
		SDL_FPoint p[CIRCLE_SEGMENTS];
		for (int i = 0; i < seg; i++) {
			const float a = 6.2831853f * float(i) / float(seg);
			p[i] = SDL_FPoint{ cx + rad * cosf(a), cy + rad * sinf(a) };
		}
		fan(ren, cx, cy, p, seg, alpha, gray);
	}
	static void ring(SDL_Renderer *ren, const SDL_FRect &r, float alpha)
	{
		SDL_SetRenderDrawColor(ren, 255, 255, 255, Uint8(alpha * 255.0f));
		const float cx = r.x + r.w / 2, cy = r.y + r.h / 2, rad = r.w / 2;
		const int seg = CIRCLE_SEGMENTS;
		SDL_FPoint p[CIRCLE_SEGMENTS + 1];
		for (int i = 0; i <= seg; i++) {
			const float a = 6.2831853f * float(i) / float(seg);
			p[i] = SDL_FPoint{ cx + rad * cosf(a), cy + rad * sinf(a) };
		}
		SDL_RenderLines(ren, p, seg + 1);
	}
	// A fixed circular base and a thumb that follows the finger, clamped to its rim.
	void pad(SDL_Renderer *ren) const
	{
		const SDL_FRect base = stick_bounds(ren);
		const float radius = base.w / 2, knob = radius * 0.32f;
		disc(ren, base, 0.72f, 0.12f);
		ring(ren, base, 0.38f);
		const float travel = radius - knob;
		const float x = base.x + radius + m_stick_x * travel;
		const float y = base.y + radius + m_stick_y * travel;
		const SDL_FRect thumb { x - knob, y - knob, 2 * knob, 2 * knob };
		disc(ren, thumb, m_stick >= 0 ? 0.95f : 0.8f, m_stick >= 0 ? 0.85f : 0.65f);
		ring(ren, thumb, m_stick >= 0 ? 0.9f : 0.45f);
	}
};
