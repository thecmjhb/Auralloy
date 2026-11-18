#include "Views.h"

using namespace juce;

namespace al
{
static String mmss (double s, bool tenths = false)
{
    s = std::max (0.0, s);
    const int t = (int) s;
    String r = String (t / 60) + ":" + String (t % 60).paddedLeft ('0', 2);
    if (tenths) r += "." + String ((int) ((s - t) * 10.0));
    return r;
}

// clips are coloured by SONG (so you can see who plays when); the track lane tells you the role
static Colour clipColour (const Track&, const Clip& c) { return sourceColour (c.colour); }

//==============================================================================
// TimelineView
TimelineView::TimelineView (Core& c) : core (c)
{
    tl = core.timeline();
    core.addChangeListener (this);
    startTimerHz (30);
    setWantsKeyboardFocus (false);
}
TimelineView::~TimelineView() { core.removeChangeListener (this); }

void TimelineView::changeListenerCallback (ChangeBroadcaster*)
{
    tl = core.timeline();
    updateTrackH();
    const double len = tl->length();
    if (mode == None && len > 0 && (lastLength <= 0 || std::abs (len - lastLength) > 0.3 * lastLength)) zoomFit();
    lastLength = len;
    clampScroll();
    repaint();
}

void TimelineView::updateTrackH()
{
    const int n = (int) tl->tracks.size();
    bool songs = false;
    for (const auto& t : tl->tracks) for (const auto& c : t.clips) if (c.songId >= 0) songs = true;
    rulerH = songs ? 56 : 30;
    if (n <= 0) { trackH = 76; return; }
    const int avail = getHeight() - rulerH - 8;
    trackH = std::max (56, std::min (92, avail / n - gap));
}

void TimelineView::resized()
{
    updateTrackH();
    if (needFit && getWidth() > 300 && ! tl->empty()) { zoomFit(); needFit = false; }
    clampScroll();
}

void TimelineView::clampScroll()
{
    scrollX = std::max (0.0, scrollX);
    const int content = rulerH + (int) tl->tracks.size() * (trackH + gap) + 8;
    scrollY = std::max (0, std::min (scrollY, std::max (0, content - getHeight())));
}

void TimelineView::zoomFit()
{
    const double len = std::max (8.0, tl->length());
    pps = std::max (4.0, (double) (getWidth() - headerW - 40) / (len * 1.02));
    scrollX = 0.0;
    repaint();
}

void TimelineView::zoomBy (double f)
{
    const double centre = scrollX + (double) (getWidth() - headerW) / pps * 0.5;
    pps = std::min (600.0, std::max (3.0, pps * f));
    scrollX = std::max (0.0, centre - (double) (getWidth() - headerW) / pps * 0.5);
    repaint();
}

double TimelineView::snap (double t, int exclude) const
{
    double best = t;
    if (core.snapBpm > 0)
    {
        const double beat = 60.0 / core.snapBpm;
        const double step = pps * beat > 16.0 ? beat : beat * 4.0;
        best = std::round ((t - core.snapOffset) / step) * step + core.snapOffset;
    }
    const double tol = 7.0 / pps;
    double bestD = std::abs (best - t) < tol * 4 ? std::abs (best - t) : 1e9;
    auto consider = [&] (double e) { if (std::abs (e - t) < tol && std::abs (e - t) < bestD) { best = e; bestD = std::abs (e - t); } };
    consider (core.position());
    for (const auto& tr : tl->tracks) for (const auto& c : tr.clips) if (c.id != exclude && ! core.selection.count (c.id)) { consider (c.start); consider (c.end()); }
    return std::max (0.0, best);
}

TimelineView::Hit TimelineView::hitTest (Point<int> p) const
{
    Hit h;
    for (int i = 0; i < (int) tl->tracks.size(); ++i)
    {
        const int top = trackTop (i);
        if (p.y < top || p.y >= top + trackH) continue;
        h.track = i;
        const auto& tr = tl->tracks[(size_t) i];
        for (int k = (int) tr.clips.size() - 1; k >= 0; --k)
        {
            const auto& c = tr.clips[(size_t) k];
            const float x0 = xOf (c.start), x1 = xOf (c.end());
            if (p.x < x0 - 2 || p.x > x1 + 2 || p.y < top + 4 || p.y > top + trackH - 4) continue;
            h.clip = c.id;
            if (std::abs (p.x - x0) <= 7) h.zone = 1;
            else if (std::abs (p.x - x1) <= 7) h.zone = 2;
            else if (p.y < top + 20 && p.x < x0 + 22) h.zone = 3;
            else if (p.y < top + 20 && p.x > x1 - 22) h.zone = 4;
            return h;
        }
        return h;
    }
    return h;
}

void TimelineView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawPanel (g, b.reduced (1.0f), 20.0f, Colours::white, 0.8f);
    g.setColour (Colour (0x38000000));
    g.fillRoundedRectangle (b.reduced (1.0f), 20.0f);

    const int W = getWidth(), H = getHeight();
    const double span = (double) (W - headerW) / pps;

    g.saveState();
    g.reduceClipRegion (getLocalBounds().reduced (1));

    // empty state
    if (tl->empty() && tl->tracks.empty())
    {
        const auto c = b.getCentre();
        LogoState ls; ls.wave = (float) Time::getMillisecondCounterHiRes() * 0.0016f; ls.glow = 0.25f; ls.alpha = 0.92f;
        drawLogoMark (g, Rectangle<float> (c.x - 54.0f, c.y - 84.0f, 108.0f, 108.0f), ls);
        g.setColour (col::text100);
        g.setFont (uiFont (18.0f, true));
        g.drawText ("Drop songs here", Rectangle<float> (c.x - 200.0f, c.y + 34.0f, 400.0f, 26.0f), Justification::centred);
        g.setColour (col::text300);
        g.setFont (uiFont (13.0f));
        g.drawText ("or double-click a song in the library  -  or open Forge to build a mashup automatically", Rectangle<float> (c.x - 300.0f, c.y + 62.0f, 600.0f, 22.0f), Justification::centred);
        g.restoreState();
        return;
    }

    // ---- ruler ----
    {
        g.setColour (Colour (0x30000000));
        g.fillRect (headerW, 0, W - headerW, rulerH);
        g.setFont (monoFont (11.0f));
        const bool grid = core.snapBpm > 0;
        const double barSec = grid ? 240.0 / core.snapBpm : 0.0, beatSec = barSec / 4.0;
        if (grid)
        {
            const int firstBar = (int) std::floor ((scrollX - core.snapOffset) / barSec);
            int labelEvery = 1; while (pps * barSec * labelEvery < 54.0) labelEvery *= 2;
            for (int bar = firstBar; core.snapOffset + bar * barSec <= scrollX + span; ++bar)
            {
                const double t = core.snapOffset + bar * barSec;
                const float x = xOf (t);
                if (x < headerW) continue;
                g.setColour (Colours::white.withAlpha (0.20f));
                g.fillRect (x, 14.0f, 1.0f, (float) H - 14.0f);
                if (bar % labelEvery == 0) { g.setColour (col::text100.withAlpha (0.8f)); g.drawText (String (bar + 1), Rectangle<int> ((int) x + 3, 3, 40, 12), Justification::centredLeft); }
                if (pps * beatSec > 14.0)
                    for (int q = 1; q < 4; ++q) { g.setColour (Colours::white.withAlpha (0.06f)); g.fillRect (xOf (t + q * beatSec), 20.0f, 1.0f, (float) H - 20.0f); }
            }
        }
        else
        {
            static const double steps[] = { 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300 };
            double step = 300; for (double s : steps) if (s * pps >= 70.0) { step = s; break; }
            for (double t = std::ceil (scrollX / step) * step; t <= scrollX + span; t += step)
            {
                const float x = xOf (t);
                g.setColour (Colours::white.withAlpha (0.14f)); g.fillRect (x, 14.0f, 1.0f, (float) H - 14.0f);
                g.setColour (col::text300); g.drawText (mmss (t), Rectangle<int> ((int) x + 3, 3, 60, 12), Justification::centredLeft);
            }
        }
    }


    // ---- song ribbon: who plays when ----
    if (rulerH > 30)
    {
        std::map<int, std::vector<std::pair<double, double>>> iv;
        std::map<int, int> colourOf;
        for (const auto& tr : tl->tracks) for (const auto& c : tr.clips) if (c.songId >= 0) { iv[c.songId].push_back ({ c.start, c.end() }); colourOf[c.songId] = c.colour; }
        for (auto& kv : iv)
        {
            auto& v2 = kv.second;
            std::sort (v2.begin(), v2.end());
            std::vector<std::pair<double, double>> merged;
            for (auto& p : v2) { if (! merged.empty() && p.first <= merged.back().second + 0.02) merged.back().second = std::max (merged.back().second, p.second); else merged.push_back (p); }
            const Colour sc = sourceColour (colourOf[kv.first]);
            String nm = "song";
            if (auto sd = core.songById (kv.first)) nm = sd->name;
            for (auto& p : merged)
            {
                const float x0 = std::max ((float) headerW, xOf (p.first)), x1 = std::min ((float) W, xOf (p.second));
                if (x1 <= x0) continue;
                Rectangle<float> r (x0, 33.0f, x1 - x0, 19.0f);
                g.setGradientFill (ColourGradient (sc.withAlpha (0.75f), r.getX(), 0.0f, sc.withAlpha (0.45f), r.getRight(), 0.0f, false));
                g.fillRoundedRectangle (r, 9.5f);
                g.setColour (Colours::white.withAlpha (0.25f));
                g.drawRoundedRectangle (r, 9.5f, 1.0f);
                if (r.getWidth() > 50.0f)
                {
                    g.setColour (col::obsidian950);
                    g.setFont (uiFont (11.0f, true));
                    g.drawText (nm, r.reduced (9.0f, 0.0f), Justification::centredLeft, true);
                }
            }
        }
    }

