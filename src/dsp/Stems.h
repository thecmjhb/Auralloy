// Auralloy - "Quick Stems": a DSP stem separator (no neural network, no downloads).
//
//   percussive vs harmonic  : median-filter HPSS on the mid channel (Fitzgerald 2010)
//   bass                    : harmonic part below ~200 Hz
//   vocals (approximate)    : harmonic part in the voice band that is panned to the centre
//   other                   : everything else (also the exact residual, so  vocals+drums+bass+other == input)
//
// The separation quality is that of classic DSP: great for drums/bass, good for centred lead vocals,
// not a replacement for a neural separator. The engine is built so a neural model can be dropped in later.
#pragma once
#include "Dsp.h"
#include "Fft.h"
#include <algorithm>
#include <memory>

namespace al
{
struct StemSet
{
    std::shared_ptr<Source> vocals, drums, bass, other;
    bool ok() const { return vocals && drums && bass && other; }
    std::shared_ptr<Source> byIndex (int i) const { return i == 0 ? vocals : i == 1 ? drums : i == 2 ? bass : other; }
    static const char* name (int i) { static const char* n[4] = { "Vocals", "Drums", "Bass", "Other" }; return n[i]; }
};

inline StemSet separateStems (const Source& in, const std::atomic<bool>* cancel = nullptr, std::atomic<float>* progress = nullptr)
{
    StemSet out;
    const long long n = (long long) in.frames();
    if (n < 8192) return out;

    constexpr int N = 2048, H = 512, BINS = N / 2 + 1, CTX = 8, OWN = 192;
    const double sr = in.sampleRate;
    Fft fft (N);
    const auto win = hannWindow (N);
    const float norm = 1.0f / 1.5f;                       // sum of hann^2 at 75 % overlap

    const long long padLen = n + 2LL * N + N;
    const long long total = ((padLen - N) / H) + 1;       // frames over the padded signal
    auto at = [&] (const std::vector<float>& x, long long i) { const long long j = i - N; return (j >= 0 && j < n) ? x[(size_t) j] : 0.0f; };

    std::vector<std::vector<float>> o (8, std::vector<float> ((size_t) padLen, 0.0f));   // [stem*2 + ch] for vocals, drums, bass (3 stems used)

    // per-bin static weights
    std::vector<float> bassW ((size_t) BINS), vocBand ((size_t) BINS);
    auto smooth = [] (double a, double b, double x) { const double t = std::min (1.0, std::max (0.0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
    for (int k = 0; k < BINS; ++k)
    {
        const double f = k * sr / N;
        bassW[(size_t) k] = (float) (1.0 - smooth (110.0, 260.0, f));
        vocBand[(size_t) k] = (float) (smooth (140.0, 320.0, f) * (1.0 - smooth (7000.0, 11000.0, f)));
    }

    std::vector<std::complex<float>> bl, br;             // chunk spectra (frames x BINS)
    std::vector<float> mmag, smag;
    std::vector<std::complex<float>> tmp ((size_t) N);
    std::vector<std::complex<float>> spec ((size_t) N);
    float win17[2 * CTX + 1];
    std::vector<float> mD, mB, mV;

    for (long long f0 = 0; f0 < total; f0 += OWN)
    {
        if (cancel && cancel->load()) return StemSet {};
        const long long f1 = std::min (total, f0 + OWN);
        const long long c0 = std::max<long long> (0, f0 - CTX), c1 = std::min (total, f1 + CTX);
        const int nf = (int) (c1 - c0);
        bl.assign ((size_t) nf * BINS, {}); br.assign ((size_t) nf * BINS, {});
        mmag.assign ((size_t) nf * BINS, 0.0f); smag.assign ((size_t) nf * BINS, 0.0f);

        for (int t = 0; t < nf; ++t)
        {
            const long long s0 = (c0 + t) * H;
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto& x = ch == 0 ? in.l : in.r;
                for (int i = 0; i < N; ++i) tmp[(size_t) i] = { at (x, s0 + i) * win[(size_t) i], 0.0f };
                fft.forward (tmp.data());
                auto& dst = ch == 0 ? bl : br;
                for (int k = 0; k < BINS; ++k) dst[(size_t) t * BINS + (size_t) k] = tmp[(size_t) k];
            }
            for (int k = 0; k < BINS; ++k)
            {
                const auto L = bl[(size_t) t * BINS + (size_t) k], R = br[(size_t) t * BINS + (size_t) k];
                mmag[(size_t) t * BINS + (size_t) k] = std::abs (0.5f * (L + R));
                smag[(size_t) t * BINS + (size_t) k] = std::abs (0.5f * (L - R));
            }
        }

        for (long long ft = f0; ft < f1; ++ft)
        {
            const int t = (int) (ft - c0);
            // masks for this frame
            mD.assign ((size_t) BINS, 0.0f); mB.assign ((size_t) BINS, 0.0f); mV.assign ((size_t) BINS, 0.0f);
            for (int k = 0; k < BINS; ++k)
            {
                // harmonic: median over time
                int cnt = 0;
                for (int dt = -CTX; dt <= CTX; ++dt)
                {
                    const int tt = t + dt;
                    if (tt < 0 || tt >= nf) continue;
                    win17[cnt++] = mmag[(size_t) tt * BINS + (size_t) k];
                }
                std::nth_element (win17, win17 + cnt / 2, win17 + cnt);
                const float Hm = win17[cnt / 2];
                // percussive: median over frequency
                cnt = 0;
                for (int dk = -CTX; dk <= CTX; ++dk)
                {
                    const int kk = k + dk;
                    if (kk < 0 || kk >= BINS) continue;
                    win17[cnt++] = mmag[(size_t) t * BINS + (size_t) kk];
                }
                std::nth_element (win17, win17 + cnt / 2, win17 + cnt);
                const float Pm = win17[cnt / 2];
                const float h2 = Hm * Hm, p2 = Pm * Pm;
                const float Mh = h2 / (h2 + p2 + 1e-12f), Mp = 1.0f - Mh;
                const float M = mmag[(size_t) t * BINS + (size_t) k], S = smag[(size_t) t * BINS + (size_t) k];
                const float c = std::max (0.0f, (M * M - S * S) / (M * M + S * S + 1e-12f));
                const float cs = std::pow (c, 1.5f);
                mD[(size_t) k] = Mp;
                mB[(size_t) k] = Mh * bassW[(size_t) k];
                mV[(size_t) k] = Mh * (1.0f - bassW[(size_t) k]) * vocBand[(size_t) k] * cs;
            }

            const long long s0 = ft * H;
            for (int st = 0; st < 3; ++st)
            {
                const auto& mk = st == 0 ? mV : (st == 1 ? mD : mB);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto& src = ch == 0 ? bl : br;
                    for (int k = 0; k < BINS; ++k) spec[(size_t) k] = src[(size_t) t * BINS + (size_t) k] * mk[(size_t) k];
                    for (int k = 1; k < N / 2; ++k) spec[(size_t) (N - k)] = std::conj (spec[(size_t) k]);
                    fft.inverse (spec.data());
                    auto& dst = o[(size_t) (st * 2 + ch)];
                    for (int i = 0; i < N; ++i)
                    {
                        const long long p = s0 + i;
                        if (p < padLen) dst[(size_t) p] += spec[(size_t) i].real() * win[(size_t) i] * norm;
                    }
                }
            }
        }
        if (progress) progress->store ((float) f1 / (float) total);
    }

    auto mk = [&] { auto s = std::make_shared<Source>(); s->sampleRate = in.sampleRate; s->l.resize ((size_t) n); s->r.resize ((size_t) n); return s; };
    out.vocals = mk(); out.drums = mk(); out.bass = mk(); out.other = mk();
    for (long long i = 0; i < n; ++i)
    {
        const size_t p = (size_t) (i + N);
        out.vocals->l[(size_t) i] = o[0][p]; out.vocals->r[(size_t) i] = o[1][p];
        out.drums->l[(size_t) i]  = o[2][p]; out.drums->r[(size_t) i]  = o[3][p];
        out.bass->l[(size_t) i]   = o[4][p]; out.bass->r[(size_t) i]   = o[5][p];
        out.other->l[(size_t) i] = in.l[(size_t) i] - o[0][p] - o[2][p] - o[4][p];
        out.other->r[(size_t) i] = in.r[(size_t) i] - o[1][p] - o[3][p] - o[5][p];
    }
    for (auto* s : { out.vocals.get(), out.drums.get(), out.bass.get(), out.other.get() }) s->buildPeaks();
    return out;
}
} // namespace al
