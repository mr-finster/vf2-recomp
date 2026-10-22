// The computer, for log.txt, and Windows' scheduling of the game's threads (sysinfo.h).
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi.h>
#endif
#include "sysinfo.h"
#include "../machine/parallel.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#if defined(__linux__)
#include <sys/utsname.h>
#endif

namespace sysinfo {

namespace {

#ifdef _WIN32
std::string utf8(const wchar_t *w)
{
	const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1) return "";
	std::string s(size_t(n - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
	return s;
}

// Each logical processor's efficiency class (by group * 64 + number): the highest are the
// performance cores. All one class: not a CPU of two kinds.
struct cpu_classes {
	std::vector<uint8_t> cls;
	uint8_t top = 0;
	bool hybrid = false;
	cpu_classes()
	{
		using get_t = BOOL(WINAPI *)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);
		auto get = reinterpret_cast<get_t>(reinterpret_cast<void *>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetSystemCpuSetInformation")));
		if (!get) return;
		ULONG len = 0;
		get(nullptr, 0, &len, GetCurrentProcess(), 0);
		if (!len) return;
		std::vector<uint8_t> buf(len);
		if (!get(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data()), len, &len, GetCurrentProcess(), 0)) return;
		uint8_t lo = 255;
		for (size_t at = 0; at < len;) {
			const auto *e = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION *>(buf.data() + at);
			if (!e->Size) break;
			if (e->Type == CpuSetInformation) {
				const size_t i = size_t(e->CpuSet.Group) * 64 + e->CpuSet.LogicalProcessorIndex;
				if (cls.size() <= i) cls.resize(i + 1, 0);
				cls[i] = e->CpuSet.EfficiencyClass;
				top = std::max(top, e->CpuSet.EfficiencyClass);
				lo = std::min(lo, e->CpuSet.EfficiencyClass);
			}
			at += e->Size;
		}
		hybrid = !cls.empty() && top != lo;
	}
	bool on_performance() const
	{
		PROCESSOR_NUMBER pn;
		GetCurrentProcessorNumberEx(&pn);
		const size_t i = size_t(pn.Group) * 64 + pn.Number;
		return i < cls.size() && cls[i] == top;
	}
};
const cpu_classes &classes() { static const cpu_classes c; return c; }

std::atomic<uint64_t> g_bands{ 0 }, g_bands_p{ 0 }, g_frames{ 0 }, g_frames_p{ 0 };
void band_hook()
{
	g_bands.fetch_add(1, std::memory_order_relaxed);
	if (classes().on_performance()) g_bands_p.fetch_add(1, std::memory_order_relaxed);
}
#endif

std::string cpu_name()
{
	std::string name;
#ifdef _WIN32
	char buf[256] = "";
	DWORD size = sizeof buf;
	if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
		name = buf;
#elif defined(__APPLE__)
	char buf[256] = "";
	size_t size = sizeof buf;
	if (sysctlbyname("machdep.cpu.brand_string", buf, &size, nullptr, 0) == 0) name = buf;
#elif defined(__linux__)
	if (FILE *f = fopen("/proc/cpuinfo", "r")) {
		char line[512];
		while (fgets(line, sizeof line, f))
			if (!strncmp(line, "model name", 10)) {
				if (const char *c = strchr(line, ':')) { name = c + 1; break; }
			}
		fclose(f);
	}
#endif
	while (!name.empty() && (name.back() == '\n' || name.back() == ' ')) name.pop_back();
	while (!name.empty() && name.front() == ' ') name.erase(0, 1);
	return name.empty() ? "an unknown CPU" : name;
}

// the kinds of core: "8 performance cores (16 threads), 16 efficiency cores (16 threads)"
std::string cores()
{
	char out[256] = "";
#ifdef _WIN32
	DWORD len = 0;
	GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
	std::vector<uint8_t> buf(len);
	if (len && GetLogicalProcessorInformationEx(RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()), &len)) {
		int n[256] = {}, t[256] = {}, top = 0, lo = 255;
		for (size_t at = 0; at < len;) {
			const auto *e = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(buf.data() + at);
			if (!e->Size) break;
			const int c = e->Processor.EfficiencyClass;
			int threads = 0;
			for (WORD g = 0; g < e->Processor.GroupCount; g++)
				for (KAFFINITY m = e->Processor.GroupMask[g].Mask; m; m &= m - 1) threads++;
			n[c]++; t[c] += threads;
			top = std::max(top, c); lo = std::min(lo, c);
			at += e->Size;
		}
		if (top == lo) snprintf(out, sizeof out, "%d cores (%d threads)", n[top], t[top]);
		else {
			int en = 0, et = 0;
			for (int c = lo; c < top; c++) en += n[c], et += t[c];
			snprintf(out, sizeof out, "%d performance cores (%d threads), %d efficiency cores (%d threads)", n[top], t[top], en, et);
		}
	}
#elif defined(__APPLE__)
	int p = 0, e = 0;
	size_t size = sizeof p;
	if (sysctlbyname("hw.perflevel0.physicalcpu", &p, &size, nullptr, 0) == 0) {
		size = sizeof e;
		if (sysctlbyname("hw.perflevel1.physicalcpu", &e, &size, nullptr, 0) != 0) e = 0;
		if (e) snprintf(out, sizeof out, "%d performance cores, %d efficiency cores", p, e);
		else snprintf(out, sizeof out, "%d cores", p);
	}
#endif
	if (!*out) snprintf(out, sizeof out, "%u threads", std::thread::hardware_concurrency());
	return out;
}

std::string os_name()
{
	char out[160] = "";
#ifdef _WIN32
	using rtl_t = LONG(WINAPI *)(OSVERSIONINFOW *);
	if (auto get = reinterpret_cast<rtl_t>(reinterpret_cast<void *>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion")))) {
		OSVERSIONINFOW v{};
		v.dwOSVersionInfoSize = sizeof v;
		if (get(&v) == 0) snprintf(out, sizeof out, "Windows %lu.%lu build %lu%s", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber,
								   GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version") ? " (Wine)" : "");
	}
#elif defined(__APPLE__)
	char buf[64] = "";
	size_t size = sizeof buf;
	if (sysctlbyname("kern.osproductversion", buf, &size, nullptr, 0) == 0) snprintf(out, sizeof out, "%s %s", SDL_GetPlatform(), buf);
#elif defined(__linux__)
	struct utsname u;
	if (uname(&u) == 0) snprintf(out, sizeof out, "%s %s", u.sysname, u.release);
#endif
	return *out ? out : SDL_GetPlatform();
}

// the GPUs (Windows: DXGI's adapters, not the software one), by name and memory
std::string gpus()
{
	std::string list;
#ifdef _WIN32
	using create_t = HRESULT(WINAPI *)(REFIID, void **);
	HMODULE dxgi = LoadLibraryA("dxgi.dll");
	auto create = dxgi ? reinterpret_cast<create_t>(reinterpret_cast<void *>(GetProcAddress(dxgi, "CreateDXGIFactory1"))) : nullptr;
	IDXGIFactory1 *factory = nullptr;
	if (create && SUCCEEDED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory))) && factory) {
		IDXGIAdapter1 *a = nullptr;
		for (UINT i = 0; factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
			DXGI_ADAPTER_DESC1 d;
			if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
				char mem[48];
				snprintf(mem, sizeof mem, " (%llu MB)", (unsigned long long)(d.DedicatedVideoMemory >> 20));
				list += (list.empty() ? "" : ", ") + utf8(d.Description) + mem;
			}
			a->Release();
		}
		factory->Release();
	}
	if (dxgi) FreeLibrary(dxgi);
