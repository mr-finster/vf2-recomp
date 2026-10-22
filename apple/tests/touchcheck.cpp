// The on-screen controls (apple/touch_controls.h) without a game or a ROM: SDL's own finger
// events pressed, held, moved and lifted over the overlay's boxes, and the overlay drawn offscreen
// to a PNG to look at.
//
//   touchcheck [WIDTH] [HEIGHT] [PNG]
//   e.g. touchcheck 2400 1080 build/touch_controls.png     (a 20:9 phone, the default)
//        SDL_VIDEODRIVER=offscreen touchcheck              (without a display)
//
// The overlay has its own full-window coordinates, independent of the letterboxed game.
// Native views use the same layout and input state directly in their own points.
// The behaviours checked are the ones the host relies on: a finger on a box holds it, a lift, a
// move off it and a canceled touch let go, two fingers on one box are one press, and Start and
// Menu come once (the one-frame edges the host takes).
#include "../touch_controls.h"
#include "../../src/host/png.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static SDL_Renderer *ren;
static SDL_Window *win;
static touch_controls::box bx[touch_controls::ACTIONS];
static const char *names[] = { "up", "down", "left", "right", "guard", "punch", "kick", "Start", "Menu", "G+P", "G+K", "G+P+K", "P+K" };

static int fails;
static void check(bool ok, const char *what)
{
	printf("%s %s\n", ok ? "  ok  " : "FAIL  ", what);
	if (!ok) fails++;
}

static void window_size(int &w, int &h) { SDL_GetWindowSize(win, &w, &h); }

// A touch's x, y (of the window, 0..1) over a box's centre: -1 is the picture itself
static void spot(int action, float &nx, float &ny)
{
	for (int i = 0; i < touch_controls::ACTIONS; i++)
		if (bx[i].action == action) {
			int ww = 0, wh = 0;
			window_size(ww, wh);
			float wx = 0, wy = 0;
			touch_controls::screen_space screen(ren);
			SDL_RenderCoordinatesToWindow(ren, bx[i].r.x + bx[i].r.w / 2, bx[i].r.y + bx[i].r.h / 2, &wx, &wy);
			nx = wx / float(ww);
			ny = wy / float(wh);
			return;
		}
	check(false, "no such box");
}

static SDL_TouchFingerEvent ev(SDL_EventType type, SDL_FingerID id, int action)
{
	SDL_TouchFingerEvent f = {};
	f.type = type;
	f.fingerID = id;
	f.x = f.y = 0.5f;
	if (action >= 0) spot(action, f.x, f.y);
	return f;
}

// Stick displacement in radii, including drags beyond the visible base.
static SDL_TouchFingerEvent stick_ev(SDL_EventType type, SDL_FingerID id, float dx, float dy)
{
	auto f = ev(type, id, -1);
	const SDL_FRect r = touch_controls::stick_bounds(ren);
	int ww = 0, wh = 0;
	window_size(ww, wh);
	float wx = 0, wy = 0;
	touch_controls::screen_space screen(ren);
	SDL_RenderCoordinatesToWindow(ren, r.x + r.w * (0.5f + dx / 2), r.y + r.h * (0.5f + dy / 2), &wx, &wy);
	f.x = wx / ww; f.y = wy / wh;
	return f;
}

static unsigned directions(const touch_controls &tc)
{
	unsigned mask = 0;
	for (int a = touch_controls::UP; a <= touch_controls::RIGHT; a++)
		if (tc.held(a)) mask |= 1u << a;
	return mask;
}

static unsigned attacks(const touch_controls &tc)
{
	return (tc.held(touch_controls::GUARD) ? 1u : 0u) |
		(tc.held(touch_controls::PUNCH) ? 2u : 0u) | (tc.held(touch_controls::KICK) ? 4u : 0u);
}

