// license:BSD-3-Clause
// copyright-holders:R. Belmont, Olivier Galibert, ElSemi, Angelo Salese, Matthew Daniels, Ville Linde, Aaron Giles
// The 3D on the GPU, Metal (src/gpu/gpu3d.h). The shader's texel filtering, mipmapping, luma and
// colour are the CPU renderer's (model2_video.cpp: fetch_bilinear_texel, fast_log2 and
// draw_scanline_tex), which are MAME's (sega/model2rd.ipp; fast_log2 from video/voodoo_render.cpp),
// and the exact way's scan conversion is poly.h's render_triangle and render_polygon (MAME's
// devices/video/poly.h, Ville Linde and Aaron Giles), BSD-3-Clause, copyright-holders as above,
// written for the GPU. Low's edge smoothing is model2a::edge_smooth's (ours).
#include "gpu3d.h"
#import <Metal/Metal.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/CAMetalLayer.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {

const char *const kShaders = R"(
#include <metal_stdlib>
using namespace metal;
struct vin { float2 pos; float z, u, v; uint idx; };
struct vout {
	float4 pos [[position]];
	float ooz [[center_no_perspective]];   // 1/z, u/z, v/z: linear on the screen, as the rasterizer steps them
	float uoz [[center_no_perspective]];
	float voz [[center_no_perspective]];
	uint idx [[flat]];
};
struct target { uint owner [[color(0)]]; float4 rgba [[color(1)]]; };
// a polygon, as the shaders read it
struct pinfo {
	uint flags;       // 1 checker, 2 textured, 4 translucent, 8 sheet (texture RAM 1 is sheet 0), 16/32 raster spans
	uint colour;      // solid: 0xffRRGGBB
	uint pal, lumabase, luma;
	int texlod;
	uint texw, texh, texx, texy;
	uint wrapx, wrapy, mirrorx, mirrory, utex, utexminlod, utexx, utexy;
	int max_level;
	uint n;           // vertices
	int clipx;        // the window's left column, right column + 1 (render-scale pixels)
	int clipx1;
	int y0;           // exact: its first row, the row records' and the kept parameters' places
	uint rowbase, ckbase, ckstride;
};
struct pverts { float x[8], y[8], p[8][3]; };   // the vertices (render-scale), 1/z, u/z, v/z
// exact: a row's span (sx: its first pixel drawn, the checker's skip made; stop: past its last) and
// the step from one pixel drawn to the next
struct rowrec { int sx, stop; float d0, d1, d2; };
constant int CK = 16;   // the parameters kept every CK pixels drawn

// poly.h's round_coordinate
static int round_coordinate(float value)
{
	const float ipart = floor(value);
	const float fpart = value - ipart;
	return int(ipart) + ((fpart > 0.5f) ? 1 : 0);
}

// poly.h's extent for row y: the span [istartx, istopx) after the window's columns [cx0, cx1),
// its 1/z, u/z, v/z at its first pixel and their step across (render_triangle, render_polygon;
// where its compiler fuses a multiply and an add, fma)
static void extent(constant pinfo &o, const device pverts &pv, int y, int cx0, int cx1, thread int &istartx, thread int &istopx, thread float3 &start, thread float3 &dpdx)
{
	const float fully = float(y) + 0.5f;
	float startx, stopx;
	const uint n = o.n;
	if (n == 3) {
		int i1 = 0, i2 = 1, i3 = 2;
		if (pv.y[i2] < pv.y[i1]) { int t = i1; i1 = i2; i2 = t; }
		if (pv.y[i3] < pv.y[i2]) { int t = i2; i2 = i3; i3 = t; if (pv.y[i2] < pv.y[i1]) { t = i1; i1 = i2; i2 = t; } }
		const float x1 = pv.x[i1], y1 = pv.y[i1], x2 = pv.x[i2], y2 = pv.y[i2], x3 = pv.x[i3], y3 = pv.y[i3];
		const float dxdy_v1v2 = (y2 == y1) ? 0.0f : (x2 - x1) / (y2 - y1);
		const float dxdy_v1v3 = (y3 == y1) ? 0.0f : (x3 - x1) / (y3 - y1);
		const float dxdy_v2v3 = (y3 == y2) ? 0.0f : (x3 - x2) / (y3 - y2);
		float3 ps = 0, pdx = 0, pdy = 0;
		if (o.flags & 2u) {
			const float a00 = y2 - y3, a01 = x3 - x2, a02 = fma(x2, y3, -(x3 * y2));
			const float a10 = y3 - y1, a11 = x1 - x3, a12 = fma(x3, y1, -(x1 * y3));
			const float a20 = y1 - y2, a21 = x2 - x1, a22 = fma(x1, y2, -(x2 * y1));
			const float det = a02 + a12 + a22;
			for (int k = 0; k < 3; k++) {
				const float p1 = pv.p[i1][k], p2 = pv.p[i2][k], p3 = pv.p[i3][k];
				if (abs(det) < 0.00001f) { pdx[k] = 0.0f; pdy[k] = 0.0f; ps[k] = p1; }
				else {
					const float idet = 1.0f / det;
					pdx[k] = idet * fma(p3, a20, fma(p1, a00, p2 * a10));
					pdy[k] = idet * fma(p3, a21, fma(p1, a01, p2 * a11));
					ps[k] = idet * fma(p3, a22, fma(p1, a02, p2 * a12));
				}
			}
		}
		startx = fma(fully - y1, dxdy_v1v3, x1);
		stopx = fully < y2 ? fma(fully - y1, dxdy_v1v2, x1) : fma(fully - y2, dxdy_v2v3, x2);
		istartx = round_coordinate(startx); istopx = round_coordinate(stopx);
		if (istartx > istopx) { const int t = istartx; istartx = istopx; istopx = t; }
		istartx = max(istartx, cx0); istopx = min(istopx, cx1);
		const float fullstartx = float(istartx) + 0.5f;
		for (int k = 0; k < 3; k++) start[k] = fma(fully, pdy[k], fma(fullstartx, pdx[k], ps[k]));
		dpdx = pdx;
	} else {
		int minv = 0, maxv = 0;
		for (uint k = 1; k < n; k++) {
			if (pv.y[k] < pv.y[minv]) minv = int(k);
			else if (pv.y[k] > pv.y[maxv]) maxv = int(k);
		}
		// the edges, forward and backward from the top vertex, horizontal ones left out
		int fa[7], fb[7], ba[7], bb[7], nf = 0, nb = 0;
		for (int c = minv; c != maxv; c = (c == int(n) - 1) ? 0 : c + 1) {
			const int d = (c == int(n) - 1) ? 0 : c + 1;
			if (pv.y[c] != pv.y[d]) { fa[nf] = c; fb[nf] = d; nf++; }
		}
		for (int c = minv; c != maxv; c = (c == 0) ? int(n) - 1 : c - 1) {
			const int d = (c == 0) ? int(n) - 1 : c - 1;
			if (pv.y[c] != pv.y[d]) { ba[nb] = c; bb[nb] = d; nb++; }
		}
		if (nf == 0 || nb == 0) { istartx = istopx = 0; start = 0; dpdx = 0; return; }
		const float fdx = (pv.x[fb[0]] - pv.x[fa[0]]) * (1.0f / (pv.y[fb[0]] - pv.y[fa[0]]));
		const float bdx = (pv.x[bb[0]] - pv.x[ba[0]]) * (1.0f / (pv.y[bb[0]] - pv.y[ba[0]]));
		const bool fleft = (fa[0] == ba[0] && fdx < bdx) || (fa[0] != ba[0] && pv.x[fa[0]] < pv.x[ba[0]]);
		int la, lb, ra, rb;
		{
			int li = 0, ri = 0;
			const int nl = fleft ? nf : nb, nr = fleft ? nb : nf;
			while (li < nl - 1 && fully > pv.y[fleft ? fb[li] : bb[li]] && fully < pv.y[maxv]) li++;
			while (ri < nr - 1 && fully > pv.y[fleft ? bb[ri] : fb[ri]] && fully < pv.y[maxv]) ri++;
			la = fleft ? fa[li] : ba[li]; lb = fleft ? fb[li] : bb[li];
			ra = fleft ? ba[ri] : fa[ri]; rb = fleft ? bb[ri] : fb[ri];
		}
		const float looy = 1.0f / (pv.y[lb] - pv.y[la]), rooy = 1.0f / (pv.y[rb] - pv.y[ra]);
		const float lx = (pv.x[lb] - pv.x[la]) * looy, rx = (pv.x[rb] - pv.x[ra]) * rooy;
		startx = fma(fully - pv.y[la], lx, pv.x[la]);
		stopx = fma(fully - pv.y[ra], rx, pv.x[ra]);
		istartx = round_coordinate(startx); istopx = round_coordinate(stopx);
		if (istartx > istopx) { const int t = istartx; istartx = istopx; istopx = t; }
		istartx = max(istartx, cx0); istopx = min(istopx, cx1);
		if (o.flags & 2u) {
			const float ldy = fully - pv.y[la], rdy = fully - pv.y[ra];
			const float oox = 1.0f / (stopx - startx);
			for (int k = 0; k < 3; k++) {
				const float lparam = fma(ldy, (pv.p[lb][k] - pv.p[la][k]) * looy, pv.p[la][k]);
				const float rparam = fma(rdy, (pv.p[rb][k] - pv.p[ra][k]) * rooy, pv.p[ra][k]);
				dpdx[k] = (rparam - lparam) * oox;
				start[k] = fma(float(istartx) + 0.5f - startx, dpdx[k], lparam);
			}
		} else { start = 0; dpdx = 0; }
	}
	if (istartx >= istopx) istartx = istopx = 0;
}

