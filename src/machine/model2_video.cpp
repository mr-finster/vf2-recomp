// license:BSD-3-Clause
// copyright-holders:R. Belmont, Olivier Galibert, ElSemi, Angelo Salese, Matthew Daniels
// Sega Model 2 geometrizer (the Model 2/2A HLE of it), 3D rasterizer and polygon renderer.
// MAME's sega/model2_v.cpp and sega/model2rd.ipp (BSD-3-Clause, copyright-holders: R. Belmont,
// Olivier Galibert, ElSemi, Angelo Salese, Matthew Daniels), with model2_state's video members
// moved into model2_video. The functions are MAME's; only the framework around them changed.
// MAME's notes on how the hardware works are kept in the comments below.

/*********************************************************************************************************************************

    Sega Model 2 Geometry Engine and 3D Rasterizer Emulation

    General Notes:

    - The 3D rendering system has 3 main parts:
        1) The Geometry Engine: Model 2B and 2C hardware upload the geometry code to a DSP. The original and 2A hardware
        have the geometry code in an internal ROM inside the DSP itself. The simulation written here for the geometry engine
        is based off the disassembly of the code uploaded to a 2B game.
        2) The Z-Sort and Clip Hardware: Not much information about this hardware. There are a couple of variables that
        can be passed to this stage, like a master Z-Clip enable register, and a Z-Sort mode variable.
        3) The Hardware Renderer: Not much information about the inner workings of this hardware. Based on the available
        tidbits from the 2B manual, it is a double-buffered device (can be toggled between page flipping and single
        buffer mode), and can also be switched to generate output at 60Hz or 30Hz.

    - Most of the information used to write this code come from 3 main sources:
        1) The Geometry Engine code disassembly from a 2B game.
        2) The Model 2B-CRX Manual.
        3) ElSemi's Direct3D implementation of the geometrizer engine.

    - The emulation strategy used here is to decouple the geometrizer code from the Z-Sort/Clip/Renderer code, so that for
    original and 2A games, the HLE version of the geometrizer can be used, while for 2B and 2C games, the original geometrizer
    DSP can be emulated, and data can be pushed to the Z-Sort/Clip/Renderer code.



    Geometry Engine Notes and Known Bugs:

    - Top Skater seem to use a slightly different geometry code that has a secondary transformation matrix. Once we implement
    the real DSP code upload and emulation for 2B and 2C games, this should not be an issue.



    Z-Sort Notes and Known Bugs:

    - The Z-Sort algorithm is not entirely understood. The manual states that the Z-Sort works internally in 1.4.8 bit format,
    but then it states that the Z-Sort Mode register value is added to the floating point z value, and converted to a 16 bit
    format (most likely 4.12).
    - The system allows for Z-Sort override, by means of specifying whether a polygon will use the same Z value ordinal as the
    previous polygon, or the calculated minimum or maximum from it's points. This allows for a full object to be in front of
    another, even though the first object might have some z coordinates that are bigger than the second object's z coordinates.
    - Polygons from later windows will always be rendered on top of polygons from earlier windows, regardless of z value. This
    can be seen in the name entry screen in Sega Rally for example, where letters meant to be behind the car are rendered in an
    earlier window and letters in front of the car are rendered in a later window.
    - The current implementation takes the effective computed z value for the polygon and converts it into a 4.12 fixed point
    representation, used as an index into an array of linked polygons. Every polygon with the same z value is linked with any
    previous polygon that had that same z value:

        z-value index   linked list of polygons
            0000        polygon3->polygon2->polygon1
            0001        polygon22->polygon8->polygon7
            ....
            FFFF        polygon33->polygon11->polygon9

    As we add polygons to the array, we also keep track of the minimum and maximum z-value indexes seen this frame.
    When it's time to render, we start and the max z-value seen, and iterate through the array going down to the minimum
    z-value seen, clipping and rendering the linked polygons as we go.



    Clip Notes and Known Bugs:

    - Four clipping planes representing the viewing frustum are used to clip polygons prior to projection. In the rare event
    that a polygon intersects with the origin exactly, it will produce a clipped vertex at (0.0, 0.0, 0,0) which would result
    in NaNs being generated during projection; a tiny value is added to z to prevent this.
    - A small offset need to be added horizontally and vertically to the viewport and center variables for certain games (like
    the original Model 2 games). The coordinate system has been worked out from the 2B specifications, but the older games
    need a slight adjustment.



    Hardware Renderer Notes and Known Bugs:

    - Texturing code could use a real good speed optimization.
    - The U and V coordinates provided by the game are in 13.3 fixed point format.
    - The log tables and distance coefficients are used to calculate the number of texels per world unit that need to
    be used to render a texture. Textures can also be provided with smaller levels of details and a LOD bit selector
    in the texture header tells the rasterizer which texture map to use. The rasterizer then can average two texture
    maps to do mip mapping. More information can be found on the 2B manual, on the 'Texturing' and 'Data Format' chapters.
    - The rasterizer supports 128x128 'microtextures' which are typically used to add more details to a texture when it is
    close enough to the viewer.
    - Polygons are rendered from front-to-back. A fill buffer is used to track which pixels in the framebuffer have already
    been drawn to and prevents them from being overwritten. The real hardware does this to ensure that the polygons closest
    to the camera are rendered first, in case the renderer runs out of time and has to skip ahead to the next frame.

*********************************************************************************************************************************/

#include "model2_video.h"
#include "parallel.h"
#include "texdump.h"
#include "../gpu/gpu3d.h"

#include <bit>
#include <chrono>
#include <cmath>
#include <limits>

#define pz      p[0]
#define pu      p[1]
#define pv      p[2]



/*******************************************
 *
 *  Generic 3D Math Functions
 *
 *******************************************/

static inline void transform_point(poly_vertex *point, float *matrix)
{
	float tx = (point->x * matrix[0]) + (point->y * matrix[3]) + (point->pz * matrix[6]) + (matrix[9]);
	float ty = (point->x * matrix[1]) + (point->y * matrix[4]) + (point->pz * matrix[7]) + (matrix[10]);
	float tz = (point->x * matrix[2]) + (point->y * matrix[5]) + (point->pz * matrix[8]) + (matrix[11]);

	point->x = tx;
	point->y = ty;
	point->pz = tz;
}

static inline void transform_vector(poly_vertex *vector, float *matrix)
{
	float tx = (vector->x * matrix[0]) + (vector->y * matrix[3]) + (vector->pz * matrix[6]);
	float ty = (vector->x * matrix[1]) + (vector->y * matrix[4]) + (vector->pz * matrix[7]);
	float tz = (vector->x * matrix[2]) + (vector->y * matrix[5]) + (vector->pz * matrix[8]);

	vector->x = tx;
	vector->y = ty;
	vector->pz = tz;
}

static inline void normalize_vector(poly_vertex *vector)
{
	const float n = sqrt((vector->x * vector->x) + (vector->y * vector->y) + (vector->pz * vector->pz));

	if (n)
	{
		float oon = 1.0f / n;
		vector->x *= oon;
		vector->y *= oon;
		vector->pz *= oon;
	}
}

static inline float dot_product(const poly_vertex &v1, const poly_vertex &v2)
{
	return (v1.x * v2.x) + (v1.y * v2.y) + (v1.pz * v2.pz);
}

static inline void vector_cross3(poly_vertex *dst, const poly_vertex *v0, const poly_vertex *v1, const poly_vertex *v2)
{
	poly_vertex p1, p2;

	p1.x = v1->x - v0->x;   p1.y = v1->y - v0->y;   p1.pz = v1->pz - v0->pz;
	p2.x = v2->x - v0->x;   p2.y = v2->y - v0->y;   p2.pz = v2->pz - v0->pz;

	dst->x = (p1.y * p2.pz) - (p1.pz * p2.y);
	dst->y = (p1.pz * p2.x) - (p1.x * p2.pz);
	dst->pz = (p1.x * p2.y) - (p1.y * p2.x);
}

static inline void apply_focus(model2_video::geo_state *geo, poly_vertex *p0)
{
	p0->x *= geo->focus.x;
	p0->y *= geo->focus.y;
}

/* 1.8.23 float to 4.12 float converter, courtesy of Aaron Giles */
inline u16 model2_video::float_to_zval(float floatval, s32 z_adjust)
{
	s32 fpint = f2u(floatval);
	s32 exponent = ((fpint >> 23) & 0xff) - ((z_adjust >> 23) & 0xff);
	u32 mantissa = fpint & 0x7fffff;

	/* round the low bits and reduce to 12 */
	mantissa += 0x400;
	if (mantissa > 0x7fffff)
	{
		exponent++;
		mantissa = (mantissa & 0x7fffff) >> 1;
	}
	mantissa >>= 11;

	// if negative, clamp to 0
	if (fpint < 0)
		return 0x0000;

	// the rest depends on the exponent
	if (exponent < -12)
		return 0x0000; // less than -12 is too small, return 0
	else if (exponent < 0)
		return (mantissa | 0x1000) >> -exponent; // between -12 and 0 create a denormal with exponent of 0
	else if (exponent < 15)
		return ((exponent + 1) << 12) | mantissa; // between 0 and 14 create a FP value with exponent + 1
	else
		return 0xffff; // above 14 is too large
}

static int32_t clip_polygon(poly_vertex *v, int32_t num_vertices, poly_vertex *vout, model2_video::plane clip_plane)
{
	int32_t outcount = 0;

	const poly_vertex *cur = v;
	poly_vertex *out = vout;

	float curdot = dot_product(*cur, clip_plane.normal);
	int32_t curin = (curdot >= clip_plane.distance) ? 1 : 0;

	for (int32_t i = 0; i < num_vertices; i++)
	{
		const int32_t nextvert = (i + 1) % num_vertices;

		/* if the current point is inside the plane, add it */
		if (curin)
			out[outcount++] = *cur;

		const float nextdot = dot_product(v[nextvert], clip_plane.normal);
		const int32_t nextin = (nextdot >= clip_plane.distance) ? 1 : 0;

		/* Add a clipped vertex if one end of the current edge is inside the plane and the other is outside */
		if ((curin != nextin) && !std::isnan(curdot) && !std::isnan(nextdot))
		{
			const float scale = (clip_plane.distance - curdot) / (nextdot - curdot);

			out[outcount].x = cur->x + ((v[nextvert].x - cur->x) * scale);
			out[outcount].y = cur->y + ((v[nextvert].y - cur->y) * scale);
			out[outcount].pz = cur->pz + ((v[nextvert].pz - cur->pz) * scale);
			out[outcount].pu = cur->pu + ((v[nextvert].pu - cur->pu) * scale);
			out[outcount].pv = cur->pv + ((v[nextvert].pv - cur->pv) * scale);
			outcount++;
		}

		curdot = nextdot;
		curin = nextin;
		cur++;
	}

	return outcount;
}

// Widescreen: the sort key of a polygon that shows only in the side columns, from its part on the
// screen. Model 2 sorts each polygon by one depth, the nearest or farthest of its vertices (or the
// one before's), as it comes, whole. At the far sides of a wide view a long polygon there (the
// hedges around the stone towers' stage, a ring of long blocks behind the towers) has its other
// end well off the screen and near the camera's side: sorted by that end, it was drawn over the
// towers in front of it (at 32:9 about 9,000 pixels a frame of that stage's fight). In the 4:3
// the arcade shows, no polygon's off-screen part reaches so near. So a polygon all in the side
// columns (its vertices, clipped to the screen, outside the 4:3's) is sorted by its clipped
// vertices' nearest or farthest instead, as its mode asks; anything that shows in the 4:3 keeps
// the arcade's key, so the 4:3 part of the picture is as it was. (The key carried on to the next
// polygons, polygon_z, stays the arcade's.)
static float side_sort_z(const model2_video::raster_state *raster, int ext, u32 attr, float zvalue, const poly_vertex *v, int n, bool see_through)
{
	const int mode = (attr >> 10) & 3;
	if (!raster->widened || (mode != 1 && mode != 2)) return zvalue;
	// the 4:3's columns, as offsets x/z from the centre
	const float c = raster->center[raster->center_sel][0];
	const float lo = raster->viewport[0] + ext - c, hi = raster->viewport[2] - ext - c;
	bool left = true, right = true;
	float zmin = v[0].pz, zmax = v[0].pz;
	for (int i = 0; i < n; i++) {
		const float dx = v[i].x / (v[i].pz + std::numeric_limits<float>::min());
		left = left && dx <= lo;
		right = right && dx >= hi;
		zmin = std::min(zmin, v[i].pz);
		zmax = std::max(zmax, v[i].pz);
	}
	if (!left && !right) return zvalue;
	// A see-through polygon of the stage's far scenery (beyond 15, the fighters well in front of
	// it) that faces the camera (its depth across it under a tenth of its depth: a tree's leaves)
	// goes no nearer than its middle: by its nearest corner, Lau's tree and the tower beside it took
	// turns in front, frame to frame, as their corners' depths crossed (the arcade's 4:3 never shows
	// them). Never nearer than the arcade's key, and nothing solid moves: by their middles, Lion's
	// guardian statues went behind the wall and in front of the railing.
	const float key = mode == 1 ? std::max(zvalue, zmin) : std::min(zvalue, zmax);
	float sum = 0;
	for (int i = 0; i < n; i++) sum += v[i].pz;
	const float mean = sum / float(n);
	if (see_through && mean > 15.0f && zmax - zmin < 0.1f * mean) return std::max(key, mean);
	return key;
}

inline bool model2_video::check_culling(raster_state *raster, u32 attr, float min_z, float max_z)
{
	/* if doubleside is disabled */
	if (((attr >> 17) & 1) == 0)
	{
		/* if it's the backface, cull it */
		if (raster->command_buffer[9] & 0x00800000)
			return true;
	}

	/* if the linktype is 0, then we can also cull it */
	if (((attr >> 8) & 3) == 0)
		return true;

	/* if the minimum z value is bigger than the master z clip value, don't render */
	if (raster->master_z_clip != 0xff && (int32_t)(1.0 / min_z) > raster->master_z_clip)
		return true;

	/* if the maximum z value is < 0 then we can safely clip the entire polygon */
	if (max_z < 0)
		return true;

	return false;
}

/***********************************************************************************************/

/*******************************************
 *
 *  Hardware 3D Rasterizer Initialization
 *
 *******************************************/


/*******************************************
 *
 *  Setup (MAME's video_start, raster_init and geo_init)
 *
 *******************************************/

