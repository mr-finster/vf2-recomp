// The 3D on the GPU. The board sets up a picture's polygons as it does for its own rasterizer
// (model2_video's drawn list: projected, clipped to their windows, in the order the rasterizer
// draws them) and hands them over as gpu3d_poly; a backend draws them on the GPU.
//
// Step 1: coverage (which polygon owns each pixel, compared against the CPU's:
// model2_video::owner_map_shown, tools/gpu3dcmp.cpp). Step 2: the colours, as the CPU's scanline
// renderers work them out (model2_video.cpp, from MAME's model2rd.ipp): a solid polygon's worked
// out here (gpu3d_poly::colour), a textured one's per pixel on the GPU from the texture RAMs, the
// luma RAM, colorxlat and gamma (gpu3d_rams), each texel filtered and mipmapped as
// fetch_bilinear_texel and draw_scanline_tex do it, at the pixel's own 1/z, u/z, v/z.
//
// Step 3, the ways of drawing (gpu3d_quality):
//  - exact: the rasterizer's own scan conversion on the GPU (a compute pass, a thread a polygon's
//    row: poly.h's spans, each pixel's owner the first polygon in the list to fill it, kept with an
//    atomic minimum; its 1/z, u/z, v/z stepped along the row as the CPU adds them up, every 16th
//    pixel's kept), then a pass a pixel: its owner's colour, its parameters stepped on from the
//    nearest kept ones (the same sums). The CPU's picture, pixel for pixel.
//  - fast: drawn as triangles by the GPU's own rasterizer, the parameters interpolated across the
//    triangle (a texel or a mip level the other way on ~2% of the pixels, edges a pixel either way).
// And into the game (live): the picture goes into a texture of the host's renderer (a shared
// surface: no copy back), averaged down for antialiasing 2x and 3x (each block's coverage its
// alpha, put over the 2D premultiplied), or with Low's edges smoothed as model2a::edge_smooth does.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

struct gpu3d_poly
{
	float x[8], y[8];      // the vertices, projected (arcade pixels; a convex polygon)
	uint8_t n = 0;         // 3-8
	uint8_t renderer = 0;  // MAME's: bit 1 textured, bit 0 translucent (1, solid translucent: draws nothing)
	uint8_t checker = 0;   // every other pixel only: (x ^ y) odd, in render-scale pixels
	int16_t vp[4] = {};    // its window, arcade pixels, inclusive: min x, max x, min y, max y
	float z[8], u[8], v[8];  // textured: each vertex's 1/z, u/z, v/z (drawn_poly's pz, pu, pv)
	uint32_t colour = 0;     // solid: its colour (0xffRRGGBB)
	uint16_t pal = 0;        // textured: its palette colour (5 bits each: r, g, b), lumabase, luma, texlod
	uint16_t lumabase = 0;
	uint16_t luma = 0;   // (above 255 only for a random fighter's dark hair: model2_video::hair_colour)
	int32_t texlod = 0;
	uint8_t sheet = 0;       // 1: texture RAM 1 is its sheet (0 its mipmaps' odd levels and microtexture)
	uint16_t texw = 0, texh = 0, texx = 0, texy = 0;
	uint8_t wrapx = 0, wrapy = 0, mirrorx = 0, mirrory = 0, utex = 0, utexminlod = 0;
	uint16_t utexx = 0, utexy = 0;
};

// The RAMs the textured polygons are coloured from, and a count of each one's changes (a backend
// uploads one again only when its count has moved; gamma, 256 bytes, every time)
struct gpu3d_rams
{
	const uint32_t *tex[2] = {};   // texture RAM 0 and 1, 0x80000 words each
	const uint8_t *luma = nullptr; // 0x8000
	const uint16_t *xlat = nullptr;// colorxlat, 0x6000
	const uint8_t *gamma = nullptr;// 256
	uint32_t gen[5] = {};          // tex0, tex1, luma, xlat, (gamma: unused)
};

// A polygon's flat colour (step 1's, for pictures of the coverage)
inline uint32_t gpu3d_flat_colour(uint32_t index)
{
	uint32_t h = index * 0x9e3779b1u;
	h ^= h >> 15; h *= 0x85ebca6bu; h ^= h >> 13;
	return 0xff000000u | (h & 0x00ffffffu) | 0x00202020u;
}

enum class gpu3d_quality { exact, fast };

// A picture for the game (gpu3d::submit): the 3D drawn at w * ss x h * ss (render scale
// scale * ss), into a target w x h. ss > 1: each ss x ss block averaged into a pixel, its alpha the
// share drawn (premultiplied). edge: Low's antialiasing (model2a::edge_smooth) on the picture at
// its scale, a pixel beside the 3D taking the 2D beneath (the board's picture_parts: the backdrop,
// padded by pad, and the layers over it; at the arcade's resolution, w2d wide), opaque once blended.
struct gpu3d_frame
{
	int w = 0, h = 0, scale = 1, ss = 1;
	bool edge = false;
	const uint32_t *backdrop = nullptr, *mid = nullptr;
	int w2d = 0, pad = 0, bdx = 0, bdy = 0;
	bool filter = false;
	uint64_t version2d = 0;   // the 2D's (uploaded again when it moves)
};

