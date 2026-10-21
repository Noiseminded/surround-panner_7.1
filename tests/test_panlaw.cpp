// Unit tests for the panning math. Build and run with:  make -C tests
#include "../src/PanLaw.hpp"
#include <cstdio>
#include <cmath>

using namespace surround;

static int failures = 0;

static void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

static bool near(float a, float b, float tol = 1e-4f) {
	return std::fabs(a - b) < tol;
}

static float powerSum(const float* g) {
	float p = 0.f;
	for (int i = 0; i < NUM_SPEAKERS; i++)
		p += g[i] * g[i];
	return p;
}

int main() {
	Layout layout;
	Ring ring;
	ring.build(layout);
	float g[NUM_SPEAKERS];

	// A source exactly on a speaker plays only from that speaker
	for (int s = 0; s < NUM_SPEAKERS; s++) {
		computeGains(ring, DEFAULT_ANGLES[s], 0.f, g);
		check(near(g[s], 1.f), "source on a speaker: that speaker at full gain");
		for (int o = 0; o < NUM_SPEAKERS; o++)
			if (o != s)
				check(near(g[o], 0.f), "source on a speaker: other speakers silent");
	}

	// Halfway between L (-30) and C (0): both at -3 dB
	computeGains(ring, -15.f, 0.f, g);
	check(near(g[SPK_L], std::sqrt(0.5f)) && near(g[SPK_C], std::sqrt(0.5f)), "midpoint L/C at -3 dB");

	// Directly behind: halfway between BL (-145) and BR (145), across the wrap
	computeGains(ring, 180.f, 0.f, g);
	check(near(g[SPK_BL], std::sqrt(0.5f)) && near(g[SPK_BR], std::sqrt(0.5f)), "midpoint BL/BR across +-180");
	computeGains(ring, -180.f, 0.f, g);
	check(near(g[SPK_BL], std::sqrt(0.5f)) && near(g[SPK_BR], std::sqrt(0.5f)), "-180 equals +180");
	computeGains(ring, 540.f, 0.f, g);
	check(near(g[SPK_BL], std::sqrt(0.5f)), "azimuth wraps beyond one turn");

	// Constant power for every azimuth and spread
	for (int si = 0; si <= 10; si++) {
		for (int a = -360; a <= 360; a += 3) {
			computeGains(ring, (float) a, si / 10.f, g);
			check(near(powerSum(g), 1.f, 1e-3f), "constant power");
		}
	}

	// Full spread reaches every speaker
	computeGains(ring, 0.f, 1.f, g);
	for (int s = 0; s < NUM_SPEAKERS; s++)
		check(g[s] > 0.1f, "full spread: every speaker plays");

	// Without the centre speaker, front centre is a phantom image between L and R
	layout.enabled[SPK_C] = false;
	ring.build(layout);
	computeGains(ring, 0.f, 0.f, g);
	check(near(g[SPK_C], 0.f), "centre disabled: silent");
	check(near(g[SPK_L], std::sqrt(0.5f)) && near(g[SPK_R], std::sqrt(0.5f)), "centre disabled: phantom centre");

	// Even circle layout
	Layout even;
	even.setEvenAngles();
	ring.build(even);
	computeGains(ring, 360.f / 7.f * 0.5f, 0.f, g);
	check(near(g[SPK_C], std::sqrt(0.5f)) && near(g[SPK_R], std::sqrt(0.5f)), "even circle: midpoint C/R");

	// Movement is continuous: no jumps while sweeping a full turn in small steps
	layout = Layout();
	ring.build(layout);
	float prev[NUM_SPEAKERS];
	computeGains(ring, -180.f, 0.f, prev);
	for (int i = 1; i <= 3600; i++) {
		computeGains(ring, -180.f + i * 0.1f, 0.f, g);
		for (int s = 0; s < NUM_SPEAKERS; s++)
			check(std::fabs(g[s] - prev[s]) < 0.02f, "continuous sweep");
		for (int s = 0; s < NUM_SPEAKERS; s++)
			prev[s] = g[s];
	}

	if (failures == 0)
		std::printf("All pan law tests passed.\n");
	return failures == 0 ? 0 : 1;
}
