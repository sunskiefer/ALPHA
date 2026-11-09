# ALPHA

OWN build and code Manipulator by Polyverse &amp; Vocal Synth Izotope had a baby.


A granular vocal transformer with an LFO, 432 factory presets and a Panic tile for the Akai Force and MPC OS (Gen1, ARMv7), by L'Cronx. Put it on a track's input or an audio
track's insert, sing or play a voice through it and move the pitch, the formant and the harmonics, with or without MIDI.
It is an effect plugin (VST2).

Independent clean-room plugin: the control list follows what a publicly documented plugin of this kind offers; the
DSP, the presets and the artwork are original. No code, sample, preset or name of another product is in it.

## What it does
Signal flow: input, Smear, grains (Pitch, Formant, Smooth Grains, Alternator, MIDI voices, Jitter, Window, Density, Freeze,
Reverse), Harmonic shifter (Harmonics, Ratio, FM), Detune, Stereo, Dry/Wet.
See [docs/USER_GUIDE.md](docs/USER_GUIDE.md).

## Install
Unzip the release and run `install.sh` (or follow the generated INSTALL.md). `uninstall.sh` removes it.
User presets are stored in the `presets` folder beside the plugin.

## Tested
Offline only, on a desktop (x86): the framework's ASan/UBSan port test (604 checks) and `test/dsp_test.cc` (a synthetic voice
through the real engine: pitch ±12/7/5 st, formant, MIDI mono and gated, freeze, LFO, Panic, state and user-preset round trip, all 432 factory presets finite, latency).
`test/run_tests.sh` runs both. **Not yet run on a Force:** CPU use on the device, whether MIDI reaches an effect plugin on
each firmware, Q-Link and touch behaviour, and the look of the skin. `tested.json` stays empty until a device test is done.

Latency (measured on a click, dry path): Detect High 21 ms, Mid 33 ms, Low 58 ms.

## Build
The framework is not vendored: clone <https://github.com/sunskiefer/mpc-vst-plugins> into `third_party/mpc-vst-plugins` (or set `MPC_VST`).
`./build.sh` (needs Python 3 with Pillow, a host gcc for the skin art, and `pip install ziglang`). Output:
`build/package/L'Cronx - VST - ALPHA/`. Release zip: `tools/release_quotes.py` then the framework's `tools/release.py`
(see `docs/RELEASING.md` of mpc-vst-plugins).

## License
PolyForm Noncommercial License 1.0.0 (LICENSE, NOTICE). Free for noncommercial use; contact the author for anything commercial.
