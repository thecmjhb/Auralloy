// Auralloy - application core: audio device, timeline playback, library, analysis/stem/forge jobs, editing, export, projects.
#pragma once
#include "Params.h"
#include "../engine/Timeline.h"
#include "../mashup/Mashup.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <set>

namespace al
{
// A headless AudioProcessor whose only job is to own the AudioProcessorValueTreeState
struct ParamHost final : public juce::AudioProcessor
{
    ParamHost() : juce::AudioProcessor (BusesProperties()) {}
    const juce::String getName() const override { return "Auralloy"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

// min/max overview of a source for drawing waveforms at any zoom
struct Peaks
{
    static constexpr int block = 128;
    std::vector<float> mn, mx;
    double sr = 44100.0;
};
std::shared_ptr<const Peaks> peaksFor (const std::shared_ptr<const Source>& s);

struct SongData
{
    int id = 0;
    juce::String name;
    juce::File file;
    std::shared_ptr<Source> mix;                  // 44.1 kHz
    Analysis an;
    StemSet stems;
    std::atomic<int> state { 0 };                 // 0 loading, 1 analysing, 2 ready, 3 failed
    std::atomic<int> stemState { 0 };             // 0 none, 1 working, 2 done, 3 failed
    std::atomic<float> progress { 0.0f };
    std::mutex work;
    juce::String error;
    bool inForge = true;                          // selected for the mashup
    int colour = 0;

    double seconds() const { return mix ? mix->seconds() : 0.0; }
    SongInfo info() const
    {
        SongInfo s; s.id = id; s.name = name.toStdString(); s.an = an; s.mix = mix; s.stems = stems;
        prepareSong (s);
        return s;
    }
};

enum ExportFormat { WAV16 = 0, WAV24, WAV32F, FLAC16, FLAC24, MP3_320, MP3_192 };
struct ExportSettings
{
    int format = WAV24;
    int sampleRate = 0;          // 0 = 44.1 kHz project rate
    bool normalize = true;
    bool dither = true;
    bool useRegion = true;
};

struct ForgeOptions
{
    MashupOptions mo;
    int finishPreset = -1;       // factory preset applied after forging (-1 = leave the sound alone)
};

class Core final : public juce::AudioIODeviceCallback,
                   public juce::ChangeBroadcaster,
                   private juce::ChangeListener,
                   private juce::ValueTree::Listener,
                   private juce::Timer
{
public:
    Core();
    ~Core() override;

    // ---- parameters ----
    ParamHost host;
    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;
    Snapshot snapshot() const;
    void setFloat (const char* id, float v);
    void setBool (const char* id, bool v);
    void applyValues (const std::map<juce::String, float>& values);
    void applyFactoryPreset (int index);
    juce::File dataFolder() const;
    juce::File presetsFolder() const;
    juce::Array<juce::File> userPresets() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& f);

    // ---- transport ----
    void play()  { cmd = 1; }
    void pause() { cmd = 2; }
    void stop()  { cmd = 3; }
    void togglePlay() { if (playing.load()) pause(); else play(); }
    void seek (double timelineSec) { seekReq = timelineSec; }
    double position() const { return uiPos.load(); }
    bool isPlaying() const { return playing.load(); }
    void setRegion (double a, double b) { regS = a; regE = b; }
    void clearRegion() { regS = 0.0; regE = 0.0; }
    double regionStart() const { return regS.load(); }
    double regionEnd() const { return regE.load(); }
    bool hasRegion() const { return regE.load() > regS.load() + 0.01; }

    // ---- timeline ----
    std::shared_ptr<const Timeline> timeline() const;
    Timeline editCopy() const;
    void commit (Timeline&& t);                               // new undo step
    void beginDrag();                                         // live edits (no undo step per mouse move)
    void preview (Timeline&& t);
    void endDrag();
    bool undoTimeline();
    bool redoTimeline();
    bool canUndoTimeline() const { return ! undoStack.empty(); }
    bool canRedoTimeline() const { return ! redoStack.empty(); }
    std::set<int> selection;                                  // selected clip ids (message thread)
    double snapBpm = 0, snapOffset = 0;                       // beat grid for snapping (0 = none)
    std::string forgeKey, forgeSummary;

    int addTrack (const juce::String& name, int role = -1);
    void removeTrack (int trackId);
    void setTrackProps (int trackId, float volDb, float pan, bool mute, bool solo, const juce::String& name);
    void splitAtPlayhead();
    void deleteSelection();
    void duplicateSelection();
    void setClipGain (int clipId, float db);
    void setClipFades (int clipId, double fin, double fout);
    void reverseClip (int clipId);
    void normalizeClip (int clipId);
    void transformClip (int clipId, double tempoRatio, double semitones);     // async
    void clearTimeline();

    // ---- library & analysis ----
    std::vector<std::shared_ptr<SongData>> library;
    void addSongs (const juce::Array<juce::File>& files);
    void removeSong (int id);
    std::shared_ptr<SongData> songById (int id) const;
    void placeSong (const std::shared_ptr<SongData>& s, double atSec);
    void splitStems (const std::shared_ptr<SongData>& s);
    bool ensureAnalysis (SongData& s);
    bool ensureStems (SongData& s);

    // ---- forge (auto mashup) ----
    void startForge (const ForgeOptions& fo);
    void cancelForge() { forgeCancel = true; }
    int forgeState() const { return forgeStateV.load(); }       // 0 idle, 1 analysing, 2 stems, 3 planning, 4 rendering, 5 done, 6 failed
    float forgeProgress() const { return forgeProg.load(); }
    juce::String forgeMessage() const;
    std::vector<std::string> forgeLog() const;

    // ---- export ----
    void startExport (const ExportSettings&, const juce::File& dest);
    void cancelExport() { exportCancel = true; }
    int exportState() const { return exportStateV.load(); }      // 0 idle, 1 running, 2 done, 3 failed, 4 cancelled
    float exportProgress() const { return exportProg.load(); }
    juce::String exportMessage() const;
    static juce::File findFfmpeg();
    juce::String suggestedExportName (const ExportSettings&) const;

    // ---- projects & session ----
    bool saveProject (const juce::File&);
    bool loadProject (const juce::File&);
    void saveSession();
    void restoreSession();
    bool dirty = false;
    bool reduceMotion = false;
    void setReduceMotion (bool b);

    // ---- audio device, meters, analyser ----
    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formats;
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f }, grDb { 0.0f }, bassLevel { 0.0f };
    void copyAnalyzer (float* dest, int n) const;
    double sampleRate() const { return deviceSr; }
    juce::String audioStatus;

    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { dirty = true; }
    void renderChunk (float* L, float* R, int n);
    void publish (std::shared_ptr<const Timeline> t);
    void notifyAsync();
    void runForge (ForgeOptions fo);
    std::unique_ptr<juce::XmlElement> makeProjectXml();
    void rebuildFromXml (std::unique_ptr<juce::XmlElement> xml);
    std::shared_ptr<Source> renderFromRecipe (const SongData& s, const Recipe& r);

