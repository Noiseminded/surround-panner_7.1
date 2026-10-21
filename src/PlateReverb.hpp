#pragma once
// A plate reverb for 7.1, after Jon Dattorro, "Effect Design Part 1: Reverberator and Other
// Filters", J. Audio Eng. Soc. 45(9), 1997 (the design behind many plate reverbs, Plateau included).
//
// The input is diffused and fed into a "figure-eight" tank of two cross-coupled halves.
// Dattorro reads a stereo output from seven taps spread over the tank; here every speaker gets
// its own set of taps, so the seven outputs are decorrelated and the room sounds wide rather
// than collapsing into a phantom centre. Left-side speakers listen mostly to the left half of
// the tank, right-side speakers to the right half.
//
// Kept free of Rack dependencies so it can be unit tested on its own (see tests/).

#include <algorithm>
#include <cmath>
#include <vector>

#include "PanLaw.hpp"


namespace surround {


/** Delay line with fractional reads. Size is a power of two. */
struct ReverbDelay {
	std::vector<float> buf;
	int mask = 0;
	int w = 0;

	void init(int minSize) {
		int n = 1;
		while (n < minSize + 4)
			n <<= 1;
		buf.assign(n, 0.f);
		mask = n - 1;
		w = 0;
	}
	void clear() {
		std::fill(buf.begin(), buf.end(), 0.f);
	}
	void push(float x) {
		buf[w] = x;
		w = (w + 1) & mask;
	}
	/** The sample written `d` samples ago (d >= 1), linearly interpolated. */
	float read(float d) const {
		if (d < 1.f)
			d = 1.f;
		float maxD = (float) mask - 2.f;
		if (d > maxD)
			d = maxD;
		int i = (int) d;
		float f = d - i;
		float a = buf[(w - i) & mask];
		float b = buf[(w - i - 1) & mask];
		return a + f * (b - a);
	}
};


/** Schroeder allpass with a fractional (modulatable) length. */
struct ReverbAllpass {
	ReverbDelay line;

	float process(float x, float length, float g) {
		float z = line.read(length);
		float v = x + g * z;
		line.push(v);
		return z - g * v;
	}
};


struct PlateParams {
	float preDelayMs = 20.f;   ///< 0 to 500
	float size = 1.f;          ///< 0.25 to 2, scales every delay in the tank
	float decay = 0.7f;        ///< 0 to 0.9999
	float diffusion = 1.f;     ///< 0 to 1, amount of input diffusion
	float lowCutHz = 80.f;     ///< high-pass inside the tank
	float highCutHz = 8000.f;  ///< low-pass (damping) inside the tank
	float modSpeedHz = 0.5f;   ///< tank modulation rate
	float modDepth = 0.5f;     ///< 0 to 1
	float depth = 0.5f;        ///< 0 to 1, extra pre-delay towards the back of the room
	float rear = 0.5f;         ///< 0 = reverb mostly in front, 0.5 = even, 1 = mostly behind
	bool freeze = false;       ///< hold the current tail forever
};


struct PlateReverb7 {
	// Delay lengths from Dattorro's design, in samples at his 29761 Hz
	static constexpr float BASE_RATE = 29761.f;

	float sampleRate = 0.f;
	float k = 1.f;  ///< samples per Dattorro sample

	ReverbDelay preDelay[2];
	ReverbAllpass diffuser[2][4];
	float diffuserLength[2][4] = {};
	float bandwidthState[2] = {};

	struct Half {
		ReverbAllpass ap1;   ///< modulated "decay diffusion 1"
		ReverbDelay delayA;
		ReverbAllpass ap2;   ///< "decay diffusion 2"
		ReverbDelay delayB;
		float lp = 0.f;
		float hpState = 0.f;
		float out = 0.f;     ///< end of this half, fed into the other half
		float lenAp1 = 0.f, lenA = 0.f, lenAp2 = 0.f, lenB = 0.f;
	};
	Half half[2];

	struct Tap {
		int half;
		int line;   ///< 0 = delayA, 1 = ap2, 2 = delayB
		float pos;  ///< position along the line, 0 to 1
		float sign;
	};
	static const int TAPS_PER_OUTPUT = 7;
	Tap taps[NUM_SPEAKERS][TAPS_PER_OUTPUT];

	ReverbDelay outDelay[NUM_SPEAKERS];
	float lfoPhase = 0.f;
	float sizeSmooth = 1.f;

	PlateReverb7() {
		buildTaps();
	}

