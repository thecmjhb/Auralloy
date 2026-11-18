#include "MainComponent.h"

using namespace juce;

namespace al
{
// the "Studio" workspace: timeline, transport, quick looks and the effect rack
class StudioPane final : public Component
{
public:
    explicit StudioPane (Core& c) : core (c), timeline (c), transport (c), rack (c)
    {
        addAndMakeVisible (timeline);
        addAndMakeVisible (transport);
        const auto& fp = factoryPresets();
        const int idx[] = { 0, 1, 2, 3, 4, 5, 6 };
        for (int i : idx)
        {
            auto b = std::make_unique<TextButton> (i == 0 ? String ("Reset") : String (fp[(size_t) i].name));
            b->setWantsKeyboardFocus (false);
            b->onClick = [this, i] { core.applyFactoryPreset (i); };
            addAndMakeVisible (*b);
            chips.push_back (std::move (b));
        }
        presetBtn.setWantsKeyboardFocus (false);
        addAndMakeVisible (presetBtn);
        viewport.setViewedComponent (&rack, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        viewport.setSingleStepSizes (30, 70);
        viewport.setWantsKeyboardFocus (false);
        addAndMakeVisible (viewport);
        transport.onFit = [this] { timeline.zoomFit(); };
    }

    void resized() override
    {
        auto r = getLocalBounds();
        timeline.setBounds (r.removeFromTop (std::max (240, std::min (400, (int) (r.getHeight() * 0.38f)))));
        r.removeFromTop (10);
        transport.setBounds (r.removeFromTop (66));
        r.removeFromTop (10);
        auto row = r.removeFromTop (36);
        presetBtn.setBounds (row.removeFromRight (104));
        row.removeFromRight (8);
        const int n = (int) chips.size(), gap = 8;
        const int w = std::min (172, (row.getWidth() - gap * (n - 1)) / n);
        for (auto& c : chips) { c->setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); }
        r.removeFromTop (10);
        viewport.setBounds (r);
        const int vw = r.getWidth() - 10;
        rack.setSize (vw, rack.layoutFor (vw));
    }

    Core& core;
    TimelineView timeline;
    TransportBar transport;
    RackView rack;
    Viewport viewport;
    TextButton presetBtn { "Presets" };
    std::vector<std::unique_ptr<TextButton>> chips;
};

struct MainComponent::Impl
{
    explicit Impl (Core& c) : nav ({ "Studio", "Forge" }, col::violet), library (c), spectrum (c), meter (c), studio (c), forge (c) {}

    Segmented nav;
    TextButton fileBtn { "File" }, exportBtn { "Export" }, undoBtn { "Undo" }, redoBtn { "Redo" }, settingsBtn { "Audio" };
    ToggleButton bypassBtn { "FX bypass" };
    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;
    Label summary;
    LibraryView library;
    SpectrumView spectrum;
    MeterView meter;
    StudioPane studio;
    ForgeView forge;
    std::unique_ptr<Overlay> overlay;
    std::unique_ptr<SplashOverlay> splash;
    std::unique_ptr<FileChooser> chooser;
};

MainComponent::MainComponent (Core& c) : core (c), impl (std::make_unique<Impl> (c))
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);
    auto& i = *impl;

    for (auto* b : { &i.fileBtn, &i.exportBtn, &i.undoBtn, &i.redoBtn, &i.settingsBtn }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    i.exportBtn.getProperties().set ("primary", true);
    i.fileBtn.onClick = [this] { fileMenu(); };
    i.exportBtn.onClick = [this] { showExport(); };
    i.undoBtn.onClick = [this] { core.undoTimeline(); };
    i.redoBtn.onClick = [this] { core.redoTimeline(); };
    i.settingsBtn.onClick = [this] { showSettings(); };

    i.bypassBtn.setColour (ToggleButton::tickColourId, col::amber);
    i.bypassBtn.setWantsKeyboardFocus (false);
    addAndMakeVisible (i.bypassBtn);
    i.bypassAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (core.apvts, "bypass", i.bypassBtn);

    i.summary.setFont (uiFont (13.0f, true));
    i.summary.setColour (Label::textColourId, col::text300);
    i.summary.setJustificationType (Justification::centred);
    addAndMakeVisible (i.summary);

    addAndMakeVisible (i.nav);
    i.nav.onChange = [this] (int v) { setView (v); };
    addAndMakeVisible (i.library);
    i.library.onAdd = [this] { importSongs(); };
    addAndMakeVisible (i.spectrum);
    addAndMakeVisible (i.meter);
    addAndMakeVisible (i.studio);
    addChildComponent (i.forge);
    i.forge.onDone = [this] { setView (0); toast ("Mashup forged - every clip is editable on the timeline", col::mint); };
    i.studio.presetBtn.onClick = [this] { presetsMenu(); };

    core.addChangeListener (this);
    setSize (1440, 920);

    if (! core.reduceMotion)
    {
        i.splash = std::make_unique<SplashOverlay>();
        addAndMakeVisible (*i.splash);
        i.splash->setBounds (getLocalBounds());
        i.splash->onFinished = [safe = SafePointer<MainComponent> (this)]
        {
            MessageManager::callAsync ([safe] { if (safe != nullptr) safe->impl->splash.reset(); });
        };
    }
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    core.removeChangeListener (this);
    impl->overlay.reset();
    impl->splash.reset();
    setLookAndFeel (nullptr);
}