// (model2_video.cpp's, from MAME)
static uint LERP(uint x, uint y, uint a) { return (x + (((y - x) * a) >> 8)) & 0x00ff00ffu; }

constant uchar s_log2_table[128] = {
	  0,   2,   5,   8,  11,  14,  16,  19,  22,  25,  27,  30,  33,  35,  38,  40,
	 43,  46,  48,  51,  53,  56,  58,  61,  63,  65,  68,  70,  73,  75,  77,  80,
	 82,  84,  87,  89,  91,  93,  96,  98, 100, 102, 104, 106, 109, 111, 113, 115,
	117, 119, 121, 123, 125, 127, 129, 132, 134, 136, 138, 140, 141, 143, 145, 147,
	149, 151, 153, 155, 157, 159, 161, 162, 164, 166, 168, 170, 172, 173, 175, 177,
	179, 181, 182, 184, 186, 188, 189, 191, 193, 194, 196, 198, 200, 201, 203, 205,
	206, 208, 209, 211, 213, 214, 216, 218, 219, 221, 222, 224, 225, 227, 229, 230,
	232, 233, 235, 236, 238, 239, 241, 242, 244, 245, 247, 248, 250, 251, 253, 254 };

static int fast_log2(float value)
{
	if (value < 0.0f) return 0;
	const uint ival = as_type<uint>(value) >> 16;
	const int ex = int(ival >> 7) - 127;
	return (ex << 8) | int(s_log2_table[ival & 127]);
}

static uint fetch_bilinear_texel(constant pinfo &o, const device uint *s0, const device uint *s1, bool translucent, int miplevel, int u, int v)
{
	uint tex_width, tex_height, tex_x, tex_y;
	const device uint *sheet;
	if (miplevel == -1) {
		// microtexture
		tex_width = 128; tex_height = 128;
		tex_x = o.utexx; tex_y = o.utexy;
		sheet = s1;
		u <<= 1 << o.utexminlod;
		v <<= 1 << o.utexminlod;
	} else {
		tex_width = o.texw >> miplevel;
		tex_height = o.texh >> miplevel;
		tex_x = ((o.texx - 2048u) >> miplevel) & 2047u;
		tex_y = ((o.texy - 1024u) >> miplevel) & 1023u;
		sheet = (miplevel & 1) ? s1 : s0;
		u >>= miplevel;
		v >>= miplevel;
	}
	if (o.mirrorx && (uint(u) & (tex_width << 8))) u = ~u;
	if (o.mirrory && (uint(v) & (tex_height << 8))) v = ~v;
	u -= 0x80;
	v -= 0x80;
	uint ufrac = uint(u) & 0xffu;
	uint vfrac = uint(v) & 0xffu;
	uint u0 = uint(u >> 8) & (tex_width - 1);
	uint u1 = (u0 + 1) & (tex_width - 1);
	uint v0 = uint(v >> 8) & (tex_height - 1);
	uint v1 = (v0 + 1) & (tex_height - 1);
	if (!o.wrapx && u1 == 0) {
		if (ufrac >= 0x80) { u0 = u1; u1++; ufrac = 0; }
		else { u1 = u0; u0--; ufrac = 0x100; }
	}
	if (!o.wrapy && v1 == 0) {
		if (vfrac >= 0x80) { v0 = 0; v1++; vfrac = 0; }
		else { v1 = v0; v0--; vfrac = 0x100; }
	}
	// get_texel's addressing: sheets mapped 2048 x 1024, stored 1024 x 2048
	uint xo[2], fl[2], sh[2];
	for (int k = 0; k < 2; k++) {
		const uint x = k ? u1 : u0;
		uint x2 = tex_x + x;
		const uint right = x2 >= 1024u ? 1024u : 0u;
		x2 -= right;
		xo[k] = x2 >> 1;
		fl[k] = right << 8;
		sh[k] = ((xo[k] & 1u) << 4) + ((~x & 1u) << 2);
	}
	const uint r0 = ((tex_y + v0) >> 1) << 9, r1 = ((tex_y + v1) >> 1) << 9;
	const uint ys0 = (~v0 & 1u) << 3, ys1 = (~v1 & 1u) << 3;
	uint tex00 = ((sheet[(((r0 ^ fl[0]) | xo[0]) >> 1) & 0x7ffffu] >> (sh[0] + ys0)) & 0x0fu) << 4;
	uint tex01 = ((sheet[(((r0 ^ fl[1]) | xo[1]) >> 1) & 0x7ffffu] >> (sh[1] + ys0)) & 0x0fu) << 4;
	uint tex10 = ((sheet[(((r1 ^ fl[0]) | xo[0]) >> 1) & 0x7ffffu] >> (sh[0] + ys1)) & 0x0fu) << 4;
	uint tex11 = ((sheet[(((r1 ^ fl[1]) | xo[1]) >> 1) & 0x7ffffu] >> (sh[1] + ys1)) & 0x0fu) << 4;
	if (translucent) {
		if (tex00 != 0xf0u) tex00 |= 0x00800000u;
		if (tex01 != 0xf0u) tex01 |= 0x00800000u;
		if (tex10 != 0xf0u) tex10 |= 0x00800000u;
		if (tex11 != 0xf0u) tex11 |= 0x00800000u;
		if (tex00 == 0x000000f0u) tex00 = tex01 & 0xffu;
		if (tex01 == 0x000000f0u) tex01 = tex00 & 0xffu;
		if (tex10 == 0x000000f0u) tex10 = tex11 & 0xffu;
		if (tex11 == 0x000000f0u) tex11 = tex10 & 0xffu;
	}
	uint tex0x = LERP(tex00, tex01, ufrac);
	uint tex1x = LERP(tex10, tex11, ufrac);
	if (translucent) {
		if (tex0x == 0x000000f0u) tex0x = tex1x & 0xffu;
		if (tex1x == 0x000000f0u) tex1x = tex0x & 0xffu;
	}
	return LERP(tex0x, tex1x, vfrac);
}

