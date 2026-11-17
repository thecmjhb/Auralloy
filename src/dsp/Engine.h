// Auralloy - audio engine: windowed-sinc varispeed player + full effect chain + offline renderer.
// Pure C++ (no JUCE) so it can be tested headlessly. Everything runs in 32-bit float.
#pragma once
#include "Dsp.h"
#include <array>
#include <functional>
#include <memory>

namespace al
{

inline float dbToLin (float db) { return std::pow (10.0f, db / 20.0f); }

//==============================================================================
// RBJ biquad (transposed direct form II)
struct BQ
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    inline float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }

    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2)
    {
        b0 = (float) (nb0 / a0); b1 = (float) (nb1 / a0); b2 = (float) (nb2 / a0);
        a1 = (float) (na1 / a0); a2 = (float) (na2 / a0);
    }
    static double clampF (double f, double sr) { return std::min (std::max (f, 5.0), sr * 0.47); }

    void lowpass (double sr, double f, double q = 0.7071)
    {
        const double w = 2 * 3.14159265358979 * clampF (f, sr) / sr, cw = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + al, -2 * cw, 1 - al);
    }
    void highpass (double sr, double f, double q = 0.7071)
    {
        const double w = 2 * 3.14159265358979 * clampF (f, sr) / sr, cw = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + al, -2 * cw, 1 - al);
    }
    void peak (double sr, double f, double db, double q)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * 3.14159265358979 * clampF (f, sr) / sr;
        const double cw = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al * A, -2 * cw, 1 - al * A, 1 + al / A, -2 * cw, 1 - al / A);
    }
    void lowShelf (double sr, double f, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * 3.14159265358979 * clampF (f, sr) / sr;
        const double cw = std::cos (w), al = std::sin (w) / 2.0 * std::sqrt (2.0), t = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * cw + t), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - t),
             (A + 1) + (A - 1) * cw + t, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - t);
    }
    void highShelf (double sr, double f, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * 3.14159265358979 * clampF (f, sr) / sr;
        const double cw = std::cos (w), al = std::sin (w) / 2.0 * std::sqrt (2.0), t = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * cw + t), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - t),
             (A + 1) - (A - 1) * cw + t, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - t);
    }
};

//==============================================================================
struct MixParams
{
    bool bypass = false;

    bool eqOn = false;
    float hpf = 20, lowG = 0, lmG = 0, hmG = 0, highG = 0, lpf = 20000;

    bool lofiOn = false;
    float bits = 16, srate = 48000, wobble = 0, flutter = 0, drive = 0;

    bool compOn = false;
    float thresh = -18, ratio = 3, attack = 15, release = 150, makeup = 0, compMix = 1;

    bool chOn = false;
    float chRate = 0.6f, chDepth = 0.4f, chMix = 0;

    bool dlOn = false, dlPing = true;
    float dlTime = 380, dlFb = 0.35f, dlTone = 4000, dlMix = 0;

    bool rvOn = false;
    float rvMix = 0, rvDecay = 4, rvDamp = 0.5f, rvPre = 25, rvLow = 110;

    float width = 1.0f, monoBass = 0;
    float rain = 0, rainDens = 0.5f, vinyl = 0, hiss = 0;
    float gainDb = 0, loudness = 0, ceiling = -0.3f;
};

//==============================================================================
// Look-ahead brick-wall limiter
class Limiter
{
public:
    void prepare (double sr)
    {
        la = std::max (8, (int) std::round (0.0025 * sr));
        dl0.assign ((size_t) la + 1, 0.0f); dl1.assign ((size_t) la + 1, 0.0f); pk.assign ((size_t) la + 1, 0.0f);
        idx = 0; gain = 1.0f;
        atk = 1.0f - std::exp (-1.0f / (0.2f * (float) la));
        rel = std::exp (-1.0f / (0.12f * (float) sr));
    }
    int latency() const { return la; }
    void reset()
    {
        std::fill (dl0.begin(), dl0.end(), 0.0f); std::fill (dl1.begin(), dl1.end(), 0.0f);
        std::fill (pk.begin(), pk.end(), 0.0f); gain = 1.0f;
    }

