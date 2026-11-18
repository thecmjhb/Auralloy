#include "Widgets.h"

using namespace juce;

namespace al
{
//==============================================================================
void IconButton::paintButton (Graphics& g, bool hl, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float a = down ? 1.4f : (hl ? 1.15f : 1.0f);
    const bool big = icon == Play || icon == Pause;
    Path circle;
    circle.addEllipse (r);
    if (big)
    {
        g.setColour (col::violet.withAlpha (hl ? 0.45f : 0.28f));
        g.fillEllipse (r.expanded (3.0f));
        g.setGradientFill (alloyGradient (r.getX(), r.getY(), r.getRight(), r.getBottom()));
    }
    else
        g.setGradientFill (ColourGradient (tint.withAlpha ((active ? 0.45f : 0.16f) * a), r.getX(), r.getY(), tint.withAlpha ((active ? 0.25f : 0.05f)), r.getX(), r.getBottom(), false));
    g.fillPath (circle);
    g.saveState();
    g.reduceClipRegion (circle);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (big ? 0.35f : 0.12f), 0.0f, r.getY(), Colours::white.withAlpha (0.0f), 0.0f, r.getCentreY(), false));
    g.fillRect (r.withHeight (r.getHeight() * 0.55f));
    g.restoreState();
    g.setColour (Colours::white.withAlpha (big ? 0.5f : (active ? 0.6f : 0.22f)));
    g.drawEllipse (r, 1.0f);

    auto ic = r.withSizeKeepingCentre (r.getWidth() * 0.36f, r.getHeight() * 0.36f);
    g.setColour (big ? col::obsidian950 : Colours::white);
    Path p;
    const float sw = 1.8f;
    switch (icon)
    {
        case Play:  p.addTriangle (ic.getX() + 1.5f, ic.getY() - 2.0f, ic.getX() + 1.5f, ic.getBottom() + 2.0f, ic.getRight() + 3.0f, ic.getCentreY()); g.fillPath (p); break;
        case Pause: p.addRoundedRectangle (ic.getX() - 1.0f, ic.getY() - 1.0f, ic.getWidth() * 0.38f, ic.getHeight() + 2.0f, 1.5f);
                    p.addRoundedRectangle (ic.getRight() - ic.getWidth() * 0.38f + 1.0f, ic.getY() - 1.0f, ic.getWidth() * 0.38f, ic.getHeight() + 2.0f, 1.5f); g.fillPath (p); break;
        case Stop:  p.addRoundedRectangle (ic.reduced (0.5f), 2.0f); g.fillPath (p); break;
        case Prev:  p.addTriangle (ic.getRight(), ic.getY(), ic.getRight(), ic.getBottom(), ic.getX() + 2.0f, ic.getCentreY()); p.addRectangle (ic.getX() - 1.0f, ic.getY(), 2.5f, ic.getHeight()); g.fillPath (p); break;
        case Next:  p.addTriangle (ic.getX(), ic.getY(), ic.getX(), ic.getBottom(), ic.getRight() - 2.0f, ic.getCentreY()); p.addRectangle (ic.getRight() - 1.5f, ic.getY(), 2.5f, ic.getHeight()); g.fillPath (p); break;
        case Split:
            g.fillRect (ic.getCentreX() - 1.0f, ic.getY() - 3.0f, 2.0f, ic.getHeight() + 6.0f);
            p.addTriangle (ic.getX() - 1.0f, ic.getCentreY() - 4.0f, ic.getX() - 1.0f, ic.getCentreY() + 4.0f, ic.getCentreX() - 3.5f, ic.getCentreY());
            p.addTriangle (ic.getRight() + 1.0f, ic.getCentreY() - 4.0f, ic.getRight() + 1.0f, ic.getCentreY() + 4.0f, ic.getCentreX() + 3.5f, ic.getCentreY());
            g.fillPath (p); break;
        case Duplicate:
            g.drawRoundedRectangle (ic.getX() - 2.0f, ic.getY() + 2.0f, ic.getWidth() - 2.0f, ic.getHeight() - 2.0f, 2.0f, sw);
            g.drawRoundedRectangle (ic.getX() + 2.0f, ic.getY() - 2.0f, ic.getWidth() - 2.0f, ic.getHeight() - 2.0f, 2.0f, sw); break;
        case Delete:
            g.drawLine (ic.getX(), ic.getY(), ic.getRight(), ic.getBottom(), sw);
            g.drawLine (ic.getX(), ic.getBottom(), ic.getRight(), ic.getY(), sw); break;
        case Loop:
            p.addCentredArc (ic.getCentreX(), ic.getCentreY(), ic.getWidth() * 0.55f, ic.getHeight() * 0.55f, 0.0f, 0.6f, 5.4f, true);
            g.strokePath (p, PathStrokeType (sw));
            { Path ar; ar.addTriangle (ic.getRight() + 1.0f, ic.getY() - 2.5f, ic.getRight() + 5.0f, ic.getY() + 3.0f, ic.getRight() - 3.0f, ic.getY() + 3.5f); g.fillPath (ar); } break;
    }
}

