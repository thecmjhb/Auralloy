// Auralloy - tempo / pitch transforms on whole buffers: windowed-sinc resampling, WSOLA time-stretch,
// pitch shift = stretch + resample, slicing and reversing. Pure C++.
#pragma once
#include "Dsp.h"
#include "Stretch.h"
#include <array>
#include <memory>

namespace al
{
// Reads `in` at `rate` source samples per output sample (rate > 1 = shorter + higher pitch) with a 24-tap windowed sinc.
inline std::shared_ptr<Source> resampleSinc (const Source& in, double rate, double outSampleRate = 0.0)
{
    constexpr int HALF = 12, TAPS = 24;
    auto out = std::make_shared<Source>();
    out->sampleRate = outSampleRate > 0 ? outSampleRate : in.sampleRate;
    const long long n = (long long) in.frames();
    const long long m = (long long) std::floor ((double) n / rate);
    out->l.assign ((size_t) std::max<long long> (0, m), 0.0f);
    out->r.assign (out->l.size(), 0.0f);

    static const std::array<float, 1025> win = [] {
        std::array<float, 1025> w {};
        for (size_t i = 0; i < w.size(); ++i)
        {
            const double t = (double) i / 1024.0;
            w[i] = (float) (0.42 + 0.5 * std::cos (3.14159265358979 * t) + 0.08 * std::cos (2 * 3.14159265358979 * t));
        }
        return w;
    }();

    const double fc = rate <= 1.0 ? 1.0 : 0.97 / rate;
    const double delta = 3.14159265358979 * fc, cD = std::cos (delta), sD = std::sin (delta);
    for (long long i = 0; i < m; ++i)
    {
        const double pos = (double) i * rate;
        const long long i0 = (long long) std::floor (pos);
        const double frac = pos - (double) i0;
        float w[TAPS];
        double th = delta * ((double) (-HALF + 1) - frac);
        double s = std::sin (th), c = std::cos (th), sum = 0.0;
        for (int t = 0; t < TAPS; ++t)
        {
            const double x = (double) (t - HALF + 1) - frac;
            const double sinc = std::fabs (th) < 1e-9 ? 1.0 : s / th;
            const double ax = std::min (0.9999, std::fabs (x) / (double) HALF);
            const double wv = (double) win[(size_t) (ax * 1024.0)] * sinc;
            w[t] = (float) wv; sum += wv;
            const double ns = s * cD + c * sD;
            c = c * cD - s * sD; s = ns;
            th += delta;
        }
        const float nrm = (float) (1.0 / sum);
        float a = 0.0f, b = 0.0f;
        const long long first = i0 - HALF + 1;
        if (first >= 0 && first + TAPS < n)
        {
            const float* pl = &in.l[(size_t) first]; const float* pr = &in.r[(size_t) first];
            for (int t = 0; t < TAPS; ++t) { a += w[t] * pl[t]; b += w[t] * pr[t]; }
        }
        else
            for (int t = 0; t < TAPS; ++t)
            {
                const long long k = first + t;
                if (k < 0 || k >= n) continue;
                a += w[t] * in.l[(size_t) k]; b += w[t] * in.r[(size_t) k];
            }
        out->l[(size_t) i] = a * nrm; out->r[(size_t) i] = b * nrm;
    }
    return out;
}

// tempoRatio > 1 = faster, semitones = pitch change, independent of each other.
inline std::shared_ptr<Source> timePitch (const Source& in, double tempoRatio, double semitones)
{
    const double r = std::pow (2.0, semitones / 12.0);
    const double S = r / tempoRatio;                         // stretch factor, then play at rate r
    std::shared_ptr<Source> cur;
    if (std::abs (S - 1.0) < 0.002) { cur = std::make_shared<Source> (in); }
    else                            cur = wsolaStretch (in, S);
    if (cur == nullptr) return nullptr;
    std::shared_ptr<Source> res = std::abs (r - 1.0) < 0.0005 ? cur : resampleSinc (*cur, r);
    res->stretch = 1.0;
    res->buildPeaks();
    return res;
}

inline std::shared_ptr<Source> sliceSource (const Source& in, double startSec, double endSec)
{
    auto out = std::make_shared<Source>();
    out->sampleRate = in.sampleRate;
    const long long a = std::max<long long> (0, (long long) std::floor (startSec * in.sampleRate));
    const long long b = std::min<long long> ((long long) in.frames(), (long long) std::ceil (endSec * in.sampleRate));
    if (b > a)
    {
        out->l.assign (in.l.begin() + a, in.l.begin() + b);
        out->r.assign (in.r.begin() + a, in.r.begin() + b);
    }
    out->buildPeaks();
    return out;
}

inline std::shared_ptr<Source> reverseSource (const Source& in)
{
    auto out = std::make_shared<Source> (in);
    std::reverse (out->l.begin(), out->l.end());
    std::reverse (out->r.begin(), out->r.end());
    out->buildPeaks();
    return out;
}

// root-mean-square level (dBFS) of a stretch of audio, ignoring near-silence so sparse stems are not penalised
inline float activeRmsDb (const Source& s, long long a, long long b)
{
    a = std::max<long long> (0, a); b = std::min<long long> ((long long) s.frames(), b);
    if (b <= a) return -90.0f;
    const long long blk = 2048;
    double sum = 0; long long cnt = 0;
    for (long long i = a; i < b; i += blk)
    {
        double e = 0; const long long j1 = std::min (b, i + blk);
        for (long long j = i; j < j1; ++j) { const double m = 0.5 * (s.l[(size_t) j] + s.r[(size_t) j]); e += m * m; }
        e /= (double) (j1 - i);
        if (e > 1e-7) { sum += e; ++cnt; }               // active blocks only (> -70 dB)
    }
    if (cnt == 0) return -90.0f;
    return (float) (10.0 * std::log10 (sum / (double) cnt + 1e-12));
}
} // namespace al
