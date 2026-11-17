// Auralloy - music analysis: tempo, beats, downbeats, key, sections. Pure C++.
//
//  * Tempo / beats : log-compressed spectral-flux onset envelope -> autocorrelation tempo prior ->
//                    dynamic-programming beat tracker (Ellis 2007 style) -> robust linear fit of the beat grid.
//  * Downbeats     : bar phase (assumes 4/4) chosen from low-band onset accents on beats.
//  * Key           : spectral-peak chroma -> Krumhansl-Schmuckler profile correlation.
//  * Sections      : bar-synchronous chroma + band energies -> self-similarity -> checkerboard novelty -> boundaries on phrase grid.
#pragma once
#include "Dsp.h"
#include "Fft.h"
#include <algorithm>
#include <numeric>
#include <string>

namespace al
{
struct Section
{
    int bar0 = 0, bar1 = 0;          // [bar0, bar1) indices into Analysis::downbeats
    double start = 0, end = 0;       // seconds in the original song
    float energy = 0;                // 0..1 relative to the rest of the song
    float vocal = 0;                 // 0..1 rough "vocal presence" proxy (refined later with stems)
    std::string label;
    int bars() const { return bar1 - bar0; }
};

struct Analysis
{
    bool ok = false;
    double duration = 0;
    double bpm = 0;                  // tempo of the beat grid (normalised to a 75-160 feel)
    float bpmConfidence = 0;
    std::vector<double> beats, downbeats;
    int keyRoot = 0;                 // 0 = C
    bool keyMinor = false;
    float keyConfidence = 0;
    std::string keyName, camelot;
    float loudnessDb = -30.0f;
    std::vector<Section> sections;
    std::vector<float> barEnergy;    // per bar, 0..1

