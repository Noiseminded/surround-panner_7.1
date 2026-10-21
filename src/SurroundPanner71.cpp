#include "plugin.hpp"
#include "PanLaw.hpp"

using namespace surround;


static const int MAX_CHANNELS = 16;

static const char* SPEAKER_SHORT[NUM_SPEAKERS] = {"L", "R", "C", "BL", "BR", "SL", "SR"};
static const char* SPEAKER_LONG[NUM_SPEAKERS] = {
	"Front left (L)",
	"Front right (R)",
	"Centre (C)",
	"Back left (BL)",
	"Back right (BR)",
	"Side left (SL)",
	"Side right (SR)",
};

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

	/** Output port for each full-range speaker. */
	static int speakerOutput(int speaker) {
		static const int map[NUM_SPEAKERS] = {L_OUTPUT, R_OUTPUT, C_OUTPUT, BL_OUTPUT, BR_OUTPUT, SL_OUTPUT, SR_OUTPUT};
		return map[speaker];
	}

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
			configOutput(speakerOutput(s), SPEAKER_LONG[s]);
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

	/** Recomputes the target gain of every voice. With `jumpFrom`, voices from that index on snap to their target instead of gliding. */
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
			Output& out = outputs[speakerOutput(s)];
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
		json_t* anglesJ = json_array();
		for (int s = 0; s < NUM_SPEAKERS; s++)
			json_array_append_new(anglesJ, json_real(layout.angles[s]));
		json_object_set_new(rootJ, "angles", anglesJ);
		json_object_set_new(rootJ, "useCentre", json_boolean(layout.enabled[SPK_C]));
		json_object_set_new(rootJ, "sumPoly", json_boolean(sumPoly));
		json_object_set_new(rootJ, "lfeCutoff", json_real(lfeCutoff));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* anglesJ = json_object_get(rootJ, "angles");
		if (anglesJ && json_is_array(anglesJ)) {
			for (int s = 0; s < NUM_SPEAKERS && s < (int) json_array_size(anglesJ); s++) {
				json_t* aJ = json_array_get(anglesJ, s);
				if (json_is_number(aJ))
					layout.angles[s] = math::clamp((float) json_number_value(aJ), -180.f, 180.f);
			}
		}
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


// Context menu slider for one speaker angle

struct AngleQuantity : Quantity {
	SurroundPanner71* module;
	int speaker;

	AngleQuantity(SurroundPanner71* module, int speaker) : module(module), speaker(speaker) {}

	void setValue(float value) override {
		module->layout.angles[speaker] = math::clamp(value, getMinValue(), getMaxValue());
	}
	float getValue() override {
		return module->layout.angles[speaker];
	}
	float getMinValue() override {
		return -180.f;
	}
	float getMaxValue() override {
		return 180.f;
	}
	float getDefaultValue() override {
		return DEFAULT_ANGLES[speaker];
	}
	int getDisplayPrecision() override {
		return 4;
	}
	std::string getLabel() override {
		return SPEAKER_LONG[speaker];
	}
	std::string getUnit() override {
		return "°";
	}
};

struct AngleSlider : ui::Slider {
	AngleSlider(SurroundPanner71* module, int speaker) {
		quantity = new AngleQuantity(module, speaker);
		box.size.x = 240.f;
	}
	~AngleSlider() {
		delete quantity;
	}
};


// Colours shared by the panel widgets
static const NVGcolor COLOR_TEXT = nvgRGB(0xe6, 0xe1, 0xd6);
static const NVGcolor COLOR_MUTED = nvgRGB(0x8a, 0x94, 0xa3);
static const NVGcolor COLOR_ACCENT = nvgRGB(0xe2, 0x79, 0x3a);
static const NVGcolor COLOR_SPEAKER_OFF = nvgRGB(0x3a, 0x44, 0x56);


/** Static text on the panel, drawn with Rack's built-in font (no text-to-path conversion needed in the SVG). */
struct PanelLabels : widget::TransparentWidget {
	struct Label {
		float x, y, size;
		const char* text;
		NVGcolor color;
	};
	std::vector<Label> labels;

