// Unit tests for the 7.1 plate reverb. Build and run with:  make -C tests
#include "../src/PlateReverb.hpp"
#include <chrono>
#include <cstdio>
#include <cmath>
#include <random>

using namespace surround;

static int failures = 0;

static void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

static bool finite(float x) {
	return std::isfinite(x) && std::fabs(x) < 1e4f;
}

/** Feeds an impulse and returns the energy (all speakers) in consecutive windows of `win` samples. */
static std::vector<double> impulseEnergy(PlateReverb7& r, const PlateParams& p, int windows, int win) {
	std::vector<double> e(windows, 0.0);
	float out[NUM_SPEAKERS];
	for (int n = 0; n < windows * win; n++) {
		float x = (n == 0) ? 1.f : 0.f;
		r.process(x, x, p, out);
		for (int s = 0; s < NUM_SPEAKERS; s++)
			e[n / win] += out[s] * out[s];
	}
	return e;
}

/** Time for the impulse response energy to fall by 60 dB from its peak window, in seconds. */
static double rt60(const std::vector<double>& e, int win, float rate) {
	double peak = 0.0;
	size_t peakAt = 0;
	for (size_t i = 0; i < e.size(); i++)
		if (e[i] > peak) {
			peak = e[i];
			peakAt = i;
		}
	for (size_t i = peakAt; i < e.size(); i++)
		if (e[i] < peak * 1e-6)
			return (double) (i - peakAt) * win / rate;
	return 1e9;
}

