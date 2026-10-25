// vf2soundonly: online play's sound board alone (model2a::set_sound_only), checked against the game's own. One board
// runs the game with its sound (the attract and, with --random, two players' versus), its bytes for the sound board
// taken each frame; a second board, the sound board alone, is given them and plays them. Each second's loudness (RMS)
// of both is printed, and how alike the two are (the correlation of the seconds' loudness, and of the samples within
// 20 ms of each other). --wav-a / --wav-b write them.
//   vf2soundonly <vf2.zip> [--frames N] [--random SEED] [--wav-a FILE] [--wav-b FILE]
#include "../src/machine/model2a.h"
#include "random_players.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

bool write_wav(const std::string &path, const std::vector<int16_t> &v, int rate)
{
	FILE *f = fopen(path.c_str(), "wb");
	if (!f) return false;
	const uint32_t data = uint32_t(v.size() * 2), riff = 36 + data, fmt = 16, byte_rate = uint32_t(rate) * 4;
	const uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
	const uint32_t r = uint32_t(rate);
	fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
	fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&r, 4, 1, f); fwrite(&byte_rate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
	fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(v.data(), 2, v.size(), f);
	return fclose(f) == 0;
}

double rms(const std::vector<int16_t> &v, size_t from, size_t n)
{
	double s = 0;
	for (size_t i = from; i < from + n && i < v.size(); i++) s += double(v[i]) * double(v[i]);
	return std::sqrt(s / double(std::max<size_t>(n, 1)));
}

} // namespace

int main(int argc, char **argv)
{
	std::string rom, wav_a, wav_b;
	long frames = 3000;
	long seed = -1;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--frames" && i + 1 < argc) frames = strtol(argv[++i], nullptr, 10);
		else if (a == "--random" && i + 1 < argc) seed = strtol(argv[++i], nullptr, 10);
		else if (a == "--wav-a" && i + 1 < argc) wav_a = argv[++i];
		else if (a == "--wav-b" && i + 1 < argc) wav_b = argv[++i];
		else if (a[0] != '-' && rom.empty()) rom = a;
		else { fprintf(stderr, "usage: vf2soundonly <vf2.zip> [--frames N] [--random SEED] [--wav-a FILE] [--wav-b FILE]\n"); return 2; }
	}
	romset::result ra = romset::load(rom), rb = romset::load(rom);
	if (!ra.ok) { fprintf(stderr, "vf2soundonly: %s\n", ra.error.c_str()); return 1; }
	auto pa = std::make_unique<model2a>(std::move(ra));
	auto pb = std::make_unique<model2a>(std::move(rb));
	model2a &game = *pa, &snd = *pb;
	game.set_sound(true);
	game.set_byte_link(true);
	game.set_draw_3d(false);
	snd.set_sound(true);
	snd.set_sound_only(true);
	snd.set_draw_3d(false);
	game.reset();
	snd.reset();
	std::vector<uint8_t> bytes;
	game.on_uart_tx = [&](uint8_t b) { bytes.push_back(b); };
	std::vector<int16_t> a, b;
	players::Random rnd(uint32_t(seed < 0 ? 1 : seed));
	size_t sent = 0;
	for (long f = 0; f < frames; f++) {
		if (seed >= 0) {
			const players::Ports p = rnd.next(uint64_t(f));
			game.m_in0 = uint8_t(~p.in0);
			game.m_in1 = uint8_t(~p.in1);
			game.m_in2 = uint8_t(~p.in2);
			game.m_sw = 0xff;
		}
		game.run_frame();
		snd.sound_bytes(bytes.data(), bytes.size());
		sent += bytes.size();
		bytes.clear();
		snd.run_frame();
		a.insert(a.end(), game.m_audio_out.begin(), game.m_audio_out.end());
		b.insert(b.end(), snd.m_audio_out.begin(), snd.m_audio_out.end());
		game.m_audio_out.clear();
		snd.m_audio_out.clear();
	}
	printf("%ld frames, %zu bytes to the sound board, %zu waiting at the end\n", frames, sent, snd.sound_bytes_waiting());
	const size_t sec = size_t(model2a::SOUND_RATE) * 2;
	std::vector<double> la, lb;
	for (size_t at = 0; at + sec <= std::min(a.size(), b.size()); at += sec) {
		la.push_back(rms(a, at, sec));
		lb.push_back(rms(b, at, sec));
		printf("second %3zu: the game's %7.1f, the sound board alone %7.1f\n", at / sec, la.back(), lb.back());
	}
	auto corr = [](const std::vector<double> &x, const std::vector<double> &y) {
		double mx = 0, my = 0;
		for (size_t i = 0; i < x.size(); i++) { mx += x[i]; my += y[i]; }
		mx /= double(x.size()); my /= double(y.size());
		double sxy = 0, sxx = 0, syy = 0;
		for (size_t i = 0; i < x.size(); i++) { sxy += (x[i] - mx) * (y[i] - my); sxx += (x[i] - mx) * (x[i] - mx); syy += (y[i] - my) * (y[i] - my); }
		return sxx > 0 && syy > 0 ? sxy / std::sqrt(sxx * syy) : 0.0;
	};
	if (la.size() > 2) printf("the seconds' loudness alike: %.3f (1 the same)\n", corr(la, lb));
	if (!wav_a.empty()) write_wav(wav_a, a, model2a::SOUND_RATE);
	if (!wav_b.empty()) write_wav(wav_b, b, model2a::SOUND_RATE);
	return 0;
}
