# Changelog

All notable changes are listed here. Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow [Semantic Versioning](https://semver.org/).

## [0.0.1] - 2026-11-20

First public release.

### Added
- **Forge**: rule-based automatic mashups from two or more songs. Default **Sequential** mode plays the songs one after another, beat-matched and key-matched, with bar-aligned overlap transitions; **Layered** mode puts one song's vocal over another's beat. Decisions are written to a plain-language log.
- **Analysis**: tempo, beats, bars (downbeat), key and section boundaries, all in pure C++.
- **Quick Stems**: DSP stem split into vocals, drums, bass and other; the four stems always sum back to the input.
- **Studio**: multi-track timeline with snapping, split, trim, fades, reverse, per-clip gain, tempo and pitch, loop regions, unlimited undo/redo, master varispeed, projects (`.alloy`).
- **Looks and effects**: ten one-click looks (Slowed + Reverb, Lofi Chill, Rainy Lofi, Nightcore, Vaporwave, Dreamy Slow, Tape Cassette, Bass Boosted, Mashup Glue, Wide & Warm); EQ, lofi tape, compressor, chorus, echo, FDN reverb, stereo width, rain and vinyl ambience, lookahead limiter.
- **Export**: WAV 16/24/32-bit float, FLAC, MP3 at 44.1 to 96 kHz, with normalise and dither.
- Dark, modern interface with an animated logo intro, aurora background and audio-reactive glow.
- Documentation in 20 languages, a research preprint, a seeded benchmark suite and headless tests.

### Known limitations
- Assumes mostly steady 4/4 tempo. Quick Stems is classic DSP, not a neural separator. Validated on synthetic songs only; no listening study. See the README and the paper.
