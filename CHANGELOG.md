# Changelog

All notable changes to Chorda are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed
- Loud clicks between notes in Mono and Legato. Mono takes its one voice for
  every new note, and the old string was stopped dead; it now fades out over
  8 ms under the new note (the step at the change: up to 19 dB above the
  string before, about 1 dB after). The same fade covers any voice stolen
  when polyphony runs out. Legato with Glide at 0 retuned the string in less
  than a period, which left a kink in the wave; a legato slide now takes at
  least 20 ms.
- Every note sounded one period late (31 ms at C1, more on lower octaves): the
  string was heard at the far end of its delay line. It is now heard where the
  pluck goes in, within half a millisecond.
- The string could run away (one report: 300 dB in Logic). A stress test that
  plays and turns knobs at random found bursts of up to 31x full scale: a long
  pluck still feeding the string when the pitch jumped (glide, bend, octave)
  piled up at the new pitch; the feedback could sit above the stability limit
  for a moment after a retune; and octave -2 on the lowest notes asked for
  pitches the string could not be long enough for. All three are fixed (0 in
  4500 random runs), a voice that runs away anyway is silenced, and a safety
  limiter at +6 dBFS and a guard against non-numbers sit at the output.
- Pitch bend added a buzz of harmonics: the string's length stepped once per
  block. It now glides sample by sample (top end under a bend: -45 dB before,
  -84 dB after).
- Low noise plucks were harsh: the burst entered the string brighter than a
  low string can carry. Below C3 it is band-limited by the loop's own cascade.
- Moving the pick changed nothing until the next note. The string is now heard
  from the pick, so moving it reshapes a ringing note.
- Glide made the glided note up to 4 dB louder (gliding down) or 6 dB
  quieter (gliding up): the pluck was set up for the note being glided to
  while the string started at the note glided from. It is now set up at the
  pitch it is plucked at, and matches a plain pluck of that note.
- A click every 3.2 s on any held note with Width above zero: the stereo
  widener's shimmer took its phase from the slow sweep and jumped each time
  the sweep wrapped. It has its own phase now.
- Held low notes ticked once a period, and notes clicked as they began to
  hold: holding took the loop low-pass out in one step, freezing the pluck's
  top end and jumping the loop delay. The low-pass now eases to a very light
  one over 150 ms, and a moving delay tap carries its fine-tuning filter's
  memory across, which also smooths glides.
- Dragging the damper crackled: its position arrived once per block and the
  filter jumped to it. It now glides there over 15 ms, and its history keeps
  running while it is off, so switching it on is clean too.
- Low notes were harsh, grainy and buzzy: the loop's Brightness filter acts
  once per trip round the string, so a 20 Hz note kept its top end eight
  times longer than a 160 Hz one. Below C3 the filter is now a cascade that
  takes the same top end away per second as at C3 (E0 went from -11 to
  -61 dB/s of high-frequency decay); C3 and above are unchanged.
- The damper cut notes short, made them quieter, and at the middle of the
  string could run the octave on for ever. It is now a filter after the
  string instead of a loss inside it, with an automatic level make-up: it
  picks out the partials with a node at the blue dot as before, but the
  envelope is exactly what Attack, Decay, Sustain and Release say, and damped
  notes are as loud as undamped ones (within 1 dB across the damper).
- Save was greyed out on factory presets. It now always works: on a user
  preset it overwrites, on a factory preset it asks for a name.
- Sine and square plucked up to 10 dB louder than the noise burst, more so on
  high notes, and full-velocity chords on them could clip. Each tone is now
  brought down to the noise burst's loudness, measured K-weighted across the
  keyboard; the noise itself is unchanged.
- A note held at full Sustain ticked, at a rate that changed with the pitch.
  Holding stops the damping, and it used to start as soon as the attack was
  over, so the raw excitation was frozen into a loop that no longer damped it;
  the partials above the string's range then rang on indefinitely and beat
  against one another. The excitation is now band-limited on the way in, and
  the hold waits for the string to settle first.
- The sub ran at half the nominal pitch while the string ran at whatever the
  loop delay actually produced, so the two drifted apart and phased. The sub
  now follows the string's own period. Its level comes from a continuous
  measurement instead of a once-per-period one, whose window slid against the
  waveform and modulated the sub.

### Added
- A new factory set: fifteen presets plus Init, from basses (Sub, Picked,
  Glide) through guitars (Nylon, Steel, Electric, Harp, Koto) and keys
  (Electric Piano, Harmonic Bells, Mono Lead) to pads and movement (Bowed,
  Halo, Tempo Pulse) and a Muted Pluck. Each was adjusted by ear in the
  plugin and saved from it.
- The pick hand plucks at every note: three frames at 0.1 s, the finger
  rising a little behind the string. (An envelope-driven version was tried
  and taken back out.)
