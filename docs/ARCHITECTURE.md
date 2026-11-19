# Architecture

```
src/
  dsp/        pure C++17, no dependencies - unit-tested headlessly
    Dsp.h        Source buffer, Rng, delay lines, FDN reverb
    Fft.h        radix-2 FFT
    Engine.h     BQ filters, effect Chain (EQ, lofi tape, compressor, chorus, echo, reverb, width, ambience, limiter)
    Stretch.h    WSOLA time-stretch
    TimePitch.h  sinc resampler, tempo/pitch transform, slicing, reversing, loudness
    Analysis.h   tempo, beats, downbeats, key, sections
    Stems.h      Quick Stems (DSP separator)
  mashup/
    Mashup.h     the rule engine (plan) + clip renderer
  engine/
    Timeline.h   Timeline / Track / Clip model, clip mixer, offline renderer
  app/        JUCE application
    Core.*       audio device callback, library, analysis/stem/forge jobs, editing + undo, export, projects
    Theme.*      design tokens, logo, aurora, LookAndFeel
    Widgets.*    knobs (FL-style wheel), module cards, clip inspector, segmented control
    Views.*      timeline, library, spectrum/meter, transport, Forge, export/settings overlays, intro
    MainComponent.* window layout, views, shortcuts, toasts
tests/          headless tests (+ synthetic songs with known tempo/key/structure)
```

## Data flow

1. **Import** (`Core::addSongs`) decodes to float, resamples to 44.1 kHz, then `analyze()` runs on a worker thread.
2. **Forge** (`Core::startForge`) → `ensureAnalysis` → `ensureStems` → `planMashup` → `renderClipBuffer` for every planned clip → a `Timeline` is committed (one undo step).
3. **Playback**: the audio callback mixes the published `Timeline` snapshot (`mixTimelineBlock`), through the master `Chain`, with a master *varispeed* (the playhead advances at `speed`).
4. **Editing** copies the immutable timeline, edits the copy and publishes it (`commit` = undo step, `preview` for live drags).
5. **Export** runs the same mixer and chain offline (`renderTimeline`) and writes WAV / FLAC (MP3 via a local ffmpeg).
6. **Projects** (`.alloy`) store parameters, the library paths and every clip's *recipe* (song, stem, source range, tempo, semitones, reversed); the audio is rebuilt from the recipes on load.

## Threading

* Audio thread: lock-free reads (try-lock on the timeline pointer), no allocation in `renderChunk`.
* Worker pool (3 threads): decoding, analysis, stems, forge, transforms, export.
* Message thread: UI, edits, undo stacks.

## Tests

`cmake -B build -DAURALLOY_TESTS=ON` builds `AnalysisTest`, `StemsTest`, `MashupTest`, `TimelineTest` (pure C++) and `AuralloyAppTest`
(end-to-end: import → analyse → forge → play → edit → export → project round trip → UI snapshots).
`MashupTest` and `AuralloyAppTest` re-analyse the *rendered* mashup and assert it measures the planned tempo and key.