#endif
	return list;
}

} // namespace

std::string gpu_names() { return gpus(); }

void tune()
{
#ifdef _WIN32
	// Power throttling off for the game: its threads at the clocks and on the cores the scheduler
	// gives a program that's in use, and its timers as fine as asked for, even minimized
	using set_t = BOOL(WINAPI *)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
	if (auto set = reinterpret_cast<set_t>(reinterpret_cast<void *>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetProcessInformation")))) {
		PROCESS_POWER_THROTTLING_STATE st{};
		st.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
		st.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
		st.StateMask = 0;
		if (!set(GetCurrentProcess(), ProcessPowerThrottling, &st, sizeof st)) {   // (Windows 10 before 21H2 has no timer flag)
			st.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
			set(GetCurrentProcess(), ProcessPowerThrottling, &st, sizeof st);
		}
	}
	if (classes().hybrid) row_pool::band_hook = band_hook;
#endif
}

std::string describe()
{
	std::string s = cpu_name() + ", " + cores() + ", " + std::to_string(SDL_GetNumLogicalCPUCores()) + " logical processors; " + os_name();
	const std::string g = gpus();
	if (!g.empty()) s += "; GPU " + g;
	return s;
}

void note_frame()
{
#ifdef _WIN32
	if (!classes().hybrid) return;
	g_frames.fetch_add(1, std::memory_order_relaxed);
	if (classes().on_performance()) g_frames_p.fetch_add(1, std::memory_order_relaxed);
#endif
}

std::string placement()
{
#ifdef _WIN32
	if (!classes().hybrid) return "";
	const uint64_t b = g_bands.exchange(0), bp = g_bands_p.exchange(0), f = g_frames.exchange(0), fp = g_frames_p.exchange(0);
	char out[160];
	snprintf(out, sizeof out, "on the performance cores: the drawing %.0f%% (of %llu bands), the game %.0f%% (of %llu frames)",
			 b ? 100.0 * double(bp) / double(b) : 0.0, (unsigned long long)b, f ? 100.0 * double(fp) / double(f) : 0.0, (unsigned long long)f);
	return out;
#else
	return "";
#endif
}

}
