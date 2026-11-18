// Auralloy - visual identity: tokens, logo, aurora background, motion helpers and the LookAndFeel (see DESIGN.md)
#pragma once
#include "Core.h"

namespace al
{
namespace col
{
    const juce::Colour obsidian950 { 0xff05060a };
    const juce::Colour obsidian900 { 0xff0a0c14 };
    const juce::Colour graphite800 { 0xff11131d };
    const juce::Colour graphite700 { 0xff1a1d2b };
    const juce::Colour mint   { 0xff5cf2d6 };
    const juce::Colour violet { 0xff9b8cff };
    const juce::Colour rose   { 0xffff6fb5 };
    const juce::Colour amber  { 0xffffc857 };
    const juce::Colour sky    { 0xff5ab8ff };
    const juce::Colour danger { 0xffff5c7a };
    const juce::Colour text100 { 0xffedeff8 };
    const juce::Colour text300 { 0xffa9aec4 };
    const juce::Colour text500 { 0xff6f748c };
}

juce::Colour sourceColour (int index);               // palette for songs in a mashup
juce::Colour roleColour (int role);                  // Vocals rose, Drums amber, Bass violet, Other mint, Mix sky
juce::Font uiFont (float size, bool semibold = false);
juce::Font monoFont (float size);
juce::String formatValue (Fmt f, double v);
double parseValue (Fmt f, const juce::String& t);
const PDef* findDef (const char* id);

float easeOutExpo (float t);
float easeInOut (float t);
float clamp01 (float t);

// exponential smoothing for hover / reactive values
struct Smooth
{
    float v = 0.0f;
    void step (float target, float dt, float tau) { v += (target - v) * (1.0f - std::exp (-dt / std::max (1e-4f, tau))); }
};

juce::ColourGradient alloyGradient (float x1, float y1, float x2, float y2);   // mint -> violet -> rose
void drawPanel (juce::Graphics&, juce::Rectangle<float>, float corner, juce::Colour tint = juce::Colours::white, float strength = 1.0f);
void drawAurora (juce::Graphics&, juce::Rectangle<float> area, double seconds, float reactive);

// The mark: two legs (two songs) fuse into an "A"; the waveform bars are the crossbar. Same geometry as assets/logo.svg.
struct LogoState
{
    float legs = 1.0f;         // draw-in progress of the legs, 0..1 (bottom -> apex)
    float spread = 1.0f;       // horizontal separation of the legs before they fuse (1 = fused A)
    float apex = 1.0f;         // brightness of the fused apex
    float bars = 1.0f;         // waveform bars growth 0..1
    float wave = 0.0f;         // phase for dancing bars (busy states)
    float pulse = 0.0f;        // 0..1 audio-reactive glow
    float glow = 0.35f;
    float alpha = 1.0f;
};
void drawLogoMark (juce::Graphics&, juce::Rectangle<float> bounds, const LogoState& s);

// geometric monoline wordmark "AURALLOY" (custom letterforms, no font needed); progress reveals it letter by letter
float wordmarkWidth (float height);
void drawWordmark (juce::Graphics&, juce::Rectangle<float> area, float progress = 1.0f, float sweep = -1.0f);

class AlloyLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AlloyLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

void applyDarkTitleBar (juce::Component&);
} // namespace al
