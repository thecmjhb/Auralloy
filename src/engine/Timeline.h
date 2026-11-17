// Auralloy - multi-track timeline: data model, real-time/offline clip mixer, offline renderer. Pure C++.
//
// Every source on the timeline is stored at kProjectRate (44.1 kHz). Timeline positions are in seconds at 1x speed;
// the master "speed" (varispeed) just makes the playhead advance faster or slower, so slowed + reverb works on any arrangement.
#pragma once
#include "../dsp/Engine.h"
#include "../dsp/TimePitch.h"
#include <array>
#include <memory>
#include <string>

namespace al
{
constexpr double kProjectRate = 44100.0;

// How a clip's audio is produced from a library song, so projects can be saved and the audio rebuilt later
struct Recipe
{
    bool valid = false;
    int songId = -1;
    int role = 4;                 // 0 vocals, 1 drums, 2 bass, 3 other (stems), 4 full mix
    double srcStart = 0, srcEnd = 0;
    double tempoRatio = 1, semitones = 0;
    bool reversed = false;
};

struct Clip
{
    int id = 0;
    std::shared_ptr<const Source> src;
    double start = 0;          // timeline seconds
    double offset = 0;         // seconds into src where the clip starts
    double length = 0;         // seconds on the timeline
    float gainDb = 0;
    double fadeIn = 0, fadeOut = 0;
    int colour = 0;            // index into the source colour palette
    int songId = -1, role = -1;
    std::string name;
    Recipe recipe;
    double end() const { return start + length; }
};

struct Track
{
    int id = 0;
    std::string name;
    int role = -1;             // Role, or -1 for a free track
    int colour = 0;
    float volDb = 0, pan = 0;  // pan -1..1
    bool mute = false, solo = false;
    std::vector<Clip> clips;
};

struct Timeline
{
    std::vector<Track> tracks;
    int nextId = 1;

