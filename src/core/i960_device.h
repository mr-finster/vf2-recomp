// MAME's Intel 80960KB core (third_party/mame/i960; its members made protected, nothing else
// changed) on the shim's plain bus, with named access to the registers it publishes through
// state_add.
//
// The recompiler (src/rec) runs the game's code in the interpreter's place when one is attached
// (set_rec): recompiled functions for the code it knows, MAME's interpreter for the rest, the
// same state and the same cycle counts either way. set_rec(nullptr) puts the interpreter back.
#pragma once
#include "emu.h"
#include "../../third_party/mame/i960/i960.h"
#include <functional>
#include <vector>

struct i960_rec;
namespace i960rec { struct runtime; }

class i960_device : public i80960kb_device
{
	friend struct i960_rec;
public:
	explicit i960_device(mame_bus32 &bus) : i80960kb_device(s_config, "maincpu", nullptr, 25000000)
	{
		set_bus(&bus);
		start();
		for (int i = 0; i < 38; i++) m_reg[i] = state_find(i);
	}

	// I960_R0..I960_G15 (0-31), I960_SAT, I960_PRCB, I960_PC, I960_AC, I960_IP, I960_PIP
	u32 reg(int i) const { return u32(m_reg[i]->value()); }
	void set_reg(int i, u32 v) { m_reg[i]->set_value(v); }
	u32 ip() const { return reg(I960_IP); }       // the next instruction
	u32 pip() const { return reg(I960_PIP); }     // the instruction being executed
	u32 pc_reg() const { return reg(I960_PC); }   // process controls (priority in bits 16-20)

	// The recompiled code (src/rec/i960_rec.h), or null: the interpreter
	void set_rec(i960rec::runtime *r) { m_rec = r; }
	i960rec::runtime *rec() const { return m_rec; }

	// Everything the core keeps, for comparing two of them (the lockstep check): the registers,
	// the register cache, the floating-point registers, the interrupt and stall state, the
	// cycle count.
	void state_words(std::vector<u32> &out) const;
	// Save states (src/machine/state_io.h): the same fields as state_words, run both ways. Between the
	// scheduler's slices only (the recompiled code keeps nothing of its own between them).
	template <class IO> void state(IO &io)
	{
		io.pod(m_r); io.pod(m_rcache); io.pod(m_rcache_frame_addr); io.pod(m_rcache_pos);
		for (double &d : m_fp) { u64 b = d2u(d); io.pod(b); d = u2d(b); }
		io.pod(m_SAT); io.pod(m_PRCB); io.pod(m_PC); io.pod(m_AC); io.pod(m_IP); io.pod(m_PIP); io.pod(m_ICR);
		io.pod(m_immediate_irq); io.pod(m_immediate_vector); io.pod(m_immediate_pri);
		io.pod(m_irq_line_state);
		io.pod(m_stalled);
		io.pod(m_stall_state.t1); io.pod(m_stall_state.t2); io.pod(m_stall_state.index); io.pod(m_stall_state.size);
		io.pod(m_stall_state.burst_mode); io.pod(m_stall_state.iswriteop);
		io.pod(m_icount);
	}
	// A cold start's: everything state() lists to what a board just switched on has (MAME's reset leaves the
	// floating-point registers, the stall state and more as they were). reset() after it.
	void power_on()
	{
		memset(m_r, 0, sizeof m_r); memset(m_rcache, 0, sizeof m_rcache); memset(m_rcache_frame_addr, 0, sizeof m_rcache_frame_addr);
		m_rcache_pos = 0;
		for (double &d : m_fp) d = 0;
		m_SAT = m_PRCB = m_PC = m_AC = m_IP = m_PIP = m_ICR = 0;
		m_immediate_irq = m_immediate_vector = m_immediate_pri = 0;
		for (s8 &l : m_irq_line_state) l = CLEAR_LINE;
		m_stalled = false;
		m_stall_state = {};
		m_icount = 0;
	}
	// After each slice the scheduler gives it (testing: the lockstep check's finer comparison)
	std::function<void()> on_slice;
	// Time spent running it (measuring: counted while time_runs is set), in ns
	bool time_runs = false;
	uint64_t run_ns = 0, runs = 0;

protected:
	void execute_run() override;

private:
	static inline machine_config s_config;
	const device_state_entry *m_reg[38];
	i960rec::runtime *m_rec = nullptr;
};
