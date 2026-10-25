// The on-screen controls (src/host/touch_controls.h) without a game or a ROM: SDL's own finger
// events pressed, held, moved and lifted over the overlay's boxes, and the overlay drawn offscreen
// to a PNG to look at.
//
//   touchcheck [WIDTH] [HEIGHT] [PNG]
//   e.g. touchcheck 2400 1080 build/touch_controls.png     (a 20:9 phone, the default)
//        SDL_VIDEODRIVER=offscreen touchcheck              (without a display)
//
// The overlay is laid out in the renderer's logical coordinates (the game's own 384-line picture),
// so the check sets the same kind of logical presentation the game does and asks the same two
// things the host does: where a touch lands (SDL_RenderCoordinatesFromWindow) and what it holds.
// The behaviours checked are the ones the host relies on: a finger on a box holds it, a lift, a
// move off it and a canceled touch let go, two fingers on one box are one press, and Start and
// Menu come once (the one-frame edges the host takes).
#include "../src/host/touch_controls.h"
#include "../src/host/png.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static SDL_Renderer *ren;
static SDL_Window *win;
static touch_controls::box bx[touch_controls::ACTIONS];
static const char *names[] = { "up", "down", "left", "right", "guard", "punch", "kick", "Start", "Menu" };

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

int main(int argc, char **argv)
{
	const int ww = argc > 1 ? atoi(argv[1]) : 2400;
	const int wh = argc > 2 ? atoi(argv[2]) : 1080;
	const char *out = argc > 3 ? argv[3] : "build/touch_controls.png";
	if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
	win = SDL_CreateWindow("touchcheck", ww, wh, SDL_WINDOW_HIDDEN);
	ren = win ? SDL_CreateRenderer(win, nullptr) : nullptr;
	if (!ren) { fprintf(stderr, "no renderer: %s\n", SDL_GetError()); return 1; }
	// the game's picture is 384 lines tall and as wide as the shape it's shown at
	SDL_SetRenderLogicalPresentation(ren, int(384.0 * double(ww) / double(wh) + 0.5), 384, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	printf("%dx%d, %s\n", ww, wh, SDL_GetRendererName(ren));

	touch_controls tc;
	const int n = touch_controls::boxes(ren, bx);
	check(n == touch_controls::ACTIONS, "a box for each control");
	int vw = 0, vh = 0;
	window_size(vw, vh);
	for (int i = 0; i < n; i++) {
		float wx = 0, wy = 0;
		SDL_RenderCoordinatesToWindow(ren, bx[i].r.x + bx[i].r.w / 2, bx[i].r.y + bx[i].r.h / 2, &wx, &wy);
		const bool inside = wx > 0 && wx < vw && wy > 0 && wy < vh;
		printf("  %-5s logical %6.1f,%6.1f %5.1fx%-5.1f  on screen %6.1f,%6.1f%s\n",
			names[bx[i].action], bx[i].r.x, bx[i].r.y, bx[i].r.w, bx[i].r.h, wx, wy, inside ? "" : "  OFF SCREEN");
		if (!inside) fails++;
	}

	// Android's finger IDs start at 0: the first one must not be mistaken for a free slot
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 0, touch_controls::LEFT), true);
	check(tc.held(touch_controls::LEFT) && !tc.held(touch_controls::RIGHT), "finger 0 on left: left held");
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 1, touch_controls::PUNCH), true);
	check(tc.held(touch_controls::PUNCH) && tc.held(touch_controls::LEFT), "a second finger on punch: both held");
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_UP, 1, touch_controls::PUNCH), false);
	check(!tc.held(touch_controls::PUNCH) && tc.held(touch_controls::LEFT), "punch's lift: punch free, left held");

	// a finger that slides from one button to another, and off the overlay
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_MOTION, 0, touch_controls::KICK), true);
	check(!tc.held(touch_controls::LEFT) && tc.held(touch_controls::KICK), "the same finger moved onto kick");
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_MOTION, 0, -1), true);
	check(!tc.held(touch_controls::LEFT) && !tc.held(touch_controls::KICK), "moved onto the picture: nothing held");
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_UP, 0, -1), false);

	// two fingers on one button: one press, until the last of them lifts
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 2, touch_controls::UP), true);
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 3, touch_controls::UP), true);
	check(tc.held(touch_controls::UP), "two fingers on up: held");
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_UP, 2, touch_controls::UP), false);
	check(tc.held(touch_controls::UP), "one of them lifted: still held");
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_UP, 3, touch_controls::UP), false);
	check(!tc.held(touch_controls::UP), "the last lifted: free");

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

	// draw it with three of them held, and write it out to look at
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 0, touch_controls::LEFT), true);
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 1, touch_controls::PUNCH), true);
	tc.finger(ren, win, ev(SDL_EVENT_FINGER_DOWN, 2, touch_controls::START), true);
	SDL_SetRenderDrawColor(ren, 30, 30, 40, 255);
	SDL_RenderClear(ren);
	tc.draw(ren);
	SDL_Surface *shot = SDL_RenderReadPixels(ren, nullptr);
	SDL_Surface *rgb = shot ? SDL_ConvertSurface(shot, SDL_PIXELFORMAT_ARGB8888) : nullptr;
	if (!rgb) { fprintf(stderr, "reading the pixels: %s\n", SDL_GetError()); return 1; }
	std::vector<uint32_t> px(size_t(ww) * wh);
	for (int y = 0; y < wh; y++)
		memcpy(&px[size_t(y) * ww], static_cast<uint8_t *>(rgb->pixels) + size_t(y) * rgb->pitch, size_t(ww) * 4);
	if (!write_png(out, px.data(), ww, wh, true)) { fprintf(stderr, "%s: couldn't be written\n", out); return 1; }
	printf("wrote %s (left, punch and Start held)\n", out);
	SDL_DestroySurface(rgb);
	SDL_DestroySurface(shot);
	SDL_Quit();
	if (fails) { printf("%d check(s) failed\n", fails); return 1; }
	printf("all checks passed\n");
	return 0;
}
