#include "plugin.hpp"
#include "Common.hpp"
#include "Binaural.hpp"

using namespace noiseminded;


/** Previews a 7.1 mix on headphones: each speaker feed is rendered from its direction
with measured head-related impulse responses (MIT KEMAR), then summed to stereo. */
struct HeadphoneMonitor71 : Module {
	enum ParamId {
		LEVEL_PARAM,
		PARAMS_LEN
	};
	// Inputs follow the common 7.1 channel order (WAV / Windows): L R C LFE BL BR SL SR
	enum InputId {
		L_INPUT,
		R_INPUT,
		C_INPUT,
		LFE_INPUT,
		BL_INPUT,
		BR_INPUT,
		SL_INPUT,
		SR_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		LEFT_OUTPUT,
		RIGHT_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	Layout layout;
	BinauralRenderer renderer;
	dsp::ClockDivider configDivider;

	HeadphoneMonitor71() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// 0 to 2 (+6 dB), shown in dB
		configParam(LEVEL_PARAM, 0.f, 2.f, 1.f, "Level", " dB", -10.f, 20.f);

		for (int s = 0; s < NUM_SPEAKERS; s++)
			configInput(speakerChannel(s), SPEAKER_LONG[s]);
		configInput(LFE_INPUT, "LFE");

		configOutput(LEFT_OUTPUT, "Headphones left");
		configOutput(RIGHT_OUTPUT, "Headphones right");

		configDivider.setDivision(512);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		layout = Layout();
	}

	void process(const ProcessArgs& args) override {
		// Pick up sample rate and angle changes (cheap check, rebuilt only when something changed)
		if (renderer.taps == 0 || configDivider.process())
			renderer.configure(args.sampleRate, layout.angles);

		float speakers[NUM_SPEAKERS];
		for (int s = 0; s < NUM_SPEAKERS; s++)
			speakers[s] = inputs[speakerChannel(s)].getVoltageSum();
		float lfe = inputs[LFE_INPUT].getVoltageSum();

		float left, right;
		renderer.process(speakers, lfe, left, right);

		float level = params[LEVEL_PARAM].getValue();
		outputs[LEFT_OUTPUT].setVoltage(left * level);
		outputs[RIGHT_OUTPUT].setVoltage(right * level);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "angles", anglesToJson(layout));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		anglesFromJson(json_object_get(rootJ, "angles"), layout);
	}
};


struct HeadphoneMonitor71Widget : ModuleWidget {
	HeadphoneMonitor71Widget(HeadphoneMonitor71* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/HeadphoneMonitor71.svg")));
		addScrews(this);

		// Inputs in pairs: L R, C LFE, BL BR, SL SR
		const float colX[2] = {14.0f, 36.8f};
		for (int row = 0; row < 4; row++) {
			float y = 59.f + row * 11.6f;
			addInput(createInputCentered<PunkJack>(mm2px(Vec(colX[0], y)), module, 2 * row));
			addInput(createInputCentered<PunkJack>(mm2px(Vec(colX[1], y)), module, 2 * row + 1));
		}

		addOutput(createOutputCentered<PunkJackDark>(mm2px(Vec(10.6f, 112.6f)), module, HeadphoneMonitor71::LEFT_OUTPUT));
		addParam(createParamCentered<PunkKnob>(mm2px(Vec(25.4f, 112.8f)), module, HeadphoneMonitor71::LEVEL_PARAM));
		addOutput(createOutputCentered<PunkJackDark>(mm2px(Vec(40.2f, 112.6f)), module, HeadphoneMonitor71::RIGHT_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		HeadphoneMonitor71* module = getModule<HeadphoneMonitor71>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Virtual speaker layout"));
		menu->addChild(createSpeakerAnglesMenu(&module->layout));
		menu->addChild(createMenuLabel("Match the angles set on the Surround Panner"));
	}
};


Model* modelHeadphoneMonitor71 = createModel<HeadphoneMonitor71, HeadphoneMonitor71Widget>("HeadphoneMonitor71");