//==============================================================================
void MainComponent::setView (int v, bool animate)
{
    viewTarget = (float) v;
    impl->nav.setSelected (v);
    if (! animate || core.reduceMotion) { viewT = viewTarget; layoutViews(); }
    grabKeyboardFocus();
}

void MainComponent::layoutViews()
{
    auto& i = *impl;
    const float k = easeInOut (viewT);
    i.studio.setVisible (viewT < 0.995f);
    i.forge.setVisible (viewT > 0.005f);
    i.studio.setAlpha (1.0f - k);
    i.forge.setAlpha (k);
    const auto area = i.studio.getParentComponent() ? getLocalBounds() : getLocalBounds();
    (void) area;
    resized();
}

void MainComponent::setSplashTime (float t)
{
    if (impl->splash) impl->splash->setTime (t);
}

void MainComponent::toast (const String& t, Colour c)
{
    toastText = t; toastColour = c; toastUntil = Time::getMillisecondCounterHiRes() + 3800.0;
    repaint();
}

void MainComponent::paint (Graphics& g)
{
    drawAurora (g, getLocalBounds().toFloat(), auroraT, reactive);

    // brand: logo mark (pulses with the bass) + wordmark
    LogoState ls;
    ls.pulse = pulse; ls.glow = 0.30f;
    drawLogoMark (g, Rectangle<float> (14.0f, 10.0f, 56.0f, 56.0f), ls);
    const float wh = 20.0f;
    drawWordmark (g, Rectangle<float> (80.0f, 17.0f, wordmarkWidth (wh), wh), 1.0f);
    g.setColour (col::text500);
    g.setFont (uiFont (11.0f, true));
    g.drawText ("MASHUP STUDIO", Rectangle<float> (81.0f, 44.0f, 160.0f, 16.0f), Justification::centredLeft);
}

