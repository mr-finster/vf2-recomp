// Smooth motion: a picture for any moment between two game frames, for screens that show more
// pictures than the game's 57.5 a second (a 120 Hz screen shows each frame two or three times,
// unevenly: the motion judders). VF2 sends the geometrizer a new display list every frame: a
// matrix (the camera's view of the object: its place and turn) before each object, from the
// polygon ROM, and the lights and focus. With smooth motion on, each list is kept with the
// geometrizer's state as it began, and for a moment between two frames the newer list is set up
// again with each object's matrix blended with the same object's in the list before (and the
// lights and focus likewise). Nothing is invented: an object only in the newer list, one that
// jumped (a teleport, a new use of the model) and every object at a camera cut is drawn as the
// newer frame has it. The game runs untouched; the picture is a frame behind it.
#include "model2_video.h"
#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace { constexpr size_t KEPT = 6; }   // the newest, the scene sync delay's two before, and a shift's one before those (and one more)

// (the free camera's matrices: as the lists have them, the linear part by columns, 0-2, 3-5, 6-8, then the place)
namespace {
void mul(const double *x, const double *y, double *o)   // o = x * y
{
	double t[12];
	for (int c = 0; c < 3; c++)
		for (int r = 0; r < 3; r++) t[c * 3 + r] = x[r] * y[c * 3] + x[3 + r] * y[c * 3 + 1] + x[6 + r] * y[c * 3 + 2];
	for (int r = 0; r < 3; r++) t[9 + r] = x[r] * y[9] + x[3 + r] * y[10] + x[6 + r] * y[11] + x[9 + r];
	memcpy(o, t, sizeof t);
}
void identity(double *m) { for (int i = 0; i < 12; i++) m[i] = i % 4 == 0 && i < 9 ? 1 : 0; }
void inverse(const double *m, double *o)
{
	const double det = m[0] * (m[4] * m[8] - m[7] * m[5]) - m[3] * (m[1] * m[8] - m[7] * m[2]) + m[6] * (m[1] * m[5] - m[4] * m[2]);
	if (!(std::fabs(det) > 1e-12)) { identity(o); return; }
	double t[12];
	t[0] = (m[4] * m[8] - m[7] * m[5]) / det; t[3] = (m[6] * m[5] - m[3] * m[8]) / det; t[6] = (m[3] * m[7] - m[6] * m[4]) / det;
	t[1] = (m[7] * m[2] - m[1] * m[8]) / det; t[4] = (m[0] * m[8] - m[6] * m[2]) / det; t[7] = (m[6] * m[1] - m[0] * m[7]) / det;
	t[2] = (m[1] * m[5] - m[4] * m[2]) / det; t[5] = (m[3] * m[2] - m[0] * m[5]) / det; t[8] = (m[0] * m[4] - m[3] * m[1]) / det;
	for (int r = 0; r < 3; r++) t[9 + r] = -(t[r] * m[9] + t[3 + r] * m[10] + t[6 + r] * m[11]);
	memcpy(o, t, sizeof t);
}
double dot3(const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void norm3(double *a) { const double n = std::sqrt(dot3(a, a)); if (n > 0) for (int i = 0; i < 3; i++) a[i] /= n; }
constexpr double DEG = 3.14159265358979323846 / 180;
constexpr double FIGHTERS_APART = 8;   // (the free camera's path: two fighters further apart than this (world units) aren't one fight's)
}

// The game's camera at r's frame from the anchor's view (the free camera's world): the frame before's, moved by the
// camera's move between them (what sat still in the world: pair_still); none found, the frame before's (riding along
// with the game's camera); no frame before kept, the newest earlier one worked out (or the anchor's own, none).
const double *model2_video::freecam_cam(smooth_rec &r)
{
	if (r.fc_cam_set) return r.fc_cam;
	// the nearest frame worked out, before this one (the last kept: m_fc_last), else after it
	std::shared_ptr<smooth_rec> q;
	auto nearer = [&](const std::shared_ptr<smooth_rec> &c) {
		if (!c || !c->fc_cam_set || c.get() == &r) return;
		if (!q || (c->frame < r.frame) > (q->frame < r.frame) ||
			((c->frame < r.frame) == (q->frame < r.frame) && (c->frame < r.frame ? c->frame > q->frame : c->frame < q->frame))) q = c;
	};
	nearer(m_fc_last);
	for (const auto &c : m_recs) nearer(c);
	r.fc_cam_set = true;
	if (!q) { identity(r.fc_cam); return r.fc_cam; }
	const bool before = q->frame < r.frame;
	const smooth_rec &pa = before ? *q : r, &pb = before ? r : *q;
	if (m_pair_a != &pa || m_pair_b != &pb) smooth_pair(pa, pb);
	if (m_still_n >= 8) {   // (the camera's move from pa to pb)
		if (before) mul(m_still_D, q->fc_cam, r.fc_cam);
		else { double Di[12]; inverse(m_still_D, Di); mul(Di, q->fc_cam, r.fc_cam); }
	} else memcpy(r.fc_cam, q->fc_cam, sizeof r.fc_cam);   // (none found: riding along with the game's camera)
	for (const auto &c : m_recs) if (c.get() == &r && (!m_fc_last || r.frame > m_fc_last->frame)) m_fc_last = c;
	static const bool log = getenv("VF2_FREECAM_LOG") != nullptr;
	if (log) fprintf(stderr, "freecam: frame %llu from %llu: %d still (of %zu), camera at %.3f %.3f %.3f\n", (unsigned long long)r.frame, (unsigned long long)q->frame,
		m_still_n, r.objects.size(), r.fc_cam[9], r.fc_cam[10], r.fc_cam[11]);
	return r.fc_cam;
}

void model2_video::freecam_start()
{
	for (const auto &q : m_recs) q->fc_cam_set = false;
	m_fc_on = true;
	m_fc_K_ok = false;
	std::shared_ptr<const smooth_rec> shown = shown_list();
	smooth_rec *b = shown ? const_cast<smooth_rec *>(shown.get()) : m_recs.empty() ? nullptr : m_recs.back().get();
	m_fc_up[0] = 0; m_fc_up[1] = 1; m_fc_up[2] = 0;
	m_fc_last.reset();
	for (const auto &q : m_recs) if (q.get() == b) m_fc_last = q;
	if (b) {
		identity(b->fc_cam);
		b->fc_cam_set = true;
		// The world's up in the camera's view: the still pieces' own up (their matrices' second column), the way most
		// of them agree on (a stage's bushes, palms and posts stand up), the camera's roll left out (no jump as it
		// comes on); else the camera's own up
		smooth_rec *p = nullptr;
		for (const auto &q : m_recs) if (q->frame + 1 == b->frame) p = q.get();
		std::vector<std::array<double, 3>> ups;
		if (p) {
			smooth_pair(*p, *b);
			for (size_t k = 0; k < b->objects.size() && k < m_still.size(); k++)
				if (m_still[k]) {
					std::array<double, 3> v = { b->objects[k].m[3], b->objects[k].m[4], b->objects[k].m[5] };
					norm3(v.data());
					if (v[1] < 0) for (double &x : v) x = -x;
					ups.push_back(v);
				}
		}
		int best = -1, best_n = 0;
		for (size_t i = 0; i < ups.size(); i++) {
			int n = 0;
			for (size_t j = 0; j < ups.size(); j++) n += dot3(ups[i].data(), ups[j].data()) > 0.9995;
			if (n > best_n) best_n = n, best = int(i);
		}
		if (best >= 0 && best_n >= 4 && best_n * 4 >= int(ups.size())) {
			double u[3] = {};
			for (const auto &v : ups) if (dot3(ups[best].data(), v.data()) > 0.9995) for (int e = 0; e < 3; e++) u[e] += v[e];
			u[0] = 0;   // (the roll left out)
			norm3(u);
			if (u[1] > 0.5) memcpy(m_fc_up, u, sizeof u);
		}
		static const bool log = getenv("VF2_FREECAM_LOG") != nullptr;
		if (log) fprintf(stderr, "freecam: on at frame %llu: %zu still, %d agree on up %.4f %.4f %.4f\n", (unsigned long long)b->frame, ups.size(), best_n, m_fc_up[0], m_fc_up[1], m_fc_up[2]);
		// a stage's scale: the objects' median depth (most are the fighters' parts), times two: a ring's width or so
		std::vector<float> zs;
		for (const auto &o : b->objects) zs.push_back(std::fabs(o.m[11]));
		if (zs.size() >= 8) {
			std::nth_element(zs.begin(), zs.begin() + zs.size() / 2, zs.end());
			m_fc_unit = std::clamp(2 * zs[zs.size() / 2], 0.5f, 1000.0f);
		}
		if (log) fprintf(stderr, "freecam: unit %.3f\n", m_fc_unit);
	}
	// level ahead and to the right, about the world's up
	const double ez[3] = { 0, 0, 1 }, ex[3] = { 1, 0, 0 };
	for (int e = 0; e < 3; e++) m_fc_fwd0[e] = ez[e] - dot3(ez, m_fc_up) * m_fc_up[e];
	norm3(m_fc_fwd0);
	m_fc_right0[0] = m_fc_up[1] * m_fc_fwd0[2] - m_fc_up[2] * m_fc_fwd0[1];
	m_fc_right0[1] = m_fc_up[2] * m_fc_fwd0[0] - m_fc_up[0] * m_fc_fwd0[2];
	m_fc_right0[2] = m_fc_up[0] * m_fc_fwd0[1] - m_fc_up[1] * m_fc_fwd0[0];
	if (dot3(m_fc_right0, ex) < 0) for (double &x : m_fc_right0) x = -x;
	double K[12];
	identity(K);
	freecam_pose_from(K);
}

void model2_video::freecam_reset()
{
	if (!m_fc_on) return;
	double K[12];
	if (m_fc_K_ok) memcpy(K, m_fc_K, sizeof K); else identity(K);
	freecam_pose_from(K);
}

// The pose that sees as the game's camera K does (its roll left out): its place, and its forward's yaw and pitch
void model2_video::freecam_pose_from(const double *K)
{
	double Ki[12];
	inverse(K, Ki);
	for (int e = 0; e < 3; e++) m_fc_pos[e] = Ki[9 + e];
	double f[3] = { Ki[6], Ki[7], Ki[8] };   // (the view's z in the anchor's view)
	norm3(f);
	m_fc_pitch = std::asin(std::clamp(dot3(f, m_fc_up), -1.0, 1.0)) / DEG;
	m_fc_yaw = std::atan2(dot3(f, m_fc_right0), dot3(f, m_fc_fwd0)) / DEG;
	m_fc_pitch = std::clamp(m_fc_pitch, -89.0, 89.0);
}

// The free camera's pose: its axes (right, up, forward) and place in the anchor's view
void model2_video::freecam_pose(double *P) const
{
	const double cy = std::cos(m_fc_yaw * DEG), sy = std::sin(m_fc_yaw * DEG), cp = std::cos(m_fc_pitch * DEG), sp = std::sin(m_fc_pitch * DEG);
	for (int e = 0; e < 3; e++) {
		const double h = cy * m_fc_fwd0[e] + sy * m_fc_right0[e];
		P[e] = cy * m_fc_right0[e] - sy * m_fc_fwd0[e];   // x: right
		P[3 + e] = cp * m_fc_up[e] - sp * h;                // y: up
		P[6 + e] = cp * h + sp * m_fc_up[e];                // z: forward
		P[9 + e] = m_fc_pos[e];
	}
}

void model2_video::freecam_fly(float fwd, float right, float up, float yaw, float pitch)
{
	if (!m_fc_on) return;
	double P[12];
	freecam_pose(P);
	for (int e = 0; e < 3; e++) m_fc_pos[e] += fwd * P[6 + e] + right * P[e] + up * m_fc_up[e];
	m_fc_yaw = std::remainder(m_fc_yaw + yaw, 360.0);
	m_fc_pitch = std::clamp(m_fc_pitch + pitch, -89.0, 89.0);
	static const bool log = getenv("VF2_FREECAM_LOG") != nullptr;
	if (log) fprintf(stderr, "freecam: at %.3f %.3f %.3f yaw %.1f pitch %.1f (unit %.3f)\n", m_fc_pos[0], m_fc_pos[1], m_fc_pos[2], m_fc_yaw, m_fc_pitch, m_fc_unit);
}

bool model2_video::freecam_sky(double &yaw, double &pitch) const
{
	yaw = pitch = 0;
	if (!m_fc_on || !m_fc_K_ok) return false;
	double Ki[12];
	inverse(m_fc_K, Ki);
	double f[3] = { Ki[6], Ki[7], Ki[8] };
	norm3(f);
	const double gp = std::asin(std::clamp(dot3(f, m_fc_up), -1.0, 1.0)) / DEG, gy = std::atan2(dot3(f, m_fc_right0), dot3(f, m_fc_fwd0)) / DEG;
	yaw = std::remainder(m_fc_yaw - gy, 360.0);
	pitch = m_fc_pitch - gp;
	return true;
}

void model2_video::set_smooth(bool on)
{
	if (on == m_smooth) return;
	m_smooth = on;
	m_recs.clear();
	m_pair_a = m_pair_b = nullptr;
	if (on && !m_geo_r) {
		m_geo_r = std::make_unique<geo_state>();
		m_raster_r = std::make_unique<raster_state>();
		m_raster_r->texture_rom = m_raster->texture_rom;
		m_raster_r->texture_rom_mask = m_raster->texture_rom_mask;
	}
}

void model2_video::smooth_reset()
{
	m_recs.clear();
	m_pair_a = m_pair_b = nullptr;
	m_smooth_floor = m_ram_changed = 0;
	m_fc_last.reset();
	m_fc_K_ok = false;
	m_last_moment = moment{};
	std::fill(std::begin(m_pic_frame), std::end(m_pic_frame), 0);
}

// geo_parse begins: the list and the state it starts from kept, in the oldest record's place
void model2_video::smooth_record_start()
{
	std::shared_ptr<smooth_rec> r;
	if (m_recs.size() >= KEPT) {
		r = std::move(m_recs.front());
		m_recs.erase(m_recs.begin());
		if (m_pair_a == r.get() || m_pair_b == r.get()) m_pair_a = m_pair_b = nullptr;
		if (r.use_count() > 1) r = std::make_shared<smooth_rec>();   // (still held by a moment kept: a new one)
	} else
		r = std::make_shared<smooth_rec>();
	r->frame = m_geo_frame;
	r->start = m_geo_read_start_address;
	r->list.assign(m_bufferram, m_bufferram + 0x20000 / 4);
	r->geo = *m_geo;
	r->geo.raster = nullptr;
	const raster_state &ra = *m_raster;
	memcpy(r->viewport, ra.viewport, sizeof r->viewport);
	r->widened = ra.widened;
	memcpy(r->center, ra.center, sizeof r->center);
	r->center_sel = ra.center_sel;
	r->reverse = ra.reverse;
	r->z_adjust = ra.z_adjust;
	r->master_z_clip = ra.master_z_clip;
	r->cur_command = ra.cur_command;
	memcpy(r->command_buffer, ra.command_buffer, sizeof r->command_buffer);
	r->command_index = ra.command_index;
	for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) r->clip_plane[i][j] = ra.clip_plane[i][j];
	r->texture_ram.assign(std::begin(ra.texture_ram), std::end(ra.texture_ram));
	r->log_ram.assign(std::begin(ra.log_ram), std::end(ra.log_ram));
	r->objects.clear();
	r->lights.clear();
	r->focals.clear();
	r->fc_cam_set = false;
	m_rec = r.get();
	m_recs.push_back(std::move(r));
}

