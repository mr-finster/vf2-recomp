// The computer, for log.txt (what a player's log needs to show why the game is slow there), and
// Windows' scheduling of the game's threads.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sysinfo {

// Windows: the game opts out of power throttling (EcoQoS: Windows 11 may run a program it
// judges unimportant on the efficiency cores at low clocks, and let its timers run coarse)
void tune();
// One line: the CPU (its kinds of core), the system, the GPUs
std::string describe();
// The GPUs by name and memory (Windows: DXGI's adapters), as in describe()'s line
std::string gpu_names();
// Where the game's threads ran (Windows, on a CPU with performance and efficiency cores: Intel's
// 12th generation on): the share of the drawing's bands, and of the game's frames, run on the
// performance cores since the last call. Empty where that isn't known.
void note_frame();   // (the game's thread, at each frame)
std::string placement();
// The cores' clocks as they are now, by kind of core ("4 at 2.28 of 3.05 GHz, 4 at 1.79 of 2.26"),
// for log.txt: a phone slows down as it warms up, and this says whether a slow run was a hot one.
// Empty where the clocks aren't readable (Windows, macOS).
std::string cpu_frequencies();
// The GPU's clock as it is now ("0.90 of 1.30 GHz"), for log.txt: a phone's PowerHAL is said to
// hold a program it doesn't take for a game at a low clock (a settled quarter of the top), which
// would make a slow run's GPU numbers look like the GPU's fault when they aren't. Empty where
// there's no GPU clock to read (Windows, macOS, an x86 desktop's integrated graphics).
std::string gpu_frequencies();

// Android 13+ (API 33): a performance hint session (ADPF, android/performance_hint.h) over the
// threads a frame's work runs on, with the frame's own 17.4 ms as its target and the work each
// pass really took reported back — the platform's way of keeping those cores (and the GPU) at
// their clocks, where a phone would otherwise take the game for a program in the background.
// The Android counterpart of tune()'s Windows power throttling. No-op elsewhere, and on phones
// below Android 13 (their libandroid.so has no such calls): perf_hints_start() then returns false.
bool perf_hints_available();
bool perf_hints_start(const std::vector<long> &thread_ids, int64_t target_ns);
void perf_hints_report(int64_t work_ns);
void perf_hints_stop();

}
