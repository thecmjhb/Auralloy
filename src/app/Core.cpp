#include "Core.h"

using namespace juce;

namespace al
{
//==============================================================================
std::shared_ptr<const Peaks> peaksFor (const std::shared_ptr<const Source>& s)
{
    static std::mutex m;
    static std::map<const Source*, std::pair<std::weak_ptr<const Source>, std::shared_ptr<const Peaks>>> cache;
    if (! s) return nullptr;
    std::lock_guard<std::mutex> lk (m);
    if (cache.size() > 96)
        for (auto it = cache.begin(); it != cache.end();) it = it->second.first.expired() ? cache.erase (it) : std::next (it);
    auto it = cache.find (s.get());
    if (it != cache.end() && it->second.first.lock() == s) return it->second.second;

    auto p = std::make_shared<Peaks>();
    p->sr = s->sampleRate;
    const long long n = (long long) s->frames();
    const long long nb = (n + Peaks::block - 1) / Peaks::block;
    p->mn.resize ((size_t) nb); p->mx.resize ((size_t) nb);
    for (long long b = 0; b < nb; ++b)
    {
        float mn = 1e9f, mx = -1e9f;
        const long long a = b * Peaks::block, e = std::min (n, a + Peaks::block);
        for (long long i = a; i < e; ++i) { const float v = 0.5f * (s->l[(size_t) i] + s->r[(size_t) i]); mn = std::min (mn, v); mx = std::max (mx, v); }
        if (mn > mx) mn = mx = 0.0f;
        p->mn[(size_t) b] = mn; p->mx[(size_t) b] = mx;
    }
    cache[s.get()] = { s, p };
    return p;
}

static AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    for (const auto& d : floatParams())
    {
        NormalisableRange<float> r (d.mn, d.mx, 0.0f);
        if (d.centre > d.mn && d.centre < d.mx) r.setSkewForCentre (d.centre);
        v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { d.id, 1 }, d.name, r, d.def));
    }
    for (const auto& d : boolParams())
        v.push_back (std::make_unique<AudioParameterBool> (ParameterID { d.id, 1 }, d.name, d.def));
    return { v.begin(), v.end() };
}

//==============================================================================
Core::Core() : apvts (host, &undo, "AURALLOY", makeLayout())
{
    jassert ((int) floatParams().size() == F_COUNT && (int) boolParams().size() == B_COUNT);
    for (int i = 0; i < F_COUNT; ++i) rawF[(size_t) i] = apvts.getRawParameterValue (floatParams()[(size_t) i].id);
    for (int i = 0; i < B_COUNT; ++i) rawB[(size_t) i] = apvts.getRawParameterValue (boolParams()[(size_t) i].id);

    formats.registerBasicFormats();
    undo.setMaxNumberOfStoredUnits (500, 30);
    tlMain = std::make_shared<const Timeline>();

    PropertiesFile::Options o;
    o.applicationName = "Auralloy";
    o.filenameSuffix = ".settings";
    o.folderName = "Auralloy";
    o.osxLibrarySubFolder = "Application Support";
    props.setStorageParameters (o);
    if (auto* us = props.getUserSettings()) reduceMotion = us->getBoolValue ("reduceMotion", false);

    std::unique_ptr<XmlElement> saved;
    if (auto* us = props.getUserSettings()) saved = us->getXmlValue ("audio");
    audioStatus = deviceManager.initialise (0, 2, saved.get(), true);
    deviceManager.addAudioCallback (this);
    deviceManager.addChangeListener (this);
    apvts.state.addListener (this);
    startTimerHz (10);
}

Core::~Core()
{
    stopTimer();
    alive->store (false);
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (this);
    forgeCancel = true; exportCancel = true;
    pool.removeAllJobs (true, 60000);
    saveSession();
    deviceManager.closeAudioDevice();
}

void Core::setReduceMotion (bool b)
{
    reduceMotion = b;
    if (auto* us = props.getUserSettings()) { us->setValue ("reduceMotion", b); props.saveIfNeeded(); }
    sendChangeMessage();
}

void Core::changeListenerCallback (ChangeBroadcaster* src)
{
    if (src == &deviceManager)
        if (auto xml = deviceManager.createStateXml())
            if (auto* us = props.getUserSettings()) { us->setValue ("audio", xml.get()); props.saveIfNeeded(); }
}

void Core::notifyAsync()
{
    auto a = alive;
    MessageManager::callAsync ([this, a] { if (a->load()) sendChangeMessage(); });
}

//==============================================================================
// parameters & presets
Snapshot Core::snapshot() const
{
    Snapshot s;
    for (int i = 0; i < F_COUNT; ++i) s.f[i] = rawF[(size_t) i]->load();
    for (int i = 0; i < B_COUNT; ++i) s.b[i] = rawB[(size_t) i]->load() > 0.5f;
    return s;
}

