// license:BSD-3-Clause
// copyright-holders:R. Belmont, Olivier Galibert, ElSemi, Angelo Salese, Matthew Daniels
// Sega Model 2 3D: the geometrizer, the rasterizer's Z-sort and clip, and the polygon renderer,
// as MAME's sega/model2.h declares them (BSD-3-Clause, copyright-holders: R. Belmont, Olivier
// Galibert, ElSemi, Angelo Salese, Matthew Daniels), in a class of their own: model2_video holds
// the members of model2_state the functions in model2_video.cpp use, under the same names.
#pragma once
#include <array>
#include "mame_lite.h"
#include "poly.h"
#include <memory>

class model2_video;

// rgb_t's packing: alpha in the top byte, so a black pixel isn't the transparent 0
inline uint32_t rgb_t(uint32_t r, uint32_t g, uint32_t b) { return 0xff000000 | (r << 16) | (g << 8) | b; }

#define ATTR_FORCE_INLINE __attribute__((always_inline))
#define UNEXPECTED(x) __builtin_expect(!!(x), 0)

class bitmap_ind8
{
public:
	bitmap_ind8(int w, int h) : m_w(w), m_pix(size_t(w) * h, 0) {}
	void allocate(int w, int h) { m_w = w; m_pix.assign(size_t(w) * h, 0); }
	uint8_t &pix(int y, int x = 0) { return m_pix[size_t(y) * m_w + x]; }
	void fill(uint8_t v, const rectangle &r)
	{
		for (int y = r.min_y; y <= r.max_y; y++) std::fill(&pix(y, r.min_x), &pix(y, r.max_x) + 1, v);
	}
private:
	int m_w;
	std::vector<uint8_t> m_pix;
};

struct m2_poly_extra_data
{
	model2_video *  state;
	u32      lumabase;
	u32      colorbase;
	u8       checker;
	u32 *    texsheet[2];
	u32      texwidth;
	u32      texheight;
	u32      texx;
	u32      texy;
	u8       texwrapx;
	u8       texwrapy;
	u8       texmirrorx;
	u8       texmirrory;
	u8       utex;
	u8       utexminlod;
	u32      utexx;
	u32      utexy;
	s32      texlod;
	u8       luma;
};

class model2_renderer;
struct gpu3d_poly;
struct gpu3d_rams;

class model2_video
{
public:
	struct plane;
	struct texture_parameter;
	struct polygon;
	struct quad_m2;
	struct raster_state;
	struct geo_state;

	// The board's memories, as MAME's model2_state holds them. bufferram, the texture RAMs and
	// the luma RAM are the board's; the ROMs are the regions.
	model2_video(u32 *bufferram, u32 *textureram0, u32 *textureram1, u8 *lumaram, u16 *palram,
				 u16 *colorxlat, u8 *gamma, const u8 *texture_rom, size_t texture_rom_bytes,
				 const u8 *polygon_rom, size_t polygon_rom_bytes);
	~model2_video();

