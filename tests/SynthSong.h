// Synthetic songs with known tempo / key / structure for testing the analysis and mashup engines
#pragma once
#include "../src/dsp/Dsp.h"
#include <cmath>
#include <vector>

namespace testsong
{
struct Spec
{
    double bpm = 100;
    int root = 9;            // key root pitch class (9 = A)
    bool minor = true;
    double lead = 0.37;      // silence before the first beat
    int introBars = 8, verseBars = 16, chorusBars = 16, outroBars = 8;
    double sr = 44100.0;
    bool vocal = false;      // add a "vocal" (a centred, vibrato sine melody) in verse/chorus
    unsigned seed = 1;
};

inline double midiHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

inline al::Source make (const Spec& sp)
{
    al::Source s; s.sampleRate = sp.sr;
    const double beat = 60.0 / sp.bpm, bar = 4 * beat;
    const int totalBars = sp.introBars + sp.verseBars + sp.chorusBars + sp.outroBars;
    const size_t n = (size_t) ((sp.lead + totalBars * bar + 1.0) * sp.sr);
    s.l.assign (n, 0.0f); s.r.assign (n, 0.0f);
    al::Rng rng; rng.s = 12345u + sp.seed * 7919u;
    const double TWO_PI = 2 * 3.14159265358979;

    // chord progression as semitone offsets from the key root (minor: i - iv - V - i, major: I - IV - V - I)
    const int degMinor[4][3] = { { 0, 3, 7 }, { 5, 8, 12 }, { 7, 11, 14 }, { 0, 3, 7 } };
    const int degMajor[4][3] = { { 0, 4, 7 }, { 5, 9, 12 }, { 7, 11, 14 }, { 0, 4, 7 } };
    const int bassRoot[4] = { 0, 5, 7, 0 };

    auto add = [&] (size_t i, float vl, float vr) { if (i < n) { s.l[i] += vl; s.r[i] += vr; } };
    for (int b = 0; b < totalBars; ++b)
    {
        const bool intro = b < sp.introBars;
        const bool outro = b >= totalBars - sp.outroBars;
        const bool chorus = ! intro && ! outro && b >= sp.introBars + sp.verseBars;
        const bool verse = ! intro && ! outro && ! chorus;
        const double t0 = sp.lead + b * bar;
        const int chord = (b % 4);
        // pads
        if (verse || chorus)
            for (int k = 0; k < 3; ++k)
            {
                const double f = midiHz (48 + sp.root + (sp.minor ? degMinor : degMajor)[chord][k]);
                const size_t i0 = (size_t) (t0 * sp.sr), len = (size_t) (bar * sp.sr);
                for (size_t i = 0; i < len; ++i)
                {
                    const double t = (double) i / sp.sr;
                    const double env = std::min (1.0, t * 20.0) * std::min (1.0, (bar - t) * 20.0);
                    double v = 0;
                    for (int h = 1; h <= 4; ++h) v += std::sin (TWO_PI * f * h * t) / (h * h);
                    const float a = (float) (v * env * (chorus ? 0.09 : 0.06));
                    const float pan = k == 0 ? 0.7f : (k == 1 ? 1.0f : 0.5f);
                    add (i0 + i, a * pan, a * (1.7f - pan));
                }
            }
        for (int q = 0; q < 4; ++q)
        {
            const double tb = t0 + q * beat;
            const size_t i0 = (size_t) (tb * sp.sr);
            const float kv = (q == 0 ? 0.85f : 0.6f) * (outro ? 0.6f : 1.0f);
            for (size_t i = 0; i < (size_t) (0.22 * sp.sr); ++i)
            {
                const double t = (double) i / sp.sr;
                const double fr = 48.0 + 110.0 * std::exp (-t * 38.0);
                const float v = (float) (std::sin (TWO_PI * fr * t) * std::exp (-t * 14.0)) * kv;
                add (i0 + i, v, v);
            }
            for (int h = 0; h < 2; ++h)
            {
                const size_t ih = i0 + (size_t) (h * 0.5 * beat * sp.sr);
                for (size_t i = 0; i < (size_t) (0.05 * sp.sr); ++i)
                {
                    const double t = (double) i / sp.sr;
                    const float v = (float) (rng.bip() * std::exp (-t * 90.0)) * (chorus ? 0.16f : (intro ? 0.05f : 0.1f));
                    add (ih + i, v, v * 0.8f);
                }
            }
            if ((q == 1 || q == 3) && (verse || chorus))
                for (size_t i = 0; i < (size_t) (0.16 * sp.sr); ++i)
                {
                    const double t = (double) i / sp.sr;
                    const float v = (float) (rng.bip() * std::exp (-t * 28.0) * 0.35 + std::sin (TWO_PI * 190 * t) * std::exp (-t * 30.0) * 0.2);
                    add (i0 + i, v, v);
                }
            if (verse || chorus)
                for (int h = 0; h < 2; ++h)
                {
                    const double f = midiHz (28 + ((sp.root + bassRoot[chord]) % 12));
                    const size_t ib = i0 + (size_t) (h * 0.5 * beat * sp.sr);
                    const size_t len = (size_t) (0.45 * beat * sp.sr);
                    for (size_t i = 0; i < len; ++i)
                    {
                        const double t = (double) i / sp.sr;
                        const float v = (float) (std::sin (TWO_PI * f * t) * std::min (1.0, t * 80) * std::exp (-t * 2.5)) * (chorus ? 0.34f : 0.26f);
                        add (ib + i, v, v);
                    }
                }
        }
        if (sp.vocal && (verse || chorus))
            for (int q = 0; q < 4; ++q)
            {
                const int tone = (sp.minor ? degMinor : degMajor)[chord][q % 3] + 12;
                const double f = midiHz (60 + sp.root % 12 + tone);
                const size_t i0 = (size_t) ((t0 + q * beat) * sp.sr), len = (size_t) (0.85 * beat * sp.sr);
                for (size_t i = 0; i < len; ++i)
                {
                    const double t = (double) i / sp.sr;
                    const double env = std::min (1.0, t * 30) * std::min (1.0, (0.85 * beat - t) * 30);
                    const double ph = TWO_PI * f * t + 0.5 * std::sin (TWO_PI * 5.5 * t);
                    const double v = std::sin (ph) + 0.5 * std::sin (2 * ph) + 0.3 * std::sin (3 * ph);
                    const float a = (float) (v * env * 0.12);
                    add (i0 + i, a, a);
                }
            }
    }
    s.buildPeaks();
    return s;
}
} // namespace testsong