    // ---- tracks ----
    for (int i = 0; i < (int) tl->tracks.size(); ++i)
    {
        const auto& tr = tl->tracks[(size_t) i];
        const int top = trackTop (i);
        if (top + trackH < rulerH || top > H) continue;
        const Rectangle<float> lane ((float) headerW, (float) top, (float) (W - headerW), (float) trackH);
        g.setColour (Colours::white.withAlpha (i % 2 ? 0.020f : 0.035f));
        g.fillRect (lane);

        // clips
        for (const auto& c : tr.clips)
        {
            if (! c.src) continue;
            const float x0 = xOf (c.start), x1 = xOf (c.end());
            if (x1 < headerW || x0 > W) continue;
            const bool sel = core.selection.count (c.id) > 0;
            const Colour cc = clipColour (tr, c);
            const float a = tr.mute ? 0.35f : 1.0f;
            Rectangle<float> r (x0, (float) top + 4.0f, std::max (3.0f, x1 - x0), (float) trackH - 8.0f);
            if (sel) { g.setColour (cc.withAlpha (0.28f)); g.fillRoundedRectangle (r.expanded (3.0f), 11.0f); }
            g.setGradientFill (ColourGradient (cc.withAlpha (0.34f * a), r.getX(), r.getY(), cc.withAlpha (0.16f * a), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, 8.0f);

            // waveform
            if (auto pk = peaksFor (c.src))
            {
                g.saveState();
                g.reduceClipRegion (r.toNearestInt().reduced (1));
                const float mid = r.getCentreY() + 5.0f, half = (r.getHeight() - 22.0f) * 0.5f;
                const int xs = std::max ((int) std::floor (r.getX()), headerW), xe = std::min ((int) std::ceil (r.getRight()), W);
                g.setColour (cc.brighter (0.3f).withAlpha (0.92f * a));
                const double gain = std::pow (10.0, (double) c.gainDb / 20.0);
                const int nb = (int) pk->mn.size();
                for (int x = xs; x < xe; ++x)
                {
                    const double tA = (c.offset + ((double) x - x0) / pps), tB = (c.offset + ((double) x + 1.0 - x0) / pps);
                    int i0 = (int) std::floor (tA * pk->sr / Peaks::block), i1 = (int) std::floor (tB * pk->sr / Peaks::block);
                    if (i1 < i0) i1 = i0;
                    if (i0 >= nb || i1 < 0) continue;
                    i0 = std::max (0, i0); i1 = std::min (nb - 1, i1);
                    float mn = 1e9f, mx = -1e9f;
                    for (int k = i0; k <= i1; ++k) { mn = std::min (mn, pk->mn[(size_t) k]); mx = std::max (mx, pk->mx[(size_t) k]); }
                    const float hi = std::min (1.0f, (float) (mx * gain * 1.4)), lo = std::max (-1.0f, (float) (mn * gain * 1.4));
                    g.fillRect ((float) x, mid - hi * half, 1.0f, std::max (1.2f, (hi - lo) * half));
                }
                g.restoreState();
            }

            // fades
            g.setColour (Colours::black.withAlpha (0.28f));
            if (c.fadeIn > 0.01) { Path p; const float fx = xOf (c.start + c.fadeIn); p.addTriangle (x0, r.getY(), fx, r.getY(), x0, r.getBottom()); g.fillPath (p); }
            if (c.fadeOut > 0.01) { Path p; const float fx = xOf (c.end() - c.fadeOut); p.addTriangle (x1, r.getY(), fx, r.getY(), x1, r.getBottom()); g.fillPath (p); }
            // rim
            if (sel) { g.setGradientFill (alloyGradient (r.getX(), r.getY(), r.getRight(), r.getBottom())); g.drawRoundedRectangle (r, 8.0f, 2.0f); }
            else     { g.setColour (cc.withAlpha (0.75f * a)); g.drawRoundedRectangle (r, 8.0f, 1.0f); }
            // name chip
            g.setColour (Colours::black.withAlpha (0.35f));
            const String nm = String (c.name);
            const float cw = std::min (r.getWidth() - 10.0f, 12.0f + (float) nm.length() * 6.4f);
            if (cw > 24.0f)
            {
                g.fillRoundedRectangle (r.getX() + 5.0f, r.getY() + 4.0f, cw, 15.0f, 7.5f);
                g.setColour (Colours::white.withAlpha (0.95f));
                g.setFont (uiFont (10.5f, true));
                g.drawText (nm, Rectangle<float> (r.getX() + 11.0f, r.getY() + 4.0f, cw - 10.0f, 15.0f), Justification::centredLeft, true);
            }
            if (std::abs (c.gainDb) > 0.15f && r.getWidth() > 70.0f)
            {
                g.setColour (Colours::white.withAlpha (0.7f)); g.setFont (monoFont (10.0f));
                g.drawText ((c.gainDb > 0 ? "+" : "") + String (c.gainDb, 1) + " dB", Rectangle<float> (r.getRight() - 70.0f, r.getBottom() - 15.0f, 64.0f, 12.0f), Justification::centredRight);
            }
        }
    }

    // region
    if (core.hasRegion())
    {
        const float rx0 = std::max ((float) headerW, xOf (core.regionStart())), rx1 = std::min ((float) W, xOf (core.regionEnd()));
        g.setColour (col::amber.withAlpha (0.10f));
        g.fillRect (rx0, (float) rulerH - 6.0f, rx1 - rx0, (float) H - rulerH + 6.0f);
        g.setColour (col::amber.withAlpha (0.9f));
        g.fillRect (xOf (core.regionStart()) - 1.0f, (float) rulerH - 6.0f, 2.0f, (float) H);
        g.fillRect (xOf (core.regionEnd()) - 1.0f, (float) rulerH - 6.0f, 2.0f, (float) H);
        g.fillRect (rx0, 0.0f, rx1 - rx0, 3.0f);
    }

    // snap guide
    if (snapGuide >= 0 && mode != None)
    {
        g.setColour (col::violet.withAlpha (0.85f));
        g.fillRect (xOf (snapGuide) - 0.5f, (float) rulerH, 1.0f, (float) H);
    }

    // track headers (drawn last so clips scroll under them)
    g.setColour (Colour (0xff0b0d17));
    g.fillRect (0, 0, headerW, H);
    g.setColour (Colours::white.withAlpha (0.08f));
    g.fillRect (headerW - 1, 0, 1, H);
    for (int i = 0; i < (int) tl->tracks.size(); ++i)
    {
        const auto& tr = tl->tracks[(size_t) i];
        const int top = trackTop (i);
        if (top + trackH < rulerH || top > H) continue;
        Rectangle<float> hr (6.0f, (float) top + 3.0f, (float) headerW - 14.0f, (float) trackH - 6.0f);
        drawPanel (g, hr, 12.0f, Colours::white, 0.9f);
        const Colour rc = tr.role >= 0 ? roleColour (tr.role) : sourceColour (tr.colour);
        g.setColour (rc);
        g.fillRoundedRectangle (hr.getX() + 6.0f, hr.getY() + 10.0f, 4.0f, 16.0f, 2.0f);
        g.setColour (col::text100);
        g.setFont (uiFont (13.0f, true));
        g.drawText (String (tr.name), Rectangle<float> (hr.getX() + 16.0f, hr.getY() + 6.0f, hr.getWidth() - 74.0f, 24.0f), Justification::centredLeft, true);
        auto btn = [&] (Rectangle<float> rr, const char* t, bool on, Colour c)
        {
            g.setColour (on ? c.withAlpha (0.9f) : Colours::white.withAlpha (0.10f));
            g.fillRoundedRectangle (rr, 6.0f);
            g.setColour (on ? col::obsidian950 : col::text300);
            g.setFont (uiFont (11.0f, true));
            g.drawText (t, rr, Justification::centred);
        };
        btn (Rectangle<float> (hr.getRight() - 54.0f, hr.getY() + 8.0f, 22.0f, 20.0f), "M", tr.mute, col::amber);
        btn (Rectangle<float> (hr.getRight() - 28.0f, hr.getY() + 8.0f, 22.0f, 20.0f), "S", tr.solo, col::mint);
        // volume: slim bar with the dB value to its right (never collides with the name)
        const Rectangle<float> vr (hr.getX() + 10.0f, hr.getBottom() - 18.0f, hr.getWidth() - 62.0f, 7.0f);
        g.setColour (Colours::white.withAlpha (0.12f)); g.fillRoundedRectangle (vr, 3.5f);
        const float vp = jlimit (0.0f, 1.0f, (tr.volDb + 30.0f) / 36.0f);
        g.setGradientFill (alloyGradient (vr.getX(), 0.0f, vr.getRight(), 0.0f));
        g.fillRoundedRectangle (vr.withWidth (std::max (4.0f, vr.getWidth() * vp)), 3.5f);
        g.setColour (col::text300); g.setFont (monoFont (10.0f));
        g.drawText (String (tr.volDb, 1), Rectangle<float> (vr.getRight() + 4.0f, hr.getBottom() - 23.0f, 44.0f, 16.0f), Justification::centredLeft);
    }
    g.setColour (Colour (0xff0b0d17));
    g.fillRect (0, 0, headerW, rulerH);
    if (rulerH > 30) { g.setColour (col::text500); g.setFont (uiFont (10.5f, true)); g.drawText ("SONGS", Rectangle<int> (14, 33, headerW - 20, 20), Justification::centredLeft); }

    // playhead
    {
        const float px = xOf (core.position());
        if (px >= headerW - 1 && px <= W)
        {
            const float pulse = playPulse.v;
            g.setColour (col::violet.withAlpha (0.14f + 0.12f * pulse));
            g.fillRoundedRectangle (px - 5.0f, 12.0f, 10.0f, (float) H - 16.0f, 5.0f);
            g.setGradientFill (ColourGradient (col::mint, px, 12.0f, col::rose, px, (float) H, false));
            g.fillRect (px - 1.0f, 14.0f, 2.0f, (float) H - 14.0f);
            Path tri; tri.addTriangle (px - 7.0f, 6.0f, px + 7.0f, 6.0f, px, 18.0f);
            g.setColour (Colours::white);
            g.fillPath (tri);
        }
    }
    g.restoreState();
}

void TimelineView::timerCallback()
{
    const double pos = core.position();
    playPulse.step (core.isPlaying() ? 1.0f : 0.0f, 0.033f, 0.25f);
    if (core.isPlaying() && mode == None)
    {
        const double span = (double) (getWidth() - headerW) / pps;
        if (pos > scrollX + span * 0.92 || pos < scrollX) { scrollX = std::max (0.0, pos - span * 0.08); repaint(); }
    }
    if (std::abs (pos - lastPos) > 1e-4 || tl->empty()) { lastPos = pos; repaint(); }
}

void TimelineView::mouseDown (const MouseEvent& e)
{
    movedPx = 0;
    const auto p = e.getPosition();
    const Hit h = hitTest (p);
    if (e.mods.isPopupMenu()) { showMenu (h, p); return; }

    if (p.y < rulerH && p.x >= headerW) { mode = Seek; core.seek (std::max (0.0, timeAt ((float) p.x))); return; }

    if (p.x < headerW)
    {
        const int ti = (p.y + scrollY - rulerH) / (trackH + gap);
        if (ti >= 0 && ti < (int) tl->tracks.size())
        {
            const auto& tr = tl->tracks[(size_t) ti];
            const int top = trackTop (ti);
            Rectangle<float> hr (6.0f, (float) top + 3.0f, (float) headerW - 14.0f, (float) trackH - 6.0f);
            if (Rectangle<float> (hr.getRight() - 54.0f, hr.getY() + 8.0f, 22.0f, 20.0f).contains (p.toFloat())) { core.setTrackProps (tr.id, tr.volDb, tr.pan, ! tr.mute, tr.solo, String (tr.name)); return; }
            if (Rectangle<float> (hr.getRight() - 28.0f, hr.getY() + 8.0f, 22.0f, 20.0f).contains (p.toFloat())) { core.setTrackProps (tr.id, tr.volDb, tr.pan, tr.mute, ! tr.solo, String (tr.name)); return; }
            if (p.y > top + trackH - 26) { mode = TrackVol; volTrack = ti; volStart = tr.volDb; base = core.editCopy(); core.beginDrag(); grabX = (float) p.x; }
        }
        return;
    }

    if (h.clip >= 0)
    {
        if (! core.selection.count (h.clip)) { if (! e.mods.isShiftDown()) core.selection.clear(); core.selection.insert (h.clip); }
        else if (e.mods.isShiftDown()) core.selection.erase (h.clip);
        base = core.editCopy();
        core.beginDrag();
        dragClip = h.clip; dragTrack = h.track;
        const Clip* c = base.findClip (h.clip);
        grabOffset = timeAt ((float) p.x) - c->start;
        mode = h.zone == 1 ? TrimL : h.zone == 2 ? TrimR : h.zone == 3 ? FadeIn : h.zone == 4 ? FadeOut : Move;
        repaint();
        return;
    }
    if (e.mods.isShiftDown()) { mode = Region; anchorT = snap (timeAt ((float) p.x)); return; }
    core.selection.clear();
    mode = Seek;
    core.seek (std::max (0.0, timeAt ((float) p.x)));
    repaint();
}

void TimelineView::mouseDrag (const MouseEvent& e)
{
    movedPx = std::max (movedPx, std::abs (e.getDistanceFromDragStartX()) + std::abs (e.getDistanceFromDragStartY()));
    const auto p = e.getPosition();
    if (mode == Seek) { core.seek (std::max (0.0, timeAt ((float) p.x))); return; }
    if (mode == Region)
    {
        const double t = snap (timeAt ((float) p.x));
        const double a = std::min (anchorT, t), b = std::max (anchorT, t);
        if (b - a > 0.05) core.setRegion (a, b);
        repaint();
        return;
    }
    if (mode == TrackVol && volTrack >= 0)
    {
        Timeline t = base;
        t.tracks[(size_t) volTrack].volDb = jlimit (-30.0f, 6.0f, volStart + (float) (p.x - grabX) * 0.25f);
        core.preview (std::move (t));
        return;
    }
    if (mode == None || dragClip < 0) return;

    Timeline t = base;
    Clip* c = t.findClip (dragClip);
    const Clip* c0 = base.findClip (dragClip);
    if (c == nullptr || c0 == nullptr) return;
    snapGuide = -1.0;
    const double now = timeAt ((float) p.x);

    if (mode == Move)
    {
        double ns = snap (now - grabOffset, dragClip);
        // snap the clip's end as well
        const double ne = snap (now - grabOffset + c0->length, dragClip);
        if (std::abs (ne - (now - grabOffset + c0->length)) < std::abs (ns - (now - grabOffset))) ns = ne - c0->length;
        ns = std::max (0.0, ns);
        snapGuide = ns;
        const double delta = ns - c0->start;
        for (auto& tr : t.tracks) for (auto& cl : tr.clips) if (core.selection.count (cl.id)) { const Clip* o = base.findClip (cl.id); cl.start = std::max (0.0, o->start + delta); }
        if (core.selection.size() == 1)
        {
            const int ti = jlimit (0, (int) t.tracks.size() - 1, (p.y + scrollY - rulerH) / (trackH + gap));
            for (int i = 0; i < (int) t.tracks.size(); ++i)
            {
                auto& cl = t.tracks[(size_t) i].clips;
                auto it = std::find_if (cl.begin(), cl.end(), [&] (const Clip& x) { return x.id == dragClip; });
                if (it != cl.end() && i != ti) { Clip moved = *it; cl.erase (it); t.tracks[(size_t) ti].clips.push_back (moved); break; }
            }
        }
    }
    else if (mode == TrimL)
    {
        double ns = snap (now, dragClip);
        ns = std::max (c0->start - c0->offset, std::min (ns, c0->end() - 0.05));
        const double d = ns - c0->start;
        c->start = ns; c->offset = c0->offset + d; c->length = c0->length - d;
        c->fadeIn = std::min (c->fadeIn, c->length);
        snapGuide = ns;
    }
    else if (mode == TrimR)
    {
        double ne = snap (now, dragClip);
        const double maxEnd = c0->start + (c0->src ? c0->src->seconds() - c0->offset : c0->length);
        ne = std::max (c0->start + 0.05, std::min (ne, maxEnd));
        c->length = ne - c0->start;
        c->fadeOut = std::min (c->fadeOut, c->length);
        snapGuide = ne;
    }
    else if (mode == FadeIn)  c->fadeIn  = jlimit (0.0, c0->length - c0->fadeOut, now - c0->start);
    else if (mode == FadeOut) c->fadeOut = jlimit (0.0, c0->length - c0->fadeIn, c0->end() - now);
    core.preview (std::move (t));
}

void TimelineView::mouseUp (const MouseEvent&)
{
    if (mode == Move || mode == TrimL || mode == TrimR || mode == FadeIn || mode == FadeOut || mode == TrackVol) core.endDrag();
    mode = None; dragClip = -1; volTrack = -1; snapGuide = -1.0;
    repaint();
}

void TimelineView::mouseDoubleClick (const MouseEvent& e)
{
    const Hit h = hitTest (e.getPosition());
    if (h.clip < 0 && e.x > headerW) core.clearRegion();
    else if (h.clip >= 0)
    {
        // double-click a clip: loop region = the clip
        if (const Clip* c = tl->findClip (h.clip)) { core.setRegion (c->start, c->end()); core.seek (c->start); }
    }
}

void TimelineView::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        scrollX = std::max (0.0, scrollX - (double) (w.deltaX != 0 ? w.deltaX : w.deltaY) * 420.0 / pps);
        repaint(); return;
    }
    if (e.mods.isShiftDown() || e.x < headerW) { scrollY -= (int) (w.deltaY * 160.0f); clampScroll(); repaint(); return; }
    const double tm = timeAt ((float) e.x);
    const double f = std::exp ((double) w.deltaY * 0.7);
    const double frac = (double) (e.x - headerW) / std::max (1, getWidth() - headerW);
    pps = std::min (600.0, std::max (3.0, pps * f));
    const double span = (double) (getWidth() - headerW) / pps;
    scrollX = std::max (0.0, tm - frac * span);
    repaint();
}