void model2_video::smooth_record_end() { m_rec = nullptr; }

void model2_video::smooth_note_object(u32 oba, const float *matrix)
{
	smooth_rec::object o;
	o.oba = oba;
	memcpy(o.m, matrix, sizeof o.m);
	m_rec->objects.push_back(o);
}

void model2_video::smooth_note_focal(float x, float y) { m_rec->focals.push_back({ x, y }); }
void model2_video::smooth_note_light(float x, float y, float z) { m_rec->lights.push_back({ x, y, z }); }

void model2_video::smooth_cut()
{
	const int shown = shown_now();
	m_smooth_floor = shown >= 0 ? m_pic_frame[shown] : m_recs.empty() ? 0 : m_recs.back()->frame;
}

// The objects of two lists paired in order, like a diff: the same model pairs; a different model
// in the same place (a hand or a face swapped for another) pairs too; objects that went or came
// are skipped. Then the jumps: an object that moved far or turned sharply in a frame. When most
// of them did (a camera cut), or most can't be paired (another scene), nothing is blended.
void model2_video::smooth_pair(const smooth_rec &a, const smooth_rec &b)
{
	m_pair_a = &a;
	m_pair_b = &b;
	const size_t na = a.objects.size(), nb = b.objects.size();
	m_pair.assign(nb, -1);
	size_t i = 0, j = 0;
	m_repaired = 0;
	m_in_changed_run.assign(nb, 0);
	while (i < na && j < nb) {
		if (a.objects[i].oba == b.objects[j].oba) {
			// A run of objects of one model (a stage's bushes, palms): when one of them came or
			// went as the camera turned, pairing them in order would pair a bush with the next one
			// of its kind, and the pictures between the frames would draw it halfway to that one's
			// place (one picture with the scenery jumped about). So the shorter run is paired where
			// it lies nearest in the longer one; runs of the same length pair in order.
			size_t ra = 1, rb = 1;
			while (i + ra < na && a.objects[i + ra].oba == a.objects[i].oba) ra++;
			while (j + rb < nb && b.objects[j + rb].oba == b.objects[j].oba) rb++;
			if (ra == rb) { for (size_t k = 0; k < ra; k++) m_pair[j + k] = int(i + k); i += ra; j += rb; continue; }
			for (size_t k = 0; k < rb; k++) m_in_changed_run[j + k] = 1;
			const size_t n = std::min(ra, rb), slack = std::max(ra, rb) - n;
			auto dist = [&](size_t ia, size_t jb) {
				const float *pa = a.objects[ia].m, *pb = b.objects[jb].m;
				return (pb[9] - pa[9]) * (pb[9] - pa[9]) + (pb[10] - pa[10]) * (pb[10] - pa[10]) + (pb[11] - pa[11]) * (pb[11] - pa[11]);
			};
			size_t best = 0;
			double best_d = -1;
			for (size_t o = 0; o <= slack && o <= 64; o++) {
				double d = 0;
				for (size_t k = 0; k < n; k++) d += ra > rb ? dist(i + o + k, j + k) : dist(i + k, j + o + k);
				if (best_d < 0 || d < best_d) best_d = d, best = o;
			}
			for (size_t k = 0; k < n; k++) {
				if (ra > rb) m_pair[j + k] = int(i + best + k);
				else m_pair[j + best + k] = int(i + k);
			}
			if (best) m_repaired += int(n);
			i += ra; j += rb;
			continue;
		}
		size_t ka = 1, kb = 1;
		while (i + ka < na && ka <= 8 && a.objects[i + ka].oba != b.objects[j].oba) ka++;
		while (j + kb < nb && kb <= 8 && b.objects[j + kb].oba != a.objects[i].oba) kb++;
		const bool a_skip = i + ka < na && ka <= 8, b_skip = j + kb < nb && kb <= 8;
		if (a_skip && (!b_skip || ka <= kb)) i += ka;   // objects in the older list that went
		else if (b_skip) j += kb;                        // objects that are new
		else m_pair[j++] = int(i++);                     // the same place, a different model
	}
	// The pairing in order can leave an object without its model's partner (new, or paired with
	// another model in its slot) when the list's order changed more than it looks ahead (a stage's
	// floor 25 places on), or give it a partner of its model in another place while the one in its
	// own place goes to nothing (Shun's deck: one section the game draws as another model from frame
	// to frame). Either way the pictures between the frames had a piece of floor off or missing. So
	// such an object is paired with the nearest one of its model in the older list, near enough not
	// to be a jump, unless that one is held by another of its model at least four times as far; one
	// still without a partner (or paired away from its place), with one of any model in its very
	// place; and one still without, drawn
	// with the same matrix as one with a partner (the shadow the deck takes under the bridge, drawn
	// over it), is blended as that one is, so the two stay together.
	{
		std::vector<int> holder(na, -1);
		for (size_t k = 0; k < nb; k++) if (m_pair[k] >= 0) holder[m_pair[k]] = int(k);
		std::unordered_map<u32, std::vector<int>> of_model;
		for (size_t q = 0; q < na; q++) of_model[a.objects[q].oba].push_back(int(q));
		auto d2 = [&](int q, size_t k) {
			const float *pa = a.objects[q].m, *pb = b.objects[k].m;
			return (pb[9] - pa[9]) * (pb[9] - pa[9]) + (pb[10] - pa[10]) * (pb[10] - pa[10]) + (pb[11] - pa[11]) * (pb[11] - pa[11]);
		};
		// (places after the camera's move, fitted to the pairs as they are: a stage's pieces sit still
		// in the world while the camera moves; without a fit, as they are)
		double R[3][3], T[3];
		const bool cam = fit_camera(a, b, R, T);
		auto dp = [&](int q, size_t k) {
			const float *pa = a.objects[q].m, *pb = b.objects[k].m;
			if (!cam) return (pb[9] - pa[9]) * (pb[9] - pa[9]) + (pb[10] - pa[10]) * (pb[10] - pa[10]) + (pb[11] - pa[11]) * (pb[11] - pa[11]);
			float o[3];
			for (int r = 0; r < 3; r++) o[r] = float(R[r][0] * pa[9] + R[r][1] * pa[10] + R[r][2] * pa[11] + T[r]);
			return (pb[9] - o[0]) * (pb[9] - o[0]) + (pb[10] - o[1]) * (pb[10] - o[1]) + (pb[11] - o[2]) * (pb[11] - o[2]);
		};
		auto depth2 = [&](int q, size_t k) { const float d = std::max({ std::fabs(a.objects[q].m[11]), std::fabs(b.objects[k].m[11]), 1.0f }); return d * d; };
		// (its very place: after the fitted move, or, when the camera held still (a quarter of the
		// objects, eight at least, where they were within a thousandth of their depth), where it was,
		// whichever is nearer. A fit thrown off by the pairs it's made from moves a camera that held
		// still: Jeffry's beach in a 1P game, its row of bushes each drawn as another's swaying shape
		// a frame before, the order's pairs across the row (and a fighter in the air) fitted as a turn
		// or a shift, and a bush in its very place looked a tenth of a unit or more off it. It kept
		// its model's partner in the next bush's place and the picture between the frames drew it
		// halfway there: a bush jumping aside for a picture, every few seconds. Only for finding one
		// in its very place: whether a partner is away from its place still goes by the fit)
		size_t still_n = 0;
		for (size_t k = 0; k < nb; k++)
			for (size_t q = 0; q < na; q++)
				if (d2(int(q), k) < 1e-6f * depth2(int(q), k)) { still_n++; break; }
		const bool still = still_n >= 8 && still_n * 4 >= nb;
		auto dv = [&](int q, size_t k) { return still ? std::min(dp(q, k), d2(q, k)) : dp(q, k); };
		auto own = [&](size_t k) { return m_pair[k] >= 0 && a.objects[m_pair[k]].oba == b.objects[k].oba; };
		for (size_t k = 0; k < nb; k++) {
			if (own(k)) continue;
			const auto it = of_model.find(b.objects[k].oba);
			if (it == of_model.end()) continue;
			int best = -1;
			float bd = 0;
			for (int q : it->second) {
				const float d = d2(q, k);
				if (d >= 0.09f * depth2(q, k)) continue;
				const int h = holder[q];
				if (h >= 0 && own(size_t(h)) && d * 16 >= d2(q, size_t(h))) continue;
				if (best < 0 || d < bd) best = q, bd = d;
			}
			if (best < 0) continue;
			if (m_pair[k] >= 0) holder[m_pair[k]] = -1;
			if (holder[best] >= 0) m_pair[holder[best]] = -1;
			m_pair[k] = best;
			holder[best] = int(k);
			m_repaired++;
		}
		// (one paired away from its place, over a tenth of its depth, with another model, or with its
		// own while something stands in its very place in the older list (its place after the camera's move): let go of, and looked for
		// in its place like one without. Jeffry's beach: a row of bushes, each drawn as one of its
		// swaying shapes from frame to frame, so a bush's model is the next one's a frame before and
		// the order put each against its neighbour, the pictures between drawing it halfway there)
		auto in_place = [&](size_t k) { for (size_t q = 0; q < na; q++) if (dv(int(q), k) < 2.5e-5f * depth2(int(q), k)) return true; return false; };
		std::vector<int> let_go;
		for (size_t k = 0; k < nb; k++)
			if (m_pair[k] >= 0 && dp(m_pair[k], k) >= 0.01f * depth2(m_pair[k], k) && (!own(k) || in_place(k))) let_go.push_back(int(k));
		for (int k : let_go) { holder[m_pair[k]] = -1; m_pair[k] = -1; }
		for (size_t k = 0; k < nb; k++) {
			if (m_pair[k] != -1) continue;
			int best = -1;
			float bd = 0;
			for (size_t q = 0; q < na; q++)
				if (holder[q] < 0) {
					const float d = dv(int(q), k);
					if (d < 1e-4f * depth2(int(q), k) && (best < 0 || d < bd)) best = int(q), bd = d;
				}
			if (best >= 0) { m_pair[k] = best; holder[best] = int(k); m_repaired++; }
		}
		for (size_t k = 0; k < nb; k++) {
			if (m_pair[k] != -1) continue;
			for (size_t j = 0; j < nb; j++)
				if (m_pair[j] >= 0 && !memcmp(b.objects[j].m, b.objects[k].m, sizeof b.objects[k].m)) { m_pair[k] = m_pair[j]; break; }
		}
	}
	repair_by_camera(a, b);
	std::vector<char> still;
	pair_still(a, b, still);
	m_still = still;
	// Then the jumps: an object that moved far (over 30% of its depth) or turned sharply in a frame.
	// What sat still in the world moved only as the camera did, and is blended as the camera's move
	// is, all of it alike, however near the camera (a palm beside it swinging past as the camera
	// turns), unless most of it jumped: the camera itself did (a cut).
	std::vector<char> jump(nb, 0);   // 1 moved, 2 only turned
	int still_n = 0, still_jumps = 0;
	for (size_t k = 0; k < nb; k++) {
		if (m_pair[k] < 0) continue;
		const float *pa = a.objects[m_pair[k]].m, *pb = b.objects[k].m;
		const float dx = pb[9] - pa[9], dy = pb[10] - pa[10], dz = pb[11] - pa[11];
		const float depth = std::max({ std::fabs(pa[11]), std::fabs(pb[11]), 1.0f });
		bool turned = false;   // over about 40 degrees: blending would squash the shape
		for (int r = 0; r < 9 && !turned; r += 3) {
			const float la = pa[r] * pa[r] + pa[r + 1] * pa[r + 1] + pa[r + 2] * pa[r + 2];
			const float lb = pb[r] * pb[r] + pb[r + 1] * pb[r + 1] + pb[r + 2] * pb[r + 2];
			const float dot = pa[r] * pb[r] + pa[r + 1] * pb[r + 1] + pa[r + 2] * pb[r + 2];
			if (la > 0 && lb > 0 && dot < 0.77f * std::sqrt(la * lb)) turned = true;
		}
		const bool moved = dx * dx + dy * dy + dz * dz > 0.09f * depth * depth;
		jump[k] = moved ? 1 : turned ? 2 : 0;
		if (still[k]) still_n++, still_jumps += jump[k] != 0;
	}
	const bool camera_jumped = still_jumps * 2 > still_n;
	int jumps = 0, paired = 0;
	for (size_t k = 0; k < nb; k++) {
		if (m_pair[k] < 0) continue;
		paired++;
		if (!jump[k] || (still[k] && !camera_jumped)) continue;
		jumps++;
		// a part that only turned (a finger as the hand closes) keeps to its joint: its place
		// is blended, its turn is the newer one's; one that moved far is the newer one's
		static const bool log = getenv("VF2_SMOOTH_LOG") != nullptr;
		if (log) fprintf(stderr, "jump frame %llu obj %zu oba %06x moved %d turned %d\n", (unsigned long long)b.frame, k, b.objects[k].oba, int(jump[k] == 1), int(jump[k] == 2));
		m_pair[k] = jump[k] == 1 ? -2 : -3 - m_pair[k];
	}
	m_pair_cut = jumps * 2 > paired || paired * 2 < int(nb);
	m_pair_jumps = jumps;
}

