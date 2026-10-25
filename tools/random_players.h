// Inputs for the netplay tools (vf2soak, vf2net): a frame's ports, and two random players for a versus.
#pragma once
#include <cstdint>
#include <random>

namespace players {

// A frame's inputs, active high (a set bit: held): IN0's coins and starts, each player's IN1 / IN2
struct Ports { uint8_t in0 = 0, in1 = 0, in2 = 0; };
constexpr uint8_t P2_IN0 = 0x02 | 0x20;   // coin2, start2

// Two random players: the stick and buttons held for a few frames at a time, Start pressed now and then
struct Random
{
	std::mt19937 rng;
	uint8_t held[2] = {};
	int left[2] = {};
	explicit Random(uint32_t seed) : rng(seed) {}
	Ports next(uint64_t f)
	{
		Ports p;
		for (int k = 0; k < 2; k++) {
			if (left[k]-- <= 0) {
				const uint32_t r = rng();
				static const uint8_t STICK[9] = { 0, 0x10, 0x20, 0x40, 0x80, 0x50, 0x90, 0x60, 0xa0 };   // (none, the four ways, the diagonals)
				held[k] = uint8_t(STICK[r % 9] | ((r >> 4) & 7));   // P K G in bits 0-2
				left[k] = 2 + int((r >> 8) % 18);
			}
			// Start for 6 frames every few seconds, the players a second and a half apart
			const bool start = (f + uint64_t(k) * 90) % 400 < 6;
			if (k == 0) { p.in1 = held[0]; if (start) p.in0 |= 0x10; }
			else { p.in2 = held[1]; if (start) p.in0 |= 0x20; }
		}
		return p;
	}
};

} // namespace players
