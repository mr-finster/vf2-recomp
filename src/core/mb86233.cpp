// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
// Fujitsu MB86233 (TGP), the Model 1 geometry coprocessor. MAME's core
// (src/devices/cpu/mb86233/mb86233.cpp, BSD-3-Clause, copyright-holders: Olivier Galibert),
// without the device framework: memory goes through mb86233_bus.
#include "mb86233.h"
#include <cstdarg>

static void mb_log(const char *fmt, ...)
{
	if (!getenv("MB86233_LOG")) return;
	va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

void mb86233_device::reset()
{
	m_pc = 0;
	m_ppc = 0;
	m_st = F_ZRC|F_ZRD|F_ZX0|F_ZX1|F_ZX2|F_ZC0|F_ZC1;
	m_sp = 0;

	m_a = 0;
	m_b = 0;
	m_d = 0;
	m_p = 0;
	m_r = 1;
	m_rpc = 1;
	m_c0 = 1;
	m_c1 = 1;
	m_b0 = 0;
	m_b1 = 0;
	m_x0 = 0;
	m_x1 = 0;
	m_i0 = 0;
	m_i1 = 0;
	m_sft = 0;
	m_vsm = 0;
	m_vsmr = 7;
	m_mask = 0;
	m_m = 1;

	m_alu_stmask = 0;
	m_alu_stset = 0;
	m_alu_r1 = 0;
	m_alu_r2 = 0;

	std::fill(std::begin(m_pcs), std::end(m_pcs), 0);

	m_stall = false;
}

u32 mb86233_device::set_exp(u32 val, u32 exp)
{
	return (val & 0x807fffff) | ((exp & 0xff) << 23);
}

u32 mb86233_device::set_mant(u32 val, u32 mant)
{
	return (val & 0x07f800000) | ((mant & 0x00800000) << 8) | (mant & 0x007fffff);
}

u32 mb86233_device::get_exp(u32 val)
{
	return (val >> 23) & 0xff;
}

u32 mb86233_device::get_mant(u32 val)
{
	return val & 0x80000000 ? val | 0x7f800000 : val & 0x807fffff;
}

void mb86233_device::pcs_push()
{
	for(unsigned int i=3; i; i--)
		m_pcs[i] = m_pcs[i-1];
	m_pcs[0] = m_pc;
}

void mb86233_device::pcs_pop()
{
	m_pc = m_pcs[0];
	for(unsigned int i=0; i != 3; i++)
		m_pcs[i] = m_pcs[i+1];
}

void mb86233_device::stset_set_sz_int(u32 val)
{
	m_alu_stset = val ? (val & 0x80000000 ? F_SGD : 0) : F_ZRD;
}

void mb86233_device::stset_set_sz_fp(u32 val)
{
	m_alu_stset = (val & 0x7fffffff) ? (val & 0x80000000 ? F_SGD : 0) : F_ZRD;
}

void mb86233_device::alu_pre(u32 alu)
{
	switch(alu) {
	case 0x00: break; // no alu

	case 0x01: {
		// andd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = m_d & m_a;
		stset_set_sz_int(m_alu_r1);
		break;
	}

	case 0x02: {
		// orad
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = m_d | m_a;
		stset_set_sz_int(m_alu_r1);
		break;
	}

	case 0x03: {
		// eord
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = m_d ^ m_a;
		stset_set_sz_int(m_alu_r1);
		break;
	}

	case 0x04: {
		// notd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = ~m_d;
		stset_set_sz_int(m_alu_r1);
		break;
	}

	case 0x05: {
		// fcpd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		u32 r = f2u(u2f(m_d) - u2f(m_a));
		stset_set_sz_fp(r);
		break;
	}

	case 0x06: {
		// fadd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = f2u(u2f(m_d) + u2f(m_a));
		stset_set_sz_fp(m_alu_r1);
		break;
	}

	case 0x07: {
		// fsbd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = f2u(u2f(m_d) - u2f(m_a));
		stset_set_sz_fp(m_alu_r1);
		break;
	}

	case 0x08: {
		// fml
		m_alu_stmask = 0;
		m_alu_r1 = f2u(u2f(m_a) * u2f(m_b));
		m_alu_stset = 0;
		break;
	}

	case 0x09: {
		// fmsd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = f2u(u2f(m_d) + u2f(m_p));
		m_alu_r2 = f2u(u2f(m_a) * u2f(m_b));
		stset_set_sz_fp(m_alu_r1);
		break;
	}

	case 0x0a: {
		// fmrd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = f2u(u2f(m_d) - u2f(m_p));
		m_alu_r2 = f2u(u2f(m_a) * u2f(m_b));
		stset_set_sz_fp(m_alu_r1);
		break;
	}

	case 0x0b: {
		// fabd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = m_d & 0x7fffffff;
		stset_set_sz_fp(m_alu_r1);
		break;
	}

	case 0x0c: {
		// fsmd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = f2u(u2f(m_d) + u2f(m_p));
		stset_set_sz_fp(m_alu_r1);
		break;
	}

	case 0x0d: {
		// fspd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = m_p;
		m_alu_r2 = f2u(u2f(m_a) * u2f(m_b));
		stset_set_sz_fp(m_alu_r1);
		break;
	}

	case 0x0e: {
		// cxfd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = f2u(s32(m_d));
		stset_set_sz_int(m_alu_r1);
		break;
	}

	case 0x0f: {
		// cfxd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		switch((m_m >> 1) & 3) {
		case 0: m_alu_r1 = f2s32(roundf(u2f(m_d))); break;   // (as x86-64 on every machine: mame_shim.h)
		case 1: m_alu_r1 = f2s32(ceilf(u2f(m_d))); break;
		case 2: m_alu_r1 = f2s32(floorf(u2f(m_d))); break;
		case 3: m_alu_r1 = f2s32(u2f(m_d)); break;
		}
		stset_set_sz_int(m_alu_r1);
		break;
	}

	case 0x10: {
		// fdvd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = f2u(u2f(m_d) / u2f(m_a));
		stset_set_sz_fp(m_alu_r1);
		break;
	}

	case 0x11: {
		// fned
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = m_d ? m_d ^ 0x80000000 : 0;
		stset_set_sz_fp(m_alu_r1);
		break;
	}

	case 0x13: {
		// d = b + a
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = f2u(u2f(m_b) + u2f(m_a));
		stset_set_sz_fp(m_alu_r1);
		break;
	}

	case 0x14: {
		// d = b - a
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = f2u(u2f(m_b) - u2f(m_a));
		stset_set_sz_fp(m_alu_r1);
		break;
	}

	case 0x16: {
		// lsrd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = m_d >> m_sft;
		stset_set_sz_int(m_alu_r1);
		break;
	}

	case 0x17: {
		// lsld
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = m_d << m_sft;
		stset_set_sz_int(m_alu_r1);
		break;
	}

	case 0x18: {
		// asrd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = s32(m_d) >> m_sft;
		stset_set_sz_int(m_alu_r1);
		break;
	}

	case 0x19: {
		// asld
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = s32(m_d) << m_sft;
		stset_set_sz_int(m_alu_r1);
		break;
	}

	case 0x1a: {
		// addd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = m_d + m_a;
		stset_set_sz_int(m_alu_r1);
		break;
	}

	case 0x1b: {
		// subd
		m_alu_stmask = F_ZRD|F_SGD|F_CPD|F_OVD|F_DVZD;
		m_alu_r1 = m_d - m_a;
		stset_set_sz_int(m_alu_r1);
		break;
	}

	default:
		mb_log("unhandled alu pre %02x\n", alu);
		break;
	}
}