	void add(float xMm, float yMm, float size, const char* text, NVGcolor color = COLOR_TEXT) {
		Label l;
		l.x = xMm;
		l.y = yMm;
		l.size = size;
		l.text = text;
		l.color = color;
		labels.push_back(l);
	}

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
		if (!font || font->handle < 0)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (const Label& l : labels) {
			nvgFontSize(args.vg, l.size);
			nvgFillColor(args.vg, l.color);
			Vec p = mm2px(Vec(l.x, l.y));
			nvgText(args.vg, p.x, p.y, l.text, NULL);
		}
	}
};


/** Top view of the room: speakers light up with their gain, orange dots show each voice and its spread. */
struct PanDisplay : widget::TransparentWidget {
	SurroundPanner71* module = NULL;

	Vec polar(Vec centre, float radius, float degrees) {
		float r = degrees * float(M_PI) / 180.f;
		return Vec(centre.x + radius * std::sin(r), centre.y - radius * std::cos(r));
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1)
			drawScene(args);
		Widget::drawLayer(args, layer);
	}

	void drawScene(const DrawArgs& args) {
		NVGcontext* vg = args.vg;
		Vec centre = box.size.div(2.f);
		float ringR = mm2px(15.5f);

		// Ring
		nvgBeginPath(vg);
		nvgCircle(vg, centre.x, centre.y, ringR);
		nvgStrokeColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x30));
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);

		// Listener
		nvgBeginPath(vg);
		nvgMoveTo(vg, centre.x, centre.y - mm2px(2.2f));
		nvgLineTo(vg, centre.x - mm2px(1.4f), centre.y + mm2px(1.2f));
		nvgLineTo(vg, centre.x + mm2px(1.4f), centre.y + mm2px(1.2f));
		nvgClosePath(vg);
		nvgFillColor(vg, COLOR_MUTED);
		nvgFill(vg);

		// Voices
		int channels = module ? module->dispChannels : 1;
		float innerR = ringR * 0.62f;
		for (int c = 0; c < channels; c++) {
			float az = module ? module->dispAzimuth[c] : 0.f;
			float spread = module ? module->dispSpread[c] : 0.f;
			if (spread > 0.001f) {
				float width = spread * 360.f;
				nvgBeginPath(vg);
				if (width >= 359.f) {
					nvgCircle(vg, centre.x, centre.y, innerR);
				}
				else {
					float a0 = (az - width / 2.f - 90.f) * float(M_PI) / 180.f;
					float a1 = (az + width / 2.f - 90.f) * float(M_PI) / 180.f;
					nvgArc(vg, centre.x, centre.y, innerR, a0, a1, NVG_CW);
				}
				nvgStrokeColor(vg, nvgTransRGBAf(COLOR_ACCENT, 0.55f));
				nvgStrokeWidth(vg, mm2px(1.2f));
				nvgLineCap(vg, NVG_ROUND);
				nvgStroke(vg);
			}
			Vec p = polar(centre, innerR, az);
			nvgBeginPath(vg);
			nvgCircle(vg, p.x, p.y, mm2px(1.3f));
			nvgFillColor(vg, COLOR_ACCENT);
			nvgFill(vg);
		}

		// Speakers and their names
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
		for (int s = 0; s < NUM_SPEAKERS; s++) {
			float angle = module ? module->layout.angles[s] : DEFAULT_ANGLES[s];
			bool enabled = module ? module->layout.enabled[s] : true;
			float gain = module ? module->dispGain[s] : (s == SPK_C ? 1.f : 0.f);
			Vec p = polar(centre, ringR, angle);

			nvgBeginPath(vg);
			nvgCircle(vg, p.x, p.y, mm2px(1.9f));
			if (enabled) {
				nvgFillColor(vg, nvgLerpRGBA(COLOR_SPEAKER_OFF, COLOR_ACCENT, math::clamp(gain, 0.f, 1.f)));
				nvgFill(vg);
			}
			else {
				nvgStrokeColor(vg, COLOR_SPEAKER_OFF);
				nvgStrokeWidth(vg, 1.f);
				nvgStroke(vg);
			}

			if (font && font->handle >= 0) {
				Vec t = polar(centre, ringR + mm2px(4.6f), angle);
				nvgFontFaceId(vg, font->handle);
				nvgFontSize(vg, 8.f);
				nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgFillColor(vg, enabled ? COLOR_TEXT : COLOR_MUTED);
				nvgText(vg, t.x, t.y, SPEAKER_SHORT[s], NULL);
			}
		}
	}
};


