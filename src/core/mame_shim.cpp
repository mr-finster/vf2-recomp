// The shim's formatting and logging (see mame_shim.h).
#include "mame_shim.h"
#include <stdexcept>

namespace mame_fmt {

// printf conversions, with each argument formatted by its actual type: length modifiers
// (l, ll, h, z) are ignored, %s takes strings, integers go to %d/%x/%o/%c, doubles to %f/%e/%g.
std::string format(const char *f, const arg *args, size_t n)
{
	std::string out;
	size_t next = 0;
	while (*f) {
		if (*f != '%') { out += *f++; continue; }
		if (f[1] == '%') { out += '%'; f += 2; continue; }
		std::string spec = "%";
		f++;
		while (*f && strchr("-+ #0", *f)) spec += *f++;
		while (*f && (isdigit(uint8_t(*f)) || *f == '.' || *f == '*')) spec += *f++;
		while (*f && strchr("hlLqjzt", *f)) f++;
		char conv = *f ? *f++ : 'd';
		const arg a = next < n ? args[next++] : arg::make(0);
		char buf[256];
		switch (conv) {
		case 'd': case 'i':
			if (a.kind == arg::DBL) snprintf(buf, sizeof buf, (spec + "lld").c_str(), (long long)a.d);
			else snprintf(buf, sizeof buf, (spec + "lld").c_str(), (long long)a.i);
			break;
		case 'u': case 'x': case 'X': case 'o': {
			uint64_t v = a.kind == arg::DBL ? uint64_t(a.d) : a.u;
			// a negative int32 shown in hex is 32 bits wide, as printf would show it
			if (a.kind == arg::INT && a.i < 0 && a.i >= INT32_MIN) v = uint32_t(a.i);
			snprintf(buf, sizeof buf, (spec + "ll" + conv).c_str(), (unsigned long long)v);
			break;
		}
		case 'c': snprintf(buf, sizeof buf, (spec + "c").c_str(), int(a.i)); break;
		case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
			snprintf(buf, sizeof buf, (spec + conv).c_str(), a.kind == arg::DBL ? a.d : a.kind == arg::INT ? double(a.i) : double(a.u));
			break;
		case 's':
			if (a.kind == arg::STR) snprintf(buf, sizeof buf, (spec + "s").c_str(), a.s);
			else snprintf(buf, sizeof buf, "%lld", (long long)a.i);
			break;
		case 'p': snprintf(buf, sizeof buf, "%llx", (unsigned long long)a.u); break;
		default: buf[0] = conv; buf[1] = 0; break;
		}
		out += buf;
	}
	return out;
}

} // namespace mame_fmt

bool mame_log_enabled()
{
	static const bool on = getenv("MAME_LOG") != nullptr;
	return on;
}

thread_local jmp_buf *g_mame_trap = nullptr;

void mame_fatal(const std::string &msg)
{
	fprintf(stderr, "fatal: %s", msg.c_str());
	if (msg.empty() || msg.back() != '\n') fputc('\n', stderr);
	// (in the i960's run: the CPU stopped, not the process (i960_device::execute_run). Only code the game's ROM doesn't
	// run gets here: a state from elsewhere's, a watcher's snapshot from a hostile host)
	if (g_mame_trap) longjmp(*g_mame_trap, 1);
	throw std::runtime_error(msg);
}
