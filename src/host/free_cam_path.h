// The free camera flown along a path from a file (tests and footage: VF2_TEST_FREECAM_PATH; nothing of this reaches
// players). The file's format is the same in the three games (VF3's free_cam_path.h, ported; its times and distances in
// the game's own units):
//
//   # a comment
//   notoast                       no "Free camera: ..." notice as it turns on (a recording)
//   aim <up>                      "lookat fighters" aims this far above the fighters' middle (default 0)
//   at <T> on|off|pause|unpause|quit
//   key <T> <where> <look>        the camera at time T: there, looking so
//       <where>:  game                       where the game's own camera is at that moment
//                 rel <radius> <az> <height>  round the fighters' middle: that far out (level), az degrees round
//                                             the world's up from where the game's camera was when the free camera
//                                             started (positive: round to its right), that far above the middle
//                 pos <x> <y> <z>             a place in the world (VF2: the anchor's view's axes, see below)
//       <look>:   lookat game                 as the game's camera looks at that moment
//                 lookat fighters             at the fighters' middle
//                 look <yaw> <pitch>          degrees: yaw from the game camera's heading when the free camera
//                                             started (positive right), pitch up from level
//   orbit <T0> <T1> through <deg> [radius <r>] [height <h>]
//                                 from where the camera is at T0, round the fighters' middle through that angle by
//                                 T1 (eased in and out), going to that radius and height on the way (else kept),
//                                 looking at the fighters; the camera is then there, as a key at T1
//
// A time T is on the test hooks' clock: game frames while the game runs, then a tick for each picture while it is
// stopped (paused), so a paused game's camera goes on moving; "+N" is N after the line before's time. Between keys the
// place goes smoothly (a Catmull-Rom spline through the keys in radius, angle and height about the fighters' middle,
// at rest at the first and last key and at an orbit's ends), the look eased from one key's to the next's the short
// way round. Before the first key the camera is where it was; after the last it stays.
// VF2: there is no camera of the game's to read, and so no world's axes: the free camera's world is the game camera's
// view as the free camera came on (x right, y up, z ahead; model2_video's anchor), kept still in the world by the
// stage's pieces that sat still (the game's camera's cuts included). `pos` is in it. The fighters' middle: each side's
// fighter's models (the board's freecam_fighters: its record's parts, by their polygon ROM addresses) found in the
// frame's list, each of x, y and z the middle one of their places; followed smoothly (about a quarter of a second
// behind), so a jump or a throw doesn't shake the camera.
#pragma once
#include "../machine/model2_video.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace freecam {

constexpr double PI = 3.14159265358979323846, DEG = PI / 180;
using State = model2_video::freecam_pilot_state;

struct Path
{
	struct Key
	{
		double t = 0;
		int where = 0;            // 0 game, 1 rel, 2 pos
		double a[3] = {};         // rel: radius, az (radians), height; pos: x, y, z
		int look = 0;             // 0 game, 1 fighters, 2 angles
		double yaw = 0, pitch = 0;
	};
	struct Orbit { double t0 = 0, t1 = 0; double through = 0; bool has_r = false, has_h = false; double r = 0, h = 0; bool started = false; double from[3] = {}, look0[2] = {}; };
	struct Event { double t = 0; std::string what; bool done = false; };
	std::vector<Key> keys;
	std::vector<Orbit> orbits;
	std::vector<Event> events;
	bool no_toast = false, loaded = false;
	double aim = 0;
	// (the fighters' middle as followed, in the anchor's view; the clock it was last followed at)
	bool have_mid = false;
	double mid[3] = {};
	double mid_clock = 0;
	std::string error;