void Core::setFloat (const char* id, float v)
{
    if (auto* p = apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
}
void Core::setBool (const char* id, bool v) { setFloat (id, v ? 1.0f : 0.0f); }

void Core::applyValues (const std::map<String, float>& values)
{
    undo.beginNewTransaction ("Preset");
    for (const auto& d : floatParams())
    {
        if (isTransportParam (d.id)) continue;
        auto it = values.find (d.id);
        setFloat (d.id, it != values.end() ? it->second : d.def);
    }
    for (const auto& d : boolParams())
    {
        if (isTransportParam (d.id)) continue;
        auto it = values.find (d.id);
        setBool (d.id, it != values.end() ? it->second > 0.5f : d.def);
    }
    dirty = true;
}

void Core::applyFactoryPreset (int index)
{
    const auto& all = factoryPresets();
    if (index < 0 || index >= (int) all.size()) return;
    std::map<String, float> m;
    for (const auto& kv : all[(size_t) index].v) m[kv.first] = kv.second;
    applyValues (m);
}

File Core::dataFolder() const
{
    auto f = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Auralloy");
    f.createDirectory();
    return f;
}
File Core::presetsFolder() const { auto f = dataFolder().getChildFile ("Presets"); f.createDirectory(); return f; }

Array<File> Core::userPresets() const
{
    Array<File> r;
    for (const auto& e : RangedDirectoryIterator (presetsFolder(), false, "*.alpreset")) r.add (e.getFile());
    r.sort();
    return r;
}

bool Core::saveUserPreset (const String& name)
{
    XmlElement e ("AuralloyPreset");
    e.setAttribute ("name", name);
    const auto s = snapshot();
    for (int i = 0; i < F_COUNT; ++i)
        if (! isTransportParam (floatParams()[(size_t) i].id)) e.setAttribute (floatParams()[(size_t) i].id, (double) s.f[i]);
    for (int i = 0; i < B_COUNT; ++i)
        if (! isTransportParam (boolParams()[(size_t) i].id)) e.setAttribute (boolParams()[(size_t) i].id, s.b[i] ? 1 : 0);
    return e.writeTo (presetsFolder().getChildFile (File::createLegalFileName (name) + ".alpreset"));
}

bool Core::loadUserPreset (const File& f)
{
    auto xml = parseXML (f);
    if (xml == nullptr || ! xml->hasTagName ("AuralloyPreset")) return false;
    std::map<String, float> m;
    for (const auto& d : floatParams()) if (xml->hasAttribute (d.id)) m[d.id] = (float) xml->getDoubleAttribute (d.id);
    for (const auto& d : boolParams())  if (xml->hasAttribute (d.id)) m[d.id] = (float) xml->getIntAttribute (d.id);
    applyValues (m);
    return true;
}

//==============================================================================
// audio
void Core::audioDeviceAboutToStart (AudioIODevice* d)
{
    deviceSr = d != nullptr ? d->getCurrentSampleRate() : 44100.0;
    if (deviceSr < 8000.0) deviceSr = 44100.0;
    chain.prepare (deviceSr);
    lastVersion = -1;
}

void Core::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n, const AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr) FloatVectorOperations::clear (out[c], n);
    if (numOut < 1) return;
    ScopedNoDenormals nd;
    int done = 0;
    while (done < n)
    {
        const int m = std::min (1024, n - done);
        renderChunk (bufL.data(), bufR.data(), m);
        for (int c = 0; c < numOut; ++c)
            if (out[c] != nullptr) FloatVectorOperations::copy (out[c] + done, c == 0 ? bufL.data() : bufR.data(), m);
        done += m;
    }
}

void Core::renderChunk (float* L, float* R, int n)
{
    const int ver = tlVersion.load();
    if (ver != lastVersion)
    {
        const SpinLock::ScopedTryLockType sl (tlLock);
        if (sl.isLocked()) { aTl = tlMain; lastVersion = ver; }
    }

    const Snapshot s = snapshot();
    const MixParams mp = toMix (s);
    const double speed = std::max (0.05, (double) s.f[F_speed]);
    const double tlen = aTl ? aTl->length() : 0.0;
    const bool hasReg = regE.load() > regS.load() + 0.01;
    const double a = hasReg ? regS.load() : 0.0;
    const double b = hasReg ? regE.load() : tlen;

    switch (cmd.exchange (0))
    {
        case 1: if (tPos >= b - 0.02 || tPos < a - 0.001) tPos = a; wantPlay = true; rewindPending = false; break;
        case 2: wantPlay = false; rewindPending = false; break;
        case 3: wantPlay = false; rewindPending = true; break;
        default: break;
    }
    const double sk = seekReq.exchange (-1.0);          // a seek always wins over a pending stop/rewind
    if (sk >= 0.0) { tPos = std::min (std::max (0.0, sk), std::max (b, tlen)); playGain = 0.0f; rewindPending = false; }

    std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f);
    const double dt = speed / deviceSr;
    const float gStep = (float) (1.0 / (0.006 * deviceSr));
    int off = 0, guard = 0;
    while (off < n && aTl != nullptr && b > a && (wantPlay || playGain > 0.0f) && ++guard < 8)
    {
        const double toEnd = (b - tPos) / dt;
        if (toEnd < 1.0)
        {
            if (s.b[B_loop] && b > a + 0.05) { tPos = a; playGain = std::min (playGain, 0.0f); continue; }
            wantPlay = false; ended = true; tPos = a; playGain = 0.0f;
            break;
        }
        const int chunk = std::max (1, std::min (n - off, (int) std::floor (toEnd)));
        mixTimelineBlock (*aTl, tPos, dt, deviceSr, chunk, L + off, R + off);
        for (int i = 0; i < chunk; ++i)
        {
            playGain += wantPlay ? gStep : -gStep;
            playGain = std::min (1.0f, std::max (0.0f, playGain));
            float g = playGain;
            if (! s.b[B_loop]) g *= masterFade (tPos + (double) i * dt, a, b, s.f[F_fadeIn], s.f[F_fadeOut], speed);
            L[off + i] *= g; R[off + i] *= g;
        }
        tPos += (double) chunk * dt;
        off += chunk;
    }
    if (rewindPending && ! wantPlay && playGain <= 0.0f) { tPos = a; rewindPending = false; }

    chain.process (L, R, n, mp);

    const float vol = s.f[F_vol];
    float pkL = 0.0f, pkR = 0.0f, bass = 0.0f;
    const float bc = 1.0f - std::exp (-6.2831853f * 140.0f / (float) deviceSr);
    for (int i = 0; i < n; ++i)
    {
        const float v = prevVol + (vol - prevVol) * (float) (i + 1) / (float) n;
        L[i] *= v; R[i] *= v;
        pkL = std::max (pkL, std::fabs (L[i])); pkR = std::max (pkR, std::fabs (R[i]));
        const float mono = 0.5f * (L[i] + R[i]);
        bassLp += bc * (mono - bassLp);
        bass += bassLp * bassLp;
        const int w = fifoW.load (std::memory_order_relaxed);
        fifo[(size_t) (w & 16383)] = mono;
        fifoW.store (w + 1, std::memory_order_relaxed);
    }
    prevVol = vol;
    peakL.store (std::max (peakL.load(), pkL)); peakR.store (std::max (peakR.load(), pkR));
    bassLevel.store (std::sqrt (bass / (float) n));
    grDb.store (chain.lastGainReductionDb());
    uiPos = tPos;
    playing = wantPlay;
}

