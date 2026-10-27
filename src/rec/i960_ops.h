// license:BSD-3-Clause
// copyright-holders:Farfetch'd, R. Belmont
//
// The i960KB's instructions for the recompiled code: MAME's execute_op (third_party/mame/i960/
// i960.cpp), case for case, as an always-inlined function of the instruction word. Called with a
// constant word (and displacement), the compiler keeps only that instruction's case with its
// operands decoded, so the recompiled C++ does exactly what the interpreter does, with the same
// cycle counts, in the same order, through the same memory accesses.
//
// Differences from execute_op, all of them about the instruction already being decoded:
//  - IP is the next instruction's address on entry (the interpreter's after the fetch and the
//    displacement word, which get_ea reads); get_ea's displacement comes as an argument.
//  - the operand helpers (get_1_ri, set_ri ...) are inline copies; the rare and long ones
//    (do_call, do_ret, check_pending_irqs, send_iac, burst_stall_save) are MAME's own.
#pragma once
#include "../core/i960_device.h"
#include "i960_rec.h"
#include <bit>
#include <cmath>

#if defined(__GNUC__)
#define I960_FI inline __attribute__((always_inline))
#else
#define I960_FI inline
#endif

struct i960_rec
{
	using C = i960_device;

	static u32 *regs(C &c) { return c.m_r; }
	static u32 &ip(C &c) { return c.m_IP; }
	static u32 &pip(C &c) { return c.m_PIP; }
	static int &icount(C &c) { return c.m_icount; }
	static bool &stalled(C &c) { return c.m_stalled; }
	static bool burst_mode(C &c) { return c.m_stall_state.burst_mode; }
	static u32 &ac(C &c) { return c.m_AC; }

	// ---- the interpreter's loop body: one instruction (execute_run) ----
	static void interp_one(C &c)
	{
		c.m_PIP = c.m_IP;
		c.debugger_instruction_hook(c.m_IP);
		const u32 opcode = c.m_cache.read_dword(c.m_IP);
		c.m_IP += 4;
		c.m_stalled = false;
		if (c.m_stall_state.burst_mode) c.execute_burst_stall_op(opcode);
		else c.execute_op(opcode);
	}
	static void check_immediate_irqs(C &c) { c.check_immediate_irqs(); }

