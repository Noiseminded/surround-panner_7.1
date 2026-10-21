#include <atomic>

#include "plugin.hpp"
#include "Common.hpp"
#include "PlateReverb.hpp"

using namespace noiseminded;


/** A plate reverb on the whole 7.1 bus. The 8 channels come in, each full-range speaker gets its
own decorrelated reverb tail, and dry plus wet go out in the same order. The LFE passes dry. */
struct SurroundReverb71 : Module {
	enum ParamId {
		PREDELAY_PARAM,
		SIZE_PARAM,
		DECAY_PARAM,
		DIFFUSION_PARAM,
		LOWCUT_PARAM,
		HIGHCUT_PARAM,
		MODSPEED_PARAM,
		MODDEPTH_PARAM,
		DEPTH_PARAM,
		REAR_PARAM,
		DRY_PARAM,
		WET_PARAM,
		FREEZE_PARAM,
		PARAMS_LEN
	};
	// Inputs 0 to 7 are the 7.1 bus in channel order: L R C LFE BL BR SL SR
	enum InputId {
		SIZE_CV_INPUT = NUM_CHANNELS_71,
		DECAY_CV_INPUT,
		HIGHCUT_CV_INPUT,
		MOD_CV_INPUT,
		WET_CV_INPUT,
		FREEZE_INPUT,
		INPUTS_LEN
	};
	// Outputs 0 to 7: the 7.1 bus in channel order
	enum OutputId {
		OUTPUTS_LEN = NUM_CHANNELS_71
	};
	enum LightId {
		LIGHTS_LEN
	};

	PlateReverb7 reverb;
	PlateParams plate;
	dsp::ClockDivider paramDivider;
	dsp::SchmittTrigger freezeGate;
	bool freezeGateHigh = false;
	float dry = 1.f;
	float wet = 0.5f;
	/** Set from the UI thread, handled by the engine so the buffers are never touched from two threads. */
	std::atomic<bool> clearRequested{false};

	SurroundReverb71() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(PREDELAY_PARAM, 0.f, 500.f, 20.f, "Pre-delay", " ms");
		configParam(SIZE_PARAM, 0.25f, 2.f, 1.f, "Size", "%", 0.f, 100.f);
		configParam(DECAY_PARAM, 0.f, 0.9999f, 0.7f, "Decay", "%", 0.f, 100.f);
		configParam(DIFFUSION_PARAM, 0.f, 1.f, 1.f, "Diffusion", "%", 0.f, 100.f);
		// Exponential knobs: shown in Hz
		configParam(LOWCUT_PARAM, 0.f, 1.f, 0.3544f, "Low cut", " Hz", 50.f, 20.f);
		configParam(HIGHCUT_PARAM, 0.f, 1.f, 0.7516f, "High cut", " Hz", 40.f, 500.f);
		configParam(MODSPEED_PARAM, 0.f, 1.f, 0.5f, "Modulation speed", " Hz", 100.f, 0.05f);
		configParam(MODDEPTH_PARAM, 0.f, 1.f, 0.5f, "Modulation depth", "%", 0.f, 100.f);
		configParam(DEPTH_PARAM, 0.f, 1.f, 0.5f, "Depth (extra pre-delay towards the back)", "%", 0.f, 100.f);
		configParam(REAR_PARAM, 0.f, 1.f, 0.5f, "Front / rear balance", "%", 0.f, 100.f);
		configParam(DRY_PARAM, 0.f, 1.f, 1.f, "Dry", "%", 0.f, 100.f);
		configParam(WET_PARAM, 0.f, 1.f, 0.5f, "Wet", "%", 0.f, 100.f);
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze", {"Off", "On"});

		static const char* const busNames[NUM_CHANNELS_71] = {
			"Front left (L)", "Front right (R)", "Centre (C)", "LFE",
			"Back left (BL)", "Back right (BR)", "Side left (SL)", "Side right (SR)",
		};
		for (int i = 0; i < NUM_CHANNELS_71; i++) {
			configInput(i, busNames[i]);
			configOutput(i, busNames[i]);
			configBypass(i, i);
		}
		configInput(SIZE_CV_INPUT, "Size CV");
		configInput(DECAY_CV_INPUT, "Decay CV");
		configInput(HIGHCUT_CV_INPUT, "High cut CV");
		configInput(MOD_CV_INPUT, "Modulation depth CV");
		configInput(WET_CV_INPUT, "Wet CV");
		configInput(FREEZE_INPUT, "Freeze gate");

