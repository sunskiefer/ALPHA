# ALPHA

OWN build and code, Manipulator by Polyverse & Vocal Synth Izotope had a baby.


A granular vocal transformer with an LFO, TUNE, ALIGN, 492 factory presets and a Panic tile for the Akai Force and MPC OS (Gen1, ARMv7), by L'Cronx and DreamX Wilson. Put it on a track's input or an audio
track's insert, sing or play a voice through it and move the pitch, the formant and the harmonics, with or without MIDI.
It is an effect plugin (VST2).

Independent clean-room plugin: the control list follows what a publicly documented plugin of this kind offers; the
DSP, the presets and the artwork are original. No code, sample, preset or name of another product is in it.

## Status
Version 1.2.1. Built for the ARMv7 Force and MPC OS 3.x. **Not yet run on a Force** (1.1.0 was the last build checked offline; ALIGN, TUNE and the new skin are untested on the device).

## Highlights
- Pitch-synchronous grains: Pitch (±24 st) and Formant (±12 st) move separately, Gender moves both.
- Harmonic shifter, Alternator, Smear, Detune and Stereo, Freeze and Reverse, Jitter, Window and Density.
- MIDI voices: Off, Mono, Poly, Mono Gated, Poly Gated, with Glide, Attack and Release.
- Four modulation slots, six sources (Meta Knob, MIDI, Sequencer, Follower, ADSR, LFO), 16 targets, cross-modulation.
- LFO: 7 waves, free rate or tempo sync, phase, retrigger.
- **New in 1.2.0:** TUNE (22 scales, key, speed, amount, on the Pitch page), ALIGN (Global page) and a new white skin.
- 492 wired-in factory presets (60 TUNE presets), plus user presets 1-9999 saved as plain text.
- START page with the six controls that matter most, and a PANIC tile on every page.
- 100 % wet is only the effect (Consonant protect is off by default).

## Pages
START, PITCH, HARMONICS, ALTERNATOR, GRAINS, EFFECTS, MIDI, MOD SOURCE, MOD AMOUNTS, SEQUENCER, USER PRESETS, GLOBAL.
The 16 Q-Links follow the page you are on. Details in [docs/USER_GUIDE.md](docs/USER_GUIDE.md).

![Pitch page](docs/screens/ALPHA_page_01_Pitch.png)

## Requirements
A first-generation MPC OS device (32-bit ARM: Force, MPC Live / Live II, One, X, Key 61), MPC OS 3.x, and root (SSH) access.
Installing plugins this way is unofficial: back up first, use at your own risk.

## Latest release
See the Releases page for `ALPHA-1.2.1-mpc-armv7.zip` and [CHANGELOG.md](CHANGELOG.md).

## How to use ALPHA (full guide)