	// ---- operands (MAME's helpers, inline) ----
	static I960_FI u32 ea(C &c, u32 op, u32 disp)
	{
		const int abase = (op >> 14) & 0x1f;
		if (!(op & 0x1000)) {
			const u32 offset = op & 0x1fff;
			return (op & 0x2000) ? c.m_r[abase] + offset : offset;
		}
		const int index = op & 0x1f, scale = (op >> 7) & 7;
		switch ((op >> 10) & 0xf) {
		case 0x4: return c.m_r[abase];
		case 0x5: return disp + c.m_IP;              // (IP: past the displacement)
		case 0x7: return c.m_r[abase] + (c.m_r[index] << scale);
		case 0xc: return disp;
		case 0xd: return disp + c.m_r[abase];
		case 0xe: return disp + (c.m_r[index] << scale);
		case 0xf: return disp + c.m_r[abase] + (c.m_r[index] << scale);
		default: fatalerror("I960: %x: unhandled MEMB mode %x\n", c.m_PIP, (op >> 10) & 0xf);
		}
	}
	static I960_FI u32 get_1_ri(C &c, u32 op) { return !(op & 0x800) ? c.m_r[op & 0x1f] : op & 0x1f; }
	static I960_FI u32 get_2_ri(C &c, u32 op) { return !(op & 0x1000) ? c.m_r[(op >> 14) & 0x1f] : (op >> 14) & 0x1f; }
	static I960_FI u64 get_2_ri64(C &c, u32 op)
	{
		if (!(op & 0x1000)) return c.m_r[(op >> 14) & 0x1f] | (u64(c.m_r[((op >> 14) & 0x1f) + 1]) << 32);
		return (op >> 14) & 0x1f;
	}
	static I960_FI void set_ri(C &c, u32 op, u32 v)
	{
		if (!(op & 0x2000)) c.m_r[(op >> 19) & 0x1f] = v;
		else fatalerror("I960: %x: set_ri on literal?\n", c.m_PIP);
	}
	static I960_FI void set_ri2(C &c, u32 op, u32 v, u32 v2)
	{
		if (!(op & 0x2000)) { c.m_r[(op >> 19) & 0x1f] = v; c.m_r[((op >> 19) & 0x1f) + 1] = v2; }
		else fatalerror("I960: %x: set_ri2 on literal?\n", c.m_PIP);
	}
	static I960_FI void set_ri64(C &c, u32 op, u64 v)
	{
		if (!(op & 0x2000)) { c.m_r[(op >> 19) & 0x1f] = u32(v); c.m_r[((op >> 19) & 0x1f) + 1] = u32(v >> 32); }
		else fatalerror("I960: %x: set_ri64 on literal?\n", c.m_PIP);
	}
	static I960_FI double lit_f(C &c, int idx) { return idx < 4 ? c.m_fp[idx] : idx == 0x16 ? 1.0 : 0.0; }
	static I960_FI double get_1_rif(C &c, u32 op) { return !(op & 0x800) ? double(u2f(c.m_r[op & 0x1f])) : lit_f(c, op & 0x1f); }
	static I960_FI double get_2_rif(C &c, u32 op) { return !(op & 0x1000) ? double(u2f(c.m_r[(op >> 14) & 0x1f])) : lit_f(c, (op >> 14) & 0x1f); }
	// (a NaN stored as one pattern, x86's own: an invalid operation's NaN is negative on x86, positive on ARM, and
	// a Mac and a PC in a match must store the same bits)
	static I960_FI double nan_fixed(double v) { return v != v ? u2d(0xfff8000000000000ull) : v; }
	static I960_FI void set_rif(C &c, u32 op, double v)
	{
		v = nan_fixed(v);
		if (!(op & 0x2000)) c.m_r[(op >> 19) & 0x1f] = v != v ? 0xffc00000u : f2u(float(v));
		else if (!(op & 0x00e00000)) c.m_fp[(op >> 19) & 3] = v;
		else fatalerror("I960: %x: set_rif on literal?\n", c.m_PIP);
	}
	static I960_FI double get_1_rifl(C &c, u32 op)
	{
		if (!(op & 0x800)) return u2d(c.m_r[op & 0x1e] | (u64(c.m_r[(op & 0x1e) + 1]) << 32));
		return lit_f(c, op & 0x1f);
	}
	static I960_FI double get_2_rifl(C &c, u32 op)
	{
		if (!(op & 0x1000)) return u2d(c.m_r[(op >> 14) & 0x1e] | (u64(c.m_r[((op >> 14) & 0x1e) + 1]) << 32));
		return lit_f(c, (op >> 14) & 0x1f);
	}
	static I960_FI void set_rifl(C &c, u32 op, double v)
	{
		v = nan_fixed(v);
		if (!(op & 0x2000)) { const u64 b = d2u(v); c.m_r[(op >> 19) & 0x1e] = u32(b); c.m_r[((op >> 19) & 0x1e) + 1] = u32(b >> 32); }
		else if (!(op & 0x00e00000)) c.m_fp[(op >> 19) & 3] = v;
		else fatalerror("I960: %x: set_rifl on literal?\n", c.m_PIP);
	}
	static I960_FI u32 get_1_ci(C &c, u32 op) { return !(op & 0x2000) ? c.m_r[(op >> 19) & 0x1f] : (op >> 19) & 0x1f; }
	static I960_FI u32 get_2_ci(C &c, u32 op) { return c.m_r[(op >> 14) & 0x1f]; }
	static I960_FI u32 get_disp(u32 op) { return u32(util::sext(op, 24) - 4); }
	static I960_FI u32 get_disp_s(u32 op) { return u32(util::sext(op, 13) - 4); }
	static I960_FI void cmp_s(C &c, s32 a, s32 b) { c.m_AC = (c.m_AC & ~7u) | (a < b ? 4 : a == b ? 2 : 1); }
	static I960_FI void cmp_u(C &c, u32 a, u32 b) { c.m_AC = (c.m_AC & ~7u) | (a < b ? 4 : a == b ? 2 : 1); }
	static I960_FI void concmp_s(C &c, s32 a, s32 b) { c.m_AC = (c.m_AC & ~7u) | (a <= b ? 2 : 1); }
	static I960_FI void concmp_u(C &c, u32 a, u32 b) { c.m_AC = (c.m_AC & ~7u) | (a <= b ? 2 : 1); }
	static I960_FI void cmp_d(C &c, double a, double b)
	{
		c.m_AC &= ~7u;
		if (a < b) c.m_AC |= 4;
		else if (a == b) c.m_AC |= 2;
		else if (a > b) c.m_AC |= 1;
	}
	static I960_FI void bxx(C &c, u32 op, int mask) { if (c.m_AC & mask) { c.m_IP += get_disp(op); c.m_IP &= ~3u; } }
	static I960_FI void bxx_s(C &c, u32 op, int mask) { if (c.m_AC & mask) { c.m_IP += get_disp_s(op); c.m_IP &= ~3u; } }
	static I960_FI void fxx(C &c, u32, int mask) { if (c.m_AC & mask) fatalerror("Taking the fault on a FAULT insn not yet supported\n"); }
	static I960_FI void test(C &c, u32 op, int mask) { c.m_r[(op >> 19) & 0x1f] = (c.m_AC & mask) ? 1 : 0; }
	static I960_FI double round_to_int(C &c, double v)
	{
		switch ((c.m_AC >> 30) & 3) {
		case 0: return round(v);
		case 1: return floor(v);
		case 2: return ceil(v);
		default: return trunc(v);
		}
	}