bool model2_video::fit_camera(const smooth_rec &a, const smooth_rec &b, double R[3][3], double T[3]) const
{
	struct pt { double x, y, z; };
	std::vector<pt> P, Q;
	for (size_t k = 0; k < m_pair.size(); k++) {
		const int q = m_pair[k];
		if (q < 0) continue;
		const float *pa = a.objects[q].m, *pb = b.objects[k].m;
		P.push_back({ pa[9], pa[10], pa[11] });
		Q.push_back({ pb[9], pb[10], pb[11] });
	}
	if (P.size() < 8) return false;
	for (int r = 0; r < 3; r++) { for (int c = 0; c < 3; c++) R[r][c] = r == c; T[r] = 0; }
	std::vector<char> use(P.size(), 1);
	auto residual = [&](const pt &p, const pt &q) {
		double o[3];
		for (int r = 0; r < 3; r++) o[r] = R[r][0] * p.x + R[r][1] * p.y + R[r][2] * p.z + T[r];
		const double dx = o[0] - q.x, dy = o[1] - q.y, dz = o[2] - q.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz) / std::max({ std::fabs(p.z), std::fabs(q.z), 1.0 });
	};
	for (int pass = 0; pass < 3; pass++) {
		// the best turn and shift for the objects in use: the orthonormal part of the covariance
		// (Newton's iteration for the polar decomposition), then the shift between the centres
		double cp[3] = {}, cq[3] = {}, H[3][3] = {};
		int n = 0;
		for (size_t i = 0; i < P.size(); i++)
			if (use[i]) { cp[0] += P[i].x; cp[1] += P[i].y; cp[2] += P[i].z; cq[0] += Q[i].x; cq[1] += Q[i].y; cq[2] += Q[i].z; n++; }
		if (n < 4) return false;
		for (int r = 0; r < 3; r++) cp[r] /= n, cq[r] /= n;
		for (size_t i = 0; i < P.size(); i++)
			if (use[i]) {
				const double p[3] = { P[i].x - cp[0], P[i].y - cp[1], P[i].z - cp[2] }, q[3] = { Q[i].x - cq[0], Q[i].y - cq[1], Q[i].z - cq[2] };
				for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) H[r][c] += q[r] * p[c];
			}
		double M[3][3];
		memcpy(M, H, sizeof M);
		for (int it = 0; it < 30; it++) {
			const double det = M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
			if (std::fabs(det) < 1e-30) return false;
			double C[3][3];   // the inverse's transpose: cofactors / det
			C[0][0] = (M[1][1] * M[2][2] - M[1][2] * M[2][1]) / det; C[0][1] = (M[1][2] * M[2][0] - M[1][0] * M[2][2]) / det; C[0][2] = (M[1][0] * M[2][1] - M[1][1] * M[2][0]) / det;
			C[1][0] = (M[0][2] * M[2][1] - M[0][1] * M[2][2]) / det; C[1][1] = (M[0][0] * M[2][2] - M[0][2] * M[2][0]) / det; C[1][2] = (M[0][1] * M[2][0] - M[0][0] * M[2][1]) / det;
			C[2][0] = (M[0][1] * M[1][2] - M[0][2] * M[1][1]) / det; C[2][1] = (M[0][2] * M[1][0] - M[0][0] * M[1][2]) / det; C[2][2] = (M[0][0] * M[1][1] - M[0][1] * M[1][0]) / det;
			for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) M[r][c] = 0.5 * (M[r][c] + C[r][c]);
		}
		memcpy(R, M, sizeof(double) * 9);
		for (int r = 0; r < 3; r++) T[r] = cq[r] - (R[r][0] * cp[0] + R[r][1] * cp[1] + R[r][2] * cp[2]);
		std::vector<double> sorted;
		std::vector<double> res(P.size());
		for (size_t i = 0; i < P.size(); i++) sorted.push_back(res[i] = residual(P[i], Q[i]));
		std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
		const double med = sorted[sorted.size() / 2];
		for (size_t i = 0; i < P.size(); i++) use[i] = res[i] <= std::max(3 * med, 0.002);
	}
	return true;
}

