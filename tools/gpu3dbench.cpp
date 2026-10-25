// The GPU renderer (src/gpu/gpu3d.h) against the CPU's, from the made-up bench scene
// (model2_video::bench_make): no ROM, so it runs anywhere, and the same pictures a phone's driver
// can be told apart from the desktop's with. tools/gpu3dcmp.cpp does the same against the game's
// own scene, from a ROM.
//
//   gpu3dbench [POLYS] [SCALES] [AA] [exact|fast]
//   e.g. gpu3dbench 1500 1,2,3,4 1,2,3 exact        (the default without arguments)
//
// The board draws its picture at scale * antialiasing (model2a::set_render_scale), render_size is
// that picture's size, and the game's own check (src/host/main.cpp, g3_verify) compares the GPU's
// picture of it with the CPU's, pixel for pixel: the same comparison and the same thresholds are
// used here, so a line's OK means Auto would keep the GPU on this driver with this scene.
// A picture is drawn at 496*scale*aa x 384*scale*aa: 4x3 with 2x antialiasing is 3968 x 3072.
#include "../src/machine/model2_video.h"
#include "../src/gpu/gpu3d.h"
#ifdef GPU3DBENCH_SDLGPU
#include <SDL3/SDL.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <vector>

static std::vector<int> ints(const char *s)
{
	std::vector<int> v; std::stringstream ss(s); std::string t;
	while (std::getline(ss, t, ',')) if (!t.empty()) v.push_back(atoi(t.c_str()));
	return v;
}

int main(int argc, char **argv)
{
	const int n = argc > 1 ? atoi(argv[1]) : 1500;
	const std::vector<int> scales = argc > 2 ? ints(argv[2]) : std::vector<int>{ 1, 2, 3, 4 };
	const std::vector<int> aas = argc > 3 ? ints(argv[3]) : std::vector<int>{ 1, 2, 3 };
	const bool fast = argc > 4 && !strcmp(argv[4], "fast");

	// The board's memories, as model2a holds them (model2a.cpp:175-181); the ROM regions are dummy:
	// the bench's polygons carry their own texture parameters, so the rasterizer reads these only
	// through the texture-RAM paths this is about.
	std::vector<u32> bufferram(0x10000, 0), tex0(0x80000), tex1(0x80000);
	std::vector<u8> lumaram(0x8000);
	std::vector<u16> palram(0x2000), xlat(0x6000);
	std::vector<u8> texrom(0x200000), polyrom(0x100000);
	u8 gamma[256];
	u32 seed = 0x12345678;
	auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return u8(seed >> 24); };
	for (auto *ram : { &tex0, &tex1 })
		for (auto &w : *ram) w = u32(rnd()) | u32(rnd()) << 8 | u32(rnd()) << 16 | u32(rnd()) << 24;
	for (auto &b : lumaram) b = rnd();
	for (auto &w : palram) w = u16(rnd() | rnd() << 8);
	for (auto &w : xlat) w = u16(rnd() | rnd() << 8);
	for (auto &b : texrom) b = rnd();
	for (auto &b : polyrom) b = rnd();
	for (int i = 0; i < 256; i++)   // model2_state::video_start's bias 64, gain 255/191
		gamma[i] = u8(std::max((double(i) - 64.0) * 255.0 / 191.0, 0.0));

	model2_video v(bufferram.data(), tex0.data(), tex1.data(), lumaram.data(), palram.data(), xlat.data(), gamma,
				   texrom.data(), texrom.size(), polyrom.data(), polyrom.size());
	v.bench_make(n);

#ifdef GPU3DBENCH_SDLGPU
	// SDL_GPU (Vulkan, Direct3D 12): headless, SDL_VIDEODRIVER=offscreen without a display
	if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
	std::unique_ptr<gpu3d> dev = gpu3d_create_sdlgpu();
#else
	std::unique_ptr<gpu3d> dev = gpu3d_create_metal();