	/** Allocates the delay lines for a sample rate. Clears the tail. */
	void init(float rate) {
		sampleRate = rate;
		k = rate / BASE_RATE;
		const float maxSize = 2.f;
		for (int h = 0; h < 2; h++)
			preDelay[h].init((int) (0.5f * rate) + 8);
		// Input diffusers: Dattorro's lengths on the left, slightly longer on the right
		const float base[4] = {142.f, 107.f, 379.f, 277.f};
		const float spread[2] = {1.f, 1.071f};
		for (int h = 0; h < 2; h++) {
			for (int i = 0; i < 4; i++) {
				diffuserLength[h][i] = base[i] * spread[h] * k;
				diffuser[h][i].line.init((int) diffuserLength[h][i] + 4);
			}
		}
		const float lenAp1[2] = {672.f, 908.f};
		const float lenA[2] = {4453.f, 4217.f};
		const float lenAp2[2] = {1800.f, 2656.f};
		const float lenB[2] = {3720.f, 3163.f};
		for (int h = 0; h < 2; h++) {
			half[h].lenAp1 = lenAp1[h] * k;
			half[h].lenA = lenA[h] * k;
			half[h].lenAp2 = lenAp2[h] * k;
			half[h].lenB = lenB[h] * k;
			half[h].ap1.line.init((int) ((lenAp1[h] * maxSize + 64.f) * k));
			half[h].delayA.init((int) (lenA[h] * maxSize * k) + 8);
			half[h].ap2.line.init((int) (lenAp2[h] * maxSize * k) + 8);
			half[h].delayB.init((int) (lenB[h] * maxSize * k) + 8);
		}
		for (int s = 0; s < NUM_SPEAKERS; s++)
			outDelay[s].init((int) (0.04f * rate) + 8);
		clear();
	}

	void clear() {
		for (int h = 0; h < 2; h++) {
			preDelay[h].clear();
			for (int i = 0; i < 4; i++)
				diffuser[h][i].line.clear();
			half[h].ap1.line.clear();
			half[h].delayA.clear();
			half[h].ap2.line.clear();
			half[h].delayB.clear();
			half[h].lp = half[h].hpState = half[h].out = 0.f;
			bandwidthState[h] = 0.f;
		}
		for (int s = 0; s < NUM_SPEAKERS; s++)
			outDelay[s].clear();
	}

	/** Gives every speaker its own tap set. Same-side taps are added, far-side taps subtracted,
	following the pattern of Dattorro's stereo outputs. Positions come from a fixed sequence so
	that no two speakers read the tank at the same points. */
	void buildTaps() {
		// Which half each speaker listens to most: 0 = left, 1 = right, -1 = both (centre)
		const int nearHalf[NUM_SPEAKERS] = {0, 1, -1, 0, 1, 0, 1};  // L R C BL BR SL SR
		for (int s = 0; s < NUM_SPEAKERS; s++) {
			float seed = 0.1f + 0.137f * s;
			auto nextPos = [&]() {
				seed += 0.6180340f;
				seed -= std::floor(seed);
				return 0.06f + 0.88f * seed;
			};
			int nearH = nearHalf[s] < 0 ? (s % 2) : nearHalf[s];
			int farH = 1 - nearH;
			if (nearHalf[s] >= 0) {
				taps[s][0] = {nearH, 0, nextPos(), +1.f};
				taps[s][1] = {nearH, 0, nextPos(), +1.f};
				taps[s][2] = {nearH, 1, nextPos(), -1.f};
				taps[s][3] = {nearH, 2, nextPos(), +1.f};
				taps[s][4] = {farH, 0, nextPos(), -1.f};
				taps[s][5] = {farH, 1, nextPos(), -1.f};
				taps[s][6] = {farH, 2, nextPos(), -1.f};
			}
			else {
				// Centre: an even mix of both halves
				taps[s][0] = {0, 0, nextPos(), +1.f};
				taps[s][1] = {1, 0, nextPos(), +1.f};
				taps[s][2] = {0, 1, nextPos(), -1.f};
				taps[s][3] = {1, 1, nextPos(), -1.f};
				taps[s][4] = {0, 2, nextPos(), +1.f};
				taps[s][5] = {1, 2, nextPos(), +1.f};
				taps[s][6] = {0, 0, nextPos(), -1.f};
			}
		}
	}

	static float onePoleCoef(float cutoffHz, float rate) {
		float c = 1.f - std::exp(-2.f * float(M_PI) * cutoffHz / rate);
		return std::min(std::max(c, 0.f), 1.f);
	}