//==============================================================================
Segmented::Segmented (const StringArray& items, Colour acc) : names (items), accent (acc)
{
    setWantsKeyboardFocus (false);
}

void Segmented::setSelected (int i, bool notify)
{
    i = jlimit (0, names.size() - 1, i);
    if (i == sel) return;
    sel = i;
    startTimerHz (60);
    if (notify && onChange) onChange (sel);
}

void Segmented::mouseDown (const MouseEvent& e)
{
    const int i = (int) ((float) e.x / ((float) getWidth() / (float) names.size()));
    setSelected (i, true);
}

void Segmented::timerCallback()
{
    const float target = (float) sel;
    animPos += (target - animPos) * 0.28f;
    if (std::abs (target - animPos) < 0.003f) { animPos = target; stopTimer(); }
    repaint();
}

void Segmented::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    Path pill;
    pill.addRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (Colour (0x22ffffff));
    g.fillPath (pill);
    g.setColour (Colours::white.withAlpha (0.16f));
    g.strokePath (pill, PathStrokeType (1.0f));
    const float w = r.getWidth() / (float) names.size();
    auto hi = Rectangle<float> (r.getX() + animPos * w + 2.0f, r.getY() + 2.0f, w - 4.0f, r.getHeight() - 4.0f);
    g.setColour (accent.withAlpha (0.30f));
    g.fillRoundedRectangle (hi.expanded (2.0f), hi.getHeight() * 0.5f + 2.0f);
    g.setGradientFill (ColourGradient (accent.withAlpha (0.95f), hi.getX(), hi.getY(), accent.darker (0.5f).withAlpha (0.9f), hi.getX(), hi.getBottom(), false));
    g.fillRoundedRectangle (hi, hi.getHeight() * 0.5f);
    for (int i = 0; i < names.size(); ++i)
    {
        g.setColour (i == sel ? col::obsidian950 : col::text300);
        g.setFont (uiFont (13.0f, true));
        g.drawText (names[i], Rectangle<float> (r.getX() + (float) i * w, r.getY(), w, r.getHeight()), Justification::centred);
    }
}

//==============================================================================
KnobCell::KnobCell (Core& core, const PDef& d, Colour accent, Component& parent) : def (&d)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 80, 20);
    slider.setColour (Slider::rotarySliderFillColourId, accent);
    slider.setWantsKeyboardFocus (false);
    slider.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    slider.setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    slider.setColour (Slider::textBoxTextColourId, col::text100);
    slider.getProperties().set ("bipolar", d.bipolar);
    parent.addAndMakeVisible (slider);

    name.setText (d.name, dontSendNotification);
    name.setFont (uiFont (11.5f, true));
    name.setColour (Label::textColourId, col::text300);
    name.setJustificationType (Justification::centred);
    name.setInterceptsMouseClicks (false, false);
    parent.addAndMakeVisible (name);

    att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (core.apvts, d.id, slider);
    const Fmt f = d.fmt;
    slider.textFromValueFunction = [f] (double v) { return formatValue (f, v); };
    slider.valueFromTextFunction = [f] (const String& t) { return parseValue (f, t); };
    slider.updateText();
}

