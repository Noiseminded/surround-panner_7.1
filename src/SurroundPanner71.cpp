#include "plugin.hpp"
#include "Common.hpp"

using namespace noiseminded;


static const int MAX_CHANNELS = 16;

static const float LFE_CUTOFFS[] = {80.f, 100.f, 120.f};
static const int NUM_LFE_CUTOFFS = 3;


struct SurroundPanner71 : Module {
	enum ParamId {
		AZIMUTH_PARAM,
		AZIMUTH_ATT_PARAM,
		SPREAD_PARAM,
		SPREAD_ATT_PARAM,
		LFE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_INPUT,
		AZIMUTH_INPUT,
		SPREAD_INPUT,
		INPUTS_LEN
	};
	// Outputs follow the common 7.1 channel order (WAV / Windows): L R C LFE BL BR SL SR
	enum OutputId {
		L_OUTPUT,
		R_OUTPUT,
		C_OUTPUT,
		LFE_OUTPUT,
		BL_OUTPUT,
		BR_OUTPUT,
		SL_OUTPUT,
		SR_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	// Settings (saved with the patch)
	Layout layout;
	bool sumPoly = true;
	float lfeCutoff = 120.f;

	// Engine state
	dsp::ClockDivider controlDivider;
	Ring ring;
	float gainCur[MAX_CHANNELS][NUM_SPEAKERS] = {};
	float gainStep[MAX_CHANNELS][NUM_SPEAKERS] = {};
	int lastChannels = 0;
	dsp::TBiquadFilter<float> lfeFilter[MAX_CHANNELS];
	float filterSampleRate = 0.f;
	float filterCutoff = 0.f;

	// Read by the display (written by the engine at control rate)
	int dispChannels = 1;
	float dispAzimuth[MAX_CHANNELS] = {};
	float dispSpread[MAX_CHANNELS] = {};
	float dispGain[NUM_SPEAKERS] = {};

	SurroundPanner71() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(AZIMUTH_PARAM, -180.f, 180.f, 0.f, "Azimuth", "°");
		configParam(AZIMUTH_ATT_PARAM, -1.f, 1.f, 1.f, "Azimuth CV amount", "%", 0.f, 100.f);
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.f, "Spread", "%", 0.f, 100.f);
		configParam(SPREAD_ATT_PARAM, -1.f, 1.f, 1.f, "Spread CV amount", "%", 0.f, 100.f);
		configParam(LFE_PARAM, 0.f, 1.f, 0.f, "LFE send", "%", 0.f, 100.f);

		configInput(IN_INPUT, "Audio");
		configInput(AZIMUTH_INPUT, "Azimuth CV (10 V = one full turn)");
		configInput(SPREAD_INPUT, "Spread CV (10 V = 100%)");

		for (int s = 0; s < NUM_SPEAKERS; s++)
			configOutput(speakerChannel(s), SPEAKER_LONG[s]);
		configOutput(LFE_OUTPUT, "LFE (low-passed send)");

		configBypass(IN_INPUT, C_OUTPUT);