struct SurroundPanner71Widget : ModuleWidget {
	SurroundPanner71Widget(SurroundPanner71* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/SurroundPanner71.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Text
		PanelLabels* labels = createWidget<PanelLabels>(Vec(0, 0));
		labels->box.size = box.size;
		labels->add(40.64f, 7.2f, 10.f, "SURROUND PANNER 7.1");
		labels->add(16.f, 58.6f, 8.f, "AZIMUTH");
		labels->add(40.64f, 58.6f, 8.f, "SPREAD");
		labels->add(65.28f, 58.6f, 8.f, "LFE SEND");
		labels->add(9.5f, 74.6f, 7.f, "CV", COLOR_MUTED);
		labels->add(21.5f, 74.6f, 7.f, "AMT", COLOR_MUTED);
		labels->add(34.f, 74.6f, 7.f, "CV", COLOR_MUTED);
		labels->add(46.f, 74.6f, 7.f, "AMT", COLOR_MUTED);
		labels->add(66.04f, 74.6f, 8.f, "IN", COLOR_ACCENT);
		static const char* row1[4] = {"L", "R", "C", "LFE"};
		static const char* row2[4] = {"BL", "BR", "SL", "SR"};
		static const char* nums1[4] = {"1", "2", "3", "4"};
		static const char* nums2[4] = {"5", "6", "7", "8"};
		const float jackX[4] = {13.97f, 31.75f, 49.53f, 67.31f};
		for (int i = 0; i < 4; i++) {
			labels->add(jackX[i], 92.2f, 8.f, row1[i]);
			labels->add(jackX[i] + 5.6f, 92.2f, 6.f, nums1[i], COLOR_MUTED);
			labels->add(jackX[i], 106.4f, 8.f, row2[i]);
			labels->add(jackX[i] + 5.6f, 106.4f, 6.f, nums2[i], COLOR_MUTED);
		}
		labels->add(40.64f, 123.0f, 8.f, "NOISEMINDED", COLOR_MUTED);
		addChild(labels);

		// Room display
		PanDisplay* display = createWidget<PanDisplay>(mm2px(Vec(5.f, 11.f)));
		display->box.size = mm2px(Vec(71.28f, 43.f));
		display->module = module;
		addChild(display);

		// Controls
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(16.f, 66.f)), module, SurroundPanner71::AZIMUTH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(40.64f, 66.f)), module, SurroundPanner71::SPREAD_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(65.28f, 66.f)), module, SurroundPanner71::LFE_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.5f, 81.f)), module, SurroundPanner71::AZIMUTH_INPUT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(21.5f, 81.f)), module, SurroundPanner71::AZIMUTH_ATT_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(34.f, 81.f)), module, SurroundPanner71::SPREAD_INPUT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(46.f, 81.f)), module, SurroundPanner71::SPREAD_ATT_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(66.04f, 81.f)), module, SurroundPanner71::IN_INPUT));

		// Outputs in 7.1 channel order
		const int row1Out[4] = {SurroundPanner71::L_OUTPUT, SurroundPanner71::R_OUTPUT, SurroundPanner71::C_OUTPUT, SurroundPanner71::LFE_OUTPUT};
		const int row2Out[4] = {SurroundPanner71::BL_OUTPUT, SurroundPanner71::BR_OUTPUT, SurroundPanner71::SL_OUTPUT, SurroundPanner71::SR_OUTPUT};
		for (int i = 0; i < 4; i++) {
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(jackX[i], 98.5f)), module, row1Out[i]));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(jackX[i], 112.7f)), module, row2Out[i]));
		}
	}

	void appendContextMenu(Menu* menu) override {
		SurroundPanner71* module = getModule<SurroundPanner71>();

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Speaker layout"));

		menu->addChild(createSubmenuItem("Speaker angles", "", [=](Menu* menu) {
			for (int s = 0; s < NUM_SPEAKERS; s++)
				menu->addChild(new AngleSlider(module, s));
			menu->addChild(new MenuSeparator);
			menu->addChild(createMenuItem("Reset to 7.1 (ITU / Dolby)", "", [=]() {
				module->layout.setDefaultAngles();
			}));
			menu->addChild(createMenuItem("Even circle (7 × 51.4°)", "", [=]() {
				module->layout.setEvenAngles();
			}));
		}));

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
