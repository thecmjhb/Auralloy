#include "../src/dsp/Stems.h"
#include "../src/dsp/TimePitch.h"
#include <chrono>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); ++failures; } else { std::printf ("ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static double goertzelPower (const std::vector<float>& x, double hz, double sr)
{
    double s1 = 0, s2 = 0; const double c = 2 * std::cos (2 * 3.14159265358979 * hz / sr);
    for (float v : x) { const double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / ((double) x.size() * (double) x.size());
}
static double corr (const std::vector<float>& a, const std::vector<float>& b)
{
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < a.size(); ++i) { sab += (double) a[i] * b[i]; saa += (double) a[i] * a[i]; sbb += (double) b[i] * b[i]; }
    return sab / std::sqrt (saa * sbb + 1e-20);
}

int main()
{
    setvbuf (stdout, nullptr, _IONBF, 0);
    const double sr = 44100.0; const size_t n = (size_t) (8 * sr);
    al::Source in; in.sampleRate = sr; in.l.assign (n, 0.0f); in.r.assign (n, 0.0f);
    std::vector<float> truthVocal (n), truthPad (n), truthDrums (n), truthBass (n);
    al::Rng rng; const double TP = 2 * 3.14159265358979;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        // vocal-like melody: 440 Hz with harmonics and vibrato, centred
        const double note = std::fmod (t, 1.0) < 0.8 ? 1.0 : 0.0;
        const double ph = TP * 440 * t + 0.4 * std::sin (TP * 5.5 * t);
        truthVocal[i] = (float) ((std::sin (ph) + 0.5 * std::sin (2 * ph) + 0.3 * std::sin (3 * ph)) * 0.18 * note);
        // stereo pad hard left
        truthPad[i] = (float) ((std::sin (TP * 330 * t) + 0.4 * std::sin (TP * 660 * t)) * 0.16);
        // kick + hats, centred
        const double kt = std::fmod (t, 0.5), ht = std::fmod (t, 0.25);
        truthDrums[i] = (float) (std::sin (TP * (50 + 120 * std::exp (-kt * 40)) * kt) * std::exp (-kt * 16) * 0.6 + rng.bip() * std::exp (-ht * 90) * 0.2);
        truthBass[i] = (float) (std::sin (TP * 55 * t) * 0.25);
        in.l[i] = truthVocal[i] + truthPad[i] + truthDrums[i] + truthBass[i];
        in.r[i] = truthVocal[i] + 0.0f * truthPad[i] + truthDrums[i] + truthBass[i];
    }

    auto t0 = std::chrono::steady_clock::now();
    auto st = al::separateStems (in);
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("      separated %.0f s of audio in %.2f s (%.1fx realtime)\n", (double) n / sr, sec, (double) n / sr / sec);
    CHECK (st.ok(), "stems produced");

    // reconstruction
    double err = 0, ref = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double sum = st.vocals->l[i] + st.drums->l[i] + st.bass->l[i] + st.other->l[i];
        err += (sum - in.l[i]) * (sum - in.l[i]); ref += (double) in.l[i] * in.l[i];
    }
    CHECK (10 * std::log10 (err / ref + 1e-20) < -80.0, "vocals + drums + bass + other == input (error %.0f dB)", 10 * std::log10 (err / ref + 1e-20));

    const double v440 = goertzelPower (st.vocals->l, 440, sr), v330 = goertzelPower (st.vocals->l, 330, sr);
    const double o440 = goertzelPower (st.other->l, 440, sr), o330 = goertzelPower (st.other->l, 330, sr);
    std::printf ("      vocal stem: 440 Hz %.2e vs 330 Hz(pad) %.2e | other stem: 440 Hz %.2e, 330 Hz(pad) %.2e\n", v440, v330, o440, o330);
    CHECK (v440 > 8.0 * v330, "centred melody lands in the vocal stem, the hard-left pad does not (%.1f dB)", 10 * std::log10 (v440 / (v330 + 1e-30)));
    CHECK (o330 > 5.0 * o440, "the pad stays in 'other' (%.1f dB)", 10 * std::log10 (o330 / (o440 + 1e-30)));

    const double cd = corr (st.drums->l, truthDrums), cb = corr (st.bass->l, truthBass);
    std::printf ("      correlation: drums %.2f, bass %.2f\n", cd, cb);
    CHECK (cd > 0.6, "drum stem follows the true kick+hats (r = %.2f)", cd);
    CHECK (cb > 0.7, "bass stem follows the true 55 Hz bass (r = %.2f)", cb);

    // time/pitch transform sanity (sine 440 -> +2 semitones at 1.1x tempo)
    al::Source sine; sine.sampleRate = sr; sine.l.resize ((size_t) (6 * sr)); sine.r = sine.l;
    for (size_t i = 0; i < sine.l.size(); ++i) sine.l[i] = sine.r[i] = 0.4f * (float) std::sin (TP * 440 * (double) i / sr);
    auto tp = al::timePitch (sine, 1.1, 2.0);
    int cross = 0; double first = -1, last = -1;
    for (size_t i = tp->l.size() / 4 + 1; i < tp->l.size() * 3 / 4; ++i)
        if (tp->l[i - 1] < 0 && tp->l[i] >= 0) { const double tt = (double) (i - 1) + (double) (-tp->l[i - 1]) / (double) (tp->l[i] - tp->l[i - 1]); if (first < 0) first = tt; last = tt; ++cross; }
    const double hz = (cross - 1) * sr / (last - first);
    std::printf ("      timePitch(1.1x, +2 st): %.1f Hz (expected %.1f), %.2f s (expected %.2f)\n", hz, 440 * std::pow (2.0, 2.0 / 12.0), tp->l.size() / sr, 6.0 / 1.1);
    CHECK (std::abs (hz - 440 * std::pow (2.0, 2.0 / 12.0)) < 3.0 && std::abs (tp->l.size() / sr - 6.0 / 1.1) < 0.15, "time+pitch transform is independent");

    std::printf ("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL STEM TESTS PASSED", failures);
    return failures;
}