int main(int argc, char **argv)
{
	const int ww = argc > 1 ? atoi(argv[1]) : 2400;
	const int wh = argc > 2 ? atoi(argv[2]) : 1080;
	const char *out = argc > 3 ? argv[3] : "build/touch_controls.png";
	if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
	win = SDL_CreateWindow("touchcheck", ww, wh, SDL_WINDOW_HIDDEN);
	ren = win ? SDL_CreateRenderer(win, nullptr) : nullptr;
	if (!ren) { fprintf(stderr, "no renderer: %s\n", SDL_GetError()); return 1; }
	// A narrow game in a wide window must not pull its controls inward.
	SDL_SetRenderLogicalPresentation(ren, 512, 384, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	printf("%dx%d, %s\n", ww, wh, SDL_GetRendererName(ren));

	touch_controls tc;
	const int n = touch_controls::boxes(ren, bx);
	check(n == touch_controls::ACTIONS, "a box for each control");
	int vw = 0, vh = 0;
	window_size(vw, vh);
	for (int i = 0; i < n; i++) {
		float wx = 0, wy = 0;
		touch_controls::screen_space screen(ren);
		SDL_RenderCoordinatesToWindow(ren, bx[i].r.x + bx[i].r.w / 2, bx[i].r.y + bx[i].r.h / 2, &wx, &wy);
		const bool inside = wx > 0 && wx < vw && wy > 0 && wy < vh;
		printf("  %-5s logical %6.1f,%6.1f %5.1fx%-5.1f  on screen %6.1f,%6.1f%s\n",
			names[bx[i].action], bx[i].r.x, bx[i].r.y, bx[i].r.w, bx[i].r.h, wx, wy, inside ? "" : "  OFF SCREEN");
		if (!inside) fails++;
		if (bx[i].circle) {
			for (int j = 0; j < i; j++) if (bx[j].circle) {
				const float dx = bx[i].r.x + bx[i].r.w / 2 - bx[j].r.x - bx[j].r.w / 2;
				const float dy = bx[i].r.y + bx[i].r.h / 2 - bx[j].r.y - bx[j].r.h / 2;
				const float radii = (bx[i].r.w + bx[j].r.w) / 2;
				check(dx * dx + dy * dy >= radii * radii, "attack button touch areas do not overlap");
			}
		}
	}
	touch_controls::box wide[touch_controls::ACTIONS];
	SDL_SetRenderLogicalPresentation(ren, 896, 384, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	touch_controls::boxes(ren, wide);
	for (int i = 0; i < n; i++)
		check(bx[i].r.x == wide[i].r.x && bx[i].r.y == wide[i].r.y && bx[i].r.w == wide[i].r.w && bx[i].r.h == wide[i].r.h,
			"control position and size are independent of game aspect ratio");
	SDL_SetRenderLogicalPresentation(ren, 512, 384, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	float start_x = 0, start_y = 0, menu_x = 0, menu_y = 0;
	spot(touch_controls::START, start_x, start_y);
	spot(touch_controls::MENU, menu_x, menu_y);
	check(menu_x < .25f && start_x > .75f && start_y < .2f && menu_y < .2f,
		"Menu is at the upper left and Start is at the upper right");

	// Android's finger IDs start at 0: the first one must not be mistaken for a free slot
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 0, touch_controls::LEFT), true);
	check(tc.held(touch_controls::LEFT) && !tc.held(touch_controls::RIGHT), "finger 0 on left: left held");
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 1, touch_controls::PUNCH), true);
	check(tc.held(touch_controls::PUNCH) && tc.held(touch_controls::LEFT), "a second finger on punch: both held");
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_UP, 1, touch_controls::PUNCH), false);
	check(!tc.held(touch_controls::PUNCH) && tc.held(touch_controls::LEFT), "punch's lift: punch free, left held");

	// A button finger slides between attack buttons; a stick finger stays on the stick.
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_UP, 0, touch_controls::LEFT), false);
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 0, touch_controls::GUARD), true);
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_MOTION, 0, touch_controls::KICK), true);
	check(!tc.held(touch_controls::LEFT) && tc.held(touch_controls::KICK), "the same finger moved onto kick");
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_MOTION, 0, -1), true);
	check(!tc.held(touch_controls::LEFT) && !tc.held(touch_controls::KICK), "moved onto the picture: nothing held");
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_UP, 0, -1), false);

	// two fingers on one button: one press, until the last of them lifts
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 2, touch_controls::PUNCH), true);
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 3, touch_controls::PUNCH), true);
	check(tc.held(touch_controls::PUNCH), "two fingers on punch: held");
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_UP, 2, touch_controls::PUNCH), false);
	check(tc.held(touch_controls::PUNCH), "one of them lifted: still held");
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_UP, 3, touch_controls::PUNCH), false);
	check(!tc.held(touch_controls::PUNCH), "the last lifted: free");

	// Start and Menu come once (the host takes the edge), and the system taking a finger away is a lift
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 4, touch_controls::START), true);
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 5, touch_controls::MENU), true);
	check(tc.take_start() && !tc.take_start(), "Start is taken once");
	check(tc.take_menu() && !tc.take_menu(), "Menu is taken once");
	check(tc.held(touch_controls::START) && tc.held(touch_controls::MENU), "both are still held");
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_CANCELED, 4, touch_controls::START), false);
	check(!tc.held(touch_controls::START), "a canceled finger lets go");

	// everything at once: the menu opening, or the app going to the background
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 6, touch_controls::GUARD), true);
	tc.release();
	check(!tc.held(touch_controls::GUARD) && !tc.held(touch_controls::MENU), "release() frees every finger");
	check(!tc.take_start() && !tc.take_menu(), "release() clears the edges");

	// Menu navigation must see a quick tap even if down/up arrive in one frame,
	// and must not repeat a command for motion or a second finger on the same button.
	check(tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 0, touch_controls::DOWN), true) == touch_controls::DOWN, "menu tap emits down immediately");
	check(tc.finger(ren, win, ev(SDL_EVENT_FINGER_MOTION, 0, touch_controls::DOWN), true) == touch_controls::NONE, "motion within a menu button does not repeat");
	check(tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 1, touch_controls::DOWN), true) == touch_controls::NONE, "second finger does not duplicate navigation");
	check(tc.finger(ren, win, ev(SDL_EVENT_FINGER_UP, 1, touch_controls::DOWN), false) == touch_controls::NONE, "lift is not a navigation command");
	check(tc.finger(ren, win, ev(SDL_EVENT_FINGER_MOTION, 0, touch_controls::LEFT), true) == touch_controls::LEFT, "stick direction change navigates once");
	tc.release();
	check(tc.finger(ren, win, ev(SDL_EVENT_FINGER_MOTION, 0, touch_controls::LEFT), true) == touch_controls::NONE && directions(tc) == 0,
		"menu reset ignores a still-held finger until the next touch down");

	// The arcade sees eight digital directions; continuous drags must preserve
	// down -> down-left -> left, simultaneous attacks, neutral and cancellation.
	using t = touch_controls;
	constexpr unsigned U = 1u << t::UP, D = 1u << t::DOWN, L = 1u << t::LEFT, R = 1u << t::RIGHT;
	tc.finger(ren, win, stick_ev(SDL_EVENT_FINGER_DOWN, 0, 0, 0), true);
	check(directions(tc) == 0, "stick center is neutral");
	tc.finger(ren, win, stick_ev(SDL_EVENT_FINGER_MOTION, 0, 0.1f, -0.1f), true);
	check(directions(tc) == 0, "small movement stays inside dead zone");
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 1, t::PUNCH), true);
	const struct { float x, y; unsigned mask; } turns[] = {
		{0, .8f, D}, {-.6f, .6f, D | L}, {-.8f, 0, L}, {-.6f, -.6f, U | L},
		{0, -.8f, U}, {.6f, -.6f, U | R}, {.8f, 0, R}, {.6f, .6f, D | R}
	};
	for (const auto &turn : turns) {
		tc.finger(ren, win, stick_ev(SDL_EVENT_FINGER_MOTION, 0, turn.x, turn.y), true);
		check(directions(tc) == turn.mask && tc.held(t::PUNCH), "circular drag gives exact direction bits while holding punch");
	}
	tc.finger(ren, win, stick_ev(SDL_EVENT_FINGER_MOTION, 0, -2, 1.5f), true);
	check(directions(tc) == (D | L), "drag outside circle keeps joystick ownership and diagonal");
	tc.finger(ren, win, stick_ev(SDL_EVENT_FINGER_DOWN, 2, .8f, 0), true);
	check(directions(tc) == (D | L), "second stick finger cannot add an opposing direction");
	tc.finger(ren, win, stick_ev(SDL_EVENT_FINGER_MOTION, 0, 0, 0), true);
	check(directions(tc) == 0 && tc.held(t::PUNCH), "returning to center releases directions only");
	tc.finger(ren, win, stick_ev(SDL_EVENT_FINGER_MOTION, 0, -.6f, -.6f), true);
	tc.finger(ren, win, stick_ev(SDL_EVENT_FINGER_CANCELED, 0, -.6f, -.6f), false);
	check(directions(tc) == 0 && tc.held(t::PUNCH), "cancellation releases both diagonal bits without releasing punch");
	tc.finger(ren, win, stick_ev(SDL_EVENT_FINGER_MOTION, 2, .8f, 0), true);
	check(directions(tc) == 0, "ignored second finger does not take over after cancellation");
	tc.release();
	check(tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 0, touch_controls::DOWN), true) == touch_controls::DOWN, "new press after release navigates again");
	tc.release();

	// Combination buttons assert their component bits in the same sample. Shared
	// components stay held until the last physical finger using them is released.
	const struct { int button; unsigned bits; } combos[] = {
		{t::GUARD_PUNCH, 3}, {t::GUARD_KICK, 5}, {t::GUARD_PUNCH_KICK, 7}, {t::PUNCH_KICK, 6}
	};
	for (const auto &combo : combos) {
		tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 0, combo.button), true);
		check(attacks(tc) == combo.bits && tc.held(combo.button), "combination presses exactly its component buttons");
		tc.finger(ren, win, ev(SDL_EVENT_FINGER_UP, 0, combo.button), false);
		check(attacks(tc) == 0 && !tc.held(combo.button), "combination lift releases all its inputs");
	}
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 0, t::GUARD_PUNCH), true);
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 1, t::GUARD_KICK), true);
	check(attacks(tc) == 7, "two combination buttons share guard and hold all three attacks");
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_UP, 0, t::GUARD_PUNCH), false);
	check(attacks(tc) == 5, "releasing G+P preserves the other finger's G+K");
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 2, t::GUARD), true);
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_CANCELED, 1, t::GUARD_KICK), false);
	check(attacks(tc) == 1, "canceled combination preserves a separately held guard");
	tc.release();
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 0, t::GUARD_PUNCH), true);
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_MOTION, 0, t::GUARD_KICK), true);
	check(attacks(tc) == 5 && !tc.held(t::GUARD_PUNCH), "sliding between combinations releases the previous components");
	tc.finger(ren, win, stick_ev(SDL_EVENT_FINGER_DOWN, 1, -.6f, .6f), true);
	check(directions(tc) == (D | L) && attacks(tc) == 5, "diagonal and combination input work together");
	tc.release();
	check(attacks(tc) == 0 && directions(tc) == 0, "menu or background reset clears combinations and stick together");
	touch_controls::box menu_boxes[touch_controls::ACTIONS];
	check(touch_controls::boxes(ren, menu_boxes, true) == t::MENU + 1, "menu shows only navigation controls");
	for (const auto &combo : combos) {
		check(tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 0, combo.button), true, true) == t::NONE && attacks(tc) == 0,
			"hidden combination buttons cannot navigate or press attacks in menus");
		tc.release();
	}
	// UIKit/AppKit supply native view points and a safe area, with no renderer calls.
	touch_controls::box native[touch_controls::ACTIONS];
	const int native_count = t::layout({40, 0, 880, 390}, native);
	const auto &native_combo = native[t::GUARD_PUNCH_KICK].r;
	for (const auto &combo : combos) {
		const auto &button = native[combo.button].r;
		tc.finger_at(123, button.x + button.w / 2, button.y + button.h / 2, true, true, native, native_count);
		check(attacks(tc) == combo.bits, "native view points feed the exact combination without renderer coordinates");
		tc.finger_at(123, 0, 0, false, false, native, native_count);
		check(attacks(tc) == 0, "native touch cancellation releases the combination");
	}

	// Small thumb drift while walking must stay horizontal. Check both sides of
	// the 30-degree boundary in every quadrant, at different distances, and on
	// the return from a deliberate diagonal while attack buttons remain held.
	const SDL_FRect native_stick = t::stick_bounds(native);
	const float radius = native_stick.w / 2;
	const float center_x = native_stick.x + radius, center_y = native_stick.y + radius;
	const struct { float degrees; bool horizontal, vertical; } angles[] = {
		{0, true, false}, {10, true, false}, {22.6f, true, false}, {29.9f, true, false},
		{30.1f, true, true}, {45, true, true}, {67.4f, true, true}, {67.6f, false, true},
		{90, false, true}, {45, true, true}, {29.9f, true, false}
	};
	for (float reach : {.25f, .8f, 1.5f}) {
		for (int horizontal : {-1, 1}) for (int vertical : {-1, 1}) {
			tc.finger_at(123, center_x, center_y, true, true, native, native_count);
			tc.finger_at(124, native_combo.x + native_combo.w / 2, native_combo.y + native_combo.h / 2,
				true, true, native, native_count);
			for (const auto &angle : angles) {
				const float radians = angle.degrees * 3.14159265358979323846f / 180.0f;
				tc.finger_at(123, center_x + horizontal * reach * radius * std::cos(radians),
					center_y + vertical * reach * radius * std::sin(radians), true, false, native, native_count);
				const unsigned expected = (angle.horizontal ? (horizontal < 0 ? L : R) : 0) |
					(angle.vertical ? (vertical < 0 ? U : D) : 0);
				char message[128];
				snprintf(message, sizeof message, "native stick %.1f degrees, quadrant %d/%d, radius %.2f keeps intended directions and attacks",
					angle.degrees, horizontal, vertical, reach);
				check(directions(tc) == expected && attacks(tc) == 7, message);
			}
			tc.finger_at(123, center_x, center_y, false, false, native, native_count);
			check(directions(tc) == 0 && attacks(tc) == 7, "releasing the corrected stick preserves held attacks");
			tc.release();
		}
	}

	// Draw the overlay with a stick and combination held.
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 0, touch_controls::LEFT), true);
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 1, touch_controls::GUARD_PUNCH), true);
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 2, touch_controls::START), true);
	SDL_SetRenderDrawColor(ren, 30, 30, 40, 255);
	SDL_RenderClear(ren);
	tc.draw(ren);
	int saved_w = 0, saved_h = 0;
	SDL_RendererLogicalPresentation saved_mode;
	SDL_GetRenderLogicalPresentation(ren, &saved_w, &saved_h, &saved_mode);
	check(saved_w == 512 && saved_h == 384 && saved_mode == SDL_LOGICAL_PRESENTATION_LETTERBOX,
		"overlay layout, input and drawing restore the game's presentation");
	{
	touch_controls::screen_space screen(ren);
	SDL_Surface *shot = SDL_RenderReadPixels(ren, nullptr);
	SDL_Surface *rgb = shot ? SDL_ConvertSurface(shot, SDL_PIXELFORMAT_ARGB8888) : nullptr;
	if (!rgb) { fprintf(stderr, "reading the pixels: %s\n", SDL_GetError()); return 1; }
	std::vector<uint32_t> px(size_t(ww) * wh);
	for (int y = 0; y < wh; y++)
		memcpy(&px[size_t(y) * ww], static_cast<uint8_t *>(rgb->pixels) + size_t(y) * rgb->pitch, size_t(ww) * 4);
	if (!write_png(out, px.data(), ww, wh, true)) { fprintf(stderr, "%s: couldn't be written\n", out); return 1; }
	printf("wrote %s (left, G+P and Start held)\n", out);
	SDL_DestroySurface(rgb);
	SDL_DestroySurface(shot);
	}
	SDL_Quit();
	if (fails) { printf("%d check(s) failed\n", fails); return 1; }
	printf("all checks passed\n");
	return 0;
}
