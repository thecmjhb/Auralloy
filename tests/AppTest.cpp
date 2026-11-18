// Headless end-to-end test of Auralloy: library -> analysis -> forge -> edit -> export -> project, plus UI snapshots
#include "../src/app/MainComponent.h"
#include "SynthSong.h"
#include <cstdio>

using namespace juce;
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); ++failures; } else { std::printf ("ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static void pump (int ms) { MessageManager::getInstance()->runDispatchLoopUntil (ms); }
static bool waitFor (const std::function<bool()>& c, int ms) { for (int t = 0; t < ms; t += 25) { if (c()) return true; pump (25); } return c(); }

static void writeSong (const File& f, const testsong::Spec& sp)
{
    auto s = testsong::make (sp);
    f.deleteFile();
    auto os = std::make_unique<FileOutputStream> (f);
    std::unique_ptr<OutputStream> stream = std::move (os);
    WavAudioFormat wav;
    auto w = wav.createWriterFor (stream, AudioFormatWriterOptions {}.withSampleRate (sp.sr).withNumChannels (2).withBitsPerSample (16));
    const float* ch[2] = { s.l.data(), s.r.data() };
    w->writeFromFloatArrays (ch, 2, (int) s.l.size());
}

static void snapshot (Component& c, const File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    PNGImageFormat png; f.deleteFile();
    FileOutputStream fos (f);
    png.writeImageToStream (img, fos);
}

int main (int argc, char** argv)
{
    setvbuf (stdout, nullptr, _IONBF, 0);
    ScopedJuceInitialiser_GUI gui;
    const File dir (argc > 1 ? argv[1] : "C:/auralloy/tests/out");
    dir.createDirectory();

    { testsong::Spec s; s.bpm = 100; s.root = 9; s.minor = true; s.vocal = true; s.seed = 1; writeSong (dir.getChildFile ("Midnight Drive.wav"), s); }
    { testsong::Spec s; s.bpm = 106; s.root = 0; s.minor = false; s.vocal = false; s.lead = 0.2; s.seed = 2; writeSong (dir.getChildFile ("Neon Rain.wav"), s); }
    { testsong::Spec s; s.bpm = 96; s.root = 2; s.minor = true; s.vocal = true; s.lead = 0.45; s.seed = 3; writeSong (dir.getChildFile ("Velvet Skyline.wav"), s); }

    {
        al::Core core;
        const bool haveDevice = core.deviceManager.getCurrentAudioDevice() != nullptr;
        std::printf ("      audio device: %s\n", haveDevice ? core.deviceManager.getCurrentAudioDevice()->getName().toRawUTF8() : "none");

        core.addSongs ({ dir.getChildFile ("Midnight Drive.wav"), dir.getChildFile ("Neon Rain.wav"), dir.getChildFile ("Velvet Skyline.wav") });
        CHECK (core.library.size() == 3, "three songs in the library");
        CHECK (waitFor ([&] { for (auto& s : core.library) if (s->state.load() < 2) return false; return true; }, 60000), "all songs loaded and analysed");
        for (auto& s : core.library) std::printf ("      %-18s %.2f BPM  %s (%s)  %d bars\n", s->name.toRawUTF8(), s->an.bpm, s->an.keyName.c_str(), s->an.camelot.c_str(), s->an.numBars());
        CHECK (std::abs (core.library[0]->an.bpm - 100.0) < 0.3 && core.library[0]->an.keyMinor && core.library[0]->an.keyRoot == 9, "analysis of the first song is right");

        {
            al::MainComponent mc (core);
            mc.setBounds (0, 0, 1440, 920);
            mc.setVisible (true);
            pump (3300);                                                    // the intro is over by now
            core.placeSong (core.library[0], 0.0);
            pump (500);
            snapshot (mc, dir.getChildFile ("ui_studio_single.png"));
            mc.setView (1, false);
            pump (600);
            snapshot (mc, dir.getChildFile ("ui_forge_before.png"));
            core.clearTimeline();
        }

        // ---- forge ----
        al::ForgeOptions fo;
        fo.mo.targetLengthSec = 150;
        fo.finishPreset = 9;                                               // Mashup Glue
        core.startForge (fo);
        CHECK (waitFor ([&] { return core.forgeState() == 5 || core.forgeState() == 6; }, 240000) && core.forgeState() == 5, "forge finished (%s)", core.forgeMessage().toRawUTF8());
        pump (200);
        auto tl = core.timeline();
        int clips = 0; for (auto& t : tl->tracks) clips += (int) t.clips.size();
        std::printf ("      forged timeline: %zu tracks, %d clips, %.1f s, grid %.1f BPM, %s\n", tl->tracks.size(), clips, tl->length(), core.snapBpm, core.forgeSummary.c_str());
        CHECK (tl->tracks.size() >= 4 && clips >= 12 && tl->length() > 100, "mashup timeline built with role tracks");
        for (auto& l : core.forgeLog()) std::printf ("        %s\n", l.c_str());
        CHECK (core.snapshot().b[al::B_compOn] && core.snapshot().b[al::B_rvOn], "'Mashup Glue' finishing preset applied");

        // ---- realtime playback ----
        if (haveDevice)
        {
            core.play();
            waitFor ([&] { return core.isPlaying(); }, 2000);
            const double p0 = core.position();
            pump (1000);
            const double p1 = core.position();
            CHECK (p1 - p0 > 0.7 && p1 - p0 < 1.4, "timeline plays in real time (%.2f s in ~1 s)", p1 - p0);
            core.setFloat ("speed", 0.8f);
            pump (400);
            const double q0 = core.position();
            pump (1000);
            const double q1 = core.position();
            CHECK (q1 - q0 > 0.6 && q1 - q0 < 1.0, "slowed to 0.8x (%.2f s per second)", q1 - q0);
            core.setFloat ("speed", 1.0f);
            core.pause(); pump (200);
            CHECK (! core.isPlaying(), "pause works");
            core.stop();
        }

        // ---- editing ----
        auto count = [&] { int n = 0; for (auto& t : core.timeline()->tracks) n += (int) t.clips.size(); return n; };
        const int c0 = count();
        core.selection.clear(); core.selection.insert (tl->tracks[1].clips[0].id);
        core.seek (tl->tracks[1].clips[0].start + 1.0);
        pump (100);
        core.splitAtPlayhead();
        CHECK (count() == c0 + 1, "split adds a clip");
        core.undoTimeline();
        CHECK (count() == c0, "undo restores it");
        core.redoTimeline();
        CHECK (count() == c0 + 1, "redo works");
        core.selection.clear(); core.selection.insert (core.timeline()->tracks[1].clips.back().id);
        core.duplicateSelection();
        CHECK (count() == c0 + 2, "duplicate adds a clip");
        core.deleteSelection();
        CHECK (count() == c0 + 1, "delete removes the selection");
        core.undoTimeline(); core.undoTimeline(); core.undoTimeline();

        // clip edits
        const int cid = core.timeline()->tracks[0].clips[0].id;
        core.setClipGain (cid, -3.0f);
        CHECK (std::abs (core.timeline()->findClip (cid)->gainDb + 3.0f) < 1e-4f, "clip gain");
        core.reverseClip (cid);
        CHECK (core.timeline()->findClip (cid)->recipe.reversed, "reverse clip");
        core.reverseClip (cid);
        core.transformClip (cid, 1.25, 2.0);
        const double lenBefore = core.timeline()->findClip (cid)->length;
        waitFor ([&] { return std::abs (core.timeline()->findClip (cid)->length - lenBefore) > 0.01; }, 20000);
        std::printf ("      transform: clip length %.2f -> %.2f (expected %.2f)\n", lenBefore, core.timeline()->findClip (cid)->length, lenBefore / 1.25);
        CHECK (std::abs (core.timeline()->findClip (cid)->length - lenBefore / 1.25) < 0.15, "tempo/pitch transform of a clip");
        core.undoTimeline();

        // ---- export, then analyse the exported file: it must be at the planned tempo ----
        core.applyFactoryPreset (0);
        core.setFloat ("speed", 1.0f);
        const File out = dir.getChildFile ("forged.wav");
        out.deleteFile();
        al::ExportSettings es; es.format = al::WAV24; es.normalize = true;
        core.startExport (es, out);
        CHECK (waitFor ([&] { return core.exportState() >= 2; }, 120000) && core.exportState() == 2, "export finished");
        AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (out));
        CHECK (rd != nullptr && rd->bitsPerSample == 24, "24-bit WAV written");
        if (rd)
        {
            al::Source ex; ex.sampleRate = rd->sampleRate;
            AudioBuffer<float> buf (2, (int) rd->lengthInSamples);
            rd->read (&buf, 0, (int) rd->lengthInSamples, 0, true, true);
            ex.l.assign (buf.getReadPointer (0), buf.getReadPointer (0) + buf.getNumSamples());
            ex.r.assign (buf.getReadPointer (1), buf.getReadPointer (1) + buf.getNumSamples());
            auto an = al::analyze (ex);
            std::printf ("      exported file: %.1f s, analysed %.2f BPM, %s\n", ex.seconds(), an.bpm, an.keyName.c_str());
            CHECK (an.ok && std::abs (an.bpm - core.snapBpm) < 0.7, "the exported mashup really is at the target tempo");
        }

        // ---- project round trip ----
        const File proj = dir.getChildFile ("test.alloy");
        CHECK (core.saveProject (proj), "project saved");
        const int before = count();
        core.clearTimeline();
        core.library.clear();
        CHECK (core.loadProject (proj), "project loaded");
        CHECK (waitFor ([&] { return count() >= before; }, 120000), "timeline clips rebuilt from the recipes (%d of %d)", count(), before);
        proj.deleteFile();

        // ---- snapshots of the finished mashup ----
        {
            al::MainComponent mc (core);
            mc.setBounds (0, 0, 1440, 920);
            mc.setVisible (true);
            pump (3300);
            core.seek (22.0);
            core.setRegion (16.0, 40.0);
            core.selection.clear(); core.selection.insert (core.timeline()->tracks[1].clips[1].id);
            core.applyFactoryPreset (2);
            pump (700);
            snapshot (mc, dir.getChildFile ("ui_studio_mashup.png"));
            mc.setView (1, false);
            pump (800);
            snapshot (mc, dir.getChildFile ("ui_forge_after.png"));
            mc.setView (0, false);
            mc.showExport(); pump (500);
            snapshot (mc, dir.getChildFile ("ui_export.png"));
        }
        {
            al::SplashOverlay sp;
            sp.setBounds (0, 0, 1440, 920);
            sp.setVisible (true);
            pump (550);  snapshot (sp, dir.getChildFile ("ui_splash_1.png"));
            pump (750);  snapshot (sp, dir.getChildFile ("ui_splash_2.png"));
            pump (650);  snapshot (sp, dir.getChildFile ("ui_splash_3.png"));
        }
        std::printf ("      UI snapshots saved\n");
    }
    std::printf ("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL APP TESTS PASSED", failures);
    return failures;
}