void MainComponent::paintOverChildren (Graphics& g)
{
    const double now = Time::getMillisecondCounterHiRes();
    if (toastText.isNotEmpty() && now < toastUntil)
    {
        const float a = (float) std::min (1.0, std::min ((toastUntil - now) / 500.0, (3800.0 - (toastUntil - now)) / 250.0 + 0.001));
        const float w = std::min ((float) getWidth() - 80.0f, 80.0f + (float) toastText.length() * 7.4f);
        Rectangle<float> r (((float) getWidth() - w) * 0.5f, (float) getHeight() - 78.0f - 10.0f * (1.0f - a), w, 44.0f);
        g.setColour (Colour (0xe0141628).withMultipliedAlpha (a));
        g.fillRoundedRectangle (r, 22.0f);
        drawPanel (g, r, 22.0f, toastColour, 1.4f * a);
        g.setColour (toastColour.withAlpha (a));
        g.fillEllipse (r.getX() + 18.0f, r.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour (col::text100.withAlpha (a));
        g.setFont (uiFont (14.0f, true));
        g.drawText (toastText, r.withTrimmedLeft (36.0f), Justification::centredLeft);
    }
}

void MainComponent::resized()
{
    auto& i = *impl;
    auto r = getLocalBounds().reduced (14, 10);

    headerArea = r.removeFromTop (60);
    {
        auto h = headerArea;
        i.settingsBtn.setBounds (h.removeFromRight (80).withSizeKeepingCentre (80, 36));
        h.removeFromRight (8);
        i.exportBtn.setBounds (h.removeFromRight (108).withSizeKeepingCentre (108, 40));
        h.removeFromRight (12);
        i.bypassBtn.setBounds (h.removeFromRight (126).withSizeKeepingCentre (126, 30));
        h.removeFromRight (6);
        i.redoBtn.setBounds (h.removeFromRight (66).withSizeKeepingCentre (66, 34));
        h.removeFromRight (6);
        i.undoBtn.setBounds (h.removeFromRight (66).withSizeKeepingCentre (66, 34));
        h.removeFromRight (10);
        i.fileBtn.setBounds (h.removeFromRight (64).withSizeKeepingCentre (64, 34));
        h.removeFromLeft (232);
        i.nav.setBounds (h.removeFromLeft (230).withSizeKeepingCentre (230, 38));
        h.removeFromLeft (14);
        i.summary.setBounds (h.withSizeKeepingCentre (std::min (h.getWidth(), 420), 28));
    }
    r.removeFromTop (6);

    auto side = r.removeFromLeft (300);
    r.removeFromLeft (14);
    {
        auto bottom = side.removeFromBottom (180);
        side.removeFromBottom (12);
        i.library.setBounds (side);
        i.meter.setBounds (bottom.removeFromRight (84));
        bottom.removeFromRight (8);
        i.spectrum.setBounds (bottom);
    }

    const float k = easeInOut (viewT);
    i.studio.setBounds (r.translated ((int) (-36.0f * k), 0));
    i.forge.setBounds (r.translated ((int) (36.0f * (1.0f - k)), 0));
    if (i.overlay) i.overlay->setBounds (getLocalBounds());
    if (i.splash) i.splash->setBounds (getLocalBounds());
}

//==============================================================================
void MainComponent::timerCallback()
{
    auto& i = *impl;
    ++tickN;
    const float dt = 1.0f / 30.0f;

    // motion: aurora drift + audio-reactive glow
    if (! core.reduceMotion)
    {
        auroraT += dt;
        const float bass = std::min (1.0f, core.bassLevel.load() * 5.0f);
        reactive += (bass - reactive) * (bass > reactive ? 0.35f : 0.06f);
        pulse += (std::min (1.0f, core.bassLevel.load() * 6.0f) - pulse) * 0.4f;
        if (tickN % 3 == 0 && isShowing() && ! i.splash) repaint (getLocalBounds().withTrimmedLeft (0));
        else repaint (headerArea);
    }
    if (std::abs (viewT - viewTarget) > 0.002f)
    {
        viewT += (viewTarget - viewT) * 0.2f;
        if (std::abs (viewT - viewTarget) < 0.01f) viewT = viewTarget;
        layoutViews();
    }

    // header status
    String sm;
    if (core.exportState() == 1) sm = "Exporting " + String (roundToInt (core.exportProgress() * 100.0f)) + "%";
    else if (! core.forgeSummary.empty()) sm = String (core.forgeSummary);
    else if (core.audioStatus.isNotEmpty()) sm = "Audio: " + core.audioStatus;
    i.summary.setText (sm, dontSendNotification);
    i.undoBtn.setEnabled (core.canUndoTimeline());
    i.redoBtn.setEnabled (core.canRedoTimeline());

    // transport read-out
    const auto s = core.snapshot();
    auto tl = core.timeline();
    const double len = tl->length(), speed = std::max (0.05, (double) s.f[F_speed]);
    auto fmt = [] (double sec) { sec = std::max (0.0, sec); const int t = (int) sec; return String (t / 60) + ":" + String (t % 60).paddedLeft ('0', 2) + "." + String ((int) ((sec - t) * 10)); };
    String info = tl->empty() ? String ("Empty - add songs or forge a mashup") : "Length " + fmt (len / speed) + (std::abs (speed - 1.0) > 0.01 ? "  at " + String (speed, 2) + "x" : String());
    if (core.hasRegion()) info += "  |  region " + fmt (core.regionStart()) + " - " + fmt (core.regionEnd());
    i.studio.transport.update (fmt (core.position()) + " / " + fmt (len), info, core.isPlaying());
}

bool MainComponent::keyPressed (const KeyPress& k)
{
    const auto m = k.getModifiers();
    const int code = k.getKeyCode();
    if (code == KeyPress::spaceKey) { core.togglePlay(); return true; }
    if (m.isCtrlDown() && code == 'O') { importSongs(); return true; }
    if (m.isCtrlDown() && code == 'E') { showExport(); return true; }
    if (m.isCtrlDown() && m.isShiftDown() && code == 'S') { saveProjectAs(); return true; }
    if (m.isCtrlDown() && code == 'S') { if (projectFile != File()) core.saveProject (projectFile); else saveProjectAs(); return true; }
    if (m.isCtrlDown() && code == 'Z' && ! m.isShiftDown()) { core.undoTimeline(); return true; }
    if (m.isCtrlDown() && (code == 'Y' || (code == 'Z' && m.isShiftDown()))) { core.redoTimeline(); return true; }
    if (m.isAltDown() && code == 'Z') { core.undo.undo(); return true; }
    if (m.isCtrlDown() && code == 'D') { core.duplicateSelection(); return true; }
    if (m.isCtrlDown() && code == 'A') { core.selection.clear(); for (const auto& t : core.timeline()->tracks) for (const auto& c : t.clips) core.selection.insert (c.id); repaint(); return true; }
    if (! m.isAnyModifierKeyDown())
    {
        if (code == 'S') { core.splitAtPlayhead(); return true; }
        if (code == KeyPress::deleteKey || code == KeyPress::backspaceKey) { core.deleteSelection(); return true; }
        if (code == 'L') { core.setBool ("loop", ! core.snapshot().b[B_loop]); return true; }
        if (code == 'B') { core.setBool ("bypass", ! core.snapshot().b[B_bypass]); return true; }
        if (code == 'F') { impl->studio.timeline.zoomFit(); return true; }
        if (code == '1') { setView (0); return true; }
        if (code == '2') { setView (1); return true; }
        if (code == '=' || code == '+') { impl->studio.timeline.zoomBy (1.25); return true; }
        if (code == '-') { impl->studio.timeline.zoomBy (0.8); return true; }
        if (code == KeyPress::homeKey) { core.seek (core.hasRegion() ? core.regionStart() : 0.0); return true; }
    }
    if (code == KeyPress::leftKey)  { core.seek (std::max (0.0, core.position() - (m.isShiftDown() ? 15.0 : 5.0))); return true; }
    if (code == KeyPress::rightKey) { core.seek (core.position() + (m.isShiftDown() ? 15.0 : 5.0)); return true; }
    if (code == KeyPress::upKey)    { core.setFloat ("vol", std::min (1.2f, core.snapshot().f[F_vol] + 0.05f)); return true; }
    if (code == KeyPress::downKey)  { core.setFloat ("vol", std::max (0.0f, core.snapshot().f[F_vol] - 0.05f)); return true; }
    return false;
}

//==============================================================================
void MainComponent::filesDropped (const StringArray& files, int, int)
{
    Array<File> songs;
    for (const auto& f : files)
    {
        const File file (f);
        if (file.hasFileExtension ("alloy")) { if (core.loadProject (file)) projectFile = file; return; }
        if (file.isDirectory())
            for (const auto& e : RangedDirectoryIterator (file, false, "*.wav;*.mp3;*.flac;*.ogg;*.aiff;*.aif;*.m4a;*.aac;*.wma")) songs.add (e.getFile());
        else songs.add (file);
    }
    const bool wasEmpty = core.timeline()->empty();
    core.addSongs (songs);
    if (wasEmpty && ! core.library.empty() && songs.size() == 1)
    {
        // a single song dropped on an empty project goes straight onto the timeline once it is ready
        auto sd = core.library.back();
        Timer::callAfterDelay (50, [this, sd, safe = SafePointer<MainComponent> (this)]
        {
            if (safe == nullptr) return;
            if (sd->state.load() >= 2 || sd->state.load() == 1) core.placeSong (sd, 0.0);
        });
    }
}

void MainComponent::importSongs()
{
    auto& i = *impl;
    i.chooser = std::make_unique<FileChooser> ("Add songs", File::getSpecialLocation (File::userMusicDirectory), "*.wav;*.mp3;*.flac;*.ogg;*.aiff;*.aif;*.m4a;*.aac;*.wma");
    i.chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                            [this, safe = SafePointer<MainComponent> (this)] (const FileChooser& fc)
                            {
                                if (safe == nullptr) return;
                                const auto res = fc.getResults();
                                if (! res.isEmpty()) core.addSongs (res);
                            });
}