- A faint grid behind the string shows where the damper can go: its position
  across the string and its pressure up from the string at rest. A damper
  with no pressure is a grey ball marked "Damp off".
- Pick and damper cover the whole string, bridge to nut, 0 to 100 %. The far
  half mirrors the near one the way a real string's modes do.
- The string bends under the pick hand while you hold it, and keeps its kink
  there after a pluck. (A strumming plectrum was tried and dropped.)
- **Two LFOs** on the damper, one on its position and one on its pressure,
  each an Amount and a Rate with its own tempo switch, sharing one pair of
  knobs; a Target slide switch, knob-sized, at the left of the Damp LFO
  section, picks Position or Pressure. The position LFO reaches the whole
  string at 100 %. A damper with no pressure shows as off. Both are bipolar sines
  around where the damper is set. The damper dot moves with them, a soft
  glow marks the area they reach, and hovering over it holds the dot still
  at its set place for editing.
- **Reverb** send, the last stage of the chain. The Output section reads
  Width, Drive, Reverb, Gain.
- The Exciter knob (formerly Tone) shows the waveform it makes, sine through
  square to noise, instead of naming it.

### Removed
- Repluck, in favour of the LFO.
- **Glide**, the time taken to slide from the note before this one, and a
  **Legato** voice mode: one string, where a key pressed while another is
  still down slides it rather than plucking it again.
- **Tone**, one knob across the three excitation waveforms: sine, then square,
  then a noise burst. It replaces the four-way exciter switch. Each waveform
  keeps its own drive through the crossfade, so either end sounds exactly as
  that waveform always did and the way between them holds its level.

### Changed
- The cards split the window in half, with 3, 4, 4 and 4 knobs spread
  evenly.
- The string is drawn on its own dark panel at the top of the window, above
  the controls rather than among them, headed by one line with Voices, Glide
  and Octave, spaced out over the string from bridge to nut, with no rule.
- Octave reads in octaves (-2 to +2) rather than semitones. Voices, Glide
  and Octave change with a vertical drag.
- The octave transposer is a small field rather than a five-stop slider, so it
  sits in that line with the other two.
- The pick can no longer be switched off: it is a place on the string, and its
  lowest position is 1 % (hard against the bridge) rather than off.
- Exciter and Timbre are one section, called Timbre. Polyphony and Octave are
  stacked beside it.

## [1.0.0] - 2026-09-09

First public release. A ground-up rework of Pluck Designer.

### Added
- Envelope built into the string: Attack is the excitation length, Decay,
  Sustain and Release change the loop damping. Live display of the resulting
  string level over time.
- Pick-position comb filter on the exciter; dampener comb inside the
  loop. Pick and damper are markers you drag on the drawn string.
- Sub oscillator an octave below that follows the string's level.
- Velocity mapped to pluck length and brightness as well as level; mod wheel
  raises brightness; octave transposer; damper
  pressure by vertical drag on the blue dot.
- Valve-style drive stage, 2x oversampled, on the output.
- Voice modes: Mono and polyphony limits from 2 to 64.
- Feedback compensation for the loop filter with an in-loop high-pass and a
  resonance-aware stability cap, so decay and release times hold at every
  pitch; allpass fractional delay so high notes lose nothing to interpolation.
- Preset system: thirteen factory presets, user presets on disk, prev/next browsing,
  Save / Save As / Delete, modified indicator, preset name saved with the session.
- Output Width: a stereo ensemble widener (mono at 0).
- Host state save and restore (previously the plugin forgot its settings).
- Resizable, fully vector-drawn UI laid out as a picture of the instrument:
  waveform buttons for the exciter, a drawn string that vibrates while you
  play with draggable pick and damper.
- Pitch bend (2 semitones) and sustain pedal support.
- CMake build with automatic JUCE download, plus a Projucer project.
- Offline render test suite run by `ctest`.
- GitHub Actions workflow building on macOS, Windows and Linux.

### Changed
- Voice management moved to `juce::Synthesiser`: sample-accurate note timing,
  voice stealing, per-note note-off.
- String decay is now specified in seconds (T60) and holds across pitch and
  brightness settings, instead of a raw feedback coefficient.
- Delay line uses fractional, filter-compensated length, so high notes are in tune.
- Master gain is in dB and smoothed.
- Reverb dry/wet uses an equal-power crossfade.
- The on-screen MIDI keyboard now belongs to the editor, not the processor.

### Fixed
- Note-off silenced every voice at once and cut the audio instantly (click).
- The delay line was never cleared between notes, so a new note played back
  remnants of the previous one.
- Crash when the host requested a mono output layout.
- Random number generation used the shared system generator from the audio thread.
- `getTailLengthSeconds()` reported zero despite the reverb tail.

### Removed
- The 3.4 MB background PNG. The UI is drawn in code.
- The reverb, tremolo, low cut and high cut of the coursework version, in
  favour of controls that live on the string itself.