void Core::copyAnalyzer (float* dest, int n) const
{
    const int w = fifoW.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) dest[i] = fifo[(size_t) ((w - n + i) & 16383)];
}

//==============================================================================
// timeline & editing
void Core::publish (std::shared_ptr<const Timeline> t)
{
    {
        const SpinLock::ScopedLockType sl (tlLock);
        retiredTl = tlMain;
        tlMain = std::move (t);
        tlVersion++;
    }
    dirty = true;
    sendChangeMessage();
}

std::shared_ptr<const Timeline> Core::timeline() const { const SpinLock::ScopedLockType sl (tlLock); return tlMain; }
Timeline Core::editCopy() const { return *timeline(); }

void Core::commit (Timeline&& t)
{
    undoStack.push_back (timeline());
    if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
    redoStack.clear();
    publish (std::make_shared<const Timeline> (std::move (t)));
}

void Core::beginDrag() { dragBase = timeline(); }
void Core::preview (Timeline&& t) { publish (std::make_shared<const Timeline> (std::move (t))); }
void Core::endDrag()
{
    if (dragBase && dragBase != timeline())
    {
        undoStack.push_back (dragBase);
        if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
        redoStack.clear();
    }
    dragBase = nullptr;
    sendChangeMessage();
}

bool Core::undoTimeline()
{
    if (undoStack.empty()) return false;
    redoStack.push_back (timeline());
    auto t = undoStack.back(); undoStack.pop_back();
    publish (t);
    return true;
}
bool Core::redoTimeline()
{
    if (redoStack.empty()) return false;
    undoStack.push_back (timeline());
    auto t = redoStack.back(); redoStack.pop_back();
    publish (t);
    return true;
}

int Core::addTrack (const String& name, int role)
{
    Timeline t = editCopy();
    Track tr; tr.id = t.nextId++; tr.name = name.toStdString(); tr.role = role; tr.colour = (int) t.tracks.size() % 8;
    t.tracks.push_back (tr);
    const int id = tr.id;
    commit (std::move (t));
    return id;
}

void Core::removeTrack (int trackId)
{
    Timeline t = editCopy();
    t.tracks.erase (std::remove_if (t.tracks.begin(), t.tracks.end(), [&] (const Track& x) { return x.id == trackId; }), t.tracks.end());
    selection.clear();
    commit (std::move (t));
}

void Core::setTrackProps (int trackId, float volDb, float pan, bool mute, bool solo, const String& name)
{
    Timeline t = editCopy();
    for (auto& tr : t.tracks) if (tr.id == trackId) { tr.volDb = volDb; tr.pan = pan; tr.mute = mute; tr.solo = solo; tr.name = name.toStdString(); }
    commit (std::move (t));
}

void Core::splitAtPlayhead()
{
    const double pos = position();
    Timeline t = editCopy();
    bool any = false;
    for (auto& tr : t.tracks)
    {
        std::vector<Clip> add;
        for (auto& c : tr.clips)
        {
            const bool wanted = selection.empty() || selection.count (c.id);
            if (! wanted || pos <= c.start + 0.02 || pos >= c.end() - 0.02) continue;
            Clip b = c;
            b.id = t.nextId++;
            b.start = pos; b.offset = c.offset + (pos - c.start); b.length = c.end() - pos;
            b.fadeIn = 0.003;
            c.length = pos - c.start;
            c.fadeOut = 0.003;
            add.push_back (b); any = true;
        }
        for (auto& b : add) tr.clips.push_back (b);
    }
    if (any) commit (std::move (t));
}

void Core::deleteSelection()
{
    if (selection.empty()) return;
    Timeline t = editCopy();
    for (auto& tr : t.tracks)
        tr.clips.erase (std::remove_if (tr.clips.begin(), tr.clips.end(), [&] (const Clip& c) { return selection.count (c.id) > 0; }), tr.clips.end());
    selection.clear();
    commit (std::move (t));
}

void Core::duplicateSelection()
{
    if (selection.empty()) return;
    Timeline t = editCopy();
    std::set<int> fresh;
    for (auto& tr : t.tracks)
    {
        std::vector<Clip> add;
        for (const auto& c : tr.clips) if (selection.count (c.id)) { Clip d = c; d.id = t.nextId++; d.start = c.end(); add.push_back (d); fresh.insert (d.id); }
        for (auto& d : add) tr.clips.push_back (d);
    }
    selection = fresh;
    commit (std::move (t));
}

void Core::setClipGain (int clipId, float db)
{
    Timeline t = editCopy();
    if (auto* c = t.findClip (clipId)) c->gainDb = db;
    commit (std::move (t));
}
void Core::setClipFades (int clipId, double fin, double fout)
{
    Timeline t = editCopy();
    if (auto* c = t.findClip (clipId)) { c->fadeIn = std::max (0.0, std::min (fin, c->length)); c->fadeOut = std::max (0.0, std::min (fout, c->length)); }
    commit (std::move (t));
}

void Core::reverseClip (int clipId)
{
    Timeline t = editCopy();
    auto* c = t.findClip (clipId);
    if (c == nullptr || ! c->src) return;
    const double total = c->src->seconds();
    c->src = reverseSource (*c->src);
    c->offset = std::max (0.0, total - (c->offset + c->length));
    c->recipe.reversed = ! c->recipe.reversed;
    std::swap (c->fadeIn, c->fadeOut);
    commit (std::move (t));
}

