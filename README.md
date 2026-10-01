# Chorda

Strings, at hand.

Chorda lets you shape a simulated string into many instruments: from deep basses to bright harps, from quick plucks to bowed swells.

Built on Karplus-Strong synthesis, Chorda extends the algorithm with accurate tuning at every pitch, full envelope control, a movable pluck point and a harmonic dampener driven by two LFOs, all behind a few intuitive controls.

Explore what it can do with 15 built-in presets.

AU, VST3 and standalone, for macOS, Windows and Linux.
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

## Controls

| Control | What it does |
|---|---|
| Pick (the hand) | Where the string is plucked. A comb filter, 1 − z<sup>−pN</sup>, on the pluck and on what you hear, like a pickup under the pick. |
| Damp (the blue dot) | The dampener: a six-stage comb after the string that keeps the harmonics with a node under the dot. Height sets how deep it cuts; level is made up. |
| Exciter | What goes into the string: sine, square or noise burst, crossfaded and level-matched. |
| Brightness | Low-pass inside the loop: every round trip takes more treble away. |
| Sub | A sine one octave down, following the string's level. |
| Attack | How long the exciter feeds the string: ms for a pluck, up to 2 s for a bow. |
| Decay, Release | Seconds to fall 60 dB with the key held, and after; sets the loop gain g. |
| Sustain | The level where the string stops losing energy and holds. |
| Damp LFO | Two sine LFOs on the dot's position and depth, free or tempo-synced. |
| Voices, Glide, Octave | Mono, Legato or up to 64 voices; pitch glide; ±2 octaves. |
| Width, Drive, Reverb, Gain | Stereo ensemble; 2x oversampled valve drive; reverb send. |

Fifteen presets, from basses to guitars, keys and pads, are there to start from.

## Inside

![Signal flow of one voice](docs/signal-flow.svg)

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