// Each paired object far off the camera's move (over 2% of its depth) in a run of one model that
// changed length (as when a bush of a row comes or goes as the camera turns) is paired instead
// with the object of its model that follows the move, when one does (within a quarter of that and
// 1% of its depth), else drawn where the newer frame has it. Outside such runs nothing changes: a
// fighter's parts of one model (a hand's fingers) move by themselves, not with the camera (as
// vf1-recomp found).
void model2_video::repair_by_camera(const smooth_rec &a, const smooth_rec &b)
{
	double R[3][3], T[3];
	if (!fit_camera(a, b, R, T)) return;
	auto residual = [&](const float *pa, const float *pb) {
		double o[3];
		for (int r = 0; r < 3; r++) o[r] = R[r][0] * pa[9] + R[r][1] * pa[10] + R[r][2] * pa[11] + T[r];
		const double dx = o[0] - pb[9], dy = o[1] - pb[10], dz = o[2] - pb[11];
		return std::sqrt(dx * dx + dy * dy + dz * dz) / std::max({ std::fabs(double(pa[11])), std::fabs(double(pb[11])), 1.0 });
	};
	for (size_t k = 0; k < m_pair.size(); k++) {
		if (m_pair[k] < 0 || !m_in_changed_run[k]) continue;
		const float *pb = b.objects[k].m;
		const double rk = residual(a.objects[m_pair[k]].m, pb);
		if (rk <= 0.02) continue;
		const u32 oba = b.objects[k].oba;
		int best = -1;
		double bres = std::min(rk * 0.25, 0.01);
		for (size_t q = 0; q < a.objects.size(); q++)
			if (a.objects[q].oba == oba && int(q) != m_pair[k]) {
				const double r = residual(a.objects[q].m, pb);
				if (r < bres) bres = r, best = int(q);
			}
		if (best >= 0) m_pair[k] = best, m_repaired++;
		else if (rk > 0.06) m_pair[k] = -2, m_repaired++;   // none follows it: another of the run that came into the list, drawn where the newer frame has it
	}
}