void Core::normalizeClip (int clipId)
{
    Timeline t = editCopy();
    auto* c = t.findClip (clipId);
    if (c == nullptr || ! c->src) return;
    const long long a = (long long) (c->offset * c->src->sampleRate), e = std::min<long long> ((long long) c->src->frames(), a + (long long) (c->length * c->src->sampleRate));
    float pk = 0.0f;
    for (long long i = std::max<long long> (0, a); i < e; ++i) pk = std::max (pk, std::max (std::fabs (c->src->l[(size_t) i]), std::fabs (c->src->r[(size_t) i])));
    if (pk > 1e-5f) c->gainDb = -1.0f - 20.0f * std::log10 (pk);
    commit (std::move (t));
}

void Core::clearTimeline()
{
    Timeline t;
    selection.clear();
    commit (std::move (t));
    clearRegion();
}

void Core::transformClip (int clipId, double tempoRatio, double semitones)
{
    auto tl = timeline();
    const Clip* c0 = tl->findClip (clipId);
    if (c0 == nullptr || ! c0->src) return;
    const Clip clip = *c0;
    auto a = alive;
    pool.addJob ([this, clip, tempoRatio, semitones, a]
    {
        auto slice = sliceSource (*clip.src, clip.offset, clip.offset + clip.length);
        auto res = timePitch (*slice, tempoRatio, semitones);
        if (! res || ! a->load()) return;
        MessageManager::callAsync ([this, a, clip, res, tempoRatio, semitones]
        {
            if (! a->load()) return;
            Timeline t = editCopy();
            auto* c = t.findClip (clip.id);
            if (c == nullptr) return;
            // express the transform on the original song so the audio can be rebuilt from the recipe
            if (c->recipe.valid)
            {
                const double tr0 = c->recipe.tempoRatio;
                const double s0 = c->recipe.srcStart, s1 = c->recipe.srcEnd;
                double ns, ne;
                if (! c->recipe.reversed) { ns = s0 + clip.offset * tr0; ne = s0 + (clip.offset + clip.length) * tr0; }
                else                      { ne = s1 - clip.offset * tr0; ns = s1 - (clip.offset + clip.length) * tr0; }
                c->recipe.srcStart = ns; c->recipe.srcEnd = ne;
                c->recipe.tempoRatio = tr0 * tempoRatio;
                c->recipe.semitones += semitones;
            }
            c->src = res; c->offset = 0.0; c->length = res->seconds();
            c->fadeIn = std::min (c->fadeIn, c->length); c->fadeOut = std::min (c->fadeOut, c->length);
            commit (std::move (t));
        });
    });
}

//==============================================================================
// library & analysis
std::shared_ptr<SongData> Core::songById (int id) const
{
    for (const auto& s : library) if (s->id == id) return s;
    return nullptr;
}

bool Core::ensureAnalysis (SongData& s)
{
    std::lock_guard<std::mutex> lk (s.work);
    if (s.an.ok) return true;
    if (! s.mix) return false;
    s.an = analyze (*s.mix);
    return s.an.ok;
}

bool Core::ensureStems (SongData& s)
{
    std::lock_guard<std::mutex> lk (s.work);
    if (s.stems.ok()) return true;
    if (! s.mix) return false;
    s.stemState = 1; s.progress = 0.0f;
    s.stems = separateStems (*s.mix, nullptr, &s.progress);
    s.stemState = s.stems.ok() ? 2 : 3;
    return s.stems.ok();
}

void Core::addSongs (const Array<File>& files)
{
    for (const auto& f : files)
    {
        if (! f.existsAsFile()) continue;
        bool dup = false;
        for (const auto& s : library) if (s->file == f) { dup = true; break; }
        if (dup) continue;
        auto sd = std::make_shared<SongData>();
        sd->id = nextSongId++;
        sd->name = f.getFileNameWithoutExtension();
        sd->file = f;
        sd->colour = (int) library.size() % 8;
        library.push_back (sd);
        auto a = alive;
        pool.addJob ([this, sd, f, a]
        {
            std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (f));
            if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0) { sd->error = "Could not read this file"; sd->state = 3; if (a->load()) notifyAsync(); return; }
            if (reader->lengthInSamples > (int64) (reader->sampleRate * 60.0 * 40.0)) { sd->error = "Longer than 40 minutes"; sd->state = 3; if (a->load()) notifyAsync(); return; }
            const int len = (int) reader->lengthInSamples;
            const int chans = (int) std::min (2u, reader->numChannels);
            AudioBuffer<float> buf (chans, len);
            reader->read (&buf, 0, len, 0, true, chans > 1);
            auto raw = std::make_shared<Source>();
            raw->sampleRate = reader->sampleRate;
            raw->l.assign (buf.getReadPointer (0), buf.getReadPointer (0) + len);
            if (chans > 1) raw->r.assign (buf.getReadPointer (1), buf.getReadPointer (1) + len); else raw->r = raw->l;
            if (std::abs (raw->sampleRate - kProjectRate) > 0.5) { sd->mix = resampleSinc (*raw, raw->sampleRate / kProjectRate, kProjectRate); sd->mix->sampleRate = kProjectRate; }
            else sd->mix = raw;
            sd->mix->stretch = 1.0;
            sd->state = 1;
            if (a->load()) notifyAsync();
            ensureAnalysis (*sd);
            sd->state = 2;
            if (a->load()) notifyAsync();
        });
    }
    sendChangeMessage();
}

void Core::removeSong (int id)
{
    library.erase (std::remove_if (library.begin(), library.end(), [&] (const std::shared_ptr<SongData>& s) { return s->id == id; }), library.end());
    sendChangeMessage();
}