void TimelineView::showMenu (const Hit& h, Point<int> p)
{
    PopupMenu m;
    if (h.clip >= 0)
    {
        if (! core.selection.count (h.clip)) { core.selection.clear(); core.selection.insert (h.clip); repaint(); }
        m.addItem (1, "Split at playhead     S");
        m.addItem (2, "Duplicate               Ctrl+D");
        m.addItem (3, "Reverse");
        m.addItem (4, "Normalize");
        m.addItem (5, "Loop this clip");
        m.addSeparator();
        m.addItem (6, "Delete                   Del");
    }
    else
    {
        m.addItem (10, "Add empty track");
        if (h.track >= 0) m.addItem (11, "Remove this track");
        if (core.hasRegion()) m.addItem (12, "Clear region");
        m.addItem (13, "Zoom to fit");
    }
    (void) p;
    m.showMenuAsync (PopupMenu::Options(), [this, h] (int r)
    {
        switch (r)
        {
            case 1: core.splitAtPlayhead(); break;
            case 2: core.duplicateSelection(); break;
            case 3: if (h.clip >= 0) core.reverseClip (h.clip); break;
            case 4: if (h.clip >= 0) core.normalizeClip (h.clip); break;
            case 5: if (const Clip* c = tl->findClip (h.clip)) core.setRegion (c->start, c->end()); break;
            case 6: core.deleteSelection(); break;
            case 10: core.addTrack ("Track " + String ((int) tl->tracks.size() + 1)); break;
            case 11: if (h.track >= 0 && h.track < (int) tl->tracks.size()) core.removeTrack (tl->tracks[(size_t) h.track].id); break;
            case 12: core.clearRegion(); break;
            case 13: zoomFit(); break;
            default: break;
        }
    });
}