	// screen_vblank: the geometrizer runs through the display list in buffer RAM
	void geo_parse();
	// screen_update's 3D part: frame_end() at the end of every vblank, when MAME draws (the
	// polygons, or nothing new if the geometrizer hasn't run), composite() puts it on a screen
	// (or the framebuffer, in render test mode)
	void frame_end(bool render_test_mode, u64 frame_number);
	void composite(bitmap_rgb32 &bitmap, const rectangle &cliprect);
	// What composite() puts on the screen, for drawing it with the rest: the 3D at the scale's size
	// (transparent pen 0), or nothing; or, in render test mode, the framebuffer (composite() only).
	const bitmap_rgb32 *layer();
	// layer() in two, for drawing on another thread: the picture it shows (`map`, or null), and the
	// picture whose polygons still have to be drawn into it first (-1: none), counted as drawn
	// now; draw_layer(that) then draws them (on the other thread, while nothing changes what they
	// read: the board waits for it first).
	int layer_to_draw(const bitmap_rgb32 *&map);
	void draw_layer(int picture) { raster_drawn(picture); }
	// smooth motion's picture (3: smooth_layer's, replay_layer's; drawn again each time it's used)
	// drawn straight into `out` (rows `stride` apart, the screen's size) instead of its map, only
	// rows y0 to y1 (drawn_rows'), the rest left as it was
	void draw_layer_into(int picture, u32 *out, size_t stride, int y0, int y1);
	// The rows (screen pixels, y1 not included) the polygons of the 3D in `map` (a layer's) reach
	// at most, a pixel to spare each way: the rest is clear
	void drawn_rows(const bitmap_rgb32 *map, int &y0, int &y1) const;
	bool shows_framebuffer() const { return m_render_test_mode; }
	// The polygons in the 3D layer() shows (0: none), and in the one drawn `ago` frames before the
	// newest (0-2: with a delay, the 3D still to be shown)
	int polygons_shown() const;
	// The GPU renderer's (src/gpu/gpu3d.h, step 1): the 3D shown's list, in the order drawn, as
	// gpu3d_poly; and, for checking its coverage, each pixel's polygon in that list (its place;
	// 0xffffffff none) as the CPU's rasterizer finds it, at the render scale (render_size: w x h).
	// Neither changes what is drawn.
	void export_shown(std::vector<gpu3d_poly> &out) const;
	// any picture's list (0-3: shown_index's, smooth_layer's), for the GPU's drawing in the game
	void export_picture(int picture, std::vector<gpu3d_poly> &out) const;
	// the picture shown now (its list and its map), not marked drawn (-1: none)
	int shown_index() const { return m_render_test_mode ? -1 : shown_now(); }
	// The host's Auto (CPU or GPU): a made-up scene the size of a fight's (bench_make), drawn on
	// the CPU at the render scale (bench_cpu: ms, on every core, into a picture of its own), or
	// handed over for the GPU (bench_export). Nothing the game shows changes.
	void bench_make(int n);
	void bench_take(int picture);   // (or a picture's own list: the game's scene)
	double bench_cpu(int part = 0, int parts = 1);   // (or a part of its rows: part of parts bands)
	const bitmap_rgb32 &bench_picture() const { return m_bench_map; }   // (bench_cpu's: checking the GPU's against it)
	void bench_export(std::vector<gpu3d_poly> &out) const;
	void owner_map_shown(u32 *out, size_t stride);
	// The RAMs the textured polygons are coloured from, for the GPU (gpu3d_rams), with a count of
	// each one's changes: the board counts them (note_ram, before and as it changes one)
	void gpu_rams(gpu3d_rams &out) const;
	enum { GPU_TEX0, GPU_TEX1, GPU_LUMA, GPU_XLAT };
	void note_ram(int which) { m_ram_gen[which]++; }
	u32 m_ram_gen[4] = {};
	void render_size(int &w, int &h) const { w = (496 + 2 * m_ext) * m_scale; h = 384 * m_scale; }
	int render_scale() const { return m_scale; }
	int polygons_drawn(int ago) const;
	// The 3D shown `frames` (0-2) after MAME shows it. 0 is MAME's screen. VF2 scrolls its 2D
	// backdrop for a camera two frames after MAME draws that camera's 3D (at every cut, and while
	// the camera pans): with 2, each screen has the 3D its backdrop was set for.
	// (cut: no blending across the change, which holds or skips a frame; false for a change
	// carried by set_shift, which neither holds nor skips)
	void set_delay(int frames, bool cut = true);
	// Smooth motion's pictures a further `frames` (0-1) back in time, on top of the delay: scene
	// sync's delay changed gradually (the 3D a little slower or faster for a moment) instead of a
	// frame at a time (a picture held, or one skipped).
	void set_shift(float frames) { m_shift = std::clamp(frames, 0.0f, 1.0f); }
	float delay_exact() const { return float(m_delay) + m_shift; }
	// Deferred drawing (for a host that shows only some frames): a frame's polygons are still set
	// up when MAME draws them, but drawn into their picture only when it's shown (layer()), or
	// before the RAM they're coloured from changes (texture RAM, the palette from 0x1000,
	// colorxlat, lumaram: draw_deferred(), which the board calls first). The same pixels, drawn
	// only for the frames shown.
	void defer_drawing(bool on);
	bool deferred() const { return m_deferred; }
	void draw_deferred();
	void model2_3d_zclip_w(u32 data);
	u32 polygon_count_r();
	uint64_t m_polygon_count_reads = 0;   // (polygon_count_r's: a save state leaves the count out, being the picture's)
	// Save states (the board's): the registers the game writes and the 3D keeps (xhout, xvout, the z clip; the
	// read start is m_geo_read_start_address). The rest (the geometrizer's matrices and polygons, the lists) is
	// the picture's alone, and goes by the widescreen setting, so a state leaves it out.
	void get_regs(int16_t &xoffset, int16_t &yoffset, u8 &zclip) const;
	void set_regs(int16_t xoffset, int16_t yoffset, u8 zclip);
	// A cold start's: the geometrizer and rasterizer as made, nothing kept from before (the board's power_on)
	void power_on();
	// The 3D drawn at `scale` times the arcade's 496 x 384 (1: MAME's picture). Only where the
	// polygons fall on the screen is scaled: each pixel's colour comes from the same texels, at
	// its own u, v and z, by the same code. The last polygons drawn are drawn again at once.
	void set_scale(int scale);
	int scale() const { return m_scale; }
	// Widescreen: `ext` arcade pixels more on each side. A window as wide as the screen opens out
	// to the sides (its viewport, and so its clip planes, widened; the same centre and focus: the
	// same vertical view, more of it across), and the 3D is drawn (496 + 2 ext) x 384 at the
	// scale's size, the arcade's column 0 at `ext`. 0 is MAME's screen. The pictures drawn so far
	// are dropped (their polygons were placed for the old width).
	void set_ext(int ext);
	int ext() const { return m_ext; }
	void horizontal_sync_w(u16 data);
	void vertical_sync_w(u16 data);