#endif
	if (!dev) { fprintf(stderr, "no GPU renderer\n"); return 1; }
	dev->set_quality(fast ? gpu3d_quality::fast : gpu3d_quality::exact);
	fprintf(stderr, "gpu3dbench: %s, %s, %d polygons\n", dev->name(), fast ? "fast" : "exact", n);

	bool all_ok = true;
	for (int aa_in : aas)
		for (int scale : scales) {
			// The antialiasing the scale can carry, as the game works it out (main.cpp: aa_fit):
			// within 8x the arcade's size, model2_video::set_scale caps the render scale at 8.
			const int aa = std::max(1, std::min(aa_in, 8 / std::max(1, scale)));
			v.set_scale(scale * aa);   // the board draws at scale * antialiasing (model2a::set_render_scale)
			int rw = 0, rh = 0;
			v.render_size(rw, rh);
			const int tw = rw / aa, th = rh / aa;   // the target

			std::vector<gpu3d_poly> list;
			gpu3d_rams rams;
			v.bench_export(list);
			v.gpu_rams(rams);
			dev->set_rams(rams);

			gpu3d_frame f;
			f.w = tw; f.h = th; f.scale = scale; f.ss = aa;   // (as the game's check frame, main.cpp:1465)
			std::vector<u32> gp(size_t(rw) * rh, 0);
			if (!dev->submit(list.data(), list.size(), f, -1)) { fprintf(stderr, "scale %d, aa %d: the picture couldn't be handed to the GPU\n", scale, aa); return 1; }
			double gms = 0;
			if (!dev->finish(&gms)) { fprintf(stderr, "scale %d, aa %d: the picture failed on the GPU\n", scale, aa); return 1; }
			if (!dev->read_picture(gp.data(), size_t(rw))) { fprintf(stderr, "scale %d, aa %d: read_picture failed\n", scale, aa); return 1; }

			const double cms = v.bench_cpu(0, 1);
			const bitmap_rgb32 &c = v.bench_picture();
			if (c.width() != rw || c.height() != rh) { fprintf(stderr, "the CPU's picture is %dx%d, the GPU's %dx%d\n", c.width(), c.height(), rw, rh); return 1; }

			// The game's own way of judging (main.cpp:1541-1571): a pixel drawn on one and not the
			// other is a coverage difference; drawn on both with a colour channel off by more than
			// GPU_ROUNDING_COLOUR (8) is a colour difference.
			long long cover = 0, big = 0, both = 0, ident = 0;
			int most = 0;
			for (int y = 0; y < rh; y++)
				for (int x = 0; x < rw; x++) {
					const u32 a = gp[size_t(y) * size_t(rw) + size_t(x)] & 0xffffffu;
					const u32 b = c.pix(y, x) & 0xffffffu;
					if (!a || !b) { cover += (a != 0) != (b != 0); continue; }
					both++;
					const int d = std::max({ abs(int((a >> 16) & 0xff) - int((b >> 16) & 0xff)),
											 abs(int((a >> 8) & 0xff) - int((b >> 8) & 0xff)),
											 abs(int(a & 0xff) - int(b & 0xff)) });
					if (d == 0) ident++;
					if (d > 8) big++;
					most = std::max(most, d);
				}
			const long long px = (long long)rw * rh;
			const long long ok_n = std::max<long long>(64, (long long)(px * 0.01)), ok_cover = std::max<long long>(64, (long long)(px * 0.05));
			const bool ok = big <= ok_n && cover <= ok_cover && both > px / 2;
			all_ok = all_ok && ok;
			printf("scale %d, antialiasing %d, %d x %d: %s%s | CPU %.2f ms, GPU %.2f ms | %lld of %lld pixels differ (%lld by more than 8, colours by %d at most), %lld drawn on one only\n",
				   scale, aa, rw, rh, ok ? "OK" : "FAILED", ok ? "" : " (Auto would keep the CPU's 3D)", cms, gms,
				   both - ident, both, big, most, cover);
		}
	return all_ok ? 0 : 1;
}