void mb86233_device::alu_update_st()
{
	m_st = (m_st & ~m_alu_stmask) | m_alu_stset;
}

void mb86233_device::alu_post_1(u32 alu)
{
	// integer alu post ops
	switch(alu) {
	case 0x01: case 0x02: case 0x03: case 0x04:
	case 0x0e: case 0x0f: case 0x16: case 0x17:
	case 0x18: case 0x19: case 0x1a: case 0x1b:
		// d update
		m_d = m_alu_r1;
		alu_update_st();
		break;

	default:
		break;
	}
}

void mb86233_device::alu_post_2(u32 alu)
{
	// floating point alu post ops
	// assume each one takes 2 cycles
	switch (alu) {
	case 0x05:
		// flags only
		alu_update_st();
		m_icount--;
		break;

	case 0x06: case 0x07: case 0x0b: case 0x0c:
	case 0x10: case 0x11: case 0x13: case 0x14:
		// d update
		m_d = m_alu_r1;
		alu_update_st();
		m_icount--;
		break;

	case 0x08:
		// p update
		m_p = m_alu_r1;
		m_icount--;
		break;

	case 0x09: case 0x0a: case 0x0d:
		// d, p update
		m_d = m_alu_r1;
		m_p = m_alu_r2;
		alu_update_st();
		m_icount--;
		break;

	default:
		break;
	}
}

