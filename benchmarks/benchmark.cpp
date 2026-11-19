// Auralloy benchmark suite (synthetic, seeded, reproducible).
//   benchmark <output folder> [analysisSongs=96] [mashupTriples=30]
//
// A. analysis accuracy on synthetic songs with known tempo / key / downbeat / structure
// B. stem separation quality (SI-SDR) on synthetic mixes with known stems
// C. mashups: rule compliance, tempo / key coherence of the *rendered* audio, naive-concatenation baseline, runtimes
#include "../src/mashup/Mashup.h"
#include "../tests/SynthSong.h"
#include <chrono>
#include <cstdio>
#include <map>
#include <random>
#include <set>

using Clock = std::chrono::steady_clock;
static double since (Clock::time_point t) { return std::chrono::duration<double> (Clock::now() - t).count(); }

static double foldBpm (double b) { while (b < 75.0) b *= 2.0; while (b >= 160.0) b *= 0.5; return b; }
static int tonicOf (int root, bool minor) { return minor ? (root + 3) % 12 : root % 12; }

struct Stat
{
    std::vector<double> v;
    void add (double x) { v.push_back (x); }
    double mean() const { double s = 0; for (double x : v) s += x; return v.empty() ? 0 : s / (double) v.size(); }
    double sd() const { if (v.size() < 2) return 0; double m = mean(), s = 0; for (double x : v) s += (x - m) * (x - m); return std::sqrt (s / (double) (v.size() - 1)); }
    double median() const { if (v.empty()) return 0; auto c = v; std::sort (c.begin(), c.end()); return c[c.size() / 2]; }
    double p90() const { if (v.empty()) return 0; auto c = v; std::sort (c.begin(), c.end()); return c[(size_t) ((double) c.size() * 0.9)]; }
};

