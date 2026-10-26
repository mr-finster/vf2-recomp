// The host's audio path on its own, without the game or a ROM: the same SDL stream the host opens
// (16-bit stereo at the SCSP's 44100), the same frames (the Model 2's 1/57.52 s), the same band of
// sound kept queued (about 3.5 frames), and a tone to hear on the device. What it reports and
// checks: that the device opens and what it really runs at (SDL converts our 44100 to it), that it
// plays at the wall clock's rate (a phone's AAudio stalling shows up here as played time falling
// behind, or as a queue growing past the 300 ms the host waits on), and that a pause - what
// Android gives the app going to the background, and the host gives the menu - freezes the queue
// while a resume drains it.
//
//   audiocheck [SECONDS] [tone|silent]          default 3 s, 440 Hz left / 660 Hz right, -14 dBFS
//   e.g. audiocheck 5 tone
//   SDL_AUDIODRIVER=disk audiocheck 2 silent    writes sdlaudio.raw (all the device was given)
//   SDL_AUDIODRIVER=dummy audiocheck            the hermetic case: no card, the device keeps time
//   AUDIOCHECK_FLOOD=1 audiocheck 2 silent      sound made at twice the clock's rate, as a device
//                                               that stopped taking it would see: the queue check
//                                               must fail then, or it is worth nothing
//
// The line it prints for the device's buffer plus the queue the host keeps is the sound's lag
// behind the picture: on the phone that is the number to look at.
//
// On a phone the real check is the game itself (its music through the SCSP's reverb); the queue is
// printed too with VF2_PACE_LOG=1 (bytes every 60 frames). To run this there instead: build it with
// the NDK against build/android/sdl3's libSDL3.so and push both to /data/local/tmp.
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static constexpr int RATE = 44100;                                // model2a::SOUND_RATE
static constexpr double FRAME_HZ = 16000000.0 / (656.0 * 424.0);  // MAME's Model 2 screen: 57.52
static constexpr double QUEUED = 3.5;                             // frames of sound the host keeps
static constexpr double STALL_MS = 300.0;                         // ...and waits over, if more
static constexpr double TONE[2] = { 440.0, 660.0 };               // left, right

static int fails;
static void check(bool ok, const char *what)
{
	printf("%s %s\n", ok ? "  ok  " : "FAIL  ", what);
	if (!ok) fails++;
}

