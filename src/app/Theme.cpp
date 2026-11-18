#include "Theme.h"

using namespace juce;

namespace al
{
Colour sourceColour (int i)
{
    static const Colour c[8] = { col::mint, col::rose, col::violet, col::amber, col::sky, Colour (0xff7ce38b), Colour (0xffff9f6b), Colour (0xffc58bff) };
    return c[((i % 8) + 8) % 8];
}

Colour roleColour (int role)
{
    switch (role) { case 0: return col::rose; case 1: return col::amber; case 2: return col::violet; case 3: return col::mint; default: return col::sky; }
}

Font uiFont (float size, bool semibold) { return Font (FontOptions (semibold ? "Segoe UI Semibold" : "Segoe UI", size, Font::plain)); }
Font monoFont (float size) { return Font (FontOptions ("Cascadia Mono", size, Font::plain)); }

String formatValue (Fmt f, double v)
{
    switch (f)
    {
        case Fmt::Speed:    return String (v, 2) + "x";
        case Fmt::Semi:     return (v > 0.04 ? "+" : "") + String (v, 1) + " st";
        case Fmt::Pct:      return String (roundToInt (v * 100.0)) + "%";
        case Fmt::Width:    return String (roundToInt (v * 100.0)) + "%";
        case Fmt::Db:       return (v > 0.04 ? "+" : "") + String (v, 1) + " dB";
        case Fmt::Hz:       return v >= 1000.0 ? String (v / 1000.0, 1) + " kHz" : String (roundToInt (v)) + " Hz";
        case Fmt::Ms:       return String (roundToInt (v)) + " ms";
        case Fmt::Sec:      return String (v, 1) + " s";
        case Fmt::Bits:     return String (roundToInt (v)) + " bit";
        case Fmt::Ratio:    return String (v, 1) + " : 1";
        case Fmt::MonoBass: return v < 30.0 ? String ("Off") : String (roundToInt (v)) + " Hz";
        case Fmt::Rate:     return String (v, 2) + " Hz";
    }
    return String (v);
}

double parseValue (Fmt f, const String& t)
{
    double v = t.retainCharacters ("0123456789.-+").getDoubleValue();
    if (t.containsIgnoreCase ("off")) return 0.0;
    if (t.containsIgnoreCase ("k")) v *= 1000.0;
    if (f == Fmt::Pct || f == Fmt::Width) v /= 100.0;
    return v;
}

const PDef* findDef (const char* id)
{
    for (const auto& d : floatParams()) if (String (d.id) == id) return &d;
    return nullptr;
}

float clamp01 (float t) { return std::min (1.0f, std::max (0.0f, t)); }
float easeOutExpo (float t) { t = clamp01 (t); return t >= 1.0f ? 1.0f : 1.0f - std::pow (2.0f, -10.0f * t); }
float easeInOut (float t) { t = clamp01 (t); return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow (-2.0f * t + 2.0f, 3.0f) / 2.0f; }

ColourGradient alloyGradient (float x1, float y1, float x2, float y2)
{
    ColourGradient g (col::mint, x1, y1, col::rose, x2, y2, false);
    g.addColour (0.5, col::violet);
    return g;
}

void drawPanel (Graphics& g, Rectangle<float> r, float corner, Colour tint, float strength)
{
    Path p;
    p.addRoundedRectangle (r, corner);
    g.setGradientFill (ColourGradient (tint.withAlpha (0.075f * strength), r.getX(), r.getY(), tint.withAlpha (0.028f * strength), r.getX(), r.getBottom(), false));
    g.fillPath (p);
    g.saveState();
    g.reduceClipRegion (p);
    const float sh = std::min (46.0f, r.getHeight() * 0.5f);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.065f * strength), 0.0f, r.getY(), Colours::white.withAlpha (0.0f), 0.0f, r.getY() + sh, false));
    g.fillRect (r.withHeight (sh));
    g.restoreState();
    ColourGradient rim (Colours::white.withAlpha (0.24f * strength), r.getX(), r.getY(), Colours::white.withAlpha (0.08f * strength), r.getRight(), r.getBottom(), false);
    rim.addColour (0.4, Colours::white.withAlpha (0.045f * strength));
    g.setGradientFill (rim);
    g.strokePath (p, PathStrokeType (1.0f));
}