		paramDivider.setDivision(16);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		clearRequested = true;
	}

	void updateParams() {
		auto cv = [&](int input) {
			return inputs[input].getVoltage() * 0.1f;  // 10 V = full range
		};
		plate.preDelayMs = params[PREDELAY_PARAM].getValue();
		plate.size = math::clamp(params[SIZE_PARAM].getValue() + cv(SIZE_CV_INPUT) * 1.75f, 0.25f, 2.f);
		plate.decay = math::clamp(params[DECAY_PARAM].getValue() + cv(DECAY_CV_INPUT), 0.f, 0.9999f);
		plate.diffusion = params[DIFFUSION_PARAM].getValue();
		plate.lowCutHz = 20.f * std::pow(50.f, params[LOWCUT_PARAM].getValue());
		float highCut = math::clamp(params[HIGHCUT_PARAM].getValue() + cv(HIGHCUT_CV_INPUT), 0.f, 1.f);
		plate.highCutHz = 500.f * std::pow(40.f, highCut);
		plate.modSpeedHz = 0.05f * std::pow(100.f, params[MODSPEED_PARAM].getValue());
		plate.modDepth = math::clamp(params[MODDEPTH_PARAM].getValue() + cv(MOD_CV_INPUT), 0.f, 1.f);
		plate.depth = params[DEPTH_PARAM].getValue();
		plate.rear = params[REAR_PARAM].getValue();
		dry = params[DRY_PARAM].getValue();
		wet = math::clamp(params[WET_PARAM].getValue() + cv(WET_CV_INPUT), 0.f, 1.f);
	}

	void process(const ProcessArgs& args) override {
		if (args.sampleRate != reverb.sampleRate)
			reverb.init(args.sampleRate);
		if (clearRequested.exchange(false))
			reverb.clear();
		if (paramDivider.process())
			updateParams();

		// Freeze: the button, or a high gate
		if (inputs[FREEZE_INPUT].isConnected()) {
			freezeGate.process(inputs[FREEZE_INPUT].getVoltage(), 0.1f, 1.f);
			freezeGateHigh = freezeGate.isHigh();
		}
		else {
			freezeGateHigh = false;
		}
		plate.freeze = params[FREEZE_PARAM].getValue() > 0.5f || freezeGateHigh;

		float in[NUM_CHANNELS_71];
		for (int i = 0; i < NUM_CHANNELS_71; i++)
			in[i] = inputs[i].getVoltageSum();

		// Each half of the tank hears its side of the room; the centre feeds both
		float left = 0.7f * (in[CH_L] + in[CH_SL] + in[CH_BL] + 0.5f * in[CH_C]);
		float right = 0.7f * (in[CH_R] + in[CH_SR] + in[CH_BR] + 0.5f * in[CH_C]);

		float rev[NUM_SPEAKERS];
		reverb.process(left, right, plate, rev);

		for (int s = 0; s < NUM_SPEAKERS; s++) {
			int ch = speakerChannel(s);
			outputs[ch].setVoltage(dry * in[ch] + wet * rev[s]);
		}
		// No reverb in the LFE: low reverb tails just turn to mud on a subwoofer
		outputs[CH_LFE].setVoltage(dry * in[CH_LFE]);
	}
};


struct PunkLatchButton : app::SvgSwitch {
	PunkLatchButton() {
		momentary = false;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/ButtonOff.svg")));
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/components/ButtonOn.svg")));
		shadow->opacity = 0.f;
	}
};


struct SurroundReverb71Widget : ModuleWidget {
	SurroundReverb71Widget(SurroundReverb71* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/SurroundReverb71.svg")));
		addScrews(this);

		// Positions match tools/make_panels.py
		for (int i = 0; i < NUM_CHANNELS_71; i++) {
			float x = 10.2f + i * 11.6f;
			addInput(createInputCentered<PunkJack>(mm2px(Vec(x, 28.6f)), module, i));
			addOutput(createOutputCentered<PunkJackDark>(mm2px(Vec(x, 111.4f)), module, i));
		}

		const float knobX[4] = {14.5f, 38.2f, 63.4f, 87.1f};
		const float knobY[3] = {45.f, 62.f, 79.f};
		const int knobs[3][4] = {
			{SurroundReverb71::PREDELAY_PARAM, SurroundReverb71::SIZE_PARAM, SurroundReverb71::DECAY_PARAM, SurroundReverb71::DIFFUSION_PARAM},
			{SurroundReverb71::LOWCUT_PARAM, SurroundReverb71::HIGHCUT_PARAM, SurroundReverb71::MODSPEED_PARAM, SurroundReverb71::MODDEPTH_PARAM},
			{SurroundReverb71::DEPTH_PARAM, SurroundReverb71::REAR_PARAM, SurroundReverb71::DRY_PARAM, SurroundReverb71::WET_PARAM},
		};
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 4; c++)
				addParam(createParamCentered<PunkKnob>(mm2px(Vec(knobX[c], knobY[r])), module, knobs[r][c]));

		const float cvX[6] = {10.f, 23.f, 36.f, 49.f, 62.f, 76.f};
		const int cvInputs[6] = {
			SurroundReverb71::SIZE_CV_INPUT, SurroundReverb71::DECAY_CV_INPUT, SurroundReverb71::HIGHCUT_CV_INPUT,
			SurroundReverb71::MOD_CV_INPUT, SurroundReverb71::WET_CV_INPUT, SurroundReverb71::FREEZE_INPUT,
		};
		for (int i = 0; i < 6; i++)
			addInput(createInputCentered<PunkJack>(mm2px(Vec(cvX[i], 93.2f)), module, cvInputs[i]));
		addParam(createParamCentered<PunkLatchButton>(mm2px(Vec(90.f, 93.2f)), module, SurroundReverb71::FREEZE_PARAM));
	}

	void appendContextMenu(Menu* menu) override {
		SurroundReverb71* module = getModule<SurroundReverb71>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Clear the reverb tail", "", [=]() {
			module->clearRequested = true;
		}));
	}
};


Model* modelSurroundReverb71 = createModel<SurroundReverb71, SurroundReverb71Widget>("SurroundReverb71");