	// Smooth motion (model2_smooth.cpp), the host's: each frame's display list is kept with the
	// geometrizer's state as it began, and the 3D is set up again for a moment between two frames:
	// every object's matrix (its place and turn, and the camera's), the lights and the focus
	// blended with the same object's in the frame before. Off (vf2run): nothing kept, nothing
	// changes. m_geo_frame: the frame the next geo_parse() is for (the board sets it).
	void set_smooth(bool on);
	bool smooth() const { return m_smooth; }
	void smooth_reset();   // (the board reset: the lists kept belong to the run before)
	u64 m_geo_frame = 0;
	// The 3D `alpha` (0-1) of the way from the one shown a frame before `frame` to the one shown
	// at `frame` (the delay as it is now), set up into a picture of its own: `map`, and the
	// picture to draw_layer() before reading it. -1: none kept (draw the frame as it is).
	int smooth_layer(u64 frame, float alpha, const bitmap_rgb32 *&map);
	// The next pictures start from the newest frame shown, not blended from the one before.
	void smooth_cut();
	int m_smooth_stats[3] = {};   // the last smooth picture's objects: unmatched, jumped, blended
	u64 m_ram_changed = 0;        // the frame the RAM the polygons are coloured from last changed in, when the board couldn't keep the changes
	u64 m_smooth_fb = 0;          // the frame of the list the last smooth picture was set up from
	// The lists and the blend the last smooth_layer() set up (a: none when nothing was blended),
	// for drawing that moment again later (at another size); and the list of the 3D shown now.
	struct smooth_rec;
	struct moment { std::shared_ptr<const smooth_rec> a, b; float alpha = 1; };
	moment m_last_moment;
	std::shared_ptr<const smooth_rec> shown_list() const;
	// That moment set up again (the newer list alone when `a` is null), into smooth motion's picture
	int replay_layer(const moment &mo, const bitmap_rgb32 *&map);
	// After a new width: the pictures still to be shown set up again from their lists (true), or
	// dropped (no list kept); then drawn by draw_layer, which the board does in their own colours.
	bool rebuild_picture(int picture);
	int shown_picture(int ago) const { return m_shown[2 - std::clamp(ago, 0, 2)]; }
	u64 picture_frame(int picture) const { return m_pic_frame[picture]; }
	bool picture_deferred(int picture) const;
	// The newest frame's 3D is another shot (a camera cut: smooth motion's pairing with the frame
	// before finds most objects jumped, or most can't be paired). Needs the lists kept.
	bool newest_cut();
	void picture_drawn(int picture);

	u32 m_geo_read_start_address = 0;
	u16 *m_fbvramA = nullptr, *m_fbvramB = nullptr;

