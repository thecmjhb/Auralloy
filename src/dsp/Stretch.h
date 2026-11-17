// Auralloy - WSOLA time stretcher (changes tempo without changing pitch). Pure C++.
#pragma once
#include "Dsp.h"
#include <memory>

namespace al
{
// Returns a new Source that is `factor` times as long as `in` (factor > 1 = slower) with the same pitch.
inline std::shared_ptr<al::Source> wsolaStretch (const al::Source& in, double factor,
                                                 const std::atomic<bool>* cancel = nullptr,
                                                 std::atomic<float>* progress = nullptr)
{
    auto out = std::make_shared<al::Source>();
    out->sampleRate = in.sampleRate;
    out->stretch = in.stretch * factor;

    const long long n = (long long) in.frames();
    if (n < 4096 || factor <= 0.0) { out->l = in.l; out->r = in.r; out->stretch = in.stretch; out->buildPeaks(); return out; }

    int N = 1;
    while ((double) N < in.sampleRate * 0.0464) N <<= 1;            // ~46 ms analysis window
    const int Ho = N / 2;                                            // output hop (50% overlap)
    const int delta = N / 4;                                         // search tolerance
    constexpr int D = 4;                                             // decimation for the coarse search

    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (al::kTwoPi * (float) i / (float) N);

    // mono, decimated analysis signal (box filtered)
    const long long nd = n / D;
    std::vector<float> mono ((size_t) n), dec ((size_t) nd);
    for (long long i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (in.l[(size_t) i] + in.r[(size_t) i]);
    for (long long i = 0; i < nd; ++i)
    {
        float s = 0;
        for (int k = 0; k < D; ++k) s += mono[(size_t) (i * D + k)];
        dec[(size_t) i] = s * (1.0f / D);
    }

    const long long m = (long long) std::ceil ((double) n * factor) + N;
    out->l.assign ((size_t) m, 0.0f);
    out->r.assign ((size_t) m, 0.0f);

    const int corrLen = N / 2;                 // samples used for the similarity measure
    const int corrLenD = corrLen / D;
    long long prevStart = 0;

    for (long long outPos = 0, k = 0; outPos + N <= m; outPos += Ho, ++k)
    {
        if (cancel && cancel->load()) return nullptr;

        long long c = 0;
        const long long nominal = (long long) std::llround ((double) outPos / factor);
        if (k > 0)
        {
            const long long tgt = prevStart + Ho;                  // where the previous segment naturally continues
            long long lo = std::max<long long> (0, nominal - delta);
            long long hi = std::min<long long> (n - corrLen - 1, nominal + delta);
            if (tgt + corrLen >= n || hi <= lo) c = std::min<long long> (std::max<long long> (nominal, 0), n - 1);
            else
            {
                // coarse search on the decimated signal
                double best = -1e30; long long bestC = lo;
                const long long tgtD = tgt / D;
                for (long long cd = lo / D; cd <= hi / D; ++cd)
                {
                    if (cd + corrLenD >= nd || tgtD + corrLenD >= nd) break;
                    double dot = 0, e = 1e-9;
                    const float* a = &dec[(size_t) cd];
                    const float* b = &dec[(size_t) tgtD];
                    for (int i = 0; i < corrLenD; ++i) { dot += (double) a[i] * b[i]; e += (double) a[i] * a[i]; }
                    const double score = dot / std::sqrt (e);
                    if (score > best) { best = score; bestC = cd * D; }
                }
                // fine search at full resolution
                best = -1e30; c = bestC;
                for (long long cc = std::max (lo, bestC - D); cc <= std::min (hi, bestC + D); ++cc)
                {
                    double dot = 0, e = 1e-9;
                    const float* a = &mono[(size_t) cc];
                    const float* b = &mono[(size_t) tgt];
                    for (int i = 0; i < corrLen; i += 2) { dot += (double) a[i] * b[i]; e += (double) a[i] * a[i]; }
                    const double score = dot / std::sqrt (e);
                    if (score > best) { best = score; c = cc; }
                }
            }
        }
        c = std::min<long long> (std::max<long long> (c, 0), std::max<long long> (0, n - 1));

        float* ol = &out->l[(size_t) outPos];
        float* orr = &out->r[(size_t) outPos];
        const long long avail = std::min<long long> (N, n - c);
        for (long long i = 0; i < avail; ++i)
        {
            const float w = win[(size_t) i];
            ol[i]  += in.l[(size_t) (c + i)] * w;
            orr[i] += in.r[(size_t) (c + i)] * w;
        }
        prevStart = c;
        if (progress && (k & 63) == 0) progress->store ((float) ((double) outPos / (double) m));
    }

    out->l.resize ((size_t) std::ceil ((double) n * factor));
    out->r.resize (out->l.size());
    out->buildPeaks();
    return out;
}
} // namespace al