	/** Processes one sample. `inLeft` and `inRight` feed the two halves of the tank.
	Writes the wet signal for each full-range speaker (Speaker order) into `out`. */
	void process(float inLeft, float inRight, const PlateParams& p, float* out) {
		if (sampleRate <= 0.f)
			return;

		const bool freeze = p.freeze;
		const float decay = freeze ? 1.f : std::min(std::max(p.decay, 0.f), 0.9999f);
		const float inputGain = freeze ? 0.f : 1.f;

		// Smooth size changes so moving the knob glides instead of clicking
		float targetSize = std::min(std::max(p.size, 0.25f), 2.f);
		sizeSmooth += (targetSize - sizeSmooth) * (1.f - std::exp(-1.f / (0.15f * sampleRate)));
		const float size = sizeSmooth;

		// Pre-delay, input bandwidth and diffusion, separately for each side
		const float sides[2] = {inLeft, inRight};
		const float pdSamples = std::max(1.f, p.preDelayMs * 0.001f * sampleRate);

		const float bw = onePoleCoef(18000.f, sampleRate);
		const float id1 = 0.75f * p.diffusion;
		const float id2 = 0.625f * p.diffusion;
		float diffused[2];
		for (int h = 0; h < 2; h++) {
			preDelay[h].push(sides[h] * inputGain);
			float x = preDelay[h].read(pdSamples);
			bandwidthState[h] += bw * (x - bandwidthState[h]);
			x = bandwidthState[h];
			x = diffuser[h][0].process(x, diffuserLength[h][0], id1);
			x = diffuser[h][1].process(x, diffuserLength[h][1], id1);
			x = diffuser[h][2].process(x, diffuserLength[h][2], id2);
			x = diffuser[h][3].process(x, diffuserLength[h][3], id2);
			diffused[h] = x;
		}

		// Tank modulation: two quadrature sines on the first allpass of each half
		if (!freeze) {
			lfoPhase += p.modSpeedHz / sampleRate;
			lfoPhase -= std::floor(lfoPhase);
		}
		const float excursion = 24.f * k * std::min(std::max(p.modDepth, 0.f), 1.f);
		const float mod[2] = {
			excursion * (1.f + std::sin(2.f * float(M_PI) * lfoPhase)) * 0.5f,
			excursion * (1.f + std::cos(2.f * float(M_PI) * lfoPhase)) * 0.5f,
		};

		const float dampCoef = freeze ? 1.f : onePoleCoef(std::min(p.highCutHz, 0.45f * sampleRate), sampleRate);
		const float lowCoef = freeze ? 0.f : onePoleCoef(p.lowCutHz, sampleRate);
		const float dd1 = 0.70f;
		const float dd2 = std::min(std::max(decay + 0.15f, 0.25f), 0.5f);

		// Interpolated reads lose a little high end on every pass. That is part of the sound
		// normally, but a frozen tail would slowly fade, so frozen lengths snap to whole samples.
		auto len = [freeze](float l) {
			return freeze ? std::round(l) : l;
		};

		// The two halves feed each other: compute both from last sample's ends
		float prevOut[2] = {half[0].out, half[1].out};
		for (int h = 0; h < 2; h++) {
			Half& t = half[h];
			float x = diffused[h] + decay * prevOut[1 - h];
			x = t.ap1.process(x, len(t.lenAp1 * size + mod[h]), -dd1);
			float a = t.delayA.read(len(t.lenA * size));
			t.delayA.push(x);
			t.lp += dampCoef * (a - t.lp);
			t.hpState += lowCoef * (t.lp - t.hpState);
			// Frozen: no filtering at all, so the loop gain is exactly one
			float y = (freeze ? a : (t.lp - t.hpState)) * decay;
			y = t.ap2.process(y, len(t.lenAp2 * size), dd2);
			t.out = t.delayB.read(len(t.lenB * size));
			t.delayB.push(y);
		}

		// Read every speaker's taps, then delay the outputs for depth and set the rear balance
		const float outGain = 0.6f;
		const float depthMs[NUM_SPEAKERS] = {0.f, 0.f, 0.f, 30.f, 30.f, 15.f, 15.f};  // L R C BL BR SL SR
		float rear = std::min(std::max(p.rear, 0.f), 1.f);
		float frontGain = std::min(1.f, 2.f * (1.f - rear));
		float backGain = std::min(1.f, 2.f * rear);
		const float balance[NUM_SPEAKERS] = {frontGain, frontGain, frontGain, backGain, backGain, 1.f, 1.f};
		for (int s = 0; s < NUM_SPEAKERS; s++) {
			float sum = 0.f;
			for (int i = 0; i < TAPS_PER_OUTPUT; i++) {
				const Tap& tp = taps[s][i];
				const Half& t = half[tp.half];
				float v;
				if (tp.line == 0)
					v = t.delayA.read(1.f + tp.pos * (t.lenA * size - 1.f));
				else if (tp.line == 1)
					v = t.ap2.line.read(1.f + tp.pos * (t.lenAp2 * size - 1.f));
				else
					v = t.delayB.read(1.f + tp.pos * (t.lenB * size - 1.f));
				sum += tp.sign * v;
			}
			sum *= outGain;
			outDelay[s].push(sum);
			float d = 1.f + p.depth * depthMs[s] * 0.001f * sampleRate;
			out[s] = outDelay[s].read(d) * balance[s];
		}
	}
};


} // namespace surround
