// A small pool of worker threads for splitting a frame's drawing by rows.
#pragma once
#ifdef __APPLE__
#include <pthread.h>
#endif
#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#if defined(__ANDROID__)
#include <pthread.h>
#include <unistd.h>
#endif

class row_pool
{
public:
	static row_pool &get() { static row_pool p; return p; }
	int threads() const { return int(m_workers.size()) + 1; }
#if defined(__ANDROID__)
	// The workers' ids as the kernel knows them, with this thread's first: the threads a frame's
	// drawing runs on, which is what Android's performance hints (a session, ADPF) are asked over.
	// (bionic's pthread_t is a pointer to its own thread record, not the kernel's tid: gettid() for
	// this thread, pthread_gettid_np() for the workers. ADPF wants the tids.)
	std::vector<long> worker_tids()
	{
		std::vector<long> ids;
		ids.push_back(long(gettid()));
		for (auto &w : m_workers) ids.push_back(long(pthread_gettid_np(w.native_handle())));
		return ids;
	}
#endif
	// called on the thread that takes each band, as it takes it (the host's log of where the
	// drawing runs: src/host/sysinfo.h)
	static inline void (*band_hook)() = nullptr;

	// Calls f(begin, end) over [0, n) in per_thread bands for each thread, and waits. With more
	// than one band a thread, each thread takes the next band when it's done with its last, so a
	// thread with heavy rows (the floor) doesn't leave the rest waiting.
	// tasks: f(i, i + 1) for each of n jobs, however few, as threads free up (n bands).
	void run(int n, const std::function<void(int, int)> &f, int per_thread = 1, bool tasks = false)
	{
		const int t = threads();
		// inside a band (a worker's, or this thread's mid-run): no pool to hand it to, so here
		if ((n < 2 * t && !tasks) || n < 2 || t == 1 || t_in_pool) { f(0, n); return; }
		// one caller at a time (another thread drawing at once would share the counters)
		std::lock_guard<std::mutex> one(m_run_mu);
		struct in_pool { in_pool() { t_in_pool = true; } ~in_pool() { t_in_pool = false; } } mark;
		{
			std::lock_guard<std::mutex> l(m_mu);
			m_job = &f;
			m_n = n;
			m_parts = tasks ? n : std::min(n, t * std::max(1, per_thread));
			m_next = 0;
			m_left = t - 1;             // the workers still to finish
			m_gen++;
		}
		m_cv.notify_all();
		take_bands(f, n);
		std::unique_lock<std::mutex> l(m_mu);
		m_done.wait(l, [&] { return m_left == 0; });
		m_job = nullptr;
	}

private:
	std::vector<std::thread> m_workers;
	std::mutex m_mu, m_run_mu;
	static inline thread_local bool t_in_pool = false;
	std::condition_variable m_cv, m_done;
	const std::function<void(int, int)> *m_job = nullptr;
	int m_n = 0, m_parts = 0, m_next = 0, m_left = 0;
	unsigned m_gen = 0;
	bool m_quit = false;

	static std::pair<int, int> band(int i, int n, int parts) { return { n * i / parts, n * (i + 1) / parts }; }

	// draws bands until none are left
	void take_bands(const std::function<void(int, int)> &f, int n)
	{
		for (;;) {
			int i, parts;
			{
				std::lock_guard<std::mutex> l(m_mu);
				if (m_next >= m_parts) return;
				i = m_next++;
				parts = m_parts;
			}
			if (band_hook) band_hook();
			auto [b, e] = band(i, n, parts);
			f(b, e);
		}
	}

	row_pool()
	{
		// the CPU and sound keep theirs; 12 threads in all at most (from 8: at 3x on an M4 Max's 14
		// cores a fight's picture took 3.9 ms on 8, 3.3 on 12)
		int n = std::clamp(int(std::thread::hardware_concurrency()) - 2, 0, 11);
		if (const char *t = getenv("VF2_THREADS")) n = std::clamp(atoi(t) - 1, 0, 31);   // testing: threads in all
		for (int i = 0; i < n; i++)
			m_workers.emplace_back([this] {
#ifdef __APPLE__
				pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);   // (the performance cores)
#endif
				unsigned seen = 0;
				for (;;) {
					std::unique_lock<std::mutex> l(m_mu);
					m_cv.wait(l, [&] { return m_quit || m_gen != seen; });
					if (m_quit) return;
					seen = m_gen;
					const auto *job = m_job;
					const int n = m_n;
					l.unlock();
					t_in_pool = true;
					take_bands(*job, n);
					t_in_pool = false;
					l.lock();
					if (--m_left == 0) m_done.notify_one();
				}
			});
	}
	~row_pool()
	{
		{ std::lock_guard<std::mutex> l(m_mu); m_quit = true; }
		m_cv.notify_all();
		for (auto &w : m_workers) w.join();
	}
};

inline void parallel_rows(int n, const std::function<void(int, int)> &f, int per_thread = 1) { row_pool::get().run(n, f, per_thread); }
// n jobs at once, each f(i) on a thread of the pool's
inline void parallel_tasks(int n, const std::function<void(int)> &f) { row_pool::get().run(n, [&](int a, int b) { for (int i = a; i < b; i++) f(i); }, 1, true); }

// One job at a time on a thread of its own (the host's pipelining: a picture drawn while the board
// runs on). The job can use parallel_rows: it's a caller like any other.
class background_job
{
public:
	~background_job()
	{
		if (!m_thread.joinable()) return;
		{ std::lock_guard<std::mutex> l(m_mu); m_quit = true; }
		m_cv.notify_all();
		m_thread.join();
	}
	// waits for the last job, then starts this one
	void start(std::function<void()> f)
	{
		wait();
		if (!m_thread.joinable()) m_thread = std::thread([this] {
#ifdef __APPLE__
			pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
			loop();
		});
		{ std::lock_guard<std::mutex> l(m_mu); m_job = std::move(f); m_busy = true; }
		m_cv.notify_all();
	}
	void wait()
	{
		std::unique_lock<std::mutex> l(m_mu);
		m_done.wait(l, [&] { return !m_busy; });
	}

private:
	std::thread m_thread;
	std::mutex m_mu;
	std::condition_variable m_cv, m_done;
	std::function<void()> m_job;
	bool m_busy = false, m_quit = false;

	void loop()
	{
		for (;;) {
			std::function<void()> f;
			{
				std::unique_lock<std::mutex> l(m_mu);
				m_cv.wait(l, [&] { return m_quit || m_job; });
				if (m_quit) return;
				f = std::move(m_job);
				m_job = nullptr;
			}
			f();
			{ std::lock_guard<std::mutex> l(m_mu); m_busy = false; }
			m_done.notify_all();
		}
	}
};