//==============================================================================
// Library
LibraryView::LibraryView (Core& c) : core (c)
{
    list.setModel (this);
    list.setRowHeight (62);
    list.setColour (ListBox::backgroundColourId, Colours::transparentBlack);
    list.setColour (ListBox::outlineColourId, Colours::transparentBlack);
    list.setWantsKeyboardFocus (false);
    addAndMakeVisible (list);
    addBtn.setWantsKeyboardFocus (false);
    addBtn.getProperties().set ("primary", true);
    addBtn.onClick = [this] { if (onAdd) onAdd(); };
    addAndMakeVisible (addBtn);
    core.addChangeListener (this);
    startTimerHz (12);
}
LibraryView::~LibraryView() { core.removeChangeListener (this); }

void LibraryView::paint (Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat().reduced (1.0f), 20.0f);
    g.setColour (col::text300);
    g.setFont (uiFont (11.5f, true));
    g.drawText ("LIBRARY", Rectangle<int> (18, 16, 100, 22), Justification::centredLeft);
    if (core.library.empty())
    {
        g.setColour (col::text500);
        g.setFont (uiFont (13.5f));
        g.drawFittedText ("Drop songs here\nwav / mp3 / flac / ogg / m4a", getLocalBounds().reduced (24).withTrimmedTop (30), Justification::centred, 3);
    }
}

void LibraryView::resized()
{
    addBtn.setBounds (getWidth() - 122, 10, 108, 32);
    list.setBounds (getLocalBounds().withTrimmedTop (52).reduced (8, 4));
}