		controlDivider.setDivision(16);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		layout = Layout();
		sumPoly = true;
		lfeCutoff = 120.f;
		lastChannels = 0;
	}

	/** Recomputes the target gain of every voice. Voices from `jumpFrom` on snap to their target instead of gliding. */
	void updateTargets(int channels, int jumpFrom) {
		ring.build(layout);
		const float steps = (float) controlDivider.getDivision();
		const bool azConnected = inputs[AZIMUTH_INPUT].isConnected();
		const bool spreadConnected = inputs[SPREAD_INPUT].isConnected();

		float maxGain[NUM_SPEAKERS] = {};
		for (int c = 0; c < channels; c++) {
			float azimuth = params[AZIMUTH_PARAM].getValue();
			if (azConnected)
				azimuth += inputs[AZIMUTH_INPUT].getPolyVoltage(c) * 36.f * params[AZIMUTH_ATT_PARAM].getValue();
			azimuth = wrapDegrees(azimuth);

			float spread = params[SPREAD_PARAM].getValue();
			if (spreadConnected)
				spread += inputs[SPREAD_INPUT].getPolyVoltage(c) * 0.1f * params[SPREAD_ATT_PARAM].getValue();
			spread = math::clamp(spread, 0.f, 1.f);

			float target[NUM_SPEAKERS];
			computeGains(ring, azimuth, spread, target);
			for (int s = 0; s < NUM_SPEAKERS; s++) {
				if (c >= jumpFrom) {
					gainCur[c][s] = target[s];
					gainStep[c][s] = 0.f;
				}
				else {
					gainStep[c][s] = (target[s] - gainCur[c][s]) / steps;
				}
				maxGain[s] = std::max(maxGain[s], target[s]);
			}

			dispAzimuth[c] = azimuth;
			dispSpread[c] = spread;
		}
		for (int s = 0; s < NUM_SPEAKERS; s++)
			dispGain[s] = maxGain[s];
		dispChannels = channels;
	}

	void updateFilters(float sampleRate) {
		float f = math::clamp(lfeCutoff / sampleRate, 1e-5f, 0.49f);
		for (int c = 0; c < MAX_CHANNELS; c++)
			lfeFilter[c].setParameters(dsp::TBiquadFilter<float>::LOWPASS, f, 0.7071f, 1.f);
		filterSampleRate = sampleRate;
		filterCutoff = lfeCutoff;
	}

	void process(const ProcessArgs& args) override {
		const int channels = std::max(1, inputs[IN_INPUT].getChannels());

		if (args.sampleRate != filterSampleRate || lfeCutoff != filterCutoff)
			updateFilters(args.sampleRate);

		if (controlDivider.process() || channels != lastChannels) {
			// New voices (and the very first block) jump straight to their position
			int jumpFrom = (channels > lastChannels) ? lastChannels : channels;
			updateTargets(channels, jumpFrom);
			lastChannels = channels;
		}

		float x[MAX_CHANNELS];
		for (int c = 0; c < channels; c++)
			x[c] = inputs[IN_INPUT].getVoltage(c);

		// Full-range speakers
		for (int s = 0; s < NUM_SPEAKERS; s++) {
			Output& out = outputs[speakerChannel(s)];
			if (sumPoly) {
				float sum = 0.f;
				for (int c = 0; c < channels; c++)
					sum += x[c] * gainCur[c][s];
				out.setChannels(1);
				out.setVoltage(sum);
			}
			else {
				out.setChannels(channels);
				for (int c = 0; c < channels; c++)
					out.setVoltage(x[c] * gainCur[c][s], c);
			}
		}

		// Glide gains towards their targets
		for (int c = 0; c < channels; c++)
			for (int s = 0; s < NUM_SPEAKERS; s++)
				gainCur[c][s] += gainStep[c][s];

		// LFE: a low-passed send, independent of position
		const float lfe = params[LFE_PARAM].getValue();
		Output& lfeOut = outputs[LFE_OUTPUT];
		if (sumPoly) {
			float sum = 0.f;
			for (int c = 0; c < channels; c++)
				sum += lfeFilter[c].process(x[c]);
			lfeOut.setChannels(1);
			lfeOut.setVoltage(sum * lfe);
		}
		else {
			lfeOut.setChannels(channels);
			for (int c = 0; c < channels; c++)
				lfeOut.setVoltage(lfeFilter[c].process(x[c]) * lfe, c);
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "angles", anglesToJson(layout));
		json_object_set_new(rootJ, "useCentre", json_boolean(layout.enabled[SPK_C]));
		json_object_set_new(rootJ, "sumPoly", json_boolean(sumPoly));
		json_object_set_new(rootJ, "lfeCutoff", json_real(lfeCutoff));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		anglesFromJson(json_object_get(rootJ, "angles"), layout);
		json_t* centreJ = json_object_get(rootJ, "useCentre");
		if (centreJ)
			layout.enabled[SPK_C] = json_boolean_value(centreJ);
		json_t* sumJ = json_object_get(rootJ, "sumPoly");
		if (sumJ)
			sumPoly = json_boolean_value(sumJ);
		json_t* cutoffJ = json_object_get(rootJ, "lfeCutoff");
		if (cutoffJ && json_is_number(cutoffJ))
			lfeCutoff = math::clamp((float) json_number_value(cutoffJ), 20.f, 200.f);
	}
};


