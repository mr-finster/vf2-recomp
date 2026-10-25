// The 3D on the GPU through SDL_GPU (src/gpu/gpu3d.h): Direct3D 12 on Windows, Vulkan on Linux and
// the Steam Deck (and Windows without Direct3D 12), on the device of SDL's "gpu" renderer, so the
// pictures go into textures it shows (nothing copied back) and its queue orders our work and its own.
// The exact way only, gpu3d_metal.mm's, its shaders src/gpu/gpu3d.hlsl (precompiled into
// gpu3d_shaders.h by tools/gpu/shaders.sh) with an x86-64 build's sums: the picture of the
// Windows and Linux CPU renderer, pixel for pixel. A compute pass a polygon's row (poly.h's spans,
// each pixel's owner the first polygon in the list to fill it, kept with an atomic minimum), then
// a pass a pixel (its owner's colour), then into the game's target (averaged down for
// antialiasing, or Low's edges smoothed).
#include "gpu3d.h"
#include "gpu3d_shaders.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

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
static_assert(sizeof(pinfo) == 26 * 4, "pinfo: as gpu3d.hlsl's");
struct pverts { float x[8], y[8], p[24]; };
struct rowrec { int32_t sx, stop; float d0, d1, d2; };
constexpr int CK = 16;
// fast_log2's table (model2_video.cpp's, from MAME's voodoo_render.cpp), handed to the kernels in their uniforms
constexpr uint8_t LOG2_TABLE[128] = {
	  0,   2,   5,   8,  11,  14,  16,  19,  22,  25,  27,  30,  33,  35,  38,  40,
	 43,  46,  48,  51,  53,  56,  58,  61,  63,  65,  68,  70,  73,  75,  77,  80,
	 82,  84,  87,  89,  91,  93,  96,  98, 100, 102, 104, 106, 109, 111, 113, 115,
	117, 119, 121, 123, 125, 127, 129, 132, 134, 136, 138, 140, 141, 143, 145, 147,
	149, 151, 153, 155, 157, 159, 161, 162, 164, 166, 168, 170, 172, 173, 175, 177,
	179, 181, 182, 184, 186, 188, 189, 191, 193, 194, 196, 198, 200, 201, 203, 205,
	206, 208, 209, 211, 213, 214, 216, 218, 219, 221, 222, 224, 225, 227, 229, 230,
	232, 233, 235, 236, 238, 239, 241, 242, 244, 245, 247, 248, 250, 251, 253, 254 };
