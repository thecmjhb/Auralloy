# Auralloy benchmarks

A seeded, fully synthetic and reproducible benchmark suite. It checks that the analysis, the stem splitter and the mashup planner do what the paper and the README claim. **It is not a listening test and it does not use real, copyrighted music.** Real recordings are messier; treat these numbers as regression gates and a sanity check, not as proof of musical quality.

## Run it

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 -DAURALLOY_TESTS=ON
cmake --build build --config Release --target Benchmark
build/Release/Benchmark.exe benchmarks/results 96 30     # output folder, analysis songs, mashup triples
```

Seed `20261101` is fixed in the source, so the numbers reproduce bit for bit on the same compiler. Timings depend on the machine.

## What is measured

| Part | Data | Metric |
|---|---|---|
| A. Analysis | 96 generated songs, 70 to 170 BPM, random key and mode, three section layouts, random pickup lead | tempo within 0.5 % (octave folded), tempo right at any octave, key exact / relative / fifth, first downbeat within 50 ms (median and 90th percentile), section boundaries within one bar (precision, recall, F1) |
| B. Stems | 16 generated 8 s mixes with known vocal, drum, bass and pad stems | SI-SDR of each estimated stem against its reference; reconstruction error of the four stems summed against the input |
| C. Mashups | 12 prepared songs, 30 random triples, 120 s target; 15 of them also planned in layered mode | rule compliance on all 45 plans (stretch and shift limits, clip edges on bar lines, never two vocals or two basses at once); on the 30 rendered sequential mixes: measured tempo against the planned tempo, tempo spread and key agreement over 20 s windows compared with a naive baseline; planning and render time |

The naive baseline plays the same songs in the same order, 24 bars each at their native tempo and key, joined with 4-bar equal-power crossfades and no stretching or shifting.

## Results (seed 20261101)

| Metric | Result |
|---|---:|
| Tempo within 0.5 % | 93.8 % (90 / 96) |
| Tempo correct at any octave | 95.8 % |
| Mean tempo error | 2.96 % (dominated by the few outliers) |
| Key exact | 99.0 % |
| Downbeat within 50 ms | 85.4 % (median 2.8 ms, 90th percentile 368 ms) |
| Section boundaries | precision 0.79, recall 0.65, F1 0.71 |
| Stem SI-SDR: vocals / drums / bass | 21.9 dB / -0.3 dB / 6.9 dB |
| Stem reconstruction error | -147 dB (stems sum to the input) |
| Rule compliance (45 plans) | 100 % |
| Rendered tempo error | 0.010 BPM mean, 0.036 BPM max |
| Tempo spread across 20 s windows | 0.05 BPM (Auralloy) vs 3.07 BPM (naive) |
| Key agreement across windows | 0.83 (Auralloy) vs 0.38 (naive) |
| Planning time | 0.07 ms per mashup |
| Render time | 1.3 s per minute of audio |

### Read these numbers honestly

- Drum separation is weak (about 0 dB SI-SDR). Quick Stems is classic median-filter DSP; the drum stem mostly helps the planner and the bass swap, not a clean drum solo.
- The 90th-percentile downbeat error is large: when the pickup is ambiguous the first bar is placed one beat off. Median error is small.
- Key detection is near perfect here because the synthetic songs have clean, in-key harmony. Expect much lower accuracy on real music.
- Key agreement of 0.83 means about one 20 s window in six is analysed as a different key. Windows that straddle a transition contain two songs; the benchmark does not break this down further.

## Files

| File | Content |
|---|---|
| `results/summary.json` | all aggregate numbers (read by `tools/readmes.py` for the README) |
| `results/analysis.csv` | one row per analysed song |
| `results/mashups.csv` | one row per planned mashup (sequential and layered) |
| `SHA256SUMS.txt` | checksums of the files above and the benchmark source |

Verify with `sha256sum -c SHA256SUMS.txt` (run it from this folder).