void drawAurora (Graphics& g, Rectangle<float> a, double t, float reactive)
{
    g.setGradientFill (ColourGradient (col::obsidian950, a.getX(), a.getY(), col::obsidian900.interpolatedWith (Colour (0xff120a24), 0.6f), a.getRight(), a.getBottom(), false));
    g.fillRect (a);
    struct Orb { Colour c; float fx, fy, px, py, rad, alpha; };
    static const Orb orbs[] = {
        { col::violet, 0.031f, 0.023f, 0.0f, 1.3f, 0.46f, 0.26f },
        { col::rose,   0.027f, 0.035f, 2.1f, 0.4f, 0.34f, 0.17f },
        { col::mint,   0.021f, 0.029f, 4.0f, 2.6f, 0.30f, 0.12f },
        { col::sky,    0.034f, 0.019f, 1.1f, 5.1f, 0.32f, 0.10f },
        { Colour (0xff4b3bd6), 0.018f, 0.025f, 3.3f, 0.9f, 0.50f, 0.22f },
    };
    const float W = a.getWidth(), H = a.getHeight();
    int k = 0;
    for (const auto& o : orbs)
    {
        const float cx = a.getX() + W * (0.5f + 0.46f * std::sin ((float) t * o.fx * 6.2831853f + o.px));
        const float cy = a.getY() + H * (0.5f + 0.42f * std::cos ((float) t * o.fy * 6.2831853f + o.py));
        const float rad = std::max (W, H) * o.rad;
        const float al = o.alpha * (1.0f + (k == 1 || k == 2 ? 0.55f * reactive : 0.0f));
        g.setGradientFill (ColourGradient (o.c.withAlpha (al), cx, cy, o.c.withAlpha (0.0f), cx + rad, cy, true));
        g.fillEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);
        ++k;
    }
}

