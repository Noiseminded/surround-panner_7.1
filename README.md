# Surround Panner 7.1

A [VCV Rack 2](https://vcvrack.com) module by **Noiseminded** that pans a sound around a 7.1 speaker system.

- Turn one knob (or send one CV) to move a sound around the room.
- Equal-power panning between neighbouring speakers, so the loudness stays constant while the sound moves.
- **Spread** widens a sound from a single point to the whole room.
- **LFE send** feeds a low-passed copy to the subwoofer.
- Polyphonic: every voice of a polyphonic cable can have its own position.
- A live top view of the room shows where each voice is and which speakers are playing.
- Speaker angles can be adjusted to match the real room.

Made for the *Imagem e Som Interativos* course, but free for anyone to use.

## Install

1. Open the [latest release](https://github.com/Noiseminded/surround-panner_7.1/releases/latest) and download the file for your computer:

   | Computer | File ends in |
   |---|---|
   | Windows | `win-x64.vcvplugin` |
   | Mac with Apple Silicon (M1 or later) | `mac-arm64.vcvplugin` |
   | Mac with Intel processor | `mac-x64.vcvplugin` |
   | Linux | `lin-x64.vcvplugin` |

   On a Mac, check which one you have under  → About This Mac. If Rack runs under Rosetta, use the `mac-x64` file.

2. In VCV Rack, choose **Help → Open user folder**. Put the downloaded file in the plugins folder there. In recent versions of Rack this folder has your system in its name, for example `plugins-win-x64` or `plugins-mac-arm64`.
3. Restart Rack. Right-click an empty space in the rack and search for **Surround Panner 7.1**.

Rack unpacks the file itself at startup, so do not unzip it.

## Patching it to the speakers

Add a **VCV Audio 8** module, select the room's audio interface, and connect the outputs:

| Panner output | Audio 8 input (common 7.1 order) |
|---|---|
| L | 1 |
| R | 2 |
| C | 3 |
| LFE | 4 |
| BL | 5 |
| BR | 6 |
| SL | 7 |
| SR | 8 |

The small number next to each output on the panel is this channel. If your room is wired in a different order, patch each output to the jack of the matching speaker instead. Always check with a test sound first.

## Controls

| Control | What it does |
|---|---|
| **AZIMUTH** | Direction of the sound in degrees. 0° is front centre, +90° is to the right, ±180° is directly behind. |
| **AZIMUTH CV + AMT** | Adds to the knob. 10 V = one full turn (360°), so a 0–10 V saw LFO makes the sound circle the room once per cycle. Turn AMT negative to reverse the direction. |
| **SPREAD** | 0%: a sharp point source between two speakers. 100%: the sound fills the whole circle. |
| **SPREAD CV + AMT** | Adds to the knob. 10 V = 100%. |
| **LFE SEND** | How much of the input goes to the LFE output, low-passed (120 Hz by default). Start low. |
| **IN** | Audio input, mono or polyphonic. |

### Right-click menu

- **Speaker angles**: move each speaker to match the real room, or reset to the 7.1 standard (L/R ±30°, sides ±100°, backs ±145°) or to an even circle.
- **Use centre speaker for panning**: turn off to keep the centre speaker silent; front sounds then come from L and R together.
- **Mix polyphonic voices to mono outputs** (on by default): each output carries one channel, ready for Audio 8. Turn off to keep the voices separate on polyphonic outputs.
- **LFE low-pass**: 80, 100 or 120 Hz.

## How the panning works

The seven full-range speakers form a ring. For a sound at a given angle, only the two speakers either side of it play. Their gains follow the equal-power law: `cos` and `sin` of the position between them, so at the halfway point both play at about 71% (−3 dB) and the total power stays the same.

Spread is made by splitting the sound into 16 points across the chosen width and adding up their power, so a wide sound still keeps a constant level.

The LFE output never takes part in panning: bass below about 120 Hz has almost no direction.

## Building from source

You need the [VCV Rack SDK](https://vcvrack.com/manual/Building#Building-Rack-plugins).

```sh
export RACK_DIR=/path/to/Rack-SDK
make install
```

The pan law has its own tests, which need only a C++ compiler:

```sh
make -C tests
```

New releases are built for all platforms by GitHub Actions when a version tag is pushed. The tag must match the version in `plugin.json`, for example `v2.0.1`.

## License

GPL-3.0-or-later. See [LICENSE.txt](LICENSE.txt).