    // audio-thread state
    Chain chain;
    std::array<std::atomic<float>*, F_COUNT> rawF {};
    std::array<std::atomic<float>*, B_COUNT> rawB {};
    std::shared_ptr<const Timeline> aTl;
    int lastVersion = -1;
    double deviceSr = 44100.0, tPos = 0.0;
    bool wantPlay = false, rewindPending = false;
    float playGain = 0.0f, prevVol = 0.9f, bassLp = 0.0f;
    std::array<float, 1024> bufL {}, bufR {};

    // shared
    mutable juce::SpinLock tlLock;
    std::shared_ptr<const Timeline> tlMain, retiredTl;
    std::atomic<int> tlVersion { 0 };
    std::vector<std::shared_ptr<const Timeline>> undoStack, redoStack;
    std::shared_ptr<const Timeline> dragBase;
    std::atomic<int> cmd { 0 };
    std::atomic<double> seekReq { -1.0 }, regS { 0.0 }, regE { 0.0 }, uiPos { 0.0 };
    std::atomic<bool> playing { false }, ended { false };
    std::array<float, 16384> fifo {};
    std::atomic<int> fifoW { 0 };

    int nextSongId = 1;
    mutable juce::CriticalSection textLock;
    juce::String forgeMsg, exportMsg;
    std::vector<std::string> lastLog;
    std::atomic<int> forgeStateV { 0 }, exportStateV { 0 };
    std::atomic<float> forgeProg { 0.0f }, exportProg { 0.0f };
    std::atomic<bool> forgeCancel { false }, exportCancel { false };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    juce::ThreadPool pool { 3 };
    juce::ApplicationProperties props;
    int tickCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Core)
};
} // namespace al