	// what the renderer reads (model2rd.ipp)
	u32 *m_bufferram;
	u32 *m_textureram0, *m_textureram1;
	u8 *m_lumaram;
	u16 *m_palram;
	u16 *m_colorxlat;
	u8 *m_gamma_table;

private:
	friend class model2_renderer;
	friend class texdump;          // VF2_DUMP_TEXTURES (texdump.h)
	std::unique_ptr<raster_state> m_raster;
	std::unique_ptr<geo_state> m_geo;
	std::unique_ptr<model2_renderer> m_renderer;
	bool m_render_done = false;
	bool m_render_test_mode = false;
	// MAME draws the polygons into the renderer's destmap and copies that (transparent pen 0) into
	// the screen's 3D layer, which screen_update copies (transparent pen 0) onto the screen; here
	// composite() copies the destmap straight onto the screen, when MAME's layer would hold it.
	// The destmap is one of three pictures (a delay keeps the last ones), each with the polygons
	// drawn in it; m_shown has the last three frames' pictures (-1: none), the newest last.
	struct picture3d;
	std::unique_ptr<picture3d[]> m_pictures;
	int m_shown[3] = { -1, -1, -1 };
	int m_delay = 0;
	bool m_defer = false, m_deferred = false;   // deferring; a picture's polygons not drawn yet
	int shown_now() const { return m_shown[2 - m_delay]; }
	int m_scale = 1, m_ext = 0;
	int16_t m_crtc_xoffset = 0, m_crtc_yoffset = 0;
	u64 m_frame_number = 0;

	// The polygons of a list drawn, in the order drawn, set up for the rasterizer (projected, in
	// arcade pixels) by model2_renderer::prepare: drawn in row bands, a band a thread.
	struct drawn_poly;
	void raster_drawn(int picture, bitmap_rgb32 *into = nullptr, int y0 = 0, int y1 = -1);
	void draw_picture(int picture);   // a deferred one

	void render_frame_start(raster_state *raster);
	// smooth motion
	bool m_smooth = false;
	std::vector<std::shared_ptr<smooth_rec>> m_recs;   // the last frames' lists, oldest first (a moment kept elsewhere keeps its own)
	smooth_rec *m_rec = nullptr;                       // the one being kept (in geo_parse)
	std::unique_ptr<geo_state> m_geo_r;                // the geometrizer and rasterizer set up again
	std::unique_ptr<raster_state> m_raster_r;
	bool m_replay = false;
	size_t m_replay_obj = 0, m_replay_light = 0, m_replay_focal = 0;
	std::vector<std::array<float, 12>> m_blend;        // per object of the newer list: its matrix now
	std::vector<u8> m_blend_ok;
	std::vector<std::array<float, 3>> m_light_blend;
	std::vector<std::array<float, 2>> m_focal_blend;
	const smooth_rec *m_pair_a = nullptr, *m_pair_b = nullptr;   // the pairing worked out for these
	std::vector<int> m_pair;                           // per object of the newer list, the older's (-1)
	bool m_pair_cut = false;
	int m_pair_jumps = 0;
	int m_repaired = 0;                                // objects paired again by place (smooth_pair)
	std::vector<char> m_in_changed_run;                // per object of the newer list: in a run of one model that changed length
	float m_shift = 0;                                 // set_shift
	bool m_gradual = false;                            // the delay set without a cut: smooth moments from the newest frame back
	u64 m_smooth_floor = 0;                            // no blending from a frame before this one
	u64 m_parsed_frame = 0, m_pic_frame[4] = {};       // the last list's frame; each picture's list's
	void smooth_record_start();
	void smooth_record_end();
	void smooth_pair(const smooth_rec &a, const smooth_rec &b);
	void repair_by_camera(const smooth_rec &a, const smooth_rec &b);
	void pair_still(const smooth_rec &a, const smooth_rec &b, std::vector<char> &still);   // what sat still in the world (still[k]) paired by the camera's move
	// the camera's move from a to b fitted to the paired objects' places (rigid, least squares): false if too few
	bool fit_camera(const smooth_rec &a, const smooth_rec &b, double R[3][3], double T[3]) const;
	void replay_into(const moment &mo, std::vector<drawn_poly> &drawn);
	void smooth_note_object(u32 oba, const float *matrix);
	void smooth_note_focal(float x, float y);
	void smooth_note_light(float x, float y, float z);
	void render_polygons();
	void build_drawn(raster_state *raster, std::vector<drawn_poly> &drawn);
	void geo_run(u32 *buffer, u32 start, geo_state *geo, raster_state *raster);
	void draw_framebuffer(bitmap_rgb32 &bitmap, const rectangle &cliprect);
	void export_list(const std::vector<drawn_poly> &list, std::vector<gpu3d_poly> &out) const;
	std::vector<drawn_poly> m_bench;
	bitmap_rgb32 m_bench_map;

