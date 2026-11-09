# Changelog

## 1.1.0

Offline-tested only (x86, ASan/UBSan, synthetic voice); not yet tried on a Force.

- **LFO** as a sixth modulation source: 7 waves (sine, triangle, saw up, saw down, square, smooth random, stepped random),
  free rate in Hz or tempo-synced, phase, retrigger on a note. Slot 1 now starts as an LFO on Pitch (still off until switched on).
- **START page**, first tile on the overview: Pitch, Formant, Gender, Harmonics, Dry/Wet and the MIDI mode.
- **PANIC tile in the header of every page** (stops all voices, clears grains, smear and freeze, resets the limiter).
- **Hints**: a line on the Pitch, Harmonics, Alternator, Effects, MIDI and Global pages says what the page's controls need (for
  example "RATIO AND FM DO NOTHING UNTIL HARM IS NOT 0").
- **Consonant protect is off by default**, so 100 % Dry/Wet is only the effect.
- **432 wired-in factory presets** (was 31): pitch ladder (natural and formant-shifted, -24..+24), gender, formant, harmonic
  shifter, alternator, grains, effects, MIDI, 70 LFO presets, 46 sequencer presets, follower, ADSR, MIDI-controller and Meta presets.
- User presets go up to slot 9999 (as 1.0.0).
- Parameters were only appended (params.lock), so projects from 1.0.0 still load; slot 1's default source changed to LFO and
  Consonant protect's default to Off.

## 1.0.0

First release. Offline-tested only (x86, ASan/UBSan, synthetic voice); not yet tried on a Force. See README, "Tested".

- Pitch-synchronous grain engine: Pitch (±24 st), Formant (±12 st), Smooth Grains, Gender macro, Jitter, Window shape, Density, Freeze, Reverse.
- Harmonic shifter (Harmonics, Ratio, FM), Grain Alternator (Alternator, Octave), Smear, Detune, Stereo, Dry/Wet.
- Detect Low / Mid / High, Snap, Consonant protect (with threshold).
- MIDI: Off, Mono, Poly (4), M.Gated, P.Gated; glide, glide-in-MIDI-only; attack and release for the gated modes.
- Four modulation slots with five sources (Meta Knob, MIDI, Sequencer, Follower, ADSR), 16 targets each, cross-modulation.
- Sequencer: 100 coded patterns plus random, 5 shapes, 16 editable steps, tempo sync.
- 31 factory presets; unlimited user presets (9999 slots, `presets` folder beside the plugin).
- One tab, overview tiles, Q-Links follow the page.