void MainComponent::showExport()
{
    auto& i = *impl;
    i.overlay = std::make_unique<ExportOverlay> (core);
    i.overlay->onClose = [safe = SafePointer<MainComponent> (this)]
    {
        MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->impl->overlay.reset(); safe->grabKeyboardFocus(); } });
    };
    addAndMakeVisible (*i.overlay);
    i.overlay->setBounds (getLocalBounds());
    i.overlay->grabKeyboardFocus();
}

void MainComponent::showSettings()
{
    auto& i = *impl;
    i.overlay = std::make_unique<SettingsOverlay> (core);
    i.overlay->onClose = [safe = SafePointer<MainComponent> (this)]
    {
        MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->impl->overlay.reset(); safe->grabKeyboardFocus(); } });
    };
    addAndMakeVisible (*i.overlay);
    i.overlay->setBounds (getLocalBounds());
}

void MainComponent::saveProjectAs()
{
    auto& i = *impl;
    i.chooser = std::make_unique<FileChooser> ("Save project", File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("My Mashup.alloy"), "*.alloy");
    i.chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                            [this, safe = SafePointer<MainComponent> (this)] (const FileChooser& fc)
                            {
                                if (safe == nullptr) return;
                                const auto f = fc.getResult();
                                if (f != File() && core.saveProject (f)) { projectFile = f.withFileExtension ("alloy"); toast ("Project saved", col::mint); }
                            });
}

