# Contributing to Auralloy

Thanks for helping. Auralloy is licensed under **AGPL-3.0**; by contributing you agree that your contribution is released under the same license.

## Build and test

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 -DAURALLOY_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release -R "Analysis|Stems|Mashup|Timeline" --output-on-failure
```

The engine (`src/dsp`, `src/mashup`, `src/engine`) is plain C++17 with no UI or audio-device dependency, so its tests run headless. Pass `-DAURALLOY_JUCE_DIR=path/to/JUCE` to use a local JUCE checkout.

## Ground rules

- **Keep the rules honest.** Every Forge decision must trace back to a rule in `docs/RULES.md` and appear in the log. If you add a rule, document it and add a test in `tests/MashupTest.cpp`.
- **No black boxes in the planner.** A neural model is welcome behind `separateStems()` or as an optional analysis backend, but the arrangement stays rule-based and explainable.
- **Audio thread**: no allocation, locks or I/O in the audio callback. The timeline is published as an immutable snapshot.
- **No fabricated numbers.** If you change the algorithms, re-run `benchmarks/` and update the results and `SHA256SUMS.txt`. Report what you measured, including the bad numbers.
- **Design**: follow `DESIGN.md` tokens (colours, spacing, motion).
- **Translations**: edit `docs/i18n/locales/<code>.json`, then run `python tools/readmes.py`. Never edit the generated `README*.md` by hand.
- **Commits**: small, focused, with a message that says why. Open an issue first for large changes.

## Reporting problems

Use the issue templates. For audio problems, say the BPM and key you expected and attach a short clip you own the rights to. Do not upload copyrighted recordings.