void Core::placeSong (const std::shared_ptr<SongData>& s, double atSec)
{
    if (! s || ! s->mix) return;
    Timeline t = editCopy();
    Track tr; tr.id = t.nextId++; tr.name = s->name.toStdString(); tr.colour = s->colour;
    Clip c; c.id = t.nextId++; c.src = s->mix; c.start = std::max (0.0, atSec); c.offset = 0; c.length = s->mix->seconds();
    c.colour = s->colour; c.name = s->name.toStdString(); c.songId = s->id;
    c.recipe = { true, s->id, 4, 0.0, c.length, 1.0, 0.0, false };
    tr.clips.push_back (c);
    t.tracks.push_back (tr);
    if (t.tracks.size() == 1 && s->an.ok)
    {
        snapBpm = s->an.bpm; snapOffset = s->an.downbeats.empty() ? 0.0 : s->an.downbeats.front();
    }
    selection = { c.id };
    commit (std::move (t));
}

void Core::splitStems (const std::shared_ptr<SongData>& s)
{
    if (! s || s->stemState.load() == 1) return;
    auto a = alive;
    pool.addJob ([this, s, a] { ensureStems (*s); if (a->load()) notifyAsync(); });
}

//==============================================================================
// forge
String Core::forgeMessage() const { const ScopedLock sl (textLock); return forgeMsg; }
std::vector<std::string> Core::forgeLog() const { const ScopedLock sl (textLock); return lastLog; }
String Core::exportMessage() const { const ScopedLock sl (textLock); return exportMsg; }

void Core::startForge (const ForgeOptions& fo)
{
    if (forgeStateV.load() >= 1 && forgeStateV.load() <= 4) return;
    forgeCancel = false; forgeStateV = 1; forgeProg = 0.0f;
    { const ScopedLock sl (textLock); forgeMsg = "Reading the songs..."; lastLog.clear(); }
    sendChangeMessage();
    auto a = alive;
    pool.addJob ([this, fo, a] { runForge (fo); if (a->load()) notifyAsync(); });
}

void Core::runForge (ForgeOptions fo)
{
    auto setMsg = [&] (const String& m) { const ScopedLock sl (textLock); forgeMsg = m; };
    auto fail = [&] (const String& m) { setMsg (m); forgeStateV = 6; };

    std::vector<std::shared_ptr<SongData>> sel;
    for (const auto& s : library) if (s->inForge && s->state.load() != 3) sel.push_back (s);
    if (sel.size() < 2) { fail ("Pick at least two songs in the library first."); return; }

    // wait for decoding / analysis
    for (auto& s : sel)
        while (s->state.load() < 2 && ! forgeCancel.load()) Thread::sleep (40);
    if (forgeCancel.load()) { fail ("Cancelled"); return; }

    forgeStateV = 1;
    for (size_t i = 0; i < sel.size(); ++i)
    {
        setMsg ("Analysing \"" + sel[i]->name + "\"...");
        ensureAnalysis (*sel[i]);
        forgeProg = 0.12f * (float) (i + 1) / (float) sel.size();
    }
    if (fo.mo.useStems)
    {
        forgeStateV = 2;
        for (size_t i = 0; i < sel.size(); ++i)
        {
            setMsg ("Splitting stems of \"" + sel[i]->name + "\"...");
            auto song = sel[i];
            // follow the stem progress of the song being separated
            std::atomic<bool> doneFlag { false };
            std::thread watcher ([&, song] { while (! doneFlag.load()) { forgeProg = 0.12f + 0.4f * ((float) i + song->progress.load()) / (float) sel.size(); Thread::sleep (60); } });
            ensureStems (*song);
            doneFlag = true; watcher.join();
            if (forgeCancel.load()) { fail ("Cancelled"); return; }
        }
    }

    forgeStateV = 3; setMsg ("Planning the arrangement...");
    std::vector<SongInfo> infos;
    for (auto& s : sel) infos.push_back (s->info());
    MashupPlan plan = planMashup (infos, fo.mo);
    { const ScopedLock sl (textLock); lastLog = plan.log; }
    if (! plan.ok) { fail (String (plan.error)); return; }
    forgeProg = 0.55f;

    forgeStateV = 4;
    auto tl = std::make_shared<Timeline>();
    const char* roleNames[5] = { "Vocals", "Drums", "Bass", "Other", "Mix" };
    int trackOfRole[5] = { -1, -1, -1, -1, -1 };
    auto trackFor = [&] (int role)
    {
        if (trackOfRole[role] < 0)
        {
            Track t; t.id = tl->nextId++; t.name = roleNames[role]; t.role = role; t.colour = role;
            tl->tracks.push_back (t); trackOfRole[role] = (int) tl->tracks.size() - 1;
        }
        return trackOfRole[role];
    };
    // fixed visual order: vocals on top
    for (int role : { 0, 1, 2, 3, 4 })
        for (const auto& c : plan.clips) if (c.role == role) { trackFor (role); break; }

    std::map<int, const SongInfo*> byId; for (auto& s : infos) byId[s.id] = &s;
    std::map<int, std::shared_ptr<SongData>> dataById; for (auto& s : sel) dataById[s->id] = s;
    // sequential mixes keep each song's own balance and only match the songs' overall level; layered mixes balance per role
    const bool layered = fo.mo.useStems && fo.mo.mode == MashupOptions::VocalOverBeat;
    std::map<int, double> songGain;
    if (! layered)
        for (auto& s : sel)
        {
            const float meas = s->mix ? activeRmsDb (*s->mix, 0, (long long) s->mix->frames()) : -90.0f;
            songGain[s->id] = meas > -80.0f ? std::max (-9.0f, std::min (9.0f, -20.0f - meas)) : 0.0;
        }
    for (size_t i = 0; i < plan.clips.size(); ++i)
    {
        if (forgeCancel.load()) { fail ("Cancelled"); return; }
        const auto& c = plan.clips[i];
        setMsg ("Rendering clip " + String ((int) i + 1) + " of " + String ((int) plan.clips.size()) + "...");
        double g = 0.0;
        auto buf = renderClipBuffer (*byId[c.songId], c, &g, layered);
        if (! buf) continue;
        Clip clip;
        clip.id = tl->nextId++; clip.src = buf; clip.start = c.tStart; clip.offset = 0.0; clip.length = buf->seconds();
        clip.gainDb = (float) (c.gainDb + (layered ? g : songGain[c.songId])); clip.fadeIn = c.fadeIn; clip.fadeOut = c.fadeOut;
        clip.songId = c.songId; clip.role = c.role; clip.colour = dataById[c.songId]->colour;
        clip.name = c.label;
        clip.recipe = { true, c.songId, c.role, c.srcStart, c.srcEnd, c.tempoRatio, c.semitones, false };
        tl->tracks[(size_t) trackOfRole[c.role]].clips.push_back (clip);
        forgeProg = 0.55f + 0.45f * (float) (i + 1) / (float) plan.clips.size();
    }

    auto a = alive;
    const double bpm = plan.bpm;
    const std::string key = plan.keyName + " (" + plan.camelot + ")";
    const double len = plan.lengthSec;
    MessageManager::callAsync ([this, a, tl, bpm, key, len, fo]
    {
        if (! a->load()) return;
        Timeline t = *tl;
        selection.clear();
        snapBpm = bpm; snapOffset = 0.0;
        forgeKey = key;
        forgeSummary = String ((int) std::round (bpm)).toStdString() + " BPM  |  " + key;
        commit (std::move (t));
        clearRegion(); seek (0.0);
        if (fo.finishPreset >= 0) applyFactoryPreset (fo.finishPreset);
        (void) len;
        forgeStateV = 5;
        sendChangeMessage();
    });
    setMsg ("Done");
}