    double barSeconds() const { return bpm > 0 ? 240.0 / bpm : 0.0; }
    int numBars() const { return downbeats.size() > 1 ? (int) downbeats.size() - 1 : 0; }
};

inline std::string keyNameOf (int root, bool minor)
{
    static const char* n[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return std::string (n[((root % 12) + 12) % 12]) + (minor ? " minor" : " major");
}

inline std::string camelotOf (int root, bool minor)
{
    const int majorRoot = minor ? (root + 3) % 12 : root % 12;
    const int num = ((majorRoot * 7 + 7) % 12) + 1;
    return std::to_string (num) + (minor ? "A" : "B");
}

namespace detail
{
inline double acfAt (const std::vector<float>& acf, double lag)
{
    if (lag < 0 || lag >= (double) acf.size() - 1) return 0.0;
    const int i = (int) lag; const double f = lag - i;
    return acf[(size_t) i] * (1.0 - f) + acf[(size_t) i + 1] * f;
}
} // namespace detail

inline Analysis analyze (const Source& src, const std::atomic<bool>* cancel = nullptr, std::atomic<float>* progress = nullptr)
{
    Analysis A;
    const size_t n = src.frames();
    const double sr0 = src.sampleRate;
    if (n < (size_t) (sr0 * 8.0)) return A;               // too short to analyse
    A.duration = (double) n / sr0;

    auto prog = [&] (float p) { if (progress) progress->store (p); };
    auto stop = [&] { return cancel != nullptr && cancel->load(); };

    // ---- mono, decimated by 2 (~22 kHz) ----
    const size_t m = n / 2;
    std::vector<float> ds (m);
    double sumsq = 0.0;
    for (size_t i = 0; i < m; ++i)
    {
        const float a = 0.5f * (src.l[2 * i] + src.r[2 * i]), b = 0.5f * (src.l[2 * i + 1] + src.r[2 * i + 1]);
        ds[i] = 0.5f * (a + b);
        sumsq += (double) ds[i] * ds[i];
    }
    const double sr = sr0 * 0.5;
    A.loudnessDb = (float) (10.0 * std::log10 (sumsq / (double) m + 1e-12));

    // ---- onset envelope ----
    const int N = 1024, H = 256;
    const int F = (int) ((m - (size_t) N) / (size_t) H) + 1;
    const double fr = sr / H;                                   // envelope frame rate
    Fft fft (N);
    const auto win = hannWindow (N);
    std::vector<std::complex<float>> buf ((size_t) N);
    std::vector<float> prev ((size_t) N / 2 + 1, 0.0f), cur ((size_t) N / 2 + 1);
    std::vector<float> env ((size_t) F, 0.0f), envLow ((size_t) F, 0.0f);
    for (int t = 0; t < F; ++t)
    {
        const float* x = &ds[(size_t) t * H];
        for (int i = 0; i < N; ++i) buf[(size_t) i] = { x[i] * win[(size_t) i], 0.0f };
        fft.forward (buf.data());
        float flux = 0.0f, fluxLow = 0.0f;
        for (int k = 1; k <= N / 2; ++k)
        {
            cur[(size_t) k] = std::log1p (std::abs (buf[(size_t) k]) * 4.0f);
            const float d = cur[(size_t) k] - prev[(size_t) k];
            if (d > 0.0f && t > 0)
            {
                if (k < 380) flux += d;
                if (k < 14) fluxLow += d;
            }
        }
        env[(size_t) t] = flux; envLow[(size_t) t] = fluxLow;
        prev.swap (cur);
        if ((t & 1023) == 0) { if (stop()) return A; prog (0.25f * (float) t / (float) F); }
    }
    // remove the slow trend and normalise
    auto detrend = [&] (std::vector<float>& v)
    {
        const int w = (int) (0.4 * fr);
        std::vector<float> out (v.size());
        double acc = 0.0;
        std::vector<double> cs (v.size() + 1, 0.0);
        for (size_t i = 0; i < v.size(); ++i) cs[i + 1] = cs[i] + v[i];
        for (int i = 0; i < (int) v.size(); ++i)
        {
            const int a = std::max (0, i - w), b = std::min ((int) v.size(), i + w + 1);
            const double mean = (cs[(size_t) b] - cs[(size_t) a]) / (double) (b - a);
            out[(size_t) i] = std::max (0.0f, (float) ((double) v[(size_t) i] - mean));
        }
        double s2 = 0.0; for (float x : out) s2 += (double) x * x;
        const float sd = (float) std::sqrt (s2 / (double) out.size() + 1e-12);
        for (auto& x : out) x /= sd;
        v.swap (out);
        (void) acc;
    };
    detrend (env);
    detrend (envLow);

    // ---- tempo from autocorrelation ----
    const int maxLag = (int) (fr * 60.0 / 55.0 * 4.0) + 4;
    std::vector<float> acf ((size_t) maxLag, 0.0f);
    for (int l = 0; l < maxLag && l < F; ++l)
    {
        double s = 0.0;
        for (int t = 0; t + l < F; ++t) s += (double) env[(size_t) t] * env[(size_t) (t + l)];
        acf[(size_t) l] = (float) (s / (double) (F - l));
    }
    double bestScore = -1e30, bestBpm = 120.0, secondScore = -1e30;
    for (double bpm = 62.0; bpm <= 190.0; bpm += 0.25)
    {
        const double lag = 60.0 * fr / bpm;
        double s = detail::acfAt (acf, lag) + 0.5 * detail::acfAt (acf, 2 * lag) + 0.25 * detail::acfAt (acf, 4 * lag)
                 + 0.35 * detail::acfAt (acf, 1.5 * lag);
        const double lo = std::log2 (bpm / 112.0);
        s *= std::exp (-0.5 * (lo / 0.55) * (lo / 0.55));
        if (s > bestScore) { secondScore = bestScore; bestScore = s; bestBpm = bpm; }
    }
    A.bpmConfidence = (float) std::min (1.0, std::max (0.0, bestScore / (std::abs (bestScore) + std::abs (secondScore) * 0.5 + 1e-6)));
    prog (0.35f);

    // ---- dynamic-programming beat tracker ----
    const double P = 60.0 * fr / bestBpm;
    std::vector<float> sm ((size_t) F);
    for (int t = 0; t < F; ++t)
    {
        const float a = env[(size_t) std::max (0, t - 1)], b = env[(size_t) t], c = env[(size_t) std::min (F - 1, t + 1)];
        sm[(size_t) t] = 0.25f * a + 0.5f * b + 0.25f * c;
    }
    std::vector<float> C ((size_t) F, 0.0f);
    std::vector<int> back ((size_t) F, -1);
    const int tauMin = std::max (2, (int) std::round (P * 0.5)), tauMax = (int) std::round (P * 2.0);
    const float alpha = 100.0f;
    for (int t = 0; t < F; ++t)
    {
        float best = 0.0f; int arg = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            const int s = t - tau;
            if (s < 0) break;
            const float lg = std::log ((float) tau / (float) P);
            const float v = C[(size_t) s] - alpha * lg * lg;
            if (arg < 0 || v > best) { best = v; arg = s; }
        }
        if (arg >= 0 && best > 0.0f) { C[(size_t) t] = sm[(size_t) t] + best; back[(size_t) t] = arg; }
        else                          { C[(size_t) t] = sm[(size_t) t]; back[(size_t) t] = -1; }
    }
    int tEnd = F - 1;
    {
        float best = -1e30f;
        for (int t = std::max (0, F - (int) (P * 1.2)); t < F; ++t) if (C[(size_t) t] > best) { best = C[(size_t) t]; tEnd = t; }
    }
    std::vector<int> bf;
    for (int t = tEnd; t >= 0; t = back[(size_t) t]) { bf.push_back (t); if (back[(size_t) t] < 0) break; }
    std::reverse (bf.begin(), bf.end());
    if (bf.size() < 8) return A;

    // frame -> seconds (the flux of frame t describes the window centred at t*H + N/2, onset ~ H/2 earlier)
    std::vector<double> bt;
    for (int f : bf) bt.push_back (((double) f * H + N * 0.5 - H * 0.5) / sr);

    // ---- initial period from the tracked beats ----
    std::vector<double> diffs;
    for (size_t i = 1; i < bt.size(); ++i) diffs.push_back (bt[i] - bt[i - 1]);
    std::sort (diffs.begin(), diffs.end());
    double per = diffs[diffs.size() / 2];

    // ---- fine comb search around that period: pins tempo and beat phase over the whole song ----
    auto envAt = [&] (double timeSec)
    {
        const double f = (timeSec * sr - N * 0.5 + H * 0.5) / H;
        if (f < 0 || f >= F - 1) return 0.0;
        const int i = (int) f; const double fr2 = f - i;
        return (double) sm[(size_t) i] * (1.0 - fr2) + (double) sm[(size_t) i + 1] * fr2;
    };
    double bestComb = -1e30, bestPer = per, bestPhase = bt[0];
    for (double p = per * 0.975; p <= per * 1.025; p += per * 0.0002)
    {
        for (double ph = 0.0; ph < p; ph += 1.0 / fr * 0.25)
        {
            double sc = 0.0;
            for (double t = ph; t < A.duration - 0.05; t += p) sc += envAt (t);
            sc /= std::floor ((A.duration - ph) / p) + 1.0;
            if (sc > bestComb) { bestComb = sc; bestPer = p; bestPhase = ph; }
        }
    }
    per = bestPer;
    double gridPer = per;
    while (60.0 / gridPer < 75.0) gridPer *= 0.5;       // fold into a musically common 75-160 BPM feel
    while (60.0 / gridPer >= 160.0) gridPer *= 2.0;
    A.bpm = 60.0 / gridPer;
    {
        double first = bestPhase + 0.010;      // calibrated: flux peaks ~10 ms before the true attack
        while (first - gridPer >= -0.001) first -= gridPer;
        while (first < -0.001) first += gridPer;
        for (double t = first; t < A.duration - 0.05; t += gridPer) A.beats.push_back (t);
    }
    if (A.beats.size() < 16) return A;
    prog (0.5f);
    if (stop()) return A;

    // ---- chroma + band energies per STFT frame (4096 @ 22 kHz) ----
    const int KN = 4096, KH = 2048;
    const int KF = (int) ((m - (size_t) KN) / (size_t) KH) + 1;
    Fft kfft (KN);
    const auto kwin = hannWindow (KN);
    std::vector<std::complex<float>> kb ((size_t) KN);
    std::vector<float> kmag ((size_t) KN / 2 + 1);
    struct FrameFeat { float chroma[12]; float band[6]; float rms; };
    std::vector<FrameFeat> ff ((size_t) KF);
    const double binHz = sr / KN;
    static const int bandEdgesHz[7] = { 20, 150, 500, 1500, 4000, 8000, 11000 };
    for (int t = 0; t < KF; ++t)
    {
        const float* x = &ds[(size_t) t * KH];
        double e = 0.0;
        for (int i = 0; i < KN; ++i) { kb[(size_t) i] = { x[i] * kwin[(size_t) i], 0.0f }; e += (double) x[i] * x[i]; }
        kfft.forward (kb.data());
        for (int k = 0; k <= KN / 2; ++k) kmag[(size_t) k] = std::abs (kb[(size_t) k]);
        auto& fe = ff[(size_t) t];
        for (float& c : fe.chroma) c = 0.0f;
        for (float& b : fe.band) b = 0.0f;
        fe.rms = (float) std::sqrt (e / KN);
        float peakMax = 1e-9f;
        for (int k = 8; k < KN / 2; ++k) peakMax = std::max (peakMax, kmag[(size_t) k]);
        for (int k = 3; k < KN / 2 - 1; ++k)
        {
            const double hz = k * binHz;
            for (int b = 0; b < 6; ++b) if (hz >= bandEdgesHz[b] && hz < bandEdgesHz[b + 1]) { fe.band[b] += kmag[(size_t) k] * kmag[(size_t) k]; break; }
            if (hz < 60.0 || hz > 3200.0) continue;
            const float a = kmag[(size_t) k - 1], b0 = kmag[(size_t) k], c0 = kmag[(size_t) k + 1];
            if (b0 > a && b0 >= c0 && b0 > 0.02f * peakMax)
            {
                const float denom = a - 2.0f * b0 + c0;
                const float delta = std::abs (denom) > 1e-12f ? 0.5f * (a - c0) / denom : 0.0f;
                const double f = (k + delta) * binHz;
                const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
                const double pcf = std::fmod (midi, 12.0) + (midi < 0 ? 12.0 : 0.0);
                const int p0 = (int) std::floor (pcf) % 12, p1 = (p0 + 1) % 12;
                const float fracp = (float) (pcf - std::floor (pcf));
                const float w = std::log1p (b0 * 4.0f);
                fe.chroma[p0] += w * (1.0f - fracp);
                fe.chroma[p1] += w * fracp;
            }
        }
        for (float& b : fe.band) b = std::log10 (1.0f + b);
        if ((t & 255) == 0) { if (stop()) return A; prog (0.5f + 0.2f * (float) t / (float) KF); }
    }

    // ---- key ----
    {
        double chroma[12] = {};
        for (const auto& fe : ff) for (int c = 0; c < 12; ++c) chroma[c] += fe.chroma[c];
        static const double major[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double minor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        auto corr = [&] (const double* prof, int root)
        {
            double mx = 0, my = 0;
            for (int i = 0; i < 12; ++i) { mx += chroma[i]; my += prof[i]; }
            mx /= 12; my /= 12;
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < 12; ++i)
            {
                const double x = chroma[(i + root) % 12] - mx, y = prof[i] - my;
                sxy += x * y; sxx += x * x; syy += y * y;
            }
            return sxy / std::sqrt (sxx * syy + 1e-12);
        };
        double best = -2, second = -2; int bestRoot = 0; bool bestMinor = false;
        for (int mode = 0; mode < 2; ++mode)
            for (int r = 0; r < 12; ++r)
            {
                const double c = corr (mode ? minor : major, r);
                if (c > best) { second = best; best = c; bestRoot = r; bestMinor = mode == 1; }
                else if (c > second) second = c;
            }
        A.keyRoot = bestRoot; A.keyMinor = bestMinor;
        A.keyConfidence = (float) std::min (1.0, std::max (0.0, (best - second) * 6.0 + 0.2 * best));
        A.keyName = keyNameOf (bestRoot, bestMinor);
        A.camelot = camelotOf (bestRoot, bestMinor);
    }
    prog (0.75f);

    // ---- downbeats: bar phase from low-band accents + chord changes ----
    {
        const size_t nb = A.beats.size();
        std::vector<double> accent (nb, 0.0), change (nb, 0.0);
        auto chromaIn = [&] (double t0b, double t1b, double* out)
        {
            for (int c = 0; c < 12; ++c) out[c] = 0.0;
            const int f0 = std::max (0, (int) std::floor ((t0b * sr - KN * 0.5) / KH)), f1 = std::min (KF - 1, std::max (f0, (int) std::ceil ((t1b * sr - KN * 0.5) / KH)));
            for (int f = f0; f <= f1; ++f) for (int c = 0; c < 12; ++c) out[c] += ff[(size_t) f].chroma[c];
            double s2 = 1e-9; for (int c = 0; c < 12; ++c) s2 += out[c] * out[c];
            s2 = std::sqrt (s2); for (int c = 0; c < 12; ++c) out[c] /= s2;
        };
        for (size_t i = 1; i + 1 < nb; ++i)
        {
            const int f = (int) std::round ((A.beats[i] * sr - N * 0.5 + H * 0.5) / H);
            double sacc = 0.0;
            for (int d = -2; d <= 2; ++d) { const int f2 = f + d; if (f2 >= 0 && f2 < F) sacc += envLow[(size_t) f2] + 0.35 * env[(size_t) f2]; }
            accent[i] = sacc;
            double cp[12], cn[12];
            chromaIn (A.beats[i - 1] + 0.1 * gridPer, A.beats[i] - 0.1 * gridPer, cp);
            chromaIn (A.beats[i] + 0.1 * gridPer, A.beats[i + 1] - 0.1 * gridPer, cn);
            double dot = 0; for (int c = 0; c < 12; ++c) dot += cp[c] * cn[c];
            change[i] = 1.0 - dot;
        }
        double phA[4] = {}, phC[4] = {}; int cnt[4] = {};
        for (size_t i = 1; i + 1 < nb; ++i) { phA[i % 4] += accent[i]; phC[i % 4] += change[i]; ++cnt[i % 4]; }
        double mA = 0, mC = 0, sA = 0, sC = 0;
        for (int p = 0; p < 4; ++p) { phA[p] /= std::max (1, cnt[p]); phC[p] /= std::max (1, cnt[p]); mA += phA[p] / 4; mC += phC[p] / 4; }
        for (int p = 0; p < 4; ++p) { sA += (phA[p] - mA) * (phA[p] - mA); sC += (phC[p] - mC) * (phC[p] - mC); }
        sA = std::sqrt (sA / 4 + 1e-12); sC = std::sqrt (sC / 4 + 1e-12);
        int bestPhase = 0; double bestV = -1e30;
        for (int p = 0; p < 4; ++p)
        {
            const double v = (phA[p] - mA) / sA + 1.2 * (phC[p] - mC) / sC;
            if (v > bestV) { bestV = v; bestPhase = p; }
        }
        for (size_t i = (size_t) bestPhase; i < nb; i += 4) A.downbeats.push_back (A.beats[i]);
        if (A.downbeats.size() > 1 && A.downbeats.back() + 4 * gridPer > A.duration + 0.5) A.downbeats.pop_back();
    }
    if (A.downbeats.size() < 6) return A;
    prog (0.8f);

    // ---- bar-synchronous features -> sections ----
    const int B = A.numBars();
    constexpr int D = 12 + 6 + 1;
    std::vector<std::vector<float>> feat ((size_t) B, std::vector<float> (D, 0.0f));
    std::vector<float> barRms ((size_t) B, 0.0f), barVocal ((size_t) B, 0.0f);
    for (int b = 0; b < B; ++b)
    {
        const int f0 = std::max (0, (int) std::floor ((A.downbeats[(size_t) b] * sr - KN * 0.5) / KH));
        const int f1 = std::min (KF - 1, std::max (f0, (int) std::floor ((A.downbeats[(size_t) b + 1] * sr - KN * 0.5) / KH)));
        int cnt = 0; double rms = 0, mid = 0, tot = 0;
        for (int f = f0; f <= f1; ++f)
        {
            for (int c = 0; c < 12; ++c) feat[(size_t) b][(size_t) c] += ff[(size_t) f].chroma[c];
            for (int k = 0; k < 6; ++k) feat[(size_t) b][12 + (size_t) k] += ff[(size_t) f].band[k];
            rms += ff[(size_t) f].rms;
            mid += std::pow (10.0, ff[(size_t) f].band[2]) + std::pow (10.0, ff[(size_t) f].band[3]);
            for (int k = 0; k < 6; ++k) tot += std::pow (10.0, ff[(size_t) f].band[k]);
            ++cnt;
        }
        cnt = std::max (1, cnt);
        float csum = 1e-6f;
        for (int c = 0; c < 12; ++c) csum += feat[(size_t) b][(size_t) c];
        for (int c = 0; c < 12; ++c) feat[(size_t) b][(size_t) c] /= csum;
        for (int k = 0; k < 6; ++k) feat[(size_t) b][12 + (size_t) k] /= (float) cnt;
        feat[(size_t) b][18] = (float) (rms / cnt);
        barRms[(size_t) b] = (float) (rms / cnt);
        barVocal[(size_t) b] = (float) (mid / (tot + 1e-9));
    }
    // z-score
    for (int d = 0; d < D; ++d)
    {
        double mu = 0, sd = 0;
        for (int b = 0; b < B; ++b) mu += feat[(size_t) b][(size_t) d];
        mu /= B;
        for (int b = 0; b < B; ++b) sd += (feat[(size_t) b][(size_t) d] - mu) * (feat[(size_t) b][(size_t) d] - mu);
        sd = std::sqrt (sd / B + 1e-12);
        for (int b = 0; b < B; ++b) feat[(size_t) b][(size_t) d] = (float) ((feat[(size_t) b][(size_t) d] - mu) / sd);
    }
    for (int b = 0; b < B; ++b) for (int d = 12; d < D; ++d) feat[(size_t) b][(size_t) d] *= (d == 18 ? 3.0f : 1.6f);   // loudness / timbre changes matter for section boundaries
    auto sim = [&] (int i, int j)
    {
        double dot = 0, ni = 0, nj = 0;
        for (int d = 0; d < D; ++d) { const double a = feat[(size_t) i][(size_t) d], c = feat[(size_t) j][(size_t) d]; dot += a * c; ni += a * a; nj += c * c; }
        return dot / std::sqrt (ni * nj + 1e-9);
    };
    const int L = 4;                                  // half kernel size in bars
    std::vector<double> nov ((size_t) B, 0.0);
    for (int i = 1; i < B; ++i)
    {
        double s = 0.0;
        for (int a = -L; a < L; ++a)
            for (int c = -L; c < L; ++c)
            {
                const int x = i + a, y = i + c;
                if (x < 0 || y < 0 || x >= B || y >= B) continue;
                const double sign = ((a < 0) == (c < 0)) ? 1.0 : -1.0;
                const double g = std::exp (-0.5 * ((a + 0.5) * (a + 0.5) + (c + 0.5) * (c + 0.5)) / (L * L * 0.5));
                s += sign * g * sim (x, y);
            }
        nov[(size_t) i] = s;
    }
    double nmean = 0, nsd = 0;
    for (double v : nov) nmean += v; nmean /= B;
    for (double v : nov) nsd += (v - nmean) * (v - nmean); nsd = std::sqrt (nsd / B + 1e-12);
    std::vector<int> bounds { 0 };
    for (int i = 2; i < B - 2; ++i)
        if (nov[(size_t) i] > nmean + 0.25 * nsd && nov[(size_t) i] >= nov[(size_t) i - 1] && nov[(size_t) i] >= nov[(size_t) i + 1] && i - bounds.back() >= 4)
        {
            int snapped = i;
            const int m4 = (int) std::round ((double) i / 4.0) * 4;           // phrase grid
            if (std::abs (m4 - i) <= 1 && m4 - bounds.back() >= 4) snapped = m4;
            bounds.push_back (snapped);
        }
    // extra cue: loudness steps on the phrase grid (verse -> chorus is often only a dynamics change)
    {
        std::vector<float> be ((size_t) B);
        {
            std::vector<float> db ((size_t) B);
            float lo2 = 1e9f, hi2 = -1e9f;
            for (int b = 0; b < B; ++b) { db[(size_t) b] = 20.0f * std::log10 (barRms[(size_t) b] + 1e-6f); lo2 = std::min (lo2, db[(size_t) b]); hi2 = std::max (hi2, db[(size_t) b]); }
            for (int b = 0; b < B; ++b) be[(size_t) b] = hi2 > lo2 ? (db[(size_t) b] - lo2) / (hi2 - lo2) : 0.5f;
        }
        auto meanE = [&] (int a, int c) { double s2 = 0; int k = 0; for (int i = std::max (0, a); i < std::min (B, c); ++i) { s2 += be[(size_t) i]; ++k; } return k ? s2 / k : 0.0; };
        for (int i = 8; i <= B - 8; i += 4)
        {
            const double step = std::abs (meanE (i, i + 4) - meanE (i - 4, i));
            bool near = false;
            for (int bb : bounds) if (std::abs (bb - i) < 4) near = true;
            if (! near && step > 0.12) bounds.push_back (i);
        }
        std::sort (bounds.begin(), bounds.end());
    }
    if (B - bounds.back() < 4 && bounds.size() > 1) bounds.pop_back();
    bounds.push_back (B);

    // section energy, vocal proxy, labels
    std::vector<float> rmsDb ((size_t) B);
    for (int b = 0; b < B; ++b) rmsDb[(size_t) b] = 20.0f * std::log10 (barRms[(size_t) b] + 1e-6f);
    float lo = 1e9f, hi = -1e9f;
    for (float v : rmsDb) { lo = std::min (lo, v); hi = std::max (hi, v); }
    A.barEnergy.resize ((size_t) B);
    for (int b = 0; b < B; ++b) A.barEnergy[(size_t) b] = hi > lo ? (rmsDb[(size_t) b] - lo) / (hi - lo) : 0.5f;
    for (size_t s = 0; s + 1 < bounds.size(); ++s)
    {
        Section sec;
        sec.bar0 = bounds[s]; sec.bar1 = bounds[s + 1];
        sec.start = A.downbeats[(size_t) sec.bar0]; sec.end = A.downbeats[(size_t) sec.bar1];
        double e = 0, v = 0;
        for (int b = sec.bar0; b < sec.bar1; ++b) { e += A.barEnergy[(size_t) b]; v += barVocal[(size_t) b]; }
        const int cnt = std::max (1, sec.bars());
        sec.energy = (float) (e / cnt); sec.vocal = (float) (v / cnt);
        A.sections.push_back (sec);
    }
    float eLo = 1e9f, eHi = -1e9f, vLo = 1e9f, vHi = -1e9f;
    for (auto& s : A.sections) { eLo = std::min (eLo, s.energy); eHi = std::max (eHi, s.energy); vLo = std::min (vLo, s.vocal); vHi = std::max (vHi, s.vocal); }
    for (size_t i = 0; i < A.sections.size(); ++i)
    {
        auto& s = A.sections[i];
        s.energy = eHi > eLo ? (s.energy - eLo) / (eHi - eLo) : 0.5f;
        s.vocal = vHi > vLo ? 0.25f + 0.75f * (s.vocal - vLo) / (vHi - vLo) : 0.6f;
        if (i == 0 && s.energy < 0.5f) s.label = "Intro";
        else if (i + 1 == A.sections.size() && s.energy < 0.5f) s.label = "Outro";
        else if (s.energy >= 0.72f) s.label = "Chorus / Drop";
        else if (s.energy >= 0.45f) s.label = "Verse";
        else s.label = "Break";
    }
    A.ok = true;
    prog (1.0f);
    return A;
}
} // namespace al