model2_video::model2_video(u32 *bufferram, u32 *textureram0, u32 *textureram1, u8 *lumaram, u16 *palram,
						   u16 *colorxlat, u8 *gamma, const u8 *texture_rom, size_t texture_rom_bytes,
						   const u8 *polygon_rom, size_t polygon_rom_bytes)
	: m_bufferram(bufferram), m_textureram0(textureram0), m_textureram1(textureram1), m_lumaram(lumaram),
	  m_palram(palram), m_colorxlat(colorxlat), m_gamma_table(gamma)
{
	m_renderer = std::make_unique<model2_renderer>(*this);
	m_renderer->allocate(1, 0);
	m_pictures = std::make_unique<picture3d[]>(4);   // (3: smooth motion's)

	m_raster = std::make_unique<raster_state>();
	m_raster->texture_rom = (u16 *)texture_rom;
	m_raster->texture_rom_mask = (texture_rom_bytes / 2) - 1;

	m_geo = std::make_unique<geo_state>();
	m_geo->state = this;
	m_geo->raster = m_raster.get();
	m_geo->polygon_rom = (u32 *)polygon_rom;
	m_geo->polygon_rom_mask = (polygon_rom_bytes / 4) - 1;
}

model2_video::~model2_video() = default;

void model2_video::model2_3d_zclip_w(u32 data)
{
	// setting this register to 0xff disables z-clip
	m_raster->master_z_clip = data;
}

// TODO: only Sky Target seems to use this for unknown purpose
u32 model2_video::polygon_count_r()
{
	m_polygon_count_reads++;
	return m_raster->poly_list_index;
}

void model2_video::get_regs(int16_t &xoffset, int16_t &yoffset, u8 &zclip) const
{
	xoffset = m_crtc_xoffset;
	yoffset = m_crtc_yoffset;
	zclip = m_raster->master_z_clip;
}

void model2_video::set_regs(int16_t xoffset, int16_t yoffset, u8 zclip)
{
	m_crtc_xoffset = xoffset;
	m_crtc_yoffset = yoffset;
	m_renderer->set_xoffset(xoffset);
	m_renderer->set_yoffset(yoffset);
	m_raster->master_z_clip = zclip;
}

void model2_video::power_on()
{
	u16 *const texture_rom = m_raster->texture_rom;
	const u32 texture_mask = m_raster->texture_rom_mask;
	u32 *const polygon_rom = m_geo->polygon_rom;
	const u32 polygon_mask = m_geo->polygon_rom_mask;
	m_raster = std::make_unique<raster_state>();
	m_raster->texture_rom = texture_rom;
	m_raster->texture_rom_mask = texture_mask;
	m_geo = std::make_unique<geo_state>();
	m_geo->state = this;
	m_geo->raster = m_raster.get();
	m_geo->polygon_rom = polygon_rom;
	m_geo->polygon_rom_mask = polygon_mask;
	m_geo_read_start_address = 0;
	set_regs(0, 0, 0);
	m_render_done = false;
	m_render_test_mode = false;
	std::fill(std::begin(m_shown), std::end(m_shown), -1);
	for (int p = 0; p < 4; p++) m_pictures[p].deferred = false;
	m_deferred = false;
	smooth_reset();
}

// What screen_update draws between the tile layers' two passes, worked out when MAME does it
// (at the end of vblank, every frame), for composite() to put on the screen.
void model2_video::frame_end(bool render_test_mode, u64 frame_number)
{
	m_frame_number = frame_number;
	m_render_test_mode = render_test_mode;
	if (render_test_mode)
		return;
	render_polygons();
}

// The 3D layer over the screen (transparent pen 0), or the framebuffer in render test mode.
// The cliprect is in screen pixels (the scale's).
void model2_video::composite(bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	if (m_render_test_mode)
		draw_framebuffer(bitmap, cliprect);
	else if (const bitmap_rgb32 *layer3d = layer()) {
		const bitmap_rgb32 &src = *layer3d;
		parallel_rows(cliprect.height(), [&](int r0, int r1) {
			for (int y = cliprect.min_y + r0; y < cliprect.min_y + r1; y++) {
				const u32 *s = &src.pix(y);
				u32 *d = &bitmap.pix(y);
				for (int x = cliprect.min_x; x <= cliprect.max_x; x++)
					if (const u32 p = s[x]; p != 0x00000000) d[x] = p;
			}
		});
	}
}

const bitmap_rgb32 *model2_video::layer()
{
	const int p = shown_now();
	if (m_render_test_mode || p < 0) return nullptr;
	if (m_pictures[p].deferred) draw_picture(p);
	return p == 0 ? &m_renderer->destmap() : &m_pictures[p].map;
}

int model2_video::layer_to_draw(const bitmap_rgb32 *&map)
{
	const int p = shown_now();
	map = nullptr;
	if (m_render_test_mode || p < 0) return -1;
	map = p == 0 ? &m_renderer->destmap() : &m_pictures[p].map;
	if (!m_pictures[p].deferred) return -1;
	m_pictures[p].deferred = false;
	m_deferred = m_pictures[0].deferred || m_pictures[1].deferred || m_pictures[2].deferred;
	return p;
}

void model2_video::draw_layer_into(int picture, u32 *out, size_t stride, int y0, int y1)
{
	bitmap_rgb32 into;
	into.wrap(out, (496 + 2 * m_ext) * m_scale, 384 * m_scale, stride);
	raster_drawn(picture, &into, y0, y1);
}

void model2_video::drawn_rows(const bitmap_rgb32 *map, int &y0, int &y1) const
{
	const int s = m_scale, h = 384 * s;
	y0 = 0; y1 = h;
	int p = -1;
	for (int i = 0; i < 4; i++)
		if (map == (i == 0 ? &m_renderer->destmap() : &m_pictures[i].map)) p = i;
	if (p < 0) return;
	float lo = 1e30f, hi = -1e30f;
	for (const drawn_poly &d : m_pictures[p].drawn) {
		if (!std::isfinite(d.miny) || !std::isfinite(d.maxy)) return;
		lo = std::min(lo, std::max(d.miny, float(d.vp.min_y)));
		hi = std::max(hi, std::min(d.maxy, float(d.vp.max_y + 1)));
	}
	if (lo > hi) { y0 = y1 = 0; return; }
	y0 = std::clamp((int(std::floor(lo)) - 1) * s, 0, h);
	y1 = std::clamp((int(std::ceil(hi)) + 2) * s, y0, h);
}

int model2_video::polygons_shown() const
{
	const int p = shown_now();
	return !m_render_test_mode && p >= 0 ? int(m_pictures[p].drawn.size()) : 0;
}

int model2_video::polygons_drawn(int ago) const
{
	const int p = m_shown[2 - std::clamp(ago, 0, 2)];
	return p >= 0 ? int(m_pictures[p].drawn.size()) : 0;
}

void model2_video::set_delay(int frames, bool cut)
{
	frames = std::clamp(frames, 0, 2);
	const bool changed = frames != m_delay;
	m_delay = frames;
	m_gradual = !cut;
	if (changed && cut) smooth_cut();   // (the 3D shown holds or skips a frame: no blending across it)
	for (int p = 1; p < 3; p++)
		if (m_delay && m_pictures[p].map.width() != m_renderer->destmap().width()) model2_renderer::allocate(m_pictures[p].map, m_scale, m_ext);
}

void model2_video::set_scale(int scale)
{
	scale = std::clamp(scale, 1, 8);
	if (scale == m_scale) return;
	m_scale = scale;
	m_renderer->allocate(scale, m_ext);
	for (int p = 1; p < 3; p++)
		if (m_pictures[p].map.width()) model2_renderer::allocate(m_pictures[p].map, scale, m_ext);
	// the pictures still to be shown, at the new size (a deferred one when it's shown); with the
	// lists kept, left to be drawn when shown, in their own colours (the board takes the RAMs back)
	for (int p = 0; p < 3; p++)
		if (!m_pictures[p].deferred && std::find(std::begin(m_shown), std::end(m_shown), p) != std::end(m_shown)) {
			if (m_smooth) m_pictures[p].deferred = m_deferred = true;
			else raster_drawn(p);
		}
}

void model2_video::set_ext(int ext)
{
	ext = std::clamp(ext, 0, 512);
	if (ext == m_ext) return;
	m_ext = ext;
	m_renderer->allocate(m_scale, ext);
	for (int p = 1; p < 3; p++)
		if (m_pictures[p].map.width()) model2_renderer::allocate(m_pictures[p].map, m_scale, ext);
	for (int p = 0; p < 4; p++) { m_pictures[p].drawn.clear(); m_pictures[p].deferred = false; }
	m_deferred = false;
	// with the lists kept (smooth motion's), the pictures to be shown are set up again at the new
	// width (rebuild_picture, the board's); else they're dropped
	if (!m_smooth) std::fill(std::begin(m_shown), std::end(m_shown), -1);
}

void model2_video::defer_drawing(bool on)
{
	m_defer = on;
	if (!on) draw_deferred();
}

// Before the RAM the polygons are coloured from changes: the deferred pictures that can still be
// shown drawn now (the one shown and the newer ones: the delay grows a frame at a time, so the
// picture shown never goes back), the others dropped.
void model2_video::draw_deferred()
{
	for (int k = 0; k < 3; k++) {
		const int p = m_shown[k];
		if (p >= 0 && m_pictures[p].deferred && k >= 2 - m_delay) draw_picture(p);
	}
	for (int p = 0; p < 3; p++) m_pictures[p].deferred = false;
	m_deferred = false;
}

bool model2_video::picture_deferred(int picture) const { return picture >= 0 && picture < 3 && m_pictures[picture].deferred; }
void model2_video::picture_drawn(int picture)
{
	m_pictures[picture].deferred = false;
	m_deferred = m_pictures[0].deferred || m_pictures[1].deferred || m_pictures[2].deferred;
}

void model2_video::draw_picture(int picture)
{
	raster_drawn(picture);
	m_pictures[picture].deferred = false;
	m_deferred = m_pictures[0].deferred || m_pictures[1].deferred || m_pictures[2].deferred;
}

/*******************************************
 *
 *  Hardware 3D Rasterizer Processing
 *
 *******************************************/

template <unsigned NumVerts>
void model2_video::model2_3d_process_polygon(raster_state *raster, u32 attr)
{
	quad_m2 object;
	u16 *th, *tp;
	int32_t tho;
	u32 i;
	bool cull;
	float zvalue;
	float min_z, max_z;

	static_assert(NumVerts == 3 || NumVerts == 4, "Polygon must have 3 or 4 vertices");

	/* extract P0(n-1) */
	object.v[1].x = u2f(raster->command_buffer[2] << 8);
	object.v[1].y = u2f(raster->command_buffer[3] << 8);
	object.v[1].pz = u2f(raster->command_buffer[4] << 8);

	/* extract P1(n-1) */
	object.v[0].x = u2f(raster->command_buffer[5] << 8);
	object.v[0].y = u2f(raster->command_buffer[6] << 8);
	object.v[0].pz = u2f(raster->command_buffer[7] << 8);

	/* extract P0(n) */
	object.v[2].x = u2f(raster->command_buffer[11] << 8);
	object.v[2].y = u2f(raster->command_buffer[12] << 8);
	object.v[2].pz = u2f(raster->command_buffer[13] << 8);

	if (NumVerts == 4)
	{
		/* extract P1(n) */
		object.v[3].x = u2f(raster->command_buffer[14] << 8);
		object.v[3].y = u2f(raster->command_buffer[15] << 8);
		object.v[3].pz = u2f(raster->command_buffer[16] << 8);
	}
	else
	{
		/* for triangles, the rope of P1(n) is achieved by P0(n-1) (linktype 3) */
		raster->command_buffer[14] = raster->command_buffer[11];
		raster->command_buffer[15] = raster->command_buffer[12];
		raster->command_buffer[16] = raster->command_buffer[13];
	}

	/* always calculate the min z and max z value */
	min_z = object.v[0].pz;
	if (object.v[1].pz < min_z) min_z = object.v[1].pz;
	if (object.v[2].pz < min_z) min_z = object.v[2].pz;
	if (NumVerts == 4 && object.v[3].pz < min_z) min_z = object.v[3].pz;

	max_z = object.v[0].pz;
	if (object.v[1].pz > max_z) max_z = object.v[1].pz;
	if (object.v[2].pz > max_z) max_z = object.v[2].pz;
	if (NumVerts == 4 && object.v[3].pz > max_z) max_z = object.v[3].pz;

	/* read in the texture information */

	/* texture point data */
	if (raster->command_buffer[0] & 0x800000)
		tp = &raster->texture_ram[raster->command_buffer[0] & 0xffff];
	else
		tp = &raster->texture_rom[raster->command_buffer[0] & raster->texture_rom_mask];

	object.v[0].pv = *tp++;
	object.v[0].pu = *tp++;
	object.v[1].pv = *tp++;
	object.v[1].pu = *tp++;
	object.v[2].pv = *tp++;
	object.v[2].pu = *tp++;
	if (NumVerts == 4)
	{
		object.v[3].pv = *tp++;
		object.v[3].pu = *tp++;
	}

	/* update the address */
	raster->command_buffer[0] += NumVerts * 2;

	/* texture header data */
	if (raster->command_buffer[1] & 0x800000)
		th = &raster->texture_ram[raster->command_buffer[1] & 0xffff];
	else
		th = &raster->texture_rom[raster->command_buffer[1] & raster->texture_rom_mask];

	object.texheader[0] = *th++;
	object.texheader[1] = *th++;
	object.texheader[2] = *th++;
	object.texheader[3] = *th++;

	/* extract the texture header offset */
	tho = (attr >> 12) & 0x1f;

	/* adjust for sign */
	if (tho & 0x10)
		tho |= -16;

	/* update the address */
	raster->command_buffer[1] += tho * 4;

	/* set the luma value of this polygon */
	object.luma = (raster->command_buffer[9] >> 15) & 0xff;

	/* set the texture LOD of this polygon */
	object.texlod = ((raster->command_buffer[10] >> 8) & 0x7f80) - 0x3f80;
	object.texlod += raster->log_ram[raster->command_buffer[10] & 0x7fff];

	/* determine whether we can cull this polygon */
	cull = check_culling(raster,attr,min_z,max_z);

	/* set the object's z value */
	switch ((attr >> 10) & 3)
	{
		case 0: // old value
			zvalue = raster->polygon_z;
			break;
		case 1: // min z
			zvalue = min_z;
			break;
		case 2: // max z
			zvalue = max_z;
			break;
		case 3: // error
		default:
			zvalue = 1e10;
			break;
	}

	raster->polygon_z = zvalue;

	if (cull == false)
	{
		int32_t clipped_verts;
		poly_vertex verts_in[8], verts_out[8];

		for (int i = 0; i < NumVerts; i++)
			verts_in[i] = object.v[i];

		clipped_verts = NumVerts;

		/* do clipping */
		for (int i = 0; i < 4; i++)
		{
			clipped_verts = clip_polygon(verts_in, clipped_verts, verts_out, raster->clip_plane[raster->center_sel][i]);
			for (int j = 0; j < clipped_verts; j++)
				verts_in[j] = verts_out[j];
		}

		if (clipped_verts > 2)
		{
			polygon *zpoly;

			/* adjust and set the object z-sort value */
			object.z = float_to_zval(side_sort_z(raster, m_ext, attr, zvalue, verts_out, clipped_verts, (object.texheader[0] >> 13 & 1) != 0), raster->z_adjust);

			/* get our list read to add the polygons */
			zpoly = raster->poly_sorted_list[object.z];

			/* go through the clipped vertex list, adding polygons */
			polygon *poly = &raster->poly_list[raster->poly_list_index++];

			if (raster->poly_list_index >= MAX_POLYGONS)
			{
				fatalerror("SEGA 3D: Max polygon limit exceeded\n");
			}

			/* copy the object information */
			poly->z = object.z;
			poly->texheader[0] = object.texheader[0];
			poly->texheader[1] = object.texheader[1];
			poly->texheader[2] = object.texheader[2];
			poly->texheader[3] = object.texheader[3];
			poly->luma = object.luma;
			poly->texlod = object.texlod;

			/* set the viewport */
			poly->viewport[0] = raster->viewport[0];
			poly->viewport[1] = raster->viewport[1];
			poly->viewport[2] = raster->viewport[2];
			poly->viewport[3] = raster->viewport[3];

			/* set the center */
			poly->center[0] = raster->center[raster->center_sel][0];
			poly->center[1] = raster->center[raster->center_sel][1];

			/* set the window */
			poly->window = raster->cur_window;

			poly->num_vertices = clipped_verts;

			for (int i = 0; i < clipped_verts; i++)
				poly->v[i] = verts_out[i];

			/* add to our sorted list */
			raster->poly_sorted_list[object.z] = poly;
			poly->next = zpoly;
			zpoly = poly;

			/* keep around the min and max z values for this frame */
			if (object.z < raster->min_z) raster->min_z = object.z;
			if (object.z > raster->max_z) raster->max_z = object.z;
		}
	}

	/* update linking */
	switch (((attr >> 8) & 3))
	{
		case 0:
		case 2:
		{
			/* reuse P0(n) and P1(n) */
			for (i = 0; i < 6; i++)                                        /* P0(n) -> P0(n-1) */
				raster->command_buffer[2+i] = raster->command_buffer[11+i]; /* P1(n) -> P1(n-1) */
		}
		break;

		case 1:
		{
			/* reuse P0(n-1) and P0(n) */
			for (i = 0; i < 3; i++)
				raster->command_buffer[5+i] = raster->command_buffer[11+i]; /* P0(n) -> P1(n-1) */
		}
		break;

		case 3:
		{
			/* reuse P1(n-1) and P1(n) */
			for (i = 0; i < 3; i++)
				raster->command_buffer[2+i] = raster->command_buffer[14+i]; /* P1(n) -> P1(n-1) */
		}
		break;
	}
}

