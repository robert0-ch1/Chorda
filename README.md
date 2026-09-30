# Chorda

**One virtual string, a whole shelf of instruments.**

Chorda is a synthesiser built around a simulated string. It gets plucked,
rings, mellows and dies away the way a real one does, and then goes where a
real one can't: it can be bowed for seconds, held at any level for as long as
the key is down, heard from a pickup you slide while it rings, and sculpted by
a dampener that glides along it picking out harmonics.

## Why

Chorda started as Pluck Designer, a Karplus-Strong synth I built as a
university project at Queen Mary University of London. The project ended
before the instrument felt finished, so I came back to it: a new string
engine, a dampener, an envelope that lives inside the string
itself, presets and a new interface.

AU · VST3 · Standalone, for macOS, Windows and Linux.

![Chorda](docs/screenshot.png)

## Download

Get the latest zip for your system from
[Releases](https://github.com/robert0-ch1/Chorda/releases/latest), unzip it,
and copy the plugins into place:

| System  | AU | VST3 | Standalone |
|---------|----|------|------------|
| macOS   | `Chorda.component` to `~/Library/Audio/Plug-Ins/Components` | `Chorda.vst3` to `~/Library/Audio/Plug-Ins/VST3` | `Chorda.app` to Applications |
| Windows | | `Chorda.vst3` to `C:\Program Files\Common Files\VST3` | run `Chorda.exe` |
| Linux   | | `Chorda.vst3` to `~/.vst3` | run `./Chorda` |

Then rescan plugins in your DAW.

On macOS the builds are not signed yet, so macOS will block them the first
time. Clear that once in Terminal:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Chorda.component ~/Library/Audio/Plug-Ins/VST3/Chorda.vst3 /Applications/Chorda.app
```

## What the controls do

- **Pick** (the hand): the point along the string where it is plucked. The
  excitation goes through a comb filter that removes every harmonic with a
  node at that point (plucked in the middle, all the even ones), and the
  string is read back through the same comb, like a pickup under the pick, so
  moving it reshapes a note that is already ringing.
- **Damp** (the blue dot): the dampener, a six-stage comb filter after the
  string, tuned to the dot's position. It keeps the harmonics that have a
  node under the dot and cuts the rest: in the middle the octave rings out,
  at a third the twelfth. Its height sets how deep it cuts, and its level is
  made up automatically, so it changes the colour of the note, never its
  loudness or its envelope.
- **Exciter**: the signal fed into the string when a note starts, crossfading
  from a sine through a square to a noise burst. It is band-limited to what
  the string can carry and level-matched across the whole knob.
- **Brightness**: the cutoff of the low-pass inside the string's feedback
  loop. Every round trip takes a little more treble away, which is why a
  pluck starts bright and mellows. Below C3 it becomes a cascade, so low
  strings lose their top end as fast as middle ones.
- **Sub**: a sine exactly one octave below the string's real pitch, its level
  following the string's own.
- **A D S R**: there is no amplitude envelope; the envelope is the string.
  Attack is how long the exciter feeds energy in (a few ms is a pluck, up to
  2 s is a bow). Decay and Release are the times, in seconds, for the string
  to fall by 60 dB with the key down and after it is let go, turned into the
  loop's feedback for each note. Sustain is the level where the string stops
  losing energy and holds.
- **Damp LFO**: two bipolar sine LFOs, one sliding the blue dot along the
  string, one varying how deep it cuts, from 0.05 to 20 Hz or synced to the
  host tempo. The Target switch picks which one the knobs edit.
- **Voices, Glide, Octave**: Mono, Legato (a new key slides the ringing string
  to the new pitch instead of plucking again) or up to 64 voices; a glide in
  pitch between notes; an octave shift of up to two either way.
- **Width, Drive, Reverb, Gain**: a stereo ensemble of four modulated delays;
  asymmetric valve-style saturation, 2x oversampled; a reverb send; the
  output level, followed by a safety limiter at +6 dBFS.

Fifteen presets, from basses to guitars, keys and pads, are there to start from.

## Build it yourself

You need CMake 3.22+ and a C++17 compiler; JUCE is downloaded for you.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The plugins and the app land in `build/Chorda_artefacts/Release/`. Run the
tests with `ctest --test-dir build -C Release`.

## License

[GPL v3](LICENSE). Built with [JUCE](https://juce.com). The title typeface is
Bagnard by Sebastien Sanfilippo, under the SIL Open Font License.