	bool load(const char *file)
	{
		FILE *f = fopen(file, "r");
		if (!f) { error = std::string("can't open ") + file; return false; }
		char line[512];
		double last = 0;
		int n = 0;
		auto time = [&](const char *s) { const double v = *s == '+' ? last + atof(s + 1) : atof(s); last = v; return v; };
		while (fgets(line, sizeof line, f)) {
			n++;
			if (char *c = strchr(line, '#')) *c = 0;
			std::vector<std::string> w;
			for (char *tok = strtok(line, " \t\r\n"); tok; tok = strtok(nullptr, " \t\r\n")) w.push_back(tok);
			if (w.empty()) continue;
			auto bad = [&] { error = std::string(file) + " line " + std::to_string(n) + ": not understood"; fclose(f); return false; };
			auto num = [&](size_t i) { return i < w.size() ? atof(w[i].c_str()) : 0.0; };
			if (w[0] == "notoast") no_toast = true;
			else if (w[0] == "aim" && w.size() == 2) aim = num(1);
			else if (w[0] == "at" && w.size() == 3) events.push_back({ time(w[1].c_str()), w[2], false });
			else if (w[0] == "key" && w.size() >= 3) {
				Key k;
				k.t = time(w[1].c_str());
				size_t i = 2;
				if (w[i] == "game") { k.where = 0; i += 1; }
				else if (w[i] == "rel" && w.size() >= i + 4) { k.where = 1; k.a[0] = num(i + 1); k.a[1] = num(i + 2) * DEG; k.a[2] = num(i + 3); i += 4; }
				else if (w[i] == "pos" && w.size() >= i + 4) { k.where = 2; for (int c = 0; c < 3; c++) k.a[c] = num(i + 1 + size_t(c)); i += 4; }
				else return bad();
				if (i + 1 < w.size() && w[i] == "lookat" && w[i + 1] == "game") k.look = 0;
				else if (i + 1 < w.size() && w[i] == "lookat" && w[i + 1] == "fighters") k.look = 1;
				else if (i + 2 < w.size() && w[i] == "look") { k.look = 2; k.yaw = num(i + 1) * DEG; k.pitch = num(i + 2) * DEG; }
				else return bad();
				keys.push_back(k);
			}
			else if (w[0] == "orbit" && w.size() >= 5 && w[3] == "through") {
				Orbit o;
				o.t0 = time(w[1].c_str());
				o.t1 = time(w[2].c_str());
				o.through = num(4) * DEG;
				for (size_t i = 5; i + 1 < w.size(); i += 2) {
					if (w[i] == "radius") { o.has_r = true; o.r = num(i + 1); }
					else if (w[i] == "height") { o.has_h = true; o.h = num(i + 1); }
					else return bad();
				}
				if (!(o.t1 > o.t0)) return bad();
				orbits.push_back(o);
			}
			else return bad();
		}
		fclose(f);
		loaded = true;
		return true;
	}