	// ---- memory (the core's unaligned helpers), plain memory straight from where it is ----
	static I960_FI u8 *rp(C &c, u32 a) { return a < (i960rec::runtime::PAGES << 12) ? c.m_rec->rpage.data()[a >> 12] : nullptr; }
	static I960_FI u8 *wp(C &c, u32 a) { return a < (i960rec::runtime::PAGES << 12) ? c.m_rec->wpage.data()[a >> 12] : nullptr; }
	static I960_FI u8 rd8(C &c, u32 a)
	{
		if (u8 *p = rp(c, a)) return p[a & 0xfff];
		return c.m_program.read_byte(a);
	}
	static I960_FI void wr8(C &c, u32 a, u8 v)
	{
		if (u8 *p = wp(c, a)) { p[a & 0xfff] = v; return; }
		c.m_program.write_byte(a, v);
	}
	static I960_FI u32 rd32(C &c, u32 a)
	{
		if (a & 3) return c.m_program.read_byte(a) | c.m_program.read_byte(a + 1) << 8 | c.m_program.read_byte(a + 2) << 16 | u32(c.m_program.read_byte(a + 3)) << 24;
		if (u8 *p = rp(c, a)) { u32 v; memcpy(&v, p + (a & 0xfff), 4); return v; }
		return c.m_program.read_dword(a);
	}
	static I960_FI std::pair<u32, u16> rd32f(C &c, u32 a)
	{
		if (a & 3) {
			auto v = c.m_program.read_byte_flags(a);
			return { v.first | c.m_program.read_byte(a + 1) << 8 | c.m_program.read_byte(a + 2) << 16 | u32(c.m_program.read_byte(a + 3)) << 24, v.second };
		}
		if (u8 *p = rp(c, a)) { u32 v; memcpy(&v, p + (a & 0xfff), 4); return { v, i960_cpu_device::BURST }; }
		return c.m_program.read_dword_flags(a);
	}
	static I960_FI u16 rd16(C &c, u32 a)
	{
		if (a & 1) return u16(c.m_program.read_byte(a) | c.m_program.read_byte(a + 1) << 8);
		if (u8 *p = rp(c, a)) { u16 v; memcpy(&v, p + (a & 0xfff), 2); return v; }
		return c.m_program.read_word(a);
	}
	static I960_FI void wr32(C &c, u32 a, u32 v)
	{
		if (a & 3) {
			c.m_program.write_byte(a, v & 0xff);
			c.m_program.write_byte(a + 1, (v >> 8) & 0xff);
			c.m_program.write_byte(a + 2, (v >> 16) & 0xff);
			c.m_program.write_byte(a + 3, (v >> 24) & 0xff);
		} else if (u8 *p = wp(c, a)) memcpy(p + (a & 0xfff), &v, 4);
		else c.m_program.write_dword(a, v);
	}
	static I960_FI u16 wr32f(C &c, u32 a, u32 v)
	{
		if (a & 3) {
			const u16 f = c.m_program.write_byte_flags(a, v & 0xff);
			c.m_program.write_byte(a + 1, (v >> 8) & 0xff);
			c.m_program.write_byte(a + 2, (v >> 16) & 0xff);
			c.m_program.write_byte(a + 3, (v >> 24) & 0xff);
			return f;
		}
		if (u8 *p = wp(c, a)) { memcpy(p + (a & 0xfff), &v, 4); return i960_cpu_device::BURST; }
		return c.m_program.write_dword_flags(a, v);
	}
	static I960_FI void wr16(C &c, u32 a, u16 v)
	{
		if (a & 1) { c.m_program.write_byte(a, v & 0xff); c.m_program.write_byte(a + 1, (v >> 8) & 0xff); }
		else if (u8 *p = wp(c, a)) memcpy(p + (a & 0xfff), &v, 2);
		else c.m_program.write_word(a, v);
	}

	// ldl/ldt/ldq: n words to r[t2..]; false if the access stalled (the burst saved, as MAME)
	static I960_FI bool ld_multi(C &c, u32 t1, u32 t2, int n)
	{
		for (int i = 0; i < n; i++) {
			auto pack = rd32f(c, t1);
			if (c.m_stalled) { c.burst_stall_save(t1, t2, i, n, false); return false; }
			c.m_r[t2 + i] = pack.first;
			if (pack.second & i960_cpu_device::BURST) t1 += 4;
		}
		return true;
	}
	static I960_FI bool st_multi(C &c, u32 t1, u32 t2, int n)
	{
		for (int i = 0; i < n; i++) {
			const u16 flags = wr32f(c, t1, c.m_r[t2 + i]);
			if (c.m_stalled) { c.burst_stall_save(t1, t2, i, n, true); return false; }
			if (flags & i960_cpu_device::BURST) t1 += 4;
		}
		return true;
	}

	static void do_call(C &c, u32 adr, int type, u32 stack) { c.do_call(adr, type, stack); }
	static void do_ret(C &c) { c.do_ret(); }

	// ---- one instruction: execute_op's case for `op` (IP already past it) ----
	static I960_FI void exec(C &c, const u32 op, const u32 disp);
};