void MainComponent::openProject()
{
    auto& i = *impl;
    i.chooser = std::make_unique<FileChooser> ("Open project", File::getSpecialLocation (File::userDocumentsDirectory), "*.alloy");
    i.chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                            [this, safe = SafePointer<MainComponent> (this)] (const FileChooser& fc)
                            {
                                if (safe == nullptr) return;
                                const auto f = fc.getResult();
                                if (f != File() && core.loadProject (f)) projectFile = f;
                            });
}

void MainComponent::fileMenu()
{
    PopupMenu m;
    m.addItem (1, "Add songs...             Ctrl+O");
    m.addItem (2, "Export...                  Ctrl+E");
    m.addSeparator();
    m.addItem (3, "Open project...");
    m.addItem (4, "Save project             Ctrl+S");
    m.addItem (5, "Save project as...");
    m.addSeparator();
    m.addItem (6, "Clear timeline");
    m.addItem (7, "Audio && display settings...");
    m.addSeparator();
    m.addItem (8, "Quit");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&impl->fileBtn), [this, safe = SafePointer<MainComponent> (this)] (int r)
    {
        if (safe == nullptr) return;
        switch (r)
        {
            case 1: importSongs(); break;
            case 2: showExport(); break;
            case 3: openProject(); break;
            case 4: if (projectFile != File()) { core.saveProject (projectFile); toast ("Project saved", col::mint); } else saveProjectAs(); break;
            case 5: saveProjectAs(); break;
            case 6: core.clearTimeline(); break;
            case 7: showSettings(); break;
            case 8: JUCEApplication::getInstance()->systemRequestedQuit(); break;
            default: break;
        }
    });
}

void MainComponent::presetsMenu()
{
    PopupMenu m;
    const auto& fp = factoryPresets();
    for (int k = 0; k < (int) fp.size(); ++k) m.addItem (100 + k, fp[(size_t) k].name);
    m.addSeparator();
    const auto users = core.userPresets();
    PopupMenu um;
    for (int k = 0; k < users.size(); ++k) um.addItem (1000 + k, users[k].getFileNameWithoutExtension());
    m.addSubMenu ("My presets", um, users.size() > 0);
    m.addItem (1, "Save current sound as preset...");
    m.addItem (2, "Open presets folder");
    m.addItem (3, "Undo last sound change     Alt+Z");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&impl->studio.presetBtn), [this, safe = SafePointer<MainComponent> (this), users] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r >= 1000) { core.loadUserPreset (users[r - 1000]); return; }
        if (r >= 100) { core.applyFactoryPreset (r - 100); return; }
        if (r == 2) { core.presetsFolder().revealToUser(); return; }
        if (r == 3) { core.undo.undo(); return; }
        if (r == 1)
        {
            auto* w = new AlertWindow ("Save preset", "Give your sound a name:", MessageBoxIconType::NoIcon, this);
            w->addTextEditor ("name", "My lofi sound");
            w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            w->enterModalState (true, ModalCallbackFunction::create ([this, w, safe] (int res)
            {
                if (res == 1 && safe != nullptr)
                {
                    const String n = w->getTextEditorContents ("name").trim();
                    if (n.isNotEmpty() && core.saveUserPreset (n)) toast ("Preset saved: " + n, col::mint);
                }
            }), true);
        }
    });
}
} // namespace al