/***********************************************************************************************/

/***********************************************************************************************/

// MAME's model2_3d_render up to the scan conversion, into `out` (which takes the vertices as
// model2_3d_project left them, and works out the textured ones' 1/z, u/z and v/z there)
void model2_renderer::prepare(polygon *poly, const rectangle &cliprect, model2_video::drawn_poly &out)
{
	out.extra = m2_poly_extra_data{};
	m2_poly_extra_data& extra = out.extra;

	/* select renderer based on attributes (bit14 = textured, bit13 = transparent) */
	u8 renderer = (poly->texheader[0] >> 13) & 3;
	out.renderer = renderer;

	/* calculate and clip to viewport */
	const int ext = m_state.m_ext;   // (widescreen: the arcade's column 0 at ext)
	rectangle vp(poly->viewport[0] + m_xoffs + ext, poly->viewport[2] + m_xoffs + ext, (384-poly->viewport[3]) + m_yoffs, (384-poly->viewport[1]) + m_yoffs);
	vp &= cliprect;
	out.vp = vp;
	out.num_vertices = poly->num_vertices;
	for (int i = 0; i < poly->num_vertices; i++)
		out.v[i] = poly->v[i];

	extra.state = &m_state;
	extra.checker = (poly->texheader[0] >> 15) & 1;
	extra.lumabase = (poly->texheader[1] & 0xff) << 7;
	extra.colorbase = (poly->texheader[3] >> 6) & 0x3ff;
	extra.luma = poly->luma;
	extra.texlod = poly->texlod;

	if (renderer & 2)
	{
		extra.texmirrorx = (poly->texheader[0] >> 8) & 1;
		extra.texmirrory = (poly->texheader[0] >> 9) & 1;

		// disable smooth wrapping if mirroring is enabled
		extra.texwrapx = (poly->texheader[0] >> 6) & 1 & ~extra.texmirrorx;
		extra.texwrapy = (poly->texheader[0] >> 7) & 1 & ~extra.texmirrory;

		extra.texsheet[0] = (poly->texheader[2] & 0x1000) ? m_state.m_textureram1 : m_state.m_textureram0;
		extra.texsheet[1] = (poly->texheader[2] & 0x1000) ? m_state.m_textureram0 : m_state.m_textureram1;
		extra.texwidth = 32 << ((poly->texheader[0] >> 0) & 0x7);
		extra.texheight = 32 << ((poly->texheader[0] >> 3) & 0x7);
		extra.texx = 32 * ((poly->texheader[2] >> 0) & 0x3f);
		extra.texy = 32 * ((poly->texheader[2] >> 6) & 0x1f);

		// microtexture parameters
		extra.utex = (poly->texheader[0] >> 12) & 1;
		extra.utexminlod = (poly->texheader[0] >> 10) & 3;
		extra.utexx = ((poly->texheader[2] >> 13) & 1) * 128;
		extra.utexy = ((poly->texheader[2] >> 14) & 3) * 128;

		for (int i = 0; i < poly->num_vertices; i++)
		{
			out.v[i].pz = 1.0f / (out.v[i].pz + std::numeric_limits<float>::min());
			out.v[i].pu = out.v[i].pu * out.v[i].pz * (1.0f / 8.0f);
			out.v[i].pv = out.v[i].pv * out.v[i].pz * (1.0f / 8.0f);
		}
	}

	// the rows it can reach, for skipping the bands it can't (none if a position isn't a number)
	out.miny = out.maxy = out.v[0].y;
	for (int i = 0; i < poly->num_vertices; i++) {
		const float y = out.v[i].y;
		if (std::isnan(y) || std::isnan(out.v[i].x)) { out.miny = -INFINITY; out.maxy = INFINITY; break; }
		out.miny = std::min(out.miny, y);
		out.maxy = std::max(out.maxy, y);
	}
}

// The scan conversion: the vertices and viewport scaled (a multiply by 1 at 1x: MAME's own
// positions), the rows outside `band` left to the other threads. A row's pixels are the same
// whichever rows are drawn with it: each is worked out from the vertices alone.
void model2_renderer::draw(const model2_video::drawn_poly &p, int scale, const rectangle &band)
{
	if (p.maxy * scale < band.min_y - 1 || p.miny * scale > band.max_y + 2)
		return;
	rectangle vp(p.vp.min_x * scale, p.vp.max_x * scale + scale - 1, p.vp.min_y * scale, p.vp.max_y * scale + scale - 1);
	vp &= band;
	if (vp.min_y > vp.max_y)
		return;
	poly_vertex v[8];
	const float s = float(scale);
	for (int i = 0; i < p.num_vertices; i++) {
		v[i] = p.v[i];
		v[i].x *= s;
		v[i].y *= s;
	}
	const render_delegate &cb = m_render_callbacks[p.renderer];
	switch (p.num_vertices)
	{
	case 3: render_triangle<3>(vp, cb, v[0], v[1], v[2], &p.extra); break;
	case 4: render_polygon<4, 3>(vp, cb, v, &p.extra); break;
	case 5: render_polygon<5, 3>(vp, cb, v, &p.extra); break;
	case 6: render_polygon<6, 3>(vp, cb, v, &p.extra); break;
	case 7: render_polygon<7, 3>(vp, cb, v, &p.extra); break;
	case 8: render_polygon<8, 3>(vp, cb, v, &p.extra); break;
	}
}

// The scan conversion of draw(), whole, its spans marking the pixels the polygon owns: those
// draw_scanline_* would fill (every other one for checker; none for solid translucent, which
// draws nothing) where no polygon before it has; a textured translucent one only where its texel
// is drawn (alpha half or more), as draw_scanline_tex<true> finds it.
constexpr u32 LERP(u32 x, u32 y, unsigned a);
inline s32 fast_log2(float value);

void model2_renderer::draw_owner(const model2_video::drawn_poly &p, int scale, u32 index, bitmap_rgb32 &owner)
{
	if (p.renderer == 1)
		return;
	rectangle vp(p.vp.min_x * scale, p.vp.max_x * scale + scale - 1, p.vp.min_y * scale, p.vp.max_y * scale + scale - 1);
	vp &= rectangle(0, owner.width() - 1, 0, owner.height() - 1);
	if (vp.min_y > vp.max_y || vp.min_x > vp.max_x)
		return;
	poly_vertex v[8];
	const float s = float(scale);
	for (int i = 0; i < p.num_vertices; i++) {
		v[i] = p.v[i];
		v[i].x *= s;
		v[i].y *= s;
	}
	const bool checker = p.extra.checker;
	const m2_poly_extra_data &object = p.extra;
	const s32 max_level = 30 - std::countl_zero(std::min(object.texwidth | 1, object.texheight | 1));
	const render_delegate cb = [&](int32_t y, const extent_t &e, const m2_poly_extra_data &, int) {
		u32 *const o = &owner.pix(y);
		int x = e.startx;
		const int dx = checker ? 2 : 1;
		float ooz = e.param[0].start, uoz = e.param[1].start, voz = e.param[2].start;
		float dooz = e.param[0].dpdx, duoz = e.param[1].dpdx, dvoz = e.param[2].dpdx;
		if (checker && !((x ^ y) & 1)) {
			x++;
			ooz += dooz; uoz += duoz; voz += dvoz;
		}
		if (checker) { dooz *= 2.0F; duoz *= 2.0F; dvoz *= 2.0F; }
		for (; x < e.stopx; x += dx, ooz += dooz, uoz += duoz, voz += dvoz) {
			if (o[x] != 0xffffffffu)
				continue;
			if (p.renderer == 3) {
				// textured translucent: draw_scanline_tex<true>'s texel, none where its alpha is under half
				float const z = 1.0F / ooz;
				s32 const mml = -object.texlod + fast_log2(z);
				s32 const level = std::clamp(mml >> 7, 0, max_level);
				s32 const u = s32(uoz * z * 256.0F);
				s32 const v = s32(voz * z * 256.0F);
				u32 t = fetch_bilinear_texel<true>(object, level, u, v);
				if (mml > 0 && level < max_level)
					t = LERP(t, fetch_bilinear_texel<true>(object, level + 1, u, v), (mml & 127) << 1);
				else if (object.utex && mml < 0)
					t = LERP(t, fetch_bilinear_texel<true>(object, -1, u, v), std::min(-mml >> object.utexminlod, 127));
				if (t < 0x00400000)
					continue;
			}
			o[x] = index;
		}
	};
	switch (p.num_vertices)
	{
	case 3: render_triangle<3>(vp, cb, v[0], v[1], v[2], &p.extra); break;
	case 4: render_polygon<4, 3>(vp, cb, v, &p.extra); break;
	case 5: render_polygon<5, 3>(vp, cb, v, &p.extra); break;
	case 6: render_polygon<6, 3>(vp, cb, v, &p.extra); break;
	case 7: render_polygon<7, 3>(vp, cb, v, &p.extra); break;
	case 8: render_polygon<8, 3>(vp, cb, v, &p.extra); break;
	}
}

void model2_video::export_shown(std::vector<gpu3d_poly> &out) const
{
	out.clear();
	const int p = shown_now();
	if (m_render_test_mode || p < 0) return;
	export_list(m_pictures[p].drawn, out);
}

void model2_video::export_picture(int picture, std::vector<gpu3d_poly> &out) const
{
	out.clear();
	if (picture < 0 || picture > 3) return;
	export_list(m_pictures[picture].drawn, out);
}

void model2_video::export_list(const std::vector<drawn_poly> &list, std::vector<gpu3d_poly> &out) const
{
	out.reserve(list.size());
	for (const drawn_poly &d : list) {
		gpu3d_poly &g = out.emplace_back();
		g.n = d.num_vertices;
		for (int i = 0; i < d.num_vertices; i++) { g.x[i] = d.v[i].x; g.y[i] = d.v[i].y; }
		g.renderer = d.renderer;
		g.checker = d.extra.checker;
		g.vp[0] = int16_t(d.vp.min_x); g.vp[1] = int16_t(d.vp.max_x); g.vp[2] = int16_t(d.vp.min_y); g.vp[3] = int16_t(d.vp.max_y);
		const m2_poly_extra_data &e = d.extra;
		if (!(d.renderer & 2)) {
			// draw_scanline_solid's colour
			const u32 c = m_palram[e.colorbase + 0x1000] & 0xffff, l = e.luma >> 2;
			const u32 r = m_colorxlat[0x0000 / 2 + (((c >> 0) & 0x1f) << 8) + l] & 0xff;
			const u32 gg = m_colorxlat[0x4000 / 2 + (((c >> 5) & 0x1f) << 8) + l] & 0xff;
			const u32 b = m_colorxlat[0x8000 / 2 + (((c >> 10) & 0x1f) << 8) + l] & 0xff;
			g.colour = rgb_t(m_gamma_table[r], m_gamma_table[gg], m_gamma_table[b]);
			continue;
		}
		for (int i = 0; i < d.num_vertices; i++) { g.z[i] = d.v[i].pz; g.u[i] = d.v[i].pu; g.v[i] = d.v[i].pv; }
		g.pal = u16(m_palram[e.colorbase + 0x1000] & 0x7fff);
		g.lumabase = u16(e.lumabase);
		g.luma = e.luma;
		g.texlod = e.texlod;
		g.sheet = e.texsheet[0] == m_textureram1;
		g.texw = u16(e.texwidth); g.texh = u16(e.texheight); g.texx = u16(e.texx); g.texy = u16(e.texy);
		g.wrapx = e.texwrapx; g.wrapy = e.texwrapy; g.mirrorx = e.texmirrorx; g.mirrory = e.texmirrory;
		g.utex = e.utex; g.utexminlod = e.utexminlod; g.utexx = u16(e.utexx); g.utexy = u16(e.utexy);
	}
}

