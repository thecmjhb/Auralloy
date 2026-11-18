#pragma once
#include "Theme.h"

namespace al
{
// FL Studio style control: hover + wheel nudges the value in small steps (Shift = fine), vertical drag, double-click resets.
class FlSlider final : public juce::Slider
{
public:
    FlSlider() { setScrollWheelEnabled (true); setMouseDragSensitivity (220); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        juce::MouseWheelDetails scaled (w);
        const float k = e.mods.isShiftDown() ? 0.06f : 0.30f;       // JUCE's default is 7 % per notch: far too coarse
        scaled.deltaX *= k; scaled.deltaY *= k;
        Slider::mouseWheelMove (e, scaled);
    }
};

class IconButton final : public juce::Button
{
public:
    enum Icon { Prev, Play, Pause, Stop, Next, Split, Duplicate, Delete, Loop };
    IconButton (Icon i, juce::Colour t) : Button ({}), icon (i), tint (t) {}
    void setIcon (Icon i) { if (i != icon) { icon = i; repaint(); } }
    void setActive (bool a) { if (a != active) { active = a; repaint(); } }
    void paintButton (juce::Graphics&, bool hl, bool down) override;
private:
    Icon icon;
    juce::Colour tint;
    bool active = false;
};

// segmented control with an animated highlight (used for the Forge options)
class Segmented final : public juce::Component, private juce::Timer
{
public:
    explicit Segmented (const juce::StringArray& items, juce::Colour accent = col::violet);
    int selected() const { return sel; }
    void setSelected (int i, bool notify = false);
    std::function<void (int)> onChange;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    void timerCallback() override;
    juce::StringArray names;
    juce::Colour accent;
    int sel = 0;
    float animPos = 0.0f;
};

// one knob with name and read-out, attached to an APVTS parameter
struct KnobCell
{
    KnobCell (Core&, const PDef&, juce::Colour accent, juce::Component& parent);
    void setActive (bool a);
    const PDef* def;
    FlSlider slider;
    juce::Label name;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

class ModuleCard final : public juce::Component
{
public:
    static constexpr int kKnobW = 80, kHeight = 186;
    ModuleCard (Core&, const juce::String& title, juce::Colour accent, const char* enableId,
                std::vector<const char*> knobIds, std::vector<const char*> extraToggles = {});
    int preferredWidth() const;
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    Core& core;
    juce::String title;
    juce::Colour accent;
    juce::ToggleButton enable;
    bool hasEnable = false, lastOn = true;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAtt;
    std::vector<std::unique_ptr<juce::ToggleButton>> extras;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> extraAtts;
    std::vector<std::unique_ptr<KnobCell>> knobs;
};

// edit controls for the selected clip (gain, fades, normalise, reverse, tempo/pitch transform)
class ClipCard final : public juce::Component
{
public:
    explicit ClipCard (Core&);
    int preferredWidth() const { return 6 * 80 + 36; }
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void applyChange();
    void transformDialog();
    Core& core;
    FlSlider gain, fin, fout;
    juce::Label gainL, finL, foutL;
    juce::TextButton normBtn { "Normalize" }, revBtn { "Reverse" }, xformBtn { "Tempo / Pitch" };
    int shownClip = -1;
    bool updating = false;
};

class RackView final : public juce::Component, private juce::Timer
{
public:
    explicit RackView (Core&);
    int layoutFor (int width);
private:
    void timerCallback() override;
    std::vector<std::unique_ptr<juce::Component>> cards;
    ClipCard* clipCard = nullptr;
    std::vector<ModuleCard*> modules;
};
} // namespace al
