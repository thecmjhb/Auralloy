#include "../src/mashup/Mashup.h"
#include "SynthSong.h"
#include <chrono>
#include <set>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); ++failures; } else { std::printf ("ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static al::SongInfo makeSong (int id, const char* name, testsong::Spec sp)
{
    al::SongInfo s; s.id = id; s.name = name;
    auto src = std::make_shared<al::Source> (testsong::make (sp));
    s.mix = src;
    s.an = al::analyze (*src);
    s.stems = al::separateStems (*src);
    al::prepareSong (s);
    std::printf ("      %-22s %.1f BPM  %s (%s)  %d bars, %zu sections, vocals in %.0f%% of bars\n", name, s.an.bpm, s.an.keyName.c_str(), s.an.camelot.c_str(),
                 s.an.numBars(), s.an.sections.size(), s.vocalFrac * 100);
    return s;
}

int main()
{
    setvbuf (stdout, nullptr, _IONBF, 0);
    std::vector<al::SongInfo> songs;
    { testsong::Spec s; s.bpm = 100; s.root = 9; s.minor = true; s.vocal = true; s.seed = 1; songs.push_back (makeSong (1, "Song A (A minor 100)", s)); }
    { testsong::Spec s; s.bpm = 106; s.root = 0; s.minor = false; s.vocal = false; s.lead = 0.2; s.seed = 2; songs.push_back (makeSong (2, "Song B (C major 106)", s)); }
    { testsong::Spec s; s.bpm = 96; s.root = 2; s.minor = true; s.vocal = true; s.lead = 0.45; s.seed = 3; songs.push_back (makeSong (3, "Song C (D minor 96)", s)); }
    { testsong::Spec s; s.bpm = 140; s.root = 6; s.minor = false; s.vocal = true; s.seed = 4; songs.push_back (makeSong (4, "Song D (F# major 140)", s)); }

    al::MashupOptions opt; opt.targetLengthSec = 150;
    auto plan = al::planMashup (songs, opt);
    std::printf ("\n--- plan log ---\n");
    for (auto& l : plan.log) std::printf ("  %s\n", l.c_str());
    std::printf ("---\n");
    CHECK (plan.ok, "plan built (%s)", plan.error.c_str());
    if (! plan.ok) return 1;

    CHECK (plan.usedSongs.size() == 3, "3 compatible songs used, the 140 BPM song left out (%zu used)", plan.usedSongs.size());
    bool tempoOk = true, keyOk = true;
    for (auto& kv : plan.perSong) { if (std::abs (kv.second.first - 1.0) > 0.15 + 1e-9) tempoOk = false; if (std::abs (kv.second.second) > 3) keyOk = false; }
    CHECK (tempoOk, "rule 1: every song is stretched by at most 15%%");
    CHECK (keyOk, "rule 2: every song is shifted by at most 3 semitones");

    // grid alignment
    bool grid = true;
    for (auto& c : plan.clips)
    {
        const double b0 = c.tStart / plan.barSec, b1 = (c.tStart + c.length) / plan.barSec;
        if (std::abs (b0 - std::round (b0)) > 1e-6 || std::abs (b1 - std::round (b1)) > 1e-6) grid = false;
    }
    CHECK (grid, "rule 3: all clip edges are on the bar grid");

    // vocals / bass never overlap
    auto overlapCount = [&] (int role)
    {
        int c = 0;
        for (size_t i = 0; i < plan.clips.size(); ++i) for (size_t j = i + 1; j < plan.clips.size(); ++j)
        {
            const auto& a = plan.clips[i]; const auto& b = plan.clips[j];
            if (a.role != role || b.role != role) continue;
            if (a.tStart < b.tStart + b.length - 1e-6 && b.tStart < a.tStart + a.length - 1e-6) ++c;
        }
        return c;
    };
    CHECK (overlapCount (al::RoleVocals) == 0, "rule 4: vocal clips never overlap");
    CHECK (overlapCount (al::RoleBass) == 0, "rule 5: only one bass plays at a time");
    CHECK (plan.lengthSec > 100 && plan.lengthSec < 200, "length close to the 150 s target (%.0f s)", plan.lengthSec);
    int vocalClips = 0; for (auto& c : plan.clips) if (c.role == al::RoleVocals) ++vocalClips;
    CHECK (vocalClips >= 2, "several vocal movements (%d)", vocalClips);

    // ---- sequential mix (default): songs play one after another ----
    {
        int foreign = 0;
        for (auto& v : plan.clips)
        {
            if (v.role != al::RoleVocals) continue;
            bool same = false;
            for (auto& d : plan.clips)
                if (d.role == al::RoleDrums && d.songId == v.songId && d.tStart < v.tStart + v.length && v.tStart < d.tStart + d.length) same = true;
            if (! same) ++foreign;
        }
        CHECK (foreign == 0, "sequential: every vocal belongs to the song whose drums are playing (%d foreign)", foreign);
        int maxSongs = 0;
        for (double tt = 0.0; tt < plan.lengthSec; tt += 0.25)
        {
            std::set<int> ids;
            for (auto& c : plan.clips) if (tt >= c.tStart && tt < c.tStart + c.length) ids.insert (c.songId);
            maxSongs = std::max (maxSongs, (int) ids.size());
        }
        CHECK (maxSongs <= 2, "sequential: never more than two songs at once, only inside transitions (max %d)", maxSongs);
        std::set<int> starts;
        for (auto& c : plan.clips) if (c.role == al::RoleDrums) starts.insert (c.songId);
        CHECK (starts.size() >= 3, "sequential: all three songs get a turn (%zu)", starts.size());
    }
    // ---- layered mode: vocals of one song over the beat of another ----
    {
        al::MashupOptions lo = opt; lo.mode = al::MashupOptions::VocalOverBeat;
        auto lp = al::planMashup (songs, lo);
        int foreign = 0;
        for (auto& v : lp.clips)
        {
            if (v.role != al::RoleVocals) continue;
            for (auto& d : lp.clips)
                if (d.role == al::RoleDrums && d.songId != v.songId && d.tStart < v.tStart + v.length && v.tStart < d.tStart + d.length) { ++foreign; break; }
        }
        CHECK (lp.ok && foreign > 0, "layered: vocals sit over another song's beat (%d such vocal clips)", foreign);
    }

    // ---- render the plan and analyse the result: it should really sit at the target tempo ----
    auto t0 = std::chrono::steady_clock::now();
    const double sr = 44100.0;
    std::vector<float> L ((size_t) (plan.lengthSec * sr) + 44100, 0.0f), R (L.size(), 0.0f);
    std::map<int, const al::SongInfo*> byId; for (auto& s : songs) byId[s.id] = &s;
    for (auto& c : plan.clips)
    {
        auto buf = al::renderClipBuffer (*byId[c.songId], c);
        if (! buf) { CHECK (false, "clip rendered"); continue; }
        const long long start = (long long) std::llround (c.tStart * sr);
        const long long n = (long long) buf->frames();
        for (long long i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            double g = std::pow (10.0, c.gainDb / 20.0);
            if (c.fadeIn > 0 && t < c.fadeIn) g *= std::sin (1.5707963 * t / c.fadeIn);
            const double tl = (double) (n - i) / sr;
            if (c.fadeOut > 0 && tl < c.fadeOut) g *= std::sin (1.5707963 * tl / c.fadeOut);
            if ((size_t) (start + i) < L.size()) { L[(size_t) (start + i)] += (float) (g * buf->l[(size_t) i]); R[(size_t) (start + i)] += (float) (g * buf->r[(size_t) i]); }
        }
    }
    std::printf ("      rendered %.0f s mashup in %.1f s\n", plan.lengthSec, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
    al::Source mash; mash.sampleRate = sr; mash.l = L; mash.r = R;
    float pk = 0; double e = 0; for (size_t i = 0; i < L.size(); ++i) { pk = std::max (pk, std::max (std::fabs (L[i]), std::fabs (R[i]))); e += (double) L[i] * L[i]; }
    std::printf ("      mashup peak %.2f, rms %.1f dB\n", pk, 10 * std::log10 (e / L.size() + 1e-12));
    CHECK (pk > 0.2 && pk < 6.0 && e > 1.0, "rendered mashup has sane level");
    auto an = al::analyze (mash);
    std::printf ("      analysis of the mashup: %.2f BPM, %s\n", an.bpm, an.keyName.c_str());
    CHECK (an.ok && std::abs (an.bpm - plan.bpm) < 0.6, "the finished mashup measures %.2f BPM (target %.2f)", an.bpm, plan.bpm);
    const int tonic = an.keyMinor ? (an.keyRoot + 3) % 12 : an.keyRoot;
    const int target = plan.keyMinor ? (plan.keyRoot + 3) % 12 : plan.keyRoot;
    CHECK (tonic == target, "the finished mashup is in the planned key family (%s vs %s)", an.keyName.c_str(), plan.keyName.c_str());

    std::printf ("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL MASHUP TESTS PASSED", failures);
    return failures;
}
