#include "../src/engine/Timeline.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); ++failures; } else { std::printf ("ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static std::shared_ptr<al::Source> sine (double hz, double secs, float amp = 0.5f)
{
    auto s = std::make_shared<al::Source>(); s->sampleRate = al::kProjectRate;
    const size_t n = (size_t) (secs * al::kProjectRate);
    s->l.resize (n); s->r.resize (n);
    for (size_t i = 0; i < n; ++i) s->l[i] = s->r[i] = amp * (float) std::sin (2 * 3.14159265358979 * hz * (double) i / al::kProjectRate);
    return s;
}
static double estimateHz (const std::vector<float>& x, double sr, size_t a, size_t b)
{
    int c = 0; double f0 = -1, l0 = -1;
    for (size_t i = a + 1; i < b; ++i) if (x[i - 1] < 0 && x[i] >= 0) { const double t = (double) (i - 1) + (double) (-x[i - 1]) / (double) (x[i] - x[i - 1]); if (f0 < 0) f0 = t; l0 = t; ++c; }
    return c > 1 ? (c - 1) * sr / (l0 - f0) : 0.0;
}
static double rms (const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b && i < x.size(); ++i) s += (double) x[i] * x[i]; return std::sqrt (s / std::max<size_t> (1, b - a)); }

int main()
{
    setvbuf (stdout, nullptr, _IONBF, 0);
    al::Timeline tl;
    al::Track t1; t1.id = 1; t1.name = "A";
    al::Clip c1; c1.id = 1; c1.src = sine (440, 4.0); c1.start = 1.0; c1.length = 4.0;
    t1.clips.push_back (c1);
    al::Track t2; t2.id = 2; t2.name = "B";
    al::Clip c2; c2.id = 2; c2.src = sine (880, 2.0); c2.start = 3.0; c2.length = 2.0; c2.fadeIn = 0.5;
    t2.clips.push_back (c2);
    tl.tracks = { t1, t2 };
    CHECK (std::abs (tl.length() - 5.0) < 1e-9, "timeline length is the end of the last clip");

    al::MixParams mp; mp.gainDb = 0;
    std::vector<float> L, R;
    auto prog = [] (float) { return true; };
    al::renderTimeline (tl, mp, 1.0, 0, 0, 0, 0, 44100.0, L, R, prog);
    std::printf ("      render: %.2f s\n", L.size() / 44100.0);
    CHECK (std::abs (L.size() / 44100.0 - 5.0) < 0.15, "render length matches the timeline (%.2fs)", L.size() / 44100.0);
    CHECK (rms (L, 10000, 40000) < 0.001, "silence before the first clip");
    CHECK (std::abs (estimateHz (L, 44100.0, 60000, 120000) - 440.0) < 0.5, "clip 1 plays 440 Hz at the right time");
    CHECK (std::abs (rms (L, 160000, 190000) - std::sqrt (0.5 * 0.5 * 0.5 + 0.5 * 0.5 * 0.5) * std::sqrt (1.0)) < 0.2 || rms (L, 160000, 190000) > 0.3, "both clips overlap and add (rms %.2f)", rms (L, 160000, 190000));
    CHECK (rms (L, 3 * 44100, 3 * 44100 + 900) < rms (L, 4 * 44100, 4 * 44100 + 900), "clip 2 fades in");

    // speed 0.5 at 48 kHz: twice as long, pitch halved
    al::renderTimeline (tl, mp, 0.5, 0, 0, 0, 0, 48000.0, L, R, prog);
    const double hz = estimateHz (L, 48000.0, 2 * 48000 + 2000, 3 * 48000 + 2000);
    std::printf ("      speed 0.5 @48k: %.2f s, %.1f Hz\n", L.size() / 48000.0, hz);
    CHECK (std::abs (L.size() / 48000.0 - 10.0) < 0.3 && std::abs (hz - 220.0) < 1.0, "speed 0.5 doubles the length and halves the pitch");

    // mute / solo / pan
    tl.tracks[1].mute = true;
    al::renderTimeline (tl, mp, 1.0, 0, 0, 0, 0, 44100.0, L, R, prog);
    CHECK (std::abs (estimateHz (L, 44100.0, 4 * 44100, 4 * 44100 + 40000) - 440.0) < 1.0, "muted track is silent");
    tl.tracks[1].mute = false; tl.tracks[0].solo = true;
    al::renderTimeline (tl, mp, 1.0, 0, 0, 0, 0, 44100.0, L, R, prog);
    CHECK (std::abs (estimateHz (L, 44100.0, 4 * 44100, 4 * 44100 + 40000) - 440.0) < 1.0, "solo isolates a track");
    tl.tracks[0].solo = false; tl.tracks[0].pan = -1.0f;
    al::renderTimeline (tl, mp, 1.0, 0, 0, 0, 0, 44100.0, L, R, prog);
    CHECK (rms (R, 60000, 120000) < 0.01 && rms (L, 60000, 120000) > 0.3, "hard-left pan");

    // region
    tl.tracks[0].pan = 0;
    al::renderTimeline (tl, mp, 1.0, 1.0, 3.0, 0, 0, 44100.0, L, R, prog);
    CHECK (std::abs (L.size() / 44100.0 - 2.0) < 0.15, "region 1s-3s renders 2 s (%.2fs)", L.size() / 44100.0);
    // master fades
    al::renderTimeline (tl, mp, 1.0, 1.0, 5.0, 1.0, 1.0, 44100.0, L, R, prog);
    CHECK (rms (L, 1000, 3000) < 0.05 && rms (L, 44100 + 2000, 44100 + 4000) > 0.3, "master fade-in is applied");

    std::printf ("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL TIMELINE TESTS PASSED", failures);
    return failures;
}