int main() {
	const float rate = 48000.f;
	const int win = 480;  // 10 ms windows

	// Longer decay setting gives a longer tail
	PlateReverb7 r;
	r.init(rate);
	PlateParams p;
	p.preDelayMs = 0.f;
	p.decay = 0.5f;
	double shortTail = rt60(impulseEnergy(r, p, 2000, win), win, rate);
	r.clear();
	p.decay = 0.85f;
	double longTail = rt60(impulseEnergy(r, p, 2000, win), win, rate);
	std::printf("RT60: %.2f s at decay 0.5, %.2f s at decay 0.85\n", shortTail, longTail);
	check(shortTail > 0.1 && shortTail < 20.0, "decay 0.5 has a finite tail");
	check(longTail > 1.5 * shortTail, "higher decay gives a longer tail");

	// Bigger size gives a longer tail at the same decay
	r.clear();
	p.decay = 0.7f;
	p.size = 0.5f;
	r.sizeSmooth = p.size;
	double small = rt60(impulseEnergy(r, p, 2000, win), win, rate);
	r.clear();
	p.size = 2.f;
	r.sizeSmooth = p.size;
	double big = rt60(impulseEnergy(r, p, 3000, win), win, rate);
	std::printf("RT60: %.2f s at size 0.5, %.2f s at size 2\n", small, big);
	check(big > small, "bigger size gives a longer tail");

	// Outputs are decorrelated: noise in, compare every pair of speakers
	{
		PlateReverb7 rv;
		rv.init(rate);
		PlateParams q;
		q.decay = 0.8f;
		q.depth = 0.f;
		std::mt19937 rng(1);
		std::normal_distribution<float> noise(0.f, 1.f);
		const int N = 96000;
		std::vector<float> o[NUM_SPEAKERS];
		double inEnergy = 0.0;
		float out[NUM_SPEAKERS];
		for (int n = 0; n < N; n++) {
			float x = noise(rng);
			inEnergy += x * x;
			rv.process(x, x, q, out);
			bool ok = true;
			for (int s = 0; s < NUM_SPEAKERS; s++) {
				o[s].push_back(out[s]);
				ok = ok && finite(out[s]);
			}
			if (!ok) {
				check(false, "noise input stays finite");
				break;
			}
		}
		double worst = 0.0;
		double outEnergy = 0.0;
		for (int a = 0; a < NUM_SPEAKERS; a++) {
			for (int n = N / 2; n < N; n++)
				outEnergy += o[a][n] * o[a][n];
			for (int b = a + 1; b < NUM_SPEAKERS; b++) {
				double sab = 0, saa = 0, sbb = 0;
				for (int n = N / 2; n < N; n++) {
					sab += o[a][n] * o[b][n];
					saa += o[a][n] * o[a][n];
					sbb += o[b][n] * o[b][n];
				}
				double c = std::fabs(sab / std::sqrt(saa * sbb + 1e-20));
				worst = std::max(worst, c);
			}
		}
		double level = std::sqrt(outEnergy / NUM_SPEAKERS / (N / 2) / (inEnergy / N));
		std::printf("Worst correlation between two speakers: %.2f, wet level per speaker: %.2f\n", worst, level);
		check(worst < 0.4, "speakers are decorrelated");
		check(level > 0.2 && level < 5.0, "wet level is sensible");
	}

	// Freeze holds the tail without growing or dying
	{
		PlateReverb7 rv;
		rv.init(rate);
		PlateParams q;
		q.decay = 0.8f;
		std::mt19937 rng(2);
		std::normal_distribution<float> noise(0.f, 1.f);
		float out[NUM_SPEAKERS];
		for (int n = 0; n < 24000; n++)
			rv.process(noise(rng), noise(rng), q, out);
		q.freeze = true;
		auto energyOver = [&](int samples) {
			double e = 0.0;
			for (int n = 0; n < samples; n++) {
				rv.process(noise(rng), noise(rng), q, out);  // input is ignored while frozen
				for (int s = 0; s < NUM_SPEAKERS; s++)
					e += out[s] * out[s];
			}
			return e;
		};
		energyOver(48000);
		double e1 = energyOver(48000);
		energyOver(48000 * 8);
		double e2 = energyOver(48000);
		std::printf("Freeze: energy ratio after 9 s: %.3f\n", e2 / e1);
		check(e1 > 0.0, "frozen tail is audible");
		check(e2 / e1 > 0.7 && e2 / e1 < 1.3, "frozen tail stays steady");
	}

	// Stays stable at extreme settings and other sample rates
	{
		const float rates[] = {44100.f, 96000.f, 192000.f};
		for (float sr : rates) {
			PlateReverb7 rv;
			rv.init(sr);
			PlateParams q;
			q.decay = 0.9999f;
			q.size = 2.f;
			q.modDepth = 1.f;
			q.modSpeedHz = 4.f;
			q.highCutHz = 20000.f;
			q.lowCutHz = 20.f;
			q.preDelayMs = 500.f;
			q.depth = 1.f;
			std::mt19937 rng(3);
			std::normal_distribution<float> noise(0.f, 1.f);
			float out[NUM_SPEAKERS];
			bool ok = true;
			for (int n = 0; n < (int) sr * 5 && ok; n++) {
				float x = n < (int) sr ? noise(rng) : 0.f;
				q.size = 0.25f + 1.75f * (0.5f + 0.5f * std::sin(n * 0.0001f));  // sweep size
				rv.process(x, -x, q, out);
				for (int s = 0; s < NUM_SPEAKERS; s++)
					ok = ok && finite(out[s]);
			}
			check(ok, "stable at extreme settings");
		}
	}

	// Rough cost
	{
		PlateReverb7 rv;
		rv.init(48000.f);
		PlateParams q;
		float out[NUM_SPEAKERS];
		float acc = 0.f;
		auto t0 = std::chrono::steady_clock::now();
		for (int n = 0; n < 48000; n++) {
			rv.process(std::sin(n * 0.01f), std::sin(n * 0.013f), q, out);
			acc += out[0];
		}
		auto t1 = std::chrono::steady_clock::now();
		std::printf("Plate reverb: %.1f ms per second of audio at 48 kHz%s\n",
			std::chrono::duration<double, std::milli>(t1 - t0).count(), acc == 12345.f ? " " : "");
	}

	if (failures == 0)
		std::printf("All plate reverb tests passed.\n");
	return failures == 0 ? 0 : 1;
}