    inline void process (float& l, float& r, float ceil)
    {
        dl0[(size_t) idx] = l; dl1[(size_t) idx] = r;
        pk[(size_t) idx] = std::max (std::fabs (l), std::fabs (r));
        float m = 0.0f;
        for (float v : pk) m = std::max (m, v);
        const float target = m > ceil ? ceil / m : 1.0f;
        if (target < gain) gain += (target - gain) * atk;
        else               gain = target + (gain - target) * rel;
        idx = (idx + 1) % (la + 1);
        l = std::min (ceil, std::max (-ceil, dl0[(size_t) idx] * gain));
        r = std::min (ceil, std::max (-ceil, dl1[(size_t) idx] * gain));
    }
private:
    int la = 100, idx = 0;
    float gain = 1.0f, atk = 0.1f, rel = 0.999f;
    std::vector<float> dl0, dl1, pk;
};

//==============================================================================
struct RainVoice
{
    float lpA = 0, lpB = 0, mod = 0, modT = 0, drop = 0;
    int cnt = 0;
    float next (al::Rng& rng, float density, float sr, float aHP, float aLP)
    {
        const float n = rng.bip();
        lpA += aHP * (n - lpA);
        const float band = n - lpA;
        lpB += aLP * (band - lpB);
        if (--cnt <= 0) { modT = rng.uni(); cnt = (int) (sr * (0.3f + rng.uni())); }
        mod += (modT - mod) * (1.0f / (0.25f * sr));
        if (rng.uni() < density * 90.0f / sr) drop = 0.3f + 0.7f * rng.uni();
        const float dn = rng.bip() * drop;
        drop *= 0.92f;
        return lpB * (0.55f + 0.45f * mod) * (0.4f + 0.6f * density) * 1.6f + dn * 0.25f;
    }
};