//==============================================================================
// export
File Core::findFfmpeg()
{
    static File cached;
    static bool searched = false;
    if (searched) return cached;
    searched = true;
    auto test = [&] (const File& f) { if (f.existsAsFile()) { cached = f; return true; } return false; };
    if (test (File::getSpecialLocation (File::currentApplicationFile).getSiblingFile ("ffmpeg.exe"))) return cached;
    StringArray dirs;
    dirs.addTokens (SystemStats::getEnvironmentVariable ("PATH", {}), ";", "\"");
    for (const auto& d : dirs)
        if (d.isNotEmpty() && test (File (d).getChildFile ("ffmpeg.exe"))) return cached;
    test (File::getSpecialLocation (File::userHomeDirectory).getChildFile ("AppData/Local/Microsoft/WinGet/Links/ffmpeg.exe"));
    return cached;
}

String Core::suggestedExportName (const ExportSettings& es) const
{
    const auto s = snapshot();
    String base = "Auralloy mix";
    if (! forgeSummary.empty()) base = "Auralloy mashup";
    else if (! library.empty()) base = library.front()->name;
    StringArray parts;
    if (s.f[F_speed] < 0.97f) parts.add ("slowed"); else if (s.f[F_speed] > 1.03f) parts.add ("sped up");
    if (! s.b[B_bypass])
    {
        if (s.b[B_rvOn] && s.f[F_rvMix] > 0.05f) parts.add ("reverb");
        if (s.b[B_lofiOn] || (s.b[B_eqOn] && s.f[F_lpf] < 9000.0f) || s.f[F_vinyl] > 0.02f || s.f[F_rain] > 0.02f) parts.add ("lofi");
    }
    const String ext = es.format >= MP3_320 ? ".mp3" : (es.format >= FLAC16 ? ".flac" : ".wav");
    return File::createLegalFileName (base + (parts.isEmpty() ? String() : " (" + parts.joinIntoString (" + ") + ")")) + ext;
}