int main (int argc, char** argv)
{
    setvbuf (stdout, nullptr, _IONBF, 0);
    const std::string outDir = argc > 1 ? argv[1] : ".";
    const int nAnalysis = argc > 2 ? std::atoi (argv[2]) : 96;
    const int nMash = argc > 3 ? std::atoi (argv[3]) : 30;
    std::mt19937 rng (20261101);
    auto uni = [&] (double a, double b) { return std::uniform_real_distribution<double> (a, b) (rng); };
    auto uint = [&] (int a, int b) { return std::uniform_int_distribution<int> (a, b) (rng); };

    FILE* fa = std::fopen ((outDir + "/analysis.csv").c_str(), "w");
    std::fprintf (fa, "id,bpm_true,bpm_found,bpm_err_pct,key_true,key_found,key_class,downbeat_err_ms,bound_tp,bound_fp,bound_fn,seconds\n");
    int tempoOk = 0, tempoOctave = 0, keyExact = 0, keyRel = 0, keyFifth = 0, dbOk = 0, tp = 0, fp = 0, fn = 0, analysed = 0;
    Stat tempoErr, dbErr, analysisSec;

    // ============================ A. analysis ============================
    std::printf ("== A. analysis on %d synthetic songs\n", nAnalysis);
    static const int structs[3][4] = { { 4, 8, 8, 4 }, { 8, 16, 16, 8 }, { 8, 8, 16, 8 } };
    for (int i = 0; i < nAnalysis; ++i)
    {
        testsong::Spec sp;
        sp.bpm = std::round (uni (70.0, 170.0) * 2.0) / 2.0;
        sp.root = uint (0, 11); sp.minor = uint (0, 1) == 1; sp.lead = uni (0.0, 0.6);
        const auto& st = structs[uint (0, 2)];
        sp.introBars = st[0]; sp.verseBars = st[1]; sp.chorusBars = st[2]; sp.outroBars = st[3];
        sp.vocal = uint (0, 1) == 1; sp.seed = (unsigned) (100 + i);
        auto song = testsong::make (sp);
        const auto t0 = Clock::now();
        al::Analysis a = al::analyze (song);
        const double sec = since (t0);
        analysisSec.add (sec);
        if (! a.ok) { std::fprintf (fa, "%d,%.1f,,,,,fail,,,,,%.2f\n", i, sp.bpm, sec); continue; }
        ++analysed;
        const double expect = foldBpm (sp.bpm);
        const double err = std::abs (a.bpm - expect) / expect * 100.0;
        tempoErr.add (err);
        if (err < 0.5) ++tempoOk;
        if (std::abs (std::log2 (a.bpm / sp.bpm) - std::round (std::log2 (a.bpm / sp.bpm))) < 0.01) ++tempoOctave;   // right tempo, maybe another octave
        const char* kc = "other";
        if (a.keyRoot == sp.root && a.keyMinor == sp.minor) { ++keyExact; kc = "exact"; }
        else if (tonicOf (a.keyRoot, a.keyMinor) == tonicOf (sp.root, sp.minor)) { ++keyRel; kc = "relative"; }
        else if (std::abs (((tonicOf (a.keyRoot, a.keyMinor) - tonicOf (sp.root, sp.minor)) % 12 + 12) % 12 - 6) == 1) { ++keyFifth; kc = "fifth"; }
        const double bar = 240.0 / sp.bpm, aBar = a.barSeconds();
        double ph = std::fmod (a.downbeats[0] - sp.lead, std::max (bar, 1e-3)); if (ph < 0) ph += bar; if (ph > bar / 2) ph -= bar;
        const double msErr = std::abs (ph) * 1000.0;
        dbErr.add (msErr);
        (void) aBar;
        if (msErr < 50.0) ++dbOk;
        // section boundaries (tolerance +-1 bar), truth in bars at the *detected* grid is the same grid up to the tempo fold
        const double barTrue = 240.0 / sp.bpm;
        std::vector<double> truthSec = { (sp.introBars) * barTrue + sp.lead, (sp.introBars + sp.verseBars) * barTrue + sp.lead, (sp.introBars + sp.verseBars + sp.chorusBars) * barTrue + sp.lead };
        int ltp = 0, lfp = 0, lfn = 0;
        std::set<int> matched;
        for (size_t s = 1; s < a.sections.size(); ++s)
        {
            bool hit = false;
            for (int t = 0; t < 3; ++t)
                if (std::abs (a.sections[s].start - truthSec[(size_t) t]) <= barTrue * 1.01 && ! matched.count (t)) { matched.insert (t); hit = true; break; }
            if (hit) ++ltp; else ++lfp;
        }
        lfn = 3 - (int) matched.size();
        tp += ltp; fp += lfp; fn += lfn;
        std::fprintf (fa, "%d,%.1f,%.2f,%.3f,%s,%s,%s,%.1f,%d,%d,%d,%.2f\n", i, sp.bpm, a.bpm, err, al::keyNameOf (sp.root, sp.minor).c_str(), a.keyName.c_str(), kc, msErr, ltp, lfp, lfn, sec);
        if ((i + 1) % 16 == 0) std::printf ("   %d / %d\n", i + 1, nAnalysis);
    }
    std::fclose (fa);
    const double prec = tp + fp ? (double) tp / (tp + fp) : 0, rec = tp + fn ? (double) tp / (tp + fn) : 0;

    // ============================ B. stems ============================
    std::printf ("== B. stem separation on synthetic mixes\n");
    Stat sdrV, sdrD, sdrB, recon;
    const int nStem = 16;
    for (int i = 0; i < nStem; ++i)
    {
        const double sr = 44100.0; const size_t n = (size_t) (8.0 * sr);
        const double TP = 6.283185307179586;
        al::Source in; in.sampleRate = sr; in.l.assign (n, 0.0f); in.r.assign (n, 0.0f);
        std::vector<float> tV (n), tD (n), tB (n), tP (n);
        al::Rng r2; r2.s = 777u + (unsigned) i * 31u;
        const double f0 = uni (260.0, 520.0), bassHz = uni (41.0, 82.0), padHz = uni (180.0, 420.0), kickHz = uni (45.0, 65.0), tempo = uni (0.4, 0.6);
        for (size_t k = 0; k < n; ++k)
        {
            const double t = (double) k / sr;
            const double note = std::fmod (t, 1.0) < 0.8 ? 1.0 : 0.0, ph = TP * f0 * t + 0.4 * std::sin (TP * 5.5 * t);
            tV[k] = (float) ((std::sin (ph) + 0.5 * std::sin (2 * ph) + 0.3 * std::sin (3 * ph)) * 0.18 * note);
            tP[k] = (float) ((std::sin (TP * padHz * t) + 0.4 * std::sin (TP * padHz * 2 * t)) * 0.16);
            const double kt = std::fmod (t, tempo), ht = std::fmod (t, tempo * 0.5);
            tD[k] = (float) (std::sin (TP * (kickHz + 120 * std::exp (-kt * 40)) * kt) * std::exp (-kt * 16) * 0.6 + r2.bip() * std::exp (-ht * 90) * 0.2);
            tB[k] = (float) (std::sin (TP * bassHz * t) * 0.25);
            in.l[k] = tV[k] + tP[k] + tD[k] + tB[k];
            in.r[k] = tV[k] + 0.0f * tP[k] + tD[k] + tB[k];          // the pad is hard-left
        }
        auto st = al::separateStems (in);
        auto sisdr = [&] (const std::vector<float>& est, const std::vector<float>& ref)
        {
            double sr2 = 0, se = 0, dot = 0;
            for (size_t k = 0; k < n; ++k) { dot += (double) est[k] * ref[k]; sr2 += (double) ref[k] * ref[k]; }
            const double a = dot / (sr2 + 1e-20);
            double num = 0, den = 0;
            for (size_t k = 0; k < n; ++k) { const double tgt = a * ref[k], e = est[k] - tgt; num += tgt * tgt; den += e * e; }
            (void) se;
            return 10.0 * std::log10 (num / (den + 1e-20) + 1e-20);
        };
        sdrV.add (sisdr (st.vocals->l, tV)); sdrD.add (sisdr (st.drums->l, tD)); sdrB.add (sisdr (st.bass->l, tB));
        double err = 0, ref = 0;
        for (size_t k = 0; k < n; ++k) { const double s = st.vocals->l[k] + st.drums->l[k] + st.bass->l[k] + st.other->l[k]; err += (s - in.l[k]) * (s - in.l[k]); ref += (double) in.l[k] * in.l[k]; }
        recon.add (10.0 * std::log10 (err / ref + 1e-30));
    }

    // ============================ C. mashups ============================
    std::printf ("== C. mashups: %d random triples\n", nMash);
    const int poolN = 12;
    std::vector<al::SongInfo> pool;
    Stat stemSec;
    for (int i = 0; i < poolN; ++i)
    {
        testsong::Spec sp;
        sp.bpm = std::round (uni (88.0, 118.0)); sp.root = uint (0, 11); sp.minor = uint (0, 1) == 1; sp.lead = uni (0.0, 0.6);
        sp.introBars = 8; sp.verseBars = 16; sp.chorusBars = 16; sp.outroBars = 8; sp.vocal = i % 3 != 2; sp.seed = (unsigned) (500 + i);
        al::SongInfo s; s.id = i + 1; s.name = "song" + std::to_string (i + 1);
        auto src = std::make_shared<al::Source> (testsong::make (sp));
        s.mix = src; s.an = al::analyze (*src);
        const auto t0 = Clock::now();
        s.stems = al::separateStems (*src);
        stemSec.add (since (t0) / (src->seconds() / 60.0));              // seconds of compute per minute of audio
        al::prepareSong (s);
        pool.push_back (std::move (s));
        std::printf ("   prepared %d / %d\n", i + 1, poolN);
    }

    FILE* fm = std::fopen ((outDir + "/mashups.csv").c_str(), "w");
    std::fprintf (fm, "trial,mode,songs,used,plan_ms,render_s,bpm_target,bpm_measured,tempo_err,tempo_sd_windows,tempo_sd_baseline,key_agree,key_agree_baseline,rules_ok,seconds\n");
    int planned = 0, rulesAll = 0, trials = 0;
    Stat planMs, renderS, tempoErrM, sdOurs, sdBase, keyOurs, keyBase;
    auto renderPlan = [&] (const al::MashupPlan& plan, const std::map<int, const al::SongInfo*>& byId)
    {
        const double sr = 44100.0;
        al::Source out; out.sampleRate = sr;
        out.l.assign ((size_t) (plan.lengthSec * sr) + 44100, 0.0f); out.r = out.l;
        for (auto& c : plan.clips)
        {
            double g = 0.0;
            auto buf = al::renderClipBuffer (*byId.at (c.songId), c, &g, false);
            if (! buf) continue;
            const long long start = (long long) std::llround (c.tStart * sr), n = (long long) buf->frames();
            const double lin = std::pow (10.0, c.gainDb / 20.0);
            for (long long i = 0; i < n; ++i)
            {
                const double t = (double) i / sr, tl = (double) (n - i) / sr;
                double gg = lin;
                if (c.fadeIn > 0 && t < c.fadeIn) gg *= std::sin (1.5707963 * t / c.fadeIn);
                if (c.fadeOut > 0 && tl < c.fadeOut) gg *= std::sin (1.5707963 * tl / c.fadeOut);
                if ((size_t) (start + i) < out.l.size()) { out.l[(size_t) (start + i)] += (float) (gg * buf->l[(size_t) i]); out.r[(size_t) (start + i)] += (float) (gg * buf->r[(size_t) i]); }
            }
        }
        return out;
    };
    auto windowStats = [&] (const al::Source& mix, double winSec, int targetTonic, double& sd, double& keyAgree)
    {
        Stat b; int agree = 0, cnt = 0;
        for (double t = 0.0; t + winSec <= mix.seconds(); t += winSec)
        {
            auto w = al::sliceSource (mix, t, t + winSec);
            auto an = al::analyze (*w);
            if (! an.ok) continue;
            b.add (an.bpm);
            ++cnt;
            if (tonicOf (an.keyRoot, an.keyMinor) == targetTonic) ++agree;
        }
        sd = b.sd(); keyAgree = cnt ? (double) agree / cnt : 0.0;
    };

    for (int tr = 0; tr < nMash; ++tr)
    {
        std::vector<int> idx (poolN);
        std::iota (idx.begin(), idx.end(), 0);
        std::shuffle (idx.begin(), idx.end(), rng);
        std::vector<al::SongInfo> songs;
        for (int k = 0; k < 3; ++k) songs.push_back (pool[(size_t) idx[(size_t) k]]);
        for (int modeI = 0; modeI < 2; ++modeI)
        {
            if (modeI == 1 && tr >= nMash / 2) continue;                // layered mode on half of the trials
            al::MashupOptions opt; opt.targetLengthSec = 120; opt.mode = modeI == 0 ? al::MashupOptions::Medley : al::MashupOptions::VocalOverBeat;
            const auto t0 = Clock::now();
            auto plan = al::planMashup (songs, opt);
            const double pm = since (t0) * 1000.0;
            ++trials;
            if (! plan.ok) { std::fprintf (fm, "%d,%s,3,0,%.2f,,,,,,,,,fail,\n", tr, modeI ? "layered" : "sequential", pm); continue; }
            ++planned; planMs.add (pm);

            // rule compliance
            bool ok = true;
            for (auto& kv : plan.perSong) { if (std::abs (kv.second.first - 1.0) > opt.maxStretch + 1e-9) ok = false; if (std::abs (kv.second.second) > opt.maxShift) ok = false; }
            for (auto& c : plan.clips)
            {
                const double b0 = c.tStart / plan.barSec, b1 = (c.tStart + c.length) / plan.barSec;
                if (std::abs (b0 - std::round (b0)) > 1e-6 || std::abs (b1 - std::round (b1)) > 1e-6) ok = false;
            }
            for (int role : { (int) al::RoleVocals, (int) al::RoleBass })
                for (size_t a = 0; a < plan.clips.size(); ++a) for (size_t b = a + 1; b < plan.clips.size(); ++b)
                {
                    const auto& x = plan.clips[a]; const auto& y = plan.clips[b];
                    if (x.role != role || y.role != role) continue;
                    if (x.tStart < y.tStart + y.length - 1e-6 && y.tStart < x.tStart + x.length - 1e-6) ok = false;
                }
            if (ok) ++rulesAll;

            if (modeI != 0) { std::fprintf (fm, "%d,layered,3,%zu,%.2f,,,,,,,,,%d,\n", tr, plan.usedSongs.size(), pm, ok ? 1 : 0); continue; }

            // render and measure the finished audio
            std::map<int, const al::SongInfo*> byId; for (auto& s : songs) byId[s.id] = &s;
            const auto r0 = Clock::now();
            al::Source mash = renderPlan (plan, byId);
            const double rs = since (r0);
            renderS.add (rs / (plan.lengthSec / 60.0));
            auto whole = al::analyze (mash);
            const int targetTonic = tonicOf (plan.keyRoot, plan.keyMinor);
            double sd1 = 0, ka1 = 0, sd2 = 0, ka2 = 0;
            windowStats (mash, 20.0, targetTonic, sd1, ka1);

            // baseline: same songs, same order, 24 bars each from the start at their native tempo and key, 4-bar crossfades, no stretching or shifting
            al::Source base; base.sampleRate = 44100.0;
            {
                double cursor = 0.0;
                std::vector<std::pair<const al::SongInfo*, double>> parts;
                for (int id : plan.usedSongs) parts.push_back ({ byId[id], 0.0 });
                base.l.assign ((size_t) (plan.lengthSec * 44100.0) + 44100, 0.0f); base.r = base.l;
                for (auto& pr : parts)
                {
                    const auto& s = *pr.first;
                    const double bar = s.an.barSeconds(), start = s.an.downbeats.empty() ? 0.0 : s.an.downbeats.front();
                    const double len = std::min (24.0 * bar, s.mix->seconds() - start);
                    auto cut = al::sliceSource (*s.mix, start, start + len);
                    const long long st0 = (long long) (cursor * 44100.0);
                    const double xf = 4.0 * bar;
                    for (size_t i = 0; i < cut->frames(); ++i)
                    {
                        const double t = (double) i / 44100.0, tl = len - t;
                        double g = 1.0;
                        if (cursor > 0 && t < xf) g *= std::sin (1.5707963 * t / xf);
                        if (tl < xf) g *= std::sin (1.5707963 * tl / xf);
                        if ((size_t) (st0 + (long long) i) < base.l.size()) { base.l[(size_t) (st0 + (long long) i)] += (float) (g * cut->l[i]); base.r[(size_t) (st0 + (long long) i)] += (float) (g * cut->r[i]); }
                    }
                    cursor += len - xf;
                }
                base.l.resize ((size_t) ((cursor + 4.0 * 2.4) * 44100.0)); base.r.resize (base.l.size());
            }
            windowStats (base, 20.0, targetTonic, sd2, ka2);
            const double terr = whole.ok ? std::abs (whole.bpm - plan.bpm) : -1.0;
            if (whole.ok) tempoErrM.add (terr);
            sdOurs.add (sd1); sdBase.add (sd2); keyOurs.add (ka1); keyBase.add (ka2);
            std::fprintf (fm, "%d,sequential,3,%zu,%.2f,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.2f,%.2f,%d,\n", tr, plan.usedSongs.size(), pm, rs, plan.bpm, whole.ok ? whole.bpm : 0.0, terr, sd1, sd2, ka1, ka2, ok ? 1 : 0);
            std::printf ("   trial %d: target %.1f BPM, measured %.2f, tempo sd %.2f vs baseline %.2f, key agreement %.2f vs %.2f\n", tr, plan.bpm, whole.ok ? whole.bpm : 0.0, sd1, sd2, ka1, ka2);
        }
    }
    std::fclose (fm);

    // ============================ summary ============================
    FILE* fs = std::fopen ((outDir + "/summary.json").c_str(), "w");
    std::fprintf (fs, "{\n  \"seed\": 20261101,\n");
    std::fprintf (fs, "  \"analysis\": {\n    \"songs\": %d, \"analysed\": %d,\n    \"tempo_within_0.5pct\": %.4f, \"tempo_octave_equivalent\": %.4f, \"tempo_err_pct_mean\": %.4f,\n", nAnalysis, analysed, (double) tempoOk / std::max (1, analysed), (double) tempoOctave / std::max (1, analysed), tempoErr.mean());
    std::fprintf (fs, "    \"key_exact\": %.4f, \"key_relative\": %.4f, \"key_fifth\": %.4f, \"key_exact_or_relative\": %.4f,\n", (double) keyExact / std::max (1, analysed), (double) keyRel / std::max (1, analysed), (double) keyFifth / std::max (1, analysed), (double) (keyExact + keyRel) / std::max (1, analysed));
    std::fprintf (fs, "    \"downbeat_within_50ms\": %.4f, \"downbeat_err_ms_median\": %.2f, \"downbeat_err_ms_p90\": %.2f,\n", (double) dbOk / std::max (1, analysed), dbErr.median(), dbErr.p90());
    std::fprintf (fs, "    \"boundary_precision\": %.4f, \"boundary_recall\": %.4f, \"boundary_f1\": %.4f,\n    \"analysis_seconds_mean\": %.3f\n  },\n", prec, rec, prec + rec > 0 ? 2 * prec * rec / (prec + rec) : 0.0, analysisSec.mean());
    std::fprintf (fs, "  \"stems\": { \"mixes\": %d, \"si_sdr_vocals_db_mean\": %.2f, \"si_sdr_vocals_db_sd\": %.2f, \"si_sdr_drums_db_mean\": %.2f, \"si_sdr_drums_db_sd\": %.2f, \"si_sdr_bass_db_mean\": %.2f, \"si_sdr_bass_db_sd\": %.2f, \"reconstruction_db_mean\": %.1f, \"seconds_per_audio_minute\": %.2f },\n",
                 nStem, sdrV.mean(), sdrV.sd(), sdrD.mean(), sdrD.sd(), sdrB.mean(), sdrB.sd(), recon.mean(), stemSec.mean());
    std::fprintf (fs, "  \"mashups\": {\n    \"trials\": %d, \"planned\": %d, \"rule_compliance\": %.4f,\n    \"plan_ms_mean\": %.2f, \"render_seconds_per_audio_minute\": %.2f,\n", trials, planned, planned ? (double) rulesAll / planned : 0.0, planMs.mean(), renderS.mean());
    std::fprintf (fs, "    \"rendered_tempo_err_bpm_mean\": %.4f, \"rendered_tempo_err_bpm_max\": %.4f,\n", tempoErrM.mean(), tempoErrM.v.empty() ? 0.0 : *std::max_element (tempoErrM.v.begin(), tempoErrM.v.end()));
    std::fprintf (fs, "    \"tempo_sd_across_windows_auralloy\": %.4f, \"tempo_sd_across_windows_baseline\": %.4f,\n    \"key_agreement_auralloy\": %.4f, \"key_agreement_baseline\": %.4f\n  }\n}\n", sdOurs.mean(), sdBase.mean(), keyOurs.mean(), keyBase.mean());
    std::fclose (fs);
    std::printf ("\ndone. summary in %s/summary.json\n", outDir.c_str());
    return 0;
}