/** Top view of the room, drawn in white on the panel's black window.
Speakers fill up with their gain, an X marks each voice, and a dashed arc shows its spread. */
struct PanDisplay : widget::TransparentWidget {
	SurroundPanner71* module = NULL;

	static Vec polar(Vec centre, float radius, float degrees) {
		float r = degrees * float(M_PI) / 180.f;
		return Vec(centre.x + radius * std::sin(r), centre.y - radius * std::cos(r));
	}

	/** A dashed arc between two azimuths (degrees, clockwise from the top). */
	static void dashedArc(NVGcontext* vg, Vec centre, float radius, float from, float to, float dashDeg, float gapDeg) {
		for (float a = from; a < to; a += dashDeg + gapDeg) {
			float b = std::min(a + dashDeg, to);
			nvgBeginPath(vg);
			nvgArc(vg, centre.x, centre.y, radius,
				(a - 90.f) * float(M_PI) / 180.f, (b - 90.f) * float(M_PI) / 180.f, NVG_CW);
			nvgStroke(vg);
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1)
			drawScene(args);
		Widget::drawLayer(args, layer);
	}

	void drawScene(const DrawArgs& args) {
		NVGcontext* vg = args.vg;
		Vec centre = box.size.div(2.f);
		float ringR = mm2px(13.2f);
		float innerR = ringR * 0.62f;

		// Ring
		nvgStrokeColor(vg, PAPER);
		nvgStrokeWidth(vg, mm2px(0.3f));
		nvgLineCap(vg, NVG_BUTT);
		dashedArc(vg, centre, ringR, 0.f, 360.f, 6.f, 4.f);

		// Listener
		nvgBeginPath(vg);
		nvgMoveTo(vg, centre.x, centre.y - mm2px(2.f));
		nvgLineTo(vg, centre.x - mm2px(1.3f), centre.y + mm2px(1.1f));
		nvgLineTo(vg, centre.x + mm2px(1.3f), centre.y + mm2px(1.1f));
		nvgClosePath(vg);
		nvgFillColor(vg, PAPER);
		nvgFill(vg);

		// Voices
		int channels = module ? module->dispChannels : 1;
		for (int c = 0; c < channels; c++) {
			float az = module ? module->dispAzimuth[c] : 0.f;
			float spread = module ? module->dispSpread[c] : 0.f;
			if (spread > 0.001f) {
				float width = std::min(spread * 360.f, 360.f);
				nvgStrokeColor(vg, PAPER);
				nvgStrokeWidth(vg, mm2px(1.1f));
				dashedArc(vg, centre, innerR, az - width / 2.f, az + width / 2.f, 4.f, 4.f);
			}
			Vec p = polar(centre, innerR, az);
			float k = mm2px(1.2f);
			nvgBeginPath(vg);
			nvgMoveTo(vg, p.x - k, p.y - k);
			nvgLineTo(vg, p.x + k, p.y + k);
			nvgMoveTo(vg, p.x - k, p.y + k);
			nvgLineTo(vg, p.x + k, p.y - k);
			nvgStrokeColor(vg, PAPER);
			nvgStrokeWidth(vg, mm2px(0.7f));
			nvgLineCap(vg, NVG_ROUND);
			nvgStroke(vg);
		}

		// Speakers and their names
		std::shared_ptr<window::Font> font = loadFont("SpecialElite-Regular.ttf");
		for (int s = 0; s < NUM_SPEAKERS; s++) {
			float angle = module ? module->layout.angles[s] : DEFAULT_ANGLES[s];
			bool enabled = module ? module->layout.enabled[s] : true;
			float gain = module ? module->dispGain[s] : (s == SPK_C ? 1.f : 0.f);
			Vec p = polar(centre, ringR, angle);

			nvgBeginPath(vg);
			nvgCircle(vg, p.x, p.y, mm2px(1.7f));
			nvgFillColor(vg, INK);
			nvgFill(vg);
			nvgStrokeColor(vg, PAPER);
			nvgStrokeWidth(vg, mm2px(0.35f));
			if (enabled)
				nvgStroke(vg);
			else {
				// A disabled speaker is crossed out
				float k = mm2px(1.2f);
				nvgMoveTo(vg, p.x - k, p.y - k);
				nvgLineTo(vg, p.x + k, p.y + k);
				nvgStroke(vg);
			}
			if (enabled && gain > 0.01f) {
				nvgBeginPath(vg);
				nvgCircle(vg, p.x, p.y, mm2px(0.6f + 1.1f * math::clamp(gain, 0.f, 1.f)));
				nvgFillColor(vg, PAPER);
				nvgFill(vg);
			}

			if (font && font->handle >= 0) {
				Vec t = polar(centre, ringR + mm2px(4.2f), angle);
				nvgFontFaceId(vg, font->handle);
				nvgFontSize(vg, mm2px(2.6f));
				nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgFillColor(vg, PAPER);
				nvgText(vg, t.x, t.y, SPEAKER_SHORT[s], NULL);
			}
		}
	}
};