// draw_scanline_tex's texel at 1/z, u/z, v/z (q), before luma: the mip levels and microtexture blended
static uint texel_at(constant pinfo &o, const device uint *tex0, const device uint *tex1, float3 q)
{
	const bool translucent = (o.flags & 4u) != 0;
	const device uint *s0 = (o.flags & 8u) ? tex1 : tex0;
	const device uint *s1 = (o.flags & 8u) ? tex0 : tex1;
	const float z = precise::divide(1.0f, q.x);
	const int mml = -o.texlod + fast_log2(z);
	const int level = clamp(mml >> 7, 0, o.max_level);
	const int u = int((q.y * z) * 256.0f);
	const int v = int((q.z * z) * 256.0f);
	uint tx = fetch_bilinear_texel(o, s0, s1, translucent, level, u, v);
	if (mml > 0 && level < o.max_level)
		tx = LERP(tx, fetch_bilinear_texel(o, s0, s1, translucent, level + 1, u, v), uint(mml & 127) << 1);
	else if (o.utex && mml < 0)
		tx = LERP(tx, fetch_bilinear_texel(o, s0, s1, translucent, -1, u, v), uint(min(-mml >> o.utexminlod, 127)));
	return tx;
}

// the texel's colour: luma, colorxlat, gamma (0xffRRGGBB)
static uint texel_colour(constant pinfo &o, uint tx, const device uchar *lumaram, const device ushort *xlat, const device uchar *gamma)
{
	if (o.flags & 4u) tx &= 0xffu;
	const uint luma = min((uint(lumaram[(o.lumabase + (tx >> 1)) & 0x7fffu]) * o.luma) / 256u, 0x3fu);
	const uint r = xlat[0x0000u + (((o.pal >> 0) & 0x1fu) << 8) + luma] & 0xffu;
	const uint g = xlat[0x2000u + (((o.pal >> 5) & 0x1fu) << 8) + luma] & 0xffu;
	const uint b = xlat[0x4000u + (((o.pal >> 10) & 0x1fu) << 8) + luma] & 0xffu;
	return 0xff000000u | (uint(gamma[r]) << 16) | (uint(gamma[g]) << 8) | uint(gamma[b]);
}

// ---- exact: the rasterizer's spans, a thread a polygon's row ----
struct spans_k { uint jobs, W, H; };
kernel void k_spans(uint t [[thread_position_in_grid]], constant spans_k &k [[buffer(0)]],
	constant pinfo *polys [[buffer(1)]], const device pverts *verts [[buffer(2)]],
	const device uint *jobpoly [[buffer(3)]], const device uint *jobbase [[buffer(4)]],
	device rowrec *rows [[buffer(5)]], device packed_float3 *ck [[buffer(6)]], device atomic_uint *owner [[buffer(7)]],
	const device uint *tex0 [[buffer(8)]], const device uint *tex1 [[buffer(9)]], constant uint &total [[buffer(10)]])
{
	if (t >= total) return;
	uint lo = 0, hi = k.jobs - 1;
	while (lo < hi) { const uint mid = (lo + hi + 1) >> 1; if (jobbase[mid] <= t) lo = mid; else hi = mid - 1; }
	const uint idx = jobpoly[lo];
	constant pinfo &o = polys[idx];
	const uint r = t - o.rowbase;
	const int y = o.y0 + int(r);
	int sx, stop;
	float3 q, d;
	extent(o, verts[idx], y, o.clipx, o.clipx1, sx, stop, q, d);
	int step = 1;
	if (o.flags & 1u) {   // checker: the pixels with (x ^ y) odd, two at a time
		if (((sx ^ y) & 1) == 0) { sx++; q += d; }
		d *= 2.0f;
		step = 2;
	}
	device rowrec &rr = rows[r + o.rowbase];
	rr.sx = sx; rr.stop = stop; rr.d0 = d.x; rr.d1 = d.y; rr.d2 = d.z;
	const bool tex = (o.flags & 2u) != 0, see_through = (o.flags & 6u) == 6u;
	device atomic_uint *orow = owner + uint(y) * k.W;
	uint n = 0;
	for (int x = sx; x < stop; x += step, n++, q += d) {
		if (tex && (n % CK) == 0 && n / CK < o.ckstride) ck[o.ckbase + r * o.ckstride + n / CK] = packed_float3(q);
		if (see_through && texel_at(o, tex0, tex1, q) < 0x00400000u) continue;   // (a translucent texel under half: not drawn)
		atomic_fetch_min_explicit(&orow[x], idx, memory_order_relaxed);
	}
}

// ---- exact: each pixel its owner's colour, its parameters stepped on from the nearest kept ----
struct shade_k { uint W, H; };
kernel void k_shade(uint2 g [[thread_position_in_grid]], constant shade_k &k [[buffer(0)]],
	constant pinfo *polys [[buffer(1)]], const device rowrec *rows [[buffer(5)]], const device packed_float3 *ck [[buffer(6)]],
	const device uint *owner [[buffer(7)]], const device uint *tex0 [[buffer(8)]], const device uint *tex1 [[buffer(9)]],
	const device uchar *lumaram [[buffer(10)]], const device ushort *xlat [[buffer(11)]], const device uchar *gamma [[buffer(12)]],
	device uint *out [[buffer(13)]])
{
	if (g.x >= k.W || g.y >= k.H) return;
	const uint at = g.y * k.W + g.x;
	const uint idx = owner[at];
	if (idx == 0xffffffffu) { out[at] = 0; return; }
	constant pinfo &o = polys[idx];
	if (!(o.flags & 2u)) { out[at] = o.colour; return; }
	const uint r = uint(int(g.y) - o.y0);
	const rowrec rr = rows[o.rowbase + r];
	const uint n = (int(g.x) - rr.sx) >> ((o.flags & 1u) ? 1 : 0);
	const uint j = min(n / CK, o.ckstride - 1);
	float3 q = float3(ck[o.ckbase + r * o.ckstride + j]);
	const float3 d = float3(rr.d0, rr.d1, rr.d2);
	for (uint i = j * CK; i < n; i++) q += d;
	out[at] = texel_colour(o, texel_at(o, tex0, tex1, q), lumaram, xlat, gamma);
}

// ---- fast: triangles, through the GPU's rasterizer ----
// The 1/z, u/z, v/z the CPU's rasterizer has at pixel (x, y): the row's start (extent), then
// stepped a pixel at a time (two for checker), or at once (flag 32: not quite the same sums)
static float3 span_params(constant pinfo &o, const device pverts &pv, int x, int y)
{
	int sx, stop;
	float3 q, d;
	extent(o, pv, y, o.clipx, 1 << 30, sx, stop, q, d);
	if (o.flags & 1u) {
		if (((sx ^ y) & 1) == 0) { sx++; q += d; }
		d *= 2.0f;
		if (o.flags & 32u) return q + float((x - sx) >> 1) * d;
		for (int xx = sx; xx < x; xx += 2) q += d;
	} else {
		if (o.flags & 32u) return q + float(x - sx) * d;
		for (int xx = sx; xx < x; xx++) q += d;
	}
	return q;
}

vertex vout vs_poly(uint vid [[vertex_id]], const device vin *v [[buffer(0)]], constant float4 &k [[buffer(1)]])
{
	// k: width, height, depth per polygon
	vout o;
	const vin i = v[vid];
	o.pos = float4(i.pos.x / k.x * 2.0 - 1.0, 1.0 - i.pos.y / k.y * 2.0, float(i.idx + 1) * k.z, 1.0);
	o.ooz = i.z; o.uoz = i.u; o.voz = i.v;
	o.idx = i.idx;
	return o;
}

