// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
// Fujitsu MB86233 (TGP): MAME's core without the device framework (BSD-3-Clause,
// copyright-holders: Olivier Galibert).
#pragma once
#include "mame_shim.h"

// The TGP's four address spaces, word-addressed (one 32-bit word per address).
struct mb86233_bus
{
	virtual ~mb86233_bus() = default;
	virtual u32 prog_r(u32 a) = 0;
	virtual u32 data_r(u32 a) = 0;
	virtual void data_w(u32 a, u32 v) = 0;
	virtual u32 io_r(u32 a) = 0;
	virtual void io_w(u32 a, u32 v) = 0;
	virtual u32 rf_r(u32 a) { return 0; }
	virtual void rf_w(u32 a, u32 v) {}
};

class mb86233_device
{
public:
	explicit mb86233_device(mb86233_bus &bus) : m_bus(&bus) {}
	enum regs_ids {
		REG_A, REG_B, REG_D, REG_P,
		REG_R, REG_C0, REG_C1,
		REG_B0, REG_B1, REG_X0, REG_X1, REG_I0, REG_I1,
		REG_SFT, REG_VSM, REG_MASK, REG_M,
		REG_PCS0, REG_PCS1, REG_PCS2, REG_PCS3,
		REG_SP
	};

	enum st_flags {
		F_ZRC  = 0x00000001,
		F_ZRD  = 0x00000002,
		F_SGC  = 0x00000004,
		F_SGD  = 0x00000008,
		F_CPC  = 0x00000010,
		F_CPD  = 0x00000020,
		F_OVC  = 0x00000040,
		F_OVD  = 0x00000080,
		F_UNC  = 0x00000100,
		F_UND  = 0x00000200,
		F_DVZC = 0x00000400,
		F_DVZD = 0x00000800,
		F_CA   = 0x00001000,
		F_CPP  = 0x00002000,
		F_OVM  = 0x00004000,
		F_UNM  = 0x00008000,
		F_SIF0 = 0x00010000,
		F_SIF1 = 0x00020000,
		F_SOF0 = 0x00040000,

		F_PIF  = 0x00100000,
		F_POF  = 0x00200000,
		F_PAIF = 0x00400000,
		F_PAOF = 0x00800000,
		F_F0S  = 0x01000000,
		F_F1S  = 0x02000000,
		F_IT   = 0x04000000,
		F_ZX0  = 0x08000000,
		F_ZX1  = 0x10000000,
		F_ZX2  = 0x20000000,
		F_ZC0  = 0x40000000,
		F_ZC1  = 0x80000000
	};


	void reset();
	// MAME's execute_run with icount = cycles; returns the cycles used (one per instruction).
	int run(int cycles) { m_icount = m_cycles_running = cycles; execute_run(); return m_cycles_running - m_icount; }
	int cycles_used() const { return m_cycles_running - m_icount; }
	// MAME's abort_timeslice: the run ends after this instruction, the rest isn't spent.
	void abort_timeslice() { const int left = m_icount; m_cycles_running -= left; m_icount -= left; }
	// An input FIFO was empty: the instruction is retried (MAME's stall()).
	void stall() { m_stall = true; }
	u16 pc() const { return m_pc; }
	std::function<void(u16)> on_instruction;   // before each instruction (the debugger's hook)

	// Save states (src/machine/state_io.h): every register, run both ways; power_on: a cold start's, reset()
	// after it (reset() leaves the GPIO lines as they were)
	template <class IO> void state(IO &io)
	{
		io.pod(m_cycles_running); io.pod(m_icount);
		io.pod(m_st); io.pod(m_a); io.pod(m_b); io.pod(m_d); io.pod(m_p);
		io.pod(m_alu_stmask); io.pod(m_alu_stset); io.pod(m_alu_r1); io.pod(m_alu_r2);
		io.pod(m_ppc); io.pod(m_pc); io.pod(m_sp); io.pod(m_b0); io.pod(m_b1); io.pod(m_x0); io.pod(m_x1); io.pod(m_i0); io.pod(m_i1);
		io.pod(m_vsmr); io.pod(m_pcs); io.pod(m_mask); io.pod(m_m);
		io.pod(m_r); io.pod(m_rpc); io.pod(m_c0); io.pod(m_c1); io.pod(m_sft); io.pod(m_vsm);
		io.pod(m_gpio0); io.pod(m_gpio1); io.pod(m_gpio2); io.pod(m_gpio3);
		io.pod(m_stall);
	}
	void power_on() { m_cycles_running = m_icount = 0; m_gpio0 = m_gpio1 = m_gpio2 = m_gpio3 = false; reset(); }

	void gpio0_w(int state) { m_gpio0 = state; }
	void gpio1_w(int state) { m_gpio1 = state; }
	void gpio2_w(int state) { m_gpio2 = state; }
	void gpio3_w(int state) { m_gpio3 = state; }

private:
	mb86233_bus *m_bus;
	int m_cycles_running = 0;
	void execute_run();
	int m_icount = 0;

	u32 m_st, m_a, m_b, m_d, m_p;
	u32 m_alu_stmask, m_alu_stset, m_alu_r1, m_alu_r2;
	u16 m_ppc, m_pc, m_sp, m_b0, m_b1, m_x0, m_x1, m_i0, m_i1, m_vsmr, m_pcs[4], m_mask, m_m;
	u8 m_r, m_rpc, m_c0, m_c1, m_sft, m_vsm;
	bool m_gpio0 = false, m_gpio1 = false, m_gpio2 = false, m_gpio3 = false;

	bool m_stall;

	static u32 set_exp(u32 val, u32 exp);
	static u32 set_mant(u32 val, u32 mant);
	static u32 get_exp(u32 val);
	static u32 get_mant(u32 val);

	void alu_update_st();
	void alu_pre(u32 alu);
	void alu_post_1(u32 alu);
	void alu_post_2(u32 alu);
	u16 ea_pre_0(u32 r);
	void ea_post_0(u32 r);
	u16 ea_pre_1(u32 r);
	void ea_post_1(u32 r);
	void pcs_push();
	void pcs_pop();
	inline void stset_set_sz_int(u32 val);
	inline void stset_set_sz_fp(u32 val);

	u32 read_reg(u32 r);
	void write_reg(u32 r, u32 v);
	void write_mem_internal_1(u32 r, u32 v, bool bank);
	void write_mem_external_1(u32 r, u32 v);
	void write_mem_io_1(u32 r, u32 v);
};