// The host's Auto's test scene (CPU or GPU, timed before the game is on show): `n` polygons made
// up, not the game's, of the sizes and kinds a fight has: the whole picture covered by a floor of
// large textured quads (a fight's 3D covers the screen: the stage's floor and walls), some larger
// ones over it, and small textured and solid ones crowded in the middle (the fighters), drawn from
// whatever is in the texture RAMs. The same each time (a fixed sequence).
void model2_video::bench_make(int n)
{
	m_bench.clear();
	const int w = 496 + 2 * m_ext;
	u32 seed = 12345;
	auto rnd = [&](float a, float b) { seed = seed * 1664525u + 1013904223u; return a + (b - a) * float(seed >> 8) / float(1 << 24); };
	auto add = [&](float cx, float cy, float rx, float ry, bool tex, bool four) {
		drawn_poly &d = m_bench.emplace_back();
		d.num_vertices = four ? 4 : 3;
		d.renderer = tex ? 2 : 0;
		d.vp = rectangle(0, w - 1, 0, 383);
		const float z0 = rnd(0.002f, 0.05f);
		for (int i = 0; i < d.num_vertices; i++) {
			const float a = float(i) * 6.2831853f / float(d.num_vertices) + rnd(-0.3f, 0.3f);
			d.v[i].x = cx + rx * std::cos(a);
			d.v[i].y = cy + ry * std::sin(a);
			const float z = z0 * rnd(0.8f, 1.25f);
			d.v[i].p[0] = z;
			d.v[i].p[1] = rnd(0, 255) * z * 8;
			d.v[i].p[2] = rnd(0, 255) * z * 8;
		}
		d.miny = d.maxy = d.v[0].y;
		for (int i = 1; i < d.num_vertices; i++) { d.miny = std::min(d.miny, d.v[i].y); d.maxy = std::max(d.maxy, d.v[i].y); }
		m2_poly_extra_data &e = d.extra;
		e.state = this;
		e.colorbase = u32(rnd(0, 1023));
		e.luma = u8(rnd(0, 255));
		e.lumabase = u32(rnd(0, 255)) << 7;
		if (tex) {
			e.texsheet[0] = m_textureram0; e.texsheet[1] = m_textureram1;
			e.texwidth = 32 << int(rnd(0, 3)); e.texheight = 32 << int(rnd(0, 3));
			e.texx = 32 * u32(rnd(0, 63)); e.texy = 32 * u32(rnd(0, 31));
			e.texwrapx = e.texwrapy = 1;
			e.texlod = s32(rnd(-200, 600));
			e.utex = rnd(0, 1) < 0.3f;
			e.utexminlod = u8(rnd(0, 3));
		}
	};
	// the floor: a grid of quads over the whole picture (a little overlap), then a few over it
	const int cols = 6, rows = 4;
	for (int r = 0; r < rows; r++)
		for (int c = 0; c < cols; c++) {
			drawn_poly &d = (add(0, 0, 1, 1, true, true), m_bench.back());
			const float x0 = float(w) * c / cols - 2, x1 = float(w) * (c + 1) / cols + 2, y0 = 384.0f * r / rows - 2, y1 = 384.0f * (r + 1) / rows + 2;
			const float xs[4] = { x0, x1, x1, x0 }, ys[4] = { y0, y0, y1, y1 };
			for (int i = 0; i < 4; i++) { d.v[i].x = xs[i] + rnd(-1, 1); d.v[i].y = ys[i] + rnd(-1, 1); }
			d.miny = std::min({ d.v[0].y, d.v[1].y, d.v[2].y, d.v[3].y }); d.maxy = std::max({ d.v[0].y, d.v[1].y, d.v[2].y, d.v[3].y });
		}
	const int over = std::max(1, n / 100);
	for (int i = 0; i < over; i++)
		add(rnd(0, float(w)), rnd(150, 384), rnd(50, 120), rnd(20, 60), true, true);
	for (int i = rows * cols + over; i < n; i++)
		add(w * 0.5f + rnd(-120, 120), rnd(60, 330), rnd(4, 22), rnd(4, 22), rnd(0, 1) < 0.85f, rnd(0, 1) < 0.6f);
	// (the fighters in front: drawn first, the rasterizer keeping the first to fill a pixel)
	std::rotate(m_bench.begin(), m_bench.begin() + std::min<size_t>(m_bench.size(), size_t(rows * cols + over)), m_bench.end());
}

void model2_video::bench_take(int picture)
{
	m_bench.clear();
	if (picture >= 0 && picture < 4) m_bench = m_pictures[picture].drawn;
}

void model2_video::bench_export(std::vector<gpu3d_poly> &out) const
{
	out.clear();
	export_list(m_bench, out);
}