fragment target fs_poly(vout i [[stage_in]], constant pinfo *polys [[buffer(0)]], const device pverts *verts [[buffer(6)]],
	const device uint *tex0 [[buffer(1)]], const device uint *tex1 [[buffer(2)]],
	const device uchar *lumaram [[buffer(3)]], const device ushort *xlat [[buffer(4)]], const device uchar *gamma [[buffer(5)]])
{
	constant pinfo &o = polys[i.idx];
	if (o.flags & 1u) {   // checker: the rasterizer draws the pixels with (x ^ y) odd
		const uint2 p = uint2(i.pos.xy);
		if (((p.x ^ p.y) & 1u) == 0u) discard_fragment();
	}
	target t;
	t.owner = i.idx;
	uint c = o.colour;
	if (o.flags & 2u) {
		float3 q = float3(i.ooz, i.uoz, i.voz);
		if (o.flags & 16u) q = span_params(o, verts[i.idx], int(i.pos.x), int(i.pos.y));
		const uint tx = texel_at(o, tex0, tex1, q);
		if ((o.flags & 4u) && tx < 0x00400000u) discard_fragment();
		c = texel_colour(o, tx, lumaram, xlat, gamma);
	}
	t.rgba = float4(float((c >> 16) & 255u), float((c >> 8) & 255u), float(c & 255u), 255.0) / 255.0;
	return t;
}

// (the fast way's picture into the exact way's: 0xAARRGGBB words)
kernel void k_from_tex(uint2 g [[thread_position_in_grid]], texture2d<float, access::read> src [[texture(0)]], device uint *out [[buffer(0)]], constant uint2 &wh [[buffer(1)]])
{
	if (g.x >= wh.x || g.y >= wh.y) return;
	const float4 c = src.read(g);
	const uint4 b = uint4(rint(c * 255.0));
	out[g.y * wh.x + g.x] = b.a ? (b.a << 24) | (b.r << 16) | (b.g << 8) | b.b : 0u;
}

// ---- into the game's target ----
static float4 unpack(uint c) { return float4(float((c >> 16) & 255u), float((c >> 8) & 255u), float(c & 255u), float(c >> 24)) / 255.0; }
struct out_k { uint w, h, ss; };
// as it is, or each ss x ss block averaged (premultiplied: what isn't drawn adds nothing, its share drawn the alpha)
kernel void k_out(uint2 g [[thread_position_in_grid]], constant out_k &k [[buffer(0)]], const device uint *src [[buffer(1)]],
	texture2d<float, access::write> dst [[texture(0)]])
{
	if (g.x >= k.w || g.y >= k.h) return;
	const uint W = k.w * k.ss;
	if (k.ss == 1) { dst.write(unpack(src[g.y * W + g.x]), g); return; }
	uint r = 0, gg = 0, b = 0, a = 0;
	for (uint j = 0; j < k.ss; j++)
		for (uint i = 0; i < k.ss; i++) {
			const uint c = src[(g.y * k.ss + j) * W + g.x * k.ss + i];
			if (!c) continue;
			r += (c >> 16) & 255u; gg += (c >> 8) & 255u; b += c & 255u; a += 255u;
		}
	const uint n = k.ss * k.ss;
	dst.write(float4(float(r / n), float(gg / n), float(b / n), float(a) / float(n)) / 255.0, g);
}

// Low: model2a::edge_smooth on the 3D at its scale, the picture beneath from the 2D (below_3d)
struct edge_k { uint W, H, s, w, pad; int bdx, bdy; uint filter; };
static uint below_3d(constant edge_k &k, const device uint *bd, const device uint *mid, int X, int Y)
{
	const int s = int(k.s), w = int(k.w), W = w * s, H = 384 * s, P = int(k.pad), pw = w + 2 * P;
	const int x = X / s, y = Y / s;
	const uint m = mid[y * w + x];
	if (m) return m;
	if (k.filter) {
		const float u = (float(X + k.bdx) + 0.5f) / float(s) - 0.5f, v = (float(Y + k.bdy) + 0.5f) / float(s) - 0.5f;
		const int ua = clamp(int(floor(u)), 0, w - 1), va = clamp(int(floor(v)), 0, 383);
		const int ub = clamp(int(floor(u)) + 1, 0, w - 1), vb = clamp(int(floor(v)) + 1, 0, 383);
		const float fu = u - floor(u), fv = v - floor(v);
		const uint c00 = bd[(va + P) * pw + ua + P], c10 = bd[(va + P) * pw + ub + P], c01 = bd[(vb + P) * pw + ua + P], c11 = bd[(vb + P) * pw + ub + P];
		uint o = 0xff000000u;
		for (int sh = 0; sh < 24; sh += 8) {
			const float t = float((c00 >> sh) & 255u) * (1 - fu) + float((c10 >> sh) & 255u) * fu, b = float((c01 >> sh) & 255u) * (1 - fu) + float((c11 >> sh) & 255u) * fu;
			o |= uint(floor(t * (1 - fv) + b * fv + 0.5f)) << sh;
		}
		return o;
	}
	if (k.bdx || k.bdy) return bd[(clamp(Y + k.bdy, 0, H - 1) / s + P) * pw + clamp(X + k.bdx, 0, W - 1) / s + P];
	return bd[(y + P) * pw + x + P];
}
static int luma_of(uint c) { return int((c >> 16) & 0xffu) * 77 + int((c >> 8) & 0xffu) * 150 + int(c & 0xffu) * 29; }
kernel void k_edge(uint2 g [[thread_position_in_grid]], constant edge_k &k [[buffer(0)]], const device uint *src [[buffer(1)]],
	const device uint *bd [[buffer(2)]], const device uint *mid [[buffer(3)]], texture2d<float, access::write> dst [[texture(0)]])
{
	if (g.x >= k.W || g.y >= k.H) return;
	const int X = int(g.x), Y = int(g.y), W = int(k.W);
	uint out = src[Y * W + X];
	if (Y >= 1 && Y < int(k.H) - 1 && X >= 1 && X < W - 1) {
		const uint c3[5] = { src[Y * W + X], src[(Y - 1) * W + X], src[(Y + 1) * W + X], src[Y * W + X - 1], src[Y * W + X + 1] };
		int l[5];
		bool any = false, all = true;
		for (int i = 0; i < 5; i++) { l[i] = c3[i] ? luma_of(c3[i] & 0xffffffu) : -1; any = any || c3[i]; all = all && c3[i]; }
		const int TH = 32 * 256;
		bool go = any;
		uint c[5];
		if (go && all) {
			int lo = l[0], hi = l[0];
			for (int i = 1; i < 5; i++) { lo = min(lo, l[i]); hi = max(hi, l[i]); }
			go = hi - lo >= TH;
			for (int i = 0; i < 5; i++) c[i] = c3[i] & 0xffffffu;
		} else if (go) {
			const int2 at[5] = { int2(X, Y), int2(X, Y - 1), int2(X, Y + 1), int2(X - 1, Y), int2(X + 1, Y) };
			for (int i = 0; i < 5; i++) { c[i] = (c3[i] ? c3[i] : below_3d(k, bd, mid, at[i].x, at[i].y)) & 0xffffffu; l[i] = luma_of(c[i]); }
		}
		if (go) {
			const int lm = l[0], ln = l[1], ls = l[2], lw = l[3], le = l[4];
			int lo = lm, hi = lm;
			for (int i = 1; i < 5; i++) { lo = min(lo, l[i]); hi = max(hi, l[i]); }
			if (hi - lo >= TH) {
				const float sub = min(1.0f, float(abs(ln + ls + lw + le - 4 * lm)) / float(4 * (hi - lo)));
				const float sm = sub * sub * fma(-2.0f, sub, 3.0f);
				const int kk = int(fma(sm * sm, 0.75f * 0.5f * 256.0f, 0.5f));
				if (kk) {
					const bool across_v = abs(ln + ls - 2 * lm) >= abs(lw + le - 2 * lm);
					const uint c1 = across_v ? c[1] : c[3], c2 = across_v ? c[2] : c[4];
					const int l1 = across_v ? ln : lw, l2 = across_v ? ls : le;
					const uint other = abs(l1 - lm) >= abs(l2 - lm) ? c1 : c2;
					uint m = 0xff000000u;
					for (int sh = 0; sh < 24; sh += 8) {
						const int p = int((c[0] >> sh) & 255u), q = int((other >> sh) & 255u);
						m |= uint(p + (((q - p) * kk + 128) >> 8)) << sh;
					}
					out = m;
				}
			}
		}
	}
	dst.write(unpack(out), g);
}

)";