struct kernel_k { uint32_t a, b, c, d; uint8_t log2[128]; };
inline kernel_k with_log2(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { kernel_k k = { a, b, c, d, {} }; memcpy(k.log2, LOG2_TABLE, 128); return k; }
constexpr uint32_t LUMA_AT = 0, XLAT_AT = 0x8000, GAMMA_AT = 0x8000 + 0xc000, LUTS_BYTES = GAMMA_AT + 256;   // (bytes: gpu3d.hlsl's words x 4)

// poly.h's round_coordinate
inline int round_coordinate(float v)
{
	if (!(v < 2147483520.0f)) return INT32_MAX;
	const float ip = std::floor(v);
	if (ip < -2147483648.0f) return INT32_MIN;
	return int(ip) + ((v - ip > 0.5f) ? 1 : 0);
}

using clk = std::chrono::steady_clock;
inline double ms_since(clk::time_point t) { return std::chrono::duration<double, std::milli>(clk::now() - t).count(); }

class gpu3d_sdlgpu final : public gpu3d
{
public:
	gpu3d_sdlgpu(SDL_GPUDevice *dev, bool own) : m_dev(dev), m_own(own) {}
	~gpu3d_sdlgpu() override
	{
		finish();
		for (auto &t : m_tg) SDL_ReleaseGPUTexture(m_dev, t);
		for (SDL_GPUComputePipeline *p : { m_clear, m_spans, m_shade, m_out, m_edge }) if (p) SDL_ReleaseGPUComputePipeline(m_dev, p);
		for (SDL_GPUBuffer *b : { m_ram[0], m_ram[1], m_ram[2], m_ibuf, m_pbuf, m_jpbuf, m_jbbuf, m_rows, m_ck, m_owner, m_cbuf, m_bd, m_mid })
			if (b) SDL_ReleaseGPUBuffer(m_dev, b);
		if (m_up) SDL_ReleaseGPUTransferBuffer(m_dev, m_up);
		if (m_down) SDL_ReleaseGPUTransferBuffer(m_dev, m_down);
		if (m_own) SDL_DestroyGPUDevice(m_dev);
	}
	bool init()
	{
		const auto t0 = clk::now();
		const SDL_GPUShaderFormat fm = SDL_GetGPUShaderFormats(m_dev);
		const bool dxil = (fm & SDL_GPU_SHADERFORMAT_DXIL) != 0, spirv = (fm & SDL_GPU_SHADERFORMAT_SPIRV) != 0;
		if (!dxil && !spirv) { fprintf(stderr, "gpu3d sdl_gpu: %s takes neither SPIR-V nor DXIL\n", SDL_GetGPUDeviceDriver(m_dev)); return false; }
		auto make = [&](const uint8_t *spv, size_t spv_n, const uint8_t *dx, size_t dx_n, const char *name,
			uint32_t ro, uint32_t rwt, uint32_t rwb, uint32_t tx, uint32_t ty) -> SDL_GPUComputePipeline * {
			SDL_GPUComputePipelineCreateInfo ci = {};
			ci.code = spirv ? spv : dx;
			ci.code_size = spirv ? spv_n : dx_n;
			ci.format = spirv ? SDL_GPU_SHADERFORMAT_SPIRV : SDL_GPU_SHADERFORMAT_DXIL;
			ci.entrypoint = "main";
			ci.num_readonly_storage_buffers = ro;
			ci.num_readwrite_storage_textures = rwt;
			ci.num_readwrite_storage_buffers = rwb;
			ci.num_uniform_buffers = 1;
			ci.threadcount_x = tx; ci.threadcount_y = ty; ci.threadcount_z = 1;
			SDL_GPUComputePipeline *p = SDL_CreateGPUComputePipeline(m_dev, &ci);
			if (!p) fprintf(stderr, "gpu3d sdl_gpu: %s: %s\n", name, SDL_GetError());
			return p;
		};
#define K(n) gpu3d_##n##_spv, sizeof gpu3d_##n##_spv, gpu3d_##n##_dxil, sizeof gpu3d_##n##_dxil, #n
		m_clear = make(K(clear), 0, 0, 1, 64, 1);
		m_spans = make(K(spans), 6, 0, 3, 64, 1);
		m_shade = make(K(shade), 7, 0, 1, 8, 8);
		m_out = make(K(out), 1, 1, 0, 8, 8);
		m_edge = make(K(edge), 3, 1, 0, 8, 8);
#undef K
		if (!m_clear || !m_spans || !m_shade || !m_out || !m_edge) return false;
		if (!SDL_GPUTextureSupportsFormat(m_dev, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTURETYPE_2D,
				SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE)) {
			fprintf(stderr, "gpu3d sdl_gpu: no RGBA8 storage textures\n");
			return false;
		}
		// texture RAM 0 and 1; the luma RAM, colorxlat and gamma in one (gpu3d.hlsl's luts)
		if (!(m_ram[0] = buffer(0x80000 * 4, false)) || !(m_ram[1] = buffer(0x80000 * 4, false)) || !(m_ram[2] = buffer(LUTS_BYTES, false))) return false;
		snprintf(m_name, sizeof m_name, "%s (%s)", SDL_GetGPUDeviceDriver(m_dev), spirv ? "SPIR-V" : "DXIL");
		fprintf(stderr, "gpu3d sdl_gpu: %s, its pipelines ready in %.0f ms\n", m_name, ms_since(t0));
		return true;
	}
	const char *name() const override { return m_name; }
	void set_rams(const gpu3d_rams &r) override { m_rams = r; }
	void set_quality(gpu3d_quality) override {}   // (the exact way only)
	gpu3d_quality quality() const override { return gpu3d_quality::exact; }

	bool draw(const gpu3d_poly *polys, size_t count, int w, int h, int scale,
		uint32_t *owner, size_t ostride, uint32_t *rgba, size_t rstride, double *gpu_ms) override
	{
		if (w <= 0 || h <= 0 || !finish()) return false;
		const auto t0 = clk::now();
		SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(m_dev);
		if (!cb) return false;
		if (!encode_picture(cb, polys, count, w, h, scale)) { SDL_CancelGPUCommandBuffer(cb); return false; }
		const uint32_t bytes = uint32_t(w) * h * 4;
		if (!transfer(m_down, m_down_size, bytes * 2, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD)) { SDL_CancelGPUCommandBuffer(cb); return false; }
		SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
		SDL_GPUBufferRegion r = { m_owner, 0, bytes };
		SDL_GPUTransferBufferLocation l = { m_down, 0 };
		SDL_DownloadFromGPUBuffer(cp, &r, &l);
		r.buffer = m_cbuf; l.offset = bytes;
		SDL_DownloadFromGPUBuffer(cp, &r, &l);
		SDL_EndGPUCopyPass(cp);
		if (!submit_cb(cb, t0) || !finish(gpu_ms)) return false;
		const uint8_t *p = (const uint8_t *)SDL_MapGPUTransferBuffer(m_dev, m_down, false);
		if (!p) return false;
		for (int y = 0; y < h; y++) {
			if (owner) memcpy(owner + size_t(y) * ostride, p + size_t(y) * w * 4, size_t(w) * 4);
			if (rgba) memcpy(rgba + size_t(y) * rstride, p + bytes + size_t(y) * w * 4, size_t(w) * 4);
		}
		SDL_UnmapGPUTransferBuffer(m_dev, m_down);
		return true;
	}

	bool make_targets(int w, int h, int count) override
	{
		finish();
		for (auto &t : m_tg) SDL_ReleaseGPUTexture(m_dev, t);
		m_tg.clear();
		for (int i = 0; i < count; i++) {
			SDL_GPUTextureCreateInfo ti = {};
			ti.type = SDL_GPU_TEXTURETYPE_2D;
			ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
			ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
			ti.width = uint32_t(w); ti.height = uint32_t(h);
			ti.layer_count_or_depth = 1; ti.num_levels = 1;
			ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
			SDL_GPUTexture *t = SDL_CreateGPUTexture(m_dev, &ti);
			if (!t) { fprintf(stderr, "gpu3d sdl_gpu: a target: %s\n", SDL_GetError()); return false; }
			m_tg.push_back(t);
		}
		m_tw = w; m_th = h;
		return true;
	}
	void *target_handle(int i) const override { return i >= 0 && i < int(m_tg.size()) ? m_tg[i] : nullptr; }
	bool targets_rgba() const override { return true; }

	bool submit(const gpu3d_poly *polys, size_t count, const gpu3d_frame &f, int target) override
	{
		// (2026-10-15, vf2-recomp #3 again: with the show no longer waiting, the wait moved here, ~7 ms
		// "draw" a picture at 6x on an RTX 5080: the last picture's fence comes after the renderer's
		// present, so waiting for it took in the screen's refresh. A picture still in flight is left
		// alone; only a third waits, for the oldest. Targets, buffers and the upload (cycled) are the
		// GPU's to order: its queue runs them in turn.)
		if (target < 0) { if (!finish()) return false; }   // (a test of its time: alone on the GPU)
		else if (!reap(1)) return false;
		if (target < -1 || target >= int(m_tg.size()) || (target >= 0 && (f.w != m_tw || f.h != m_th)) || f.ss < 1) return false;
		const auto t0 = clk::now();
		const int W = f.w * f.ss, H = f.h * f.ss, s = f.scale * f.ss;
		SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(m_dev);
		if (!cb) return false;
		const bool edge = target >= 0 && f.edge && f.ss == 1 && f.backdrop && f.mid;
		if (edge) {   // (the 2D beneath, uploaded with the picture's data when it has moved)
			const int P = f.pad, pw = f.w2d + 2 * P;
			m_bdb = uint32_t(pw) * (384 + 2 * P) * 4; m_midb = uint32_t(f.w2d) * 384 * 4;
			if (!grow(m_bd, m_bd_size, m_bdb) || !grow(m_mid, m_mid_size, m_midb)) { SDL_CancelGPUCommandBuffer(cb); return false; }
			m_put2d = f.version2d != m_v2d || f.w2d != m_w2d || m_bd_fresh;
			m_frame = &f;
		} else m_put2d = false;
		if (!encode_picture(cb, polys, count, W, H, s)) { SDL_CancelGPUCommandBuffer(cb); return false; }
		if (target >= 0) {
			SDL_GPUStorageTextureReadWriteBinding tb = {};
			tb.texture = m_tg[target];
			SDL_GPUComputePass *cp = SDL_BeginGPUComputePass(cb, &tb, 1, nullptr, 0);
			if (edge) {
				m_v2d = f.version2d; m_w2d = f.w2d; m_bd_fresh = false;
				struct { uint32_t W, H, s, w, pad; int32_t bdx, bdy; uint32_t filter; } k = { uint32_t(W), uint32_t(H), uint32_t(s), uint32_t(f.w2d), uint32_t(f.pad), f.bdx, f.bdy, f.filter ? 1u : 0u };
				SDL_BindGPUComputePipeline(cp, m_edge);
				SDL_GPUBuffer *ro[3] = { m_cbuf, m_bd, m_mid };
				SDL_BindGPUComputeStorageBuffers(cp, 0, ro, 3);
				SDL_PushGPUComputeUniformData(cb, 0, &k, sizeof k);
				SDL_DispatchGPUCompute(cp, (W + 7) / 8, (H + 7) / 8, 1);
			} else {
				const uint32_t k[4] = { uint32_t(f.w), uint32_t(f.h), uint32_t(f.ss), 0 };
				SDL_BindGPUComputePipeline(cp, m_out);
				SDL_BindGPUComputeStorageBuffers(cp, 0, &m_cbuf, 1);
				SDL_PushGPUComputeUniformData(cb, 0, k, sizeof k);
				SDL_DispatchGPUCompute(cp, (f.w + 7) / 8, (f.h + 7) / 8, 1);
			}
			SDL_EndGPUComputePass(cp);
		}
		return submit_cb(cb, t0);
	}

	// the oldest pictures in flight finished, until at most `keep` are left (waiting where needed)
	bool reap(size_t keep, bool wait = true)
	{
		while (m_flight.size() > keep || (!m_flight.empty() && SDL_QueryGPUFence(m_dev, m_flight.front().f))) {
			const bool ok = SDL_WaitForGPUFences(m_dev, true, &m_flight.front().f, 1);
			m_last_gpu_ms = ms_since(m_flight.front().t);
			SDL_ReleaseGPUFence(m_dev, m_flight.front().f);
			m_flight.erase(m_flight.begin());
			if (!ok) { fprintf(stderr, "gpu3d sdl_gpu: a picture failed (%s)\n", SDL_GetError()); m_last_ok = false; }
			if (!wait && m_flight.size() <= keep) break;
		}
		return m_last_ok;
	}
	bool finish(double *gpu_ms = nullptr, double *cpu_ms = nullptr) override
	{
		reap(0);
		if (gpu_ms) *gpu_ms = m_last_gpu_ms;
		if (cpu_ms) *cpu_ms = m_cpu_ms;
		return m_last_ok;
	}
	bool busy() const override { return !m_flight.empty(); }
	// (2026-10-14, vf2-recomp #3: an RTX 5080 at 6x on Direct3D 12 waited ~7.7 ms a picture here, for its
	// fence: the picture went in after the renderer's previous present, so the wait took in the screen's
	// refresh too, and 120 pictures a second couldn't be met. The renderer's commands come after the
	// picture's on this device's queue, so nothing needs the CPU to wait: the next submit finishes it.)
	bool settle(double *gpu_ms = nullptr, double *cpu_ms = nullptr) override
	{
		reap(1, false);
		if (!m_flight.empty() && !SDL_QueryGPUFence(m_dev, m_flight.back().f)) {
			if (gpu_ms) *gpu_ms = -1;
			if (cpu_ms) *cpu_ms = m_cpu_ms;
			return m_last_ok;
		}
		return finish(gpu_ms, cpu_ms);
	}

	bool read_target(int target, uint32_t *out, size_t stride) override
	{
		if (!finish() || target < 0 || target >= int(m_tg.size())) return false;
		const uint32_t bytes = uint32_t(m_tw) * m_th * 4;
		if (!transfer(m_down, m_down_size, bytes, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD)) return false;
		SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(m_dev);
		if (!cb) return false;
		SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
		SDL_GPUTextureRegion r = {};
		r.texture = m_tg[target]; r.w = uint32_t(m_tw); r.h = uint32_t(m_th); r.d = 1;
		SDL_GPUTextureTransferInfo ti = {};
		ti.transfer_buffer = m_down; ti.pixels_per_row = uint32_t(m_tw); ti.rows_per_layer = uint32_t(m_th);
		SDL_DownloadFromGPUTexture(cp, &r, &ti);
		SDL_EndGPUCopyPass(cp);
		SDL_GPUFence *fe = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
		if (!fe) return false;
		const bool ok = SDL_WaitForGPUFences(m_dev, true, &fe, 1);
		SDL_ReleaseGPUFence(m_dev, fe);
		if (!ok) return false;
		const uint32_t *p = (const uint32_t *)SDL_MapGPUTransferBuffer(m_dev, m_down, false);
		if (!p) return false;
		// RGBA bytes to 0xAARRGGBB words
		for (int y = 0; y < m_th; y++)
			for (int x = 0; x < m_tw; x++) {
				const uint32_t c = p[size_t(y) * m_tw + x];
				out[size_t(y) * stride + x] = (c & 0xff00ff00u) | ((c >> 16) & 0xffu) | ((c & 0xffu) << 16);
			}
		SDL_UnmapGPUTransferBuffer(m_dev, m_down);
		return true;
	}

	bool read_picture(uint32_t *out, size_t stride) override
	{
		if (!finish() || !m_cbuf || m_pw <= 0) return false;
		const uint32_t bytes = uint32_t(m_pw) * m_ph * 4;
		if (!transfer(m_down, m_down_size, bytes, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD)) return false;
		SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(m_dev);
		if (!cb) return false;
		SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
		SDL_GPUBufferRegion r = { m_cbuf, 0, bytes };
		SDL_GPUTransferBufferLocation l = { m_down, 0 };
		SDL_DownloadFromGPUBuffer(cp, &r, &l);
		SDL_EndGPUCopyPass(cp);
		SDL_GPUFence *fe = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
		if (!fe) return false;
		const bool ok = SDL_WaitForGPUFences(m_dev, true, &fe, 1);
		SDL_ReleaseGPUFence(m_dev, fe);
		const uint8_t *p = ok ? (const uint8_t *)SDL_MapGPUTransferBuffer(m_dev, m_down, false) : nullptr;
		if (!p) return false;
		for (int y = 0; y < m_ph; y++) memcpy(out + size_t(y) * stride, p + size_t(y) * m_pw * 4, size_t(m_pw) * 4);
		SDL_UnmapGPUTransferBuffer(m_dev, m_down);
		return true;
	}

private:
	SDL_GPUBuffer *buffer(uint32_t size, bool rw)
	{
		SDL_GPUBufferCreateInfo bi = {};
		bi.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | (rw ? SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE : 0);
		bi.size = std::max<uint32_t>(size, 16);
		SDL_GPUBuffer *b = SDL_CreateGPUBuffer(m_dev, &bi);
		if (!b) fprintf(stderr, "gpu3d sdl_gpu: a buffer of %u bytes: %s\n", size, SDL_GetError());
		return b;
	}
	// b at least n bytes (made again larger, with room to grow; the GPU idle: finish() came first)
	bool grow(SDL_GPUBuffer *&b, uint32_t &have, uint32_t n, bool rw = true)
	{
		n = std::max<uint32_t>(n, 16);
		if (b && have >= n) return true;
		if (b) { reap(0); SDL_ReleaseGPUBuffer(m_dev, b); }   // (a picture in flight may use it: finished first)
		have = n + n / 2;
		b = buffer(have, rw);
		if (&b == &m_bd || &b == &m_mid) m_bd_fresh = true;
		return b != nullptr;
	}
	bool transfer(SDL_GPUTransferBuffer *&t, uint32_t &have, uint32_t n, SDL_GPUTransferBufferUsage usage)
	{
		n = std::max<uint32_t>(n, 16);
		if (t && have >= n) return true;
		if (t) { reap(0); SDL_ReleaseGPUTransferBuffer(m_dev, t); }
		SDL_GPUTransferBufferCreateInfo ti = {};
		ti.usage = usage;
		ti.size = have = n + n / 2;
		t = SDL_CreateGPUTransferBuffer(m_dev, &ti);
		if (!t) fprintf(stderr, "gpu3d sdl_gpu: a transfer buffer of %u bytes: %s\n", n, SDL_GetError());
		return t != nullptr;
	}
	bool submit_cb(SDL_GPUCommandBuffer *cb, clk::time_point t0)
	{
		m_cpu_ms = ms_since(t0);
		SDL_GPUFence *fe = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
		if (!fe) { fprintf(stderr, "gpu3d sdl_gpu: submit: %s\n", SDL_GetError()); m_last_ok = false; return false; }
		m_flight.push_back({ fe, clk::now() });
		return true;
	}

	// The picture w x h at `scale` into m_cbuf (0xAARRGGBB, 0 none), the owners in m_owner
	bool encode_picture(SDL_GPUCommandBuffer *cb, const gpu3d_poly *polys, size_t count, int w, int h, int scale)
	{
		const uint32_t npx = uint32_t(w) * uint32_t(h);
		m_pw = w; m_ph = h;
		if (!grow(m_owner, m_owner_size, npx * 4) || !grow(m_cbuf, m_cbuf_size, npx * 4)) return false;
		m_info.assign(std::max<size_t>(count, 1), pinfo{});
		m_pv.resize(std::max<size_t>(count, 1));
		const float s = float(scale);
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
				pv.p[k * 3 + 0] = tex ? p.z[k] : 0; pv.p[k * 3 + 1] = tex ? p.u[k] : 0; pv.p[k * 3 + 2] = tex ? p.v[k] : 0;
				minx = std::min(minx, pv.x[k]); maxx = std::max(maxx, pv.x[k]);
				miny = std::min(miny, pv.y[k]); maxy = std::max(maxy, pv.y[k]);
			}
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
		}
		// the upload: the polygons, their vertices, the jobs, the RAMs that have changed (gamma each
		// time) and the 2D beneath (Low), one transfer buffer, cycled
		const uint32_t ib = uint32_t(m_info.size() * sizeof(pinfo)), pb = uint32_t(m_pv.size() * sizeof(pverts));
		const uint32_t jb = uint32_t(std::max<size_t>(m_jobpoly.size(), 1) * 4);
		if (!grow(m_ibuf, m_ibuf_size, ib, false) || !grow(m_pbuf, m_pbuf_size, pb, false) || !grow(m_jpbuf, m_jp_size, jb, false) || !grow(m_jbbuf, m_jb_size, jb, false)
			|| !grow(m_rows, m_rows_size, std::max<uint32_t>(rows, 1) * uint32_t(sizeof(rowrec))) || !grow(m_ck, m_ck_size, std::max<uint32_t>(cks, 1) * 12))
			return false;
		static const uint32_t ram_sizes[5] = { 0x80000 * 4, 0x80000 * 4, 0x8000, 0x6000 * 2, 256 };
		static const uint32_t ram_at[5] = { 0, 0, LUMA_AT, XLAT_AT, GAMMA_AT };   // (their places in their buffers)
		const void *ram_src[5] = { m_rams.tex[0], m_rams.tex[1], m_rams.luma, m_rams.xlat, m_rams.gamma };
		bool ram_put[5];
		uint32_t total = ib + pb + jb * 2;
		for (int k = 0; k < 5; k++) {
			ram_put[k] = ram_src[k] && !(k < 4 && m_uploaded[k] && m_gen[k] == m_rams.gen[k]);
			if (ram_put[k]) total += ram_sizes[k];
		}
		if (m_put2d) total += m_bdb + m_midb;
		if (!transfer(m_up, m_up_size, total, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD)) return false;
		uint8_t *map = (uint8_t *)SDL_MapGPUTransferBuffer(m_dev, m_up, true);
		if (!map) return false;
		struct piece { SDL_GPUBuffer *b; uint32_t off, n, at; };
		piece pieces[12];
		int np = 0;
		uint32_t at = 0;
		auto add = [&](SDL_GPUBuffer *b, const void *src, uint32_t n, uint32_t to = 0) {
			if (src) memcpy(map + at, src, n); else memset(map + at, 0, n);
			pieces[np++] = { b, at, n, to };
			at += n;
		};
		add(m_ibuf, m_info.data(), ib);
		add(m_pbuf, m_pv.data(), pb);
		add(m_jpbuf, m_jobpoly.empty() ? nullptr : m_jobpoly.data(), jb);
		add(m_jbbuf, m_jobbase.empty() ? nullptr : m_jobbase.data(), jb);
		for (int k = 0; k < 5; k++) if (ram_put[k]) {
			add(m_ram[std::min(k, 2)], ram_src[k], ram_sizes[k], ram_at[k]);
			m_gen[k] = m_rams.gen[k]; m_uploaded[k] = true;
		}
		if (m_put2d) { add(m_bd, m_frame->backdrop, m_bdb); add(m_mid, m_frame->mid, m_midb); }
		SDL_UnmapGPUTransferBuffer(m_dev, m_up);
		SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
		for (int i = 0; i < np; i++) {
			SDL_GPUTransferBufferLocation l = { m_up, pieces[i].off };
			SDL_GPUBufferRegion r = { pieces[i].b, pieces[i].at, pieces[i].n };
			SDL_UploadToGPUBuffer(cp, &l, &r, false);
		}
		SDL_EndGPUCopyPass(cp);

		for (int rep = 0; rep < std::max(1, test_repeat); rep++) {   // (tests: a slow GPU, the picture drawn n times)
			// the owners cleared
			{
				SDL_GPUStorageBufferReadWriteBinding bw = {}; bw.buffer = m_owner;
				SDL_GPUComputePass *c = SDL_BeginGPUComputePass(cb, nullptr, 0, &bw, 1);
				SDL_BindGPUComputePipeline(c, m_clear);
				const uint32_t k[4] = { npx, 0xffffffffu, 0, 0 };
				SDL_PushGPUComputeUniformData(cb, 0, k, sizeof k);
				dispatch1(c, npx);
				SDL_EndGPUComputePass(c);
			}
			// the spans: a thread a polygon's row
			if (rows) {
				SDL_GPUStorageBufferReadWriteBinding bw[3] = {};
				bw[0].buffer = m_rows; bw[1].buffer = m_ck; bw[2].buffer = m_owner;
				SDL_GPUComputePass *c = SDL_BeginGPUComputePass(cb, nullptr, 0, bw, 3);
				SDL_BindGPUComputePipeline(c, m_spans);
				SDL_GPUBuffer *ro[6] = { m_ibuf, m_pbuf, m_jpbuf, m_jbbuf, m_ram[0], m_ram[1] };
				SDL_BindGPUComputeStorageBuffers(c, 0, ro, 6);
				const kernel_k k = with_log2(uint32_t(m_jobpoly.size()), uint32_t(w), uint32_t(h), rows);
				SDL_PushGPUComputeUniformData(cb, 0, &k, sizeof k);
				dispatch1(c, rows);
				SDL_EndGPUComputePass(c);
			}
			// the colours: a thread a pixel
			{
				SDL_GPUStorageBufferReadWriteBinding bw = {}; bw.buffer = m_cbuf;
				SDL_GPUComputePass *c = SDL_BeginGPUComputePass(cb, nullptr, 0, &bw, 1);
				SDL_BindGPUComputePipeline(c, m_shade);
				SDL_GPUBuffer *ro[7] = { m_ibuf, m_rows, m_ck, m_owner, m_ram[0], m_ram[1], m_ram[2] };
				SDL_BindGPUComputeStorageBuffers(c, 0, ro, 7);
				const kernel_k k = with_log2(uint32_t(w), uint32_t(h), 0, 0);
				SDL_PushGPUComputeUniformData(cb, 0, &k, sizeof k);
				SDL_DispatchGPUCompute(c, (w + 7) / 8, (h + 7) / 8, 1);
				SDL_EndGPUComputePass(c);
			}
		}
		return true;
	}
	// n threads of a 64-wide kernel (groups past 65535 in rows of 65535: the kernel's index)
	static void dispatch1(SDL_GPUComputePass *c, uint32_t n)
	{
		const uint32_t groups = (n + 63) / 64;
		const uint32_t gx = std::min<uint32_t>(groups, 65535), gy = (groups + 65534) / 65535;
		SDL_DispatchGPUCompute(c, gx, gy, 1);
	}

	SDL_GPUDevice *m_dev;
	bool m_own;
	char m_name[64] = "sdl_gpu";
	SDL_GPUComputePipeline *m_clear = nullptr, *m_spans = nullptr, *m_shade = nullptr, *m_out = nullptr, *m_edge = nullptr;
	SDL_GPUBuffer *m_ram[3] = {};
	SDL_GPUBuffer *m_ibuf = nullptr, *m_pbuf = nullptr, *m_jpbuf = nullptr, *m_jbbuf = nullptr, *m_rows = nullptr, *m_ck = nullptr, *m_owner = nullptr, *m_cbuf = nullptr;
	uint32_t m_ibuf_size = 0, m_pbuf_size = 0, m_jp_size = 0, m_jb_size = 0, m_rows_size = 0, m_ck_size = 0, m_owner_size = 0, m_cbuf_size = 0;
	SDL_GPUBuffer *m_bd = nullptr, *m_mid = nullptr;
	uint32_t m_bd_size = 0, m_mid_size = 0, m_bdb = 0, m_midb = 0;
	bool m_bd_fresh = true, m_put2d = false;
	const gpu3d_frame *m_frame = nullptr;
	uint64_t m_v2d = ~0ull;
	int m_w2d = 0;
	SDL_GPUTransferBuffer *m_up = nullptr, *m_down = nullptr;
	uint32_t m_up_size = 0, m_down_size = 0;
	gpu3d_rams m_rams;
	uint32_t m_gen[5] = {};
	bool m_uploaded[5] = {};
	std::vector<pinfo> m_info;
	std::vector<pverts> m_pv;
	std::vector<uint32_t> m_jobpoly, m_jobbase;
	std::vector<SDL_GPUTexture *> m_tg;
	int m_tw = 0, m_th = 0, m_pw = 0, m_ph = 0;
	struct flight { SDL_GPUFence *f; clk::time_point t; };
	std::vector<flight> m_flight;   // (pictures submitted, not yet seen finished: the oldest first)
	double m_cpu_ms = 0, m_last_gpu_ms = 0;
	bool m_last_ok = true;
};

}