struct SurroundPanner71Widget : ModuleWidget {
	SurroundPanner71Widget(SurroundPanner71* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/SurroundPanner71.svg")));
		addScrews(this);

		// Room display, inside the black window of the panel
		PanDisplay* display = createWidget<PanDisplay>(mm2px(Vec(5.f, 18.5f)));
		display->box.size = mm2px(Vec(71.28f, 38.f));
		display->module = module;
		addChild(display);

		// Controls
		addParam(createParamCentered<PunkKnobLarge>(mm2px(Vec(16.f, 69.f)), module, SurroundPanner71::AZIMUTH_PARAM));
		addParam(createParamCentered<PunkKnob>(mm2px(Vec(40.64f, 69.f)), module, SurroundPanner71::SPREAD_PARAM));
		addParam(createParamCentered<PunkKnob>(mm2px(Vec(65.28f, 69.f)), module, SurroundPanner71::LFE_PARAM));

		addInput(createInputCentered<PunkJack>(mm2px(Vec(9.5f, 84.f)), module, SurroundPanner71::AZIMUTH_INPUT));
		addParam(createParamCentered<PunkTrimpot>(mm2px(Vec(21.5f, 84.f)), module, SurroundPanner71::AZIMUTH_ATT_PARAM));
		addInput(createInputCentered<PunkJack>(mm2px(Vec(34.f, 84.f)), module, SurroundPanner71::SPREAD_INPUT));
		addParam(createParamCentered<PunkTrimpot>(mm2px(Vec(46.f, 84.f)), module, SurroundPanner71::SPREAD_ATT_PARAM));
		addInput(createInputCentered<PunkJack>(mm2px(Vec(66.04f, 84.f)), module, SurroundPanner71::IN_INPUT));

		// Outputs in 7.1 channel order, two rows of four
		const float jackX[4] = {13.97f, 31.75f, 49.53f, 67.31f};
		for (int i = 0; i < 4; i++) {
			addOutput(createOutputCentered<PunkJackDark>(mm2px(Vec(jackX[i], 100.6f)), module, i));
			addOutput(createOutputCentered<PunkJackDark>(mm2px(Vec(jackX[i], 113.8f)), module, 4 + i));
		}
	}

	void appendContextMenu(Menu* menu) override {
		SurroundPanner71* module = getModule<SurroundPanner71>();

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Speaker layout"));
		menu->addChild(createSpeakerAnglesMenu(&module->layout));
		menu->addChild(createBoolPtrMenuItem("Use centre speaker for panning", "", &module->layout.enabled[SPK_C]));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Outputs"));
		menu->addChild(createBoolPtrMenuItem("Mix polyphonic voices to mono outputs", "", &module->sumPoly));

		menu->addChild(createSubmenuItem("LFE low-pass", string::f("%g Hz", module->lfeCutoff), [=](Menu* menu) {
			for (int i = 0; i < NUM_LFE_CUTOFFS; i++) {
				float cutoff = LFE_CUTOFFS[i];
				menu->addChild(createCheckMenuItem(string::f("%g Hz", cutoff), "",
					[=]() { return module->lfeCutoff == cutoff; },
					[=]() { module->lfeCutoff = cutoff; }
				));
			}
		}));
	}
};


Model* modelSurroundPanner71 = createModel<SurroundPanner71, SurroundPanner71Widget>("SurroundPanner71");