u16 mb86233_device::ea_pre_0(u32 r)
{
	switch(r & 0x180) {
	case 0x000: return r & 0x7f;
	case 0x080: case 0x100: return (r & 0x7f) + m_b0 + m_x0;
	case 0x180: {
		switch(r & 0x60) {
		case 0x00: return m_b0 + m_x0;
		case 0x20: return m_x0;
		case 0x40: return m_b0 + (m_x0 & m_vsmr);
		case 0x60: return m_x0 & m_vsmr;
		}
	}
	}
	return 0;
}

void mb86233_device::ea_post_0(u32 r)
{
	if(!(r & 0x100))
		return;
	if(!(r & 0x080))
		m_x0 += m_i0;
	else
		m_x0 += util::sext(r, 5);
}

u16 mb86233_device::ea_pre_1(u32 r)
{
	switch(r & 0x180) {
	case 0x000: return r & 0x7f;
	case 0x080: case 0x100: return (r & 0x7f) + m_b1 + m_x1;
	case 0x180: {
		switch(r & 0x60) {
		case 0x00: return m_b1 + m_x1;
		case 0x20: return m_x1;
		case 0x40: return m_b1 + (m_x1 & m_vsmr);
		case 0x60: return m_x1 & m_vsmr;
		}
	}
	}
	return 0;
}

void mb86233_device::ea_post_1(u32 r)
{
	if(!(r & 0x100))
		return;
	if(!(r & 0x080))
		m_x1 += m_i1;
	else
		m_x1 += util::sext(r, 5);
}

u32 mb86233_device::read_reg(u32 r)
{
	r &= 0x3f;
	if(r >= 0x20 && r < 0x30)
		return m_bus->rf_r(r & 0x1f);
	switch(r) {
	case 0x00: return m_b0;
	case 0x01: return m_b1;
	case 0x02: return m_x0;
	case 0x03: return m_x1;

	case 0x0c: return m_c0;
	case 0x0d: return m_c1;

	case 0x10: return m_a;
	case 0x11: return get_exp(m_a);
	case 0x12: return get_mant(m_a);
	case 0x13: return m_b;
	case 0x14: return get_exp(m_b);
	case 0x15: return get_mant(m_b);
	case 0x19: return m_d;
		/* c */
	case 0x1a: return get_exp(m_d);
	case 0x1b: return get_mant(m_d);
	case 0x1c: return m_p;
	case 0x1d: return get_exp(m_p);
	case 0x1e: return get_mant(m_p);
	case 0x1f: return m_sft;

	case 0x34: return m_rpc;

	default:
		mb_log("unimplemented read_reg(%02x) (%x)\n", r, m_ppc);
		return 0;
	}
}

void mb86233_device::write_reg(u32 r, u32 v)
{
	r &= 0x3f;
	if(r >= 0x20 && r < 0x30) {
		m_bus->rf_w(r & 0x1f, v);
		return;
	}
	switch(r) {
	case 0x00: m_b0 = v; break;
	case 0x01: m_b1 = v; break;
	case 0x02: m_x0 = v; break;
	case 0x03: m_x1 = v; break;

	case 0x05: m_i0 = v; break;
	case 0x06: m_i1 = v; break;

	case 0x08: m_sp = v; break;

	case 0x0a: m_vsm = v & 7; m_vsmr = (8 << m_vsm) - 1; break;

	case 0x0c:
		m_c0 = v;
		if(m_c0 == 1)
			m_st |= F_ZC0;
		else
			m_st &= ~F_ZC0;
		break;

	case 0x0d:
		m_c1 = v;
		if(m_c1 == 1)
			m_st |= F_ZC1;
		else
			m_st &= ~F_ZC1;
		break;

	case 0x0f: break;

	case 0x10: m_a = v; break;
	case 0x11: m_a = set_exp(m_a, v); break;
	case 0x12: m_a = set_mant(m_a, v); break;
	case 0x13: m_b = v; break;
	case 0x14: m_b = set_exp(m_b, v); break;
	case 0x15: m_b = set_mant(m_b, v); break;
		/* c */
	case 0x19: m_d = v; break;
	case 0x1a: m_d = set_exp(m_d, v); break;
	case 0x1b: m_d = set_mant(m_d, v); break;
	case 0x1c: m_p = v; break;
	case 0x1d: m_p = set_exp(m_p, v); break;
	case 0x1e: m_p = set_mant(m_p, v); break;
	case 0x1f: m_sft = v; break;

	case 0x34: m_rpc = v; break;
	case 0x3c: m_mask = v; break;

	default:
		mb_log("unimplemented write_reg(%02x, %08x) (%x)\n", r, v, m_ppc);
		break;
	}
}

