// The game's own coin sound (Online > Settings > Match found chime, VF1 issue #44): what the cabinet plays when a coin
// goes in, made at run time by a sound board of its own (model2a::set_sound_only: its 68000 and SCSP alone) from the
// ROM's sound program and samples (nothing of it stored). The main board asks for it with three bytes on the link,
// 9E 0A 7F (logged at a coin from the attract, two coins; alone on a fresh sound board it is the effect heard then).
// A fresh board's program takes about 170 frames to start; bytes sent in its first frames are lost, so they go at
// frame 60 and wait there. The sound itself is about 1.6 s. The game's own board is never touched: this one is apart,
// and thrown away after.
#pragma once
#include "../machine/model2a.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

namespace coin_sound {

constexpr int RATE = model2a::SOUND_RATE;

// Stereo 16-bit at RATE at the board's level `level` (the game's LEVEL), from the sound's first sample to where it
// has died away; empty if the board played nothing
inline std::vector<int16_t> make(const romset::result &roms, float level)
{
	if (!roms.ok) return {};
	auto board = std::make_unique<model2a>(romset::result(roms), false);   // (its i960 never runs: not recompiled)
	model2a &b = *board;
	b.set_draw_3d(false);
	b.set_sound(true);
	b.set_sound_only(true);
	b.set_byte_link(true);
	b.set_level(level);
	b.reset();
	std::vector<int16_t> out;
	for (int f = 0; f < 60 + 110 + 130; f++) {
		if (f == 60) {
			static const uint8_t coin[] = { 0x9E, 0x0A, 0x7F };
			b.sound_bytes(coin, sizeof coin);
		}
		b.run_frame();
		out.insert(out.end(), b.m_audio_out.begin(), b.m_audio_out.end());
		b.m_audio_out.clear();
	}
	// from the first sample heard to the last above about -60 dB
	size_t first = 0, last = 0;
	while (first < out.size() && std::abs(int(out[first])) < 8) first++;
	for (size_t i = first; i < out.size(); i++)
		if (std::abs(int(out[i])) >= 32) last = i;
	if (first >= out.size()) return {};
	first &= ~size_t(1);
	last = std::min(out.size(), (last | 1) + 1);
	std::vector<int16_t> pcm(out.begin() + ptrdiff_t(first), out.begin() + ptrdiff_t(last));
	const size_t fade = std::min<size_t>(pcm.size() / 2, RATE / 200);   // (5 ms at the end: cut where it's quiet, not at a zero)
	for (size_t i = 0; i < fade; i++) {
		const float k = float(i) / float(fade);
		pcm[pcm.size() - 2 - 2 * i] = int16_t(pcm[pcm.size() - 2 - 2 * i] * k);
		pcm[pcm.size() - 1 - 2 * i] = int16_t(pcm[pcm.size() - 1 - 2 * i] * k);
	}
	return pcm;
}

} // namespace coin_sound