void LibraryView::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) core.library.size()) return;
    const auto& s = *core.library[(size_t) row];
    auto r = Rectangle<float> (2.0f, 2.0f, (float) w - 4.0f, (float) h - 4.0f);
    if (selected) { g.setColour (col::violet.withAlpha (0.14f)); g.fillRoundedRectangle (r, 14.0f); }
    g.setColour (Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (r, 14.0f, 1.0f);
    const Colour sc = sourceColour (s.colour);
    g.setColour (sc);
    g.fillRoundedRectangle (r.getX() + 8.0f, r.getY() + 14.0f, 4.0f, r.getHeight() - 28.0f, 2.0f);
    g.setColour (col::text100);
    g.setFont (uiFont (14.0f, true));
    g.drawText (s.name, Rectangle<float> (r.getX() + 20.0f, r.getY() + 8.0f, r.getWidth() - 86.0f, 20.0f), Justification::centredLeft, true);

    const int st = s.state.load();
    g.setFont (uiFont (12.0f));
    Rectangle<float> line (r.getX() + 20.0f, r.getY() + 31.0f, r.getWidth() - 86.0f, 18.0f);
    if (st == 3) { g.setColour (col::danger); g.drawText (s.error.isNotEmpty() ? s.error : "Can't read this file", line, Justification::centredLeft); }
    else if (st < 2)
    {
        g.setColour (col::text300);
        g.drawText (st == 0 ? "Loading..." : "Analysing tempo and key...", line.withTrimmedLeft (18.0f), Justification::centredLeft);
        Path arc; arc.addCentredArc (line.getX() + 7.0f, line.getCentreY(), 6.0f, 6.0f, 0.0f, phase, phase + 4.0f, true);
        g.setColour (col::mint); g.strokePath (arc, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    else if (! s.an.ok) { g.setColour (col::amber); g.drawText ("No steady beat found", line, Justification::centredLeft); }
    else
    {
        const int num = s.an.camelot.empty() ? 1 : String (s.an.camelot).getIntValue();
        const Colour kc = Colour::fromHSV (((float) ((num + 5) % 12)) / 12.0f, 0.55f, 0.98f, 1.0f);
        auto chip = [&] (Rectangle<float> rr, const String& t, Colour c)
        {
            g.setColour (c.withAlpha (0.20f)); g.fillRoundedRectangle (rr, rr.getHeight() * 0.5f);
            g.setColour (c.withAlpha (0.85f)); g.drawRoundedRectangle (rr, rr.getHeight() * 0.5f, 1.0f);
            g.setColour (col::text100); g.setFont (monoFont (10.5f)); g.drawText (t, rr, Justification::centred);
        };
        const String bpm = String (s.an.bpm, 1) + " BPM", key = String (s.an.camelot);
        chip (Rectangle<float> (line.getX(), line.getY() + 1.0f, 74.0f, 16.0f), bpm, col::mint);
        chip (Rectangle<float> (line.getX() + 80.0f, line.getY() + 1.0f, 38.0f, 16.0f), key, kc);
        if (s.stemState.load() == 2) chip (Rectangle<float> (line.getX() + 124.0f, line.getY() + 1.0f, 46.0f, 16.0f), "STEMS", col::rose);
        else if (s.stemState.load() == 1) chip (Rectangle<float> (line.getX() + 124.0f, line.getY() + 1.0f, 46.0f, 16.0f), String (roundToInt (s.progress.load() * 100.0f)) + "%", col::rose);
    }
    // include toggle
    {
        Rectangle<float> t (r.getRight() - 50.0f, r.getCentreY() - 10.0f, 38.0f, 20.0f);
        const bool on = s.inForge;
        g.setColour (on ? col::mint.withAlpha (0.9f) : Colours::white.withAlpha (0.14f));
        g.fillRoundedRectangle (t, 10.0f);
        g.setColour (Colours::white);
        g.fillEllipse (on ? t.getRight() - 17.0f : t.getX() + 3.0f, t.getY() + 3.0f, 14.0f, 14.0f);
    }
}

void LibraryView::listBoxItemClicked (int row, const MouseEvent& e)
{
    if (row < 0 || row >= (int) core.library.size()) return;
    auto& s = *core.library[(size_t) row];
    if (e.mods.isPopupMenu())
    {
        PopupMenu m;
        m.addItem (1, "Add to timeline at the playhead");
        m.addItem (2, "Split stems (vocals, drums, bass, other)", s.state.load() == 2 && s.stemState.load() != 1);
        m.addItem (3, s.inForge ? "Exclude from the mashup" : "Include in the mashup");
        m.addItem (4, "Show in folder");
        m.addSeparator();
        m.addItem (5, "Remove from library");
        const int id = s.id;
        m.showMenuAsync (PopupMenu::Options(), [this, id] (int r)
        {
            auto sd = core.songById (id);
            if (! sd) return;
            if (r == 1) core.placeSong (sd, core.position());
            else if (r == 2) core.splitStems (sd);
            else if (r == 3) { sd->inForge = ! sd->inForge; core.sendChangeMessage(); }
            else if (r == 4) sd->file.revealToUser();
            else if (r == 5) core.removeSong (id);
        });
        return;
    }
    if (e.x > list.getWidth() - 64) { s.inForge = ! s.inForge; core.sendChangeMessage(); }
}

void LibraryView::listBoxItemDoubleClicked (int row, const MouseEvent& e)
{
    if (row < 0 || row >= (int) core.library.size() || e.x > list.getWidth() - 64) return;
    core.placeSong (core.library[(size_t) row], core.position());
}

void LibraryView::deleteKeyPressed (int row)
{
    if (row >= 0 && row < (int) core.library.size()) core.removeSong (core.library[(size_t) row]->id);
}

//==============================================================================
SpectrumView::SpectrumView (Core& c) : core (c), fft (12)
{
    data.assign (8192, 0.0f);
    window.resize (4096);
    for (int i = 0; i < 4096; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * 3.14159265f * (float) i / 4095.0f);
    bands.fill (-100.0f); peaks.fill (-100.0f);
    startTimerHz (30);
}

void SpectrumView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawPanel (g, b.reduced (1.0f), 16.0f, Colours::white, 0.8f);
    auto r = b.reduced (10.0f, 10.0f);
    r.removeFromBottom (12.0f);
    const float bw = r.getWidth() / (float) kBands;
    g.setColour (Colours::white.withAlpha (0.06f));
    for (int db = -60; db <= 0; db += 20) g.fillRect (r.getX(), r.getBottom() - r.getHeight() * (float) (db + 80) / 80.0f, r.getWidth(), 1.0f);
    for (int i = 0; i < kBands; ++i)
    {
        const float v = clamp01 ((bands[(size_t) i] + 80.0f) / 80.0f), pk = clamp01 ((peaks[(size_t) i] + 80.0f) / 80.0f);
        const float x = r.getX() + (float) i * bw, h = v * r.getHeight();
        const float t = (float) i / (float) (kBands - 1);
        const Colour c = t < 0.5f ? col::mint.interpolatedWith (col::violet, t * 2.0f) : col::violet.interpolatedWith (col::rose, (t - 0.5f) * 2.0f);
        g.setGradientFill (ColourGradient (c.brighter (0.2f), 0.0f, r.getBottom() - h, c.withAlpha (0.2f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (x + 0.5f, r.getBottom() - h, std::max (1.5f, bw - 1.2f), std::max (1.0f, h), 1.2f);
        g.setColour (Colours::white.withAlpha (0.55f));
        g.fillRect (x + 0.5f, r.getBottom() - pk * r.getHeight() - 1.5f, std::max (1.5f, bw - 1.2f), 1.5f);
    }
    g.setColour (col::text500);
    g.setFont (monoFont (9.5f));
    g.drawText ("50", Rectangle<float> (r.getX(), r.getBottom() + 1.0f, 40.0f, 12.0f), Justification::left);
    g.drawText ("1k", Rectangle<float> (r.getX() + r.getWidth() * 0.43f, r.getBottom() + 1.0f, 40.0f, 12.0f), Justification::left);
    g.drawText ("10k", Rectangle<float> (r.getRight() - 40.0f, r.getBottom() + 1.0f, 40.0f, 12.0f), Justification::right);
}

void SpectrumView::timerCallback()
{
    if (! isShowing()) return;
    core.copyAnalyzer (data.data(), 4096);
    for (int i = 0; i < 4096; ++i) data[(size_t) i] *= window[(size_t) i];
    std::fill (data.begin() + 4096, data.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (data.data());
    const double sr = std::max (8000.0, core.sampleRate()), binHz = sr / 4096.0;
    for (int i = 0; i < kBands; ++i)
    {
        const double f0 = 40.0 * std::pow (17000.0 / 40.0, (double) i / kBands), f1 = 40.0 * std::pow (17000.0 / 40.0, (double) (i + 1) / kBands);
        int b0 = std::max (1, (int) std::floor (f0 / binHz)), b1 = std::min (2047, std::max (b0, (int) std::ceil (f1 / binHz)));
        float m = 0.0f;
        for (int b = b0; b <= b1; ++b) m = std::max (m, data[(size_t) b]);
        const float db = 20.0f * std::log10 (std::max (1e-7f, m / 1024.0f)) + 2.0f * (float) std::log2 ((f0 + f1) * 0.5 / 1000.0);
        auto& s = bands[(size_t) i];
        s = db > s ? s * 0.4f + db * 0.6f : s * 0.86f + db * 0.14f;
        auto& p = peaks[(size_t) i];
        p = s > p ? s : p - 0.6f;
    }
    repaint();
}

MeterView::MeterView (Core& c) : core (c) { startTimerHz (30); }

void MeterView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawPanel (g, b.reduced (1.0f), 16.0f, Colours::white, 0.8f);
    auto r = b.reduced (10.0f, 14.0f);
    r.removeFromBottom (14.0f);
    const float cw = (r.getWidth() - 8.0f) / 3.0f;
    drawBar (g, Rectangle<float> (r.getX(), r.getY(), cw, r.getHeight()), lvl[0], hold[0], false);
    drawBar (g, Rectangle<float> (r.getX() + cw + 4.0f, r.getY(), cw, r.getHeight()), lvl[1], hold[1], false);
    drawBar (g, Rectangle<float> (r.getX() + 2.0f * (cw + 4.0f), r.getY(), cw, r.getHeight()), gr, 0.0f, true);
    g.setColour (col::text500);
    g.setFont (monoFont (9.5f));
    g.drawText ("L", Rectangle<float> (r.getX(), r.getBottom() + 1.0f, cw, 12.0f), Justification::centred);
    g.drawText ("R", Rectangle<float> (r.getX() + cw + 4.0f, r.getBottom() + 1.0f, cw, 12.0f), Justification::centred);
    g.drawText ("GR", Rectangle<float> (r.getX() + 2.0f * (cw + 4.0f), r.getBottom() + 1.0f, cw, 12.0f), Justification::centred);
    if (hold[0] > -0.5f || hold[1] > -0.5f) { g.setColour (col::danger); g.setFont (uiFont (10.0f, true)); g.drawText ("CLIP", Rectangle<float> (r.getX(), b.getY() + 2.0f, r.getWidth(), 12.0f), Justification::centred); }
}

void MeterView::drawBar (Graphics& g, Rectangle<float> r, float v, float h, bool grBar)
{
    g.setColour (Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (r, 3.0f);
    if (grBar)
    {
        const float hh = std::min (1.0f, v / 18.0f) * r.getHeight();
        g.setColour (col::amber);
        g.fillRoundedRectangle (r.withHeight (std::max (0.0f, hh)), 3.0f);
        return;
    }
    const float hh = clamp01 ((v + 60.0f) / 60.0f) * r.getHeight();
    g.setGradientFill (ColourGradient (col::rose, 0.0f, r.getY(), col::mint, 0.0f, r.getBottom(), false));
    g.saveState();
    g.reduceClipRegion (r.getSmallestIntegerContainer());
    g.fillRoundedRectangle (r.withTop (r.getBottom() - hh), 3.0f);
    g.restoreState();
    g.setColour (Colours::white.withAlpha (0.85f));
    g.fillRect (r.getX(), r.getBottom() - clamp01 ((h + 60.0f) / 60.0f) * r.getHeight() - 1.0f, r.getWidth(), 1.5f);
}

void MeterView::timerCallback()
{
    const float pk[2] = { core.peakL.exchange (0.0f), core.peakR.exchange (0.0f) };
    for (int i = 0; i < 2; ++i)
    {
        const float db = 20.0f * std::log10 (std::max (pk[i], 1e-5f));
        lvl[i] = db > lvl[i] ? db : lvl[i] - 2.2f;
        if (db > hold[i]) { hold[i] = db; holdAge[i] = 0; } else if (++holdAge[i] > 45) hold[i] -= 1.0f;
        hold[i] = std::max (hold[i], -70.0f); lvl[i] = std::max (lvl[i], -70.0f);
    }
    gr = std::max (core.grDb.load(), gr - 0.8f);
    repaint();
}

//==============================================================================
TransportBar::TransportBar (Core& c)
    : core (c), toStart (IconButton::Prev, col::violet), play (IconButton::Play, col::violet), stop (IconButton::Stop, col::violet), loop (IconButton::Loop, col::amber),
      split (IconButton::Split, col::mint), dup (IconButton::Duplicate, col::mint), del (IconButton::Delete, col::rose)
{
    for (auto* b : { (Button*) &toStart, (Button*) &play, (Button*) &stop, (Button*) &loop, (Button*) &split, (Button*) &dup, (Button*) &del })
    { addAndMakeVisible (*b); b->setWantsKeyboardFocus (false); }
    toStart.onClick = [this] { core.seek (core.hasRegion() ? core.regionStart() : 0.0); };
    play.onClick = [this] { core.togglePlay(); };
    stop.onClick = [this] { core.stop(); };
    loop.onClick = [this] { core.setBool ("loop", ! core.snapshot().b[B_loop]); };
    split.onClick = [this] { core.splitAtPlayhead(); };
    dup.onClick = [this] { core.duplicateSelection(); };
    del.onClick = [this] { core.deleteSelection(); };

    time.setFont (monoFont (17.0f));
    time.setJustificationType (Justification::centredLeft);
    addAndMakeVisible (time);
    info.setFont (uiFont (12.5f));
    info.setColour (Label::textColourId, col::text300);
    info.setJustificationType (Justification::centredLeft);
    addAndMakeVisible (info);
    volLabel.setText ("VOL", dontSendNotification);
    volLabel.setFont (uiFont (11.0f, true));
    volLabel.setColour (Label::textColourId, col::text300);
    addAndMakeVisible (volLabel);
    vol.setSliderStyle (Slider::LinearHorizontal);
    vol.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    vol.setWantsKeyboardFocus (false);
    addAndMakeVisible (vol);
    volAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (core.apvts, "vol", vol);
    for (auto* b : { &fitBtn, &clearRegion }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    fitBtn.onClick = [this] { if (onFit) onFit(); };
    clearRegion.onClick = [this] { core.clearRegion(); };
}

void TransportBar::update (const String& timeText, const String& infoText, bool playing)
{
    play.setIcon (playing ? IconButton::Pause : IconButton::Play);
    loop.setActive (core.snapshot().b[B_loop]);
    time.setText (timeText, dontSendNotification);
    info.setText (infoText, dontSendNotification);
    clearRegion.setVisible (core.hasRegion());
}

void TransportBar::paint (Graphics& g) { drawPanel (g, getLocalBounds().toFloat().reduced (1.0f), 20.0f); }

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    toStart.setBounds (r.removeFromLeft (38).withSizeKeepingCentre (38, 38)); r.removeFromLeft (4);
    play.setBounds (r.removeFromLeft (52).withSizeKeepingCentre (52, 52)); r.removeFromLeft (4);
    stop.setBounds (r.removeFromLeft (38).withSizeKeepingCentre (38, 38)); r.removeFromLeft (4);
    loop.setBounds (r.removeFromLeft (38).withSizeKeepingCentre (38, 38)); r.removeFromLeft (18);
    auto left = r.removeFromLeft (250);
    time.setBounds (left.removeFromTop (28));
    info.setBounds (left);
    r.removeFromLeft (10);
    split.setBounds (r.removeFromLeft (38).withSizeKeepingCentre (38, 38)); r.removeFromLeft (4);
    dup.setBounds (r.removeFromLeft (38).withSizeKeepingCentre (38, 38)); r.removeFromLeft (4);
    del.setBounds (r.removeFromLeft (38).withSizeKeepingCentre (38, 38));
    auto right = r.removeFromRight (190);
    volLabel.setBounds (right.removeFromLeft (30));
    vol.setBounds (right.withSizeKeepingCentre (right.getWidth(), 26));
    r.removeFromRight (12);
    clearRegion.setBounds (r.removeFromRight (112).withSizeKeepingCentre (112, 28)); r.removeFromRight (8);
    fitBtn.setBounds (r.removeFromRight (56).withSizeKeepingCentre (56, 28));
}

//==============================================================================
// Forge
ForgeOrb::ForgeOrb (Core& c) : core (c) { startTimerHz (30); setWantsKeyboardFocus (false); }

void ForgeOrb::timerCallback()
{
    const int st = core.forgeState();
    const bool busy = st >= 1 && st <= 4;
    t += 0.033f * (busy ? 2.4f : 0.6f);
    merge += ((busy ? 1.0f : 0.0f) - merge) * 0.12f;
    hoverAmt += ((hover && ! busy ? 1.0f : 0.0f) - hoverAmt) * 0.2f;
    if (st == 5 && lastState != 5) flash = 1.0f;
    lastState = st;
    flash *= 0.93f;
    if (isShowing()) repaint();
}

void ForgeOrb::paint (Graphics& g)
{
    const int st = core.forgeState();
    const bool busy = st >= 1 && st <= 4;
    auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float R = std::min (b.getWidth(), b.getHeight()) * 0.5f;

    LogoState ls;
    ls.spread = busy ? 1.0f + 0.55f * (0.5f + 0.5f * std::sin (t * 2.0f)) * (1.0f - core.forgeProgress() * 0.7f) : 1.0f;
    ls.apex = busy ? 0.4f + 0.6f * merge : 0.85f + 0.15f * hoverAmt;
    ls.bars = busy ? 1.0f : 0.9f + 0.1f * hoverAmt;
    ls.wave = busy ? t : 0.0f;
    ls.glow = 0.4f + 0.5f * hoverAmt + (busy ? 0.4f : 0.0f) + flash;
    drawLogoMark (g, Rectangle<float> (c.x - R * 0.82f, c.y - R * 0.82f, R * 1.64f, R * 1.64f), ls);

    // progress ring
    if (busy)
    {
        Path track; track.addCentredArc (c.x, c.y, R - 4.0f, R - 4.0f, 0.0f, 0.0f, MathConstants<float>::twoPi, true);
        g.setColour (Colours::white.withAlpha (0.10f));
        g.strokePath (track, PathStrokeType (3.0f));
        Path p; p.addCentredArc (c.x, c.y, R - 4.0f, R - 4.0f, 0.0f, 0.0f, MathConstants<float>::twoPi * std::max (0.02f, core.forgeProgress()), true);
        g.setGradientFill (alloyGradient (b.getX(), b.getY(), b.getRight(), b.getBottom()));
        g.strokePath (p, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    else
    {
        g.setColour (Colours::white.withAlpha (0.10f + 0.25f * hoverAmt));
        g.drawEllipse (c.x - R + 4.0f, c.y - R + 4.0f, (R - 4.0f) * 2.0f, (R - 4.0f) * 2.0f, 1.5f);
    }
}

ForgeView::ForgeView (Core& c)
    : core (c), orb (c), mode ({ "Sequential mix", "Layered" }, col::violet), shape ({ "Rising", "Wave", "Flat" }, col::mint), segLen ({ "Short", "Medium", "Long" }, col::mint),
      tempoTol ({ "8%", "12%", "15%", "20%" }, col::amber), keyTol ({ "2 st", "3 st", "4 st" }, col::amber), trans ({ "2 bars", "4 bars", "8 bars" }, col::rose)
{
    tempoTol.setSelected (2); keyTol.setSelected (1); trans.setSelected (1); segLen.setSelected (1);
    for (auto* s : { &mode, &shape, &segLen, &tempoTol, &keyTol, &trans }) { addAndMakeVisible (*s); s->onChange = [this] (int) { refresh(); }; }
    addAndMakeVisible (orb);
    orb.onClick = [this] { startForge(); };
    lengthSlider.setSliderStyle (Slider::LinearHorizontal);
    lengthSlider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    lengthSlider.setRange (60.0, 360.0, 5.0);
    lengthSlider.setValue (180.0, dontSendNotification);
    lengthSlider.setWantsKeyboardFocus (false);
    lengthSlider.onValueChange = [this] { refresh(); repaint(); };
    addAndMakeVisible (lengthSlider);
    stemsBtn.setToggleState (true, dontSendNotification);
    stemsBtn.setColour (ToggleButton::tickColourId, col::mint);
    stemsBtn.setWantsKeyboardFocus (false);
    addAndMakeVisible (stemsBtn);
    finish.addItem ("Leave the sound alone", 1);
    const auto& fp = factoryPresets();
    for (int i = 1; i < (int) fp.size(); ++i) finish.addItem (fp[(size_t) i].name, i + 1);
    finish.setSelectedId (1, dontSendNotification);
    addAndMakeVisible (finish);
    log.setMultiLine (true, true);
    log.setReadOnly (true);
    log.setCaretVisible (false);
    log.setScrollbarsShown (true);
    log.setColour (TextEditor::backgroundColourId, Colours::transparentBlack);
    log.setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    log.setColour (TextEditor::focusedOutlineColourId, Colours::transparentBlack);
    log.setColour (TextEditor::textColourId, col::text300);
    log.setWantsKeyboardFocus (false);
    addAndMakeVisible (log);
    status.setFont (uiFont (13.5f));
    status.setColour (Label::textColourId, col::text300);
    addAndMakeVisible (status);
    core.addChangeListener (this);
    startTimerHz (6);
    refresh();
}
ForgeView::~ForgeView() { core.removeChangeListener (this); }

ForgeOptions ForgeView::options() const
{
    ForgeOptions fo;
    fo.mo.mode = mode.selected() == 1 ? MashupOptions::VocalOverBeat : MashupOptions::Medley;     // Medley = songs play one after another
    static const int mn[3] = { 8, 16, 24 }, mx[3] = { 16, 32, 48 };
    fo.mo.minBars = mn[segLen.selected()]; fo.mo.maxBars = mx[segLen.selected()];
    fo.mo.shape = shape.selected() == 1 ? MashupOptions::Wave : shape.selected() == 2 ? MashupOptions::Flat : MashupOptions::Rising;
    static const double tt[4] = { 0.08, 0.12, 0.15, 0.20 };
    fo.mo.maxStretch = tt[tempoTol.selected()];
    fo.mo.maxShift = 2 + keyTol.selected();
    fo.mo.transitionBars = 2 << trans.selected();
    fo.mo.targetLengthSec = lengthSlider.getValue();
    fo.mo.useStems = stemsBtn.getToggleState();
    fo.finishPreset = finish.getSelectedId() <= 1 ? -1 : finish.getSelectedId() - 1;
    return fo;
}

void ForgeView::refresh()
{
    deckSongs.clear();
    for (const auto& s : core.library) if (s->inForge) deckSongs.push_back (s);
    bool ready = deckSongs.size() >= 2;
    for (auto& s : deckSongs) if (s->state.load() < 2) ready = false;
    preview = MashupPlan();
    if (ready)
    {
        std::vector<SongInfo> infos;
        for (auto& s : deckSongs) { SongInfo si; si.id = s->id; si.name = s->name.toStdString(); si.an = s->an; si.mix = s->mix; infos.push_back (si); }
        auto fo = options();
        fo.mo.useStems = false;
        preview = planMashup (infos, fo.mo);
    }

    const int st = core.forgeState();
    std::vector<std::string> lines = (st == 0 || st == 6) && ! (st == 6 && core.forgeLog().empty()) && core.forgeLog().empty() ? preview.log : core.forgeLog();
    if (lines.empty()) lines = preview.log;
    std::string text; for (auto& l : lines) text += l + "\n";
    if (text != lastLogText)
    {
        lastLogText = text;
        log.clear();
        for (auto& l : lines)
        {
            const String s (l);
            const bool rule = s.startsWith ("Rule") || s.startsWith ("Movement");
            log.setColour (TextEditor::textColourId, s.startsWith ("Left out") || s.startsWith ("Skipped") ? col::amber : (rule ? col::text100 : col::text300));
            log.setFont (uiFont (12.5f, rule && ! s.startsWith ("Movement")));
            log.moveCaretToEnd();
            log.insertTextAtCaret (s + "\n");
        }
        log.moveCaretToTop (false);
    }

    String sm;
    if (st >= 1 && st <= 4) sm = core.forgeMessage();
    else if (st == 6) sm = core.forgeMessage();
    else if (deckSongs.size() < 2) sm = "Switch on at least two songs in the library.";
    else if (! ready) sm = "Analysing your songs...";
    else if (preview.ok) sm = String (preview.bpm, 1) + " BPM   |   " + String (preview.keyName) + " (" + String (preview.camelot) + ")   |   " + String ((int) preview.usedSongs.size()) + " of " + String ((int) deckSongs.size()) + " songs fit";
    else sm = String (preview.error);
    status.setText (sm, dontSendNotification);
    status.setColour (Label::textColourId, st == 6 || (! preview.ok && ready && st == 0) ? col::amber : col::text300);
    repaint();
}

void ForgeView::timerCallback()
{
    const int st = core.forgeState();
    if (st != lastState)
    {
        lastState = st;
        refresh();
        if (st == 5 && onDone) onDone();
    }
    else if (st >= 1 && st <= 4) { status.setText (core.forgeMessage(), dontSendNotification); repaint(deckArea); }
}

void ForgeView::startForge()
{
    if (deckSongs.size() < 2) return;
    core.startForge (options());
    refresh();
}

void ForgeView::resized()
{
    auto r = getLocalBounds().reduced (4, 0);
    auto head = r.removeFromTop (124);
    orb.setBounds (head.removeFromRight (150).withSizeKeepingCentre (120, 120));
    status.setBounds (head.removeFromBottom (30).withTrimmedLeft (8));
    r.removeFromTop (6);
    deckArea = r.removeFromTop (204);
    r.removeFromTop (12);

    optionsArea = r.removeFromLeft (448);
    r.removeFromLeft (12);
    matrixArea = r.removeFromLeft (300);
    r.removeFromLeft (12);
    logArea = r;

    auto o = optionsArea.reduced (18, 14).withTrimmedTop (4);
    auto row = [&] (Component& c, int h = 30) { o.removeFromTop (16); c.setBounds (o.removeFromTop (h)); o.removeFromTop (4); };
    row (mode); row (shape); row (segLen); row (tempoTol); row (keyTol); row (trans);
    o.removeFromTop (18); lengthSlider.setBounds (o.removeFromTop (28)); o.removeFromTop (4);
    stemsBtn.setBounds (o.removeFromTop (26)); o.removeFromTop (4);
    o.removeFromTop (16); finish.setBounds (o.removeFromTop (30));
    log.setBounds (logArea.reduced (14, 12).withTrimmedTop (28));
}

void ForgeView::mouseDown (const MouseEvent& e)
{
    if (! deckArea.contains (e.getPosition())) return;
    const int n = (int) deckSongs.size();
    if (n == 0) return;
    const int cw = std::min (260, (deckArea.getWidth() - (n - 1) * 12) / n);
    const int i = (e.x - deckArea.getX()) / (cw + 12);
    if (i >= 0 && i < n) { deckSongs[(size_t) i]->inForge = false; core.sendChangeMessage(); }
}

void ForgeView::paint (Graphics& g)
{
    // heading
    g.setColour (col::text100);
    g.setFont (uiFont (30.0f, true));
    g.drawText ("Forge", Rectangle<int> (8, 14, 400, 40), Justification::centredLeft);
    g.setColour (col::text300);
    g.setFont (uiFont (14.0f));
    g.drawFittedText ("Pick two or more songs. Auralloy matches their tempo and key, then plays them one after another - beat-matched, with a smooth blend at every switch. "
                      "Choose Layered to put one song's vocals over another's beat instead. Every clip stays editable in the Studio.", Rectangle<int> (8, 56, getWidth() - 210, 60), Justification::topLeft, 3);

    // deck cards
    const int n = (int) deckSongs.size();
    if (n == 0)
    {
        drawPanel (g, deckArea.toFloat(), 20.0f);
        g.setColour (col::text500); g.setFont (uiFont (14.0f));
        g.drawFittedText ("No songs selected. Add songs to the library and switch them on.", deckArea.reduced (30), Justification::centred, 2);
    }
    else
    {
        const int cw = std::min (260, (deckArea.getWidth() - (n - 1) * 12) / n);
        for (int i = 0; i < n; ++i)
        {
            const auto& s = *deckSongs[(size_t) i];
            Rectangle<float> r ((float) (deckArea.getX() + i * (cw + 12)), (float) deckArea.getY(), (float) cw, (float) deckArea.getHeight());
            const Colour sc = sourceColour (s.colour);
            const bool used = preview.ok && std::find (preview.usedSongs.begin(), preview.usedSongs.end(), s.id) != preview.usedSongs.end();
            drawPanel (g, r, 20.0f, sc.interpolatedWith (Colours::white, 0.6f), used || ! preview.ok ? 1.0f : 0.6f);
            g.setColour (sc); g.fillRoundedRectangle (r.getX() + 16.0f, r.getY() + 18.0f, 4.0f, 22.0f, 2.0f);
            g.setColour (col::text100); g.setFont (uiFont (15.5f, true));
            g.drawText (s.name, Rectangle<float> (r.getX() + 28.0f, r.getY() + 14.0f, r.getWidth() - 44.0f, 28.0f), Justification::centredLeft, true);
            g.setColour (col::text500); g.setFont (uiFont (10.5f, true));
            g.drawText ("click to remove", Rectangle<float> (r.getX() + 28.0f, r.getBottom() - 26.0f, 120.0f, 14.0f), Justification::centredLeft);
            if (s.state.load() < 2) { g.setColour (col::text300); g.setFont (uiFont (13.0f)); g.drawText ("Analysing...", Rectangle<float> (r.getX() + 20.0f, r.getY() + 56.0f, r.getWidth() - 40.0f, 20.0f), Justification::centredLeft); continue; }
            if (! s.an.ok) { g.setColour (col::amber); g.setFont (uiFont (13.0f)); g.drawText ("No steady beat found - can't be used", Rectangle<float> (r.getX() + 20.0f, r.getY() + 56.0f, r.getWidth() - 30.0f, 20.0f), Justification::centredLeft); continue; }
            // big numbers
            g.setColour (col::text100); g.setFont (monoFont (24.0f));
            g.drawText (String (s.an.bpm, 1), Rectangle<float> (r.getX() + 20.0f, r.getY() + 50.0f, 100.0f, 30.0f), Justification::centredLeft);
            g.setColour (col::text300); g.setFont (uiFont (11.0f, true)); g.drawText ("BPM", Rectangle<float> (r.getX() + 20.0f, r.getY() + 80.0f, 60.0f, 14.0f), Justification::centredLeft);
            const int num = String (s.an.camelot).getIntValue();
            const Colour kc = Colour::fromHSV (((float) ((num + 5) % 12)) / 12.0f, 0.55f, 0.98f, 1.0f);
            g.setColour (kc.withAlpha (0.22f)); g.fillRoundedRectangle (r.getX() + 120.0f, r.getY() + 52.0f, 52.0f, 26.0f, 13.0f);
            g.setColour (kc); g.drawRoundedRectangle (r.getX() + 120.0f, r.getY() + 52.0f, 52.0f, 26.0f, 13.0f, 1.2f);
            g.setColour (col::text100); g.setFont (monoFont (14.0f)); g.drawText (String (s.an.camelot), Rectangle<float> (r.getX() + 120.0f, r.getY() + 52.0f, 52.0f, 26.0f), Justification::centred);
            g.setColour (col::text300); g.setFont (uiFont (11.5f)); g.drawText (String (s.an.keyName), Rectangle<float> (r.getX() + 120.0f, r.getY() + 80.0f, 120.0f, 14.0f), Justification::centredLeft);
            // plan adjustments
            if (used)
            {
                const auto it = preview.perSong.find (s.id);
                if (it != preview.perSong.end())
                {
                    const double tp = (it->second.first - 1.0) * 100.0, sh = it->second.second;
                    g.setColour (col::mint.withAlpha (0.18f)); g.fillRoundedRectangle (r.getX() + 16.0f, r.getY() + 108.0f, r.getWidth() - 32.0f, 28.0f, 14.0f);
                    g.setColour (col::mint); g.setFont (monoFont (12.5f));
                    g.drawText ("tempo " + String (tp, 1) + "%   pitch " + (sh == 0 ? String ("0") : String (sh, 0) + " st"), Rectangle<float> (r.getX() + 16.0f, r.getY() + 108.0f, r.getWidth() - 32.0f, 28.0f), Justification::centred);
                }
            }
            else if (preview.ok) { g.setColour (col::amber); g.setFont (uiFont (12.5f)); g.drawFittedText ("Too far from the others in tempo or key", Rectangle<int> ((int) r.getX() + 16, (int) r.getY() + 106, (int) r.getWidth() - 32, 34), Justification::centred, 2); }
            // mini section bar
            const Rectangle<float> bar (r.getX() + 16.0f, r.getY() + 146.0f, r.getWidth() - 32.0f, 10.0f);
            g.setColour (Colours::white.withAlpha (0.08f)); g.fillRoundedRectangle (bar, 5.0f);
            if (! s.an.sections.empty())
                for (const auto& sec : s.an.sections)
                {
                    const float x0 = bar.getX() + bar.getWidth() * (float) (sec.start / s.an.duration), x1 = bar.getX() + bar.getWidth() * (float) (sec.end / s.an.duration);
                    g.setColour (sc.withAlpha (0.25f + 0.7f * sec.energy));
                    g.fillRect (x0 + 0.5f, bar.getY(), std::max (1.0f, x1 - x0 - 1.0f), bar.getHeight());
                }
            g.setColour (col::text300); g.setFont (uiFont (11.0f));
            g.drawText (String ((int) s.an.sections.size()) + " sections  |  " + mmss (s.seconds()), Rectangle<float> (r.getX() + 16.0f, r.getY() + 162.0f, r.getWidth() - 32.0f, 14.0f), Justification::centredLeft);
        }
    }

    // options panel
    drawPanel (g, optionsArea.toFloat(), 20.0f);
    auto o = optionsArea.reduced (18, 14).withTrimmedTop (4);
    auto label = [&] (const String& t, int h = 30) { g.setColour (col::text300); g.setFont (uiFont (11.0f, true)); g.drawText (t.toUpperCase(), o.removeFromTop (16), Justification::centredLeft); o.removeFromTop (h + 4); };
    label ("Mode"); label ("Energy shape"); label ("Segment length"); label ("Tempo tolerance"); label ("Key shift allowed"); label ("Transition length");
    g.setColour (col::text300); g.setFont (uiFont (11.0f, true));
    g.drawText ("LENGTH   " + mmss (lengthSlider.getValue()), o.removeFromTop (18), Justification::centredLeft);
    o.removeFromTop (28 + 4 + 26 + 4);
    g.drawText ("AFTER FORGING, APPLY THE SOUND", o.removeFromTop (16), Justification::centredLeft);

    // compatibility matrix
    drawPanel (g, matrixArea.toFloat(), 20.0f);
    g.setColour (col::text300); g.setFont (uiFont (11.0f, true));
    g.drawText ("COMPATIBILITY", Rectangle<int> (matrixArea.getX() + 18, matrixArea.getY() + 14, 200, 18), Justification::centredLeft);
    if (n >= 2)
    {
        const int cells = std::min (n, 8);
        const float cs = std::min (46.0f, (float) (matrixArea.getWidth() - 60) / (float) cells);
        const float ox = (float) matrixArea.getX() + 34.0f, oy = (float) matrixArea.getY() + 64.0f;
        std::vector<SongInfo> infos;
        for (int i = 0; i < cells; ++i) { SongInfo si; si.an = deckSongs[(size_t) i]->an; infos.push_back (si); }
        for (int i = 0; i < cells; ++i)
        {
            g.setColour (sourceColour (deckSongs[(size_t) i]->colour));
            g.fillEllipse (ox + (float) i * cs + cs * 0.5f - 4.0f, oy - 16.0f, 8.0f, 8.0f);
            g.fillEllipse (ox - 16.0f, oy + (float) i * cs + cs * 0.5f - 4.0f, 8.0f, 8.0f);
            for (int j = 0; j < cells; ++j)
            {
                Rectangle<float> cr (ox + (float) j * cs + 2.0f, oy + (float) i * cs + 2.0f, cs - 4.0f, cs - 4.0f);
                if (i == j) { g.setColour (Colours::white.withAlpha (0.05f)); g.fillRoundedRectangle (cr, 8.0f); continue; }
                const double sc = pairScore (infos[(size_t) i], infos[(size_t) j], options().mo.maxStretch);
                g.setColour (col::graphite700.interpolatedWith (col::mint, (float) sc).withAlpha (0.35f + 0.6f * (float) sc));
                g.fillRoundedRectangle (cr, 8.0f);
                g.setColour (Colours::white.withAlpha (0.9f)); g.setFont (monoFont (11.5f));
                g.drawText (String (roundToInt (sc * 100)), cr, Justification::centred);
            }
        }
        g.setColour (col::text500); g.setFont (uiFont (11.5f));
        g.drawFittedText ("How well each pair fits by tempo, key and how complementary their vocals are (0-100).", Rectangle<int> (matrixArea.getX() + 18, matrixArea.getBottom() - 54, matrixArea.getWidth() - 36, 44), Justification::topLeft, 3);
    }

    // log panel
    drawPanel (g, logArea.toFloat(), 20.0f);
    g.setColour (col::text300); g.setFont (uiFont (11.0f, true));
    g.drawText (core.forgeState() == 5 ? "HOW IT WAS BUILT" : "THE PLAN", Rectangle<int> (logArea.getX() + 18, logArea.getY() + 14, 220, 18), Justification::centredLeft);
}

//==============================================================================
void Overlay::paint (Graphics& g)
{
    g.fillAll (Colour (0xd904050a));
    g.setColour (Colour (0xff12131f));
    g.fillRoundedRectangle (card.toFloat(), 26.0f);
    drawPanel (g, card.toFloat(), 26.0f, Colours::white, 1.2f);
    g.setColour (col::text100);
    g.setFont (uiFont (21.0f, true));
    g.drawText (heading, card.getX() + 28, card.getY() + 20, card.getWidth() - 56, 30, Justification::centredLeft);
}
bool Overlay::keyPressed (const KeyPress& k) { if (k == KeyPress::escapeKey) { close(); return true; } return true; }

ExportOverlay::ExportOverlay (Core& c) : core (c), bar (progress)
{
    heading = "Export";
    for (auto* l : { &fl, &sl }) { l->setFont (uiFont (11.5f, true)); l->setColour (Label::textColourId, col::text300); addAndMakeVisible (*l); }
    fl.setText ("FORMAT", dontSendNotification); sl.setText ("SAMPLE RATE", dontSendNotification);
    const bool mp3 = Core::findFfmpeg().existsAsFile();
    format.addItem ("WAV  16-bit", 1); format.addItem ("WAV  24-bit  (best quality)", 2); format.addItem ("WAV  32-bit float", 3);
    format.addItem ("FLAC  16-bit", 4); format.addItem ("FLAC  24-bit", 5);
    format.addItem (mp3 ? "MP3  320 kbps" : "MP3  320 kbps  (needs ffmpeg)", 6); format.addItem (mp3 ? "MP3  192 kbps" : "MP3  192 kbps  (needs ffmpeg)", 7);
    format.setItemEnabled (6, mp3); format.setItemEnabled (7, mp3);
    format.setSelectedId (2, dontSendNotification);
    rate.addItem ("44.1 kHz", 1); rate.addItem ("48 kHz", 2); rate.addItem ("96 kHz", 3);
    rate.setSelectedId (1, dontSendNotification);
    addAndMakeVisible (format); addAndMakeVisible (rate);
    normalize.setButtonText ("Normalize loudness (peak -1 dBFS)"); normalize.setToggleState (true, dontSendNotification);
    dither.setButtonText ("Dither (for 16-bit)"); dither.setToggleState (true, dontSendNotification);
    region.setButtonText ("Export only the selected region");
    region.setToggleState (core.hasRegion(), dontSendNotification); region.setEnabled (core.hasRegion());
    for (auto* t : { &normalize, &dither, &region }) { t->setColour (ToggleButton::tickColourId, col::mint); t->setWantsKeyboardFocus (false); addAndMakeVisible (*t); }
    status.setFont (uiFont (13.0f)); status.setColour (Label::textColourId, col::text300);
    status.setJustificationType (Justification::centredLeft);
    addAndMakeVisible (status); addAndMakeVisible (bar);
    go.setButtonText ("Export...");
    go.getProperties().set ("primary", true);
    go.onClick = [this] { if (core.exportState() == 1) core.cancelExport(); else choose(); };
    closeBtn.setButtonText ("Close"); closeBtn.onClick = [this] { close(); };
    reveal.setButtonText ("Show in folder"); reveal.onClick = [this] { File (core.exportMessage()).revealToUser(); };
    for (auto* b : { &go, &closeBtn, &reveal }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
    reveal.setVisible (false);
    startTimerHz (10);
}

void ExportOverlay::resized()
{
    card = Rectangle<int> (0, 0, 540, 470).withCentre (getLocalBounds().getCentre());
    auto r = card.reduced (28, 0).withTrimmedTop (66);
    r.removeFromBottom (24);
    fl.setBounds (r.removeFromTop (18)); format.setBounds (r.removeFromTop (34)); r.removeFromTop (12);
    sl.setBounds (r.removeFromTop (18)); rate.setBounds (r.removeFromTop (34)); r.removeFromTop (16);
    normalize.setBounds (r.removeFromTop (28)); dither.setBounds (r.removeFromTop (28)); region.setBounds (r.removeFromTop (28));
    r.removeFromTop (12);
    status.setBounds (r.removeFromTop (22));
    bar.setBounds (r.removeFromTop (14)); r.removeFromTop (12);
    auto row = r.removeFromBottom (42);
    closeBtn.setBounds (row.removeFromLeft (100));
    reveal.setBounds (row.removeFromLeft (150).withTrimmedLeft (8));
    go.setBounds (row.removeFromRight (160));
}

void ExportOverlay::choose()
{
    ExportSettings es;
    es.format = format.getSelectedId() - 1;
    const int r = rate.getSelectedId();
    es.sampleRate = r == 2 ? 48000 : (r == 3 ? 96000 : 0);
    es.normalize = normalize.getToggleState();
    es.dither = dither.getToggleState();
    es.useRegion = region.getToggleState();
    const String ext = es.format >= MP3_320 ? "*.mp3" : (es.format >= FLAC16 ? "*.flac" : "*.wav");
    chooser = std::make_unique<FileChooser> ("Export", File::getSpecialLocation (File::userMusicDirectory).getChildFile (core.suggestedExportName (es)), ext);
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                          [this, es, safe = SafePointer<Component> (this)] (const FileChooser& fc)
                          {
                              if (safe == nullptr) return;
                              const auto f = fc.getResult();
                              if (f != File()) { started = true; core.startExport (es, f); }
                          });
}

void ExportOverlay::timerCallback()
{
    const int st = core.exportState();
    progress = (double) core.exportProgress();
    go.setButtonText (st == 1 ? "Cancel" : "Export...");
    reveal.setVisible (started && st == 2);
    bar.setVisible (st == 1);
    if (! started && st != 1) { status.setText (core.timeline()->empty() ? "Nothing to export yet - add songs or forge a mashup" : "Exports everything you hear, at full quality", dontSendNotification); return; }
    if (st == 1)      status.setText ("Rendering...  " + String (roundToInt (progress * 100.0)) + "%", dontSendNotification);
    else if (st == 2) status.setText ("Saved: " + core.exportMessage(), dontSendNotification);
    else if (st == 3) status.setText ("Failed: " + core.exportMessage(), dontSendNotification);
    else if (st == 4) status.setText ("Cancelled", dontSendNotification);
}

SettingsOverlay::SettingsOverlay (Core& c) : core (c), selector (c.deviceManager, 0, 0, 2, 2, false, false, true, false)
{
    heading = "Audio & display";
    addAndMakeVisible (selector);
    motion.setToggleState (core.reduceMotion, dontSendNotification);
    motion.setColour (ToggleButton::tickColourId, col::mint);
    motion.setWantsKeyboardFocus (false);
    motion.onClick = [this] { core.setReduceMotion (motion.getToggleState()); };
    addAndMakeVisible (motion);
    closeBtn.setButtonText ("Done");
    closeBtn.getProperties().set ("primary", true);
    closeBtn.onClick = [this] { close(); };
    addAndMakeVisible (closeBtn);
    info.setFont (uiFont (12.0f));
    info.setColour (Label::textColourId, col::text300);
    info.setText (String ("MP3 export: ") + (Core::findFfmpeg().existsAsFile() ? "ready (ffmpeg found)" : "needs ffmpeg.exe on this PC") + "     |     Auralloy 0.0.1     |     32-bit float processing", dontSendNotification);
    addAndMakeVisible (info);
}

void SettingsOverlay::resized()
{
    card = Rectangle<int> (0, 0, 660, 560).withCentre (getLocalBounds().getCentre());
    auto r = card.reduced (28, 0).withTrimmedTop (64);
    info.setBounds (r.removeFromBottom (24));
    auto row = r.removeFromBottom (46);
    closeBtn.setBounds (row.removeFromRight (130).withTrimmedTop (4));
    motion.setBounds (row.removeFromLeft (420));
    selector.setBounds (r);
}

//==============================================================================
SplashOverlay::SplashOverlay() { startTimerHz (60); setInterceptsMouseClicks (true, false); }

void SplashOverlay::setTime (float t)
{
    stopTimer();
    elapsed = t;
    if (elapsed > 2.5f) { if (onFinished) onFinished(); return; }
    repaint();
}

void SplashOverlay::timerCallback()
{
    elapsed += 1.0f / 60.0f;
    if (elapsed > 2.5f) { stopTimer(); if (onFinished) onFinished(); return; }
    repaint();
}

void SplashOverlay::paint (Graphics& g)
{
    const float T = elapsed;
    const float fade = 1.0f - clamp01 ((T - 2.05f) / 0.45f);
    const auto b = getLocalBounds().toFloat();
    g.saveState();
    g.beginTransparencyLayer (fade);
    drawAurora (g, b, 14.0 + (double) T * 4.0, 0.0f);
    g.setColour (Colour (0x66000000));
    g.fillRect (b);

    const auto c = b.getCentre();
    LogoState ls;
    ls.legs = easeOutExpo (T / 0.85f);
    ls.spread = 1.0f + 2.4f * (1.0f - easeOutExpo ((T - 0.2f) / 0.85f));
    ls.apex = easeOutExpo ((T - 0.95f) / 0.4f);
    ls.bars = easeOutExpo ((T - 1.05f) / 0.5f);
    ls.glow = 0.30f + (T > 0.95f ? 1.4f * std::exp (-(T - 0.95f) * 4.0f) : 0.0f);
    ls.alpha = clamp01 (T / 0.2f);
    drawLogoMark (g, Rectangle<float> (c.x - 130.0f, c.y - 210.0f, 260.0f, 260.0f), ls);

    const float wh = 40.0f, ww = wordmarkWidth (wh);
    drawWordmark (g, Rectangle<float> (c.x - ww * 0.5f, c.y + 62.0f, ww, wh), (T - 1.1f) / 0.9f, T > 1.7f ? (T - 1.7f) / 0.6f * 1.2f - 0.1f : -1.0f);
    const float ks = easeOutExpo ((T - 1.55f) / 0.6f);
    g.setColour (col::text300.withAlpha (ks));
    g.setFont (uiFont (13.5f, true));
    g.drawText ("TWO SONGS IN   -   ONE ALLOY OUT", Rectangle<float> (c.x - 300.0f, c.y + 128.0f, 600.0f, 22.0f), Justification::centred);
    g.endTransparencyLayer();
    g.restoreState();
}
} // namespace al