void drawLogoMark (Graphics& g, Rectangle<float> b, const LogoState& s)
{
    const float S = std::min (b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    const float T = S * 0.165f;
    const float off = (s.spread - 1.0f) * 0.42f * S;
    const float a = s.alpha;

    // soft glow
    const float glowR = S * 0.72f;
    g.setGradientFill (ColourGradient (col::violet.withAlpha ((s.glow + 0.35f * s.pulse) * 0.55f * a), c.x, c.y, col::violet.withAlpha (0.0f), c.x + glowR, c.y, true));
    g.fillEllipse (c.x - glowR, c.y - glowR, glowR * 2.0f, glowR * 2.0f);

    auto makeLeg = [&] (bool left, Path& outline, Point<float>& bottom, Point<float>& top)
    {
        const float sx = left ? -1.0f : 1.0f;
        bottom = { c.x + sx * 0.355f * S + sx * off, c.y + 0.36f * S };
        top    = { c.x + sx * 0.012f * S + sx * off, c.y - 0.34f * S };
        const Point<float> end = bottom + (top - bottom) * clamp01 (s.legs);
        Path line;
        line.startNewSubPath (bottom);
        line.lineTo (end);
        PathStrokeType (T, PathStrokeType::curved, PathStrokeType::rounded).createStrokedPath (outline, line);
    };
    Path lo, ro;
    Point<float> lb, lt, rb, rt;
    makeLeg (true, lo, lb, lt);
    makeLeg (false, ro, rb, rt);

    auto paintLeg = [&] (const Path& p, Point<float> bot, Point<float> top, Colour from)
    {
        g.setGradientFill (ColourGradient (from.withAlpha (0.96f * a), bot.x, bot.y, col::violet.withAlpha (0.96f * a), top.x, top.y, false));
        g.fillPath (p);
        g.saveState();
        g.reduceClipRegion (p);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.20f * a), 0.0f, c.y - 0.36f * S, Colours::white.withAlpha (0.0f), 0.0f, c.y + 0.05f * S, false));
        g.fillRect (b.expanded (S));
        g.restoreState();
        g.setColour (Colours::white.withAlpha (0.38f * a));
        g.strokePath (p, PathStrokeType (std::max (1.0f, S * 0.012f)));
    };
    paintLeg (lo, lb, lt, col::mint);
    paintLeg (ro, rb, rt, col::rose);

    // fused apex: where the two songs overlap
    if (s.apex > 0.002f && s.legs > 0.9f)
    {
        g.saveState();
        g.reduceClipRegion (lo);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.85f * s.apex * a), c.x, c.y - 0.30f * S, col::violet.withAlpha (0.25f * s.apex * a), c.x, c.y - 0.05f * S, false));
        g.fillPath (ro);
        g.restoreState();
    }

    // waveform crossbar
    if (s.bars > 0.002f && s.legs > 0.85f)
    {
        static const float hs[5] = { 0.070f, 0.130f, 0.195f, 0.110f, 0.065f };
        const float bw = S * 0.034f, gap = S * 0.064f, cy = c.y + 0.115f * S;
        for (int i = 0; i < 5; ++i)
        {
            const float dance = 1.0f + 0.25f * s.pulse * (i == 2 ? 1.0f : 0.5f) + (s.wave > 0.0f ? 0.22f * std::sin (s.wave * 3.0f + (float) i * 1.4f) : 0.0f);
            const float h = S * hs[i] * 2.0f * s.bars * dance;
            const Rectangle<float> r (c.x + (float) (i - 2) * gap - bw * 0.5f, cy - h * 0.5f, bw, std::max (0.1f, h));
            g.setGradientFill (ColourGradient (Colours::white.withAlpha (a), r.getX(), r.getY(), Colour (0xffd9d4ff).withAlpha (0.9f * a), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, bw * 0.5f);
        }
    }
}

//==============================================================================
// custom geometric letterforms in a 40 x 56 box (monoline, round caps) - identical to the SVG wordmark
static Path letterPath (juce_wchar ch)
{
    Path p;
    const float k = 0.5523f;
    switch (ch)
    {
        case 'A': p.startNewSubPath (2, 56); p.lineTo (20, 2); p.lineTo (38, 56); p.startNewSubPath (9.5f, 38); p.lineTo (30.5f, 38); break;
        case 'U': p.startNewSubPath (2, 2); p.lineTo (2, 38);
                  p.cubicTo (2, 38 + k * 18, 20 - k * 18, 56, 20, 56); p.cubicTo (20 + k * 18, 56, 38, 38 + k * 18, 38, 38); p.lineTo (38, 2); break;
        case 'R': p.startNewSubPath (2, 56); p.lineTo (2, 2); p.lineTo (20, 2);
                  p.cubicTo (20 + k * 14, 2, 34, 2 + (1 - k) * 14, 34, 16); p.cubicTo (34, 16 + k * 14, 20 + k * 14, 30, 20, 30); p.lineTo (2, 30);
                  p.startNewSubPath (19, 30); p.lineTo (37, 56); break;
        case 'L': p.startNewSubPath (2, 2); p.lineTo (2, 56); p.lineTo (36, 56); break;
        case 'O': p.addEllipse (2, 2, 36, 54); break;
        case 'Y': p.startNewSubPath (2, 2); p.lineTo (20, 30); p.lineTo (38, 2); p.startNewSubPath (20, 30); p.lineTo (20, 56); break;
        default: break;
    }
    return p;
}

float wordmarkWidth (float height) { return height / 56.0f * (8.0f * 40.0f + 7.0f * 17.0f); }

