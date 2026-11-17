// Auralloy - the mashup rule engine.
//
// Input : analysed songs (tempo, beats, key, sections) + optional stems.
// Output: a MashupPlan - clips placed on role tracks of a bar grid, plus a human-readable log that
//         explains every decision (which rule fired and why). Rendering the clips (time-stretch / pitch-shift)
//         is a separate step (renderClipBuffer) so the plan itself is cheap, deterministic and testable.
//
// The rules (see docs/RULES.md):
//   1. One tempo for everybody: choose the target BPM that needs the least stretching; no song is stretched more than maxStretch.
//   2. One key for everybody: songs are mapped to major-equivalent tonics (A minor == C major) and shifted by the
//      smallest number of semitones (<= maxShift) to one shared tonic.
//   3. Everything sits on the bar grid: sections are cut on downbeats, on 4-bar phrase boundaries.
//   4. One vocal at a time. A vocal donor never overlaps another vocal.
//   5. One bass at a time: bass is swapped on a bar line inside each transition (never two basses at once).
//   6. Sections are matched by energy (verse over verse, chorus over drop) and follow a chosen energy shape.
//   7. Transitions overlap drums/other for a few bars with equal-power fades; the incoming bass swaps in half-way.
//   8. Vocals are loudness-matched to the band and the "other" stem is ducked while a vocal is present.
//   9. Don't repeat the same section twice in a row (usage penalty) and prefer a natural ending (outro) for the last movement.
#pragma once
#include "../dsp/Analysis.h"
#include "../dsp/Stems.h"
#include "../dsp/TimePitch.h"
#include <cmath>
#include <map>
#include <sstream>

namespace al
{
enum Role { RoleVocals = 0, RoleDrums = 1, RoleBass = 2, RoleOther = 3, RoleMix = 4 };

struct SongInfo
{
    int id = 0;
    std::string name;
    Analysis an;
    std::shared_ptr<const Source> mix;
    StemSet stems;                         // empty until computed
    std::vector<float> vocalBarDb;         // per-bar vocal stem level (dBFS rms); filled by prepareSong()
    float vocalFrac = 0.5f;                // fraction of bars with a vocal present