    double length() const
    {
        double e = 0;
        for (const auto& t : tracks) for (const auto& c : t.clips) e = std::max (e, c.end());
        return e;
    }
    Clip* findClip (int id)
    {
        for (auto& t : tracks) for (auto& c : t.clips) if (c.id == id) return &c;
        return nullptr;
    }
    const Clip* findClip (int id) const
    {
        for (const auto& t : tracks) for (const auto& c : t.clips) if (c.id == id) return &c;
        return nullptr;
    }
    Track* trackOfClip (int id)
    {
        for (auto& t : tracks) for (auto& c : t.clips) if (c.id == id) return &t;
        return nullptr;
    }
    bool empty() const { for (const auto& t : tracks) if (! t.clips.empty()) return false; return true; }
};

//==============================================================================
namespace detail
{
inline const std::array<float, 1025>& sincWindow()
{
    static const std::array<float, 1025> w = [] {
        std::array<float, 1025> a {};
        for (size_t i = 0; i < a.size(); ++i)
        {
            const double t = (double) i / 1024.0;
            a[i] = (float) (0.42 + 0.5 * std::cos (3.14159265358979 * t) + 0.08 * std::cos (2 * 3.14159265358979 * t));
        }
        return a;
    }();
    return w;
}

// interpolated read of a stereo source at a fractional position
inline void sincRead (const Source& s, double pos, double fc, double cD, double sD, float& outL, float& outR)
{
    constexpr int HALF = 12, TAPS = 24;
    const long long n = (long long) s.frames();
    const long long i0 = (long long) std::floor (pos);
    const double frac = pos - (double) i0;
    const auto& win = sincWindow();
    const double delta = 3.14159265358979 * fc;
    float w[TAPS];
    double th = delta * ((double) (-HALF + 1) - frac);
    double sn = std::sin (th), cs = std::cos (th), sum = 0.0;
    for (int t = 0; t < TAPS; ++t)
    {
        const double x = (double) (t - HALF + 1) - frac;
        const double sinc = std::fabs (th) < 1e-9 ? 1.0 : sn / th;
        const double ax = std::min (0.9999, std::fabs (x) / (double) HALF);
        const double wv = (double) win[(size_t) (ax * 1024.0)] * sinc;
        w[t] = (float) wv; sum += wv;
        const double ns = sn * cD + cs * sD;
        cs = cs * cD - sn * sD; sn = ns;
        th += delta;
    }
    const float nrm = (float) (1.0 / sum);
    float a = 0.0f, b = 0.0f;
    const long long first = i0 - HALF + 1;
    if (first >= 0 && first + TAPS < n)
    {
        const float* pl = &s.l[(size_t) first]; const float* pr = &s.r[(size_t) first];
        for (int t = 0; t < TAPS; ++t) { a += w[t] * pl[t]; b += w[t] * pr[t]; }
    }
    else
        for (int t = 0; t < TAPS; ++t)
        {
            const long long k = first + t;
            if (k < 0 || k >= n) continue;
            a += w[t] * s.l[(size_t) k]; b += w[t] * s.r[(size_t) k];
        }
    outL = a * nrm; outR = b * nrm;
}
} // namespace detail

// Mixes all clips that overlap [t0, t0 + n*dt) into L/R (added). dt = timeline seconds per output sample.
inline void mixTimelineBlock (const Timeline& tl, double t0, double dt, double outSr, int n, float* L, float* R)
{
    (void) outSr;
    bool anySolo = false;
    for (const auto& t : tl.tracks) if (t.solo) anySolo = true;
    const double tEnd = t0 + (double) n * dt;
    const double r = dt * kProjectRate;                              // source frames per output sample
    const double fc = r <= 1.0 ? 1.0 : 0.97 / r;
    const double delta = 3.14159265358979 * fc, cD = std::cos (delta), sD = std::sin (delta);
    const bool direct = std::abs (r - 1.0) < 1e-9;

    for (const auto& t : tl.tracks)
    {
        if (t.mute || (anySolo && ! t.solo)) continue;
        const float tg = dbToLin (t.volDb);
        const float ang = (t.pan + 1.0f) * 0.25f * 3.14159265f;
        const float gl = tg * std::cos (ang) * 1.41421356f, gr = tg * std::sin (ang) * 1.41421356f;
        for (const auto& c : t.clips)
        {
            if (! c.src || c.end() <= t0 || c.start >= tEnd || c.length <= 0) continue;
            const int i0 = std::max (0, (int) std::ceil ((c.start - t0) / dt));
            const int i1 = std::min (n, (int) std::ceil ((c.end() - t0) / dt));
            const float cg = dbToLin (c.gainDb);
            const double edge = 0.002;
            for (int i = i0; i < i1; ++i)
            {
                const double local = t0 + (double) i * dt - c.start;
                const double pos = (local + c.offset) * kProjectRate;
                double g = (double) cg;
                const double toEnd = c.length - local;
                g *= std::min (1.0, std::min (local / edge, toEnd / edge));
                if (c.fadeIn > 0 && local < c.fadeIn)   g *= std::sin (1.5707963267948966 * local / c.fadeIn);
                if (c.fadeOut > 0 && toEnd < c.fadeOut) g *= std::sin (1.5707963267948966 * toEnd / c.fadeOut);
                float a, b;
                if (direct)
                {
                    const long long k = (long long) std::llround (pos);
                    if (k < 0 || k >= (long long) c.src->frames()) continue;
                    a = c.src->l[(size_t) k]; b = c.src->r[(size_t) k];
                }
                else detail::sincRead (*c.src, pos, fc, cD, sD, a, b);
                L[i] += (float) g * gl * a;
                R[i] += (float) g * gr * b;
            }
        }
    }
}

// master fade in/out (output-time seconds) at the edges of the played region
inline float masterFade (double t, double a, double b, double fadeIn, double fadeOut, double speed)
{
    double g = 1.0;
    const double fi = fadeIn * speed, fo = fadeOut * speed;
    if (fi > 1e-6 && t - a < fi) g *= std::sin (1.5707963267948966 * std::max (0.0, (t - a) / fi));
    if (fo > 1e-6 && b - t < fo) g *= std::sin (1.5707963267948966 * std::max (0.0, (b - t) / fo));
    return (float) g;
}

//==============================================================================
// Offline render of the whole timeline (or a region) through the master chain.
inline bool renderTimeline (const Timeline& tl, const MixParams& mp, double speed, double regionA, double regionB,
                            double fadeIn, double fadeOut, double outSr,
                            std::vector<float>& oL, std::vector<float>& oR,
                            const std::function<bool (float)>& progress)
{
    Chain chain; chain.prepare (outSr);
    const double a = regionB > regionA + 0.01 ? regionA : 0.0;
    const double b = regionB > regionA + 0.01 ? regionB : tl.length();
    if (b <= a) return false;
    const double dt = speed / outSr;
    const int B = 1024;
    std::vector<float> L ((size_t) B), R ((size_t) B);
    const double totalOut = (b - a) / dt;
    double tail = 0.6;
    if (mp.rvOn && ! mp.bypass) tail = std::max (tail, (double) mp.rvDecay * 1.3 + mp.rvPre * 0.001);
    if (mp.dlOn && ! mp.bypass && mp.dlMix > 0.01f) tail = std::max (tail, 7.0);
    tail = std::min (tail, 30.0);

    oL.clear(); oR.clear();
    oL.reserve ((size_t) (totalOut + tail * outSr + 4096)); oR.reserve (oL.capacity());
    double t = a, done = 0.0, afterEnd = 0.0;
    int quiet = 0;
    while (true)
    {
        std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
        const bool playing = t < b;
        if (playing)
        {
            mixTimelineBlock (tl, t, dt, outSr, B, L.data(), R.data());
            for (int i = 0; i < B; ++i)
            {
                const double ti = t + (double) i * dt;
                if (ti >= b) { L[(size_t) i] = R[(size_t) i] = 0.0f; continue; }
                const float f = masterFade (ti, a, b, fadeIn, fadeOut, speed);
                L[(size_t) i] *= f; R[(size_t) i] *= f;
            }
        }
        chain.process (L.data(), R.data(), B, mp);
        oL.insert (oL.end(), L.begin(), L.end());
        oR.insert (oR.end(), R.begin(), R.end());
        t += (double) B * dt; done += B;
        if (! playing || t >= b)
        {
            if (! playing)
            {
                afterEnd += B;
                float pk = 0;
                for (int i = 0; i < B; ++i) pk = std::max (pk, std::max (std::fabs (L[(size_t) i]), std::fabs (R[(size_t) i])));
                const bool noisy = mp.rain > 0.001f || mp.vinyl > 0.001f || mp.hiss > 0.001f;
                quiet = (pk < 3e-5f && ! noisy) ? quiet + B : 0;
                if (afterEnd > tail * outSr || (quiet > (int) (0.35 * outSr) && afterEnd > 0.4 * outSr)) break;
            }
        }
        if (! progress ((float) std::min (1.0, done / (totalOut + tail * outSr * 0.5)))) return false;
    }
    const size_t lat = std::min ((size_t) chain.latency(), oL.size());
    oL.erase (oL.begin(), oL.begin() + (std::ptrdiff_t) lat);
    oR.erase (oR.begin(), oR.begin() + (std::ptrdiff_t) lat);
    size_t last = 0;
    for (size_t i = oL.size(); i-- > 0;)
        if (std::fabs (oL[i]) > 1e-4f || std::fabs (oR[i]) > 1e-4f) { last = i; break; }
    const size_t keep = std::min (oL.size(), std::max ((size_t) totalOut, last + (size_t) (0.05 * outSr)));
    oL.resize (keep); oR.resize (keep);
    return true;
}
} // namespace al