### 1. Install and first sound
1. Unzip `ALPHA-1.2.1-mpc-armv7.zip`, copy the folder to the device and run `sh install.sh` as root over SSH (MPC is stopped and restarted, `MPC.settings` is backed up first). `uninstall.sh` removes it again.
2. On a track, add **ALPHA** from the plugin browser. It is an **effect**: put it on an audio track's insert (or the input you sing into). Nothing happens until a voice goes through it.
3. Sing or speak. The header line on every page shows what ALPHA hears (`F0 ... HZ`, the note, `VOICED`) and the latency. If it says `UNVOICED` there is no pitch to follow: sing louder or change **Detect** (Global page).
4. Open the **overview** (first screen): twelve tiles, one per page. Tap a tile to open that page. In the header, **OVERVIEW** goes back, `<` and `>` step between pages, and **PANIC** stops all voices and clears the grains, smear and freeze.
5. The 16 **Q-Links** always follow the page you are on (name and value are shown on the Force's screen).
6. Quickest start: open a preset from the PRESET menu (492 of them, all start at full wet), then change **Pitch**, **Formant** and **Dry/Wet** on the START page.

### 2. The pages
| Page | What is on it |
|---|---|
| START | Pitch, Formant, Gender, Harmonics, Dry/Wet and the MIDI mode: the six controls that matter most |
| PITCH | Pitch (±24 st), Smooth Grains, Formant (±12 st), Gender, plus the **TUNE** row: TUNE on/off, Scale, Key, Speed, Amount |
| HARMONICS | Harmonics (±8), Ratio, FM |
| ALTERNATOR | Alternator (0-12 st), Octave (±8) |
| GRAINS | Jitter, Window, Density, Freeze, Reverse |
| EFFECTS | Smear, Stereo, Detune, Dry/Wet |
| MIDI | mode list (Off, Mono, Poly, M.Gated, P.Gated), Glide, Glide M, Attack and Release (gated modes) |
| MOD SOURCE | slot 1-4, On, source list, the source's own controls, cross-modulation |
| MOD AMOUNTS | the selected slot's amount (-100..+100 %) on each of 16 targets |
| SEQUENCER | the selected slot's 16 steps |
| USER PRESETS | slot knob (1-9999), Load, Save, Delete, status line |
| GLOBAL | Snap, Consonant protect (with its Voiced threshold), **Align**, Detect (Low/Mid/High), Panic |

### 3. The sound controls, in the order the signal goes
- **Pitch** moves the voice's pitch (±24 semitones). **Formant** moves the vowel colour (±12) without changing the pitch: raise it for a smaller, lighter voice, lower it for a bigger one. **Gender** moves both at once (up = lighter, down = deeper). **Snap** (Global) rounds Pitch, Formant and Harmonics to whole steps.
- **Smooth Grains**: low = the exact pitch; high = longer, smoother grains with some of the original pitch leaking through.
- **TUNE** (Pitch page): switch it on and the voice is pulled to the nearest note of the chosen **Scale** in the chosen **Key**. Scale has 22 choices (Chromatic, Major, Minor, Harm Minor, Mel Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Major Pent, Minor Pent, Blues, Whole Tone, Diminished, Hung Minor, Phryg Dom, Hirajoshi, In Sen, Major Triad, Minor Triad, Power 5th); Key is C to B. **Speed**: low = a slow glide to the note (natural), high = a hard snap. **Amount**: how far it moves (100 % = onto the note, 50 % = halfway). The header line shows the key and scale while TUNE is on. Your Pitch knob adds on top of the corrected note. TUNE follows the voice, so in the MIDI modes (where the notes decide the pitch) it does nothing. Start from a TUNE preset ("Tune C Major Hard", "Tune A Min Pent Soft", ...).
- **Harmonics, Ratio, FM**: a frequency shifter after the grains. Harmonics shifts by that many multiples of the voice's pitch (times Ratio); FM adds a slow wobble. Ratio and FM do nothing while Harmonics is 0.
- **Alternator / Octave**: successive grains alternate between pitch +Alt and -Alt semitones. Octave above 0 holds each pitch for 2^n grains (slower swapping); Octave below 0 adds a second grain stream that many octaves lower. Alternator must be above 0 for either to act.
- **Jitter** randomises grain timing; **Window** sets the grain width; **Density** below 0 drops grains, above 0 doubles them; **Freeze** holds the current sound; **Reverse** plays each grain backwards.
- **Smear** loops short pieces of the input before the grains; **Detune** adds four slow detuned voices; **Stereo** adds a mono-compatible spread; **Dry/Wet** mixes with the dry voice.
- **Consonant protect** (Global, off by default): lets unvoiced sounds (s, t, breath) pass around the grains so they stay clear; **Voiced** sets how strongly a sound must be voiced to count as a vowel. With it off, 100 % wet is only the effect.
- **Align** (Global, off by default): the dry voice is delayed by the real delay of the effect path (hold plus one voice period, followed smoothly) so dry and wet line up when you blend them. Off keeps the fixed delay of 1.1.0. Use it when Dry/Wet sits between 0 and 100 % and the mix sounds phasey or doubled.
- **Detect** (Global): the pitch range. Low 25-700 Hz, Mid 40-1000 Hz, High 80-4000 Hz. Lower ranges follow low voices better and add latency (High 21 ms, Mid 33 ms, Low 58 ms).

### 4. MIDI
Set the mode on the MIDI page (or on START). **Off**: the Pitch knob decides. **Mono**: one note sets the pitch (last note wins). **Poly**: up to four voices. **M.Gated / P.Gated**: sound only while a note is held, with Attack and Release. **Glide** slides between notes; **Glide M** makes it glide only in the MIDI modes. Pitch still adds on top of the note. Whether a MIDI track reaches an effect plugin depends on the firmware: check it on your Force.

### 5. Modulation
Four slots (MOD SOURCE page), each with a source and 16 amounts (MOD AMOUNTS page). **Nothing moves until the slot is switched On.**
1. Pick a slot (Slot 1-4) and switch **On**.
2. Choose the source: **LFO** (7 waves, free rate in Hz or tempo sync, phase, retrigger on a note), **Meta Knob** (one knob with polarity +, - or both, and optional steps), **MIDI** (note, velocity, pitch wheel, mod wheel, aftertouch), **Sequencer** (100 patterns plus random, 5 shapes, tempo sync or free rate, retrigger, 16 editable steps), **Follower** (input level with attack, release and gain) or **ADSR** (triggered by MIDI notes).
3. On MOD AMOUNTS, turn up the slot's amount on the targets you want (Pitch, Smooth, Formant, Harmonics, Ratio, FM, Alternator, Octave, Smear, Stereo, Detune, Wet, Jitter, Window, Density, Gender). For vibrato: slot On, LFO, a little amount on Pitch.
4. **Cross-modulation** scales a slot by another slot's level. The SEQUENCER page edits the selected slot's 16 steps.

### 6. Presets
- The PRESET menu lists the **492 factory presets** (pitch ladders, gender, formant, harmonics, alternator, grains, effects, MIDI, LFO, sequencer, follower, ADSR, Meta, and 60 TUNE presets). Each starts at full wet.
- **USER PRESETS** page: turn the slot knob to a number (1-9999), then **Save** to store the whole sound, **Load** to bring it back, **Delete** to remove it. The status line says SAVED / LOADED / EMPTY. Files are plain text in the `presets` folder beside the plugin and survive updates.

### 7. Troubleshooting
- *No sound change*: the voice must be detected (`VOICED` in the header). Raise the input level, change Detect, or check the plugin is on the track the voice is on.
- *Only the effect, no original*: Dry/Wet is at 100 %; lower it, or turn Consonant protect on.
- *Mix sounds phasey*: turn **Align** on (Global).
- *Stuck or ringing*: tap **PANIC**.
- *TUNE does nothing*: it needs a voiced input and MIDI mode Off.
- *Hint line*: the Harmonics, Alternator, Effects, MIDI and Global pages show a one-line hint when a control needs another one.

## What it does
Signal flow: input, Smear, grains (Pitch, Formant, Smooth Grains, Alternator, MIDI voices, Jitter, Window, Density, Freeze,
Reverse), TUNE, Harmonic shifter (Harmonics, Ratio, FM), Detune, Stereo, Dry/Wet.
See [docs/USER_GUIDE.md](docs/USER_GUIDE.md).

## Install
Unzip the release and run `install.sh` (or follow the generated INSTALL.md). `uninstall.sh` removes it.
User presets are stored in the `presets` folder beside the plugin.

## Tested
1.1.0 was checked offline on a desktop (x86): the framework's ASan/UBSan port test and `test/dsp_test.cc`. 1.2.0 was not run on x86 or a Force. **Not yet run on a Force:** CPU use on the device, whether MIDI reaches an effect plugin on
each firmware, Q-Link and touch behaviour, and the look of the skin. `tested.json` stays empty until a device test is done.

Latency (measured on a click, dry path): Detect High 21 ms, Mid 33 ms, Low 58 ms.

## Build
The framework is not vendored: clone <https://github.com/sunskiefer/mpc-vst-plugins> into `third_party/mpc-vst-plugins` (or set `MPC_VST`).
`./build.sh` (needs Python 3 with Pillow, a host gcc for the skin art, and `pip install ziglang`). Output:
`build/package/L'Cronx - VST - ALPHA/`. Release zip: `tools/release_quotes.py` then the framework's `tools/release.py`
(see `docs/RELEASING.md` of mpc-vst-plugins).

## License
PolyForm Noncommercial License 1.0.0 (LICENSE, NOTICE). Free for noncommercial use; contact the author for anything commercial.

## Skin art
The 1.2.0 skin is the generated skin (`./build.sh`) post-processed with the scripts in `tools/skin/` (filmstrips from `art/skin/`, selectors, option cells, the layout exported from the panel editor). They are kept as run in the build session (absolute paths inside): a record of how the shipped skin was made, not a one-command build.
