# The mashup rules

Auralloy's *Forge* is **rule-based and explainable**: every decision it makes is one of the rules below, and the plan
is written to the log you see in the app ("The plan" / "How it was built"). Nothing is a black box, and every clip it
creates stays fully editable on the timeline.

The implementation is `src/mashup/Mashup.h` (planning) and `renderClipBuffer` (rendering); it has no dependency on
the UI and is covered by `tests/MashupTest.cpp`.

## Two arrangement modes

* **Sequential (Medley), the default.** The songs play **one after another**, in the order you choose. Each song gets a few phrases of
  its own, matched in tempo and key, and the next song takes over through an overlap transition on a bar line. The log shows who plays
  when, the song ribbon in the Studio colours every clip by song, and only the two songs involved in a transition are ever audible
  together. Every song keeps its own vocal, bass and drums; there are no foreign vocals.
* **Layered.** The classic "vocal over another song's beat" mashup: one song's vocal on another song's instrumental. Use it when you want a
  blend rather than a medley.

Rules 1 to 9 below apply to both modes. In sequential mode, rules 4 and 5 mean exactly one vocal and one bass are audible except
during a transition, where the outgoing bass leaves and the incoming bass arrives on a bar line.

| # | Rule | Why | Default |
|---|------|-----|---------|
| 1 | **One tempo.** The target BPM is the candidate that needs the least time-stretching overall. A song that would be stretched more than the tolerance is left out (and the log says so). | Stretching far hurts transients and phase. | ±15 % |
| 2 | **One key.** Keys are mapped to a *major-equivalent tonic* (A minor ≡ C major), then every song is shifted by the smallest number of semitones to one shared tonic. Songs that would need a bigger shift are left out. Detection confidence weights the cost. | Relative major/minor share the same notes; small shifts keep vocals natural. | ≤ 3 semitones |
| 3 | **Everything on the grid.** Sections are cut on detected downbeats, in 4-bar phrase blocks; clip edges sit on the bar grid of the target tempo. | Loops and phrases line up; edits snap. | 4/4 |
| 4 | **One vocal at a time.** Vocal clips never overlap; the next vocal starts after the previous movement's vocal has finished. | Two lead vocals fight. | — |
| 5 | **One bass at a time.** In every transition the outgoing bass leaves and the incoming bass arrives on a bar line (a *bass swap*). | Two basses mud the low end. | half-way through the transition |
| 6 | **Match energy.** Each movement picks the section whose energy is closest to the target energy curve; vocals are matched to base sections by energy and vocal activity (verse over verse, chorus over drop). | Keeps the arrangement musical. | *Rising* shape |
| 7 | **Overlap transitions.** Drums and "other" overlap for a few bars with equal-power fades. | Smooth DJ-style blends. | 4 bars |
| 8 | **Mix like a human.** Vocals are loudness-matched per role; the "other" stem is ducked 2.5 dB under a donor vocal. | Keeps the vocal clear. | — |
| 9 | **Don't repeat; end well.** A usage penalty avoids repeating the same section; the last movement prefers a song's outro. | Variety and a natural ending. | — |

## What feeds the rules

* **Tempo, beats, downbeats, key, sections** — `src/dsp/Analysis.h` (onset envelope → tempo prior → dynamic-programming beat tracker → fine comb search for tempo/phase → bar phase from low-band accents and chord changes → chroma key detection → self-similarity novelty for sections).
* **Stems** — `src/dsp/Stems.h` (*Quick Stems*, classic DSP: percussive/harmonic median-filter separation, centre-panned voice band for vocals, low harmonic part for bass; the four stems always sum back to the original).
* **Tempo and pitch changes** — `src/dsp/TimePitch.h` (WSOLA time-stretch + windowed-sinc resampling; tempo and pitch are independent).

## Pair score

`pairScore(a, b)` (0–100 in the UI's compatibility matrix) = 50 % tempo closeness + 40 % key closeness (after allowed shifts) + 10 % how complementary the vocal presence is.

## Honest limitations

* The beat model assumes a mostly steady 4/4 tempo. Songs with tempo drift, odd meters or no clear beat are skipped with a message.
* *Quick Stems* is DSP, not a neural separator: great for drums and bass, decent for a centred lead vocal, imperfect on dense mixes. The separator is isolated behind `separateStems()` so a neural model can replace it.
* Key detection can be wrong on ambiguous harmony; the log shows the detected key of every song so you can see it.
* Nothing here can judge taste. Forge gives you a rule-correct starting point; the Studio is where you finish it.