int main(int argc, char **argv)
{
	const double seconds = argc > 1 ? atof(argv[1]) : 3.0;
	const bool tone = !(argc > 2 && strcmp(argv[2], "silent") == 0);
	if (!SDL_Init(SDL_INIT_AUDIO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
#ifdef __ANDROID__
	// as the host does on Android: this is game audio (AAUDIO_USAGE_GAME)
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_STREAM_ROLE, "Game");
#endif

	const SDL_AudioSpec want{ SDL_AUDIO_S16, 2, RATE };
	SDL_AudioStream *audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, nullptr, nullptr);
	if (!audio) { fprintf(stderr, "no audio device: %s\n", SDL_GetError()); return 1; }
	SDL_AudioSpec in{}, out{}, dev{};
	int dev_frames = 0;
	SDL_GetAudioStreamFormat(audio, &in, &out);
	SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &dev, &dev_frames);
	printf("driver %s: %d Hz %s %d ch, a buffer of %d frames (%.1f ms)\n", SDL_GetCurrentAudioDriver(),
		   dev.freq, SDL_GetAudioFormatName(dev.format), dev.channels, dev_frames,
		   dev.freq ? 1000.0 * dev_frames / dev.freq : 0.0);
	printf("stream: %d Hz %s %d ch -> %d Hz %s %d ch\n",
		   in.freq, SDL_GetAudioFormatName(in.format), in.channels,
		   out.freq, SDL_GetAudioFormatName(out.format), out.channels);
	check(in.freq == RATE && in.format == SDL_AUDIO_S16 && in.channels == 2,
		  "the stream takes the SCSP's own 44100 Hz, 16-bit, stereo");
	if (out.freq != RATE)
		printf("  (the device runs at %d Hz: SDL resamples ours, %.2f%% of it)\n", out.freq, 100.0 * out.freq / RATE);
	SDL_ResumeAudioStreamDevice(audio);

	// The host's unit of sound, and the frames its clock runs at
	const int frame_bytes = int(RATE / FRAME_HZ) * 4;
	const double frame_ms = 1000.0 / FRAME_HZ;
	printf("keeping about %.1f frames (%.0f ms) queued, one frame (%.1f ms) at a time\n", QUEUED, QUEUED * frame_ms, frame_ms);

	std::vector<int16_t> buf;
	int peak = 0;
	uint64_t made = 0;   // sample frames the board would have made by now (its own clock)
	auto push_until = [&](double t) {
		const uint64_t total = uint64_t(RATE * t);
		if (total <= made) return;
		const size_t n = size_t(total - made);
		buf.resize(n * 2);
		for (size_t i = 0; i < n; i++)
			for (int c = 0; c < 2; c++) {
				const double v = tone ? 0.2 * std::sin(2.0 * 3.14159265358979 * TONE[c] * double(total - n + i) / RATE) : 0.0;
				const int16_t s = int16_t(std::lround(v * 32767.0));
				buf[i * 2 + c] = s;
				if (s > peak) peak = s;
				else if (-int(s) > peak) peak = -int(s);
			}
		SDL_PutAudioStreamData(audio, buf.data(), int(buf.size() * sizeof(int16_t)));
		made = total;
	};

	// The host's own loop: a frame every 1/57.52 s, its sound pushed, and the frames the queue is
	// short of run at once (the catch-up the host does on a queue running low). AUDIOCHECK_FLOOD=1
	// has the sound made at twice the clock's rate instead, as a device that stopped taking it
	// would leave the queue piling up: the checks below must then fail (the host's 300 ms wait).
	const char *flood_env = SDL_getenv("AUDIOCHECK_FLOOD");
	const bool flood = flood_env && *flood_env != '0';
	const double bytes_per_ms = frame_bytes * FRAME_HZ / 1000.0;
	const uint64_t t0 = SDL_GetTicksNS();
	int qmin = 1 << 30, qmax = 0, refills = 0;
	double qsum = 0;
	int qn = 0;
	for (double now = 0; now < seconds; now += 1.0 / FRAME_HZ) {
		const uint64_t target = t0 + uint64_t((now + 1.0 / FRAME_HZ) * 1e9);
		if (const uint64_t left = SDL_GetTicksNS(); left < target) SDL_DelayPrecise(target - left);
		const double at = double(SDL_GetTicksNS() - t0) / 1e9;
		push_until(flood ? at * 2.0 : at);
		int q = SDL_GetAudioStreamQueued(audio);
		if (!flood && q < frame_bytes * 3 / 2) {
			// the host runs the frames the queue is short of, at once (its `low`)
			const int need = int(std::ceil(QUEUED - double(q) / frame_bytes));
			for (int i = 0; i < need; i++) { push_until(at + (i + 1) * 1.0 / FRAME_HZ); refills++; }
		}
		q = SDL_GetAudioStreamQueued(audio);
		if (q < qmin) qmin = q;
		if (q > qmax) qmax = q;
		qsum += q;
		qn++;
	}
	const double wall = double(SDL_GetTicksNS() - t0) / 1e9;
	const double pushed_ms = 1000.0 * double(made) / RATE;
	const double queued_ms = SDL_GetAudioStreamQueued(audio) / bytes_per_ms;
	const double played_ms = pushed_ms - queued_ms;
	printf("%.0f ms of sound pushed in %.0f ms (%.0f%% of the clock), still queued %.0f ms, so the device played %.0f ms\n",
		   pushed_ms, wall * 1000.0, 100.0 * pushed_ms / (wall * 1000.0), queued_ms, played_ms);
	printf("queue %.0f-%.0f ms, %.0f ms average (%d catch-ups)\n", qmin / bytes_per_ms, qmax / bytes_per_ms, qsum / qn / bytes_per_ms, refills);
	const double dev_ms = dev.freq ? 1000.0 * dev_frames / dev.freq : 0.0;
	printf("the sound lags the picture by about %.0f ms (the queue, plus the device's %.1f ms buffer)\n", queued_ms + dev_ms, dev_ms);
	if (tone) printf("  (a 440 Hz tone in the left ear and 660 in the right, peak %d of 32767)\n", peak);

	// A pause, as Android gives the app going to the background (and the host gives the menu):
	// nothing is pulled, so the queue holds still; a resume pulls it again
	const int before = SDL_GetAudioStreamQueued(audio);
	SDL_PauseAudioStreamDevice(audio);
	SDL_Delay(500);
	const int held = SDL_GetAudioStreamQueued(audio);
	check(held == before, "a pause freezes the queue");
	SDL_ResumeAudioStreamDevice(audio);
	SDL_Delay(250);
	const int after = SDL_GetAudioStreamQueued(audio);
	check(after < held, "a resume drains it again");

	check(qmax < int(STALL_MS * bytes_per_ms), "the queue stays under the host's 300 ms");
	check(played_ms > 0.95 * wall * 1000.0, "the device played at the wall clock's rate");
	SDL_DestroyAudioStream(audio);
	SDL_Quit();
	if (fails) { printf("%d check(s) failed\n", fails); return 1; }
	printf("all checks passed\n");
	return 0;
}