std::unique_ptr<gpu3d> gpu3d_create_sdlgpu(SDL_GPUDevice *dev)
{
	bool own = false;
	if (!dev) {   // (GPU3D_VALIDATE=1: the driver's validation, Vulkan's layers)
		// A device of our own (the tools, and the day the renderer isn't "gpu"): made with the same
		// optional Vulkan features SDL's own gpu renderer leaves off, so this runs wherever the
		// renderer runs. shaderClipDistance, depthClamp, drawIndirectFirstInstance and
		// samplerAnisotropy are true by default in SDL_GPU but are on only about 65%, 79% of Android
		// devices and less (SDL_gpu_vulkan.c reads the four properties; SDL_render_gpu.c turns all
		// four off, which is SDL #12652's fix - without it SDL_GPU excludes every Mali GPU). We use
		// none of them: the 3D is compute pipelines into storage images, with no rasterizer state
		// and no sampler at all. DEBUGMODE is what SDL_CreateGPUDevice's debug_mode was.
		SDL_PropertiesID props = SDL_CreateProperties();
		SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
		SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
		SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, getenv("GPU3D_VALIDATE") != nullptr);
		SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
		SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, false);
		SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false);
		SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, false);
		dev = SDL_CreateGPUDeviceWithProperties(props);
		SDL_DestroyProperties(props);
		if (!dev) { fprintf(stderr, "gpu3d sdl_gpu: no device: %s\n", SDL_GetError()); return nullptr; }
		own = true;
	}
	auto g = std::make_unique<gpu3d_sdlgpu>(dev, own);
	if (!g->init()) return nullptr;
	return g;
}
