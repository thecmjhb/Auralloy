#include "../src/dsp/Analysis.h"
#include "SynthSong.h"
#include <chrono>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); ++failures; } else { std::printf ("ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

int main()
{
    setvbuf (stdout, nullptr, _IONBF, 0);
    struct Case { const char* name; testsong::Spec sp; };
    std::vector<Case> cases;
    { testsong::Spec s; s.bpm = 100; s.root = 9; s.minor = true; s.lead = 0.37; cases.push_back ({ "A minor 100", s }); }
    { testsong::Spec s; s.bpm = 128; s.root = 0; s.minor = false; s.lead = 0.21; s.seed = 2; cases.push_back ({ "C major 128", s }); }
    { testsong::Spec s; s.bpm = 87; s.root = 6; s.minor = true; s.lead = 0.52; s.seed = 3; cases.push_back ({ "F# minor 87", s }); }
    { testsong::Spec s; s.bpm = 140; s.root = 3; s.minor = false; s.lead = 0.1; s.seed = 4; cases.push_back ({ "Eb major 140", s }); }
    { testsong::Spec s; s.bpm = 76; s.root = 2; s.minor = true; s.lead = 0.3; s.seed = 5; s.vocal = true; cases.push_back ({ "D minor 76 +vocal", s }); }

    for (auto& c : cases)
    {
        auto song = testsong::make (c.sp);
        auto t0 = std::chrono::steady_clock::now();
        al::Analysis a = al::analyze (song);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("--- %s  (%.0f s of audio, analysed in %.0f ms)\n", c.name, song.seconds(), ms);
        CHECK (a.ok, "analysis succeeded");
        if (! a.ok) continue;
        std::printf ("      bpm %.2f (conf %.2f), key %s / %s (conf %.2f), %zu beats, %zu bars, %zu sections\n",
                     a.bpm, a.bpmConfidence, a.keyName.c_str(), a.camelot.c_str(), a.keyConfidence, a.beats.size(), a.downbeats.size(), a.sections.size());
        CHECK (std::abs (a.bpm - c.sp.bpm) < 0.35, "tempo %.2f vs %.2f", a.bpm, c.sp.bpm);
        CHECK (a.keyRoot == c.sp.root && a.keyMinor == c.sp.minor, "key %s vs %s", a.keyName.c_str(), al::keyNameOf (c.sp.root, c.sp.minor).c_str());
        const double bar = 240.0 / c.sp.bpm;
        double ph = std::fmod (a.downbeats[0] - c.sp.lead, bar); if (ph < 0) ph += bar; if (ph > bar / 2) ph -= bar;
        std::printf ("      first downbeat at %.3f s (truth %.3f), phase error %.0f ms\n", a.downbeats[0], c.sp.lead, ph * 1000);
        CHECK (std::abs (ph) < 0.045, "downbeat grid aligned with the music (error %.0f ms)", ph * 1000);
        std::printf ("      sections:");
        for (auto& s : a.sections) std::printf ("  [%d-%d %s e=%.2f]", s.bar0, s.bar1, s.label.c_str(), s.energy);
        std::printf ("\n");
        int found = 0;
        for (int truth : { 8, 24, 40 })
            for (auto& s : a.sections) if (std::abs (s.bar0 - truth) <= 1) { ++found; break; }
        CHECK (found >= 2, "section boundaries near bars 8/24/40 (%d of 3 found)", found);
    }
    std::printf ("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL ANALYSIS TESTS PASSED", failures);
    return failures;
}
