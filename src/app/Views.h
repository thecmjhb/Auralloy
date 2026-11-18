#pragma once
#include "Widgets.h"

namespace al
{
//==============================================================================
class TimelineView final : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit TimelineView (Core&);
    ~TimelineView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void zoomFit();
    void zoomBy (double factor);
private:
    enum Drag { None, Seek, Move, TrimL, TrimR, FadeIn, FadeOut, Region, Pan, TrackVol };
    static constexpr int headerW = 168, gap = 6;
    int rulerH = 30;
    int trackH = 76;
    bool needFit = true;
    void updateTrackH();
    int trackTop (int i) const { return rulerH + i * (trackH + gap) - scrollY; }
    float xOf (double t) const { return (float) headerW + (float) ((t - scrollX) * pps); }
    double timeAt (float x) const { return scrollX + (double) (x - (float) headerW) / pps; }
    struct Hit { int track = -1, clip = -1; int zone = 0; };   // zone: 0 body, 1 left edge, 2 right edge, 3 fade-in, 4 fade-out
    Hit hitTest (juce::Point<int>) const;
    double snap (double t, int excludeClip = -1) const;
    void showMenu (const Hit&, juce::Point<int>);
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void clampScroll();

    Core& core;
    std::shared_ptr<const Timeline> tl;
    double pps = 40.0, scrollX = 0.0;
    int scrollY = 0;
    Drag mode = None;
    Timeline base;                       // timeline when the drag started
    int dragClip = -1, dragTrack = -1;
    double grabOffset = 0.0, anchorT = 0.0, panStart = 0.0, lastPos = -1.0, snapGuide = -1.0, lastLength = -1.0;
    float grabX = 0.0f;
    int movedPx = 0, volTrack = -1;
    float volStart = 0.0f;
    Smooth playPulse;
};

//==============================================================================
class LibraryView final : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener, private juce::Timer
{
public:
    explicit LibraryView (Core&);
    ~LibraryView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onAdd;
private:
    int getNumRows() override { return (int) core.library.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override;
    void listBoxItemClicked (int, const juce::MouseEvent&) override;
    void deleteKeyPressed (int) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { list.updateContent(); list.repaint(); repaint(); }
    void timerCallback() override { phase += 0.12f; list.repaint(); }
    Core& core;
    juce::ListBox list;
    juce::TextButton addBtn { "Add songs" };
    float phase = 0.0f;
};

class SpectrumView final : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumView (Core&);
    void paint (juce::Graphics&) override;
private:
    static constexpr int kBands = 56;
    void timerCallback() override;
    Core& core;
    juce::dsp::FFT fft;
    std::vector<float> data, window;
    std::array<float, kBands> bands {}, peaks {};
};

class MeterView final : public juce::Component, private juce::Timer
{
public:
    explicit MeterView (Core&);
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    void drawBar (juce::Graphics&, juce::Rectangle<float>, float v, float h, bool gr);
    Core& core;
    float lvl[2] { -70.0f, -70.0f }, hold[2] { -70.0f, -70.0f }, gr = 0.0f;
    int holdAge[2] { 0, 0 };
};

//==============================================================================
class TransportBar final : public juce::Component
{
public:
    explicit TransportBar (Core&);
    void update (const juce::String& timeText, const juce::String& infoText, bool playing);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onFit;
private:
    Core& core;
    IconButton toStart, play, stop, loop, split, dup, del;
    juce::Label time, info, volLabel;
    FlSlider vol;
    juce::TextButton fitBtn { "Fit" }, clearRegion { "Clear region" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volAtt;
};

//==============================================================================
// Forge: auto mashup
class ForgeOrb final : public juce::Component, private juce::Timer
{
public:
    explicit ForgeOrb (Core&);
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; }
    void mouseExit (const juce::MouseEvent&) override { hover = false; }
private:
    void timerCallback() override;
    Core& core;
    bool hover = false;
    float t = 0.0f, merge = 0.0f, flash = 0.0f, hoverAmt = 0.0f;
    int lastState = 0;
};

class ForgeView final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit ForgeView (Core&);
    ~ForgeView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void()> onDone;
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void refresh();
    void startForge();
    ForgeOptions options() const;
    Core& core;
    ForgeOrb orb;
    Segmented mode, shape, segLen, tempoTol, keyTol, trans;
    juce::Slider lengthSlider;
    juce::ToggleButton stemsBtn { "Split stems (vocals, drums, bass, other)" };
    juce::ComboBox finish;
    juce::TextEditor log;
    juce::Label status;
    MashupPlan preview;
    std::vector<std::shared_ptr<SongData>> deckSongs;
    juce::Rectangle<int> deckArea, optionsArea, matrixArea, logArea;
    int lastState = 0;
    std::string lastLogText;
};

//==============================================================================
class Overlay : public juce::Component
{
public:
    std::function<void()> onClose;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (! card.contains (getMouseXYRelative())) close(); }
    bool keyPressed (const juce::KeyPress&) override;
    void close() { if (onClose) onClose(); }
protected:
    juce::Rectangle<int> card;
    juce::String heading;
};

class ExportOverlay final : public Overlay, private juce::Timer
{
public:
    explicit ExportOverlay (Core&);
    void resized() override;
private:
    void choose();
    void timerCallback() override;
    Core& core;
    double progress = 0.0;
    bool started = false;
    juce::Label fl, sl, status;
    juce::ComboBox format, rate;
    juce::ToggleButton normalize, dither, region;
    juce::ProgressBar bar;
    juce::TextButton go, closeBtn, reveal;
    std::unique_ptr<juce::FileChooser> chooser;
};

class SettingsOverlay final : public Overlay
{
public:
    explicit SettingsOverlay (Core&);
    void resized() override;
private:
    Core& core;
    juce::AudioDeviceSelectorComponent selector;
    juce::ToggleButton motion { "Reduce motion (no intro, still background)" };
    juce::TextButton closeBtn;
    juce::Label info;
};

// startup intro: the two rings of the logo fuse into the alloy lens, the wordmark reveals, then it fades away
class SplashOverlay final : public juce::Component, private juce::Timer
{
public:
    SplashOverlay();
    std::function<void()> onFinished;
    void setTime (float t);                          // deterministic stepping (used by the demo recorder)
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { elapsed = std::max (elapsed, 1.6f); }
private:
    void timerCallback() override;
    float elapsed = 0.0f;
};
} // namespace al