// The objects that sat still in the world while the camera moved, paired by their whole matrices
// (place and turn) rather than their models and order. Each object's matrix is the camera's view of
// it, so from one frame to the next everything that stayed where it was in the world is moved by
// the same matrix: the camera's move, D = M' M^-1 from any such pair. Each pair of one model that
// the order made gives a candidate; the one the most of those pairs follow (eight at least) is the
// camera's. Then an object of the newer list that some object of the older list becomes under D,
// within 1/10000 of its depth (exact but for rounding), is paired with that one, of whatever
// model: it's the same thing in the same place, or one just like it there, and the picture
// between the frames draws it where it is in the world. (Jeffry's beach: a row of bushes and the
// palms' leaves, each drawn as one of its swaying shapes from frame to frame, so a bush's model is
// another bush's a frame before; by model and order, and by places fitted to such pairs, a bush
// was paired with its neighbour and drawn halfway there, or with none and left behind as the
// camera moved: the bushes flickered.) The pairs the order, places and models made stand where
// they're already such a pair; objects that moved in the world (the fighters) are left to them.
void model2_video::pair_still(const smooth_rec &a, const smooth_rec &b, std::vector<char> &still)
{
	const size_t na = a.objects.size(), nb = b.objects.size();
	still.assign(nb, 0);
	m_still_n = 0;
	if (na < 8 || nb < 8) return;
	auto to = [](const float *m, double *o) { for (int i = 0; i < 12; i++) o[i] = m[i]; };
	// o = x * y (the linear parts by columns, as the list has them: 0-2, 3-5, 6-8; then the place)
	auto mul = [](const double *x, const double *y, double *o) {
		for (int c = 0; c < 3; c++)
			for (int r = 0; r < 3; r++) o[c * 3 + r] = x[r] * y[c * 3] + x[3 + r] * y[c * 3 + 1] + x[6 + r] * y[c * 3 + 2];
		for (int r = 0; r < 3; r++) o[9 + r] = x[r] * y[9] + x[3 + r] * y[10] + x[6 + r] * y[11] + x[9 + r];
	};
	std::vector<std::array<double, 12>> ma(na), mb(nb), inv_a(na);
	std::vector<char> inv_ok(na, 0);
	for (size_t q = 0; q < na; q++) {
		double *m = ma[q].data(), *o = inv_a[q].data();
		to(a.objects[q].m, m);
		const double det = m[0] * (m[4] * m[8] - m[7] * m[5]) - m[3] * (m[1] * m[8] - m[7] * m[2]) + m[6] * (m[1] * m[5] - m[4] * m[2]);
		if (!(std::fabs(det) > 1e-12)) continue;
		// the inverse's linear part: the cofactors over the determinant (transposed); its place
		o[0] = (m[4] * m[8] - m[7] * m[5]) / det; o[3] = (m[6] * m[5] - m[3] * m[8]) / det; o[6] = (m[3] * m[7] - m[6] * m[4]) / det;
		o[1] = (m[7] * m[2] - m[1] * m[8]) / det; o[4] = (m[0] * m[8] - m[6] * m[2]) / det; o[7] = (m[6] * m[1] - m[0] * m[7]) / det;
		o[2] = (m[1] * m[5] - m[4] * m[2]) / det; o[5] = (m[3] * m[2] - m[0] * m[5]) / det; o[8] = (m[0] * m[4] - m[3] * m[1]) / det;
		for (int r = 0; r < 3; r++) o[9 + r] = -(o[r] * m[9] + o[3 + r] * m[10] + o[6 + r] * m[11]);
		inv_ok[q] = 1;
	}
	for (size_t k = 0; k < nb; k++) to(b.objects[k].m, mb[k].data());
	// how far D takes the older one from the newer: its place over its depth, its turn over its size
	auto off = [&](const double *D, size_t q, size_t k) {
		double o[12];
		mul(D, ma[q].data(), o);
		const double *n = mb[k].data();
		double dl = 0, nl = 0;
		for (int i = 0; i < 9; i++) { dl = std::max(dl, std::fabs(o[i] - n[i])); nl = std::max(nl, std::fabs(n[i])); }
		const double dx = o[9] - n[9], dy = o[10] - n[10], dz = o[11] - n[11];
		return std::max(std::sqrt(dx * dx + dy * dy + dz * dz) / std::max(std::fabs(n[11]), 1.0), nl > 0 ? dl / nl : 1.0);
	};
	constexpr double TOL = 1e-4;
	double D[12], best[12];
	int best_n = 0;
	std::vector<char> tried(nb, 0);
	for (size_t k = 0; k < nb; k++) {
		const int q = m_pair[k];
		if (q < 0 || tried[k] || !inv_ok[q] || a.objects[q].oba != b.objects[k].oba) continue;
		mul(mb[k].data(), inv_a[q].data(), D);
		int n = 0;
		for (size_t j = 0; j < nb; j++) {
			const int p = m_pair[j];
			if (p >= 0 && a.objects[p].oba == b.objects[j].oba && off(D, size_t(p), j) < TOL) { n++; tried[j] = 1; }
		}
		if (n > best_n) { best_n = n; memcpy(best, D, sizeof D); }
	}
	if (best_n < 8) return;
	m_still_n = best_n;
	memcpy(m_still_D, best, sizeof best);
	// the older objects' places after the move (a quick look before the whole matrix)
	std::vector<std::array<double, 3>> moved(na);
	for (size_t q = 0; q < na; q++) {
		const double *m = ma[q].data();
		for (int r = 0; r < 3; r++) moved[q][r] = best[r] * m[9] + best[3 + r] * m[10] + best[6 + r] * m[11] + best[9 + r];
	}
	for (size_t k = 0; k < nb; k++) {
		const double *n = mb[k].data();
		const double reach = TOL * std::max(std::fabs(n[11]), 1.0);
		const int cur = m_pair[k];
		int pick = -1;
		double pick_off = 0;
		bool pick_own = false;
		for (size_t q = 0; q < na; q++) {
			if (std::fabs(moved[q][0] - n[9]) > reach || std::fabs(moved[q][1] - n[10]) > reach || std::fabs(moved[q][2] - n[11]) > reach) continue;
			const double d = off(best, q, k);
			if (d >= TOL) continue;
			if (int(q) == cur) { pick = cur; break; }   // (its pair is such a one: it stands)
			const bool own = a.objects[q].oba == b.objects[k].oba;
			if (pick < 0 || (own && !pick_own) || (own == pick_own && d < pick_off)) pick = int(q), pick_off = d, pick_own = own;
		}
		if (pick < 0) continue;
		still[k] = 1;
		if (pick != cur) { m_pair[k] = pick; m_repaired++; }
	}
}

