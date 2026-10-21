#pragma once
// Panning math for Surround Panner 7.1.
// Kept free of Rack dependencies so it can be unit tested on its own (see tests/).

#include <cmath>


namespace surround {


/** Number of full-range speakers in a 7.1 layout (the LFE is handled separately). */
static const int NUM_SPEAKERS = 7;

/** Full-range speaker indices. */
enum Speaker {
	SPK_L,
	SPK_R,
	SPK_C,
	SPK_BL,
	SPK_BR,
	SPK_SL,
	SPK_SR,
};

/** Angles in degrees: 0 = front centre, positive = clockwise (to the listener's right). */
static const float DEFAULT_ANGLES[NUM_SPEAKERS] = {
	-30.f,  // L
	30.f,   // R
	0.f,    // C
	-145.f, // BL
	145.f,  // BR
	-100.f, // SL
	100.f,  // SR
};

/** Number of virtual point sources used to render a spread source. */
static const int SPREAD_SAMPLES = 16;


/** Wraps an angle in degrees into [-180, 180). */
inline float wrapDegrees(float a) {
	a = std::fmod(a + 180.f, 360.f);
	if (a < 0.f)
		a += 360.f;
	return a - 180.f;
}


/** Where the speakers are, and which ones take part in panning. */
struct Layout {
	float angles[NUM_SPEAKERS];
	bool enabled[NUM_SPEAKERS];

	Layout() {
		setDefaultAngles();
		for (int i = 0; i < NUM_SPEAKERS; i++)
			enabled[i] = true;
	}

	void setDefaultAngles() {
		for (int i = 0; i < NUM_SPEAKERS; i++)
			angles[i] = DEFAULT_ANGLES[i];
	}

	/** Seven speakers evenly spaced around the circle, centre at 0°. */
	void setEvenAngles() {
		const float step = 360.f / NUM_SPEAKERS;
		angles[SPK_C] = 0.f;
		angles[SPK_R] = step;
		angles[SPK_SR] = 2.f * step;
		angles[SPK_BR] = 3.f * step;
		angles[SPK_BL] = -3.f * step;
		angles[SPK_SL] = -2.f * step;
		angles[SPK_L] = -step;
	}
};


/** The enabled speakers of a Layout, sorted by angle so neighbours can be found quickly. */
struct Ring {
	int count = 0;
	int index[NUM_SPEAKERS];
	float angle[NUM_SPEAKERS];

	void build(const Layout& layout) {
		count = 0;
		for (int i = 0; i < NUM_SPEAKERS; i++) {
			if (!layout.enabled[i])
				continue;
			// Insertion sort by angle
			float a = wrapDegrees(layout.angles[i]);
			int j = count;
			while (j > 0 && angle[j - 1] > a) {
				angle[j] = angle[j - 1];
				index[j] = index[j - 1];
				j--;
			}
			angle[j] = a;
			index[j] = i;
			count++;
		}
	}
};


/** Adds the power (gain squared, times `weight`) of a point source at `azimuth` degrees.
Only the two speakers either side of the source receive signal, using the equal-power (sin/cos) law.
*/
inline void addPointPower(const Ring& ring, float azimuth, float weight, float* power) {
	if (ring.count == 0)
		return;
	if (ring.count == 1) {
		power[ring.index[0]] += weight;
		return;
	}

	float a = wrapDegrees(azimuth);
	int lo = ring.count - 1;
	int hi = 0;
	for (int k = 0; k < ring.count - 1; k++) {
		if (a >= ring.angle[k] && a < ring.angle[k + 1]) {
			lo = k;
			hi = k + 1;
			break;
		}
	}

	float span = ring.angle[hi] - ring.angle[lo];
	float d = a - ring.angle[lo];
	if (hi == 0) {
		// Segment that wraps around ±180°
		span += 360.f;
		if (d < 0.f)
			d += 360.f;
	}

	float t = (span > 1e-6f) ? (d / span) : 0.5f;
	if (t < 0.f)
		t = 0.f;
	if (t > 1.f)
		t = 1.f;
	float theta = t * float(M_PI) * 0.5f;
	float c = std::cos(theta);
	float s = std::sin(theta);
	power[ring.index[lo]] += weight * c * c;
	power[ring.index[hi]] += weight * s * s;
}


/** Computes the gain of each speaker for a source at `azimuth` degrees.
`spread` in [0, 1] widens the source from a point (0) to the whole circle (1).
The sum of squared gains is always 1, so loudness stays constant while the source moves or widens.
*/
inline void computeGains(const Ring& ring, float azimuth, float spread, float* gains) {
	float power[NUM_SPEAKERS] = {};
	if (spread < 0.f)
		spread = 0.f;
	if (spread > 1.f)
		spread = 1.f;
	float width = spread * 360.f;

	if (width < 1e-3f) {
		addPointPower(ring, azimuth, 1.f, power);
	}
	else {
		// Render the spread source as several point sources across its width
		const float weight = 1.f / SPREAD_SAMPLES;
		for (int j = 0; j < SPREAD_SAMPLES; j++) {
			float offset = ((j + 0.5f) / SPREAD_SAMPLES - 0.5f) * width;
			addPointPower(ring, azimuth + offset, weight, power);
		}
	}

	for (int i = 0; i < NUM_SPEAKERS; i++)
		gains[i] = std::sqrt(power[i]);
}


} // namespace surround