//==============================================================================
class Chain
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        rv.prepare (sr);
        lim.prepare (sr);
        for (auto& d : wobD) d.init ((int) (0.04 * sr));
        for (auto& d : chD) d.init ((int) (0.08 * sr));
        for (auto& d : dlD) d.init ((int) (2.2 * sr));
        aHP = 1.0f - std::exp (-kTwoPi * 1200.0f / (float) sr);
        aLP = 1.0f - std::exp (-kTwoPi * 6500.0f / (float) sr);
        reset();
    }

    void reset()
    {
        rv.reset(); lim.reset();
        for (auto& d : wobD) d.clear();
        for (auto& d : chD) d.clear();
        for (auto& d : dlD) d.clear();
        for (int c = 0; c < 2; ++c)
        {
            hp[c].reset(); ls[c].reset(); p1[c].reset(); p2[c].reset(); hs[c].reset(); lp1[c].reset(); lp2[c].reset();
            bassLp[c].reset(); fbLp[c] = 0; held[c] = 0; pop[c] = 0; hissV[c] = 0; rain[c] = RainVoice();
        }
        grDb = 0; holdPhase = 0; dSm = 0; first = true;
    }

    // samples by which the output is late compared with the input (wobble delay + limiter look-ahead)
    int latency() const { return (int) std::round (0.006 * sr) + lim.latency(); }
    float lastGainReductionDb() const { return grOut; }

    void process (float* L, float* R, int n, const MixParams& p)
    {
        if (n <= 0) return;
        const bool fx = ! p.bypass;
        const bool eqE = fx && p.eqOn, loE = fx && p.lofiOn, coE = fx && p.compOn, chE = fx && p.chOn,
                   dlE = fx && p.dlOn, rvE = fx && p.rvOn;

        Targets t;
        t.wobble = loE ? p.wobble : 0.0f;
        t.flutter = loE ? p.flutter : 0.0f;
        t.drive = loE ? p.drive : 0.0f;
        t.compMix = coE ? p.compMix : 0.0f;
        t.chMix = chE ? p.chMix : 0.0f;
        t.dlMix = dlE ? p.dlMix : 0.0f;
        t.rvMix = rvE ? p.rvMix : 0.0f;
        t.width = fx ? p.width : 1.0f;
        t.rain = fx ? p.rain : 0.0f;
        t.vinyl = fx ? p.vinyl : 0.0f;
        t.hiss = fx ? p.hiss : 0.0f;
        t.gain = dbToLin (p.gainDb + p.loudness);
        t.eq = eqE ? 1.0f : 0.0f;
        if (first) { prev = t; first = false; }

        for (int c = 0; c < 2; ++c)
        {
            hp[c].highpass (sr, eqE ? p.hpf : 5.0);
            ls[c].lowShelf (sr, 120.0, eqE ? p.lowG : 0.0);
            p1[c].peak (sr, 400.0, eqE ? p.lmG : 0.0, 0.8);
            p2[c].peak (sr, 2500.0, eqE ? p.hmG : 0.0, 0.8);
            hs[c].highShelf (sr, 8000.0, eqE ? p.highG : 0.0);
            lp1[c].lowpass (sr, eqE ? p.lpf : sr * 0.46);
            lp2[c].lowpass (sr, eqE ? p.lpf : sr * 0.46);
            bassLp[c].lowpass (sr, std::max (30.0f, p.monoBass));
        }
        rv.setParams (p.rvDecay, p.rvDamp, p.rvPre);
        rv.setLowCut (p.rvLow);
        const float wetScale = 4.0f * std::pow (std::max (0.1f, p.rvDecay), -0.45f);
        const float ceil = std::max (0.05f, dbToLin (p.ceiling));
        const bool crushBits = loE && p.bits < 15.9f;
        const float levels = std::exp2 (std::max (3.0f, p.bits) - 1.0f);
        const bool crushRate = loE && p.srate < (float) sr * 0.98f;
        const float hold = crushRate ? (float) sr / std::max (1500.0f, p.srate) : 1.0f;
        const float baseD = std::round ((float) (0.006 * sr));
        const float attC = std::exp (-1.0f / std::max (1.0f, p.attack * 0.001f * (float) sr));
        const float relC = std::exp (-1.0f / std::max (1.0f, p.release * 0.001f * (float) sr));
        const float makeup = dbToLin (p.makeup);
        const float ratioK = 1.0f - 1.0f / std::max (1.0f, p.ratio);
        const float dlTarget = std::max (2.0f, p.dlTime * 0.001f * (float) sr);
        const float dlToneC = 1.0f - std::exp (-kTwoPi * std::min (p.dlTone, (float) sr * 0.45f) / (float) sr);
        const float invN = 1.0f / (float) n;
        const float srf = (float) sr;
        const bool monoBassOn = fx && p.monoBass >= 30.0f;
        if (dSm <= 0.0f) dSm = dlTarget;
        float grMax = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float k = (float) (i + 1) * invN;
            auto rp = [k] (float a, float b) { return a + (b - a) * k; };
            const float wobAmt = rp (prev.wobble, t.wobble), fltAmt = rp (prev.flutter, t.flutter), drv = rp (prev.drive, t.drive);
            const float cmix = rp (prev.compMix, t.compMix), chm = rp (prev.chMix, t.chMix);
            const float dlm = rp (prev.dlMix, t.dlMix), rvm = rp (prev.rvMix, t.rvMix);
            const float wid = rp (prev.width, t.width), gain = rp (prev.gain, t.gain);
            const float rainL = rp (prev.rain, t.rain), vin = rp (prev.vinyl, t.vinyl), his = rp (prev.hiss, t.hiss);
            const float eqm = rp (prev.eq, t.eq);

            float x[2] = { L[i], R[i] };

            // ---- tape wow & flutter (modulated delay) ----
            wow += 0.55f / srf; if (wow >= 1.0f) wow -= 1.0f;
            fl  += 6.3f / srf;  if (fl >= 1.0f) fl -= 1.0f;
            if (--driftCnt <= 0) { driftT = rng.bip(); driftCnt = (int) (0.8f * srf); }
            drift += (driftT - drift) * (1.0f / (0.6f * srf));
            const float mod = wobAmt * (0.0030f * std::sin (kTwoPi * wow) + 0.0015f * drift)
                            + fltAmt * 0.0006f * std::sin (kTwoPi * fl);
            const float dd = std::max (1.0f, baseD + mod * srf);
            for (int c = 0; c < 2; ++c) { wobD[c].push (x[c]); x[c] = wobD[c].tap (dd); }

            // ---- saturation ----
            if (drv > 0.002f)
            {
                const float dg = 1.0f + drv * 5.0f, comp = 1.0f / std::sqrt (dg);
                x[0] = std::tanh (x[0] * dg) * comp;
                x[1] = std::tanh (x[1] * dg) * comp;
            }

            // ---- bit / sample-rate crush ----
            if (crushRate)
            {
                holdPhase += 1.0f;
                if (holdPhase >= hold) { holdPhase -= hold; held[0] = x[0]; held[1] = x[1]; }
                x[0] = held[0]; x[1] = held[1];
            }
            if (crushBits)
            {
                x[0] = std::round (x[0] * levels) / levels;
                x[1] = std::round (x[1] * levels) / levels;
            }

            // ---- EQ (flat when disabled) ----
            for (int c = 0; c < 2; ++c)
            {
                const float y = lp2[c].process (lp1[c].process (hs[c].process (p2[c].process (p1[c].process (ls[c].process (hp[c].process (x[c])))))));
                x[c] += (y - x[c]) * eqm;                       // filters always run (stable state); the mix ramps on/off
            }

            // ---- compressor ----
            if (cmix > 0.0005f || grDb > 0.02f)
            {
                const float lvl = std::max (std::fabs (x[0]), std::fabs (x[1]));
                const float lvlDb = 20.0f * std::log10 (std::max (lvl, 1e-6f));
                const float over = lvlDb - p.thresh, knee = 6.0f;
                float grT;
                if (2.0f * over < -knee) grT = 0.0f;
                else if (2.0f * std::fabs (over) <= knee) { const float a = over + knee * 0.5f; grT = ratioK * a * a / (2.0f * knee); }
                else grT = ratioK * over;
                grDb = grT > grDb ? attC * grDb + (1.0f - attC) * grT : relC * grDb + (1.0f - relC) * grT;
                const float g = dbToLin (-grDb) * makeup;
                x[0] = x[0] * (1.0f - cmix) + x[0] * g * cmix;
                x[1] = x[1] * (1.0f - cmix) + x[1] * g * cmix;
                grMax = std::max (grMax, grDb);
            }

            // ---- chorus ----
            chPh += p.chRate / srf; if (chPh >= 1.0f) chPh -= 1.0f;
            for (int c = 0; c < 2; ++c)
            {
                const float ph = chPh + (c ? 0.27f : 0.0f);
                const float dl = (0.014f + p.chDepth * 0.006f * std::sin (kTwoPi * ph)) * srf;
                chD[c].push (x[c]);
                const float w = chD[c].tap (dl);
                x[c] = x[c] * (1.0f - 0.5f * chm) + w * chm * 0.95f;
            }

            // ---- delay / echo ----
            dSm += (dlTarget - dSm) * 0.0008f;
            const float tL = dlD[0].tap (dSm), tR = dlD[1].tap (dSm);
            fbLp[0] += dlToneC * (tL - fbLp[0]);
            fbLp[1] += dlToneC * (tR - fbLp[1]);
            const float fb = std::min (0.95f, p.dlFb);
            if (p.dlPing)
            {
                const float m = 0.5f * (x[0] + x[1]);
                dlD[0].push (m + fb * fbLp[1]);
                dlD[1].push (fb * fbLp[0]);
            }
            else
            {
                dlD[0].push (x[0] + fb * fbLp[0]);
                dlD[1].push (x[1] + fb * fbLp[1]);
            }
            x[0] += tL * dlm;
            x[1] += tR * dlm;

            // ---- reverb ----
            float wl, wr;
            rv.process (x[0], x[1], wl, wr);
            const float dryG = 1.0f - 0.45f * rvm, wetG = rvm * wetScale;
            x[0] = x[0] * dryG + wl * wetG;
            x[1] = x[1] * dryG + wr * wetG;

            // ---- stereo: mono-bass + width ----
            if (monoBassOn)
            {
                const float lowL = bassLp[0].process (x[0]), lowR = bassLp[1].process (x[1]);
                const float mono = 0.5f * (lowL + lowR);
                x[0] = x[0] - lowL + mono;
                x[1] = x[1] - lowR + mono;
            }
            if (std::fabs (wid - 1.0f) > 0.001f)
            {
                const float m = 0.5f * (x[0] + x[1]), s = 0.5f * (x[0] - x[1]) * wid;
                x[0] = m + s; x[1] = m - s;
            }

            // ---- ambience: rain, vinyl crackle, tape hiss ----
            if (rainL > 0.001f)
                for (int c = 0; c < 2; ++c) x[c] += rainL * 0.30f * rain[c].next (rng, p.rainDens, srf, aHP, aLP);
            if (vin > 0.001f)
            {
                if (rng.uni() < vin * 20.0f / srf)
                {
                    const float a = 0.04f + 0.6f * std::pow (rng.uni(), 3.0f);
                    pop[0] = a * (rng.bip() > 0 ? 1.0f : -1.0f);
                    pop[1] = pop[0] * (0.5f + 0.5f * rng.uni());
                }
                x[0] += vin * pop[0] * rng.bip() * 0.9f;
                x[1] += vin * pop[1] * rng.bip() * 0.9f;
                pop[0] *= 0.82f; pop[1] *= 0.82f;
            }
            if (his > 0.001f)
            {
                hissV[0] += 0.45f * (rng.bip() - hissV[0]);
                hissV[1] += 0.45f * (rng.bip() - hissV[1]);
                x[0] += his * hissV[0] * 0.012f;
                x[1] += his * hissV[1] * 0.012f;
            }

            // ---- master gain + limiter ----
            float l = x[0] * gain, r = x[1] * gain;
            if (! std::isfinite (l) || ! std::isfinite (r)) { l = r = 0.0f; reset(); first = false; }
            lim.process (l, r, ceil);
            L[i] = l; R[i] = r;
        }

        prev = t;
        grOut = grMax;
    }