void KnobCell::setActive (bool a) { const float al = a ? 1.0f : 0.42f; slider.setAlpha (al); name.setAlpha (al); }

//==============================================================================
ModuleCard::ModuleCard (Core& c, const String& t, Colour a, const char* enableId, std::vector<const char*> ids, std::vector<const char*> extraToggles)
    : core (c), title (t), accent (a)
{
    if (enableId != nullptr)
    {
        enable.setButtonText ({});
        enable.setColour (ToggleButton::tickColourId, a);
        enable.setWantsKeyboardFocus (false);
        addAndMakeVisible (enable);
        enableAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (core.apvts, enableId, enable);
        hasEnable = true;
    }
    for (auto* id : extraToggles)
    {
        auto t2 = std::make_unique<ToggleButton>();
        for (const auto& bd : boolParams()) if (String (bd.id) == id) t2->setButtonText (bd.name);
        t2->setColour (ToggleButton::tickColourId, a);
        t2->setWantsKeyboardFocus (false);
        addAndMakeVisible (*t2);
        extraAtts.push_back (std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (core.apvts, id, *t2));
        extras.push_back (std::move (t2));
    }
    for (auto* id : ids)
        if (auto* d = findDef (id)) knobs.push_back (std::make_unique<KnobCell> (core, *d, a, *this));
}

int ModuleCard::preferredWidth() const { return std::max ((int) knobs.size() * kKnobW + 26, extras.empty() ? 0 : 190); }

void ModuleCard::refresh()
{
    const bool on = ! hasEnable || enable.getToggleState();
    for (auto& k : knobs) k->setActive (on);
    if (on != lastOn) { lastOn = on; repaint(); }
}

void ModuleCard::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = ! hasEnable || enable.getToggleState();
    drawPanel (g, r, 18.0f, on ? accent.interpolatedWith (Colours::white, 0.6f) : Colours::white, on ? 1.0f : 0.7f);
    g.setGradientFill (ColourGradient (accent.withAlpha (on ? 0.9f : 0.25f), r.getX() + 16.0f, 0.0f, accent.withAlpha (0.0f), r.getRight() - 16.0f, 0.0f, false));
    g.fillRoundedRectangle (r.getX() + 16.0f, r.getY() + 34.0f, r.getWidth() - 32.0f, 1.5f, 0.75f);
    g.setColour (on ? accent.brighter (0.25f) : col::text500);
    g.setFont (uiFont (11.5f, true));
    g.drawText (title.toUpperCase(), Rectangle<int> (18, 9, getWidth() - 80, 22), Justification::centredLeft);
}

void ModuleCard::resized()
{
    if (hasEnable) enable.setBounds (getWidth() - 56, 8, 44, 24);
    int y = 40;
    if (! extras.empty())
    {
        int x = 14;
        for (auto& e : extras) { e->setBounds (x, y, 168, 24); x += 170; }
        y += 26;
    }
    int x = 13;
    for (auto& k : knobs)
    {
        k->name.setBounds (x, y, kKnobW, 16);
        k->slider.setBounds (x, y + 16, kKnobW, getHeight() - y - 18);
        x += kKnobW;
    }
}