bool model2_video::newest_cut()
{
	if (m_recs.size() < 2) return false;
	const auto &b = m_recs.back(), &a = m_recs[m_recs.size() - 2];
	if (a->frame + 1 != b->frame) return false;
	if (m_pair_a != a.get() || m_pair_b != b.get()) smooth_pair(*a, *b);
	return m_pair_cut;
}

std::shared_ptr<const model2_video::smooth_rec> model2_video::shown_list() const
{
	const int shown = shown_now();
	if (shown < 0) return nullptr;
	for (const auto &r : m_recs) if (r->frame == m_pic_frame[shown]) return r;
	return nullptr;
}

int model2_video::smooth_layer(u64 frame, float alpha, const bitmap_rgb32 *&map)
{
	map = nullptr;
	(void)frame;
	m_last_moment = moment{};
	const int shown = shown_now();
	m_smooth_stats[0] = -4; m_smooth_stats[1] = m_smooth_stats[2] = 0;
	if (!m_smooth || m_render_test_mode || shown < 0) return -1;
	u64 fb = m_pic_frame[shown];   // the list of the 3D shown now
	if (m_gradual && !m_recs.empty()) {
		// a delay changed gradually (set_delay without a cut, set_shift): the moment from the
		// newest frame back, the delay and its fraction, between frames kb - 1 and kb (not from
		// the picture the whole frames of the delay show)
		const double t = double(m_recs.back()->frame) - 1 + alpha - (m_delay + m_shift);
		const double kb = std::ceil(t - 1e-6);
		for (const auto &r : m_recs)
			if (double(r->frame) == kb) { fb = r->frame; alpha = std::clamp(float(t - (kb - 1)), 0.0f, 1.0f); break; }
	}
	m_smooth_fb = fb;
	std::shared_ptr<smooth_rec> a, b;
	for (const auto &r : m_recs) {
		if (r->frame == fb) b = r;
		if (r->frame + 1 == fb && r->frame >= m_smooth_floor) a = r;
	}
	// none if the RAM the polygons are coloured from changed in a way not kept
	if (!b || (m_ram_changed > fb && !m_fc_on)) {   // (the free camera's picture comes from the lists whatever the colours)
		m_smooth_stats[0] = !b ? -4 : -3;   // (-3: the colours changed; -4: no list kept)
		return -1;
	}
	moment mo;
	mo.b = b;
	mo.alpha = alpha;
	if (a && alpha < 1) {
		if (m_pair_a != a.get() || m_pair_b != b.get()) smooth_pair(*a, *b);
		if (!m_pair_cut) mo.a = a;
		m_smooth_stats[1] = m_pair_jumps;
		m_smooth_stats[0] = m_pair_cut ? -1 : 0;   // (-1: a cut)
	} else if (!a) m_smooth_stats[0] = -2;          // (-2: no frame before to blend from)
	if (!mo.a && alpha < 1 && m_gradual) {
		// a moment with nothing to blend from (a new scene's or shot's first frame): rather than
		// show that frame now and again at the next moment (a picture repeated), the delay gives
		// up the rest of the way to it, which nobody has seen (it's a new shot): the moments go on
		// from there with no picture shown twice.
		const float adv = 1 - alpha, total = float(m_delay) + m_shift;
		if (adv <= total + 1e-6f) {
			const float left = std::max(0.0f, total - adv);
			m_delay = std::min(2, int(left + 1e-4f));
			m_shift = std::clamp(left - float(m_delay), 0.0f, 1.0f);
		}
	}
	if (!mo.a) mo.alpha = 1;
	m_last_moment = mo;
	return replay_layer(mo, map);
}

