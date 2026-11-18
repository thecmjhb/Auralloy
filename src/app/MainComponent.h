#pragma once
#include "Views.h"

namespace al
{
class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit MainComponent (Core&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray&, int, int) override;

    void setView (int v, bool animate = true);          // 0 Studio, 1 Forge
    void showExport();
    void showSettings();
    void importSongs();
    void fileMenu();
    void presetsMenu();
    void saveProjectAs();
    void openProject();
    void toast (const juce::String& text, juce::Colour c = col::mint);
    void setSplashTime (float t);          // demo recorder: step the intro deterministically

    struct Impl;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint (headerArea); }
    void timerCallback() override;
    void layoutViews();

    Core& core;
    AlloyLookAndFeel lnf;
    std::unique_ptr<Impl> impl;
    juce::File projectFile;
    juce::Rectangle<int> headerArea;
    double auroraT = 20.0;
    float reactive = 0.0f, pulse = 0.0f, viewT = 0.0f, viewTarget = 0.0f;
    juce::String toastText;
    juce::Colour toastColour;
    double toastUntil = 0.0;
    int tickN = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace al