private:
    struct Targets
    {
        float wobble = 0, flutter = 0, drive = 0, compMix = 0, chMix = 0, dlMix = 0, rvMix = 0,
              width = 1, rain = 0, vinyl = 0, hiss = 0, gain = 1, eq = 0;
    };
    Targets prev;
    double sr = 44100.0;
    bool first = true;
    al::Reverb rv;
    Limiter lim;
    al::DelayLine wobD[2], chD[2], dlD[2];
    BQ hp[2], ls[2], p1[2], p2[2], hs[2], lp1[2], lp2[2], bassLp[2];
    float fbLp[2] {}, held[2] {}, pop[2] {}, hissV[2] {};
    RainVoice rain[2];
    al::Rng rng;
    float grDb = 0, grOut = 0, holdPhase = 0, dSm = 0, wow = 0, fl = 0.3f, drift = 0, driftT = 0, chPh = 0;
    int driftCnt = 0;
    float aHP = 0.1f, aLP = 0.5f;
};

//==============================================================================
// Varispeed player with a windowed-sinc interpolator (anti-aliased when played faster than 1x)
class Player
{
public:
    void setPlaying (bool b) { target = b; if (b) ended = false; }
    bool isPlaying() const { return target; }
    bool isSilent() const { return gain <= 0.0f; }
    bool hasEnded() const { return ended; }
    void clearEnded() { ended = false; }
    void setPosition (double frames, bool fade = true) { pos = std::max (0.0, frames); if (fade) gain = 0.0f; }
    double position() const { return pos; }
    void setRegion (double startFrames, double endFrames) { rs = startFrames; re = endFrames; }
    void snapRate (double r) { prevRate = r; }