int model2_video::replay_layer(const moment &mo, const bitmap_rgb32 *&map)
{
	map = nullptr;
	if (!mo.b || !m_geo_r) return -1;
	picture3d &pic = m_pictures[3];
	if (pic.map.width() != m_renderer->destmap().width() || pic.map.height() != m_renderer->destmap().height())
		model2_renderer::allocate(pic.map, m_scale, m_ext);
	replay_into(mo, pic.drawn);
	pic.deferred = false;
	map = &pic.map;
	return 3;
}

bool model2_video::rebuild_picture(int picture)
{
	if (picture < 0 || picture > 2) return false;
	std::shared_ptr<smooth_rec> b;
	for (const auto &r : m_recs) if (r->frame == m_pic_frame[picture]) b = r;
	if (!b || !m_geo_r) { m_pictures[picture].drawn.clear(); return false; }
	moment mo;
	mo.b = b;
	replay_into(mo, m_pictures[picture].drawn);
	m_pictures[picture].deferred = false;
	return true;
}

void model2_video::replay_into(const moment &mo, std::vector<drawn_poly> &drawn)
{
	const smooth_rec *a = mo.a.get(), *b = mo.b.get();
	const float alpha = mo.alpha;
	// the free camera: the game's camera at both frames (first: working it out pairs other frames)
	double ka[12], kb[12];
	const bool fc = m_fc_on && m_fc_scene;
	if (fc) {
		memcpy(kb, freecam_cam(const_cast<smooth_rec &>(*b)), sizeof kb);
		if (a) memcpy(ka, freecam_cam(const_cast<smooth_rec &>(*a)), sizeof ka);
	}
	// the blend for this moment
	const size_t nb = b->objects.size();
	m_blend.resize(nb);
	m_blend_ok.assign(nb, 0);
	m_light_blend.clear();
	m_focal_blend.clear();
	if (a && alpha < 1) {
		if (m_pair_a != a || m_pair_b != b) smooth_pair(*a, *b);
		if (!m_pair_cut) {
			m_smooth_stats[2] = 0;
			for (size_t k = 0; k < nb; k++) {
				const int pk = m_pair[k];
				if (pk == -1 || pk == -2) { m_smooth_stats[0] += pk == -1; continue; }
				const bool turn_only = pk <= -3;
				const float *pa = a->objects[turn_only ? -3 - pk : pk].m, *pb = b->objects[k].m;
				for (int e = 0; e < 12; e++) m_blend[k][e] = turn_only && e < 9 ? pb[e] : pa[e] + (pb[e] - pa[e]) * alpha;
				m_blend_ok[k] = 1;
				m_smooth_stats[2]++;
			}
			// (a light that changes in kind between the frames isn't blended: the nearer frame's.
			// The game's flashes swap the usual light, about 1 long, for one thousands long for a
			// frame or two: blended, 0.1% of it swamped the other, and the fighters in the pictures
			// between a bright frame and a dark one were bright again, Pai pink on a dark stage)
			if (a->lights.size() == b->lights.size())
				for (size_t k = 0; k < b->lights.size(); k++) {
					const auto &la = a->lights[k], &lb = b->lights[k];
					const float na = la[0] * la[0] + la[1] * la[1] + la[2] * la[2], nb2 = lb[0] * lb[0] + lb[1] * lb[1] + lb[2] * lb[2];
					std::array<float, 3> l;
					if (na > 16 * nb2 || nb2 > 16 * na) l = alpha < 0.5f ? la : lb;
					else for (int e = 0; e < 3; e++) l[e] = la[e] + (lb[e] - la[e]) * alpha;
					m_light_blend.push_back(l);
				}
			if (a->focals.size() == b->focals.size())
				for (size_t k = 0; k < b->focals.size(); k++) {
					std::array<float, 2> f;
					for (int e = 0; e < 2; e++) f[e] = a->focals[k][e] + (b->focals[k][e] - a->focals[k][e]) * alpha;
					m_focal_blend.push_back(f);
				}
		}
	}
	// the newer list set up again from its state, with the blend
	*m_geo_r = b->geo;
	m_geo_r->raster = m_raster_r.get();
	m_geo_r->state = this;
	raster_state &ra = *m_raster_r;
	memcpy(ra.viewport, b->viewport, sizeof ra.viewport);
	ra.widened = b->widened;
	memcpy(ra.center, b->center, sizeof ra.center);
	ra.center_sel = b->center_sel;
	ra.reverse = b->reverse;
	ra.z_adjust = b->z_adjust;
	ra.master_z_clip = b->master_z_clip;
	ra.cur_command = b->cur_command;
	memcpy(ra.command_buffer, b->command_buffer, sizeof ra.command_buffer);
	ra.command_index = b->command_index;
	for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) ra.clip_plane[i][j] = b->clip_plane[i][j];
	std::copy(b->texture_ram.begin(), b->texture_ram.end(), ra.texture_ram);
	std::copy(b->log_ram.begin(), b->log_ram.end(), ra.log_ram);
	// the free camera's F for this moment: its view of the game's camera then (blended as the objects are)
	m_fc_apply = false;
	if (fc) {
		double K[12];
		memcpy(K, kb, sizeof K);
		if (a && alpha < 1 && !m_pair_cut)
			for (int e = 0; e < 12; e++) K[e] = ka[e] + (kb[e] - ka[e]) * alpha;
		memcpy(m_fc_K, K, sizeof K);
		m_fc_K_ok = true;
		if (m_fc_pilot) {   // (tests: a path flies it)
			freecam_pilot_state st;
			double Ki[12], Kbi[12];
			inverse(K, Ki);
			inverse(kb, Kbi);
			for (int e = 0; e < 3; e++) {
				st.p[e] = m_fc_pos[e];
				st.u[e] = m_fc_up[e]; st.h0[e] = m_fc_fwd0[e]; st.s0[e] = m_fc_right0[e];
				st.gp[e] = Ki[9 + e]; st.gf[e] = Ki[6 + e];
			}
			st.yaw = m_fc_yaw * DEG; st.pitch = m_fc_pitch * DEG;
			// (the fighters' middle: each side's fighter's place (each of x, y and z the middle one of its models' places),
			// taken into the anchor's view by the frame's own camera, and the middle of the two; a fighter far from the
			// other (the game parks one away from a shot of the other alone) left out, the one nearer the camera kept)
			double side[2][3];
			bool have[2] = { false, false };
			for (int pl = 0; pl < 2; pl++) {
				std::vector<double> at[3];
				for (const auto &o : b->objects)
					if (std::find(m_fc_fighters[pl].begin(), m_fc_fighters[pl].end(), o.oba) != m_fc_fighters[pl].end())
						for (int r = 0; r < 3; r++) at[r].push_back(o.m[9 + r]);
				if (at[0].size() < 4) continue;
				double v[3];
				for (int r = 0; r < 3; r++) {
					std::nth_element(at[r].begin(), at[r].begin() + std::ptrdiff_t(at[r].size() / 2), at[r].end());
					v[r] = at[r][at[r].size() / 2];
				}
				if (!(v[2] > 0)) continue;   // (behind the game's camera: one put out of the way)
				for (int r = 0; r < 3; r++) side[pl][r] = Kbi[r] * v[0] + Kbi[3 + r] * v[1] + Kbi[6 + r] * v[2] + Kbi[9 + r];
				have[pl] = true;
			}
			if (have[0] && have[1]) {
				const double d2 = (side[0][0] - side[1][0]) * (side[0][0] - side[1][0]) + (side[0][1] - side[1][1]) * (side[0][1] - side[1][1]) + (side[0][2] - side[1][2]) * (side[0][2] - side[1][2]);
				if (d2 > FIGHTERS_APART * FIGHTERS_APART) have[1] = false;
			}
			if (have[0] || have[1]) {
				for (int r = 0; r < 3; r++) st.mid[r] = have[0] && have[1] ? (side[0][r] + side[1][r]) / 2 : side[have[0] ? 0 : 1][r];
				st.mid_ok = true;
			}
			m_fc_pilot(st);
			for (int e = 0; e < 3; e++) m_fc_pos[e] = st.p[e];
			m_fc_yaw = std::remainder(st.yaw / DEG, 360.0);
			m_fc_pitch = std::clamp(st.pitch / DEG, -89.0, 89.0);
		}
		// F = the free camera's view (its pose inverted) after the game's camera inverted
		double Ki[12], P[12], Pi[12];
		inverse(K, Ki);
		freecam_pose(P);
		inverse(P, Pi);
		mul(Pi, Ki, m_fc_F);
		m_fc_apply = true;
	}
	m_replay = true;
	m_replay_obj = m_replay_light = m_replay_focal = 0;
	m_build_frame = b->frame;   // (the random fighters' build: as at that frame)
	geo_run(const_cast<u32 *>(b->list.data()), b->start, m_geo_r.get(), m_raster_r.get());
	m_replay = false;
	m_fc_apply = false;
	build_drawn(m_raster_r.get(), drawn);
}