	u32 *geo_process_command( geo_state *geo, u32 opcode, u32 *input, bool *end_code );
	// geo commands
	u32 *geo_nop( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_object_data( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_direct_data( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_window_data( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_texture_data( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_polygon_data( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_texture_parameters( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_mode( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_zsort_mode( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_focal_distance( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_light_source( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_matrix_write( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_translate_write( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_data_mem_push( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_test( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_end( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_dummy( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_log_data( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_lod( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_code_upload( geo_state *geo, u32 opcode, u32 *input );
	u32 *geo_code_jump( geo_state *geo, u32 opcode, u32 *input );
	// geo code drawing paths
	void geo_parse_np_ns( geo_state *geo, u32 *input, u32 count );
	void geo_parse_np_s( geo_state *geo, u32 *input, u32 count );
	void geo_parse_nn_ns( geo_state *geo, u32 *input, u32 count );
	void geo_parse_nn_s( geo_state *geo, u32 *input, u32 count );

	// raster functions
	// main data input port
	void model2_3d_push(raster_state *raster, u32 input);
	// polygon push path
	template <unsigned NumVerts>
	void model2_3d_process_polygon(raster_state *raster, u32 attr);

	// inliners
	inline void model2_3d_project(polygon *poly);
	inline u16 float_to_zval(float floatval, s32 z_adjust);
	inline bool check_culling(raster_state *raster, u32 attr, float min_z, float max_z);
};

/*****************************
 *
 * Modern polygon renderer
 *
 *****************************/

// 0x10000 = size of the poly_sorted_list array
class model2_renderer : public poly_manager<float, m2_poly_extra_data, 4>
{
public:
	using polygon = model2_video::polygon;

	model2_renderer(model2_video& state) :
		m_render_callbacks{
				[this](int32_t s, const extent_t &e, const m2_poly_extra_data &o, int t) { draw_scanline_solid<false>(s, e, o, t); },
				[this](int32_t s, const extent_t &e, const m2_poly_extra_data &o, int t) { draw_scanline_solid<true>(s, e, o, t); },
				[this](int32_t s, const extent_t &e, const m2_poly_extra_data &o, int t) { draw_scanline_tex<false>(s, e, o, t); },
				[this](int32_t s, const extent_t &e, const m2_poly_extra_data &o, int t) { draw_scanline_tex<true>(s, e, o, t); } },
		m_state(state),
		m_fillmap(512, 512),
		m_xoffs(90),
		m_yoffs(-8)
	{
		m_destmap.allocate(512, 512);
	}

	bitmap_rgb32 &destmap() { return *m_dest; }
	bitmap_ind8 &fillmap() { return m_fillmap; }
	// The maps for drawing at a scale and width (MAME's 512 x 512 at 1x, 4:3)
	static void allocate(bitmap_rgb32 &map, int scale, int ext) { map.allocate(std::max(512, (496 + 2 * ext) * scale), std::max(512, 384 * scale)); }
	void allocate(int scale, int ext)
	{
		allocate(m_destmap, scale, ext);
		m_fillmap.allocate(std::max(512, (496 + 2 * ext) * scale), std::max(512, 384 * scale));
	}
	// Where the polygons are drawn (the destmap, or another picture of the same size)
	void set_dest(bitmap_rgb32 *map) { m_dest = map ? map : &m_destmap; }

	// MAME's model2_3d_render in two: what it works out for a polygon (its object data, the
	// viewport, the vertices' 1/z, u/z and v/z when it's textured), into a drawn_poly; then its
	// scan conversion within `band` (screen rows), at `scale` pixels per arcade pixel.
	void prepare(polygon *poly, const rectangle &cliprect, model2_video::drawn_poly &out);
	void draw(const model2_video::drawn_poly &p, int scale, const rectangle &band);
	// The same scan conversion, writing `index` where no polygon before it has (owner: 0xffffffff
	// none yet), not the picture: which polygon owns each pixel (model2_video::owner_map_shown)
	void draw_owner(const model2_video::drawn_poly &p, int scale, u32 index, bitmap_rgb32 &owner);
	void set_xoffset(int16_t xoffs) { m_xoffs = xoffs; }
	void set_yoffset(int16_t yoffs) { m_yoffs = yoffs; }

	template <bool Translucent>
	void draw_scanline_solid(int32_t scanline, const extent_t &extent, const m2_poly_extra_data &object, int threadid);

	template <bool Translucent>
	void draw_scanline_tex(int32_t scanline, const extent_t &extent, const m2_poly_extra_data &object, int threadid);

private:
	render_delegate m_render_callbacks[4];

	model2_video &m_state;
	bitmap_rgb32 m_destmap;
	bitmap_rgb32 *m_dest = &m_destmap;
	bitmap_ind8 m_fillmap;
	int16_t m_xoffs, m_yoffs;

	template <bool Translucent>
	u32 fetch_bilinear_texel(const m2_poly_extra_data& object, s32 miplevel, s32 fu, s32 fv);

	static u16 get_texel(u32 base_x, u32 base_y, int x, int y, const u32 *sheet);
};

typedef model2_renderer::vertex_t poly_vertex;

struct model2_video::drawn_poly
{
	poly_vertex v[8];               // projected (arcade pixels); 1/z, u/z, v/z when textured
	u8 num_vertices = 0;
	u8 renderer = 0;                // (texheader[0] >> 13) & 3: textured, translucent
	rectangle vp;                   // the viewport within the screen (arcade pixels, inclusive)
	float miny = 0, maxy = 0;       // the vertices' extent, for skipping bands (+-inf: never)
	m2_poly_extra_data extra{};
};

// A 3D picture: the polygons drawn (picture 0 in the renderer's destmap) and the list drawn
struct model2_video::picture3d
{
	bitmap_rgb32 map;
	std::vector<drawn_poly> drawn;
	bool deferred = false;   // the list not drawn in the map yet
};


/*******************************************
 *
 *  Basic Data Types
 *
 *******************************************/

struct model2_video::plane
{
	plane() : normal(0, 0)
	{
		std::fill(std::begin(normal.p), std::end(normal.p), 0);
	}

	poly_vertex     normal;
	float           distance = 0;
};

struct model2_video::texture_parameter
{
	float           diffuse = 0;
	float           ambient = 0;
	u32             specular_control = 0;
	float           specular_scale = 0;
};

struct model2_video::polygon
{
	polygon() : v{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
				   { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } }
	{
		for (poly_vertex &vertex : v)
			std::fill(std::begin(vertex.p), std::end(vertex.p), 0);
	}

	void *          next = nullptr;
	poly_vertex     v[8];
	u8              num_vertices = 3;
	u16             z = 0;
	u16             texheader[4] = { 0, 0, 0, 0 };
	u8              luma = 0;
	s32             texlod = 0;
	int16_t         viewport[4] = { 0, 0, 0, 0 };
	int16_t         center[2] = { 0, 0 };
	u8              window = 0;
};

struct model2_video::quad_m2
{
	quad_m2() : v{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } }
	{
		for (poly_vertex &vertex : v)
			std::fill(std::begin(vertex.p), std::end(vertex.p), 0);
	}

	poly_vertex     v[4];
	u16             z = 0;
	u16             texheader[4] = { 0, 0, 0, 0 };
	u8              luma = 0;
	s32             texlod = 0;
};

/*******************************************
 *
 *  Hardware 3D Rasterizer Internal State
 *
 *******************************************/

#define MAX_POLYGONS       32768

struct model2_video::raster_state
{
	raster_state()
	{
		std::fill(std::begin(command_buffer), std::end(command_buffer), 0);
		std::fill(std::begin(poly_sorted_list), std::end(poly_sorted_list), nullptr);
		std::fill(std::begin(texture_ram), std::end(texture_ram), 0);
		std::fill(std::begin(log_ram), std::end(log_ram), 0);
	}

//  u32             mode = 0;                       // bit 0 = Test Mode, bit 2 = Switch 60Hz(1)/30Hz(0) operation
	u16 *           texture_rom = nullptr;          // Texture ROM pointer
	u32             texture_rom_mask = 0;           // Texture ROM mask
	int16_t         viewport[4] = { 0, 0, 0, 0 };   // View port (startx,starty,endx,endy)
	bool            widened = false;                // (widescreen: the viewport opened out by ext each side)
	int16_t         center[4][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } }; // Centers (eye 0[x,y],1[x,y],2[x,y],3[x,y])
	u16             center_sel = 0;                 // Selected center
	u32             reverse = 0;                    // Left/Right Reverse
	s32             z_adjust = 0;                   // ZSort Mode
	float           polygon_z = 0;                  // Current polygon z value
	u8              master_z_clip = 0;              // Master Z-Clip value
	u32             cur_command = 0;                // Current command
	u32             command_buffer[32];             // Command buffer
	u32             command_index = 0;              // Command buffer index
	polygon         poly_list[MAX_POLYGONS];        // Polygon list
	u32             poly_list_index = 0;            // Polygon list index
	polygon *       poly_sorted_list[0x10000];      // Sorted polygon list
	u16             min_z = 0;                      // Minimum sortable Z value
	u16             max_z = 0;                      // Maximum sortable Z value
	u16             texture_ram[0x10000];           // Texture RAM pointer
	u8              log_ram[0x8000];                // Log RAM pointer
	u8              cur_window = 0;                 // Current window
	plane           clip_plane[4][4];               // Polygon clipping planes
};

/*******************************************
 *
 *  Geometry Engine Internal State
 *
 *******************************************/

struct model2_video::geo_state
{
	geo_state() : focus(0, 0), light(0, 0)
	{
		std::fill(std::begin(matrix), std::end(matrix), 0);
		std::fill(std::begin(focus.p), std::end(focus.p), 0);
		std::fill(std::begin(light.p), std::end(light.p), 0);
		std::fill(std::begin(coef_table), std::end(coef_table), 0);
		std::fill(std::begin(polygon_ram0), std::end(polygon_ram0), 0);
		std::fill(std::begin(polygon_ram1), std::end(polygon_ram1), 0);
	}

	raster_state *      raster = nullptr;
	u32                 mode = 0;                   // bit 0 = Enable Specular, bit 1 = Calculate Normals
	u32 *               polygon_rom = nullptr;      // Polygon ROM pointer
	u32                 polygon_rom_mask = 0;       // Polygon ROM mask
	float               matrix[12];                 // Current Transformation Matrix
	poly_vertex         focus;                      // Focus (x,y)
	poly_vertex         light;                      // Light Vector
	float               lod = 0;                    // LOD
	float               coef_table[32];             // Distane Coefficient table
	texture_parameter   texture_parameters[32];     // Texture parameters
	u32                 polygon_ram0[0x8000];       // Fast Polygon RAM pointer
	u32                 polygon_ram1[0x8000];       // Slow Polygon RAM pointer
	model2_video *      state = nullptr;
};

// Smooth motion: a frame's display list as the geometrizer read it, its state as the list began
// (what the list doesn't set carries over from the frames before), and what it met on the way:
// each object's polygon address and matrix, the lights, the focus settings.
struct model2_video::smooth_rec
{
	u64 frame = 0;
	u32 start = 0;
	std::vector<u32> list;                             // buffer RAM, 128 KB
	geo_state geo;                                     // (raster: null)
	// the rasterizer's registers (what render_frame_start leaves), and its texture header and
	// log RAMs, which the list's polygons are set up from
	int16_t viewport[4]; bool widened; int16_t center[4][2]; u16 center_sel; u32 reverse; s32 z_adjust;
	u8 master_z_clip; u32 cur_command; u32 command_buffer[32]; u32 command_index;
	plane clip_plane[4][4];
	std::vector<u16> texture_ram;
	std::vector<u8> log_ram;
	struct object { u32 oba; float m[12]; };
	std::vector<object> objects;
	std::vector<std::array<float, 3>> lights;
	std::vector<std::array<float, 2>> focals;
};
