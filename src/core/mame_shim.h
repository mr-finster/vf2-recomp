// The few MAME framework names the ported CPU cores use, so MAME's files compile unchanged.
#pragma once
#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

using u8 = uint8_t;   using s8 = int8_t;
using u16 = uint16_t; using s16 = int16_t;
using u32 = uint32_t; using s32 = int32_t;
using u64 = uint64_t; using s64 = int64_t;
using offs_t = uint32_t;

enum { CLEAR_LINE = 0, ASSERT_LINE = 1, HOLD_LINE = 2 };

// A float to an integer as x86-64 converts it, on every machine: in range, towards zero; out of range or not a
// number, the most negative one (x86-64's "integer indefinite"). C++ leaves those undefined, and an Arm CPU saturates
// instead (and gives 0 for not a number): two machines in a netplay match (a Mac and a PC or a Steam Deck) ran
// different games from there. The i960's cvtri/cvtzri and the TGP's cfxd go through these.
inline int32_t f2s32(double v) { return v >= -2147483648.0 && v < 2147483648.0 ? int32_t(v) : INT32_MIN; }
inline int64_t f2s64(double v) { return v >= -9223372036854775808.0 && v < 9223372036854775808.0 ? int64_t(v) : INT64_MIN; }
template <typename T, typename U> constexpr T BIT(T x, U n) noexcept { return (x >> n) & T(1); }
inline int64_t mul_32x32(int32_t a, int32_t b) { return int64_t(a) * int64_t(b); }
inline uint64_t mulu_32x32(uint32_t a, uint32_t b) { return uint64_t(a) * uint64_t(b); }
namespace util {
template <typename T, typename U> constexpr std::make_signed_t<T> sext(T value, U width) noexcept
{ return std::make_signed_t<T>(value << (8 * sizeof(value) - width)) >> (8 * sizeof(value) - width); }
}
inline uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
inline float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
inline uint64_t d2u(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
inline double u2d(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }

// ---- printf-style formatting that, like MAME's, goes by each argument's real type ----
// (MAME's cores pass u32s to "%lx" and std::strings to "%s"; plain printf can't take those.)
namespace mame_fmt {
struct arg
{
	enum { INT, UINT, DBL, STR } kind;
	int64_t i = 0; uint64_t u = 0; double d = 0; const char *s = "";
	template <typename T> static arg make(const T &v)
	{
		arg a;
		if constexpr (std::is_same_v<std::decay_t<T>, std::string>) { a.kind = STR; a.s = v.c_str(); }
		else if constexpr (std::is_convertible_v<T, const char *>) { a.kind = STR; a.s = v; }
		else if constexpr (std::is_floating_point_v<T>) { a.kind = DBL; a.d = double(v); }
		else if constexpr (std::is_signed_v<T>) { a.kind = INT; a.i = int64_t(v); a.u = uint64_t(a.i); }
		else { a.kind = UINT; a.u = uint64_t(v); a.i = int64_t(a.u); }
		return a;
	}
};
std::string format(const char *fmt, const arg *args, size_t n);
template <typename... A> std::string fmt(const char *f, const A &...a)
{
	const arg args[sizeof...(A) + 1] = { arg::make(a)..., arg::make(0) };
	return format(f, args, sizeof...(A));
}
}
namespace util {
template <typename... A> std::string string_format(const char *f, const A &...a) { return mame_fmt::fmt(f, a...); }
template <typename... A> std::ostream &stream_format(std::ostream &s, const char *f, const A &...a) { return s << mame_fmt::fmt(f, a...); }
}
template <typename... A> std::string string_format(const char *f, const A &...a) { return mame_fmt::fmt(f, a...); }

// Logging: MAME_LOG=1 in the environment prints the cores' logerror() lines to stderr.
bool mame_log_enabled();
[[noreturn]] void mame_fatal(const std::string &msg);
template <typename... A> void logerror(const char *f, const A &...a)
{
	if (mame_log_enabled()) fputs(mame_fmt::fmt(f, a...).c_str(), stderr);
}
#define fatalerror(...) mame_fatal(mame_fmt::fmt(__VA_ARGS__))
#define osd_printf_debug(...) ((void)0)
