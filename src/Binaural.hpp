#pragma once
// Binaural rendering of a 7.1 speaker layout for headphones.
// Each speaker signal is convolved with the head-related impulse responses (HRIRs) of its
// direction, one per ear, and the results are summed. Kept free of Rack dependencies so it
// can be unit tested on its own (see tests/).

#include <algorithm>
#include <cmath>
#include <vector>

#include "PanLaw.hpp"
#include "HrtfData.hpp"


namespace surround {


/** Picks the measured set closest to `sampleRate`. */
inline const hrtf::Set& closestHrtfSet(float sampleRate) {
	int best = 0;
	float bestDistance = 1e9f;
	for (int i = 0; i < hrtf::NUM_SETS; i++) {
		float d = std::fabs(std::log(sampleRate / hrtf::SETS[i].sampleRate));
		if (d < bestDistance) {
			bestDistance = d;
			best = i;
		}
	}
	return hrtf::SETS[best];
}


/** Head-related impulse response pair for a direction in the horizontal plane, at `sampleRate`.
`azimuth` in degrees: 0 = front, positive = to the right. Directions between the measured
5° steps are interpolated, and the response is resampled when no measured set matches the rate.
*/
inline void hrirForAzimuth(float azimuth, float sampleRate, std::vector<float>& left, std::vector<float>& right) {
	const hrtf::Set& set = closestHrtfSet(sampleRate);
	float a = wrapDegrees(azimuth);
	bool mirror = a < 0.f;
	float m = std::fabs(a);
	if (m > 180.f)
		m = 180.f;

	// Interpolate between the two nearest measured directions
	float pos = m / hrtf::AZIMUTH_STEP;
	int i0 = std::min((int) pos, hrtf::NUM_AZIMUTHS - 1);
	int i1 = std::min(i0 + 1, hrtf::NUM_AZIMUTHS - 1);
	float frac = pos - i0;
	std::vector<float> measuredL(set.taps), measuredR(set.taps);
	for (int k = 0; k < set.taps; k++) {
		float l = (1.f - frac) * set.left[i0 * set.taps + k] + frac * set.left[i1 * set.taps + k];
		float r = (1.f - frac) * set.right[i0 * set.taps + k] + frac * set.right[i1 * set.taps + k];
		measuredL[k] = l / 32768.f;
		measuredR[k] = r / 32768.f;
	}
	// The data is for a source on the right; for the left side, swap the ears
	if (mirror)
		std::swap(measuredL, measuredR);

	// Resample to the engine's rate if needed (linear interpolation, gain-compensated)
	if (std::fabs(sampleRate - set.sampleRate) < 1.f) {
		left = measuredL;
		right = measuredR;
		return;
	}
	float ratio = set.sampleRate / sampleRate;
	int taps = std::max(8, (int) std::ceil(set.taps / ratio));
	left.assign(taps, 0.f);
	right.assign(taps, 0.f);
	for (int n = 0; n < taps; n++) {
		float t = n * ratio;
		int j = (int) t;
		float f = t - j;
		if (j + 1 < set.taps) {
			left[n] = ((1.f - f) * measuredL[j] + f * measuredL[j + 1]) * ratio;
			right[n] = ((1.f - f) * measuredR[j] + f * measuredR[j + 1]) * ratio;
		}
		else if (j < set.taps) {
			left[n] = measuredL[j] * ratio;
			right[n] = measuredR[j] * ratio;
		}
	}
}


struct BinauralRenderer {
	int taps = 0;
	/** Impulse responses per speaker, stored reversed so convolution is a forward dot product. */
	std::vector<float> irLeft[NUM_SPEAKERS];
	std::vector<float> irRight[NUM_SPEAKERS];
	/** Input history per speaker, written twice so the last `taps` samples are always contiguous. */
	std::vector<float> history[NUM_SPEAKERS];
	int pos = 0;
	/** The LFE has no direction; it goes to both ears at this gain. */
	float lfeGain = 0.7071f;

	float configuredRate = 0.f;
	float configuredAngles[NUM_SPEAKERS] = {};

	/** Rebuilds the filters when the sample rate or speaker angles changed. Returns true if it did. */
	bool configure(float sampleRate, const float* angles) {
		bool same = (sampleRate == configuredRate);
		for (int s = 0; s < NUM_SPEAKERS && same; s++)
			same = (angles[s] == configuredAngles[s]);
		if (same)
			return false;

		// Loudness reference: a source straight ahead comes out at about unity level in each ear
		std::vector<float> frontL, frontR;
		hrirForAzimuth(0.f, sampleRate, frontL, frontR);
		float energy = 0.f;
		for (size_t k = 0; k < frontL.size(); k++)
			energy += 0.5f * (frontL[k] * frontL[k] + frontR[k] * frontR[k]);
		float norm = (energy > 0.f) ? 1.f / std::sqrt(energy) : 1.f;

		int newTaps = (int) frontL.size();
		for (int s = 0; s < NUM_SPEAKERS; s++) {
			std::vector<float> l, r;
			hrirForAzimuth(angles[s], sampleRate, l, r);
			l.resize(newTaps, 0.f);
			r.resize(newTaps, 0.f);
			irLeft[s].assign(newTaps, 0.f);
			irRight[s].assign(newTaps, 0.f);
			for (int k = 0; k < newTaps; k++) {
				irLeft[s][newTaps - 1 - k] = l[k] * norm;
				irRight[s][newTaps - 1 - k] = r[k] * norm;
			}
			if (newTaps != taps)
				history[s].assign(2 * newTaps, 0.f);
			configuredAngles[s] = angles[s];
		}
		if (newTaps != taps)
			pos = 0;
		taps = newTaps;
		configuredRate = sampleRate;
		return true;
	}

	/** Renders one sample. `speakers` holds the 7 full-range speaker signals in Speaker order. */
	void process(const float* speakers, float lfe, float& outLeft, float& outRight) {
		float yl = 0.f;
		float yr = 0.f;
		if (taps > 0) {
			for (int s = 0; s < NUM_SPEAKERS; s++) {
				float* h = history[s].data();
				h[pos] = speakers[s];
				h[pos + taps] = speakers[s];
				const float* x = h + pos + 1;
				const float* fl = irLeft[s].data();
				const float* fr = irRight[s].data();
				float al = 0.f;
				float ar = 0.f;
				for (int k = 0; k < taps; k++) {
					al += fl[k] * x[k];
					ar += fr[k] * x[k];
				}
				yl += al;
				yr += ar;
			}
			if (++pos >= taps)
				pos = 0;
		}
		outLeft = yl + lfe * lfeGain;
		outRight = yr + lfe * lfeGain;
	}
};


} // namespace surround