class gpu3d
{
public:
	virtual ~gpu3d() = default;
	virtual const char *name() const = 0;
	virtual void set_rams(const gpu3d_rams &rams) = 0;
	virtual void set_quality(gpu3d_quality q) = 0;
	virtual gpu3d_quality quality() const = 0;
	// The list drawn into a w x h picture (render-scale pixels: the polygons' and windows' arcade
	// pixels times `scale`) and read back (tools). owner: each pixel's polygon (its place in the
	// list; 0xffffffff none), rows ostride apart; rgba: the colours (0 none), rows rstride apart;
	// either can be null. gpu_ms: the GPU's time for the drawing. False: not drawn.
	virtual bool draw(const gpu3d_poly *polys, size_t count, int w, int h, int scale,
		uint32_t *owner, size_t ostride, uint32_t *rgba, size_t rstride, double *gpu_ms = nullptr) = 0;

	// ---- the game's (live): targets the host's renderer shows, drawn without a copy back ----
	// `count` targets w x h (BGRA, premultiplied alpha), each a handle for the host's renderer
	// (Metal: a CVPixelBufferRef for SDL_PROP_TEXTURE_CREATE_METAL_PIXELBUFFER_POINTER). False: none.
	virtual bool make_targets(int w, int h, int count) = 0;
	virtual void *target_handle(int i) const = 0;
	// the targets' bytes R, G, B, A (SDL_PIXELFORMAT_ABGR8888), else B, G, R, A (ARGB8888)
	virtual bool targets_rgba() const { return false; }
	// The list drawn into target `target` (-1: nowhere, a test of its time) after the last submit
	// has finished (one at a time). The
	// RAMs are read (copied) here: the caller may change them once it returns. False: not drawn
	// (an error: the caller goes back to the CPU).
	virtual bool submit(const gpu3d_poly *polys, size_t count, const gpu3d_frame &f, int target) = 0;
	// The last submit finished: its GPU time and the CPU's time setting it up (ms). False: it failed.
	virtual bool finish(double *gpu_ms = nullptr, double *cpu_ms = nullptr) = 0;
	virtual bool busy() const = 0;   // a submit not finished yet
	// The last submit's picture about to be shown by the host's renderer: where that renderer draws on
	// the same device and queue (SDL's gpu renderer), its own commands come after the picture's, so the
	// CPU needn't wait for it. Finished already: its times as finish()'s, true; not yet: *gpu_ms -1,
	// true (it's still checked: the next submit finishes it). False: it failed. Else finish()'s.
	virtual bool settle(double *gpu_ms = nullptr, double *cpu_ms = nullptr) { return finish(gpu_ms, cpu_ms); }
	// The target's pixels read back (tests: checking against the CPU's), rows `stride` apart
	virtual bool read_target(int target, uint32_t *out, size_t stride) = 0;
	// The last submit's picture before it went into a target (w * ss x h * ss, 0xAARRGGBB, 0 none),
	// rows `stride` apart: checking it against the CPU's. False: not here.
	virtual bool read_picture(uint32_t *out, size_t stride) { (void)out; (void)stride; return false; }
	// Tests: each picture drawn `n` times over (a slow GPU)
	int test_repeat = 1;
};

// Metal (gpu3d_metal.mm, Apple only): on `mtl_device` (an id<MTLDevice>: the one the host's SDL
// renderer draws with, from its Metal layer), or the system's default when null. Null if none.
std::unique_ptr<gpu3d> gpu3d_create_metal(void *mtl_device = nullptr);
// The same, on the device of a CAMetalLayer (SDL_GetRenderMetalLayer's: the host's renderer's)
std::unique_ptr<gpu3d> gpu3d_create_metal_layer(void *ca_metal_layer);
// SDL_GPU (gpu3d_sdlgpu.cpp: Direct3D 12 or Vulkan, Windows and Linux): on `dev` (the device of
// SDL's "gpu" renderer, SDL_GetGPURendererDevice: the targets are its textures), or a device of its
// own when null (tools: no window). The x86-64 CPU renderer's pictures. Null if it can't.
struct SDL_GPUDevice;
std::unique_ptr<gpu3d> gpu3d_create_sdlgpu(SDL_GPUDevice *dev = nullptr);
// (its renderer made on another thread from now, for gpu3d_create_metal_layer to take: the start's
// head start, while the game boots)
void gpu3d_metal_prewarm();