    // rateTarget: source frames consumed per output frame. Fade lengths are in source frames.
    void render (const al::Source* src, float* L, float* R, int n, double rateTarget, double outSr, bool loop,
                 double fadeInFr = 0.0, double fadeOutFr = 0.0)
    {
        if (src == nullptr || src->frames() < 64 || (! target && gain <= 0.0f))
        {
            std::fill (L, L + n, 0.0f);
            std::fill (R, R + n, 0.0f);
            prevRate = rateTarget;
            return;
        }

        const double N = (double) src->frames();
        double a = std::max (0.0, std::min (rs, N)), b = std::max (0.0, std::min (re, N));
        if (b <= a + 64.0) { a = 0.0; b = N; }
        const double edge = std::max (16.0, 0.003 * src->sampleRate);

        const double r0 = prevRate;
        const float gStep = (float) (1.0 / (0.006 * outSr));
        const double fc = rateTarget <= 1.0 ? 1.0 : 0.97 / rateTarget;
        const double delta = kPi * fc, cD = std::cos (delta), sD = std::sin (delta);
        static const std::array<float, 1025> win = makeWindow();

        for (int i = 0; i < n; ++i)
        {
            if (ended || (! target && gain <= 0.0f)) { L[i] = R[i] = 0.0f; continue; }
            if (pos < a) pos = a;
            if (pos >= b)
            {
                if (loop) pos = a + std::fmod (pos - a, b - a);
                else { ended = true; target = false; gain = 0.0f; L[i] = R[i] = 0.0f; continue; }
            }

            const double rate = r0 + (rateTarget - r0) * (double) (i + 1) / (double) n;
            const long long i0 = (long long) std::floor (pos);
            const double frac = pos - (double) i0;

            // weights: sinc(fc * x) * blackman, x = distance (in source samples) from pos
            float w[kTaps];
            double th = delta * ((double) (-kHalf + 1) - frac);
            double s = std::sin (th), c = std::cos (th), sum = 0.0;
            for (int t = 0; t < kTaps; ++t)
            {
                const double x = (double) (t - kHalf + 1) - frac;
                const double sinc = std::fabs (th) < 1e-9 ? 1.0 : s / th;
                const double ax = std::min (0.9999, std::fabs (x) / (double) kHalf);
                const double wv = (double) win[(size_t) (ax * 1024.0)] * sinc;
                w[t] = (float) wv; sum += wv;
                const double ns = s * cD + c * sD;
                c = c * cD - s * sD; s = ns;
                th += delta;
            }
            const float norm = (float) (1.0 / sum);

            float outL = 0.0f, outR = 0.0f;
            const long long first = i0 - kHalf + 1;
            if (first >= 0 && first + kTaps < (long long) src->frames())
            {
                const float* pl = &src->l[(size_t) first];
                const float* pr = &src->r[(size_t) first];
                for (int t = 0; t < kTaps; ++t) { outL += w[t] * pl[t]; outR += w[t] * pr[t]; }
            }
            else
            {
                for (int t = 0; t < kTaps; ++t)
                {
                    const long long k = first + t;
                    if (k < 0 || k >= (long long) src->frames()) continue;
                    outL += w[t] * src->l[(size_t) k]; outR += w[t] * src->r[(size_t) k];
                }
            }
            outL *= norm; outR *= norm;

            gain += target ? gStep : -gStep;
            gain = std::min (1.0f, std::max (0.0f, gain));
            double g = (double) gain;
            g *= std::min (1.0, std::min ((pos - a) / edge, (b - pos) / edge));
            if (! loop)
            {
                if (fadeInFr > 1.0)  g *= std::min (1.0, (pos - a) / fadeInFr);
                if (fadeOutFr > 1.0) g *= std::min (1.0, (b - pos) / fadeOutFr);
            }
            L[i] = outL * (float) g; R[i] = outR * (float) g;
            pos += rate;
        }
        prevRate = rateTarget;
    }

private:
    static constexpr int kHalf = 12, kTaps = 24;
    static std::array<float, 1025> makeWindow()
    {
        std::array<float, 1025> w {};
        for (size_t i = 0; i < w.size(); ++i)
        {
            const double t = (double) i / 1024.0;   // 0..1 across the half-width
            w[i] = (float) (0.42 + 0.5 * std::cos (3.14159265358979 * t) + 0.08 * std::cos (2 * 3.14159265358979 * t));
        }
        return w;
    }

