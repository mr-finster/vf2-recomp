// A made-up ROM for the netplay tools' runs without the game (vf2soak --synthetic, vf2net --synthetic): a small
// i960 program that mixes the inputs into a number it writes all over the RAMs and devices (work, tile,
// character, palette, colour and texture RAMs, a timer, the UART to the sound board), and backup RAM with the
// settings block the game would write (free play), so a match's start finds it. Nothing of the game's.
#pragma once
#include "../src/host/romset.h"
#include <cstdint>
#include <cstring>
#include <vector>

namespace synthetic {

// i960 instructions (the KB's REG, MEMB and CTRL forms). Registers: r0-r15 are 0-15, g0-g15 16-31.
enum { G0 = 16 };
inline uint32_t reg(uint32_t op, uint32_t src1, uint32_t src2, uint32_t dst, bool lit1 = false)
{
	return (op >> 4) << 24 | dst << 19 | src2 << 14 | (lit1 ? 1u << 11 : 0) | (op & 15) << 7 | src1;
}
inline uint32_t memb(uint32_t op, uint32_t r, uint32_t abase, uint32_t mode) { return op << 24 | r << 19 | abase << 14 | 1u << 12 | mode << 10; }
enum : uint32_t { ADDO = 0x590, MULO = 0x701, XOR = 0x586, AND = 0x581, SHRO = 0x598, SHLO = 0x59c,
	LDOB = 0x80, STOB = 0x82, LDA = 0x8c, ST = 0x92, ABS = 0xc, DISP_BASE = 0xd };

inline romset::result roms()
{
	romset::result r;
	r.ok = true;
	const size_t sizes[romset::N_REGIONS] = { 0x200000, 0x2400000, 0x800000, 0x2000000, 0x1000000, 0x80000, 0x800000, 0x40000, 0x80000, 0x200000 };
	for (int i = 0; i < romset::N_REGIONS; i++) r.regions[i].assign(sizes[i], 0);
	std::vector<uint8_t> &rom = r.regions[romset::MAINCPU];
	auto put = [&](uint32_t at, uint32_t w) { memcpy(&rom[at], &w, 4); };
	// the boot: the PRCB at 0x1000 (its interrupt table at 0x2000, the stack in RAM at 0x230000), the code at 0x100
	put(4, 0x1000);
	put(12, 0x100);
	put(0x1000 + 20, 0x2000);
	put(0x1000 + 24, 0x230000);
	std::vector<uint32_t> c;
	auto lda = [&](uint32_t v, int g) { c.push_back(memb(LDA, G0 + g, 0, ABS)); c.push_back(v); };
	auto mem = [&](uint32_t op, int g, int base, uint32_t disp) { c.push_back(memb(op, G0 + g, G0 + base, DISP_BASE)); c.push_back(disp); };
	auto masked = [&](int shift, int mask_g, int to) {   // g<to> = (g0 >> shift) & g<mask>
		c.push_back(reg(SHRO, uint32_t(shift), G0 + 0, G0 + to, true));
		c.push_back(reg(AND, G0 + mask_g, G0 + to, G0 + to));
	};
	lda(0x12345678, 0);    // the number
	lda(1103515245, 1);    // its multiplier
	lda(0xffffc, 6); lda(0xfffc, 9); lda(0x3ffc, 10); lda(0x3ffffc, 11); lda(0x7fffc, 12); lda(0xfff0, 13);
	lda(0x4e, 2); mem(STOB, 2, 14, 0x01c80002);   // the UART: its mode (g14 is 0), then the command: transmit on
	lda(0x05, 2); mem(STOB, 2, 14, 0x01c80002);
	const uint32_t loop = uint32_t(c.size());
	c.push_back(reg(MULO, G0 + 1, G0 + 0, G0 + 0));
	c.push_back(reg(ADDO, 13, G0 + 0, G0 + 0, true));
	mem(LDOB, 3, 14, 0x01c00004);                 // IN1
	mem(LDOB, 4, 14, 0x01c00006);                 // IN2
	c.push_back(reg(SHLO, 8, G0 + 4, G0 + 4, true));
	c.push_back(reg(XOR, G0 + 3, G0 + 0, G0 + 0));
	c.push_back(reg(XOR, G0 + 4, G0 + 0, G0 + 0));
	masked(12, 12, 5); mem(ST, 0, 5, 0x00510000);   // work RAM (past the game's own variables)
	masked(0, 9, 5); mem(ST, 0, 5, 0x01000000);     // tile RAM
	masked(9, 12, 5); mem(ST, 0, 5, 0x01080000);    // character RAM
	masked(3, 10, 5); mem(ST, 0, 5, 0x01800000);    // palette
	masked(7, 9, 5); mem(ST, 0, 5, 0x01810000);     // colour tables
	masked(6, 11, 5); mem(ST, 0, 5, 0x12000000);    // texture RAM 0
	masked(10, 11, 5); mem(ST, 0, 5, 0x12400000);   // texture RAM 1
	masked(4, 13, 5); mem(ST, 5, 14, 0x00f00000);   // timer 0, counting down from a number up to 0xfff0
	mem(STOB, 0, 14, 0x01c80000);                   // a byte to the sound board
	const int32_t back = -int32_t((c.size() - loop) * 4);
	c.push_back(0x08u << 24 | (uint32_t(back) & 0xfffffc));   // b loop
	for (size_t i = 0; i < c.size(); i++) put(uint32_t(0x100 + 4 * i), c[i]);
	return r;
}

// The settings block the game writes in backup RAM on its first boot (model2a.cpp: at 0x3300, its CRCs), with
// free play and the arcade's game assignment
inline void settings(std::vector<uint8_t> &b)
{
	auto crc16 = [&](uint32_t from, uint32_t to) {
		uint16_t crc = 0;
		for (uint32_t i = from; i < to; i++) {
			crc ^= uint16_t(b[i] << 8);
			for (int k = 0; k < 8; k++) crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
		}
		return crc;
	};
	const uint32_t S = 0x3300;
	std::fill(b.begin() + S, b.begin() + S + 0x60, 0);
	b[S + 6] = 0x18;
	memcpy(&b[S + 8], "VIRTUA FIGHTER 2", 16);
	b[S + 0x24] = 0x19;   // FREE PLAY
	b[S + 0x40] = 2; b[S + 0x41] = 2; b[S + 0x42] = 1; b[S + 0x4f] = 15;
	const uint16_t c1 = crc16(S + 0x1a, S + 0x2f), c2 = crc16(S + 0x40, S + 0x5d);
	b[S] = uint8_t(c1); b[S + 1] = uint8_t(c1 >> 8);
	b[S + 2] = uint8_t(c2); b[S + 3] = uint8_t(c2 >> 8);
}

} // namespace synthetic