//==============================================================================
ClipCard::ClipCard (Core& c) : core (c)
{
    auto setup = [this] (FlSlider& s, Label& l, const char* name, double mn, double mx, double def, bool skew)
    {
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
        s.setTextBoxStyle (Slider::TextBoxBelow, false, 80, 20);
        s.setColour (Slider::rotarySliderFillColourId, col::mint);
        s.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        s.setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        s.setWantsKeyboardFocus (false);
        s.setRange (mn, mx, 0.0);
        if (skew) s.setSkewFactorFromMidPoint (mn + (mx - mn) * 0.15);
        s.setDoubleClickReturnValue (true, def);
        s.setValue (def, dontSendNotification);
        addAndMakeVisible (s);
        l.setText (name, dontSendNotification);
        l.setFont (uiFont (11.5f, true));
        l.setColour (Label::textColourId, col::text300);
        l.setJustificationType (Justification::centred);
        l.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (l);
        s.onDragStart = [this] { core.beginDrag(); };
        s.onDragEnd   = [this] { core.endDrag(); };
        s.onValueChange = [this] { if (! updating) applyChange(); };
    };
    setup (gain, gainL, "Gain", -24.0, 12.0, 0.0, false);
    setup (fin, finL, "Fade in", 0.0, 16.0, 0.0, true);
    setup (fout, foutL, "Fade out", 0.0, 16.0, 0.0, true);
    gain.textFromValueFunction = [] (double v) { return formatValue (Fmt::Db, v); };
    fin.textFromValueFunction  = [] (double v) { return formatValue (Fmt::Sec, v); };
    fout.textFromValueFunction = [] (double v) { return formatValue (Fmt::Sec, v); };
    for (auto* b : { &normBtn, &revBtn, &xformBtn }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    normBtn.onClick = [this] { if (shownClip >= 0) core.normalizeClip (shownClip); };
    revBtn.onClick  = [this] { if (shownClip >= 0) core.reverseClip (shownClip); };
    xformBtn.onClick = [this] { transformDialog(); };
    for (auto* s : { &gain, &fin, &fout }) s->updateText();
    refresh();
}

void ClipCard::applyChange()
{
    if (shownClip < 0) return;
    Timeline t = core.editCopy();
    if (auto* c = t.findClip (shownClip))
    {
        c->gainDb = (float) gain.getValue();
        c->fadeIn = std::min (fin.getValue(), c->length);
        c->fadeOut = std::min (fout.getValue(), c->length);
        core.preview (std::move (t));
    }
}

void ClipCard::refresh()
{
    const int id = core.selection.size() == 1 ? *core.selection.begin() : (core.selection.empty() ? -1 : *core.selection.begin());
    auto tl = core.timeline();
    const Clip* c = id >= 0 ? tl->findClip (id) : nullptr;
    const int newId = c ? id : -1;
    const bool changed = newId != shownClip;
    shownClip = newId;
    const bool en = c != nullptr;
    for (auto* s : { &gain, &fin, &fout }) { s->setEnabled (en); s->setVisible (en); }
    for (auto* l : { &gainL, &finL, &foutL }) l->setVisible (en);
    for (auto* b : { &normBtn, &revBtn, &xformBtn }) { b->setEnabled (en); b->setVisible (en); }
    if (c != nullptr && ! gain.isMouseButtonDown() && ! fin.isMouseButtonDown() && ! fout.isMouseButtonDown())
    {
        updating = true;
        gain.setValue (c->gainDb, dontSendNotification);
        fin.setValue (c->fadeIn, dontSendNotification);
        fout.setValue (c->fadeOut, dontSendNotification);
        gain.updateText(); fin.updateText(); fout.updateText();
        updating = false;
    }
    if (changed) repaint();
}

void ClipCard::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    drawPanel (g, r, 18.0f, col::mint.interpolatedWith (Colours::white, 0.6f), 1.0f);
    g.setGradientFill (ColourGradient (col::mint.withAlpha (0.9f), r.getX() + 16.0f, 0.0f, col::mint.withAlpha (0.0f), r.getRight() - 16.0f, 0.0f, false));
    g.fillRoundedRectangle (r.getX() + 16.0f, r.getY() + 34.0f, r.getWidth() - 32.0f, 1.5f, 0.75f);
    g.setColour (col::mint.brighter (0.2f));
    g.setFont (uiFont (11.5f, true));
    auto tl = core.timeline();
    const Clip* c = shownClip >= 0 ? tl->findClip (shownClip) : nullptr;
    g.drawText (c ? "CLIP  -  " + String (c->name).toUpperCase() : String ("CLIP"), Rectangle<int> (18, 9, getWidth() - 36, 22), Justification::centredLeft, true);
    if (c == nullptr)
    {
        g.setColour (col::text500);
        g.setFont (uiFont (12.5f));
        g.drawFittedText ("Select a clip on the timeline to edit its gain, fades, tempo and pitch.", Rectangle<int> (18, 46, 260, 90), Justification::topLeft, 4);
    }
}

