// Records the README demo: drives the real Auralloy UI (import -> analyse -> Forge -> play -> preset -> export)
// with synthetic songs and writes PNG frames + an ffmpeg concat list.
//   AuralloyDemo <output folder>
#include "../src/app/MainComponent.h"
#include "../tests/SynthSong.h"
#include <cstdio>

using namespace juce;

static void pump (int ms) { MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static void writeSong (const File& f, const testsong::Spec& sp)
{
    auto s = testsong::make (sp);
    f.deleteFile();
    std::unique_ptr<OutputStream> os = std::make_unique<FileOutputStream> (f);
    WavAudioFormat wav;
    auto w = wav.createWriterFor (os, AudioFormatWriterOptions {}.withSampleRate (sp.sr).withNumChannels (2).withBitsPerSample (16));
    const float* ch[2] = { s.l.data(), s.r.data() };
    w->writeFromFloatArrays (ch, 2, (int) s.l.size());
}

struct Recorder
{
    File dir;
    al::MainComponent* mc = nullptr;
    struct Frame { String file; double t; double speed; double forced; };
    std::vector<Frame> frames;
    double speed = 1.0;

    void grab (double minGapMs = 0.0, double forcedDur = -1.0)
    {
        const double now = Time::getMillisecondCounterHiRes();
        if (forcedDur < 0 && ! frames.empty() && now - frames.back().t < minGapMs) return;
        auto img = mc->createComponentSnapshot (mc->getLocalBounds(), true, 960.0f / 1440.0f);
        const String name = "frame_" + String ((int) frames.size()).paddedLeft ('0', 5) + ".png";
        PNGImageFormat png;
        FileOutputStream fos (dir.getChildFile (name));
        png.writeImageToStream (img, fos);
        frames.push_back ({ name, Time::getMillisecondCounterHiRes(), speed, forcedDur });
    }
    // keep capturing for `ms` of real time
    void hold (int ms, double gapMs = 90.0)
    {
        const double end = Time::getMillisecondCounterHiRes() + ms;
        while (Time::getMillisecondCounterHiRes() < end) { grab (gapMs); pump (15); }
    }
    void until (const std::function<bool()>& cond, int maxMs, double gapMs = 90.0)
    {
        const double end = Time::getMillisecondCounterHiRes() + maxMs;
        while (! cond() && Time::getMillisecondCounterHiRes() < end) { grab (gapMs); pump (15); }
    }
    void finish()
    {
        String list;
        for (size_t i = 0; i < frames.size(); ++i)
        {
            double d = i + 1 < frames.size() ? (frames[i + 1].t - frames[i].t) / 1000.0 / frames[i].speed : 1.5;
            d = std::min (1.4, std::max (0.04, d));
            if (frames[i].forced > 0) d = frames[i].forced;
            list << "file '" << frames[i].file << "'\nduration " << String (d, 3) << "\n";
        }
        if (! frames.empty()) list << "file '" << frames.back().file << "'\n";
        dir.getChildFile ("frames.txt").replaceWithText (list);
        std::printf ("recorded %d frames\n", (int) frames.size());
    }
};

int main (int argc, char** argv)
{
    setvbuf (stdout, nullptr, _IONBF, 0);
    ScopedJuceInitialiser_GUI gui;
    const File out (argc > 1 ? argv[1] : "demo");
    const File songs = out.getChildFile ("songs");
    const File frames = out.getChildFile ("frames");
    songs.createDirectory(); frames.createDirectory();
    for (auto& f : frames.findChildFiles (File::findFiles, false)) f.deleteFile();

    { testsong::Spec s; s.bpm = 100; s.root = 9; s.minor = true; s.vocal = true; s.seed = 1; writeSong (songs.getChildFile ("Midnight Drive.wav"), s); }
    { testsong::Spec s; s.bpm = 106; s.root = 0; s.minor = false; s.vocal = true; s.lead = 0.2; s.seed = 2; writeSong (songs.getChildFile ("Neon Rain.wav"), s); }
    { testsong::Spec s; s.bpm = 96; s.root = 2; s.minor = true; s.vocal = true; s.lead = 0.45; s.seed = 3; writeSong (songs.getChildFile ("Velvet Skyline.wav"), s); }

    al::Core core;
    core.reduceMotion = false;
    {
        al::MainComponent mc (core);
        mc.setBounds (0, 0, 1440, 920);
        mc.setVisible (true);
        Recorder rec;
        rec.dir = frames; rec.mc = &mc;

        // 1. intro, stepped deterministically at 20 fps
        rec.speed = 1.0;
        for (int i = 0; i <= 52; ++i) { mc.setSplashTime ((float) i * 0.05f); pump (5); rec.grab (0.0, 0.05); }
        mc.setSplashTime (2.6f);
        pump (50);
        // 2. empty studio, then the songs arrive
        rec.hold (1100, 120.0);
        core.addSongs ({ songs.getChildFile ("Midnight Drive.wav"), songs.getChildFile ("Neon Rain.wav"), songs.getChildFile ("Velvet Skyline.wav") });
        rec.speed = 1.6;
        rec.until ([&] { for (auto& s : core.library) if (s->state.load() < 2) return false; return true; }, 60000, 110.0);
        rec.speed = 1.0;
        rec.hold (1200, 120.0);
        // 3. Forge
        mc.setView (1);
        rec.hold (1500, 80.0);
        al::ForgeOptions fo;
        fo.mo.targetLengthSec = 150;
        core.startForge (fo);
        rec.speed = 5.0;                                              // time-lapse while it works
        rec.until ([&] { return core.forgeState() == 5 || core.forgeState() == 6; }, 240000, 450.0);
        rec.speed = 1.0;
        rec.hold (2200, 90.0);                                        // switches to the Studio by itself
        // 4. play the sequential mix
        core.seek (0.0);
        core.play();
        rec.hold (3200, 90.0);
        core.seek (core.timeline()->length() * 0.28);
        rec.hold (4200, 90.0);
        // 5. one-click look
        core.applyFactoryPreset (1);                                  // Slowed + Reverb
        rec.hold (3800, 90.0);
        core.pause();
        // 6. export dialog
        mc.showExport();
        rec.hold (1800, 120.0);
        rec.finish();
    }
    return 0;
}