    bool usable() const { return an.ok && an.numBars() >= 8; }
    int majorTonic() const { return an.keyMinor ? (an.keyRoot + 3) % 12 : an.keyRoot % 12; }
};

struct MashupOptions
{
    // Medley (= Sequential) is the default: the songs play one after another, beat-matched, blended at each switch.
    // VocalOverBeat (= Layered): the vocals of one song over the beat of another (needs stems), everything at once.
    enum Mode { Auto, VocalOverBeat, Medley };
    enum Shape { Rising, Wave, Flat };
    Mode mode = Auto;
    Shape shape = Rising;
    double targetLengthSec = 180.0;
    double maxStretch = 0.15;              // +-15 % tempo change at most
    int maxShift = 3;                      // semitones
    int transitionBars = 4;
    bool useStems = true;
    int minBars = 16, maxBars = 32;        // length of each movement (segment) in bars
    double forceBpm = 0.0;                 // 0 = automatic
};

struct ClipPlan
{
    int songId = 0;
    int role = 0;                          // Role
    double srcStart = 0, srcEnd = 0;       // seconds in the original song
    double tStart = 0, length = 0;         // seconds on the mashup timeline (after tempo change)
    double gainDb = 0;
    double fadeIn = 0, fadeOut = 0;        // seconds
    double semitones = 0, tempoRatio = 1;
    std::string label;
};

struct MashupPlan
{
    bool ok = false;
    std::string error;
    double bpm = 0, barSec = 0, lengthSec = 0;
    int keyRoot = 0; bool keyMinor = false;
    std::string keyName, camelot;
    std::vector<ClipPlan> clips;
    std::vector<std::string> log;
    std::map<int, std::pair<double, double>> perSong;     // songId -> (tempoRatio, semitones)
    std::vector<int> usedSongs;
};

//==============================================================================
inline int signedSemis (int from, int to)                       // shortest signed distance from -> to in semitones, -6..5
{
    int d = ((to - from) % 12 + 12) % 12;
    return d > 5 ? d - 12 : d;
}

inline void prepareSong (SongInfo& s)
{
    s.vocalBarDb.clear();
    if (! s.an.ok || ! s.stems.ok()) { s.vocalFrac = 0.5f; return; }
    int active = 0;
    for (int b = 0; b < s.an.numBars(); ++b)
    {
        const long long a = (long long) (s.an.downbeats[(size_t) b] * s.stems.vocals->sampleRate);
        const long long c = (long long) (s.an.downbeats[(size_t) b + 1] * s.stems.vocals->sampleRate);
        const float db = activeRmsDb (*s.stems.vocals, a, c);
        s.vocalBarDb.push_back (db);
        if (db > -46.0f) ++active;
    }
    s.vocalFrac = s.an.numBars() > 0 ? (float) active / (float) s.an.numBars() : 0.0f;
}

inline float vocalFracIn (const SongInfo& s, int bar0, int bar1)
{
    if (s.vocalBarDb.empty()) return s.vocalFrac;
    int a = 0, n = 0;
    for (int b = bar0; b < bar1 && b < (int) s.vocalBarDb.size(); ++b) { ++n; if (s.vocalBarDb[(size_t) b] > -46.0f) ++a; }
    return n ? (float) a / (float) n : 0.0f;
}

// 0..1 how well two songs fit (tempo + key + complementary vocals)
inline double pairScore (const SongInfo& a, const SongInfo& b, double maxStretch = 0.15)
{
    if (! a.usable() || ! b.usable()) return 0.0;
    const double dt = std::abs (std::log (a.an.bpm / b.an.bpm));
    const double tempo = std::max (0.0, 1.0 - dt / std::log (1.0 + 2.0 * maxStretch));
    const int d = std::abs (signedSemis (a.majorTonic(), b.majorTonic()));
    const double key = d == 0 ? 1.0 : (d <= 3 ? 1.0 - 0.17 * d : 0.15);
    const double role = std::min (1.0, std::abs (a.vocalFrac - b.vocalFrac) * 1.6 + 0.4);
    return 0.5 * tempo + 0.4 * key + 0.1 * role;
}

//==============================================================================
namespace mashup_detail
{
struct Run { int song = 0; int sec0 = 0, sec1 = 0; int bar0 = 0, bar1 = 0; float energy = 0; bool hasIntro = false, hasOutro = false; };

inline std::string fmt (const char* f, double a = 0, double b = 0, double c = 0, double d = 0)
{
    char buf[256]; std::snprintf (buf, sizeof buf, f, a, b, c, d); return buf;
}

// all runs of contiguous sections of a song that are at least minBars long (merged forward when a section is short)
inline std::vector<Run> runsOf (const SongInfo& s, int songIdx, int minBars, int maxBars)
{
    std::vector<Run> out;
    const auto& secs = s.an.sections;
    for (int i = 0; i < (int) secs.size(); ++i)
    {
        Run r; r.song = songIdx; r.sec0 = i; r.bar0 = secs[(size_t) i].bar0;
        int j = i; double e = 0; int bars = 0;
        while (j < (int) secs.size() && bars < minBars)
        {
            e += secs[(size_t) j].energy * secs[(size_t) j].bars(); bars += secs[(size_t) j].bars(); ++j;
        }
        if (bars < minBars) continue;
        r.sec1 = j; r.bar1 = r.bar0 + std::min (bars, maxBars);
        r.energy = (float) (e / bars);
        r.hasIntro = i == 0 && secs[(size_t) i].label == "Intro";
        r.hasOutro = (j == (int) secs.size());
        out.push_back (r);
    }
    return out;
}
} // namespace mashup_detail

//==============================================================================
inline MashupPlan planMashup (const std::vector<SongInfo>& songsIn, const MashupOptions& opt)
{
    using namespace mashup_detail;
    MashupPlan P;
    auto log = [&] (const std::string& s) { P.log.push_back (s); };

    // ---- usable songs ----
    std::vector<int> idx;                                  // indices into songsIn
    for (int i = 0; i < (int) songsIn.size(); ++i)
    {
        if (songsIn[(size_t) i].usable()) idx.push_back (i);
        else log ("Skipped \"" + songsIn[(size_t) i].name + "\": could not find a steady beat grid (needs at least 8 bars).");
    }
    if (idx.size() < 2) { P.error = "Need at least two analysed songs with a clear beat."; return P; }

    // ---- rule 1: one tempo ----
    double bestT = 0, bestCost = 1e30;
    std::vector<double> cands;
    if (opt.forceBpm > 0) cands.push_back (opt.forceBpm);
    else for (int i : idx) cands.push_back (songsIn[(size_t) i].an.bpm);
    const double cap = std::log (1.0 + opt.maxStretch);
    for (double T : cands)
    {
        double cost = 0; int excl = 0;
        for (int i : idx)
        {
            const double d = std::abs (std::log (T / songsIn[(size_t) i].an.bpm));
            if (d > cap) { ++excl; cost += 4.0; } else cost += d;
        }
        cost += excl * 10.0;
        if (cost < bestCost) { bestCost = cost; bestT = T; }
    }
    P.bpm = bestT; P.barSec = 240.0 / bestT;
    std::vector<int> use;
    for (int i : idx)
    {
        const double ratio = bestT / songsIn[(size_t) i].an.bpm;
        if (std::abs (std::log (ratio)) > cap + 1e-9)
            log (fmt ("Left out \"", 0) + songsIn[(size_t) i].name + fmt ("\": %.0f BPM is more than %.0f%% away from the target tempo.", songsIn[(size_t) i].an.bpm, opt.maxStretch * 100));
        else use.push_back (i);
    }
    if (use.size() < 2) { P.error = "The songs' tempos are too far apart (try a larger tempo tolerance)."; return P; }
    log (fmt ("Rule 1 - tempo: target %.1f BPM, the least stretching for everyone.", P.bpm));
    for (int i : use)
    {
        const double ratio = bestT / songsIn[(size_t) i].an.bpm;
        log ("   \"" + songsIn[(size_t) i].name + fmt ("\" %.1f BPM -> %+.1f%%", songsIn[(size_t) i].an.bpm, (ratio - 1.0) * 100.0));
        P.perSong[songsIn[(size_t) i].id].first = ratio;
    }

    // ---- rule 2: one key (major-equivalent tonics, so A minor == C major) ----
    int bestE = 0; double bestK = 1e30;
    for (int E = 0; E < 12; ++E)
    {
        double c = 0;
        for (int i : use)
        {
            const auto& s = songsIn[(size_t) i];
            const int sh = signedSemis (s.majorTonic(), E);
            const double w = 0.4 + 0.6 * s.an.keyConfidence;
            c += std::abs (sh) * w + (std::abs (sh) > opt.maxShift ? 6.0 : 0.0);
        }
        if (c < bestK) { bestK = c; bestE = E; }
    }
    {
        std::vector<int> keep;
        int minors = 0;
        for (int i : use)
        {
            const auto& s = songsIn[(size_t) i];
            const int sh = signedSemis (s.majorTonic(), bestE);
            if (std::abs (sh) > opt.maxShift)
            {
                log ("Left out \"" + s.name + "\": its key (" + s.an.keyName + ") would need a " + std::to_string (std::abs (sh)) + "-semitone shift, over the limit of "
                     + std::to_string (opt.maxShift) + ".");
                continue;
            }
            keep.push_back (i);
            P.perSong[s.id].second = sh;
            if (s.an.keyMinor) ++minors;
        }
        use = keep;
        if (use.size() < 2) { P.error = "The songs' keys are too far apart (try allowing a bigger key shift)."; return P; }
        P.keyMinor = minors * 2 > (int) use.size();
        P.keyRoot = P.keyMinor ? (bestE + 9) % 12 : bestE;
        P.keyName = keyNameOf (P.keyRoot, P.keyMinor);
        P.camelot = camelotOf (P.keyRoot, P.keyMinor);
        log ("Rule 2 - key: target " + P.keyName + " (" + P.camelot + "), the smallest total pitch shift; relative major/minor counts as the same key.");
        for (int i : use)
        {
            const auto& s = songsIn[(size_t) i];
            log ("   \"" + s.name + "\" " + s.an.keyName + " (" + s.an.camelot + ") -> " + (P.perSong[s.id].second == 0 ? std::string ("no shift") : fmt ("%+.0f semitone(s)", P.perSong[s.id].second)));
        }
    }
    for (int i : use) P.usedSongs.push_back (songsIn[(size_t) i].id);

    auto S = [&] (int k) -> const SongInfo& { return songsIn[(size_t) use[(size_t) k]]; };
    const int U = (int) use.size();
    const bool haveStems = opt.useStems && [&] { for (int k = 0; k < U; ++k) if (! S (k).stems.ok()) return false; return true; }();

    // ---- bar grid ----
    const int totalBars = std::max (16, (int) std::round (opt.targetLengthSec / P.barSec));
    const int W = std::max (2, std::min (8, opt.transitionBars));
    log (fmt ("Rule 3 - grid: %.0f bars of 4/4 at %.1f BPM (about %.0f s); every cut lands on a downbeat, phrases in blocks of 4 bars.", totalBars, P.bpm, totalBars * P.barSec));

    // order songs so that neighbours are compatible: greedy chain starting at the calmest opener
    std::vector<int> order;
    {
        std::vector<bool> taken ((size_t) U, false);
        int cur = 0; float bestE = 9;
        for (int k = 0; k < U; ++k) { const float e = S (k).an.sections.empty() ? 1.0f : S (k).an.sections.front().energy; if (e < bestE) { bestE = e; cur = k; } }
        order.push_back (cur); taken[(size_t) cur] = true;
        while ((int) order.size() < U)
        {
            int nxt = -1; double bs = -1;
            for (int k = 0; k < U; ++k) if (! taken[(size_t) k]) { const double sc = pairScore (S (cur), S (k), opt.maxStretch); if (sc > bs) { bs = sc; nxt = k; } }
            order.push_back (nxt); taken[(size_t) nxt] = true; cur = nxt;
        }
    }

    // ---- candidate runs per song ----
    std::vector<std::vector<Run>> runs ((size_t) U);
    // every song should get a turn: cap the movement length so that U songs fit into the total length
    const int perSong = ((totalBars / U) / 4) * 4 + W;
    const int effMax = std::max (8, std::min (opt.maxBars, perSong));
    const int effMin = std::min (opt.minBars, effMax);
    for (int k = 0; k < U; ++k) runs[(size_t) k] = runsOf (S (k), k, effMin, effMax);
    for (int kk = 0; kk < U; ++kk) if (runs[(size_t) kk].empty()) runs[(size_t) kk] = runsOf (S (kk), kk, 8, effMax);
    std::map<std::pair<int, int>, int> usedBase, usedDonor;          // (song, section) -> times used

    const bool vocalMode = haveStems && opt.mode == MashupOptions::VocalOverBeat;      // Auto and Medley both mean 'one song after another'
    log (vocalMode ? "Mode - layered: the vocals of one song sit over the beat of another, all at once."
                   : "Mode - sequential mix: the songs play one after another; at every switch the next song is beat-matched and blended in.");
    std::vector<ClipPlan> movementsClips;
    double tBar = 0;                                                  // movement start, in bars (before overlap)
    double prevEndBar = 0;
    int k = 0;
    struct MoveInfo { double startBar, endBar; int base, donor; };
    std::vector<MoveInfo> moves;

    while (prevEndBar < totalBars - 3 && k < 24)
    {
        const bool first = k == 0;
        const double startBar = first ? 0.0 : prevEndBar - W;
        const int remaining = (int) std::round (totalBars - startBar);
        const bool wantEnd = remaining <= 40;            // prediction, used only to prefer an outro when choosing the section
        const double eT = opt.shape == MashupOptions::Flat ? 0.7
                        : opt.shape == MashupOptions::Rising ? 0.35 + 0.65 * std::min (1.0, startBar / std::max (1.0, totalBars * 0.7))
                        : 0.6 + 0.3 * std::sin (6.2831853 * startBar / std::max (24.0, totalBars / 1.6));

        // --- base song + run ---
        const int baseK = order[(size_t) (k % U)];
        const Run* bestRun = nullptr; double bestScore = 1e30;
        for (const auto& r : runs[(size_t) baseK])
        {
            double sc = std::abs (r.energy - eT);
            sc += 0.7 * usedBase[{ baseK, r.sec0 }];
            if (! first && r.hasIntro) sc += 0.35;
            if (r.hasOutro && ! wantEnd) sc += 0.45;
            if (wantEnd && r.hasOutro) sc -= 0.3;
            if (first && r.hasIntro) sc -= 0.25;
            if (sc < bestScore) { bestScore = sc; bestRun = &r; }
        }
        if (! bestRun) break;
        Run base = *bestRun;
        int bars = std::min (base.bar1 - base.bar0, std::min (effMax, remaining));
        bars = std::max (8, (bars / 4) * 4);

        // --- vocal donor ---
        int donorK = -1; Run donor;
        if (vocalMode)
        {
            double bestD = -1e30;
            for (int dk = 0; dk < U; ++dk)
            {
                if (dk == baseK) continue;
                for (const auto& r : runs[(size_t) dk])
                {
                    const int dbars = std::min (r.bar1 - r.bar0, bars);
                    if (dbars < 8) continue;
                    const float vf = vocalFracIn (S (dk), r.bar0, r.bar0 + dbars);
                    if (vf < 0.3f) continue;
                    double sc = 1.2 * vf - 0.8 * std::abs (r.energy - base.energy) + 0.5 * pairScore (S (baseK), S (dk), opt.maxStretch)
                              - 0.9 * usedDonor[{ dk, r.sec0 }] - 0.06 * std::abs (P.perSong[S (dk).id].second);
                    if (sc > bestD) { bestD = sc; donorK = dk; donor = r; }
                }
            }
            if (donorK >= 0) bars = std::max (8, (std::min (bars, donor.bar1 - donor.bar0) / 4) * 4);
        }

        const double endBar = startBar + bars;
        const bool lastMove = endBar >= totalBars - 3;      // known only now that the movement length is final
        moves.push_back ({ startBar, endBar, baseK, donorK });
        usedBase[{ baseK, base.sec0 }]++;
        if (donorK >= 0) usedDonor[{ donorK, donor.sec0 }]++;

        // ---- clips ----
        auto addClip = [&] (int songK, int role, int srcBar0, int srcBars, double tStartBar, double gain, double fin, double fout, const std::string& label)
        {
            const auto& s = S (songK);
            ClipPlan c;
            c.songId = s.id; c.role = role;
            const int b0 = std::max (0, srcBar0), b1 = std::min (s.an.numBars(), srcBar0 + srcBars);
            if (b1 <= b0) return;
            c.srcStart = s.an.downbeats[(size_t) b0]; c.srcEnd = s.an.downbeats[(size_t) b1];
            c.tempoRatio = P.perSong[s.id].first; c.semitones = P.perSong[s.id].second;
            c.tStart = tStartBar * P.barSec; c.length = (b1 - b0) * P.barSec;
            c.gainDb = gain; c.fadeIn = fin * P.barSec; c.fadeOut = fout * P.barSec;
            c.label = label;
            P.clips.push_back (c);
        };
        const bool hasPrev = ! first;
        const std::string baseName = S (baseK).name;
        const std::string baseLabel = baseName;
        const double bassDelay = hasPrev ? W / 2.0 : 0.0;
        const double outBassEarly = lastMove ? 0.0 : W / 2.0;
        const double outTail = lastMove ? 2.0 : (double) W;

        if (haveStems)
        {
            const double duck = donorK >= 0 ? -2.5 : 0.0;
            // sequential: all four stems of a song keep their original balance (they sum back to the original mix)
            const double gD = vocalMode ? -1.0 : 0.0, gB = vocalMode ? -1.0 : 0.0, gO = vocalMode ? -2.0 + duck : 0.0;
            addClip (baseK, RoleDrums, base.bar0, bars, startBar, gD, hasPrev ? W : 0.0, outTail, baseLabel);
            addClip (baseK, RoleOther, base.bar0, bars, startBar, gO, hasPrev ? W : 0.0, outTail, baseLabel);
            // bass: swaps in half-way through the incoming transition, leaves half-way through the outgoing one
            {
                const int skip = (int) std::round (bassDelay);
                const int len = bars - skip - (int) std::round (outBassEarly);
                addClip (baseK, RoleBass, base.bar0 + skip, len, startBar + skip, gB, 0.15, 0.15, baseLabel);
            }
            if (donorK >= 0)
            {
                const int vskip = hasPrev ? W : 0;                       // the previous vocal has finished
                const int vlen = bars - vskip - (lastMove ? 0 : 1);
                addClip (donorK, RoleVocals, donor.bar0 + vskip, vlen, startBar + vskip, 0.0, 0.3, lastMove ? 2.0 : 0.6, S (donorK).name);
            }
            else
            {
                const int vskip = hasPrev ? W : 0;
                const int vlen = bars - vskip - (lastMove ? 0 : 1);
                addClip (baseK, RoleVocals, base.bar0 + vskip, vlen, startBar + vskip, 0.0, 0.3, lastMove ? 2.0 : 0.6, baseLabel);
            }
        }
        else
        {
            addClip (baseK, RoleMix, base.bar0, bars, startBar, 0.0, hasPrev ? W : 0.0, outTail, baseLabel);
        }

        log (fmt ("Movement %.0f (bars %.0f-%.0f): ", k + 1, startBar + 1, endBar)
             + "\"" + baseName + "\" " + S (baseK).an.sections[(size_t) base.sec0].label + fmt (" (energy %.2f)", base.energy)
             + (donorK >= 0 ? " + vocals of \"" + S (donorK).name + "\"" + fmt (" (energy %.2f, vocals in %.0f%% of bars)", donor.energy, 100.0 * vocalFracIn (S (donorK), donor.bar0, donor.bar0 + bars))
                            : (haveStems ? std::string (" with its own vocals") : std::string (" (full mix)")))
             + fmt (", target energy %.2f.", eT));
        prevEndBar = endBar; ++k;
    }

    if (P.clips.empty()) { P.error = "Could not build an arrangement from these songs."; return P; }
    P.lengthSec = prevEndBar * P.barSec;
    log ("Rule 4 - one vocal at a time: vocals of different songs never overlap; the next song's vocal starts after the previous movement's vocal has ended.");
    log (fmt ("Rule 5 - one bass at a time: the bass swaps on bar lines, %.0f bars into each transition.", W / 2.0));
    log (fmt ("Rule 7 - transitions: drums and other overlap for %.0f bars with equal-power fades.", W));
    if (haveStems && vocalMode) log ("Rule 8 - mix: vocals are loudness-matched, the 'other' stem is ducked 2.5 dB under donor vocals.");
    else if (haveStems) log ("Rule 8 - mix: every song keeps its original balance and is level-matched to the others.");
    else log ("No stems were used (Quick Stems not run): songs are blended as full mixes with equal-power crossfades.");
    P.ok = true;
    return P;
}

//==============================================================================
// Renders one planned clip: cuts the section out of the stem (or mix), changes tempo and pitch, and matches loudness.
inline std::shared_ptr<Source> renderClipBuffer (const SongInfo& s, const ClipPlan& c, double* appliedGainDb = nullptr, bool matchLoudness = true)
{
    std::shared_ptr<const Source> src;
    if (c.role == RoleMix) src = s.mix;
    else if (s.stems.ok()) src = s.stems.byIndex (c.role);
    if (! src) return nullptr;
    const double pad = 0.35;                                               // extra audio so the stretcher has context at both ends
    const double a = std::max (0.0, c.srcStart - pad), b = std::min ((double) src->frames() / src->sampleRate, c.srcEnd + pad);
    auto cut = sliceSource (*src, a, b);
    auto proc = timePitch (*cut, c.tempoRatio, c.semitones);
    if (! proc) return nullptr;
    // trim the padding back off (in the processed time domain)
    const double lead = (c.srcStart - a) / c.tempoRatio;
    const double wantLen = (c.srcEnd - c.srcStart) / c.tempoRatio;
    const long long i0 = (long long) std::llround (lead * proc->sampleRate);
    const long long i1 = std::min<long long> ((long long) proc->frames(), i0 + (long long) std::llround (wantLen * proc->sampleRate));
    auto out = std::make_shared<Source>();
    out->sampleRate = proc->sampleRate;
    if (i1 > i0) { out->l.assign (proc->l.begin() + i0, proc->l.begin() + i1); out->r.assign (proc->r.begin() + i0, proc->r.begin() + i1); }
    // loudness match per role
    static const float target[5] = { -19.0f, -21.0f, -22.0f, -24.0f, -21.0f };
    const float meas = activeRmsDb (*out, 0, (long long) out->frames());
    double g = 0.0;
    if (matchLoudness && meas > -80.0f) g = std::max (-9.0f, std::min (9.0f, target[c.role] - meas));
    if (appliedGainDb) *appliedGainDb = g;
    const float lin = std::pow (10.0f, (float) g / 20.0f);
    for (auto& v : out->l) v *= lin;
    for (auto& v : out->r) v *= lin;
    out->buildPeaks();
    return out;
}
} // namespace al