struct vin { float x, y, z, u, v; uint32_t idx; };
struct pinfo {
	uint32_t flags, colour, pal, lumabase, luma;
	int32_t texlod;
	uint32_t texw, texh, texx, texy;
	uint32_t wrapx, wrapy, mirrorx, mirrory, utex, utexminlod, utexx, utexy;
	int32_t max_level;
	uint32_t n;
	int32_t clipx, clipx1, y0;
	uint32_t rowbase, ckbase, ckstride;
};
struct pverts { float x[8], y[8], p[8][3]; };
struct rowrec { int32_t sx, stop; float d0, d1, d2; };
constexpr int CK = 16;

// poly.h's round_coordinate
inline int round_coordinate(float v)
{
	if (!(v < 2147483520.0f)) return INT32_MAX;
	const float ip = std::floor(v);
	if (ip < -2147483648.0f) return INT32_MIN;
	return int(ip) + ((v - ip > 0.5f) ? 1 : 0);
}

class gpu3d_metal final : public gpu3d
{
public:
	explicit gpu3d_metal(id<MTLDevice> dev) : m_dev(dev) {}
	~gpu3d_metal() override
	{
		finish();
		for (auto &t : m_tg) if (t.pb) CVPixelBufferRelease(t.pb);
	}
	bool init()
	{
		const auto t0 = std::chrono::steady_clock::now();
		NSError *err = nil;
		MTLCompileOptions *co = [MTLCompileOptions new];
		// no fast math: the CPU's float arithmetic as it is (IEEE division, no reassociation)
		if (@available(macOS 15.0, iOS 18.0, *)) co.mathMode = MTLMathModeSafe;
		else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
			co.fastMathEnabled = NO;
#pragma clang diagnostic pop
		}
		std::string src = kShaders;
		if (getenv("GPU3D_COLD")) src += "// " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "\n";   // (tests: not from Metal's cache, as a new build's first start)
		id<MTLLibrary> lib = [m_dev newLibraryWithSource:[NSString stringWithUTF8String:src.c_str()] options:co error:&err];
		if (!lib) { fprintf(stderr, "gpu3d metal: shaders: %s\n", err ? err.localizedDescription.UTF8String : "?"); return false; }
		MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
		pd.vertexFunction = [lib newFunctionWithName:@"vs_poly"];
		pd.fragmentFunction = [lib newFunctionWithName:@"fs_poly"];
		pd.colorAttachments[0].pixelFormat = MTLPixelFormatR32Uint;
		pd.colorAttachments[1].pixelFormat = MTLPixelFormatBGRA8Unorm;
		pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
		m_pipe = [m_dev newRenderPipelineStateWithDescriptor:pd error:&err];
		if (!m_pipe) { fprintf(stderr, "gpu3d metal: pipeline: %s\n", err ? err.localizedDescription.UTF8String : "?"); return false; }
		auto kernel = [&](NSString *name) -> id<MTLComputePipelineState> {
			id<MTLFunction> f = [lib newFunctionWithName:name];
			id<MTLComputePipelineState> p = f ? [m_dev newComputePipelineStateWithFunction:f error:&err] : nil;
			if (!p) fprintf(stderr, "gpu3d metal: %s: %s\n", name.UTF8String, err ? err.localizedDescription.UTF8String : "?");
			return p;
		};
		m_k_spans = kernel(@"k_spans"); m_k_shade = kernel(@"k_shade"); m_k_out = kernel(@"k_out");
		m_k_edge = kernel(@"k_edge"); m_k_from_tex = kernel(@"k_from_tex");
		if (!m_k_spans || !m_k_shade || !m_k_out || !m_k_edge || !m_k_from_tex) return false;
		MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
		dd.depthCompareFunction = MTLCompareFunctionLess;
		dd.depthWriteEnabled = YES;
		m_depth_state = [m_dev newDepthStencilStateWithDescriptor:dd];
		m_queue = [m_dev newCommandQueue];
		static const size_t sizes[5] = { 0x80000 * 4, 0x80000 * 4, 0x8000, 0x6000 * 2, 256 };
		for (int k = 0; k < 5; k++) m_ram[k] = [m_dev newBufferWithLength:sizes[k] options:MTLResourceStorageModeShared];
		if (const char *s = getenv("GPU3D_SPANS")) { m_spans = atoi(s); m_spans_forced = true; }
		fprintf(stderr, "gpu3d metal: %s, its shaders ready in %.0f ms\n", m_dev.name.UTF8String, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
		return m_queue != nil;
	}
	const char *name() const override { return "metal"; }
	void set_rams(const gpu3d_rams &r) override { m_rams = r; }
	void set_quality(gpu3d_quality q) override { m_quality = q; }
	gpu3d_quality quality() const override { return m_quality; }

	bool draw(const gpu3d_poly *polys, size_t count, int w, int h, int scale,
		uint32_t *owner, size_t ostride, uint32_t *rgba, size_t rstride, double *gpu_ms) override
	{
		@autoreleasepool {
		if (w <= 0 || h <= 0) return false;
		finish();
		const auto t0 = std::chrono::steady_clock::now();
		id<MTLCommandBuffer> cb = [m_queue commandBuffer];
		if (!encode_picture(cb, polys, count, w, h, scale)) return false;
		// read back through shared buffers (the pictures stay private: any Mac)
		const size_t row = size_t(w) * 4, bytes = row * size_t(h);
		if (!m_rb_owner || m_rb_owner.length < bytes) {
			m_rb_owner = [m_dev newBufferWithLength:bytes options:MTLResourceStorageModeShared];
			m_rb_rgba = [m_dev newBufferWithLength:bytes options:MTLResourceStorageModeShared];
		}
		id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
		if (owner) {
			if (exact()) [bl copyFromBuffer:m_owner_buf sourceOffset:0 toBuffer:m_rb_owner destinationOffset:0 size:bytes];
			else [bl copyFromTexture:m_owner_tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
				toBuffer:m_rb_owner destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:bytes];
		}
		if (rgba) [bl copyFromBuffer:m_cbuf sourceOffset:0 toBuffer:m_rb_rgba destinationOffset:0 size:bytes];
		[bl endEncoding];
		[cb commit];
		m_cb = cb;
		m_cpu_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		if (!finish(gpu_ms)) return false;
		for (int y = 0; y < h; y++) {
			if (owner) memcpy(owner + size_t(y) * ostride, (const uint8_t *)m_rb_owner.contents + y * row, row);
			if (rgba) memcpy(rgba + size_t(y) * rstride, (const uint8_t *)m_rb_rgba.contents + y * row, row);
		}
		return true;
		}
	}

