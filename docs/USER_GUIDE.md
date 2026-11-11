# ALPHA user guide

ALPHA listens for the pitch of the voice at its input and rebuilds it from small grains, one per voice period, so the pitch
and the formants (the vowel colour) can move separately. Everything is on one tab; the **overview** has twelve tiles that open
the pages, `<` and `>` step between pages, OVERVIEW goes back, and **PANIC** (the fourth header tile, on every page) stops all voices and clears the grains, smear and freeze. The 16 Q-Links always follow the page you are on.

## Pages
| Page | Controls |
|---|---|
| START | Pitch, Formant, Gender, Harmonics, Dry/Wet and the MIDI mode: the six controls that matter most |
| PITCH | Pitch (±24 st), Smooth Grains, Formant (±12 st), Gender, and the TUNE row: TUNE, Scale, Key, Speed, Amount |
| HARMONICS | Harmonics (±8), Ratio, FM |
| ALTERNATOR | Alternator (0-12 st), Octave (±8) |
| GRAINS | Jitter, Window, Density, Freeze, Reverse |
| EFFECTS | Smear, Stereo, Detune, Dry/Wet |
| MIDI | mode list (Off, Mono, Poly, M.Gated, P.Gated), Glide, Glide M, Attack and Release (gated modes) |
| MOD SOURCE | slot 1-4, On, source list, the source's controls, cross-modulation (source and amount) |
| MOD AMOUNTS | the selected slot's amount (-100..+100 %) on each of 16 targets |
| SEQUENCER | the selected slot's 16 steps |
| USER PRESETS | slot number, Load, Save, Next free |
| GLOBAL | Detect (Low/Mid/High), Snap, Consonant protect and its threshold, Align, Panic |

## The controls
- **Pitch / Formant**: Pitch moves the voice's pitch; Formant moves the vowel colour without changing the pitch. **Gender** is a
  macro that moves both (up = lighter, down = deeper). Snap rounds Pitch, Formant and Harmonics to whole steps.
- **TUNE** (Pitch page): when on, the voice is pulled to the nearest note of the chosen **Scale** in the chosen **Key**. **Speed** is how fast it moves (low = gliding, high = hard); **Amount** is how far (100 % = onto the note). The header line shows the key and scale. In the MIDI modes the notes decide the pitch, so TUNE does nothing there.
- **ALIGN** (Global page): when on, the dry voice is delayed by the real delay of the effect path so dry and wet line up; off keeps the fixed delay of 1.1.0.
- **Smooth Grains**: low = the exact pitch; high = longer grains, smoother, with some of the original pitch leaking through.
- **Harmonics, Ratio, FM**: a frequency shifter after the grains. Harmonics shifts by that many multiples of the voice's pitch
  (times Ratio); FM adds a slow wobble to the shift.
- **Alternator / Octave**: successive grains alternate between pitch +Alternator and -Alternator semitones. Octave above 0
  holds each pitch for 2^n grains (slower swapping); Octave below 0 adds a second grain stream that many octaves lower
  (the Alternator must be above 0 for either to act).
- **Jitter** randomises grain timing; **Window** sets the grain width (narrow to wide); **Density** below 0 drops grains, above 0
  doubles them; **Freeze** holds the current sound (a toggle, not a momentary: a momentary button cannot hold a freeze);
  **Reverse** plays each grain backwards (a toggle).
- **Smear** loops short pieces of the input before the grains; **Detune** adds four slow detuned voices; **Stereo** adds a
  mono-compatible spread; **Dry/Wet** mixes with the dry voice (delayed to line up).
- Each page that has controls needing another control (Harmonics, Alternator, Effects, MIDI, Global) shows a one-line hint at the bottom.
- **Consonant protect** (off by default; at 100 % wet with it off you hear only the effect) lets unvoiced sounds (s, t, breath) pass around the grains so they stay clear; the threshold sets how
  strongly a sound must be voiced to count as a vowel.
- **Detect** picks the pitch range: Low 25-700 Hz, Mid 40-1000 Hz, High 80-4000 Hz. Lower ranges track low voices better and add
  latency (High 21 ms, Mid 33 ms, Low 58 ms). The info line in the header of every page shows F0 and the latency.

## MIDI
Off: the Pitch knob decides. Mono: one note sets the pitch (the last note wins, with fall-back). Poly: up to four voices.
M.Gated / P.Gated: sound only while a note is held, with Attack and Release. Glide slides between notes; Glide M glides only in
MIDI modes. Pitch still adds on top of the note. Whether a MIDI track reaches an effect plugin depends on the firmware: not yet
verified on a Force.

## Modulation
Four slots, each with a source and 16 amounts. Nothing moves until the slot is switched **On**. Sources: **LFO** (7 waves: sine, triangle, saw up, saw down, square, smooth random, stepped random; a free rate in Hz or tempo sync; phase; retrigger on a note: set the slot On and turn up its amount on a target, for example Pitch for vibrato), **Meta Knob** (a knob, with polarity +, - or both, and optional steps),
**MIDI** (note, velocity, pitch wheel, mod wheel, aftertouch), **Sequencer** (100 patterns plus random, 5 shapes, tempo sync or a
free rate in Hz, retrigger on notes, 16 editable steps), **Follower** (input level, with attack, release and gain) and **ADSR**
(triggered by MIDI notes). Cross-modulation scales a slot by another slot's level.

## Presets
The PRESET menu lists the 492 factory presets (60 of them TUNE presets: scale and key combinations) (pitch ladders, gender, formant, harmonics, alternator, grains, effects, MIDI, LFO, sequencer and more); each starts at full wet. USER PRESETS saves the whole sound to a numbered file (1-9999) in the `presets`
folder next to the plugin; Load brings it back. Files are plain text and survive updates.