// (outside the class, so that it can use the helpers above in any order)
I960_FI void i960_rec::exec(C &c, const u32 opcode, const u32 disp)
{
	u32 t1, t2;
	double t1f, t2f;
	int &ic = c.m_icount;
	u32 *const r = c.m_r;
#define EA() ea(c, opcode, disp)
#define FAT(sub) fatalerror("I960: %x: Unhandled %02x.%x\n", c.m_PIP, opcode >> 24, sub)
	switch (opcode >> 24) {
	case 0x08: ic--; c.m_IP += get_disp(opcode); break;                                   // b
	case 0x09: do_call(c, c.m_IP + get_disp(opcode), 0, r[I960_SP]); break;               // call
	case 0x0a: do_ret(c); break;                                                           // ret
	case 0x0b: ic -= 5; r[0x1e] = c.m_IP; c.m_IP += get_disp(opcode); break;              // bal
	case 0x10: ic--; if (!(c.m_AC & 7)) c.m_IP += get_disp(opcode); break;                // bno
	case 0x11: ic--; bxx(c, opcode, 1); break;
	case 0x12: ic--; bxx(c, opcode, 2); break;
	case 0x13: ic--; bxx(c, opcode, 3); break;
	case 0x14: ic--; bxx(c, opcode, 4); break;
	case 0x15: ic--; bxx(c, opcode, 5); break;
	case 0x16: ic--; bxx(c, opcode, 6); break;
	case 0x17: ic--; bxx(c, opcode, 7); break;
	case 0x18: ic--; if (!(c.m_AC & 7)) c.m_IP += get_disp(opcode); break;                // faultno
	case 0x19: ic--; fxx(c, opcode, 1); break;
	case 0x1a: ic--; fxx(c, opcode, 2); break;
	case 0x1b: ic--; fxx(c, opcode, 3); break;
	case 0x1c: ic--; fxx(c, opcode, 4); break;
	case 0x1d: ic--; fxx(c, opcode, 5); break;
	case 0x1e: ic--; fxx(c, opcode, 6); break;
	case 0x1f: ic--; fxx(c, opcode, 7); break;
	case 0x20: ic--; r[(opcode >> 19) & 0x1f] = !(c.m_AC & 7) ? 1 : 0; break;            // testno
	case 0x21: ic--; test(c, opcode, 1); break;
	case 0x22: ic--; test(c, opcode, 2); break;
	case 0x23: ic--; test(c, opcode, 3); break;
	case 0x24: ic--; test(c, opcode, 4); break;
	case 0x25: ic--; test(c, opcode, 5); break;
	case 0x26: ic--; test(c, opcode, 6); break;
	case 0x27: ic--; test(c, opcode, 7); break;
	case 0x30:   // bbc
		ic -= 4;
		t1 = get_1_ci(c, opcode) & 0x1f;
		t2 = get_2_ci(c, opcode);
		if (!(t2 & (1u << t1))) { c.m_AC = (c.m_AC & ~7u) | 2; c.m_IP += get_disp_s(opcode); }
		else c.m_AC &= ~7u;
		break;
	case 0x31: ic -= 4; cmp_u(c, get_1_ci(c, opcode), get_2_ci(c, opcode)); bxx_s(c, opcode, 1); break;
	case 0x32: ic -= 4; cmp_u(c, get_1_ci(c, opcode), get_2_ci(c, opcode)); bxx_s(c, opcode, 2); break;
	case 0x33: ic -= 4; cmp_u(c, get_1_ci(c, opcode), get_2_ci(c, opcode)); bxx_s(c, opcode, 3); break;
	case 0x34: ic -= 4; cmp_u(c, get_1_ci(c, opcode), get_2_ci(c, opcode)); bxx_s(c, opcode, 4); break;
	case 0x35: ic -= 4; cmp_u(c, get_1_ci(c, opcode), get_2_ci(c, opcode)); bxx_s(c, opcode, 5); break;
	case 0x36: ic -= 4; cmp_u(c, get_1_ci(c, opcode), get_2_ci(c, opcode)); bxx_s(c, opcode, 6); break;
	case 0x37:   // bbs
		ic -= 4;
		t1 = get_1_ci(c, opcode) & 0x1f;
		t2 = get_2_ci(c, opcode);
		if (t2 & (1u << t1)) { c.m_AC = (c.m_AC & ~7u) | 2; c.m_IP += get_disp_s(opcode); }
		else c.m_AC &= ~7u;
		break;
	case 0x39: ic -= 4; cmp_s(c, s32(get_1_ci(c, opcode)), s32(get_2_ci(c, opcode))); bxx_s(c, opcode, 1); break;
	case 0x3a: ic -= 4; cmp_s(c, s32(get_1_ci(c, opcode)), s32(get_2_ci(c, opcode))); bxx_s(c, opcode, 2); break;
	case 0x3b: ic -= 4; cmp_s(c, s32(get_1_ci(c, opcode)), s32(get_2_ci(c, opcode))); bxx_s(c, opcode, 3); break;
	case 0x3c: ic -= 4; cmp_s(c, s32(get_1_ci(c, opcode)), s32(get_2_ci(c, opcode))); bxx_s(c, opcode, 4); break;
	case 0x3d: ic -= 4; cmp_s(c, s32(get_1_ci(c, opcode)), s32(get_2_ci(c, opcode))); bxx_s(c, opcode, 5); break;
	case 0x3e: ic -= 4; cmp_s(c, s32(get_1_ci(c, opcode)), s32(get_2_ci(c, opcode))); bxx_s(c, opcode, 6); break;

	case 0x58:
		t1 = get_1_ri(c, opcode);
		t2 = get_2_ri(c, opcode);
		switch ((opcode >> 7) & 0xf) {
		case 0x0: ic -= 2; set_ri(c, opcode, t2 ^ (1u << (t1 & 31))); break;        // notbit
		case 0x1: ic--; set_ri(c, opcode, t2 & t1); break;                          // and
		case 0x2: ic--; set_ri(c, opcode, t2 & ~t1); break;                         // andnot
		case 0x3: ic -= 2; set_ri(c, opcode, t2 | (1u << (t1 & 31))); break;        // setbit
		case 0x4: ic--; set_ri(c, opcode, (~t2) & t1); break;                       // notand
		case 0x6: ic--; set_ri(c, opcode, t2 ^ t1); break;                          // xor
		case 0x7: ic--; set_ri(c, opcode, t2 | t1); break;                          // or
		case 0x8: ic--; set_ri(c, opcode, (~t2) & (~t1)); break;                    // nor
		case 0x9: ic--; set_ri(c, opcode, ~(t2 ^ t1)); break;                       // xnor
		case 0xa: ic--; set_ri(c, opcode, ~t1); break;                              // not
		case 0xb: ic--; set_ri(c, opcode, t2 | ~t1); break;                         // ornot
		case 0xc: ic -= 2; set_ri(c, opcode, t2 & ~(1u << (t1 & 31))); break;       // clrbit
		case 0xd: ic--; set_ri(c, opcode, (~t2) | t1); break;                       // notor
		case 0xe: ic -= 2; set_ri(c, opcode, ~t2 | ~t1); break;                     // nand
		case 0xf:                                                                   // alterbit
			ic -= 2;
			if (c.m_AC & 2) set_ri(c, opcode, t2 | (1u << (t1 & 31)));
			else set_ri(c, opcode, t2 & ~(1u << (t1 & 31)));
			break;
		default: FAT((opcode >> 7) & 0xf);
		}
		break;

	case 0x59:
		t1 = get_1_ri(c, opcode);
		t2 = get_2_ri(c, opcode);
		switch ((opcode >> 7) & 0xf) {
		case 0x0: case 0x1: ic--; set_ri(c, opcode, t2 + t1); break;               // addo, addi
		case 0x2: case 0x3: ic--; set_ri(c, opcode, t2 - t1); break;               // subo, subi
		case 0x8: ic--; set_ri(c, opcode, t1 >= 32 ? 0 : t2 >> t1); break;          // shro
		case 0xa:                                                                   // shrdi
			ic--;
			if (t1 >= 32) set_ri(c, opcode, 0);
			else if (s32(t2) < 0) {
				if (t2 & ((1u << t1) - 1)) set_ri(c, opcode, u32((s32(t2) >> t1) + 1));
				else set_ri(c, opcode, u32(s32(t2) >> t1));
			} else set_ri(c, opcode, t2 >> t1);
			break;
		case 0xb: ic--; set_ri(c, opcode, t1 >= 32 ? (s32(t2) < 0 ? ~0u : 0u) : u32(s32(t2) >> t1)); break;   // shri
		case 0xc: ic--; set_ri(c, opcode, t1 >= 32 ? 0 : t2 << t1); break;          // shlo
		case 0xd: ic--; set_ri(c, opcode, std::rotl(t2, int(t1 & 0x1f))); break;    // rotate
		case 0xe: ic--; set_ri(c, opcode, t1 >= 32 ? 0 : t2 << t1); break;          // shli
		default: FAT((opcode >> 7) & 0xf);
		}
		break;

	case 0x5a:
		switch ((opcode >> 7) & 0xf) {
		case 0x0: ic--; cmp_u(c, get_1_ri(c, opcode), get_2_ri(c, opcode)); break;              // cmpo
		case 0x1: ic--; cmp_s(c, s32(get_1_ri(c, opcode)), s32(get_2_ri(c, opcode))); break;    // cmpi
		case 0x2: ic--; if (!(c.m_AC & 4)) concmp_u(c, get_1_ri(c, opcode), get_2_ri(c, opcode)); break;
		case 0x3: ic--; if (!(c.m_AC & 4)) concmp_s(c, s32(get_1_ri(c, opcode)), s32(get_2_ri(c, opcode))); break;
		case 0x4: ic -= 2; t1 = get_1_ri(c, opcode); t2 = get_2_ri(c, opcode); cmp_u(c, t1, t2); set_ri(c, opcode, t2 + 1); break;
		case 0x5: ic -= 2; t1 = get_1_ri(c, opcode); t2 = get_2_ri(c, opcode); cmp_s(c, s32(t1), s32(t2)); set_ri(c, opcode, t2 + 1); break;
		case 0x6: ic -= 2; t1 = get_1_ri(c, opcode); t2 = get_2_ri(c, opcode); cmp_u(c, t1, t2); set_ri(c, opcode, t2 - 1); break;
		case 0x7: ic -= 2; t1 = get_1_ri(c, opcode); t2 = get_2_ri(c, opcode); cmp_s(c, s32(t1), s32(t2)); set_ri(c, opcode, t2 - 1); break;
		case 0xc:   // scanbyte
			ic -= 2;
			c.m_AC &= ~7u;
			t1 = get_1_ri(c, opcode);
			t2 = get_2_ri(c, opcode);
			if ((t1 & 0xff000000) == (t2 & 0xff000000) || (t1 & 0x00ff0000) == (t2 & 0x00ff0000) ||
				(t1 & 0x0000ff00) == (t2 & 0x0000ff00) || (t1 & 0x000000ff) == (t2 & 0x000000ff))
				c.m_AC |= 2;
			break;
		case 0xe:   // chkbit
			ic -= 2;
			t1 = get_1_ri(c, opcode) & 0x1f;
			t2 = get_2_ri(c, opcode);
			if (t2 & (1u << t1)) c.m_AC = (c.m_AC & ~7u) | 2;
			else c.m_AC &= ~7u;
			break;
		default: FAT((opcode >> 7) & 0xf);
		}
		break;

	case 0x5b:
		switch ((opcode >> 7) & 0xf) {
		case 0x0: {   // addc
			ic -= 2;
			t1 = get_1_ri(c, opcode);
			t2 = get_2_ri(c, opcode);
			// (as MAME: in 32 bits, the carry out is lost before the test)
			const u64 res = t2 + (t1 + ((c.m_AC >> 1) & 1));
			set_ri(c, opcode, u32(res));
			c.m_AC &= ~0x3u;
			c.m_AC |= (res & (u64(1) << 32)) ? 0x2 : 0;
			c.m_AC |= ((res ^ t1) & (res ^ t2) & 0x80000000) ? 1 : 0;
			break;
		}
		case 0x2: {   // subc
			ic -= 2;
			t1 = get_1_ri(c, opcode);
			t2 = get_2_ri(c, opcode);
			const u64 res = u64(t2) + u64(u32(~t1)) + ((c.m_AC >> 1) & 1);
			set_ri(c, opcode, u32(res));
			c.m_AC &= ~0x7u;
			c.m_AC |= (res & (u64(1) << 32)) ? 0x2 : 0;
			c.m_AC |= ((t2 ^ t1) & (t2 ^ res) & 0x80000000) ? 1 : 0;
			break;
		}
		default: FAT((opcode >> 7) & 0xf);
		}
		break;

	case 0x5c:   // mov
		if (((opcode >> 7) & 0xf) != 0xc) FAT((opcode >> 7) & 0xf);
		ic -= 2;
		set_ri(c, opcode, get_1_ri(c, opcode));
		break;
	case 0x5d:   // movl
		if (((opcode >> 7) & 0xf) != 0xc) FAT((opcode >> 7) & 0xf);
		ic -= 2;
		t2 = (opcode >> 19) & 0x1e;
		if (opcode & 0x800) { t1 = opcode & 0x1f; r[t2] = r[t2 + 1] = t1; }
		else memcpy(r + t2, r + (opcode & 0x1f), 2 * sizeof(u32));
		break;
	case 0x5e:   // movt
		if (((opcode >> 7) & 0xf) != 0xc) FAT((opcode >> 7) & 0xf);
		ic -= 3;
		t2 = (opcode >> 19) & 0x1c;
		if (opcode & 0x800) { t1 = opcode & 0x1f; r[t2] = r[t2 + 1] = r[t2 + 2] = t1; }
		else memcpy(r + t2, r + (opcode & 0x1f), 3 * sizeof(u32));
		break;
	case 0x5f:   // movq
		if (((opcode >> 7) & 0xf) != 0xc) FAT((opcode >> 7) & 0xf);
		ic -= 4;
		t2 = (opcode >> 19) & 0x1c;
		if (opcode & 0x800) { t1 = opcode & 0x1f; r[t2] = r[t2 + 1] = r[t2 + 2] = r[t2 + 3] = t1; }
		else memcpy(r + t2, r + (opcode & 0x1f), 4 * sizeof(u32));
		break;

	case 0x60:
		switch ((opcode >> 7) & 0xf) {
		case 0x0:   // synmov
			ic -= 6;
			t1 = get_1_ri(c, opcode);
			t2 = get_2_ri(c, opcode);
			if (t1 == 0xff000004) c.m_ICR = c.m_program.read_dword(t2);
			else c.m_program.write_dword(t1, c.m_program.read_dword(t2));
			c.m_AC = (c.m_AC & ~7u) | 2;
			break;
		case 0x2:   // synmovq
			ic -= 12;
			t1 = get_1_ri(c, opcode);
			t2 = get_2_ri(c, opcode);
			if (t1 == 0xff000010) c.send_iac(t2);
			else {
				c.m_program.write_dword(t1, c.m_program.read_dword(t2));
				c.m_program.write_dword(t1 + 4, c.m_program.read_dword(t2 + 4));
				c.m_program.write_dword(t1 + 8, c.m_program.read_dword(t2 + 8));
				c.m_program.write_dword(t1 + 12, c.m_program.read_dword(t2 + 12));
			}
			c.m_AC = (c.m_AC & ~7u) | 2;
			break;
		default: FAT((opcode >> 7) & 0xf);
		}
		break;

	case 0x64:
		switch ((opcode >> 7) & 0xf) {
		case 0x0: case 0x1: {   // spanbit, scanbit
			u32 res = 0xffffffff;
			const bool scan = ((opcode >> 7) & 0xf) == 1;
			ic -= 10;
			t1 = get_1_ri(c, opcode);
			c.m_AC &= ~7u;
			for (int i = 31; i >= 0; i--)
				if (scan ? (t1 & (1u << i)) : !(t1 & (1u << i))) { c.m_AC |= 2; res = u32(i); break; }
			set_ri(c, opcode, res);
			break;
		}
		case 0x4:   // dmovt
			ic -= 7;
			t1 = get_1_ri(c, opcode);
			set_ri(c, opcode, t1);
			c.m_AC &= 0xfff8;
			if ((t1 & 0xff) < 0x30 || (t1 & 0xff) > 0x39) c.m_AC |= 2;
			break;
		case 0x5:   // modac
			ic -= 10;
			t1 = get_1_ri(c, opcode);
			t2 = get_2_ri(c, opcode);
			set_ri(c, opcode, c.m_AC);
			c.m_AC = (c.m_AC & ~t1) | (t2 & t1);
			break;
		default: FAT((opcode >> 7) & 0xf);
		}
		break;

	case 0x65:   // modpc
		if (((opcode >> 7) & 0xf) != 0x5) FAT((opcode >> 7) & 0xf);
		ic -= 10;
		t1 = c.m_PC;
		t2 = get_2_ri(c, opcode);
		c.m_PC = (c.m_PC & ~t2) | (r[(opcode >> 19) & 0x1f] & t2);
		set_ri(c, opcode, t1);
		if ((t1 >> 16 & 0x1f) > (c.m_PC >> 16 & 0x1f)) c.check_pending_irqs();
		break;

	case 0x66:
		switch ((opcode >> 7) & 0xf) {
		case 0x0:   // calls
			t1 = get_1_ri(c, opcode);
			t2 = c.m_program.read_dword(c.m_SAT + 152);
			t2 = c.m_program.read_dword(t2 + 48 + (t1 * 4));
			if ((t2 & 3) != 0) fatalerror("I960: system calls that jump into supervisor mode aren't yet supported\n");
			do_call(c, t2, 0, r[I960_SP]);
			break;
		case 0xd:   // flushreg
			if (c.m_rcache_pos > 4) c.m_rcache_pos = 4;
			for (t1 = 0; t1 < u32(c.m_rcache_pos); t1++)
				for (int i = 0; i < 0x10; i++)
					c.m_program.write_dword(c.m_rcache_frame_addr[t1] + (i * sizeof(u32)), c.m_rcache[t1][i]);
			c.m_rcache_pos = 0;
			break;
		default: FAT((opcode >> 7) & 0xf);
		}
		break;

	case 0x67:
		switch ((opcode >> 7) & 0xf) {
		case 0x0: ic -= 37; t1 = get_1_ri(c, opcode); t2 = get_2_ri(c, opcode); set_ri64(c, opcode, mulu_32x32(t1, t2)); break;   // emul
		case 0x1: {   // ediv
			ic -= 37;
			const u64 src1 = get_1_ri(c, opcode), src2 = get_2_ri64(c, opcode);
			set_ri2(c, opcode, u32(src2 % src1), u32(src2 / src1));
			break;
		}
		case 0x4: ic -= 30; t1 = get_1_ri(c, opcode); set_rif(c, opcode, double(s32(t1))); break;    // cvtir
		case 0x5: ic -= 30; t1 = get_1_ri(c, opcode); set_rifl(c, opcode, double(s32(t1))); break;   // cvtilr
		case 0x6: ic -= 30; t1 = get_1_ri(c, opcode); t2f = get_2_rifl(c, opcode); set_rifl(c, opcode, t2f * pow(2.0, double(s32(t1)))); break;   // scalerl
		case 0x7: ic -= 30; t1 = get_1_ri(c, opcode); t2f = get_2_rif(c, opcode); set_rif(c, opcode, t2f * pow(2.0, double(s32(t1)))); break;     // scaler
		default: FAT((opcode >> 7) & 0xf);
		}
		break;

	case 0x68:
		switch ((opcode >> 7) & 0xf) {
		case 0x0: ic -= 267; t1f = get_1_rif(c, opcode); t2f = get_2_rif(c, opcode); set_rif(c, opcode, atan2(t2f, t1f)); break;
		case 0x1: ic -= 400; t1f = get_1_rif(c, opcode); t2f = get_2_rif(c, opcode); set_rif(c, opcode, t2f * log2(t1f + 1.0)); break;
		case 0x2: ic -= 438; t1f = get_1_rif(c, opcode); t2f = get_2_rif(c, opcode); set_rif(c, opcode, t2f * log2(t1f)); break;
		case 0x3: ic -= 67; t1f = get_1_rif(c, opcode); t2f = get_2_rif(c, opcode); set_rif(c, opcode, fmod(t2f, t1f)); break;
		case 0x5: ic -= 10; t1f = get_1_rif(c, opcode); t2f = get_2_rif(c, opcode); cmp_d(c, t1f, t2f); break;
		case 0x8: ic -= 104; t1f = get_1_rif(c, opcode); set_rif(c, opcode, sqrt(t1f)); break;
		case 0x9: ic -= 334; t1f = get_1_rif(c, opcode); set_rif(c, opcode, pow(2.0, t1f) - 1.0); break;
		case 0xa: ic -= 37; t1f = get_1_rif(c, opcode); set_rif(c, opcode, logb(t1f)); break;
		case 0xb: ic -= 69; t1f = get_1_rif(c, opcode); set_rif(c, opcode, round_to_int(c, t1f)); break;
		case 0xc: ic -= 406; t1f = get_1_rif(c, opcode); set_rif(c, opcode, sin(t1f)); break;
		case 0xd: ic -= 406; t1f = get_1_rif(c, opcode); set_rif(c, opcode, cos(t1f)); break;
		case 0xe: ic -= 293; t1f = get_1_rif(c, opcode); set_rif(c, opcode, tan(t1f)); break;
		default: FAT((opcode >> 7) & 0xf);
		}
		break;

	case 0x69:
		switch ((opcode >> 7) & 0xf) {
		case 0x0: ic -= 350; t1f = get_1_rifl(c, opcode); t2f = get_2_rifl(c, opcode); set_rifl(c, opcode, atan2(t2f, t1f)); break;
		case 0x2: ic -= 438; t1f = get_1_rifl(c, opcode); t2f = get_2_rifl(c, opcode); set_rifl(c, opcode, t2f * log2(t1f)); break;
		case 0x5: ic -= 12; t1f = get_1_rifl(c, opcode); t2f = get_2_rifl(c, opcode); cmp_d(c, t1f, t2f); break;
		case 0x8: ic -= 104; t1f = get_1_rifl(c, opcode); set_rifl(c, opcode, sqrt(t1f)); break;
		case 0x9: ic -= 334; t1f = get_1_rifl(c, opcode); set_rifl(c, opcode, pow(2.0, t1f) - 1.0); break;
		case 0xa: ic -= 37; t1f = get_1_rifl(c, opcode); set_rifl(c, opcode, logb(t1f)); break;
		case 0xb: ic -= 70; t1f = get_1_rifl(c, opcode); set_rifl(c, opcode, round_to_int(c, t1f)); break;
		case 0xc: ic -= 441; t1f = get_1_rifl(c, opcode); set_rifl(c, opcode, sin(t1f)); break;
		case 0xd: ic -= 441; t1f = get_1_rifl(c, opcode); set_rifl(c, opcode, cos(t1f)); break;
		case 0xe: ic -= 323; t1f = get_1_rifl(c, opcode); set_rifl(c, opcode, tan(t1f)); break;
		default: FAT((opcode >> 7) & 0xf);
		}
		break;

	case 0x6c:
		switch ((opcode >> 7) & 0xf) {
		case 0x0: ic -= 33; t1f = get_1_rif(c, opcode); set_ri(c, opcode, u32(f2s32(round_to_int(c, t1f)))); break;
		case 0x1: ic -= 35; t1f = get_1_rif(c, opcode); set_ri64(c, opcode, u64(f2s64(round_to_int(c, t1f)))); break;
		case 0x2: ic -= 43; t1f = get_1_rif(c, opcode); set_ri(c, opcode, u32(f2s32(t1f))); break;
		case 0x3: ic -= 44; t1f = get_1_rif(c, opcode); set_ri64(c, opcode, u64(f2s64(t1f))); break;
		case 0x9: ic -= 5; t1f = get_1_rif(c, opcode); set_rif(c, opcode, t1f); break;
		default: FAT((opcode >> 7) & 0xf);
		}
		break;
	case 0x6d:   // movrl
		if (((opcode >> 7) & 0xf) != 0x9) FAT((opcode >> 7) & 0xf);
		ic -= 6; t1f = get_1_rifl(c, opcode); set_rifl(c, opcode, t1f);
		break;
	case 0x6e:   // movre, cpysre: rare, MAME's own
		c.execute_op(opcode);
		break;

	case 0x70:
		switch ((opcode >> 7) & 0xf) {
		case 0x1: ic -= 18; t1 = get_1_ri(c, opcode); t2 = get_2_ri(c, opcode); set_ri(c, opcode, t2 * t1); break;    // mulo
		case 0x8: ic -= 37; t1 = get_1_ri(c, opcode); t2 = get_2_ri(c, opcode); set_ri(c, opcode, t2 % t1); break;    // remo
		case 0xb: ic -= 37; t1 = get_1_ri(c, opcode); t2 = get_2_ri(c, opcode); set_ri(c, opcode, t1 == 0 ? 0 : t2 / t1); break;   // divo
		default: FAT((opcode >> 7) & 0xf);
		}
		break;
	case 0x74:
		switch ((opcode >> 7) & 0xf) {
		case 0x1: ic -= 18; t1 = get_1_ri(c, opcode); t2 = get_2_ri(c, opcode); set_ri(c, opcode, t2 * t1); break;   // muli (the same bits as s32 * s32)
		case 0x8: ic -= 37; t1 = get_1_ri(c, opcode); t2 = get_2_ri(c, opcode); set_ri(c, opcode, u32(s32(t2) % s32(t1))); break;   // remi
		case 0x9: {   // modi
			ic -= 37;
			const s32 src1 = s32(get_1_ri(c, opcode)), src2 = s32(get_2_ri(c, opcode));
			s32 dst = src2 - ((src2 / src1) * src1);
			if (((src1 ^ src2) < 0) && (dst != 0)) dst += src1;
			set_ri(c, opcode, u32(dst));
			break;
		}
		case 0xb: ic -= 37; t1 = get_1_ri(c, opcode); t2 = get_2_ri(c, opcode); set_ri(c, opcode, u32(s32(t2) / s32(t1))); break;   // divi
		default: FAT((opcode >> 7) & 0xf);
		}
		break;
	case 0x78:
		switch ((opcode >> 7) & 0xf) {
		case 0xb: ic -= 35; t1f = get_1_rif(c, opcode); t2f = get_2_rif(c, opcode); set_rif(c, opcode, t2f / t1f); break;
		case 0xc: ic -= 18; t1f = get_1_rif(c, opcode); t2f = get_2_rif(c, opcode); set_rif(c, opcode, t2f * t1f); break;
		case 0xd: ic -= 10; t1f = get_1_rif(c, opcode); t2f = get_2_rif(c, opcode); set_rif(c, opcode, t2f - t1f); break;
		case 0xf: ic -= 10; t1f = get_1_rif(c, opcode); t2f = get_2_rif(c, opcode); set_rif(c, opcode, t2f + t1f); break;
		default: FAT((opcode >> 7) & 0xf);
		}
		break;
	case 0x79:
		switch ((opcode >> 7) & 0xf) {
		case 0xb: ic -= 77; t1f = get_1_rifl(c, opcode); t2f = get_2_rifl(c, opcode); set_rifl(c, opcode, t2f / t1f); break;
		case 0xc: ic -= 36; t1f = get_1_rifl(c, opcode); t2f = get_2_rifl(c, opcode); set_rifl(c, opcode, t2f * t1f); break;
		case 0xd: ic -= 13; t1f = get_1_rifl(c, opcode); t2f = get_2_rifl(c, opcode); set_rifl(c, opcode, t2f - t1f); break;
		case 0xf: ic -= 13; t1f = get_1_rifl(c, opcode); t2f = get_2_rifl(c, opcode); set_rifl(c, opcode, t2f + t1f); break;
		default: FAT((opcode >> 7) & 0xf);
		}
		break;

	case 0x80: { ic -= 4; const u8 v = rd8(c, EA()); if (!c.m_stalled) r[(opcode >> 19) & 0x1f] = v; break; }          // ldob
	case 0x82: ic -= 2; wr8(c, EA(), u8(r[(opcode >> 19) & 0x1f])); break;                                           // stob
	case 0x84: ic -= 3; c.m_IP = EA(); break;                                                                                         // bx
	case 0x85: ic -= 5; t1 = EA(); r[(opcode >> 19) & 0x1f] = c.m_IP; c.m_IP = t1; break;                                            // balx
	case 0x86: t1 = EA(); do_call(c, t1, 0, r[I960_SP]); break;                                                                       // callx
	case 0x88: { ic -= 4; const u16 v = rd16(c, EA()); if (!c.m_stalled) r[(opcode >> 19) & 0x1f] = v; break; }                    // ldos
	case 0x8a: ic -= 2; wr16(c, EA(), u16(r[(opcode >> 19) & 0x1f])); break;                                                         // stos
	case 0x8c: ic--; r[(opcode >> 19) & 0x1f] = EA(); break;                                                                          // lda
	case 0x90: { ic -= 4; const u32 v = rd32(c, EA()); if (!c.m_stalled) r[(opcode >> 19) & 0x1f] = v; break; }                    // ld
	case 0x92: ic -= 2; wr32(c, EA(), r[(opcode >> 19) & 0x1f]); break;                                                              // st
	case 0x98: ic -= 5; ld_multi(c, EA(), (opcode >> 19) & 0x1e, 2); break;                                                          // ldl
	case 0x9a: ic -= 3; st_multi(c, EA(), (opcode >> 19) & 0x1e, 2); break;                                                          // stl
	case 0xa0: ic -= 6; ld_multi(c, EA(), (opcode >> 19) & 0x1c, 3); break;                                                          // ldt
	case 0xa2: ic -= 4; st_multi(c, EA(), (opcode >> 19) & 0x1c, 3); break;                                                          // stt
	case 0xb0: ic -= 7; ld_multi(c, EA(), (opcode >> 19) & 0x1c, 4); break;                                                          // ldq
	case 0xb2: ic -= 5; st_multi(c, EA(), (opcode >> 19) & 0x1c, 4); break;                                                          // stq
	case 0xc0: { ic -= 4; const s8 v = s8(rd8(c, EA())); if (!c.m_stalled) r[(opcode >> 19) & 0x1f] = u32(s32(v)); break; }   // ldib
	case 0xc2: ic -= 2; wr8(c, EA(), u8(r[(opcode >> 19) & 0x1f])); break;                                           // stib
	case 0xc8: { ic -= 4; const s16 v = s16(rd16(c, EA())); if (!c.m_stalled) r[(opcode >> 19) & 0x1f] = u32(s32(v)); break; }      // ldis
	case 0xca: ic -= 2; wr16(c, EA(), u16(r[(opcode >> 19) & 0x1f])); break;                                                         // stis

	default:
		fatalerror("I960: %x: Unhandled %02x\n", c.m_PIP, opcode >> 24);
	}
#undef FAT
#undef EA
}