	bool make_targets(int w, int h, int count) override
	{
		finish();
		for (auto &t : m_tg) if (t.pb) CVPixelBufferRelease(t.pb);
		m_tg.clear();
		for (int i = 0; i < count; i++) {
			NSDictionary *attrs = @{ (id)kCVPixelBufferIOSurfacePropertiesKey: @{}, (id)kCVPixelBufferMetalCompatibilityKey: @YES };
			CVPixelBufferRef pb = nullptr;
			if (CVPixelBufferCreate(kCFAllocatorDefault, w, h, kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef)attrs, &pb) != kCVReturnSuccess || !pb) return false;
			IOSurfaceRef s = CVPixelBufferGetIOSurface(pb);
			MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w height:h mipmapped:NO];
			d.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
			id<MTLTexture> tx = s ? [m_dev newTextureWithDescriptor:d iosurface:s plane:0] : nil;
			if (!tx) { CVPixelBufferRelease(pb); return false; }
			m_tg.push_back({ pb, tx });
		}
		m_tw = w; m_th = h;
		return true;
	}
	void *target_handle(int i) const override { return i >= 0 && i < int(m_tg.size()) ? m_tg[i].pb : nullptr; }

	bool submit(const gpu3d_poly *polys, size_t count, const gpu3d_frame &f, int target) override
	{
		@autoreleasepool {
		if (!finish()) return false;
		if (target < -1 || target >= int(m_tg.size()) || (target >= 0 && (f.w != m_tw || f.h != m_th)) || f.ss < 1) return false;
		const auto t0 = std::chrono::steady_clock::now();
		const int W = f.w * f.ss, H = f.h * f.ss, s = f.scale * f.ss;
		id<MTLCommandBuffer> cb = [m_queue commandBuffer];
		if (!encode_picture(cb, polys, count, W, H, s)) return false;
		if (target < 0) {   // (a test picture: drawn, not put anywhere)
			[cb commit];
			m_cb = cb;
			m_cpu_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			return true;
		}
		id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
		if (f.edge && f.ss == 1 && f.backdrop && f.mid) {
			const int P = f.pad, pw = f.w2d + 2 * P;
			const size_t bdb = size_t(pw) * (384 + 2 * P) * 4, midb = size_t(f.w2d) * 384 * 4;
			if (!m_bd || m_bd.length < bdb) { m_bd = [m_dev newBufferWithLength:bdb options:MTLResourceStorageModeShared]; m_v2d = ~0ull; }
			if (!m_mid || m_mid.length < midb) { m_mid = [m_dev newBufferWithLength:midb options:MTLResourceStorageModeShared]; m_v2d = ~0ull; }
			if (f.version2d != m_v2d || f.w2d != m_w2d) {
				memcpy(m_bd.contents, f.backdrop, bdb);
				memcpy(m_mid.contents, f.mid, midb);
				m_v2d = f.version2d; m_w2d = f.w2d;
			}
			struct { uint32_t W, H, s, w, pad; int32_t bdx, bdy; uint32_t filter; } k = { uint32_t(W), uint32_t(H), uint32_t(s), uint32_t(f.w2d), uint32_t(P), f.bdx, f.bdy, f.filter ? 1u : 0u };
			[ce setComputePipelineState:m_k_edge];
			[ce setBytes:&k length:sizeof k atIndex:0];
			[ce setBuffer:m_cbuf offset:0 atIndex:1];
			[ce setBuffer:m_bd offset:0 atIndex:2];
			[ce setBuffer:m_mid offset:0 atIndex:3];
			[ce setTexture:m_tg[target].tex atIndex:0];
			dispatch2(ce, m_k_edge, W, H);
		} else {
			const uint32_t k[3] = { uint32_t(f.w), uint32_t(f.h), uint32_t(f.ss) };
			[ce setComputePipelineState:m_k_out];
			[ce setBytes:k length:sizeof k atIndex:0];
			[ce setBuffer:m_cbuf offset:0 atIndex:1];
			[ce setTexture:m_tg[target].tex atIndex:0];
			dispatch2(ce, m_k_out, f.w, f.h);
		}
		[ce endEncoding];
		[cb commit];
		m_cb = cb;
		m_cpu_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		return true;
		}
	}

	bool finish(double *gpu_ms = nullptr, double *cpu_ms = nullptr) override
	{
		if (!m_cb) { if (gpu_ms) *gpu_ms = m_last_gpu_ms; if (cpu_ms) *cpu_ms = m_cpu_ms; return m_last_ok; }
		[m_cb waitUntilCompleted];
		m_last_ok = m_cb.status == MTLCommandBufferStatusCompleted;
		if (!m_last_ok) fprintf(stderr, "gpu3d metal: a picture failed (%s)\n", m_cb.error ? m_cb.error.localizedDescription.UTF8String : "?");
		m_last_gpu_ms = (m_cb.GPUEndTime - m_cb.GPUStartTime) * 1000.0;
		m_cb = nil;
		if (gpu_ms) *gpu_ms = m_last_gpu_ms;
		if (cpu_ms) *cpu_ms = m_cpu_ms;
		return m_last_ok;
	}
	bool busy() const override { return m_cb != nil; }

	bool read_target(int target, uint32_t *out, size_t stride) override
	{
		@autoreleasepool {
		if (!finish() || target < 0 || target >= int(m_tg.size())) return false;
		const size_t row = size_t(m_tw) * 4, bytes = row * m_th;
		id<MTLBuffer> rb = [m_dev newBufferWithLength:bytes options:MTLResourceStorageModeShared];
		id<MTLCommandBuffer> cb = [m_queue commandBuffer];
		id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
		[bl copyFromTexture:m_tg[target].tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(m_tw, m_th, 1)
			toBuffer:rb destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:bytes];
		[bl endEncoding];
		[cb commit];
		[cb waitUntilCompleted];
		for (int y = 0; y < m_th; y++) memcpy(out + size_t(y) * stride, (const uint8_t *)rb.contents + y * row, row);
		return cb.status == MTLCommandBufferStatusCompleted;
		}
	}