void Core::startExport (const ExportSettings& es, const File& dest)
{
    if (exportStateV.load() == 1) return;
    auto tl = timeline();
    if (tl->empty()) return;
    const Snapshot snap = snapshot();
    const double rs = regS.load(), re = regE.load();
    exportCancel = false; exportProg = 0.0f; exportStateV = 1;
    { const ScopedLock sl (textLock); exportMsg = dest.getFileName(); }
    auto a = alive;

    pool.addJob ([this, es, dest, tl, snap, rs, re, a]
    {
        auto fail = [&] (const String& why) { { const ScopedLock sl (textLock); exportMsg = why; } exportStateV = 3; if (a->load()) notifyAsync(); };
        const MixParams mp = toMix (snap);
        const double outSr = es.sampleRate > 0 ? (double) es.sampleRate : kProjectRate;
        const bool region = es.useRegion && re > rs + 0.01;
        std::vector<float> L, R;
        const bool ok = renderTimeline (*tl, mp, std::max (0.05, (double) snap.f[F_speed]), region ? rs : 0.0, region ? re : 0.0,
                                        snap.f[F_fadeIn], snap.f[F_fadeOut], outSr, L, R,
                                        [this] (float p) { exportProg = 0.9f * p; return ! exportCancel.load(); });
        if (! ok) { exportStateV = exportCancel.load() ? 4 : 3; if (a->load()) notifyAsync(); return; }

        if (es.normalize)
        {
            float pk = 0.0f;
            for (size_t i = 0; i < L.size(); ++i) pk = std::max (pk, std::max (std::fabs (L[i]), std::fabs (R[i])));
            if (pk > 1e-5f) { const float g = dbToLin (-1.0f) / pk; for (size_t i = 0; i < L.size(); ++i) { L[i] *= g; R[i] *= g; } }
        }
        const bool is16 = es.format == WAV16 || es.format == FLAC16;
        if (is16 && es.dither)
        {
            Rng rng;
            for (size_t i = 0; i < L.size(); ++i)
            {
                L[i] = std::min (1.0f, std::max (-1.0f, L[i] + (rng.uni() - rng.uni()) / 32768.0f));
                R[i] = std::min (1.0f, std::max (-1.0f, R[i] + (rng.uni() - rng.uni()) / 32768.0f));
            }
        }
        exportProg = 0.92f;

        const bool mp3 = es.format >= MP3_320;
        File target = mp3 ? File::createTempFile (".wav") : dest;
        target.deleteFile();
        auto fos = std::make_unique<FileOutputStream> (target);
        if (fos->failedToOpen()) { fail ("Cannot write to " + target.getFullPathName()); return; }
        std::unique_ptr<OutputStream> os = std::move (fos);
        AudioFormatWriterOptions opt;
        opt = opt.withSampleRate (outSr).withNumChannels (2);
        std::unique_ptr<AudioFormatWriter> writer;
        if (es.format == WAV16)       { WavAudioFormat f;  writer = f.createWriterFor (os, opt.withBitsPerSample (16)); }
        else if (es.format == WAV32F) { WavAudioFormat f;  writer = f.createWriterFor (os, opt.withBitsPerSample (32).withSampleFormat (AudioFormatWriterOptions::SampleFormat::floatingPoint)); }
        else if (es.format == FLAC16) { FlacAudioFormat f; writer = f.createWriterFor (os, opt.withBitsPerSample (16)); }
        else if (es.format == FLAC24) { FlacAudioFormat f; writer = f.createWriterFor (os, opt.withBitsPerSample (24)); }
        else                          { WavAudioFormat f;  writer = f.createWriterFor (os, opt.withBitsPerSample (24)); }
        if (writer == nullptr) { fail ("Could not create the audio file writer"); return; }
        const int block = 8192;
        for (size_t pos = 0; pos < L.size(); pos += (size_t) block)
        {
            const int cnt = (int) std::min ((size_t) block, L.size() - pos);
            const float* ch[2] = { L.data() + pos, R.data() + pos };
            writer->writeFromFloatArrays (ch, 2, cnt);
            if (exportCancel.load()) break;
            exportProg = 0.92f + 0.07f * (float) pos / (float) std::max<size_t> (1, L.size());
        }
        writer.reset();
        if (exportCancel.load()) { target.deleteFile(); exportStateV = 4; if (a->load()) notifyAsync(); return; }
        if (mp3)
        {
            const File ff = findFfmpeg();
            if (! ff.existsAsFile()) { target.deleteFile(); fail ("ffmpeg.exe was not found (needed for MP3)"); return; }
            dest.deleteFile();
            ChildProcess proc;
            StringArray args { ff.getFullPathName(), "-y", "-hide_banner", "-loglevel", "error", "-i", target.getFullPathName(),
                               "-vn", "-codec:a", "libmp3lame", "-b:a", es.format == MP3_320 ? "320k" : "192k", dest.getFullPathName() };
            if (! proc.start (args)) { target.deleteFile(); fail ("Could not start ffmpeg"); return; }
            proc.waitForProcessToFinish (180000);
            target.deleteFile();
            if (! dest.existsAsFile()) { fail ("MP3 encoding failed"); return; }
        }
        { const ScopedLock sl (textLock); exportMsg = dest.getFullPathName(); }
        exportProg = 1.0f; exportStateV = 2;
        if (a->load()) notifyAsync();
    });
}

//==============================================================================
// projects & session
std::unique_ptr<XmlElement> Core::makeProjectXml()
{
    auto root = std::make_unique<XmlElement> ("AuralloyProject");
    root->setAttribute ("version", 1);
    if (auto st = apvts.copyState().createXml()) root->addChildElement (st.release());
    auto* lib = root->createNewChildElement ("Library");
    for (const auto& s : library)
    {
        auto* e = lib->createNewChildElement ("Song");
        e->setAttribute ("path", s->file.getFullPathName());
        e->setAttribute ("id", s->id);
    }
    auto tl = timeline();
    auto* te = root->createNewChildElement ("Timeline");
    te->setAttribute ("snapBpm", snapBpm); te->setAttribute ("snapOffset", snapOffset);
    te->setAttribute ("summary", String (forgeSummary));
    for (const auto& t : tl->tracks)
    {
        auto* tr = te->createNewChildElement ("Track");
        tr->setAttribute ("name", String (t.name)); tr->setAttribute ("role", t.role); tr->setAttribute ("colour", t.colour);
        tr->setAttribute ("vol", (double) t.volDb); tr->setAttribute ("pan", (double) t.pan);
        tr->setAttribute ("mute", t.mute ? 1 : 0); tr->setAttribute ("solo", t.solo ? 1 : 0);
        for (const auto& c : t.clips)
        {
            auto* ce = tr->createNewChildElement ("Clip");
            ce->setAttribute ("start", c.start); ce->setAttribute ("offset", c.offset); ce->setAttribute ("length", c.length);
            ce->setAttribute ("gain", (double) c.gainDb); ce->setAttribute ("fin", c.fadeIn); ce->setAttribute ("fout", c.fadeOut);
            ce->setAttribute ("colour", c.colour); ce->setAttribute ("name", String (c.name));
            ce->setAttribute ("valid", c.recipe.valid ? 1 : 0);
            ce->setAttribute ("song", c.recipe.songId); ce->setAttribute ("role", c.recipe.role);
            ce->setAttribute ("s0", c.recipe.srcStart); ce->setAttribute ("s1", c.recipe.srcEnd);
            ce->setAttribute ("tempo", c.recipe.tempoRatio); ce->setAttribute ("semis", c.recipe.semitones);
            ce->setAttribute ("rev", c.recipe.reversed ? 1 : 0);
        }
    }
    auto* reg = root->createNewChildElement ("Region");
    reg->setAttribute ("start", regionStart()); reg->setAttribute ("end", regionEnd());
    return root;
}

bool Core::saveProject (const File& f) { return makeProjectXml()->writeTo (f.withFileExtension ("alloy")); }

bool Core::loadProject (const File& f)
{
    auto xml = parseXML (f);
    if (xml == nullptr || ! xml->hasTagName ("AuralloyProject")) return false;
    if (auto* st = xml->getChildByName (apvts.state.getType())) apvts.replaceState (ValueTree::fromXml (*st));
    library.clear(); selection.clear();
    Array<File> files;
    std::map<int, String> oldPath;
    if (auto* lib = xml->getChildByName ("Library"))
        for (auto* e : lib->getChildWithTagNameIterator ("Song")) { files.add (File (e->getStringAttribute ("path"))); oldPath[e->getIntAttribute ("id")] = e->getStringAttribute ("path"); }
    addSongs (files);
    if (auto* reg = xml->getChildByName ("Region")) setRegion (reg->getDoubleAttribute ("start"), reg->getDoubleAttribute ("end"));
    rebuildFromXml (std::move (xml));
    return true;
}