void mb86233_device::write_mem_internal_1(u32 r, u32 v, bool bank)
{
	u16 ea = ea_pre_1(r);
	if(bank)
		ea += 0x200;
	m_bus->data_w(ea, v);
	ea_post_1(r);
}

void mb86233_device::write_mem_io_1(u32 r, u32 v)
{
	u16 ea = ea_pre_1(r);
	m_bus->io_w(ea, v);
	ea_post_1(r);
}

void mb86233_device::execute_run()
{
	while(m_icount > 0) {
		m_ppc = m_pc;
		if (on_instruction) on_instruction(m_pc);

		u32 opcode = m_bus->prog_r(m_pc++);

		switch((opcode >> 26) & 0x3f) {
		case 0x00: {
			// lab
			u32 r1 = opcode & 0x1ff;
			u32 r2 = (opcode >> 9) & 0x1ff;
			u32 alu = (opcode >> 21) & 0x1f;
			u32 op = (opcode >> 18) & 0x7;

			alu_pre(alu);

			switch(op) {
			case 0: case 1: {
				// lab mem, mem (e)

				u32 ea1 = ea_pre_0(r1);
				u32 v1 = m_bus->data_r(ea1);
				if(m_stall) goto do_stall;

				u32 ea2 = ea_pre_1(r2);
				u32 v2 = m_bus->io_r(ea2);
				if(m_stall) goto do_stall;

				ea_post_0(r1);
				ea_post_1(r2);

				m_a = v1;
				m_b = v2;
				break;
			}

			case 3: {
				// lab mem, mem + 0x200

				u32 ea1 = ea_pre_0(r1);
				u32 v1 = m_bus->data_r(ea1);
				if(m_stall) goto do_stall;

				u32 ea2 = ea_pre_1(r2) + 0x200;
				u32 v2 = m_bus->data_r(ea2);
				if(m_stall) goto do_stall;

				ea_post_0(r1);
				ea_post_1(r2);

				m_a = v1;
				m_b = v2;
				break;
			}

			case 4: {
				// lab mem + 0x200, mem

				u32 ea1 = ea_pre_0(r1) + 0x200;
				u32 v1 = m_bus->data_r(ea1);
				if(m_stall) goto do_stall;

				u32 ea2 = ea_pre_1(r2);
				u32 v2 = m_bus->data_r(ea2);
				if(m_stall) goto do_stall;

				ea_post_0(r1);
				ea_post_1(r2);

				m_a = v1;
				m_b = v2;
				break;
			}

			default:
				mb_log("unhandled lab subop %x\n", op);
				mb_log("%x\n", m_ppc);
				break;

			}

			alu_post_1(alu);
			alu_post_2(alu);
			break;
		}


		case 0x07: {
			// ld / mov
			u32 r1 = opcode & 0x1ff;
			u32 r2 = (opcode >> 9) & 0x1ff;
			u32 alu = (opcode >> 21) & 0x1f;
			u32 op = (opcode >> 18) & 0x7;

			alu_pre(alu);

			switch(op) {
			case 0: {
				// mov mem, mem (e)
				u32 ea = ea_pre_0(r1);
				u32 v = m_bus->data_r(ea);
				if(m_stall) goto do_stall;
				ea_post_0(r1);
				alu_post_1(alu);
				write_mem_io_1(r2, v);
				break;
			}

			case 1: {
				// mov mem, mem (e)
				u32 ea = ea_pre_0(r1);
				u32 v = m_bus->data_r(ea);
				if(m_stall) goto do_stall;
				ea_post_0(r1);
				alu_post_1(alu);
				write_mem_io_1(r2, v);
				break;
			}

			case 2: {
				// mov mem (e), mem
				u32 ea = ea_pre_0(r1);
				u32 v = m_bus->io_r(ea);
				if(m_stall) goto do_stall;
				ea_post_0(r1);
				alu_post_1(alu);
				write_mem_internal_1(r2, v, false);
				break;
			}

			case 3: {
				// mov mem, mem + 0x200
				u32 ea = ea_pre_0(r1);
				u32 v = m_bus->data_r(ea);
				if(m_stall) goto do_stall;
				ea_post_0(r1);
				alu_post_1(alu);
				write_mem_internal_1(r2, v, true);
				break;
			}

			case 4: {
				// mov mem + 0x200, mem
				u32 ea = ea_pre_0(r1) + 0x200;
				u32 v = m_bus->data_r(ea);
				if(m_stall) goto do_stall;
				ea_post_0(r1);
				alu_post_1(alu);
				write_mem_internal_1(r2, v, false);
				break;
			}

			case 5: {
				// mov mem (o), mem
				u32 ea = ea_pre_0(r1);
				u32 v = m_bus->prog_r(ea);
				if(m_stall) goto do_stall;
				ea_post_0(r1);
				alu_post_1(alu);
				write_mem_internal_1(r2, v, false);
				break;
			}

			case 7: {
				switch(r2 >> 6) {
				case 0: {
					// mov reg, mem
					u32 v = read_reg(r2);
					if(m_stall) goto do_stall;
					alu_post_1(alu);
					write_mem_internal_1(r1, v, false);
					break;
				}

				case 1: {
					// mov reg, mem (e)
					u32 v = read_reg(r2);
					if(m_stall) goto do_stall;
					alu_post_1(alu);
					write_mem_io_1(r1, v);
					break;
				}

				case 2: {
					// mov mem + 0x200, reg
					u32 ea = ea_pre_1(r1) + 0x200;
					u32 v = m_bus->data_r(ea);
					if(m_stall) goto do_stall;
					ea_post_1(r1);
					alu_post_1(alu);
					write_reg(r2, v);
					break;
				}

				case 3: {
					// mov mem, reg
					u32 ea = ea_pre_1(r1);
					u32 v = m_bus->data_r(ea);
					if(m_stall) goto do_stall;
					ea_post_1(r1);
					alu_post_1(alu);
					write_reg(r2, v);
					break;
				}

				case 4: {
					// mov mem (e), reg
					u32 ea = ea_pre_1(r1);
					u32 v = m_bus->io_r(ea);
					if(m_stall) goto do_stall;
					ea_post_1(r1);
					alu_post_1(alu);
					write_reg(r2, v);
					break;
				}

				case 5: {
					// mov mem (o), reg
					u32 ea = ea_pre_0(r1);
					u32 v = m_bus->prog_r(ea);
					if(m_stall) goto do_stall;
					ea_post_0(r1);
					alu_post_1(alu);
					write_reg(r2, v);
					break;
				}

				case 6: {
					// mov reg, reg
					u32 v = read_reg(r1);
					if(m_stall) goto do_stall;
					alu_post_1(alu);
					write_reg(r2, v);
					break;
				}

				default:
					alu_post_1(alu);
					mb_log("unhandled ld/mov subop 7/%x (%x)\n", r2 >> 6, m_ppc);
					break;
				}
				break;
			}

			default:
				alu_post_1(alu);
				mb_log("unhandled ld/mov subop %x (%x)\n", op, m_ppc);
				break;
			}

			// For floating point ops, registers are updated after transfer
			alu_post_2(alu);
			break;
		}

		case 0x0d: {
			// stm/clm
			u32 sub2 = (opcode >> 17) & 7;

			// Theorically has restricted alu too

			switch(sub2) {
			case 5:
				// stmh
				// bit 0 = floating point
				// bit 1-2 = rounding mode
				m_m = opcode;
				break;

			default:
				mb_log("unimplemented opcode 0d/%x (%x)\n", sub2, m_ppc);
				break;
			}
			break;
		}

		case 0x0e: {
			// lipl / lia / lib / lid
			switch((opcode >> 24) & 0x3) {
			case 0:
				m_p = (m_p & 0xffffff000000) | (opcode & 0xffffff);
				break;
			case 1:
				m_a = util::sext(opcode, 24);
				break;
			case 2:
				m_b = util::sext(opcode, 24);
				break;
			case 3:
				m_d = util::sext(opcode, 24);
				break;
			}
			break;
		}

		case 0x0f: {
			// rep/clr0/clr1/set
			u32 alu = (opcode >> 20) & 0x1f;
			u32 sub2 = (opcode >> 17) & 7;

			alu_pre(alu);

			switch(sub2) {
			case 0:
				// clr0
				if(opcode & 0x0004) m_a = 0;
				if(opcode & 0x0008) m_b = 0;
				if(opcode & 0x0010) m_d = 0;
				break;

			case 1:
				// clr1 - flags mapping unknown
				break;

			case 2: {
				// rep
				u8 r = opcode & 0x8000 ? read_reg(opcode) : opcode;
				if(m_stall) goto do_stall;
				m_r = r;
				goto rep_start;
			}

			case 3:
				// set - flags mapping unknown
				// 0800 = enable interrupt flag
				break;

			default:
				mb_log("unimplemented opcode 0f/%x (%x)\n", sub2, m_ppc);
				break;
			}

			alu_post_1(alu);
			break;
		}

		case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
		case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f: {
			// ldi
			write_reg(opcode >> 24, util::sext(opcode, 24));
			break;
		}

		case 0x2f: case 0x3f: {
			// Conditional branch of every kind
			u32 cond = ( opcode >> 20 ) & 0x1f;
			u32 subtype = ( opcode >> 17 ) & 7;
			u32 data = opcode & 0xffff;
			bool invert = opcode & 0x40000000;

			bool cond_passed = false;

			switch(cond) {
			case 0x00: // zrd - d zero
				cond_passed = m_st & F_ZRD;
				break;

			case 0x01: // ged - d >= 0
				cond_passed = !(m_st & F_SGD);
				break;

			case 0x02: // led - d <= 0
				cond_passed = m_st & (F_ZRD | F_SGD);
				break;

			case 0x0a: // gpio0
				cond_passed = m_gpio0;
				break;

			case 0x0b: // gpio1
				cond_passed = m_gpio1;
				break;

			case 0x0c: // gpio2
				cond_passed = m_gpio2;
				break;

			case 0x10: // zc0 - c0 == 1
				cond_passed = !(m_st & F_ZC0);
				break;

			case 0x11: // zc1 - c1 == 1
				cond_passed = !(m_st & F_ZC1);
				break;

			case 0x12: // gpio3
				cond_passed = m_gpio3;
				break;

			case 0x16: // alw - always
				cond_passed = true;
				break;

			default:
				mb_log("unimplemented condition %x (%x)\n", cond, m_ppc);
				break;
			}
			if(invert)
				cond_passed = !cond_passed;

			if(cond_passed) {
				switch(subtype) {
				case 0: // brif #adr
					m_pc = data;
					break;

				case 1: // brul
					if(opcode & 0x4000) {
						// brul reg
						u32 v = read_reg(opcode);
						if(m_stall) goto do_stall;
						m_pc = v;
					} else {
						// brul adr
						u32 ea = ea_pre_0(opcode);
						u32 v = m_bus->data_r(ea);
						if(m_stall) goto do_stall;
						ea_post_0(opcode);
						m_pc = v;
					}
					break;

				case 2: // bsif #adr
					pcs_push();
					m_pc = data;
					break;

				case 3: // bsul
					if(opcode & 0x4000) {
						// bsul reg
						u32 v = read_reg(opcode);
						if(m_stall) goto do_stall;
						pcs_push();
						m_pc = v;
					} else {
						// bsul adr
						u32 ea = ea_pre_0(opcode);
						u32 v = m_bus->data_r(ea);
						if(m_stall) goto do_stall;
						ea_post_0(opcode);
						pcs_push();
						m_pc = v;
					}
					break;

				case 5: // rtif #adr
					pcs_pop();
					break;

				case 6: { // ldif adr, rn
					u32 ea = ea_pre_0(opcode);
					u32 v = m_bus->data_r(ea);
					if(m_stall) goto do_stall;
					ea_post_0(opcode);
					write_reg(opcode >> 9, v);
					break;
				}

				default:
					mb_log("unimplemented branch subtype %x (%x)\n", subtype, m_ppc);
					break;
				}
			}

			if(subtype < 2)
				switch(cond) {
				case 0x10:
					if(m_c0 != 1) {
						m_c0 --;
						if(m_c0 == 1)
							m_st |= F_ZC0;
					}
					break;

				case 0x11:
					if(m_c1 != 1) {
						m_c1 --;
						if(m_c1 == 1)
							m_st |= F_ZC1;
					}
				break;
				}

			break;
		}

		default:
			mb_log("unimplemented opcode type %02x (%x)\n", (opcode >> 26) & 0x3f, m_ppc);
			break;
		}

		if(m_r != 1) {
			m_pc = m_ppc;
			m_r --;
		}

	rep_start:
		if(0) {
		do_stall:
			m_pc = m_ppc;
			m_stall = false;
		}
		m_icount--;
	}
}