    double pos = 0.0, prevRate = 1.0, rs = 0.0, re = 0.0;
    float gain = 0.0f;
    bool target = false, ended = false;
};

//==============================================================================
// Offline render (export). `src` must already be time-stretched if the tempo mode requires it.
// playRate: playback-rate multiplier of the song (speed in linked mode, pitch ratio otherwise).
inline bool renderMix (const al::Source& src, double playRate, const MixParams& mp,
                       double regionStartSec, double regionEndSec, double fadeInSec, double fadeOutSec,
                       double outSr, std::vector<float>& oL, std::vector<float>& oR,
                       const std::function<bool (float)>& progress)
{
    Chain chain;  chain.prepare (outSr);
    Player pl;
    const double rate = playRate * src.sampleRate / outSr;               // source frames per output frame
    const double toFr = src.sampleRate * src.stretch;
    double a = regionStartSec * toFr, b = regionEndSec * toFr;
    if (regionEndSec <= regionStartSec + 0.01) { a = 0.0; b = (double) src.frames(); }
    pl.setRegion (a, b);
    pl.setPosition (a, false);
    pl.snapRate (rate);
    pl.setPlaying (true);

    const int B = 1024;
    std::vector<float> L ((size_t) B), R ((size_t) B);
    const double totalOut = (b - a) / rate;
    double tail = 0.6;
    if (mp.rvOn && ! mp.bypass) tail = std::max (tail, (double) mp.rvDecay * 1.3 + mp.rvPre * 0.001);
    if (mp.dlOn && ! mp.bypass && mp.dlMix > 0.01f) tail = std::max (tail, 7.0);
    tail = std::min (tail, 30.0);

    oL.clear(); oR.clear();
    oL.reserve ((size_t) (totalOut + tail * outSr + 4096)); oR.reserve (oL.capacity());
    double done = 0.0, afterEnd = 0.0;
    int quiet = 0;

    while (true)
    {
        pl.render (&src, L.data(), R.data(), B, rate, outSr, false, fadeInSec * outSr * rate, fadeOutSec * outSr * rate);
        chain.process (L.data(), R.data(), B, mp);
        oL.insert (oL.end(), L.begin(), L.end());
        oR.insert (oR.end(), R.begin(), R.end());
        done += B;

        if (pl.hasEnded() || ! pl.isPlaying())
        {
            afterEnd += B;
            float pk = 0;
            for (int i = 0; i < B; ++i) pk = std::max (pk, std::max (std::fabs (L[(size_t) i]), std::fabs (R[(size_t) i])));
            const bool noisy = mp.rain > 0.001f || mp.vinyl > 0.001f || mp.hiss > 0.001f;
            quiet = (pk < 3e-5f && ! noisy) ? quiet + B : 0;
            if (afterEnd > tail * outSr || (quiet > (int) (0.35 * outSr) && afterEnd > 0.4 * outSr)) break;
        }
        if (! progress ((float) std::min (1.0, done / (totalOut + tail * outSr * 0.5)))) return false;
    }

    // drop the start-up latency of the chain so the song starts exactly at 0:00
    const size_t lat = std::min ((size_t) chain.latency(), oL.size());
    oL.erase (oL.begin(), oL.begin() + (std::ptrdiff_t) lat);
    oR.erase (oR.begin(), oR.begin() + (std::ptrdiff_t) lat);

    // trim the silent run-out after the song / effect tails (keep a short safety margin)
    size_t last = 0;
    for (size_t i = oL.size(); i-- > 0;)
        if (std::fabs (oL[i]) > 1e-4f || std::fabs (oR[i]) > 1e-4f) { last = i; break; }
    const size_t keep = std::min (oL.size(), std::max ((size_t) totalOut, last + (size_t) (0.05 * outSr)));
    oL.resize (keep); oR.resize (keep);
    return true;
}
} // namespace al