std::shared_ptr<Source> Core::renderFromRecipe (const SongData& s, const Recipe& r)
{
    SongInfo si; si.id = s.id; si.mix = s.mix; si.stems = s.stems;
    ClipPlan cp; cp.songId = s.id; cp.role = r.role; cp.srcStart = r.srcStart; cp.srcEnd = r.srcEnd; cp.tempoRatio = r.tempoRatio; cp.semitones = r.semitones;
    std::shared_ptr<Source> buf;
    if (r.role == RoleMix && std::abs (r.tempoRatio - 1.0) < 1e-9 && std::abs (r.semitones) < 1e-9 && r.srcStart <= 0.0 && r.srcEnd >= s.seconds() - 0.01)
        buf = std::make_shared<Source> (*s.mix);
    else
        buf = renderClipBuffer (si, cp, nullptr, false);
    if (buf && r.reversed) buf = reverseSource (*buf);
    return buf;
}

void Core::rebuildFromXml (std::unique_ptr<XmlElement> xml)
{
    auto a = alive;
    std::shared_ptr<XmlElement> shared (xml.release());
    // map recorded song ids to the freshly created library entries (same order as in the file)
    std::vector<int> newIds;
    for (const auto& s : library) newIds.push_back (s->id);
    pool.addJob ([this, shared, a, newIds]
    {
        auto* te = shared->getChildByName ("Timeline");
        auto* lib = shared->getChildByName ("Library");
        if (te == nullptr || lib == nullptr) return;
        std::map<int, int> idMap;
        int k = 0;
        for (auto* e : lib->getChildWithTagNameIterator ("Song")) { if (k < (int) newIds.size()) idMap[e->getIntAttribute ("id")] = newIds[(size_t) k]; ++k; }
        auto tl = std::make_shared<Timeline>();
        std::map<String, std::shared_ptr<Source>> cache;
        for (auto* tr : te->getChildWithTagNameIterator ("Track"))
        {
            Track t; t.id = tl->nextId++; t.name = tr->getStringAttribute ("name").toStdString(); t.role = tr->getIntAttribute ("role"); t.colour = tr->getIntAttribute ("colour");
            t.volDb = (float) tr->getDoubleAttribute ("vol"); t.pan = (float) tr->getDoubleAttribute ("pan"); t.mute = tr->getIntAttribute ("mute") != 0; t.solo = tr->getIntAttribute ("solo") != 0;
            for (auto* ce : tr->getChildWithTagNameIterator ("Clip"))
            {
                if (! a->load()) return;
                Clip c; c.id = tl->nextId++;
                c.start = ce->getDoubleAttribute ("start"); c.offset = ce->getDoubleAttribute ("offset"); c.length = ce->getDoubleAttribute ("length");
                c.gainDb = (float) ce->getDoubleAttribute ("gain"); c.fadeIn = ce->getDoubleAttribute ("fin"); c.fadeOut = ce->getDoubleAttribute ("fout");
                c.colour = ce->getIntAttribute ("colour"); c.name = ce->getStringAttribute ("name").toStdString();
                Recipe r; r.valid = ce->getIntAttribute ("valid") != 0; r.songId = idMap.count (ce->getIntAttribute ("song")) ? idMap[ce->getIntAttribute ("song")] : -1;
                r.role = ce->getIntAttribute ("role"); r.srcStart = ce->getDoubleAttribute ("s0"); r.srcEnd = ce->getDoubleAttribute ("s1");
                r.tempoRatio = ce->getDoubleAttribute ("tempo", 1.0); r.semitones = ce->getDoubleAttribute ("semis"); r.reversed = ce->getIntAttribute ("rev") != 0;
                c.recipe = r; c.songId = r.songId; c.role = r.role;
                if (r.valid)
                {
                    const String key = String (r.songId) + "|" + String (r.role) + "|" + String (r.srcStart, 4) + "|" + String (r.srcEnd, 4) + "|" + String (r.tempoRatio, 6)
                                     + "|" + String (r.semitones, 3) + "|" + String ((int) r.reversed);
                    auto it = cache.find (key);
                    if (it == cache.end())
                    {
                        std::shared_ptr<Source> buf;
                        if (auto song = songById (r.songId))
                        {
                            while (song->state.load() < 2) Thread::sleep (40);
                            if (song->mix)
                            {
                                if (r.role < 4) ensureStems (*song);
                                buf = renderFromRecipe (*song, r);
                            }
                        }
                        it = cache.emplace (key, buf).first;
                    }
                    c.src = it->second;
                }
                if (c.src) t.clips.push_back (c);
            }
            tl->tracks.push_back (t);
        }
        const double sb = te->getDoubleAttribute ("snapBpm"), so = te->getDoubleAttribute ("snapOffset");
        const String summary = te->getStringAttribute ("summary");
        MessageManager::callAsync ([this, a, tl, sb, so, summary]
        {
            if (! a->load()) return;
            Timeline t = *tl;
            snapBpm = sb; snapOffset = so; forgeSummary = summary.toStdString();
            undoStack.clear(); redoStack.clear();
            publish (std::make_shared<const Timeline> (std::move (t)));
            dirty = false;
        });
    });
}

void Core::saveSession()
{
    makeProjectXml()->writeTo (dataFolder().getChildFile ("session.alloy"));
}

void Core::restoreSession()
{
    auto f = dataFolder().getChildFile ("session.alloy");
    if (f.existsAsFile()) loadProject (f);
    dirty = false;
}

void Core::timerCallback()
{
    if (ended.exchange (false)) sendChangeMessage();
    if ((++tickCount % 50) == 0 && dirty) { saveSession(); dirty = false; }
}
} // namespace al