void ClipCard::resized()
{
    int x = 13;
    for (auto pr : { std::make_pair ((Slider*) &gain, &gainL), std::make_pair ((Slider*) &fin, &finL), std::make_pair ((Slider*) &fout, &foutL) })
    {
        pr.second->setBounds (x, 40, 80, 16);
        pr.first->setBounds (x, 56, 80, getHeight() - 58);
        x += 80;
    }
    x += 10;
    normBtn.setBounds (x, 50, 118, 30);
    revBtn.setBounds (x, 86, 118, 30);
    xformBtn.setBounds (x, 122, 118, 30);
}

void ClipCard::transformDialog()
{
    if (shownClip < 0) return;
    auto* w = new AlertWindow ("Tempo / Pitch", "Change the selected clip. Tempo and pitch are independent (time-stretch + pitch-shift).", MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("tempo", "100", "Tempo (%)");
    w->addTextEditor ("pitch", "0", "Pitch (semitones, -12 to 12)");
    w->addButton ("Apply", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([this, w, id = shownClip] (int res)
    {
        if (res != 1) return;
        const double tempo = jlimit (40.0, 250.0, w->getTextEditorContents ("tempo").getDoubleValue()) / 100.0;
        const double semis = jlimit (-12.0, 12.0, w->getTextEditorContents ("pitch").getDoubleValue());
        if (std::abs (tempo - 1.0) > 1e-3 || std::abs (semis) > 1e-3) core.transformClip (id, tempo, semis);
    }), true);
}

//==============================================================================
RackView::RackView (Core& c)
{
    auto add = [this] (std::unique_ptr<Component> comp) { addAndMakeVisible (*comp); cards.push_back (std::move (comp)); };
    auto cc = std::make_unique<ClipCard> (c);
    clipCard = cc.get();
    add (std::move (cc));
    auto mod = [&] (const char* title, Colour col, const char* en, std::vector<const char*> ids, std::vector<const char*> ex = {})
    {
        auto m = std::make_unique<ModuleCard> (c, title, col, en, ids, ex);
        modules.push_back (m.get());
        add (std::move (m));
    };
    mod ("Speed",        col::mint,   nullptr, { "speed" });
    mod ("Stereo",       col::mint,   nullptr,  { "width", "monoBass" });
    mod ("Equalizer",    col::sky,    "eqOn",   { "hpf", "lowG", "lmG", "hmG", "highG", "lpf" });
    mod ("Lofi tape",    col::amber,  "lofiOn", { "bits", "srate", "wobble", "flutter", "drive" });
    mod ("Compressor",   col::mint,   "compOn", { "thresh", "ratio", "attack", "release", "makeup", "compMix" });
    mod ("Reverb",       col::violet, "rvOn",   { "rvMix", "rvDecay", "rvDamp", "rvPre", "rvLow" });
    mod ("Echo delay",   col::sky,    "dlOn",   { "dlTime", "dlFb", "dlTone", "dlMix" }, { "dlPing" });
    mod ("Chorus",       col::violet, "chOn",   { "chRate", "chDepth", "chMix" });
    mod ("Ambience",     col::rose,   nullptr,  { "rain", "rainDens", "vinyl", "hiss" });
    mod ("Master & fades", col::rose, nullptr,  { "gain", "loudness", "ceiling", "fadeIn", "fadeOut" });
    startTimerHz (12);
}

int RackView::layoutFor (int width)
{
    const int gap = 12, pad = 2;
    int x = pad, y = pad;
    for (auto& c : cards)
    {
        int w = 0;
        if (auto* m = dynamic_cast<ModuleCard*> (c.get())) w = m->preferredWidth();
        else if (auto* cc = dynamic_cast<ClipCard*> (c.get())) w = cc->preferredWidth();
        if (x + w > width - pad && x > pad) { x = pad; y += ModuleCard::kHeight + gap; }
        c->setBounds (x, y, w, ModuleCard::kHeight);
        x += w + gap;
    }
    return y + ModuleCard::kHeight + pad + 8;
}

void RackView::timerCallback()
{
    for (auto* m : modules) m->refresh();
    if (clipCard) clipCard->refresh();
}
} // namespace al
