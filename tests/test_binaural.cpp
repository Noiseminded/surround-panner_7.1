// Unit tests for the headphone renderer. Build and run with:  make -C tests
#include "../src/Binaural.hpp"
#include <chrono>
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

/** Sends an impulse into one speaker and returns the energy reaching each ear. */
static void impulseEnergy(BinauralRenderer& r, int speaker, float lfe, float& eLeft, float& eRight) {
	eLeft = eRight = 0.f;
	for (int n = 0; n < 2048; n++) {
		float in[NUM_SPEAKERS] = {};
		float l, rr;
		if (n == 0 && speaker >= 0)
			in[speaker] = 1.f;
		r.process(in, n == 0 ? lfe : 0.f, l, rr);
		eLeft += l * l;
		eRight += rr * rr;
	}
}

static void testRate(float rate) {
	Layout layout;
	BinauralRenderer r;
	check(r.configure(rate, layout.angles), "configure builds filters");
	check(!r.configure(rate, layout.angles), "configure skips when nothing changed");
	check(r.taps > 0, "filters have taps");

	float el, er;
	impulseEnergy(r, SPK_C, 0.f, el, er);
	check(std::fabs(el - er) / (el + er) < 0.02f, "centre: both ears equal");
	check(std::fabs(0.5f * (el + er) - 1.f) < 0.05f, "centre: unity energy per ear");

	impulseEnergy(r, SPK_R, 0.f, el, er);
	check(er > 2.f * el, "front right: right ear louder");
	impulseEnergy(r, SPK_L, 0.f, el, er);
	check(el > 2.f * er, "front left: left ear louder");
	impulseEnergy(r, SPK_SR, 0.f, el, er);
	check(er > 4.f * el, "side right: right ear much louder");
	impulseEnergy(r, SPK_BL, 0.f, el, er);
	check(el > 2.f * er, "back left: left ear louder");

	// Left and right speakers are mirror images
	float lL, lR, rL, rR;
	impulseEnergy(r, SPK_SL, 0.f, lL, lR);
	impulseEnergy(r, SPK_SR, 0.f, rL, rR);
	check(std::fabs(lL - rR) < 1e-4f && std::fabs(lR - rL) < 1e-4f, "SL and SR are mirrored");

	// LFE goes equally to both ears
	impulseEnergy(r, -1, 1.f, el, er);
	check(std::fabs(el - er) < 1e-6f && el > 0.f, "LFE: both ears equal");
}

int main() {
	testRate(44100.f);
	testRate(48000.f);
	testRate(96000.f);
	testRate(32000.f); // not measured: resampled from the closest set

	// Changing an angle rebuilds the filters
	Layout layout;
	BinauralRenderer r;
	r.configure(48000.f, layout.angles);
	layout.angles[SPK_SL] = -90.f;
	check(r.configure(48000.f, layout.angles), "angle change rebuilds filters");

	// Rough cost: one second of audio at 48 kHz
	float in[NUM_SPEAKERS];
	float l = 0.f, rr = 0.f, acc = 0.f;
	auto t0 = std::chrono::steady_clock::now();
	for (int n = 0; n < 48000; n++) {
		for (int s = 0; s < NUM_SPEAKERS; s++)
			in[s] = std::sin(0.01f * n * (s + 1));
		r.process(in, 0.f, l, rr);
		acc += l + rr;
	}
	auto t1 = std::chrono::steady_clock::now();
	double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
	std::printf("Binaural render: %.1f ms per second of audio at 48 kHz (%d taps)%s\n", ms, r.taps, acc == 12345.f ? " " : "");

	if (failures == 0)
		std::printf("All binaural tests passed.\n");
	return failures == 0 ? 0 : 1;
}
