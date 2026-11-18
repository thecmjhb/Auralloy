// Auralloy - parameter table (single source of truth for the engine, the UI and presets)
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "../dsp/Engine.h"

namespace al
{
enum class Fmt { Speed, Semi, Pct, Db, Hz, Ms, Sec, Bits, Ratio, Width, MonoBass, Rate };

struct PDef
{
    const char* id;
    const char* name;
    float mn, mx, def;
    float centre;     // 0 = linear, otherwise the value that sits in the middle of the knob
    Fmt fmt;
    bool bipolar;     // draw the arc from the centre (EQ gains, pitch)
};

struct BDef { const char* id; const char* name; bool def; };

inline const std::vector<PDef>& floatParams()
{
    static const std::vector<PDef> p = {
        { "speed",    "Speed",       0.25f, 2.0f,    1.0f,   1.0f,  Fmt::Speed, false },

        { "hpf",      "High-pass",   20.f,  600.f,   20.f,   120.f, Fmt::Hz,    false },
        { "lowG",     "Low",         -12.f, 12.f,    0.0f,   0.0f,  Fmt::Db,    true  },
        { "lmG",      "Low-mid",     -12.f, 12.f,    0.0f,   0.0f,  Fmt::Db,    true  },
        { "hmG",      "High-mid",    -12.f, 12.f,    0.0f,   0.0f,  Fmt::Db,    true  },
        { "highG",    "High",        -12.f, 12.f,    0.0f,   0.0f,  Fmt::Db,    true  },
        { "lpf",      "Low-pass",    1000.f, 20000.f, 20000.f, 6000.f, Fmt::Hz,  false },

        { "bits",     "Bit depth",   4.f,   16.f,    16.f,   0.0f,  Fmt::Bits,  false },
        { "srate",    "Sample rate", 2000.f, 48000.f, 48000.f, 14000.f, Fmt::Hz, false },
        { "wobble",   "Wobble",      0.f,   1.f,     0.0f,   0.0f,  Fmt::Pct,   false },
        { "flutter",  "Flutter",     0.f,   1.f,     0.0f,   0.0f,  Fmt::Pct,   false },
        { "drive",    "Warmth",      0.f,   1.f,     0.0f,   0.0f,  Fmt::Pct,   false },

        { "thresh",   "Threshold",   -48.f, 0.f,     -18.f,  0.0f,  Fmt::Db,    false },
        { "ratio",    "Ratio",       1.f,   20.f,    3.0f,   4.0f,  Fmt::Ratio, false },
        { "attack",   "Attack",      1.f,   100.f,   15.f,   15.f,  Fmt::Ms,    false },
        { "release",  "Release",     20.f,  1000.f,  150.f,  200.f, Fmt::Ms,    false },
        { "makeup",   "Makeup",      0.f,   18.f,    0.0f,   0.0f,  Fmt::Db,    false },
        { "compMix",  "Mix",         0.f,   1.f,     1.0f,   0.0f,  Fmt::Pct,   false },

        { "chRate",   "Rate",        0.1f,  4.f,     0.6f,   0.0f,  Fmt::Rate,  false },
        { "chDepth",  "Depth",       0.f,   1.f,     0.4f,   0.0f,  Fmt::Pct,   false },
        { "chMix",    "Mix",         0.f,   1.f,     0.0f,   0.0f,  Fmt::Pct,   false },

        { "dlTime",   "Time",        20.f,  1500.f,  380.f,  300.f, Fmt::Ms,    false },
        { "dlFb",     "Feedback",    0.f,   0.95f,   0.35f,  0.0f,  Fmt::Pct,   false },
        { "dlTone",   "Tone",        500.f, 12000.f, 4000.f, 3000.f, Fmt::Hz,   false },
        { "dlMix",    "Mix",         0.f,   1.f,     0.0f,   0.0f,  Fmt::Pct,   false },

        { "rvMix",    "Mix",         0.f,   1.f,     0.0f,   0.0f,  Fmt::Pct,   false },
        { "rvDecay",  "Decay",       0.3f,  15.f,    4.0f,   3.0f,  Fmt::Sec,   false },
        { "rvDamp",   "Damping",     0.f,   1.f,     0.5f,   0.0f,  Fmt::Pct,   false },
        { "rvPre",    "Pre-delay",   0.f,   200.f,   25.f,   0.0f,  Fmt::Ms,    false },
        { "rvLow",    "Low cut",     20.f,  600.f,   110.f,  120.f, Fmt::Hz,    false },

        { "width",    "Width",       0.f,   2.f,     1.0f,   1.0f,  Fmt::Width, false },
        { "monoBass", "Mono bass",   0.f,   300.f,   0.0f,   0.0f,  Fmt::MonoBass, false },

        { "rain",     "Rain",        0.f,   1.f,     0.0f,   0.0f,  Fmt::Pct,   false },
        { "rainDens", "Intensity",   0.f,   1.f,     0.5f,   0.0f,  Fmt::Pct,   false },
        { "vinyl",    "Vinyl",       0.f,   1.f,     0.0f,   0.0f,  Fmt::Pct,   false },
        { "hiss",     "Tape hiss",   0.f,   1.f,     0.0f,   0.0f,  Fmt::Pct,   false },

        { "gain",     "Gain",        -24.f, 12.f,    0.0f,   0.0f,  Fmt::Db,    false },
        { "loudness", "Loudness",    0.f,   12.f,    0.0f,   0.0f,  Fmt::Db,    false },
        { "ceiling",  "Ceiling",     -6.f,  0.f,     -0.3f,  0.0f,  Fmt::Db,    false },
        { "fadeIn",   "Fade in",     0.f,   10.f,    0.0f,   0.0f,  Fmt::Sec,   false },
        { "fadeOut",  "Fade out",    0.f,   15.f,    0.0f,   0.0f,  Fmt::Sec,   false },

        { "vol",      "Volume",      0.f,   1.2f,    0.9f,   0.0f,  Fmt::Pct,   false },
    };
    return p;
}

inline const std::vector<BDef>& boolParams()
{
    static const std::vector<BDef> p = {
        { "eqOn", "EQ", false }, { "lofiOn", "Lofi", false }, { "compOn", "Compressor", false },
        { "chOn", "Chorus", false }, { "dlOn", "Delay", false }, { "dlPing", "Ping-pong", true }, { "rvOn", "Reverb", false },
        { "loop", "Loop", false }, { "bypass", "FX bypass", false },
    };
    return p;
}

// parameters that presets leave alone
inline bool isTransportParam (const juce::String& id)
{
    return id == "vol" || id == "loop" || id == "bypass";
}


// indices into floatParams() / boolParams(): keep in the same order as the tables above
enum FIdx { F_speed, F_hpf, F_lowG, F_lmG, F_hmG, F_highG, F_lpf, F_bits, F_srate, F_wobble, F_flutter, F_drive,
            F_thresh, F_ratio, F_attack, F_release, F_makeup, F_compMix, F_chRate, F_chDepth, F_chMix,
            F_dlTime, F_dlFb, F_dlTone, F_dlMix, F_rvMix, F_rvDecay, F_rvDamp, F_rvPre, F_rvLow,
            F_width, F_monoBass, F_rain, F_rainDens, F_vinyl, F_hiss,
            F_gain, F_loudness, F_ceiling, F_fadeIn, F_fadeOut, F_vol, F_COUNT };
enum BIdx { B_eqOn, B_lofiOn, B_compOn, B_chOn, B_dlOn, B_dlPing, B_rvOn, B_loop, B_bypass, B_COUNT };

struct Snapshot
{
    float f[F_COUNT] {};
    bool b[B_COUNT] {};
};

inline MixParams toMix (const Snapshot& s)
{
    MixParams m;
    m.bypass = s.b[B_bypass];
    m.eqOn = s.b[B_eqOn]; m.hpf = s.f[F_hpf]; m.lowG = s.f[F_lowG]; m.lmG = s.f[F_lmG]; m.hmG = s.f[F_hmG]; m.highG = s.f[F_highG]; m.lpf = s.f[F_lpf];
    m.lofiOn = s.b[B_lofiOn]; m.bits = s.f[F_bits]; m.srate = s.f[F_srate]; m.wobble = s.f[F_wobble]; m.flutter = s.f[F_flutter]; m.drive = s.f[F_drive];
    m.compOn = s.b[B_compOn]; m.thresh = s.f[F_thresh]; m.ratio = s.f[F_ratio]; m.attack = s.f[F_attack]; m.release = s.f[F_release];
    m.makeup = s.f[F_makeup]; m.compMix = s.f[F_compMix];
    m.chOn = s.b[B_chOn]; m.chRate = s.f[F_chRate]; m.chDepth = s.f[F_chDepth]; m.chMix = s.f[F_chMix];
    m.dlOn = s.b[B_dlOn]; m.dlPing = s.b[B_dlPing]; m.dlTime = s.f[F_dlTime]; m.dlFb = s.f[F_dlFb]; m.dlTone = s.f[F_dlTone]; m.dlMix = s.f[F_dlMix];
    m.rvOn = s.b[B_rvOn]; m.rvMix = s.f[F_rvMix]; m.rvDecay = s.f[F_rvDecay]; m.rvDamp = s.f[F_rvDamp]; m.rvPre = s.f[F_rvPre]; m.rvLow = s.f[F_rvLow];
    m.width = s.f[F_width]; m.monoBass = s.f[F_monoBass];
    m.rain = s.f[F_rain]; m.rainDens = s.f[F_rainDens]; m.vinyl = s.f[F_vinyl]; m.hiss = s.f[F_hiss];
    m.gainDb = s.f[F_gain]; m.loudness = s.f[F_loudness]; m.ceiling = s.f[F_ceiling];
    return m;
}

//==============================================================================
struct PresetDef
{
    const char* name;
    std::vector<std::pair<const char*, float>> v;   // everything not listed returns to its default
};

inline const std::vector<PresetDef>& factoryPresets()
{
    static const std::vector<PresetDef> p = {
        { "Clean (reset)", {} },
        { "Slowed + Reverb", { { "speed", 0.85f }, { "rvOn", 1 }, { "rvMix", 0.38f }, { "rvDecay", 5.0f }, { "rvDamp", 0.45f }, { "rvPre", 35.f } } },
        { "Lofi Chill", { { "speed", 0.93f },
              { "eqOn", 1 }, { "hpf", 60.f }, { "lowG", 2.f }, { "highG", -2.f }, { "lpf", 6500.f },
              { "lofiOn", 1 }, { "bits", 12.f }, { "srate", 22050.f }, { "wobble", 0.35f }, { "flutter", 0.2f }, { "drive", 0.25f },
              { "compOn", 1 }, { "thresh", -20.f }, { "ratio", 2.5f }, { "makeup", 2.f },
              { "chOn", 1 }, { "chMix", 0.12f },
              { "rvOn", 1 }, { "rvMix", 0.22f }, { "rvDecay", 2.4f }, { "rvDamp", 0.6f },
              { "vinyl", 0.25f }, { "hiss", 0.15f } } },
        { "Rainy Lofi", { { "speed", 0.9f },
              { "eqOn", 1 }, { "hpf", 70.f }, { "lowG", 2.f }, { "highG", -3.f }, { "lpf", 5500.f },
              { "lofiOn", 1 }, { "bits", 13.f }, { "srate", 24000.f }, { "wobble", 0.4f }, { "flutter", 0.2f }, { "drive", 0.3f },
              { "compOn", 1 }, { "thresh", -22.f }, { "ratio", 3.f }, { "makeup", 2.f },
              { "rvOn", 1 }, { "rvMix", 0.3f }, { "rvDecay", 3.2f }, { "rvDamp", 0.6f },
              { "rain", 0.55f }, { "rainDens", 0.55f }, { "vinyl", 0.18f } } },
        { "Nightcore", { { "speed", 1.3f }, { "rvOn", 1 }, { "rvMix", 0.1f }, { "rvDecay", 2.0f }, { "rvPre", 10.f } } },
        { "Vaporwave", { { "speed", 0.72f },
              { "eqOn", 1 }, { "lpf", 9000.f }, { "highG", -2.f },
              { "lofiOn", 1 }, { "wobble", 0.3f }, { "flutter", 0.1f },
              { "chOn", 1 }, { "chRate", 0.35f }, { "chDepth", 0.7f }, { "chMix", 0.4f },
              { "rvOn", 1 }, { "rvMix", 0.5f }, { "rvDecay", 7.0f }, { "rvDamp", 0.55f }, { "rvPre", 50.f } } },
        { "Dreamy Slow", { { "speed", 0.78f },
              { "chOn", 1 }, { "chMix", 0.25f }, { "chDepth", 0.5f },
              { "dlOn", 1 }, { "dlTime", 480.f }, { "dlFb", 0.4f }, { "dlTone", 3500.f }, { "dlMix", 0.22f },
              { "rvOn", 1 }, { "rvMix", 0.55f }, { "rvDecay", 8.0f }, { "rvDamp", 0.55f }, { "rvPre", 60.f } } },
        { "Tape Cassette", { { "eqOn", 1 }, { "hpf", 80.f }, { "lpf", 7000.f }, { "lowG", 1.5f },
              { "lofiOn", 1 }, { "bits", 14.f }, { "srate", 32000.f }, { "wobble", 0.6f }, { "flutter", 0.4f }, { "drive", 0.5f },
              { "compOn", 1 }, { "thresh", -16.f }, { "ratio", 3.f }, { "makeup", 2.f },
              { "hiss", 0.4f } } },
        { "Bass Boosted", { { "eqOn", 1 }, { "lowG", 8.f }, { "lmG", 2.f }, { "hpf", 25.f },
              { "compOn", 1 }, { "thresh", -18.f }, { "ratio", 3.5f }, { "makeup", 2.f },
              { "monoBass", 120.f }, { "width", 1.1f }, { "loudness", 3.f } } },
        { "Mashup Glue", { { "compOn", 1 }, { "thresh", -16.f }, { "ratio", 2.2f }, { "attack", 25.f }, { "release", 180.f }, { "makeup", 1.5f }, { "compMix", 0.8f },
              { "eqOn", 1 }, { "hpf", 28.f }, { "highG", 1.0f }, { "width", 1.12f }, { "monoBass", 110.f },
              { "rvOn", 1 }, { "rvMix", 0.08f }, { "rvDecay", 1.6f }, { "loudness", 2.0f } } },
        { "Wide & Warm", { { "eqOn", 1 }, { "lowG", 2.f }, { "highG", -1.5f }, { "lpf", 12000.f },
              { "lofiOn", 1 }, { "drive", 0.35f },
              { "chOn", 1 }, { "chMix", 0.2f }, { "width", 1.4f }, { "monoBass", 100.f },
              { "rvOn", 1 }, { "rvMix", 0.2f }, { "rvDecay", 2.5f } } },
    };
    return p;
}
} // namespace al