double model2_video::bench_cpu(int part, int parts)
{
	const int s = m_scale, w = (496 + 2 * m_ext) * s, h = 384 * s;
	if (m_bench_map.width() != w || m_bench_map.height() != h) m_bench_map.allocate(w, h);
	const int r0 = h * part / parts, r1 = h * (part + 1) / parts;
	const auto t0 = std::chrono::steady_clock::now();
	m_renderer->set_dest(&m_bench_map);
	bitmap_rgb32 &dest = m_renderer->destmap();
	bitmap_ind8 &fill = m_renderer->fillmap();
	parallel_rows(r1 - r0, [&](int y0, int y1) {
		const rectangle band(0, w - 1, r0 + y0, r0 + y1 - 1);
		dest.fill(0x00000000, band);
		fill.fill(0x00, band);
		for (const drawn_poly &p : m_bench)
			m_renderer->draw(p, s, band);
	}, 4);
	m_renderer->set_dest(nullptr);
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void model2_video::gpu_rams(gpu3d_rams &out) const
{
	out.tex[0] = m_textureram0; out.tex[1] = m_textureram1;
	out.luma = m_lumaram; out.xlat = m_colorxlat; out.gamma = m_gamma_table;
	out.gen[0] = m_ram_gen[GPU_TEX0]; out.gen[1] = m_ram_gen[GPU_TEX1]; out.gen[2] = m_ram_gen[GPU_LUMA]; out.gen[3] = m_ram_gen[GPU_XLAT]; out.gen[4] = 0;
}

void model2_video::owner_map_shown(u32 *out, size_t stride)
{
	int w, h;
	render_size(w, h);
	bitmap_rgb32 owner;
	owner.wrap(out, w, h, stride);
	owner.fill(0xffffffffu, rectangle(0, w - 1, 0, h - 1));
	const int p = shown_now();
	if (m_render_test_mode || p < 0) return;
	const std::vector<drawn_poly> &drawn = m_pictures[p].drawn;
	for (size_t i = 0; i < drawn.size(); i++)
		m_renderer->draw_owner(drawn[i], m_scale, u32(i), owner);
}

/*
    Projection:

    According to the 2B Manual the screen coordinates are:

    (8,474)                         (504,474)
       +--------------------------------+
       |                                |
       |                                |
       |                                |
       |                                |
       |                                |
       |                                |
       |                                |
       |                                |
       +--------------------------------+
    (8,90)                          (504,90)
*/

void model2_video::horizontal_sync_w(u16 data)
{
	m_crtc_xoffset = 84 + (int16_t)data;
//  printf("H %04x %d %d\n",data,(int16_t)data,m_crtc_xoffset);
	m_renderer->set_xoffset(m_crtc_xoffset);
}

void model2_video::vertical_sync_w(u16 data)
{
	m_crtc_yoffset = 130 + (int16_t)data;
//  printf("V %04x %d %d\n",data,(int16_t)data,m_crtc_yoffset);
	m_renderer->set_yoffset(m_crtc_yoffset);
}

/* 3D Rasterizer projection: projects a polygon into screen coordinates */
inline void model2_video::model2_3d_project(polygon *poly)
{
	for (int i = 0; i < poly->num_vertices; i++)
	{
		/* project the vertices */
		poly->v[i].x = m_crtc_xoffset + m_ext + poly->center[0] + (poly->v[i].x / (poly->v[i].pz + std::numeric_limits<float>::min()));
		poly->v[i].y = ((384 - poly->center[1])+m_crtc_yoffset) - (poly->v[i].y / (poly->v[i].pz + std::numeric_limits<float>::min()));
	}
}

/* 3D Rasterizer frame start: Resets frame variables */
void model2_video::render_frame_start(raster_state *raster)
{

	/* reset the polygon list index */
	raster->poly_list_index = 0;

	/* reset the sorted z list */
	std::fill(std::begin(raster->poly_sorted_list), std::end(raster->poly_sorted_list), nullptr);

	/* reset the min-max sortable Z values */
	raster->min_z = 0xffff;
	raster->max_z = 0;

	/* reset the polygon z value */
	// Zero Gunner sets backgrounds with "previous z value" mode at the start of the display list,
	// needs this to be this big in order to work properly
	raster->polygon_z = 1e10;

	raster->cur_window = 0;
}

// MAME's render_polygons: the polygons in their order, into a picture (the renderer's destmap,
// or with a delay one not about to be shown). Each is projected and set up in turn (the
// picture's list), then drawn by raster_drawn (deferred: later, if it's shown). The frame's
// picture goes on the end of m_shown.
void model2_video::render_polygons()
{
	raster_state *raster = m_raster.get();
	auto show = [&](int p) { m_shown[0] = m_shown[1]; m_shown[1] = m_shown[2]; m_shown[2] = p; };

	// if the geometrizer hasn't presented a new frame, just copy the previous frame and bail
	if (m_render_done)
	{
		show(m_shown[2]);
		return;
	}

	/* if we have nothing to render, bail */
	if (raster->poly_list_index == 0)
	{
		show(-1);
		return;
	}

	// the picture to draw in: one the next two frames won't show (with no delay, always the destmap)
	int pic = 0;
	if (m_delay)
		while (pic == m_shown[1] || pic == m_shown[2]) pic++;
	build_drawn(raster, m_pictures[pic].drawn);
	m_pic_frame[pic] = m_parsed_frame;
	static texdump *const dump = getenv("VF2_DUMP_TEXTURES") ? new texdump(getenv("VF2_DUMP_TEXTURES")) : nullptr;
	if (dump) dump->add(m_pictures[pic].drawn, *this, m_parsed_frame);
	if (m_defer) m_pictures[pic].deferred = m_deferred = true;
	else raster_drawn(pic);

	show(pic);
	m_render_done = true;
}

// A raster's sorted polygons projected and set up for drawing, in MAME's order
void model2_video::build_drawn(raster_state *raster, std::vector<drawn_poly> &drawn)
{
	int32_t z;
	const rectangle cliprect(0, 495 + 2 * m_ext, 0, 383);   // prepare() clips the viewports in arcade pixels
	drawn.clear();
	for (int window = raster->cur_window; window >= 0; window--)
	{
		/* go through the Z levels, and render each bucket */
		for (z = raster->min_z; z <= raster->max_z; z++)
		{
			/* see if we have items at this z level */
			if (raster->poly_sorted_list[z] != nullptr)
			{
				/* get a pointer to the first polygon */
				polygon *poly = raster->poly_sorted_list[z];

				/* and loop clipping and rendering each polygon */
				while (poly != nullptr)
				{
					if (poly->window == window)
					{
						/* project and render */
						model2_3d_project(poly);
						m_renderer->prepare(poly, cliprect, drawn.emplace_back());
					}

					poly = (polygon *)poly->next;
				}
			}
		}
	}
}

// A picture cleared and its list drawn in it, in bands of rows on every core: each band draws
// every polygon, in order, within its rows. Front to back, a pixel already drawn is kept (the
// fillmap), so a pixel's colour depends only on the polygons over it and their order, which
// every band keeps: the same picture as drawing them one by one.
void model2_video::raster_drawn(int picture, bitmap_rgb32 *into, int r0, int r1)
{
	const int s = m_scale, w = (496 + 2 * m_ext) * s, h = 384 * s;
	if (r1 < 0) r1 = h;
	m_renderer->set_dest(into ? into : picture ? &m_pictures[picture].map : nullptr);
	bitmap_rgb32 &dest = m_renderer->destmap();
	bitmap_ind8 &fill = m_renderer->fillmap();
	const std::vector<drawn_poly> &drawn = m_pictures[picture].drawn;
	parallel_rows(r1 - r0, [&](int y0, int y1) {
		const rectangle band(0, w - 1, r0 + y0, r0 + y1 - 1);
		dest.fill(0x00000000, band);
		fill.fill(0x00, band);
		for (const drawn_poly &p : drawn)
			m_renderer->draw(p, s, band);
	}, 4);   // 4 bands a thread, taken as threads free up: the floor's rows are the busy ones
	m_renderer->set_dest(nullptr);
}

// direct framebuffer drawing (enabled with render test mode, Last Bronx title screen)
// pretty sure test mode cuts off DSP framebuffer drawing/clear, according to the manual description too
// (above 1x each pixel as a block: cliprect is in screen pixels, the loops in the arcade's)
void model2_video::draw_framebuffer(bitmap_rgb32 &bitmap, const rectangle &screen_clip)
{
	u16 *fbvram = &(m_frame_number & 1 ? m_fbvramB[0] : m_fbvramA[0]);
	int xoffs = -m_crtc_xoffset;
	int yoffs = (512 - 384) - m_crtc_yoffset;
	const int sc = m_scale;
	const rectangle cliprect(screen_clip.min_x / sc, (screen_clip.max_x + 1) / sc - 1, screen_clip.min_y / sc, (screen_clip.max_y + 1) / sc - 1);

	for (int y = cliprect.min_y; y < cliprect.max_y; ++y)
	{
		for (int x = cliprect.min_x; x < cliprect.max_x; x++)
		{
			int offset = (x + xoffs) + (y + yoffs)*512;
			// assuming the scroll color table is used
			int r = m_colorxlat[0x0080 / 2 + ((fbvram[offset] >> 5) & 0x1f) * 0x100];
			int g = m_colorxlat[0x4080 / 2 + ((fbvram[offset] >> 10) & 0x1f) * 0x100];
			int b = m_colorxlat[0x8080 / 2 + ((fbvram[offset] >> 0) & 0x1f) * 0x100];
			r = m_gamma_table[r];
			g = m_gamma_table[g];
			b = m_gamma_table[b];
			for (int dy = 0; dy < sc; dy++)
				for (int dx = 0; dx < sc; dx++)
					bitmap.pix(y * sc + dy, x * sc + dx) = r << 16 | g << 8 | b;
		}
	}
}

/* 3D Rasterizer main data input port */
void model2_video::model2_3d_push(raster_state *raster, u32 input)
{
	/* see if we have a command in progress */
	if (raster->cur_command != 0)
	{
		raster->command_buffer[raster->command_index++] = input;

		switch (raster->cur_command)
		{
			case 0x00:  /* NOP */
			break;

			case 0x01:  /* Polygon Data */
			{
				u32  attr;

				/* start by looking if we have the basic input data */
				if (raster->command_index < 9)
					return;

				/* get the attributes */
				attr = raster->command_buffer[8];

				/* see if we're done */
				if ((attr & 3) == 0)
				{
					raster->cur_command = 0;
					return;
				}

				/* see if it's a quad or a triangle */
				if (attr & 1)
				{
					/* it's a quad, wait for the rest of the points */
					if (raster->command_index < 17)
						return;

					/* we have a full polygon info, fill up our polygon structure */
					model2_3d_process_polygon<4>(raster, attr);

					/* back up and wait for more data */
					raster->command_index = 8;
				}
				else
				{
					/* it's a triangle, wait for the rest of the point */
					if (raster->command_index < 14)
						return;

					/* we have a full polygon info, fill up our polygon structure */
					model2_3d_process_polygon<3>(raster, attr);

					/* back up and wait for more data */
					raster->command_index = 8;
				}
			}
			break;

			case 0x03:  /* Window Data */
			{
				u32  i;

				/* make sure we have all the data */
				if (raster->command_index < 6)
					return;

				/* coordinates are 12 bit signed */

				/* extract the viewport start x */
				raster->viewport[0] = (raster->command_buffer[0] >> 12) & 0xfff;

				if (raster->viewport[0] & 0x800)
					raster->viewport[0] = -(0x800 - (raster->viewport[0] & 0x7ff));

				/* extract the viewport start y */
				raster->viewport[1] = raster->command_buffer[0] & 0xfff;

				if (raster->viewport[1] & 0x800)
					raster->viewport[1] = -(0x800 - (raster->viewport[1] & 0x7ff));

				/* extract the viewport end x */
				raster->viewport[2] = (raster->command_buffer[1] >> 12) & 0xfff;

				if (raster->viewport[2] & 0x800)
					raster->viewport[2] = -(0x800 - (raster->viewport[2] & 0x7ff));

				/* extract the viewport end y */
				raster->viewport[3] = raster->command_buffer[1] & 0xfff;

				if (raster->viewport[3] & 0x800)
					raster->viewport[3] = -(0x800 - (raster->viewport[3] & 0x7ff));

				// widescreen: a window as wide as the screen opens out to the sides
				raster->widened = m_ext && raster->viewport[0] <= 0 && raster->viewport[2] >= 495;
				if (raster->widened) {
					raster->viewport[0] -= m_ext;
					raster->viewport[2] += m_ext;
				}

				/* extract the centers */
				for (i = 0; i < 4; i++)
				{
					/* center x */
					raster->center[i][0] = (raster->command_buffer[2+i] >> 12) & 0xfff;

					if (raster->center[i][0] & 0x800)
						raster->center[i][0] = -(0x800 - (raster->center[i][0] & 0x7ff));

					/* center y */
					raster->center[i][1] = raster->command_buffer[2+i] & 0xfff;

					if (raster->center[i][1] & 0x800)
						raster->center[i][1] = -(0x800 - (raster->center[i][1] & 0x7ff));

					// calculate clipping planes
					float left_plane = float(raster->center[i][0] - raster->viewport[0]);
					float right_plane = float(raster->viewport[2] - raster->center[i][0]);
					float top_plane = float(raster->viewport[3] - raster->center[i][1]);
					float bottom_plane = float(raster->center[i][1] - raster->viewport[1]);

					raster->clip_plane[i][0].normal.x = 1.0f / std::hypot(1.0f, left_plane);
					raster->clip_plane[i][0].normal.y = 0.0f;
					raster->clip_plane[i][0].normal.pz = left_plane / std::hypot(1.0f, left_plane);

					raster->clip_plane[i][1].normal.x = -1.0f / std::hypot(-1.0f, right_plane);
					raster->clip_plane[i][1].normal.y = 0.0f;
					raster->clip_plane[i][1].normal.pz = right_plane / std::hypot(-1.0f, right_plane);

					raster->clip_plane[i][2].normal.x = 0.0f;
					raster->clip_plane[i][2].normal.y = -1.0f / std::hypot(-1.0f, top_plane);
					raster->clip_plane[i][2].normal.pz = top_plane / std::hypot(-1.0f, top_plane);

					raster->clip_plane[i][3].normal.x = 0.0f;
					raster->clip_plane[i][3].normal.y = 1.0f / std::hypot(1.0f, bottom_plane);
					raster->clip_plane[i][3].normal.pz = bottom_plane / std::hypot(1.0f, bottom_plane);
				}

				/* done with this command */
				raster->cur_command = 0;
			}
			break;

			case 0x04:  /* Texture/Log Data write */
			{
				/* make sure we have enough data */
				if (raster->command_index < 2)
					return;

				/* see if the count is non-zero */
				if (raster->command_buffer[1] > 0)
				{
					/* see if we have data available */
					if (raster->command_index >= 3)
					{
						/* get the address */
						u32  address = raster->command_buffer[0];

						/* do the write */
						if (address & 0x800000)
							raster->texture_ram[address & 0xffff] = raster->command_buffer[2];
						else
							raster->log_ram[address & 0x7fff] = raster->command_buffer[2];

						/* increment the address and decrease the count */
						raster->command_buffer[0]++;
						raster->command_buffer[1]--;

						/* decrease the index, so we keep placing data in the same slot */
						raster->command_index--;
					}
				}

				/* see if we're done with this command */
				if (raster->command_buffer[1] == 0)
					raster->cur_command = 0;
			}
			break;

			case 0x08:  /* ZSort mode */
			{
				/* save the zsort mode value */
				raster->z_adjust = raster->command_buffer[0] << 8;

				/* done with this command */
				raster->cur_command = 0;
			}
			break;

			default:
			{
				fatalerror("SEGA 3D: Unknown rasterizer command %08x\n", raster->cur_command);
			}
		}
	}
	else
	{
		/* new command */
		raster->cur_command = input & 0x0f;
		raster->command_index = 0;

		/* see if it's object data */
		if (raster->cur_command == 1)
		{
			/* extract reverse bit */
			raster->reverse = (input >> 4) & 1;

			/* extract center select */
			raster->center_sel = (input >> 6) & 3;
		}
	}
}

/***********************************************************************************************/

/*******************************************
 *
 *  Geometry Engine Polygon Parsers
 *
 *******************************************/

/* Parse Polygons: Normals Present, No Specular case */
void model2_video::geo_parse_np_ns(geo_state *geo, u32 *input, u32 count)
{
	raster_state *raster = geo->raster;
	poly_vertex point, normal;
	u32  attr, i;

	/* read the 1st point */
	point.x = u2f(*input++);
	point.y = u2f(*input++);
	point.pz = u2f(*input++);

	/* transform with the current matrix */
	transform_point(&point, geo->matrix);

	/* apply focus */
	apply_focus(geo, &point);

	/* push it to the 3d rasterizer */
	model2_3d_push(raster, f2u(point.x) >> 8);
	model2_3d_push(raster, f2u(point.y) >> 8);
	model2_3d_push(raster, f2u(point.pz) >> 8);

	/* read the 2nd point */
	point.x = u2f(*input++);
	point.y = u2f(*input++);
	point.pz = u2f(*input++);

	/* transform with the current matrix */
	transform_point(&point, geo->matrix);

	/* apply focus */
	apply_focus(geo, &point);

	/* push it to the 3d rasterizer */
	model2_3d_push(raster, f2u(point.x) >> 8);
	model2_3d_push(raster, f2u(point.y) >> 8);
	model2_3d_push(raster, f2u(point.pz) >> 8);

	/* loop through the following links */
	for (i = 0; i < count; i++)
	{
		/* read in the attributes */
		attr = *input++;

		/* push to the 3d rasterizer */
		model2_3d_push(raster, attr & 0x0003ffff);

		/* read in the normal */
		normal.x = u2f(*input++);
		normal.y = u2f(*input++);
		normal.pz = u2f(*input++);

		/* transform with the current matrix */
		transform_vector(&normal, geo->matrix);

		if ((attr & 3) != 0) /* quad or triangle */
		{
			float               dotl, dotp, luminance, distance;
			float               coef, face;
			int32_t               luma;
			texture_parameter * texparam;

			/* read in the next point */
			point.x = u2f(*input++);
			point.y = u2f(*input++);
			point.pz = u2f(*input++);

			/* transform with the current matrix */
			transform_point(&point, geo->matrix);

			/* calculate the dot product of the normal and the light vector */
			dotl = dot_product(normal, geo->light);

			/* calculate the dot product of the normal and the point */
			dotp = dot_product(normal, point);

			/* apply focus */
			apply_focus(geo, &point);

			/* determine whether this is the front or the back of the polygon */
			face = 0x100; /* rear */
			if (dotp >= 0) face = 0; /* front */

			/* get the texture parameters */
			texparam = &geo->texture_parameters[(attr>>18) & 0x1f];

			/* calculate luminance */
			if ((dotl * dotp) < 0) luminance = 0;
			else luminance = fabs(dotl);

			luminance = (luminance * texparam->diffuse) + texparam->ambient;
			luminance = std::clamp(luminance, 0.0f, 255.0f);

			luma = (int32_t)luminance;

			/* add the face bit to the luma */
			luma += face;

			/* extract distance coefficient */
			coef = geo->coef_table[attr>>27];

			/* calculate texture level of detail */
			distance = coef * fabs(dotp) * geo->lod;

			/* push to the 3d rasterizer */
			model2_3d_push(raster, luma << 15);
			model2_3d_push(raster, f2u(distance) >> 8);
			model2_3d_push(raster, f2u(point.x) >> 8);
			model2_3d_push(raster, f2u(point.y) >> 8);
			model2_3d_push(raster, f2u(point.pz) >> 8);

			/* if it's a quad, push one more point */
			if (attr & 1)
			{
				/* read in the next point */
				point.x = u2f(*input++);
				point.y = u2f(*input++);
				point.pz = u2f(*input++);

				/* transform with the current matrix */
				transform_point(&point, geo->matrix);

				/* apply focus */
				apply_focus(geo, &point);

				/* push to the 3d rasterizer */
				model2_3d_push(raster, f2u(point.x) >> 8);
				model2_3d_push(raster, f2u(point.y) >> 8);
				model2_3d_push(raster, f2u(point.pz) >> 8);
			}
			else /* triangle */
			{
				/* skip the next 3 points */
				input += 3;
			}
		}
		else /* we're done */
		{
			break;
		}
	}

	/* notify the 3d rasterizer we're done */
	model2_3d_push(raster, 0);
}

/* Parse Polygons: Normals Present, Specular case */
void model2_video::geo_parse_np_s(geo_state *geo, u32 *input, u32 count)
{
	raster_state *raster = geo->raster;
	poly_vertex point, normal;
	u32  attr, i;

	/* read the 1st point */
	point.x = u2f(*input++);
	point.y = u2f(*input++);
	point.pz = u2f(*input++);

	/* transform with the current matrix */
	transform_point(&point, geo->matrix);

	/* apply focus */
	apply_focus(geo, &point);

	/* push it to the 3d rasterizer */
	model2_3d_push(raster, f2u(point.x) >> 8);
	model2_3d_push(raster, f2u(point.y) >> 8);
	model2_3d_push(raster, f2u(point.pz) >> 8);

	/* read the 2nd point */
	point.x = u2f(*input++);
	point.y = u2f(*input++);
	point.pz = u2f(*input++);

	/* transform with the current matrix */
	transform_point(&point, geo->matrix);

	/* apply focus */
	apply_focus(geo, &point);

	/* push it to the 3d rasterizer */
	model2_3d_push(raster, f2u(point.x) >> 8);
	model2_3d_push(raster, f2u(point.y) >> 8);
	model2_3d_push(raster, f2u(point.pz) >> 8);

	/* loop through the following links */
	for (i = 0; i < count; i++)
	{
		/* read in the attributes */
		attr = *input++;

		/* push to the 3d rasterizer */
		model2_3d_push(raster, attr & 0x0003ffff);

		/* read in the normal */
		normal.x = u2f(*input++);
		normal.y = u2f(*input++);
		normal.pz = u2f(*input++);

		/* transform with the current matrix */
		transform_vector(&normal, geo->matrix);

		if ((attr & 3) != 0) /* quad or triangle */
		{
			float               dotl, dotp, luminance, distance, specular;
			float               coef, face;
			int32_t             luma;
			texture_parameter * texparam;

			/* read in the next point */
			point.x = u2f(*input++);
			point.y = u2f(*input++);
			point.pz = u2f(*input++);

			/* transform with the current matrix */
			transform_point(&point, geo->matrix);

			/* calculate the dot product of the normal and the light vector */
			dotl = dot_product(normal, geo->light);

			/* calculate the dot product of the normal and the point */
			dotp = dot_product(normal, point);

			/* apply focus */
			apply_focus(geo, &point);

			/* determine whether this is the front or the back of the polygon */
			face = 0x100; /* rear */
			if (dotp >= 0) face = 0; /* front */

			/* get the texture parameters */
			texparam = &geo->texture_parameters[(attr>>18) & 0x1f];

			/* calculate luminance and specular */
			if ((dotl * dotp) < 0) luminance = 0;
			else luminance = fabs(dotl);

			specular = ((2*dotl) * normal.pz) - geo->light.pz;
			if (specular < 0) specular = 0;
			if (texparam->specular_control == 0) specular = 0;
			if ((texparam->specular_control >> 1) != 0) specular *= specular;
			if ((texparam->specular_control >> 2) != 0) specular *= specular;
			if (((texparam->specular_control+1) >> 3) != 0) specular *= specular;

			specular *= texparam->specular_scale;

			luminance = (luminance * texparam->diffuse) + texparam->ambient + specular;
			luminance = std::clamp(luminance, 0.0f, 255.0f);

			luma = (int32_t)luminance;

			/* add the face bit to the luma */
			luma += face;

			/* extract distance coefficient */
			coef = geo->coef_table[attr>>27];

			/* calculate texture level of detail */
			distance = coef * fabs(dotp) * geo->lod;

			/* push to the 3d rasterizer */
			model2_3d_push(raster, luma << 15);
			model2_3d_push(raster, f2u(distance) >> 8);
			model2_3d_push(raster, f2u(point.x) >> 8);
			model2_3d_push(raster, f2u(point.y) >> 8);
			model2_3d_push(raster, f2u(point.pz) >> 8);

			/* if it's a quad, push one more point */
			if (attr & 1)
			{
				/* read in the next point */
				point.x = u2f(*input++);
				point.y = u2f(*input++);
				point.pz = u2f(*input++);

				/* transform with the current matrix */
				transform_point(&point, geo->matrix);

				/* apply focus */
				apply_focus(geo, &point);

				/* push to the 3d rasterizer */
				model2_3d_push(raster, f2u(point.x) >> 8);
				model2_3d_push(raster, f2u(point.y) >> 8);
				model2_3d_push(raster, f2u(point.pz) >> 8);
			}
			else /* triangle */
			{
				/* skip the next 3 points */
				input += 3;
			}
		}
		else /* we're done */
		{
			break;
		}
	}

	/* notify the 3d rasterizer we're done */
	model2_3d_push(raster, 0);
}

/* Parse Polygons: No Normals, No Specular case */
void model2_video::geo_parse_nn_ns(geo_state *geo, u32 *input, u32 count)
{
	raster_state *raster = geo->raster;
	poly_vertex point, normal, p0, p1, p2, p3;
	u32  attr, i;

	/* read the 1st point */
	point.x = u2f(*input++);
	point.y = u2f(*input++);
	point.pz = u2f(*input++);

	/* transform with the current matrix */
	transform_point(&point, geo->matrix);

	/* save for normal calculation */
	p0.x = point.x; p0.y = point.y; p0.pz = point.pz;

	/* apply focus */
	apply_focus(geo, &point);

	/* push it to the 3d rasterizer */
	model2_3d_push(raster, f2u(point.x) >> 8);
	model2_3d_push(raster, f2u(point.y) >> 8);
	model2_3d_push(raster, f2u(point.pz) >> 8);

	/* read the 2nd point */
	point.x = u2f(*input++);
	point.y = u2f(*input++);
	point.pz = u2f(*input++);

	/* transform with the current matrix */
	transform_point(&point, geo->matrix);

	/* save for normal calculation */
	p1.x = point.x; p1.y = point.y; p1.pz = point.pz;

	/* apply focus */
	apply_focus(geo, &point);

	/* push it to the 3d rasterizer */
	model2_3d_push(raster, f2u(point.x) >> 8);
	model2_3d_push(raster, f2u(point.y) >> 8);
	model2_3d_push(raster, f2u(point.pz) >> 8);

	/* loop through the following links */
	for (i = 0; i < count; i++)
	{
		/* read in the attributes */
		attr = *input++;

		/* push to the 3d rasterizer */
		model2_3d_push(raster, attr & 0x0003ffff);

		if ((attr & 3) != 0) /* quad or triangle */
		{
			float               dotl, dotp, luminance, distance;
			float               coef, face;
			int32_t             luma;
			texture_parameter * texparam;

			/* Skip normal */
			input += 3;

			/* read in the next point */
			point.x = u2f(*input++);
			point.y = u2f(*input++);
			point.pz = u2f(*input++);

			/* transform with the current matrix */
			transform_point(&point, geo->matrix);

			/* save for normal calculation */
			p2.x = point.x; p2.y = point.y; p2.pz = point.pz;

			/* compute the normal */
			vector_cross3(&normal, &p0, &p1, &p2);

			/* normalize it */
			normalize_vector(&normal);

			/* calculate the dot product of the normal and the light vector */
			dotl = dot_product(normal, geo->light);

			/* calculate the dot product of the normal and the point */
			dotp = dot_product(normal, point);

			/* apply focus */
			apply_focus(geo, &point);

			/* determine whether this is the front or the back of the polygon */
			face = 0x100; /* rear */
			if (dotp >= 0) face = 0; /* front */

			/* get the texture parameters */
			texparam = &geo->texture_parameters[(attr>>18) & 0x1f];

			/* calculate luminance */
			if ((dotl * dotp) < 0) luminance = 0;
			else luminance = fabs(dotl);

			luminance = (luminance * texparam->diffuse) + texparam->ambient;
			luminance = std::clamp(luminance, 0.0f, 255.0f);

			luma = (int32_t)luminance;

			/* add the face bit to the luma */
			luma += face;

			/* extract distance coefficient */
			coef = geo->coef_table[attr>>27];

			/* calculate texture level of detail */
			distance = coef * fabs(dotp) * geo->lod;

			/* push to the 3d rasterizer */
			model2_3d_push(raster, luma << 15);
			model2_3d_push(raster, f2u(distance) >> 8);
			model2_3d_push(raster, f2u(point.x) >> 8);
			model2_3d_push(raster, f2u(point.y) >> 8);
			model2_3d_push(raster, f2u(point.pz) >> 8);

			/* if it's a quad, push one more point */
			if (attr & 1)
			{
				/* read in the next point */
				point.x = u2f(*input++);
				point.y = u2f(*input++);
				point.pz = u2f(*input++);

				/* transform with the current matrix */
				transform_point(&point, geo->matrix);

				/* save for normal calculation */
				p3.x = point.x; p3.y = point.y; p3.pz = point.pz;

				/* apply focus */
				apply_focus(geo, &point);

				/* push to the 3d rasterizer */
				model2_3d_push(raster, f2u(point.x) >> 8);
				model2_3d_push(raster, f2u(point.y) >> 8);
				model2_3d_push(raster, f2u(point.pz) >> 8);
			}
			else
			{
				/* skip the next 3 points */
				input += 3;

				/* for triangles, the rope of P1(n) is achieved by P0(n-1) (linktype 3) */
				p3.x = p2.x; p3.y = p2.y; p3.pz = p2.pz;
			}
		}
		else /* we're done */
		{
			break;
		}

		/* link type */
		switch ((attr>>8) & 3)
		{
			case 0:
			case 2:
			{
				/* reuse P0(n) and P1(n) */
				p0.x = p2.x; p0.y = p2.y; p0.pz = p2.pz;
				p1.x = p3.x; p1.y = p3.y; p1.pz = p3.pz;
			}
			break;

			case 1:
			{
				/* reuse P0(n-1) and P0(n) */
				p1.x = p2.x; p1.y = p2.y; p1.pz = p2.pz;
			}
			break;

			case 3:
			{
				/* reuse P1(n-1) and P1(n) */
				p0.x = p3.x; p0.y = p3.y; p0.pz = p3.pz;
			}
			break;
		}
	}

	/* notify the 3d rasterizer we're done */
	model2_3d_push(raster, 0);
}

/* Parse Polygons: No Normals, Specular case */
void model2_video::geo_parse_nn_s(geo_state *geo, u32 *input, u32 count)
{
	raster_state *raster = geo->raster;
	poly_vertex point, normal, p0, p1, p2, p3;
	u32  attr, i;

	/* read the 1st point */
	point.x = u2f(*input++);
	point.y = u2f(*input++);
	point.pz = u2f(*input++);

	/* transform with the current matrix */
	transform_point(&point, geo->matrix);

	/* save for normal calculation */
	p0.x = point.x; p0.y = point.y; p0.pz = point.pz;

	/* apply focus */
	apply_focus(geo, &point);

	/* push it to the 3d rasterizer */
	model2_3d_push(raster, f2u(point.x) >> 8);
	model2_3d_push(raster, f2u(point.y) >> 8);
	model2_3d_push(raster, f2u(point.pz) >> 8);

	/* read the 2nd point */
	point.x = u2f(*input++);
	point.y = u2f(*input++);
	point.pz = u2f(*input++);

	/* transform with the current matrix */
	transform_point(&point, geo->matrix);

	/* save for normal calculation */
	p1.x = point.x; p1.y = point.y; p1.pz = point.pz;

	/* apply focus */
	apply_focus(geo, &point);

	/* push it to the 3d rasterizer */
	model2_3d_push(raster, f2u(point.x) >> 8);
	model2_3d_push(raster, f2u(point.y) >> 8);
	model2_3d_push(raster, f2u(point.pz) >> 8);

	/* loop through the following links */
	for (i = 0; i < count; i++)
	{
		/* read in the attributes */
		attr = *input++;

		/* push to the 3d rasterizer */
		model2_3d_push(raster, attr & 0x0003ffff);

		if ((attr & 3) != 0) /* quad or triangle */
		{
			float               dotl, dotp, luminance, distance, specular;
			float               coef, face;
			int32_t             luma;
			texture_parameter * texparam;

			/* Skip normal */
			input += 3;

			/* read in the next point */
			point.x = u2f(*input++);
			point.y = u2f(*input++);
			point.pz = u2f(*input++);

			/* transform with the current matrix */
			transform_point(&point, geo->matrix);

			/* save for normal calculation */
			p2.x = point.x; p2.y = point.y; p2.pz = point.pz;

			/* compute the normal */
			vector_cross3(&normal, &p0, &p1, &p2);

			/* normalize it */
			normalize_vector(&normal);

			/* calculate the dot product of the normal and the light vector */
			dotl = dot_product(normal, geo->light);

			/* calculate the dot product of the normal and the point */
			dotp = dot_product(normal, point);

			/* apply focus */
			apply_focus(geo, &point);

			/* determine whether this is the front or the back of the polygon */
			face = 0x100; /* rear */
			if (dotp >= 0) face = 0; /* front */

			/* get the texture parameters */
			texparam = &geo->texture_parameters[(attr>>18) & 0x1f];

			/* calculate luminance and specular */
			if ((dotl * dotp) < 0) luminance = 0;
			else luminance = fabs(dotl);

			specular = ((2*dotl) * normal.pz) - geo->light.pz;
			if (specular < 0) specular = 0;
			if (texparam->specular_control == 0) specular = 0;
			if ((texparam->specular_control >> 1) != 0) specular *= specular;
			if ((texparam->specular_control >> 2) != 0) specular *= specular;
			if (((texparam->specular_control+1) >> 3) != 0) specular *= specular;

			specular *= texparam->specular_scale;

			luminance = (luminance * texparam->diffuse) + texparam->ambient + specular;
			luminance = std::clamp(luminance, 0.0f, 255.0f);

			luma = (int32_t)luminance;

			/* add the face bit to the luma */
			luma += face;

			/* extract distance coefficient */
			coef = geo->coef_table[attr>>27];

			/* calculate texture level of detail */
			distance = coef * fabs(dotp) * geo->lod;

			/* push to the 3d rasterizer */
			model2_3d_push(raster, luma << 15);
			model2_3d_push(raster, f2u(distance) >> 8);
			model2_3d_push(raster, f2u(point.x) >> 8);
			model2_3d_push(raster, f2u(point.y) >> 8);
			model2_3d_push(raster, f2u(point.pz) >> 8);

			/* if it's a quad, push one more point */
			if (attr & 1)
			{
				/* read in the next point */
				point.x = u2f(*input++);
				point.y = u2f(*input++);
				point.pz = u2f(*input++);

				/* transform with the current matrix */
				transform_point(&point, geo->matrix);

				/* save for normal calculation */
				p3.x = point.x; p3.y = point.y; p3.pz = point.pz;

				/* apply focus */
				apply_focus(geo, &point);

				/* push to the 3d rasterizer */
				model2_3d_push(raster, f2u(point.x) >> 8);
				model2_3d_push(raster, f2u(point.y) >> 8);
				model2_3d_push(raster, f2u(point.pz) >> 8);
			}
			else
			{
				/* skip the next 3 points */
				input += 3;

				/* for triangles, the rope of P1(n) is achieved by P0(n-1) (linktype 3) */
				p3.x = p2.x; p3.y = p2.y; p3.pz = p2.pz;
			}
		}
		else /* we're done */
		{
			break;
		}

		/* link type */
		switch ((attr>>8) & 3)
		{
			case 0:
			case 2:
			{
				/* reuse P0(n) and P1(n) */
				p0.x = p2.x; p0.y = p2.y; p0.pz = p2.pz;
				p1.x = p3.x; p1.y = p3.y; p1.pz = p3.pz;
			}
			break;

			case 1:
			{
				/* reuse P0(n-1) and P0(n) */
				p1.x = p2.x; p1.y = p2.y; p1.pz = p2.pz;
			}
			break;

			case 3:
			{
				/* reuse P1(n-1) and P1(n) */
				p0.x = p3.x; p0.y = p3.y; p0.pz = p3.pz;
			}
			break;
		}
	}

	/* notify the 3d rasterizer we're done */
	model2_3d_push(raster, 0);
}

/*******************************************
 *
 *  Geometry Engine Commands
 *
 *******************************************/

/* Command 00: NOP */
u32 *model2_video::geo_nop(geo_state *geo, u32 opcode, u32 *input)
{
	raster_state *raster = geo->raster;

	/* push the opcode to the 3d rasterizer */
	model2_3d_push(raster, opcode >> 23);

	return input;
}

/* Command 01: Object Data */
u32 *model2_video::geo_object_data(geo_state *geo, u32 opcode, u32 *input)
{
	raster_state *raster = geo->raster;
	u32  tpa = *input++;     /* Texture Point Address */
	u32  tha = *input++;     /* Texture Header Address */
	u32  oba = *input++;     /* Object Address */
	u32  obc = *input++;     /* Object Count */

	u32 *obp;                /* Object Pointer */

	/* push the initial set of data to the 3d rasterizer */
	model2_3d_push(raster, opcode >> 23);
	model2_3d_push(raster, tpa);
	model2_3d_push(raster, tha);

	/* select where we're reading polygon information from */
	if (oba & 0x01000000)
	{
		/* Fast polygon RAM */
		obp = &geo->polygon_ram1[oba & 0x7fff];
	}
	else if (oba & 0x00800000)
	{
		/* Polygon ROM */
		obp = &geo->polygon_rom[oba & geo->polygon_rom_mask];
	}
	else
	{
		/* Slow Polygon RAM */
		obp = &geo->polygon_ram0[oba & 0x7fff];
	}

	// if count == 0 then rolls over to max size
	// Virtual On & Gunblade NY
	if (obc == 0)
		obc = 0xfffff;

	// smooth motion: the object kept with its matrix, or drawn again with the blended one
	float kept[12];
	bool swapped = false;
	if (m_rec) smooth_note_object(oba, geo->matrix);
	else if (m_replay) {
		const size_t i = m_replay_obj++;
		if (i < m_blend_ok.size() && m_blend_ok[i]) {
			memcpy(kept, geo->matrix, sizeof kept);
			memcpy(geo->matrix, m_blend[i].data(), sizeof kept);
			swapped = true;
		}
	}

	switch (geo->mode & 3)
	{
		/* Normals present, No Specular */
		case 0: geo_parse_np_ns(geo, obp, obc); break;

		/* Normals present, Specular */
		case 1: geo_parse_np_s(geo, obp, obc); break;

		/* No Normals present, No Specular */
		case 2: geo_parse_nn_ns(geo, obp, obc); break;

		/* No Normals present, Specular */
		case 3: geo_parse_nn_s(geo, obp, obc); break;
	}
	if (swapped) memcpy(geo->matrix, kept, sizeof kept);

	/* move by 4 parameters */
	return input;
}

/* Command 02: Direct Data */
u32 *model2_video::geo_direct_data(geo_state *geo, u32 opcode, u32 *input)
{
	raster_state *raster = geo->raster;
	u32  tpa = *input++;     /* Texture Point Address */
	u32  tha = *input++;     /* Texture Header Address */

	/* push the initial set of data to the 3d rasterizer */
	model2_3d_push(raster, (opcode >> 23) - 1);
	model2_3d_push(raster, tpa);
	model2_3d_push(raster, tha);

	/* push the initial points */
	model2_3d_push(raster, (*input++) >> 8); /* x */
	model2_3d_push(raster, (*input++) >> 8); /* y */
	model2_3d_push(raster, (*input++) >> 8); /* z */

	model2_3d_push(raster, (*input++) >> 8); /* x */
	model2_3d_push(raster, (*input++) >> 8); /* y */
	model2_3d_push(raster, (*input++) >> 8); /* z */

	/* read in the attributes */
	u32  attr;
	while (((attr = *input++) & 3) != 0)
	{
		/* push attributes */
		model2_3d_push(raster, attr & 0x00ffffff);

		/* push luma */
		model2_3d_push(raster, (*input++) >> 8);

		/* push distance */
		model2_3d_push(raster, (*input++) >> 8);

		/* push the next point */
		model2_3d_push(raster, (*input++) >> 8); /* x */
		model2_3d_push(raster, (*input++) >> 8); /* y */
		model2_3d_push(raster, (*input++) >> 8); /* z */

		/* if it's a quad, output another point */
		if (attr & 1)
		{
			model2_3d_push(raster, (*input++) >> 8); /* x */
			model2_3d_push(raster, (*input++) >> 8); /* y */
			model2_3d_push(raster, (*input++) >> 8); /* z */
		}
	}

	/* we're done */
	model2_3d_push(raster, 0);

	return input;
}

/* Command 03: Window Data */
u32 *model2_video::geo_window_data(geo_state *geo, u32 opcode, u32 *input)
{
	raster_state *raster = geo->raster;

	/* start by pushing the opcode */
	model2_3d_push(raster, opcode >> 23);

	raster->cur_window++;

	/*
	    we're going to move 6 coordinates to the 3d rasterizer:
	    - starting coordinate
	    - completion coordinate
	    - vanishing point 0 (eye mode 0)
	    - vanishing point 1 (eye mode 1)
	    - vanishing point 2 (eye mode 2)
	    - vanishing point 3 (eye mode 3)
	*/

	for (u32 i = 0; i < 6; i++)
	{
		/* read in the coordinate */
		u32 y = *input++;

		/* convert to the 3d rasterizer format (00XXXYYY) */
		u32 x = (y & 0x0fff0000) >> 4 ;
		y &= 0xfff;

		/* push it */
		model2_3d_push(raster, x | y);
	}

	return input;
}

/* Command 04: Texture Data Write */
u32 *model2_video::geo_texture_data(geo_state *geo, u32 opcode, u32 *input)
{
	raster_state *raster = geo->raster;

	/* start by pushing the opcode */
	model2_3d_push(raster, opcode >> 23);

	/* push the starting address/dsp id */
	model2_3d_push(raster, *input++);

	/* get the count */
	u32 count = *input++;

	/* push the count */
	model2_3d_push(raster, count);

	/* loop and send the data */
	for (u32 i = 0; i < count; i++)
		model2_3d_push(raster, *input++);

	return input;
}

/* Command 05: Polygon Data */
u32 *model2_video::geo_polygon_data(geo_state *geo, u32 opcode, u32 *input)
{
	u32  address, count, i;
	u32 *p;

	(void)opcode;

	/* read in the address */
	address = *input++;

	/* prepare the pointer */
	if (address & 0x01000000)
	{
		/* Fast polygon RAM */
		p = &geo->polygon_ram1[address & 0x7fff];
	}
	else
	{
		/* Slow Polygon RAM */
		p = &geo->polygon_ram0[address & 0x7fff];
	}

	/* read the count */
	count = *input++;

	/* move the data */
	for (i = 0; i < count; i++)
		*p++ = *input++;

	return input;
}

/* Command 06: Texture Parameters */
u32 *model2_video::geo_texture_parameters(geo_state *geo, u32 opcode, u32 *input)
{
	u32  index, count, i, param;

	(void)opcode;

	/* read in the index */
	index = (*input++) >> 2;

	/* read in the conut */
	count = *input++;

	for (i = 0; i < count; i++)
	{
		/* read in the texture parameters */
		param = *input++;

		geo->texture_parameters[index].diffuse = float(param & 0xff);
		geo->texture_parameters[index].ambient = float((param >> 8) & 0xff);
		geo->texture_parameters[index].specular_control = (param >> 24) & 0xff;
		geo->texture_parameters[index].specular_scale = float((param >> 16) & 0xff);

		/* read in the distance coefficient */
		geo->coef_table[index] = u2f(*input++);

		index = (index + 1) & 0x1f;
	}

	return input;
}

/* Command 07: Geo Mode */
u32 *model2_video::geo_mode(geo_state *geo, u32 opcode, u32 *input)
{
	(void)opcode;

	/* read in the mode */
	geo->mode = *input++;

	return input;
}

/* Command 08: ZSort Mode */
u32 *model2_video::geo_zsort_mode(geo_state *geo, u32 opcode, u32 *input)
{
	raster_state *raster = geo->raster;

	/* push the opcode */
	model2_3d_push(raster, opcode >> 23);

	/* push the mode */
	model2_3d_push(raster, (*input++) >> 8);

	return input;
}

/* Command 09: Focal Distance */
u32 *model2_video::geo_focal_distance(geo_state *geo, u32 opcode, u32 *input)
{
	(void)opcode;

	/* read the x focus value */
	geo->focus.x = u2f(*input++);

	/* read the y focus value */
	geo->focus.y = u2f(*input++);

	if (m_rec) smooth_note_focal(geo->focus.x, geo->focus.y);
	else if (m_replay && m_replay_focal < m_focal_blend.size()) {
		geo->focus.x = m_focal_blend[m_replay_focal][0];
		geo->focus.y = m_focal_blend[m_replay_focal][1];
		m_replay_focal++;
	}

	return input;
}

/* Command 0A: Light Source Vector Write */
u32 *model2_video::geo_light_source(geo_state *geo, u32 opcode, u32 *input)
{
	(void)opcode;

	/* read the x light value */
	geo->light.x = u2f(*input++);

	/* read the y light value */
	geo->light.y = u2f(*input++);

	/* read the z light value */
	geo->light.pz = u2f(*input++);

	if (m_rec) smooth_note_light(geo->light.x, geo->light.y, geo->light.pz);
	else if (m_replay && m_replay_light < m_light_blend.size()) {
		geo->light.x = m_light_blend[m_replay_light][0];
		geo->light.y = m_light_blend[m_replay_light][1];
		geo->light.pz = m_light_blend[m_replay_light][2];
		m_replay_light++;
	}

	return input;
}

/* Command 0B: Transformation Matrix Write */
u32 *model2_video::geo_matrix_write(geo_state *geo, u32 opcode, u32 *input)
{
	u32  i;

	(void)opcode;

	/* read in the transformation matrix */
	for (i = 0; i < 12; i++)
		geo->matrix[i] = u2f(*input++);

	return input;
}

/* Command 0C: Parallel Transfer Vector Write */
u32 *model2_video::geo_translate_write(geo_state *geo, u32 opcode, u32 *input)
{
	u32  i;

	(void)opcode;

	/* read in the translation vector */
	for (i = 0; i < 3; i++)
		geo->matrix[i+9] = u2f(*input++);

	return input;
}

/* Command 0D: Geo Data Memory Push (undocumented, unsupported) */
u32 *model2_video::geo_data_mem_push(geo_state *geo, u32 opcode, u32 *input)
{
	u32  address, count, i;

	/*
	    This command pushes data stored in the Geometry DSP's RAM
	    to the hardware 3D rasterizer. Since we don't emulate the
	    DSP, we don't know what the RAM contents are.

	    Eventually, we could check for the address, and if it
	    happens to point to a polygon ROM, we could potentially
	    emulate it partially.

	    No games are known to use this command yet.
	*/


	(void)opcode;

	/* read in the address */
	address = *input++;

	/* read in the count */
	count = *input++;

	logerror("SEGA GEO: Executing unsupported geo_data_mem_push (address = %08x, count = %08x)\n", address, count);

	(void)i;
/*
    for (i = 0; i < count; i++)
        model2_3d_push(0);
*/

	return input;
}

/* Command 0E: Geo Test */
u32 *model2_video::geo_test(geo_state *geo, u32 opcode, u32 *input)
{
	u32      data, blocks, address, count, checksum, i;

	(void)opcode;

	/* fifo test */
	data = 1;

	for (i = 0; i < 32; i++)
	{
		if (*input++ != data)
		{
			/* TODO: Set Red LED on */
			logerror("SEGA GEO: FIFO Test failed\n");
		}

		data <<= 1;
	}

	/* get the number of checksums we have to run */
	blocks = *input++;

	for (i = 0; i < blocks; i++)
	{
		u32  sum_even, sum_odd, j;

		/* read in the address */
		address = (*input++) & 0x7fffff;

		/* read in the count */
		count = *input++;

		/* read in the checksum */
		checksum = *input++;

		/* reset the checksum counters */
		sum_even = 0;
		sum_odd = 0;

		for (j = 0; j < count; j++)
		{
			data = geo->polygon_rom[address++];

			address &= geo->polygon_rom_mask;

			sum_even += data >> 16;
			sum_even &= 0xffff;

			sum_odd += data & 0xffff;
			sum_odd &= 0xffff;
		}

		sum_even += checksum >> 16;
		sum_even &= 0xffff;

		sum_odd += checksum & 0xffff;
		sum_odd &= 0xffff;

		if (sum_even != 0 || sum_odd != 0)
		{
			/* TODO: Set Green LED on */
			logerror("SEGA GEO: Polygon ROM Test failed\n");
		}
	}

	return input;
}

/* Command 0F: End */
u32 *model2_video::geo_end(geo_state *geo, u32 opcode, u32 *input)
{
	raster_state *raster = geo->raster;

	(void)opcode;

	/* signal the end of this data block the rasterizer */
	model2_3d_push(raster, 0xff000000);

	/* signal end by returning nullptr */
	return nullptr;
}

/* Command 10: Dummy */
u32 *model2_video::geo_dummy(geo_state *geo, u32 opcode, u32 *input)
{
//  u32  data;
	(void)opcode;

	/* do the dummy read cycle */
//  data = *input++;
	input++;

	return input;
}

/* Command 14: Log Data Write */
u32 *model2_video::geo_log_data(geo_state *geo, u32 opcode, u32 *input)
{
	raster_state *raster = geo->raster;
	u32  i, count;

	/* start by pushing the opcode */
	model2_3d_push(raster, opcode >> 23);

	/* push the starting address/dsp id */
	model2_3d_push(raster, *input++);

	/* get the count */
	count = *input++;

	/* push the count */
	model2_3d_push(raster, count << 2);

	/* loop and send the data */
	for (i = 0; i < count; i++)
	{
		u32  data = *input++;

		model2_3d_push(raster, data & 0xff);
		model2_3d_push(raster, (data >> 8) & 0xff);
		model2_3d_push(raster, (data >> 16) & 0xff);
		model2_3d_push(raster, (data >> 24) & 0xff);
	}

	return input;
}

/* Command 16: LOD */
u32 *model2_video::geo_lod(geo_state *geo, u32 opcode, u32 *input)
{
	(void)opcode;

	/* read in the LOD */
	geo->lod = u2f(*input++);

	return input;
}

/* Command 1D: Code Upload  (undocumented, unsupported) */
u32 *model2_video::geo_code_upload(geo_state *geo, u32 opcode, u32 *input)
{
	u32  count, i;

	/*
	    This command uploads code to program memory and
	    optionally runs it. Probably used for debugging.

	    No games are known to use this command yet.
	*/

	logerror("SEGA GEO: Uploading debug code (unimplemented)\n");

	(void)opcode;

	/* read in the flags */
//  flags = *input++;
	input++;

	/* read in the count */
	count = *input++;

	for (i = 0; i < count; i++)
	{
		[[maybe_unused]] u64  code;

		/* read the top part of the opcode */
		code = *input++;

		code <<= 32;

		/* the bottom part comes in two pieces */
		code |= *input++;
		code |= (*input++) << 16;
	}

	/*
	    Bit 10 of flags indicate whether to run iummediately after upload
	*/

/*
    if (flags & 0x400)
        code_jump();
*/

	return input;
}

/* Command 1E: Code Jump (undocumented, unsupported) */
u32 *model2_video::geo_code_jump(geo_state *geo, u32 opcode, u32 *input)
{
//  u32  address;

	/*
	    This command jumps to a specified address in program
	    memory. Code can be uploaded with function 1D.
	    Probably used for debugging.

	    No games are known to use this command yet.
	*/

	logerror("SEGA GEO: Jumping to debug code (unimplemented)\n");

	(void)opcode;

//  address = *input++ & 0x3ff;
	input++;

/*
    code_jump(address)
*/
	return input;
}

u32 *model2_video::geo_process_command(geo_state *geo, u32 opcode, u32 *input, bool *end_code)
{
	switch ((opcode >> 23) & 0x1f)
	{
		case 0x00: input = geo_nop(geo, opcode, input);                   break;
		case 0x01: input = geo_object_data(geo, opcode, input);           break;
		case 0x02: input = geo_direct_data(geo, opcode, input);           break;
		case 0x03: input = geo_window_data(geo, opcode, input);           break;
		case 0x04: input = geo_texture_data(geo, opcode, input);          break;
		case 0x05: input = geo_polygon_data(geo, opcode, input);          break;
		case 0x06: input = geo_texture_parameters(geo, opcode, input);    break;
		case 0x07: input = geo_mode(geo, opcode, input);                  break;
		case 0x08: input = geo_zsort_mode(geo, opcode, input);            break;
		case 0x09: input = geo_focal_distance(geo, opcode, input);        break;
		case 0x0a: input = geo_light_source(geo, opcode, input);          break;
		case 0x0b: input = geo_matrix_write(geo, opcode, input);          break;
		case 0x0c: input = geo_translate_write(geo, opcode, input);       break;
		case 0x0d: input = geo_data_mem_push(geo, opcode, input);         break;
		case 0x0e: input = geo_test(geo, opcode, input);                  break;
		case 0x0f: input = geo_end(geo, opcode, input); *end_code = true; break;
		case 0x10: input = geo_dummy(geo, opcode, input);                 break;
		case 0x11: input = geo_object_data(geo, opcode, input);           break;
		case 0x12: input = geo_direct_data(geo, opcode, input);           break;
		case 0x13: input = geo_window_data(geo, opcode, input);           break;
		case 0x14: input = geo_log_data(geo, opcode, input);              break;
		case 0x15: input = geo_polygon_data(geo, opcode, input);          break;
		case 0x16: input = geo_lod(geo, opcode, input);                   break;
		case 0x17: input = geo_mode(geo, opcode, input);                  break;
		case 0x18: input = geo_zsort_mode(geo, opcode, input);            break;
		case 0x19: input = geo_focal_distance(geo, opcode, input);        break;
		case 0x1a: input = geo_light_source(geo, opcode, input);          break;
		case 0x1b: input = geo_matrix_write(geo, opcode, input);          break;
		case 0x1c: input = geo_translate_write(geo, opcode, input);       break;
		case 0x1d: input = geo_code_upload(geo, opcode, input);           break;
		case 0x1e: input = geo_code_jump(geo, opcode, input);             break;
		case 0x1f: input = geo_end(geo, opcode, input); *end_code = true; break;
	}

	return input;
}

void model2_video::geo_parse()
{
	if (m_smooth) smooth_record_start();
	geo_run(m_bufferram, m_geo_read_start_address, m_geo.get(), m_raster.get());
	m_render_done = false;
	m_parsed_frame = m_geo_frame;
	if (m_smooth) smooth_record_end();
}

// The geometrizer through a display list (buffer RAM, or a copy kept for smooth motion)
void model2_video::geo_run(u32 *buffer, u32 start, geo_state *geo, raster_state *raster)
{
	u32  address = (start & 0x1ffff)/4;
	u32 *input = &buffer[address];
	u32  opcode;
	u32  op_count = 0;
	bool end_code = false;

	// reset raster frame variables
	render_frame_start(raster);

	while (end_code == false && (input - buffer) < 0x20000/4 && op_count++ < 0x8000)
	{
		/* read in the opcode */
		opcode = *input++;

		/* if it's a jump opcode, do the jump */
		if (opcode & 0x80000000)
		{
			/* get the address */
			address = (opcode & 0x1ffff) / 4;

			/* update our pointer */
			input = &buffer[address];

			/* go again */
			continue;
		}

		/* process it */
		input = geo_process_command(geo, opcode, input, &end_code);
	}
}

/***********************************************************************************************/

/***********************************************************************************************/

// model2rd.ipp: the scanline renderers

// (MAME's, in unsigned arithmetic and without branches: x and y are never negative here, and
// the three shifts are added up; the same texel)
inline u16 model2_renderer::get_texel(u32 base_x, u32 base_y, int x, int y, const u32 *sheet)
{
	u32 x2 = base_x + u32(x);
	u32 y2 = base_y + u32(y);
	// texture sheets are mapped as 2048x1024 but stored in RAM as 1024x2048
	const u32 right = x2 >= 1024 ? 1024u : 0u;
	x2 -= right;
	y2 ^= right;
	const u32 offset = ((y2 >> 1) << 9) + (x2 >> 1);
	const u32 texel = sheet[offset >> 1];
	const u32 shift = ((offset & 1) << 4) + ((~u32(y) & 1) << 3) + ((~u32(x) & 1) << 2);
	return (texel >> shift) & 0x0f;
}


// non-textured render path
template <bool Translucent>
void model2_renderer::draw_scanline_solid(int32_t scanline, const extent_t& extent, const m2_poly_extra_data& object, int threadid)
{
	model2_video *state = object.state;
	u32 *const p = &m_dest->pix(scanline);
	u8 *const fill = &m_fillmap.pix(scanline);
	u8  *gamma_value = &state->m_gamma_table[0];

	// extract color information
	const u16 *colortable_r = &state->m_colorxlat[0x0000/2];
	const u16 *colortable_g = &state->m_colorxlat[0x4000/2];
	const u16 *colortable_b = &state->m_colorxlat[0x8000/2];
	u32  color = object.colorbase;
	u8   checker = object.checker;
	u8   luma;
	u32  tr, tg, tb;

	// if it's translucent, there's nothing to render
	if (Translucent)
		return;

	luma = object.luma >> 2;

	color = state->m_palram[(color + 0x1000)] & 0xffff;

	colortable_r += ((color >>  0) & 0x1f) << 8;
	colortable_g += ((color >>  5) & 0x1f) << 8;
	colortable_b += ((color >> 10) & 0x1f) << 8;

	/* we have the 6 bits of luma information along with 5 bits per color component */
	/* now build and index into the master color lookup table and extract the raw RGB values */

	tr = colortable_r[(luma)] & 0xff;
	tg = colortable_g[(luma)] & 0xff;
	tb = colortable_b[(luma)] & 0xff;
	tr = gamma_value[tr];
	tg = gamma_value[tg];
	tb = gamma_value[tb];

	// build the final color
	color = rgb_t(tr, tg, tb);

	int x = extent.startx;
	int dx = checker ? 2 : 1;
	if (checker && !((x ^ scanline) & 1))
		x++;

	for ( ; x < extent.stopx; x += dx)
	{
		if (fill[x] == 0)
		{
			p[x] = color;
			fill[x] = 0xff;
		}
	}
}

constexpr u32 LERP(u32 x, u32 y, unsigned a)
{
	return (x + (((y - x) * a) >> 8)) & 0x00ff00ff;
}

template <bool Translucent>
u32 model2_renderer::fetch_bilinear_texel(const m2_poly_extra_data& object, s32 miplevel, s32 u, s32 v)
{
	u32 tex_wrap_x = object.texwrapx;
	u32 tex_wrap_y = object.texwrapy;
	u32 tex_mirr_x = object.texmirrorx;
	u32 tex_mirr_y = object.texmirrory;
	u32 tex_width, tex_height;
	u32 tex_x, tex_y;
	u32* sheet;

	if (miplevel == -1)
	{
		// microtexture
		tex_width = 128;
		tex_height = 128;
		tex_x = object.utexx;
		tex_y = object.utexy;
		sheet = object.texsheet[1];
		u <<= 1 << object.utexminlod;
		v <<= 1 << object.utexminlod;
	}
	else
	{
		// regular texture
		tex_width = object.texwidth >> miplevel;
		tex_height = object.texheight >> miplevel;
		tex_x = ((object.texx - 2048) >> miplevel) & 2047;
		tex_y = ((object.texy - 1024) >> miplevel) & 1023;
		sheet = object.texsheet[miplevel & 1];
		u >>= miplevel;
		v >>= miplevel;
	}

	if (tex_mirr_x && (u & (tex_width << 8)))
		u = ~u;

	if (tex_mirr_y && (v & (tex_height << 8)))
		v = ~v;

	// subtract 1/2 texel
	u -= 0x80;
	v -= 0x80;

	// extract the fractions to use as blending factors
	u32 ufrac = u & 0xff;
	u32 vfrac = v & 0xff;

	// get the four texel locations and confine to texture dimensions
	u32 u0 = (u >> 8) & (tex_width - 1);
	u32 u1 = (u0 + 1) & (tex_width - 1);
	u32 v0 = (v >> 8) & (tex_height - 1);
	u32 v1 = (v0 + 1) & (tex_height - 1);

	// clamp the texture coordinates if smooth wrapping is not enabled
	if (!tex_wrap_x && u1 == 0)
	{
		if (ufrac >= 0x80)
			u0 = u1, u1++, ufrac = 0;     // left edge of texture
		else
			u1 = u0, u0--, ufrac = 0x100; // right edge of texture
	}

	if (!tex_wrap_y && v1 == 0)
	{
		if (vfrac >= 0x80)
			v0 = 0, v1++, vfrac = 0;      // top edge of texture
		else
			v1 = v0, v0--, vfrac = 0x100; // bottom edge of texture
	}

	// read the four texels from the texture sheet (get_texel's addressing, its column and row
	// parts worked out once for the two columns and the two rows)
	auto col = [tex_x](u32 x, u32 &xo, u32 &flip, u32 &sh) {
		u32 x2 = tex_x + x;
		const u32 right = x2 >= 1024 ? 1024u : 0u;
		x2 -= right;
		xo = x2 >> 1;
		flip = right << 8;   // (y2 ^ 1024, as a row offset: bit 18)
		sh = ((xo & 1) << 4) + ((~x & 1) << 2);
	};
	u32 xo0, f0, s0, xo1, f1, s1;
	col(u0, xo0, f0, s0);
	col(u1, xo1, f1, s1);
	const u32 r0 = ((tex_y + v0) >> 1) << 9, r1 = ((tex_y + v1) >> 1) << 9;
	const u32 ys0 = (~v0 & 1) << 3, ys1 = (~v1 & 1) << 3;
	u32 tex00 = ((sheet[((r0 ^ f0) | xo0) >> 1] >> (s0 + ys0)) & 0x0f) << 4;
	u32 tex01 = ((sheet[((r0 ^ f1) | xo1) >> 1] >> (s1 + ys0)) & 0x0f) << 4;
	u32 tex10 = ((sheet[((r1 ^ f0) | xo0) >> 1] >> (s0 + ys1)) & 0x0f) << 4;
	u32 tex11 = ((sheet[((r1 ^ f1) | xo1) >> 1] >> (s1 + ys1)) & 0x0f) << 4;

	if (Translucent)
	{
		// pack the alpha components into the upper 16 bits
		if (tex00 != 0xf0) tex00 |= 0x00800000;
		if (tex01 != 0xf0) tex01 |= 0x00800000;
		if (tex10 != 0xf0) tex10 |= 0x00800000;
		if (tex11 != 0xf0) tex11 |= 0x00800000;

		// if a texel is transparent, it takes the luma value of the neighboring texel
		if (tex00 == 0x000000f0) tex00 = tex01 & 0xff;
		if (tex01 == 0x000000f0) tex01 = tex00 & 0xff;
		if (tex10 == 0x000000f0) tex10 = tex11 & 0xff;
		if (tex11 == 0x000000f0) tex11 = tex10 & 0xff;
	}

	// linearly interpolate between left and right texels
	u32 tex0x = LERP(tex00, tex01, ufrac);
	u32 tex1x = LERP(tex10, tex11, ufrac);

	if (Translucent)
	{
		if (tex0x == 0x000000f0) tex0x = tex1x & 0xff;
		if (tex1x == 0x000000f0) tex1x = tex0x & 0xff;
	}

	// calculate the final bilinear filtered texel
	return LERP(tex0x, tex1x, vfrac);
}

// mostly copied from video/voodoo_render.cpp
inline s32 ATTR_FORCE_INLINE fast_log2(float value)
{
	// return 0 for negative values; should never happen
	if (UNEXPECTED(value < 0.0F))
		return 0;

	// we only need the exponent and highest 7 bits of mantissa
	u32 ival = f2u(value) >> 16;

	// extract exponent
	s32 exp = (ival >> 7) - 127;

	// use top 7 bits of mantissa to look up fractional log2
	static u8 const s_log2_table[128] =
	{
		  0,   2,   5,   8,  11,  14,  16,  19,  22,  25,  27,  30,  33,  35,  38,  40,
		 43,  46,  48,  51,  53,  56,  58,  61,  63,  65,  68,  70,  73,  75,  77,  80,
		 82,  84,  87,  89,  91,  93,  96,  98, 100, 102, 104, 106, 109, 111, 113, 115,
		117, 119, 121, 123, 125, 127, 129, 132, 134, 136, 138, 140, 141, 143, 145, 147,
		149, 151, 153, 155, 157, 159, 161, 162, 164, 166, 168, 170, 172, 173, 175, 177,
		179, 181, 182, 184, 186, 188, 189, 191, 193, 194, 196, 198, 200, 201, 203, 205,
		206, 208, 209, 211, 213, 214, 216, 218, 219, 221, 222, 224, 225, 227, 229, 230,
		232, 233, 235, 236, 238, 239, 241, 242, 244, 245, 247, 248, 250, 251, 253, 254
	};

	// combine and return result
	return (exp << 8) | s_log2_table[ival & 127];
}

// textured render path
template <bool Translucent>
void model2_renderer::draw_scanline_tex(int32_t scanline, const extent_t &extent, const m2_poly_extra_data& object, int threadid)
{
	model2_video *state = object.state;
	u32 *const p = &m_dest->pix(scanline);
	u8 *const fill = &m_fillmap.pix(scanline);

	/* extract color information */
	const u16 *colortable_r = &state->m_colorxlat[0x0000/2];
	const u16 *colortable_g = &state->m_colorxlat[0x4000/2];
	const u16 *colortable_b = &state->m_colorxlat[0x8000/2];
	const u8 *lumaram = &state->m_lumaram[0];
	u32  colorbase = object.colorbase;
	u32  lumabase = object.lumabase;
	u8   checker = object.checker;
	u8  *gamma_value = &state->m_gamma_table[0];
	float ooz = extent.param[0].start;
	float uoz = extent.param[1].start;
	float voz = extent.param[2].start;
	float dooz = extent.param[0].dpdx;
	float duoz = extent.param[1].dpdx;
	float dvoz = extent.param[2].dpdx;

	// calculate maximum mipmap level from texture dimensions; we go down to 2x2
	s32 max_level = 30 - std::countl_zero(std::min(object.texwidth, object.texheight));

	colorbase = state->m_palram[(colorbase + 0x1000)] & 0x7fff;

	colortable_r += ((colorbase >>  0) & 0x1f) << 8;
	colortable_g += ((colorbase >>  5) & 0x1f) << 8;
	colortable_b += ((colorbase >> 10) & 0x1f) << 8;

	int x = extent.startx;
	int dx = 1;
	if (checker)
	{
		// if the first pixel is transparent, skip to the next one
		if (!((x ^ scanline) & 1))
		{
			x++;
			ooz += dooz;
			uoz += duoz;
			voz += dvoz;
		}

		// increment by 2 pixels each time, skipping every other pixel
		dx = 2;
		dooz *= 2.0F;
		duoz *= 2.0F;
		dvoz *= 2.0F;
	}

	for ( ; x < extent.stopx; x += dx, ooz += dooz, uoz += duoz, voz += dvoz)
	{
		if (fill[x] > 0)
			continue;

		float const z = 1.0F / ooz;

		s32 const mml = -object.texlod + fast_log2(z);    // equivalent to log2(z^2)
		s32 const level = std::clamp(mml >> 7, 0, max_level);

		// we give texture coordinates 8 fractional bits
		s32 const u = s32(uoz * z * 256.0F);
		s32 const v = s32(voz * z * 256.0F);

		u32 t = fetch_bilinear_texel<Translucent>(object, level, u, v);

		if (mml > 0 && level < max_level)
		{
			u32 const t2 = fetch_bilinear_texel<Translucent>(object, level + 1, u, v);
			s32 const frac = (mml & 127) << 1;
			t = LERP(t, t2, frac);
		}
		else if (object.utex && mml < 0)
		{
			// microtexture; blend up to almost 50%
			u32 const t2 = fetch_bilinear_texel<Translucent>(object, -1, u, v);
			s32 const frac = std::min(-mml >> object.utexminlod, 127);
			t = LERP(t, t2, frac);
		}

		if (Translucent)
		{
			// if alpha is less than 50%, discard
			if (t < 0x00400000)
				continue;

			// remove the alpha value; no longer needed
			t &= 0xff;
		}

		// filtered texel has 8 bits of precision but translator map has 128 (7-bit) entries; need to shift right by 1
		u8 luma = u32(lumaram[lumabase + (t >> 1)]) * object.luma / 256;

		// Virtua Striker sets up a luma of 0x40 for national flags on bleachers, fix here.
		luma = std::min(luma, u8(0x3f));

		// we have the 6 bits of luma information along with 5 bits per color component
		// now build and index into the master color lookup table and extract the raw RGB values
		u32 tr = colortable_r[luma] & 0xff;
		u32 tg = colortable_g[luma] & 0xff;
		u32 tb = colortable_b[luma] & 0xff;
		tr = gamma_value[tr];
		tg = gamma_value[tg];
		tb = gamma_value[tb];

		p[x] = rgb_t(tr, tg, tb);
		fill[x] = 0xff;
	}
}
