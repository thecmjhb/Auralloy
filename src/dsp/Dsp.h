// Auralloy - shared DSP primitives (pure C++, no JUCE dependency so everything can be unit-tested)
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace al
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

//==============================================================================
struct Source
{
    std::vector<float> l, r;
    std::vector<float> peaks; // min,max pairs for the waveform view
    double sampleRate = 44100.0;
    double stretch = 1.0;   // length of this buffer relative to the original song (time-stretched copies)

    size_t frames() const { return l.size(); }
    double seconds() const { return sampleRate > 0 ? (double) l.size() / sampleRate : 0.0; }

    void buildPeaks (int columns = 1400)
    {
        peaks.assign ((size_t) columns * 2, 0.0f);
        const size_t n = l.size();
        if (n == 0) return;
        for (int c = 0; c < columns; ++c)
        {
            size_t a = (size_t) ((double) c * (double) n / columns);
            size_t b = (size_t) ((double) (c + 1) * (double) n / columns);
            if (b <= a) b = a + 1;
            b = std::min (b, n);
            float mn = 1e9f, mx = -1e9f;
            const size_t step = std::max<size_t> (1, (b - a) / 64);
            for (size_t i = a; i < b; i += step)
            {
                const float m = 0.5f * (l[i] + r[i]);
                mn = std::min (mn, m);
                mx = std::max (mx, m);
            }
            if (mn > mx) { mn = mx = 0.0f; }
            peaks[(size_t) c * 2]     = mn;
            peaks[(size_t) c * 2 + 1] = mx;
        }
    }
};

//==============================================================================
struct Rng
{
    uint32_t s = 2463534242u;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // 0..1
    float bip() { return uni() * 2.0f - 1.0f; }                             // -1..1
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void setLowpass (double sr, double fc, double q = 0.7071)
    {
        fc = std::min (fc, sr * 0.49);
        fc = std::max (fc, 20.0);
        const double w0 = 2.0 * 3.14159265358979 * fc / sr;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (float) (((1.0 - cw) * 0.5) / a0);
        b1 = (float) ((1.0 - cw) / a0);
        b2 = b0;
        a1 = (float) ((-2.0 * cw) / a0);
        a2 = (float) ((1.0 - alpha) / a0);
    }
    inline float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};

struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;

    void init (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 8) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // d >= 1: d == 1 is the most recently pushed sample
    inline float tap (float d) const
    {
        const int i = (int) d;
        const float f = d - (float) i;
        const float a = buf[(size_t) ((w - i) & mask)];
        const float b = buf[(size_t) ((w - i - 1) & mask)];
        return a + f * (b - a);
    }
    inline float tapInt (int d) const { return buf[(size_t) ((w - d) & mask)]; }
};

struct Allpass
{
    DelayLine line;
    int len = 1;
    float g = 0.6f;
    void init (int n) { len = std::max (1, n); line.init (len + 4); }
    inline float process (float x)
    {
        const float d = line.tapInt (len);
        const float w = x + g * d;
        line.push (w);
        return d - g * w;
    }
    void clear() { line.clear(); }
};

//==============================================================================
// 8-line feedback delay network reverb with modulation, input diffusion and damping
class Reverb
{
public:
    static constexpr int N = 8;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const double scale = sr / 48000.0 * 1.25;
        static const int base[N] = { 1327, 1621, 1933, 2269, 2647, 3037, 3469, 3911 };
        modDepth = (float) (10.0 * sr / 48000.0);
        for (int i = 0; i < N; ++i)
        {
            len[i] = (float) (base[i] * scale);
            line[i].init ((int) len[i] + (int) modDepth + 16);
            phase[i] = (float) i / (float) N;
            rate[i] = 0.11f + 0.037f * (float) i;
            lp[i] = 0;
        }
        const double a = sr / 48000.0;
        apL[0].init ((int) (241 * a)); apL[1].init ((int) (83 * a));
        apR[0].init ((int) (263 * a)); apR[1].init ((int) (97 * a));
        for (auto& ap : apL) ap.g = 0.62f;
        for (auto& ap : apR) ap.g = 0.62f;
        pre[0].init ((int) (0.26 * sr) + 8);
        pre[1].init ((int) (0.26 * sr) + 8);
        setParams (4.0f, 0.5f, 25.0f);
        hpL = hpR = 0;
        hpCoef = 1.0f - std::exp (-kTwoPi * 110.0f / (float) sr);
    }

    void reset()
    {
        for (auto& l : line) l.clear();
        for (auto& a : apL) a.clear();
        for (auto& a : apR) a.clear();
        pre[0].clear(); pre[1].clear();
        for (auto& x : lp) x = 0;
        hpL = hpR = 0;
    }

    void setParams (float decaySec, float damp, float preMs)
    {
        decaySec = std::max (0.1f, decaySec);
        for (int i = 0; i < N; ++i)
            g[i] = std::pow (10.0f, -3.0f * len[i] / ((float) sr * decaySec));
        const float fc = 16000.0f + (1500.0f - 16000.0f) * std::min (1.0f, std::max (0.0f, damp));
        dampC = 1.0f - std::exp (-kTwoPi * fc / (float) sr);
        preSamples = std::max (1.0f, preMs * 0.001f * (float) sr);
    }

    void setLowCut (float hz) { hpCoef = 1.0f - std::exp (-kTwoPi * std::max (10.0f, hz) / (float) sr); }

    inline void process (float inL, float inR, float& outL, float& outR)
    {
        pre[0].push (inL); pre[1].push (inR);
        float dl = pre[0].tap (preSamples);
        float dr = pre[1].tap (preSamples);
        for (auto& a : apL) dl = a.process (dl);
        for (auto& a : apR) dr = a.process (dr);

        float t[N];
        for (int i = 0; i < N; ++i)
        {
            const float d = len[i] + modDepth * std::sin (kTwoPi * phase[i]);
            phase[i] += rate[i] / (float) sr;
            if (phase[i] >= 1.0f) phase[i] -= 1.0f;
            const float v = line[i].tap (d);
            lp[i] += dampC * (v - lp[i]);
            t[i] = lp[i] * g[i];
        }

        float oL = 0.5f * (t[0] - t[2] + t[4] - t[6]);
        float oR = 0.5f * (t[1] - t[3] + t[5] - t[7]);

        // Hadamard mixing matrix
        float m[N];
        for (int i = 0; i < N; ++i) m[i] = t[i];
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += 2 * h)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = m[j], b = m[j + h];
                    m[j] = a + b; m[j + h] = a - b;
                }
        constexpr float norm = 0.35355339f;
        for (int i = 0; i < N; ++i)
            line[i].push (((i & 1) ? dr : dl) * 0.5f + m[i] * norm);

        // gentle low-cut on the wet signal so the tail never gets muddy
        hpL += hpCoef * (oL - hpL);
        hpR += hpCoef * (oR - hpR);
        outL = oL - hpL;
        outR = oR - hpR;
    }

private:
    double sr = 44100.0;
    float len[N] {}, g[N] {}, lp[N] {}, phase[N] {}, rate[N] {};
    DelayLine line[N];
    Allpass apL[2], apR[2];
    DelayLine pre[2];
    float modDepth = 10.0f, dampC = 0.5f, preSamples = 100.0f;
    float hpL = 0, hpR = 0, hpCoef = 0.01f;
};

} // namespace al
