#include "i960_device.h"
#include "../rec/i960_rec.h"
#include <chrono>

void i960_device::execute_run()
{
	if (m_stopped) { m_icount = 0; return; }
	std::chrono::steady_clock::time_point t0;
	if (time_runs) { t0 = std::chrono::steady_clock::now(); runs++; }
	jmp_buf trap;
	jmp_buf *const was = g_mame_trap;
	g_mame_trap = &trap;
	if (setjmp(trap)) {   // (an instruction it can't run (MAME's fatal error): stopped till a reset or a load, its slice spent)
		g_mame_trap = was;
		m_stopped = true;
		m_icount = 0;
		return;
	}
	run_trapped();
	g_mame_trap = was;
	if (time_runs) run_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
	if (on_slice) on_slice();
}

void i960_device::state_words(std::vector<u32> &out) const
{
	out.clear();
	auto put = [&](u32 v) { out.push_back(v); };
	auto put64 = [&](double d) { const u64 b = d2u(d); put(u32(b)); put(u32(b >> 32)); };
	for (u32 v : m_r) put(v);
	for (auto &f : m_rcache) for (u32 v : f) put(v);
	for (u32 v : m_rcache_frame_addr) put(v);
	put(u32(m_rcache_pos));
	for (double d : m_fp) put64(d);
	put(m_SAT); put(m_PRCB); put(m_PC); put(m_AC); put(m_IP); put(m_PIP); put(m_ICR);
	put(u32(m_immediate_irq)); put(u32(m_immediate_vector)); put(u32(m_immediate_pri));
	for (s8 v : m_irq_line_state) put(u32(v));
	put(m_stalled);
	put(m_stall_state.t1); put(m_stall_state.t2); put(u32(m_stall_state.index)); put(u32(m_stall_state.size));
	put(m_stall_state.burst_mode); put(m_stall_state.iswriteop);
	put(u32(m_icount));
}

void i960_device::run_trapped()
{
	if (m_rec) m_rec->execute(*this);
	else i960_cpu_device::execute_run();
}