void drawWordmark (Graphics& g, Rectangle<float> area, float progress, float sweep)
{
    const String word = "AURALLOY";
    const float sc = area.getHeight() / 56.0f;
    const float thick = 7.0f * sc;
    const float total = 8.0f * 40.0f + 7.0f * 17.0f;
    for (int i = 0; i < word.length(); ++i)
    {
        const float t0 = (float) i / (float) word.length() * 0.55f;
        const float k = easeOutExpo ((progress - t0) / 0.45f);
        if (k <= 0.001f) continue;
        Path p = letterPath (word[i]);
        const float lx = (float) i * (40.0f + 17.0f);
        p.applyTransform (AffineTransform::scale (sc).translated (area.getX() + lx * sc, area.getY() + (1.0f - k) * 10.0f * sc));
        ColourGradient grad (col::mint, area.getX(), 0.0f, col::rose, area.getX() + total * sc, 0.0f, false);
        grad.addColour (0.5, col::violet);
        g.setGradientFill (grad);
        g.setOpacity (k);
        g.strokePath (p, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
        if (sweep >= 0.0f)
        {
            const float lc = (lx + 20.0f) / total;
            const float s = clamp01 (1.0f - std::abs (lc - sweep) / 0.12f);
            if (s > 0.01f) { g.setColour (Colours::white.withAlpha (0.65f * s * k)); g.strokePath (p, PathStrokeType (thick * 0.55f, PathStrokeType::curved, PathStrokeType::rounded)); }
        }
    }
}

//==============================================================================
AlloyLookAndFeel::AlloyLookAndFeel()
{
    setColour (Slider::textBoxTextColourId, col::text100);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, col::violet.withAlpha (0.5f));
    setColour (Label::textColourId, col::text100);
    setColour (TextButton::buttonColourId, Colour (0x1affffff));
    setColour (TextButton::textColourOffId, col::text100);
    setColour (TextButton::textColourOnId, col::text100);
    setColour (ToggleButton::textColourId, col::text100);
    setColour (ComboBox::backgroundColourId, Colour (0xff171929));
    setColour (ComboBox::textColourId, col::text100);
    setColour (ComboBox::outlineColourId, Colour (0x30ffffff));
    setColour (ComboBox::arrowColourId, col::text300);
    setColour (PopupMenu::backgroundColourId, Colour (0xff12131f));
    setColour (PopupMenu::textColourId, col::text100);
    setColour (PopupMenu::highlightedBackgroundColourId, col::violet.withAlpha (0.45f));
    setColour (PopupMenu::highlightedTextColourId, Colours::white);
    setColour (ScrollBar::thumbColourId, Colour (0x40ffffff));
    setColour (ListBox::backgroundColourId, Colours::transparentBlack);
    setColour (ProgressBar::backgroundColourId, Colour (0x25ffffff));
    setColour (ProgressBar::foregroundColourId, col::mint);
    setColour (AlertWindow::backgroundColourId, Colour (0xff14152a));
    setColour (AlertWindow::textColourId, col::text100);
    setColour (AlertWindow::outlineColourId, Colour (0x30ffffff));
    setColour (TextEditor::backgroundColourId, Colour (0xff1c1e33));
    setColour (TextEditor::textColourId, col::text100);
    setColour (TextEditor::outlineColourId, Colour (0x30ffffff));
    setColour (TextEditor::focusedOutlineColourId, col::violet);
    setColour (TooltipWindow::backgroundColourId, Colour (0xff1c1e33));
    setColour (TooltipWindow::textColourId, col::text100);
}

Font AlloyLookAndFeel::getTextButtonFont (TextButton&, int) { return uiFont (14.0f, true); }
Font AlloyLookAndFeel::getLabelFont (Label&)                { return uiFont (13.0f, true); }
Font AlloyLookAndFeel::getComboBoxFont (ComboBox&)          { return uiFont (14.0f); }
Font AlloyLookAndFeel::getPopupMenuFont()                   { return uiFont (14.5f); }

void AlloyLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s)
{
    auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float R = std::min (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = a0 + pos * (a1 - a0);
    const auto accent = s.findColour (Slider::rotarySliderFillColourId);
    const bool bipolar = (bool) s.getProperties()["bipolar"];
    const bool hot = s.isMouseOverOrDragging();
    const float ringR = R - 2.5f, th = std::max (3.0f, R * 0.09f);

    Path track;
    track.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, a0, a1, true);
    g.setColour (Colours::white.withAlpha (hot ? 0.20f : 0.10f));
    g.strokePath (track, PathStrokeType (th, PathStrokeType::curved, PathStrokeType::rounded));

    const float from = bipolar ? (a0 + a1) * 0.5f : a0;
    if (std::abs (angle - from) > 0.01f)
    {
        Path val;
        val.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, std::min (from, angle), std::max (from, angle), true);
        g.setColour (accent.withAlpha (hot ? 0.38f : 0.22f));
        g.strokePath (val, PathStrokeType (th + (hot ? 9.0f : 6.0f), PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (hot ? accent.brighter (0.25f) : accent);
        g.strokePath (val, PathStrokeType (th, PathStrokeType::curved, PathStrokeType::rounded));
    }

    const float br = R * 0.70f;
    g.setColour (Colour (0x66000000));
    g.fillEllipse (c.x - br, c.y - br + 3.0f, br * 2.0f, br * 2.0f);
    g.setGradientFill (ColourGradient (Colour (0xff383b57), c.x, c.y - br, Colour (0xff12132a), c.x, c.y + br, false));
    g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    g.setGradientFill (ColourGradient (accent.withAlpha (0.22f), c.x, c.y + br, accent.withAlpha (0.0f), c.x, c.y, false));
    g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    Path gloss;
    gloss.addEllipse (c.x - br * 0.68f, c.y - br * 0.95f, br * 1.36f, br * 0.8f);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.30f), c.x, c.y - br * 0.95f, Colours::white.withAlpha (0.0f), c.x, c.y - br * 0.15f, false));
    g.fillPath (gloss);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.55f), c.x - br, c.y - br, Colours::white.withAlpha (0.12f), c.x + br, c.y + br, false));
    g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.1f);

    const float ir = br * 0.68f;
    const Point<float> dot (c.x + ir * std::sin (angle), c.y - ir * std::cos (angle));
    g.setColour (accent.withAlpha (0.45f));
    g.fillEllipse (dot.x - 5.5f, dot.y - 5.5f, 11.0f, 11.0f);
    g.setColour (Colours::white);
    g.fillEllipse (dot.x - 2.8f, dot.y - 2.8f, 5.6f, 5.6f);
}

void AlloyLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider&)
{
    auto r = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float cy = r.getCentreY();
    Rectangle<float> track (r.getX() + 6.0f, cy - 2.5f, r.getWidth() - 12.0f, 5.0f);
    g.setColour (Colours::white.withAlpha (0.14f));
    g.fillRoundedRectangle (track, 2.5f);
    g.setGradientFill (alloyGradient (track.getX(), 0.0f, track.getRight(), 0.0f));
    g.fillRoundedRectangle (track.withRight (pos), 2.5f);
    g.setColour (Colour (0x66000000));
    g.fillEllipse (pos - 8.0f, cy - 5.0f, 16.0f, 16.0f);
    g.setGradientFill (ColourGradient (Colours::white, pos, cy - 8.0f, Colour (0xffc9ccdf), pos, cy + 8.0f, false));
    g.fillEllipse (pos - 8.0f, cy - 8.0f, 16.0f, 16.0f);
}

void AlloyLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool hl, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float tw = 38.0f, th = 22.0f;
    const bool hasText = b.getButtonText().isNotEmpty();
    Rectangle<float> track = hasText ? Rectangle<float> (r.getX() + 1.0f, r.getCentreY() - th * 0.5f, tw, th)
                                     : Rectangle<float> (0, 0, tw, th).withCentre (r.getCentre());
    const bool on = b.getToggleState();
    const Colour acc = b.findColour (ToggleButton::tickColourId);
    Path tp;
    tp.addRoundedRectangle (track, th * 0.5f);
    g.setGradientFill (ColourGradient ((on ? acc : Colours::white).withAlpha (on ? 0.95f : 0.16f), track.getX(), track.getY(),
                                       (on ? acc.darker (0.35f) : Colours::white).withAlpha (on ? 0.9f : 0.08f), track.getX(), track.getBottom(), false));
    g.fillPath (tp);
    g.setColour (Colours::white.withAlpha (hl ? 0.5f : 0.25f));
    g.strokePath (tp, PathStrokeType (1.0f));
    const float kd = th - 5.0f;
    const float kx = on ? track.getRight() - kd - 2.5f : track.getX() + 2.5f;
    g.setColour (Colour (0x66000000));
    g.fillEllipse (kx, track.getY() + 3.0f, kd, kd);
    g.setGradientFill (ColourGradient (Colours::white, kx, track.getY(), Colour (0xffd3d6e8), kx, track.getBottom(), false));
    g.fillEllipse (kx, track.getY() + 2.5f, kd, kd);
    if (hasText)
    {
        g.setColour (on ? col::text100 : col::text300);
        g.setFont (uiFont (13.5f));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (tw + 9.0f), Justification::centredLeft);
    }
}

void AlloyLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& bg, bool hl, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    Path p;
    p.addRoundedRectangle (r, r.getHeight() * 0.5f);
    const bool primary = (bool) b.getProperties()["primary"];
    const float a = ! b.isEnabled() ? 0.4f : (down ? 1.4f : (hl ? 1.18f : 1.0f));
    if (primary)
    {
        if (b.isEnabled())
        {
            g.setColour (col::violet.withAlpha (hl ? 0.40f : 0.26f));
            g.fillRoundedRectangle (r.expanded (3.0f), r.getHeight() * 0.5f + 3.0f);
        }
        auto grad = alloyGradient (r.getX(), 0.0f, r.getRight(), 0.0f);
        grad.multiplyOpacity (a > 1.0f ? 1.0f : a);
        g.setGradientFill (grad);
        g.fillPath (p);
        g.saveState();
        g.reduceClipRegion (p);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.38f), 0.0f, r.getY(), Colours::white.withAlpha (0.0f), 0.0f, r.getCentreY() + 2.0f, false));
        g.fillRect (r.withHeight (r.getHeight() * 0.6f));
        g.restoreState();
        g.setColour (Colours::white.withAlpha (0.55f));
        g.strokePath (p, PathStrokeType (1.0f));
        return;
    }
    g.setGradientFill (ColourGradient (bg.withMultipliedAlpha (1.3f * a), r.getX(), r.getY(), bg.withMultipliedAlpha (0.8f * a), r.getX(), r.getBottom(), false));
    g.fillPath (p);
    g.saveState();
    g.reduceClipRegion (p);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.18f), 0.0f, r.getY(), Colours::white.withAlpha (0.0f), 0.0f, r.getCentreY() + 2.0f, false));
    g.fillRect (r.withHeight (r.getHeight() * 0.6f));
    g.restoreState();
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (b.getToggleState() ? 0.8f : 0.46f), r.getX(), r.getY(), Colours::white.withAlpha (0.10f), r.getRight(), r.getBottom(), false));
    g.strokePath (p, PathStrokeType (1.0f));
}

void AlloyLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour (box.findColour (ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 9.0f, 1.0f);
    Path a;
    const float cx = (float) w - 18.0f, cy = (float) h * 0.5f;
    a.addTriangle (cx - 4.5f, cy - 2.0f, cx + 4.5f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (box.findColour (ComboBox::arrowColourId));
    g.fillPath (a);
}
} // namespace al
