#include "MainComponent.h"

class AuralloyApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Auralloy"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        core = std::make_unique<al::Core>();
        window = std::make_unique<MainWindow> (*core);
        core->restoreSession();

        juce::Array<juce::File> files;
        for (const auto& a : getCommandLineParameterArray())
            if (juce::File (a).existsAsFile()) files.add (juce::File (a));
        if (! files.isEmpty()) openFiles (files);
    }

    void shutdown() override
    {
        window.reset();
        core.reset();
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        juce::Array<juce::File> files;
        for (const auto& a : juce::StringArray::fromTokens (commandLine, true))
            if (juce::File (a.unquoted()).existsAsFile()) files.add (juce::File (a.unquoted()));
        if (! files.isEmpty()) openFiles (files);
        if (window != nullptr) window->toFront (true);
    }

private:
    void openFiles (const juce::Array<juce::File>& files)
    {
        if (files.size() == 1 && files[0].hasFileExtension ("alloy")) { core->loadProject (files[0]); return; }
        core->addSongs (files);
    }

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (al::Core& c)
            : DocumentWindow ("Auralloy", al::col::obsidian950, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new al::MainComponent (c), true);
            setResizable (true, false);
            setResizeLimits (1200, 740, 4000, 2400);
            const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds;
            const int w = std::min (1440, (int) area.getWidth() - 40), h = std::min (920, (int) area.getHeight() - 40);
            centreWithSize (w, h);
            setVisible (true);
            al::applyDarkTitleBar (*this);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<al::Core> core;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (AuralloyApp)