private:
	bool exact() const { return m_quality == gpu3d_quality::exact && !m_spans_forced; }
	int fast_spans() const { return m_spans_forced ? m_spans : 0; }

	static void dispatch2(id<MTLComputeCommandEncoder> ce, id<MTLComputePipelineState> p, int w, int h)
	{
		const NSUInteger tw = p.threadExecutionWidth, th = std::max<NSUInteger>(1, p.maxTotalThreadsPerThreadgroup / tw);
		[ce dispatchThreadgroups:MTLSizeMake((w + tw - 1) / tw, (h + th - 1) / th, 1) threadsPerThreadgroup:MTLSizeMake(tw, th, 1)];
	}

	// The picture w x h at `scale` into m_cbuf (0xAARRGGBB, 0 none); the exact way's owners in
	// m_owner_buf, the fast way's in m_owner_tex
	bool encode_picture(id<MTLCommandBuffer> cb, const gpu3d_poly *polys, size_t count, int w, int h, int scale)
	{
		if (!buffers(w, h)) return false;
		upload_rams();
		m_info.assign(std::max<size_t>(count, 1), pinfo{});
		m_pv.resize(std::max<size_t>(count, 1));
		const float s = float(scale);
		const bool ex = exact();
		const int spans = ex ? 1 : fast_spans();
		struct run { size_t first, count; MTLScissorRect sc; };
		std::vector<run> runs;
		m_verts.clear();
		m_jobpoly.clear(); m_jobbase.clear();
		uint32_t rows = 0, cks = 0;
		for (size_t i = 0; i < count; i++) {
			const gpu3d_poly &p = polys[i];
			if (p.n < 3 || p.n > 8 || p.renderer == 1) continue;   // (solid translucent: the rasterizer draws nothing)
			bool finite = true;
			for (int k = 0; k < p.n; k++) finite = finite && std::isfinite(p.x[k]) && std::isfinite(p.y[k]);
			if (!finite) continue;
			const long x0 = std::max<long>(0, long(p.vp[0]) * scale), x1 = std::min<long>(w, (long(p.vp[1]) + 1) * scale);
			const long y0 = std::max<long>(0, long(p.vp[2]) * scale), y1 = std::min<long>(h, (long(p.vp[3]) + 1) * scale);
			if (x1 <= x0 || y1 <= y0) continue;
			pinfo &o = m_info[i];
			const bool tex = p.renderer & 2;
			o.flags = (p.checker ? 1u : 0u) | (tex ? 2u : 0u) | ((p.renderer & 1) ? 4u : 0u) | (p.sheet ? 8u : 0u);
			o.colour = p.colour;
			o.n = p.n;
			o.clipx = int32_t(x0); o.clipx1 = int32_t(x1);
			if (tex) {
				o.pal = p.pal; o.lumabase = p.lumabase; o.luma = p.luma; o.texlod = p.texlod;
				o.texw = p.texw; o.texh = p.texh; o.texx = p.texx; o.texy = p.texy;
				o.wrapx = p.wrapx; o.wrapy = p.wrapy; o.mirrorx = p.mirrorx; o.mirrory = p.mirrory;
				o.utex = p.utex; o.utexminlod = p.utexminlod; o.utexx = p.utexx; o.utexy = p.utexy;
				o.max_level = 30 - __builtin_clz(std::max(1u, uint32_t(std::min(p.texw, p.texh))));
			}
			pverts &pv = m_pv[i];
			float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f;
			for (int k = 0; k < p.n; k++) {
				pv.x[k] = p.x[k] * s; pv.y[k] = p.y[k] * s;
				pv.p[k][0] = tex ? p.z[k] : 0; pv.p[k][1] = tex ? p.u[k] : 0; pv.p[k][2] = tex ? p.v[k] : 0;
				minx = std::min(minx, pv.x[k]); maxx = std::max(maxx, pv.x[k]);
				miny = std::min(miny, pv.y[k]); maxy = std::max(maxy, pv.y[k]);
			}
			if (ex) {
				// its rows (poly.h's: from the top vertex's, rounded, to the bottom's) in its window
				const int ya = std::max<int>(round_coordinate(miny), int(y0)), yb = std::min<int>(round_coordinate(maxy), int(y1));
				if (yb <= ya) continue;
				o.y0 = ya;
				o.rowbase = rows;
				m_jobpoly.push_back(uint32_t(i));
				m_jobbase.push_back(rows);
				rows += uint32_t(yb - ya);
				if (tex) {
					// the parameters kept every CK pixels drawn: a row's span lies within the vertices' columns
					const long xa = std::max<long>(x0, long(std::floor(minx)) - 1), xb = std::min<long>(x1, long(std::ceil(maxx)) + 2);
					const long px = std::max<long>(1, (xb - xa) / (p.checker ? 2 : 1) + 1);
					o.ckstride = uint32_t(px / CK + 1);
					o.ckbase = cks;
					cks += o.ckstride * uint32_t(yb - ya);
				}
				continue;
			}
			const MTLScissorRect sc = { NSUInteger(x0), NSUInteger(y0), NSUInteger(x1 - x0), NSUInteger(y1 - y0) };
			if (runs.empty() || memcmp(&runs.back().sc, &sc, sizeof sc)) runs.push_back({ m_verts.size(), 0, sc });
			if (tex && spans) o.flags |= spans == 2 ? 48u : 16u;
			auto vert = [&](int k) { m_verts.push_back({ pv.x[k], pv.y[k], tex ? p.z[k] : 1.0f, tex ? p.u[k] : 0.0f, tex ? p.v[k] : 0.0f, uint32_t(i) }); };
			for (int k = 1; k + 1 < p.n; k++) { vert(0); vert(k); vert(k + 1); }
			runs.back().count = m_verts.size() - runs.back().first;
		}
		auto put = [&](id<MTLBuffer> __strong &b, const void *src, size_t n) {
			n = std::max<size_t>(n, 16);
			if (!b || b.length < n) b = [m_dev newBufferWithLength:n * 3 / 2 options:MTLResourceStorageModeShared];
			if (src) memcpy(b.contents, src, n);
		};
		put(m_ibuf, m_info.data(), m_info.size() * sizeof(pinfo));
		put(m_pbuf, m_pv.data(), m_pv.size() * sizeof(pverts));
		if (ex) {
			put(m_jpbuf, m_jobpoly.data(), m_jobpoly.size() * 4);
			put(m_jbbuf, m_jobbase.data(), m_jobbase.size() * 4);
			auto priv = [&](id<MTLBuffer> __strong &b, size_t n) {
				n = std::max<size_t>(n, 16);
				if (!b || b.length < n) b = [m_dev newBufferWithLength:n * 3 / 2 options:MTLResourceStorageModePrivate];
			};
			priv(m_rows, size_t(rows) * sizeof(rowrec));
			priv(m_ck, size_t(cks) * 12);
			for (int rep = 0; rep < std::max(1, test_repeat); rep++) {   // (tests: a slow GPU, the picture drawn n times)
			id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
			[bl fillBuffer:m_owner_buf range:NSMakeRange(0, size_t(w) * h * 4) value:0xff];
			[bl endEncoding];
			id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
			const uint32_t W = uint32_t(w), H = uint32_t(h);
			if (rows) {
				const uint32_t k[3] = { uint32_t(m_jobpoly.size()), W, H };
				[ce setComputePipelineState:m_k_spans];
				[ce setBytes:k length:sizeof k atIndex:0];
				[ce setBuffer:m_ibuf offset:0 atIndex:1];
				[ce setBuffer:m_pbuf offset:0 atIndex:2];
				[ce setBuffer:m_jpbuf offset:0 atIndex:3];
				[ce setBuffer:m_jbbuf offset:0 atIndex:4];
				[ce setBuffer:m_rows offset:0 atIndex:5];
				[ce setBuffer:m_ck offset:0 atIndex:6];
				[ce setBuffer:m_owner_buf offset:0 atIndex:7];
				[ce setBuffer:m_ram[0] offset:0 atIndex:8];
				[ce setBuffer:m_ram[1] offset:0 atIndex:9];
				[ce setBytes:&rows length:4 atIndex:10];
				const NSUInteger tg = std::min<NSUInteger>(64, m_k_spans.maxTotalThreadsPerThreadgroup);
				[ce dispatchThreadgroups:MTLSizeMake((rows + tg - 1) / tg, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
				// A separate pass lets Metal's tracked resources synchronize spans
				// with shading. The iOS simulator rejects in-encoder memory barriers;
				// separate passes also work on devices without that capability.
				[ce endEncoding];
				ce = [cb computeCommandEncoder];
			}
			const uint32_t k2[2] = { W, H };
			[ce setComputePipelineState:m_k_shade];
			[ce setBytes:k2 length:sizeof k2 atIndex:0];
			[ce setBuffer:m_ibuf offset:0 atIndex:1];
			[ce setBuffer:m_rows offset:0 atIndex:5];
			[ce setBuffer:m_ck offset:0 atIndex:6];
			[ce setBuffer:m_owner_buf offset:0 atIndex:7];
			for (int b = 0; b < 5; b++) [ce setBuffer:m_ram[b] offset:0 atIndex:8 + b];
			[ce setBuffer:m_cbuf offset:0 atIndex:13];
			dispatch2(ce, m_k_shade, w, h);
			[ce endEncoding];
			}
			return true;
		}
		put(m_vbuf, m_verts.data(), m_verts.size() * sizeof(vin));
		MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
		rp.colorAttachments[0].texture = m_owner_tex;
		rp.colorAttachments[0].loadAction = MTLLoadActionClear;
		rp.colorAttachments[0].clearColor = MTLClearColorMake(4294967295.0, 0, 0, 0);
		rp.colorAttachments[0].storeAction = MTLStoreActionStore;
		rp.colorAttachments[1].texture = m_rgba;
		rp.colorAttachments[1].loadAction = MTLLoadActionClear;
		rp.colorAttachments[1].clearColor = MTLClearColorMake(0, 0, 0, 0);
		rp.colorAttachments[1].storeAction = MTLStoreActionStore;
		rp.depthAttachment.texture = m_depth;
		rp.depthAttachment.loadAction = MTLLoadActionClear;
		rp.depthAttachment.clearDepth = 1.0;
		rp.depthAttachment.storeAction = MTLStoreActionDontCare;
		for (int rep = 0; rep < std::max(1, test_repeat); rep++) {
			id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
			[enc setRenderPipelineState:m_pipe];
			[enc setDepthStencilState:m_depth_state];
			[enc setCullMode:MTLCullModeNone];
			[enc setVertexBuffer:m_vbuf offset:0 atIndex:0];
			const float k[4] = { float(w), float(h), 1.0f / float(count + 2), 0 };
			[enc setVertexBytes:k length:sizeof k atIndex:1];
			[enc setFragmentBuffer:m_ibuf offset:0 atIndex:0];
			for (int b = 0; b < 5; b++) [enc setFragmentBuffer:m_ram[b] offset:0 atIndex:1 + b];
			[enc setFragmentBuffer:m_pbuf offset:0 atIndex:6];
			for (const run &r : runs) {
				[enc setScissorRect:r.sc];
				[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:r.first vertexCount:r.count];
			}
			[enc endEncoding];
		}
		id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
		const uint32_t wh[2] = { uint32_t(w), uint32_t(h) };
		[ce setComputePipelineState:m_k_from_tex];
		[ce setTexture:m_rgba atIndex:0];
		[ce setBuffer:m_cbuf offset:0 atIndex:0];
		[ce setBytes:wh length:sizeof wh atIndex:1];
		dispatch2(ce, m_k_from_tex, w, h);
		[ce endEncoding];
		return true;
	}

	// The RAMs copied to the GPU, those whose count of changes has moved since (all the first
	// time); gamma each time. (Shared buffers: the GPU is idle here, the last picture finished.)
	void upload_rams()
	{
		const void *src[5] = { m_rams.tex[0], m_rams.tex[1], m_rams.luma, m_rams.xlat, m_rams.gamma };
		for (int k = 0; k < 5; k++) {
			if (!src[k] || (k < 4 && m_uploaded[k] && m_gen[k] == m_rams.gen[k])) continue;
			memcpy(m_ram[k].contents, src[k], m_ram[k].length);
			m_gen[k] = m_rams.gen[k];
			m_uploaded[k] = true;
		}
	}
	bool buffers(int w, int h)
	{
		if (m_bw == w && m_bh == h) return true;
		auto make = [&](MTLPixelFormat f, MTLTextureUsage u) {
			MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:w height:h mipmapped:NO];
			d.usage = u;
			d.storageMode = MTLStorageModePrivate;
			return [m_dev newTextureWithDescriptor:d];
		};
		m_owner_tex = make(MTLPixelFormatR32Uint, MTLTextureUsageRenderTarget);
		m_rgba = make(MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
		m_depth = make(MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget);
		const size_t n = size_t(w) * h * 4;
		m_owner_buf = [m_dev newBufferWithLength:n options:MTLResourceStorageModePrivate];
		m_cbuf = [m_dev newBufferWithLength:n options:MTLResourceStorageModePrivate];
		if (!m_owner_tex || !m_rgba || !m_depth || !m_owner_buf || !m_cbuf) { m_bw = m_bh = 0; return false; }
		m_bw = w; m_bh = h;
		return true;
	}

	id<MTLDevice> m_dev;
	id<MTLCommandQueue> m_queue = nil;
	id<MTLRenderPipelineState> m_pipe = nil;
	id<MTLComputePipelineState> m_k_spans = nil, m_k_shade = nil, m_k_out = nil, m_k_edge = nil, m_k_from_tex = nil;
	id<MTLDepthStencilState> m_depth_state = nil;
	id<MTLTexture> m_owner_tex = nil, m_rgba = nil, m_depth = nil;
	id<MTLBuffer> m_owner_buf = nil, m_cbuf = nil;
	int m_bw = 0, m_bh = 0;
	id<MTLBuffer> m_vbuf = nil, m_ibuf = nil, m_pbuf = nil, m_jpbuf = nil, m_jbbuf = nil, m_rows = nil, m_ck = nil;
	id<MTLBuffer> m_rb_owner = nil, m_rb_rgba = nil;
	id<MTLBuffer> m_bd = nil, m_mid = nil;
	uint64_t m_v2d = ~0ull;
	int m_w2d = 0;
	id<MTLBuffer> m_ram[5] = {};
	gpu3d_rams m_rams;
	uint32_t m_gen[5] = {};
	bool m_uploaded[5] = {};
	std::vector<vin> m_verts;
	std::vector<pinfo> m_info;
	std::vector<pverts> m_pv;
	std::vector<uint32_t> m_jobpoly, m_jobbase;
	struct tgt { CVPixelBufferRef pb; id<MTLTexture> tex; };
	std::vector<tgt> m_tg;
	int m_tw = 0, m_th = 0;
	id<MTLCommandBuffer> m_cb = nil;
	double m_cpu_ms = 0, m_last_gpu_ms = 0;
	bool m_last_ok = true;
	gpu3d_quality m_quality = gpu3d_quality::exact;
	// GPU3D_SPANS (tests: the triangles' ways, in place of the exact way): 1, the rasterizer's own
	// row starts, stepped a pixel at a time (step 2's); 2, its row starts, then stepped to the
	// pixel at once; 0, interpolated across the triangle (the fast way's)
	int m_spans = 0;
	bool m_spans_forced = false;
};

}

