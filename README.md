# Chorda

**One virtual string, a whole shelf of instruments.**

Chorda is a synthesiser built around a simulated string. It gets plucked, it
rings, it mellows and it dies away the way a real one does. Change where it
is plucked, rest a finger on it, let that finger drift along it, and the same
string becomes a sub bass, a nylon guitar, a koto, an electric piano or a slow
glassy pad.

AU · VST3 · Standalone, for macOS, Windows and Linux.

![Chorda](docs/screenshot.png)

## Download

Get the latest zip for your system from
[Releases](https://github.com/robert0-ch1/Chorda/releases/latest), unzip it,
and copy the plugins into place:

| System  | AU | VST3 | Standalone |
|---------|----|------|------------|
| macOS   | `Chorda.component` to `~/Library/Audio/Plug-Ins/Components` | `Chorda.vst3` to `~/Library/Audio/Plug-Ins/VST3` | open `Chorda.app` |
| Windows | | `Chorda.vst3` to `C:\Program Files\Common Files\VST3` | run `Chorda.exe` |
| Linux   | | `Chorda.vst3` to `~/.vst3` | run `./Chorda` |

Then rescan plugins in your DAW.

On macOS the builds are not signed yet, so macOS will block them the first
time. Clear that once in Terminal:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Chorda.component ~/Library/Audio/Plug-Ins/VST3/Chorda.vst3
```

(For the app, right-click it and choose Open.)

## What the controls do

- **Pick** (the hand): where the string is plucked. Near the end it is thin
  and bright, in the middle round and hollow.
- **Damp** (the dot): a finger resting on the string. Slide it along to bring
  out harmonics, drag it up and down to press lighter or harder.
- **Exciter**: what sets the string moving, from a soft sine through a square
  to a bright burst of noise.
- **Brightness**: how bright the string stays while it rings.
- **Sub**: adds an octave below that follows the string.
- **A D S R**: how long the pluck takes (up to a slow bow), how fast the
  string dies away, where it holds while you keep the key down, and how long
  it rings after you let go.
- **Damp LFO**: moves the finger along the string, or changes how hard it
  presses, freely or in time with your song.
- **Voices, Glide, Octave**: mono, legato or up to 64 voices, slides between
  notes, and octave shift.
- **Width, Drive, Reverb, Gain**: stereo spread, warm valve saturation, a
  room, and the output level.

Fifteen presets, from basses to guitars, keys and pads, are there to start from.

## The story

Chorda started as Pluck Designer, a Karplus-Strong synth I built as a
university project at Queen Mary University of London. The project ended
before the instrument felt finished, so I came back to it: a new string
engine, a finger on the string, an envelope that lives inside the string
itself, presets and a new interface.

A big shout-out to Queen Mary University of London and to Professor Josh Reiss.

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