	static double dot(const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
	static double ease(double u) { u = std::clamp(u, 0.0, 1.0); return u * u * u * (u * (u * 6 - 15) + 10); }   // (at rest at both ends, no jerk)
	static void polar(const State &c, const double *p, const double *m, double out[3])
	{
		const double v[3] = { p[0] - m[0], p[1] - m[1], p[2] - m[2] };
		const double h = dot(v, c.u);
		const double lv[3] = { v[0] - h * c.u[0], v[1] - h * c.u[1], v[2] - h * c.u[2] };
		out[0] = std::sqrt(dot(lv, lv));
		out[1] = std::atan2(dot(lv, c.s0), -dot(lv, c.h0));
		out[2] = h;
	}
	static void place(const State &c, const double q[3], const double *m, double p[3])
	{
		const double cs = std::cos(q[1]), sn = std::sin(q[1]);
		for (int k = 0; k < 3; k++) p[k] = m[k] + q[0] * (-cs * c.h0[k] + sn * c.s0[k]) + q[2] * c.u[k];
	}
	static void angles(const State &c, const double *dir, double &yaw, double &pitch)
	{
		double d[3] = { dir[0], dir[1], dir[2] };
		const double n = std::sqrt(dot(d, d));
		if (!(n > 0)) { yaw = c.yaw; pitch = c.pitch; return; }
		for (double &x : d) x /= n;
		pitch = std::asin(std::clamp(dot(d, c.u), -1.0, 1.0));
		yaw = std::atan2(dot(d, c.s0), dot(d, c.h0));
	}

	// The camera's place and look at `clock`, into c
	void fly(State &c, double clock)
	{
		if (keys.empty() && orbits.empty()) return;
		// the fighters' middle, followed smoothly
		if (c.mid_ok) {
			const double a = have_mid ? 1 - std::exp(-std::max(0.0, clock - mid_clock) / 14.0) : 1.0;
			for (int k = 0; k < 3; k++) mid[k] += (c.mid[k] - mid[k]) * a;
			have_mid = true;
		}
		mid_clock = clock;
		if (!have_mid) return;
		// the game's camera now
		const double *gp = c.gp;
		double gyaw, gpitch;
		angles(c, c.gf, gyaw, gpitch);
		const double target[3] = { mid[0] + aim * c.u[0], mid[1] + aim * c.u[1], mid[2] + aim * c.u[2] };
		// (VF2_TEST_FREECAM_PATH_LOG: every 30 ticks, where the game's camera is round the fighters' middle, for writing a
		// path: its radius, angle and height as a "rel" key's, and whether the fighters were found)
		if (static const bool log = getenv("VF2_TEST_FREECAM_PATH_LOG") != nullptr; log) {
			static double said = -1e9;
			if (clock - said >= 30 || clock < said) {
				said = clock;
				double gq[3];
				polar(c, gp, mid, gq);
				fprintf(stderr, "(path) %.1f: the game's camera rel %.3f %.1f %.3f, looking %.1f %.1f; the fighters %s, their middle %.3f %.3f %.3f\n",
				        clock, gq[0], gq[1] / DEG, gq[2], gyaw / DEG, gpitch / DEG, c.mid_ok ? "found" : "not found", mid[0], mid[1], mid[2]);
			}
		}
		auto look_of = [&](const Key &k, const double *p, double &yaw, double &pitch) {
			if (k.look == 0) { yaw = gyaw; pitch = gpitch; }
			else if (k.look == 2) { yaw = k.yaw; pitch = k.pitch; }
			else { const double d[3] = { target[0] - p[0], target[1] - p[1], target[2] - p[2] }; angles(c, d, yaw, pitch); }
		};
		auto polar_of = [&](const Key &k, double q[3]) {
			if (k.where == 1) { q[0] = k.a[0]; q[1] = k.a[1]; q[2] = k.a[2]; return; }
			double p[3];
			if (k.where == 0) { p[0] = gp[0]; p[1] = gp[1]; p[2] = gp[2]; }
			else for (int i = 0; i < 3; i++) p[i] = k.a[i];
			polar(c, p, mid, q);
		};
		// the keys in time's order, an orbit's end among them once it has started (its keys in between left out)
		std::vector<Key> ks;
		for (const Key &k : keys) {
			bool inside = false;
			for (const Orbit &o : orbits) inside |= k.t > o.t0 && k.t <= o.t1;
			if (!inside) ks.push_back(k);
		}
		std::sort(ks.begin(), ks.end(), [](const Key &a, const Key &b) { return a.t < b.t; });
		// the keys' path at t: place (polar) and look
		auto along = [&](const std::vector<Key> &v, double t, double q[3], double &yaw, double &pitch) {
			if (v.empty()) { polar(c, c.p, mid, q); yaw = c.yaw; pitch = c.pitch; return; }
			std::vector<std::array<double, 3>> pq(v.size());
			for (size_t i = 0; i < v.size(); i++) {
				polar_of(v[i], pq[i].data());
				// (the angle on from the key before's: two "rel" keys as far apart as written, 0 to 270 the long way; else
				// the short way round)
				if (i) pq[i][1] = pq[i - 1][1] + (v[i].where == 1 && v[i - 1].where == 1 ? v[i].a[1] - v[i - 1].a[1] : std::remainder(pq[i][1] - pq[i - 1][1], 2 * PI));
			}
			size_t i = 0;
			while (i + 1 < v.size() && t >= v[i + 1].t) i++;
			double p[3];
			if (t <= v[0].t || i + 1 >= v.size()) {
				const size_t e = t <= v[0].t ? 0 : v.size() - 1;
				for (int k = 0; k < 3; k++) q[k] = pq[e][size_t(k)];
				place(c, q, mid, p);
				look_of(v[e], p, yaw, pitch);
				return;
			}
			const double t0 = v[i].t, t1 = v[i + 1].t;
			const double s = (t - t0) / (t1 - t0);
			for (int k = 0; k < 3; k++) {
				// (the slopes: through the keys either side, none at the path's ends)
				const size_t kk = size_t(k);
				const double m0 = i > 0 ? (pq[i + 1][kk] - pq[i - 1][kk]) / (v[i + 1].t - v[i - 1].t) * (t1 - t0) : 0.0;
				const double m1 = i + 2 < v.size() ? (pq[i + 2][kk] - pq[i][kk]) / (v[i + 2].t - v[i].t) * (t1 - t0) : 0.0;
				const double s2 = s * s, s3 = s2 * s;
				q[k] = (2 * s3 - 3 * s2 + 1) * pq[i][kk] + (s3 - 2 * s2 + s) * m0 + (-2 * s3 + 3 * s2) * pq[i + 1][kk] + (s3 - s2) * m1;
			}
			q[0] = std::max(q[0], 0.0);
			place(c, q, mid, p);
			double y0, p0, y1, p1;
			look_of(v[i], p, y0, p0);
			look_of(v[i + 1], p, y1, p1);
			const double w = ease(s);
			yaw = y0 + std::remainder(y1 - y0, 2 * PI) * w;
			pitch = p0 + (p1 - p0) * w;
		};
		double q[3], yaw, pitch;
		// an orbit under way, or the last one done (the camera then at its end, a key there for what follows)
		Orbit *now = nullptr;
		for (Orbit &o : orbits) if (clock >= o.t0 && (!now || o.t0 > now->t0)) now = &o;
		if (now) {
			Orbit &o = *now;
			if (!o.started) {
				std::vector<Key> before;
				for (const Key &k : ks) if (k.t <= o.t0) before.push_back(k);
				along(before, o.t0, o.from, o.look0[0], o.look0[1]);
				if (before.empty()) { polar(c, c.p, mid, o.from); o.look0[0] = c.yaw; o.look0[1] = c.pitch; }
				o.started = true;
			}
			const double end[3] = { o.has_r ? o.r : o.from[0], o.from[1] + o.through, o.has_h ? o.h : o.from[2] };
			std::vector<Key> after;
			for (const Key &k : ks) if (k.t > o.t1) after.push_back(k);
			if (clock >= o.t1 && !after.empty()) {
				Key e;
				e.t = o.t1; e.where = 1; e.look = 1;
				for (int k = 0; k < 3; k++) e.a[k] = end[k];
				after.insert(after.begin(), e);
				along(after, clock, q, yaw, pitch);
			} else {
				const double u = (std::min(clock, o.t1) - o.t0) / (o.t1 - o.t0), e = ease(u);
				for (int k = 0; k < 3; k++) q[k] = o.from[k] + (end[k] - o.from[k]) * e;
				double p[3];
				place(c, q, mid, p);
				const double d[3] = { target[0] - p[0], target[1] - p[1], target[2] - p[2] };
				double fy, fp;
				angles(c, d, fy, fp);
				const double w = ease(u / 0.25);   // (from the look it had to the fighters, over its first quarter)
				yaw = o.look0[0] + std::remainder(fy - o.look0[0], 2 * PI) * w;
				pitch = o.look0[1] + (fp - o.look0[1]) * w;
			}
		} else {
			if (ks.empty() || clock < ks[0].t) return;   // (before the first key: where it was)
			// (an orbit still to come: the keys up to its start only, the camera waiting at the last of them)
			double next = 1e300;
			for (const Orbit &o : orbits) if (o.t0 > clock) next = std::min(next, o.t0);
			std::vector<Key> till;
			for (const Key &k : ks) if (k.t <= next) till.push_back(k);
			along(till, clock, q, yaw, pitch);
		}
		place(c, q, mid, c.p);
		c.yaw = yaw;
		c.pitch = std::clamp(pitch, -89 * DEG, 89 * DEG);
	}
};

} // namespace freecam