std::unique_ptr<gpu3d> gpu3d_create_metal(void *mtl_device)
{
	@autoreleasepool {
	id<MTLDevice> dev = mtl_device ? (__bridge id<MTLDevice>)mtl_device : MTLCreateSystemDefaultDevice();
	if (!dev) return nullptr;
	auto g = std::make_unique<gpu3d_metal>(dev);
	if (!g->init()) return nullptr;
	return g;
	}
}

// The start's head start: the system's device's renderer made (its shaders compiled from source,
// a good half second the first time a build runs, cached by Metal after) on another thread while
// the game boots, taken by gpu3d_create_metal_layer if the host's renderer draws on that device.
namespace {
std::mutex g_pre_mutex;
std::condition_variable g_pre_cv;
bool g_pre_started = false, g_pre_done = false;
std::unique_ptr<gpu3d> g_pre;
id<MTLDevice> g_pre_dev = nil;
}

void gpu3d_metal_prewarm()
{
	{
		std::lock_guard<std::mutex> l(g_pre_mutex);
		if (g_pre_started) return;
		g_pre_started = true;
	}
	std::thread([] {
		@autoreleasepool {
		id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
		std::unique_ptr<gpu3d> g = dev ? gpu3d_create_metal((__bridge void *)dev) : nullptr;
		std::lock_guard<std::mutex> l(g_pre_mutex);
		g_pre = std::move(g);
		g_pre_dev = dev;
		g_pre_done = true;
		g_pre_cv.notify_all();
		}
	}).detach();
}

std::unique_ptr<gpu3d> gpu3d_create_metal_layer(void *ca_metal_layer)
{
	@autoreleasepool {
	CAMetalLayer *layer = (__bridge CAMetalLayer *)ca_metal_layer;
	id<MTLDevice> dev = layer ? layer.device : nil;
	if (!dev) return nullptr;
	{
		std::unique_lock<std::mutex> l(g_pre_mutex);
		if (g_pre_started) {
			g_pre_cv.wait(l, [] { return g_pre_done; });
			if (g_pre && g_pre_dev == dev) return std::move(g_pre);
			g_pre.reset();
		}
	}
	return gpu3d_create_metal((__bridge void *)dev);
	}
}
