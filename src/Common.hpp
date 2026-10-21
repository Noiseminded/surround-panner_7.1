#pragma once
// Things shared by the Noiseminded modules: speaker names, the speaker-angle menu,
// and the black-and-white panel components.

#include "plugin.hpp"
#include "PanLaw.hpp"


namespace noiseminded {

using namespace surround;


static const char* const SPEAKER_SHORT[NUM_SPEAKERS] = {"L", "R", "C", "BL", "BR", "SL", "SR"};
static const char* const SPEAKER_LONG[NUM_SPEAKERS] = {
	"Front left (L)",
	"Front right (R)",
	"Centre (C)",
	"Back left (BL)",
	"Back right (BR)",
	"Side left (SL)",
	"Side right (SR)",
};

/** Ports in the common 7.1 channel order (WAV / Windows): L R C LFE BL BR SL SR. */
enum Channel71 {
	CH_L,
	CH_R,
	CH_C,
	CH_LFE,
	CH_BL,
	CH_BR,
	CH_SL,
	CH_SR,
	NUM_CHANNELS_71
};

/** The 7.1 port of each full-range speaker. */
inline int speakerChannel(int speaker) {
	static const int map[NUM_SPEAKERS] = {CH_L, CH_R, CH_C, CH_BL, CH_BR, CH_SL, CH_SR};
	return map[speaker];
}


// Panel colours (the panels are drawn in tools/make_panels.py with the same values)
static const NVGcolor INK = nvgRGB(0x11, 0x11, 0x11);
static const NVGcolor PAPER = nvgRGB(0xef, 0xed, 0xe7);


/** Loads one of the plugin's fonts for drawing text in a widget. */
inline std::shared_ptr<window::Font> loadFont(const char* name) {
	return APP->window->loadFont(asset::plugin(pluginInstance, std::string("res/fonts/") + name));
}


// ---------------------------------------------------------------- speaker angle menu

struct AngleQuantity : Quantity {
	Layout* layout;
	int speaker;

	AngleQuantity(Layout* layout, int speaker) : layout(layout), speaker(speaker) {}

	void setValue(float value) override {
		layout->angles[speaker] = math::clamp(value, getMinValue(), getMaxValue());
	}
	float getValue() override {
		return layout->angles[speaker];
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
	AngleSlider(Layout* layout, int speaker) {
		quantity = new AngleQuantity(layout, speaker);
		box.size.x = 240.f;
	}
	~AngleSlider() {
		delete quantity;
	}
};

/** "Speaker angles" submenu: one slider per speaker plus two presets. */
inline ui::MenuItem* createSpeakerAnglesMenu(Layout* layout) {
	return createSubmenuItem("Speaker angles", "", [=](ui::Menu* menu) {
		for (int s = 0; s < NUM_SPEAKERS; s++)
			menu->addChild(new AngleSlider(layout, s));
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuItem("Reset to 7.1 (ITU / Dolby)", "", [=]() {
			layout->setDefaultAngles();
		}));
		menu->addChild(createMenuItem("Even circle (7 × 51.4°)", "", [=]() {
			layout->setEvenAngles();
		}));
	});
}

inline json_t* anglesToJson(const Layout& layout) {
	json_t* anglesJ = json_array();
	for (int s = 0; s < NUM_SPEAKERS; s++)
		json_array_append_new(anglesJ, json_real(layout.angles[s]));
	return anglesJ;
}

inline void anglesFromJson(json_t* anglesJ, Layout& layout) {
	if (!anglesJ || !json_is_array(anglesJ))
		return;
	for (int s = 0; s < NUM_SPEAKERS && s < (int) json_array_size(anglesJ); s++) {
		json_t* aJ = json_array_get(anglesJ, s);
		if (json_is_number(aJ))
			layout.angles[s] = math::clamp((float) json_number_value(aJ), -180.f, 180.f);
	}
}


// ---------------------------------------------------------------- panel components

struct PunkKnobLarge : app::SvgKnob {
	PunkKnobLarge() {
		minAngle = -0.8333f * float(M_PI);
		maxAngle = 0.8333f * float(M_PI);
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/KnobLarge.svg")));
		shadow->opacity = 0.f;
	}
};

struct PunkKnob : app::SvgKnob {
	PunkKnob() {
		minAngle = -0.8333f * float(M_PI);
		maxAngle = 0.8333f * float(M_PI);
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/Knob.svg")));
		shadow->opacity = 0.f;
	}
};

struct PunkTrimpot : app::SvgKnob {
	PunkTrimpot() {
		minAngle = -0.75f * float(M_PI);
		maxAngle = 0.75f * float(M_PI);
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/Trimpot.svg")));
		shadow->opacity = 0.f;
	}
};

struct PunkJack : app::SvgPort {
	PunkJack() {
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/Jack.svg")));
		shadow->opacity = 0.f;
	}
};

/** Jack for the black output blocks. */
struct PunkJackDark : app::SvgPort {
	PunkJackDark() {
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/JackDark.svg")));
		shadow->opacity = 0.f;
	}
};

struct PunkScrew : app::SvgScrew {
	PunkScrew() {
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/Screw.svg")));
	}
};

/** Adds the four corner screws to a module widget. */
inline void addScrews(app::ModuleWidget* mw) {
	mw->addChild(createWidget<PunkScrew>(Vec(RACK_GRID_WIDTH, 0)));
	mw->addChild(createWidget<PunkScrew>(Vec(mw->box.size.x - 2 * RACK_GRID_WIDTH, 0)));
	mw->addChild(createWidget<PunkScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	mw->addChild(createWidget<PunkScrew>(Vec(mw->box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
}


} // namespace noiseminded
