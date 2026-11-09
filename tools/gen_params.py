#!/usr/bin/env python3
"""ALPHA: the single source of every parameter, view and layout line.

    python3 tools/gen_params.py

writes  params.json       (for mpc-vst-plugins' gen_vst.py; the list order is the VST index: APPEND ONLY once shipped)
        src/param_ids.h   (P_<KEY> constants and the tables the engine needs)
        layout.conf       (the page layout, mpc-vst-plugins syntax)
        presets.json      (the factory presets MPC lists in its PRESET menu)

ALPHA is a granular vocal transformer: pitch-synchronous grains (pitch, formant, alternator), a harmonic shifter,
smear / stereo / detune effects, MIDI pitch control and a four-slot modulation system.  Everything here follows the
public Polyverse Manipulator manual's control list; the DSP and the presets are ALPHA's own.
"""
import json
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OFF_ON = ["Off", "On"]

# ------------------------------------------------------------------------------------------------------ helpers
def knob(key, name, lo, hi, default, **kw):
    p = {"key": key, "name": name, "min": float(lo), "max": float(hi), "default": float(default), "dynamic_display": True}
    p.update(kw)
    return p


def iknob(key, name, lo, hi, default, **kw):
    """a whole-number knob; the engine writes its value text (names, steps, 'FREE')"""
    if hi - lo > 100:
        kw.setdefault("nudge_pct", 10)      # a wheel click or Q-Link event is one step on a long list
    return knob(key, name, lo, hi, default, display="int", **kw)


def opts(key, name, options, default=0, **kw):
    p = {"key": key, "name": name, "options": options, "default": default}
    p.update(kw)
    return p


def trig(key, name, **kw):
    p = {"key": key, "name": name, "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True, "type": "trigger"}
    p.update(kw)
    return p


def text(key, name, **kw):
    p = {"key": key, "name": name, "min": 0.0, "max": 1.0, "default": 0.0, "display": "string", "type": "readout"}
    p.update(kw)
    return p


def tiletext(key, name):
    """a list row: its text is get_param(key), a tap sets it to 1 and the wrapper springs it back to 0"""
    return {"key": key, "name": name, "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True, "display": "string",
            "refresh_display": True}


# ------------------------------------------------------------------------------------------------------ the views
VIEWS = [  # key, tile text
    ("overview", ""), ("pitch", "PITCH"), ("harm", "HARMONICS"), ("alt", "ALTERNATOR"), ("grain", "GRAINS"),
    ("fx", "EFFECTS"), ("midi", "MIDI"), ("src", "MOD SOURCE"), ("amt", "MOD AMOUNTS"), ("seq", "SEQUENCER"),
    ("pre", "USER PRESETS"), ("glob", "GLOBAL"), ("start", "START"),
]
TILE_ORDER = ["start", "pitch", "harm", "alt", "grain", "fx", "midi", "src", "amt", "seq", "pre", "glob"]
V = {v[0]: i for i, v in enumerate(VIEWS)}
NV = len(VIEWS)
NTILES = 12                                  # go_1 .. go_12 (12 = empty) on a 4 x 3 grid

SRC = ["Meta Knob", "MIDI", "Sequencer", "Follower", "ADSR", "LFO"]
WAVE = ["Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Smooth Rnd", "Step Rnd"]
POL = ["+", "-", "B"]
MTYPE = ["Note", "Velocity", "Pitch Wheel", "Mod Wheel", "After Touch"]
XSRC = ["Off", "Slot 1", "Slot 2", "Slot 3", "Slot 4"]
MIDIMODE = ["Off", "Mono", "Poly", "M.Gated", "P.Gated"]
DETECT = ["Low", "Mid", "High"]
DIVS = ["8 Bars", "4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/8", "1/16", "1/32",
        "1/2 T", "1/4 T", "1/8 T", "1/16 T", "1/2 .", "1/4 .", "1/8 ."]
DIV_BEATS = [32, 16, 8, 4, 2, 1, 0.5, 0.25, 0.125, 4 / 3.0, 2 / 3.0, 1 / 3.0, 1 / 6.0, 3, 1.5, 0.75]

# modulation targets: key, short name on an amount knob, what an amount of 100 % moves it by
TARGETS = [
    ("pitch", "PITCH", 24), ("smooth", "SMOOTH", 1), ("formant", "FORMNT", 12), ("harm", "HARM", 8),
    ("ratio", "RATIO", 1), ("fm", "FM", 1), ("alt", "ALT", 12), ("oct", "OCTAVE", 8),
    ("smear", "SMEAR", 1), ("stereo", "STEREO", 1), ("detune", "DETUNE", 1), ("wet", "WET", 1),
    ("jitter", "JITTER", 1), ("window", "WINDOW", 1), ("density", "DENS", 1), ("gender", "GENDER", 1),
]
NT = len(TARGETS)
# one modulation slot's fields, in order (the same order for slots 1-4)
SLOT_FIELDS = ["on", "src", "meta", "pol", "steps", "mtype", "patt", "sync", "rate", "retrig", "shape",
               "fatt", "frel", "fgain", "atk", "dec", "sus", "rel", "xsrc", "xamt"] + ["a_" + t[0] for t in TARGETS]
NF = len(SLOT_FIELDS)


def slot_params(n):
    k = "m%d_" % n
    s = "Mod %d " % n
    P = [opts(k + "on", s + "On", OFF_ON, 0), opts(k + "src", s + "Source", SRC, 5 if n == 1 else n - 1),
         knob(k + "meta", s + "Meta", 0.0, 1.0, 0.0), opts(k + "pol", s + "Polarity", POL, 0),
         iknob(k + "steps", s + "Steps", 0, 32, 0), opts(k + "mtype", s + "MIDI Type", MTYPE, 1),
         iknob(k + "patt", s + "Pattern", 0, 100, 0), iknob(k + "sync", s + "Sync", 0, 16, 4),
         knob(k + "rate", s + "Rate", 0.05, 20.0, 1.0), opts(k + "retrig", s + "Retrig", OFF_ON, 0),
         iknob(k + "shape", s + "Shape", 0, 4, 1),
         knob(k + "fatt", s + "Follow Attack", 1.0, 500.0, 10.0), knob(k + "frel", s + "Follow Release", 1.0, 2000.0, 200.0),
         knob(k + "fgain", s + "Follow Gain", 0.0, 4.0, 2.0),
         knob(k + "atk", s + "Attack", 1.0, 5000.0, 10.0), knob(k + "dec", s + "Decay", 1.0, 5000.0, 200.0),
         knob(k + "sus", s + "Sustain", 0.0, 1.0, 0.7), knob(k + "rel", s + "Release", 1.0, 5000.0, 300.0),
         opts(k + "xsrc", s + "Cross Source", XSRC, 0), knob(k + "xamt", s + "Cross Amount", 0.0, 1.0, 0.0)]
    P += [knob(k + "a_" + t[0], "%s>%s" % (s.strip(), t[1].title()), -1.0, 1.0, 0.1 if (n == 1 and t[0] == "pitch") else 0.0) for t in TARGETS]
    assert len(P) == NF
    return P


# ------------------------------------------------------------------------------------------------ the parameter list
P = []
# 0.1.0 -- the voice.  Indices 0-... never move.
P += [knob("pitch", "Pitch", -24, 24, 0), knob("smooth", "Smooth Grains", 0, 1, 0.25), knob("formant", "Formant", -12, 12, 0),
      knob("harm", "Harmonics", -8, 8, 0), knob("ratio", "Ratio", 0, 1, 1), knob("fm", "FM", 0, 1, 0),
      knob("alt", "Alternator", 0, 12, 0), knob("oct", "Octave", -8, 8, 0),
      knob("smear", "Smear", 0, 1, 0), knob("stereo", "Stereo", 0, 1, 0), knob("detune", "Detune", 0, 1, 0),
      knob("wet", "Dry/Wet", 0, 1, 1),
      opts("detect", "Detect", DETECT, 1), opts("snap", "Snap", OFF_ON, 1),
      knob("jitter", "Jitter", 0, 1, 0), knob("window", "Window", 0, 1, 0.5), knob("density", "Density", -1, 1, 0),
      knob("gender", "Gender", -1, 1, 0),
      opts("cons", "Consonant Protect", OFF_ON, 0), knob("cons_thr", "Voiced Threshold", 0.2, 0.9, 0.5),
      opts("freeze", "Freeze", OFF_ON, 0), opts("reverse", "Reverse", OFF_ON, 0),
      opts("midi_mode", "MIDI Mode", MIDIMODE, 0, refresh_display=True),
      knob("m_att", "Gate Attack", 1, 2000, 10), knob("m_rel", "Gate Release", 1, 3000, 100),
      knob("glide", "Glide", 0, 1, 0), opts("glide_m", "Glide MIDI Only", OFF_ON, 0),
      trig("panic", "Panic")]
N_VOICE = len(P)
for n in range(1, 5):
    P += slot_params(n)
P_SLOT0 = N_VOICE
N_REAL = len(P)                              # everything above is stored in the project state

# the pages' own parameters (virtual: they stand for the selected slot's)
P += [opts("mslot", "Mod Slot", ["Slot 1", "Slot 2", "Slot 3", "Slot 4"], 0, refresh_display=True)]
for f, p in zip(SLOT_FIELDS, slot_params(1)):
    q = dict(p)
    q["key"] = "ps_" + f
    q["name"] = "Slot " + f
    if f in ("src",):
        q["refresh_display"] = True
    P.append(q)
P_PS0 = N_REAL + 1
P += [knob("seq_s%d" % i, "Step %d" % i, -1.0, 1.0, 0.0) for i in range(1, 17)]
P_SEQ0 = P_PS0 + NF
P += [opts("mvs", "Source Panel", ["Off"] + SRC, 0),
      opts("view", "View", [v[1].title() or "Overview" for v in VIEWS], 0, refresh_display=True),
      opts("in_sec", "In Section", ["Overview", "Section"], 0)]
P += [tiletext("nv_%d" % i, "Nav %d" % i) for i in (1, 2, 3)]
P += [tiletext("go_%d" % i, "Go %d" % i) for i in range(1, NTILES + 1)]
P += [tiletext("lslot_%d" % i, "Slot %d" % i) for i in range(1, 5)]
P += [tiletext("lsrc_%d" % i, "Source %d" % i) for i in range(1, 6)]
P += [tiletext("lmode_%d" % i, "Mode %d" % i) for i in range(1, 6)]
P += [tiletext("cpol_1", "Polarity Tile"), tiletext("cmt_1", "MIDI Type Tile"), tiletext("cxs_1", "Cross Tile"),
      tiletext("cdt_1", "Detect Tile")]
P += [text("info", "Info"), text("upre_info", "User Preset Info"),
      iknob("upre", "User Slot", 1, 9999, 1, refresh_display=True)]
P += [tiletext("upa_%d" % i, "User Preset %d" % i) for i in (1, 2, 3)]
P += [tiletext("pnc_1", "Panic Tile")]
P += [{"key": "ql%d" % (i + 1), "name": "Q-Link %d" % (i + 1), "min": 0.0, "max": 1.0, "default": 0.0,
       "dynamic_name": True, "dynamic_display": True} for i in range(16)]
# ---- 1.1.0: appended, never reordered
P_WAVE0 = len(P)
P += [opts("m%d_wave" % n, "Mod %d LFO Wave" % n, WAVE, 0) for n in range(1, 5)]
P_PHASE0 = len(P)
P += [knob("m%d_phase" % n, "Mod %d LFO Phase" % n, 0.0, 1.0, 0.0) for n in range(1, 5)]
P += [opts("ps_wave", "Slot wave", WAVE, 0), knob("ps_phase", "Slot phase", 0.0, 1.0, 0.0)]
P += [tiletext("nv_4", "Nav 4"), tiletext("lsrc_6", "Source 6"), text("hint", "Hint")]
KEYS = [p["key"] for p in P]
assert len(KEYS) == len(set(KEYS)), "duplicate key"
IDX = {k: i for i, k in enumerate(KEYS)}

# ------------------------------------------------------------------------------------------------- Q-Link maps
def qk(*keys):
    ks = [IDX[k] for k in keys]
    assert len(ks) <= 16, keys
    return ks + [-1] * (16 - len(ks))


def amt_keys():
    return ["ps_a_" + t[0] for t in TARGETS]


VIEW_QL = [qk()] * NV
VIEW_QL = list(VIEW_QL)
VIEW_QL[V["pitch"]] = qk("pitch", "smooth", "formant", "gender")
VIEW_QL[V["harm"]] = qk("harm", "ratio", "fm")
VIEW_QL[V["alt"]] = qk("alt", "oct")
VIEW_QL[V["grain"]] = qk("jitter", "window", "density", "freeze", "reverse")
VIEW_QL[V["fx"]] = qk("smear", "stereo", "detune", "wet")
VIEW_QL[V["midi"]] = qk("midi_mode", "m_att", "m_rel", "glide", "glide_m")
VIEW_QL[V["amt"]] = qk(*amt_keys())
VIEW_QL[V["seq"]] = qk(*["seq_s%d" % i for i in range(1, 17)])
VIEW_QL[V["pre"]] = qk("upre")
VIEW_QL[V["glob"]] = qk("snap", "detect", "cons", "cons_thr")
VIEW_QL[V["start"]] = qk("pitch", "formant", "gender", "harm", "wet", "midi_mode")
SRC_BASE = ["ps_on", "mslot", "ps_src"]
SRC_SPEC = [["ps_meta", "ps_pol", "ps_steps"], ["ps_mtype"], ["ps_patt", "ps_sync", "ps_rate", "ps_shape", "ps_retrig"],
            ["ps_fatt", "ps_frel", "ps_fgain"], ["ps_atk", "ps_dec", "ps_sus", "ps_rel"],
            ["ps_wave", "ps_rate", "ps_sync", "ps_phase", "ps_retrig"]]
VIEW_QL_SRC = [qk(*(SRC_BASE + s + ["ps_xsrc", "ps_xamt"])) for s in SRC_SPEC]

# ------------------------------------------------------------------------------------------------- factory presets
# every preset: the voice and the four slots (anything not named is the default)
def slot(n, **kw):
    return {"m%d_%s" % (n, k): v for k, v in kw.items()}


def amt(n, **kw):
    return {"m%d_a_%s" % (n, k): v for k, v in kw.items()}


PRESETS = [
    ("Init", {}),
    ("Natural Octave Down", {"pitch": -12, "smooth": 0.25}),
    ("Thick Octave Down", {"pitch": -12, "smooth": 0.25, "detune": 0.35, "stereo": 0.5, "wet": 0.85}),
    ("Child", {"pitch": 7, "formant": 5, "gender": 0.4}),
    ("Giant", {"pitch": -7, "formant": -7, "smooth": 0.9}),
    ("Gender Female", {"gender": 0.7}),
    ("Gender Male", {"gender": -0.7}),
    ("Formant Alien", {"formant": 9, "pitch": -3, "jitter": 0.1}),
    ("Robot Harmonics", {"harm": 1, "ratio": 1, "fm": 0.15, "smooth": 0.2}),
    ("Octaver Ratio Half", {"harm": -1, "ratio": 0.5, "wet": 0.8}),
    ("FM Grit", {"harm": 1, "ratio": 0.75, "fm": 0.6, "wet": 0.5}),
    ("Saw Riser", {"alt": 7, "oct": 2, "smooth": 0.6}),
    ("Slow Alternator", {"alt": 5, "oct": 5, "stereo": 0.4}),
    ("Sub Thicken", {"alt": 0.01, "oct": -2, "wet": 0.7}),
    ("Sub Sonic Texture", {"alt": 0.01, "oct": -6, "wet": 0.6, "smooth": 0.5}),
    ("Glitch Stutter", {"smear": 0.55, "jitter": 0.2, "wet": 0.9}),
    ("Frozen Vowel", {"freeze": 1, "stereo": 0.6, "detune": 0.3}),
    ("Reverse Grains", {"reverse": 1, "window": 0.7, "smooth": 0.8}),
    ("Sparse Grains", {"density": -0.7, "jitter": 0.35, "window": 0.35}),
    ("Thick Grains", {"density": 0.8, "window": 0.65, "detune": 0.2}),
    ("Whisper Choir", {"jitter": 0.6, "density": 0.5, "detune": 0.5, "stereo": 0.9, "wet": 0.8}),
    ("Doubler", {"smooth": 1.0, "detune": 0.25, "stereo": 0.5, "wet": 0.5}),
    ("Wide Chorus", {"detune": 0.8, "stereo": 1.0, "wet": 0.7}),
    ("MIDI Mono Tuner", {"midi_mode": 1, "glide": 0.15}),
    ("MIDI Poly Harmonizer", {"midi_mode": 2, "stereo": 0.4}),
    ("Gated Poly Pad", {"midi_mode": 4, "m_att": 250, "m_rel": 600, "detune": 0.4, "stereo": 0.8}),
    ("Sequenced Pitch Arp", dict({"midi_mode": 0}, **slot(1, on=1, src=2, patt=44, sync=7, shape=2), **amt(1, pitch=0.5))),
    ("Follower Wobble", dict(slot(1, on=1, src=3, fgain=3.0, frel=300), **amt(1, formant=-0.6, pitch=0.12))),
    ("ADSR Pitch Dive", dict(slot(1, on=1, src=4, atk=5, dec=900, sus=0.0, rel=200, retrig=1), **amt(1, pitch=-0.5),
                              **{"midi_mode": 0})),
    ("Velocity Formant", dict(slot(1, on=1, src=1, mtype=1), **amt(1, formant=0.5, wet=0.2))),
    ("Meta Morph", dict({"smear": 0.2}, **slot(1, on=1, src=0, meta=0.5, pol=2), **amt(1, formant=0.8, harm=0.5, detune=0.3))),
]

# ---- the big factory bank (names are cut to 24 characters by the wrapper: keep them unique inside that)
def lfo(n, wave, rate=None, sync=None, **amts):
    d = slot(n, on=1, src=5, wave=WAVE.index(wave), phase=0.0, retrig=0)
    if sync is None:
        d.update(slot(n, sync=0, rate=rate))
    else:
        d.update(slot(n, sync=sync))
    d.update(amt(n, **amts))
    return d


def merge(*ds):
    out = {}
    for d in ds:
        out.update(d)
    return out


BANK = []
def add_p(name, **kw):
    BANK.append((name[:24], kw))

# pitch ladder: natural (formant stays) and shifted (formant follows the pitch)
for n in list(range(-24, 0)) + list(range(1, 25)):
    add_p("Pitch %+d Natural" % n, pitch=n, smooth=0.25)
for n in list(range(-24, 0)) + list(range(1, 25)):
    add_p("Pitch %+d Shifted" % n, pitch=n, formant=max(-12, min(12, n)), smooth=0.25)
# gender and formant
for g in (-1.0, -0.75, -0.5, -0.25, 0.25, 0.5, 0.75, 1.0):
    add_p("Gender %+d" % round(g * 100), gender=g)
for f in list(range(-12, 0)) + list(range(1, 13)):
    add_p("Formant %+d" % f, formant=f)
# harmonic shifter
for h in (1, 2, 3, 4, 5, 6, 7, 8, -1, -2, -3, -4):
    for r, rn in ((1.0, "100"), (0.5, "50"), (0.25, "25")):
        add_p("Harm %+d Ratio %s" % (h, rn), harm=h, ratio=r, smooth=0.2)
for h in (1, 2, 3):
    for fm in (0.3, 0.6):
        add_p("Harm %+d FM %d" % (h, round(fm * 100)), harm=h, fm=fm, ratio=0.75)
# alternator
for a in (2, 3, 5, 7, 12):
    for o in (0, 1, 2, 3, -2, -4):
        add_p("Alt %d Oct %+d" % (a, o), alt=a, oct=o, smooth=0.5)
# grains
for j in (0.2, 0.5, 0.8):
    for d in (-0.7, 0.0, 0.8):
        add_p("Jitter %d Dens %+d" % (round(j * 100), round(d * 100)), jitter=j, density=d, window=0.5)
for w in (0.0, 0.25, 0.75, 1.0):
    add_p("Window %d" % round(w * 100), window=w)
add_p("Reverse Soft", reverse=1, window=0.4)
add_p("Reverse Long", reverse=1, window=1.0, smooth=0.6)
add_p("Freeze Wide", freeze=1, stereo=1.0, detune=0.5)
add_p("Freeze Deep", freeze=1, pitch=-12)
# effects
for sm in (0.1, 0.25, 0.4, 0.55, 0.7, 0.85, 1.0):
    add_p("Smear %d" % round(sm * 100), smear=sm)
for dt in (0.2, 0.4, 0.6, 0.8, 1.0):
    add_p("Detune %d" % round(dt * 100), detune=dt, stereo=0.4)
for st in (0.25, 0.5, 0.75, 1.0):
    add_p("Stereo %d" % round(st * 100), stereo=st)
add_p("Wide Octave Pair", pitch=-12, detune=0.6, stereo=1.0)
add_p("Dream Smear", smear=0.6, stereo=0.8, detune=0.3, wet=0.9)
add_p("Shimmer Up", pitch=12, smear=0.3, detune=0.5, stereo=0.9, wet=0.6)
# MIDI
for gl in (0.0, 0.1, 0.3, 0.6):
    add_p("MIDI Mono Glide %d" % round(gl * 100), midi_mode=1, glide=gl)
for gl in (0.0, 0.2):
    add_p("MIDI Poly Glide %d" % round(gl * 100), midi_mode=2, glide=gl, stereo=0.4)
for a, r, nm in ((10, 100, "Short"), (80, 300, "Soft"), (250, 800, "Pad"), (600, 1500, "Slow")):
    add_p("Gated Mono %s" % nm, midi_mode=3, m_att=a, m_rel=r)
    add_p("Gated Poly %s Mix" % nm, midi_mode=4, m_att=a, m_rel=r, detune=0.3, stereo=0.7)
add_p("MIDI Fifth Choir", midi_mode=4, m_att=150, m_rel=500, formant=3, stereo=0.8, detune=0.4)
add_p("MIDI Glide M Lead", midi_mode=1, glide=0.35, glide_m=1)
# LFO
RATES = (("Slow", 0.4), ("Fast", 5.0))
TGT = (("Pitch", dict(pitch=0.12), {}), ("Formant", dict(formant=0.35), {}), ("Gender", dict(gender=0.5), {}),
       ("Wet", dict(wet=0.25), {"wet": 0.75}), ("Harm", dict(harm=0.15), {"harm": 1, "smooth": 0.2}))
WSH = (("Sine", "Sine"), ("Triangle", "Tri"), ("Saw Up", "SawUp"), ("Saw Down", "SawDn"), ("Square", "Sq"),
       ("Smooth Rnd", "SmRnd"), ("Step Rnd", "Step"))
for wv, ws in WSH:
    for tn, am, base in TGT:
        for rn, rt in RATES:
            add_p("LFO %s %s %s" % (ws, tn, rn), **merge(base, lfo(1, wv, rate=rt, **am)))
add_p("Vibrato Natural", **lfo(1, "Sine", rate=5.5, pitch=0.05))
add_p("Vibrato Wide", **lfo(1, "Sine", rate=4.5, pitch=0.12))
add_p("Siren", **lfo(1, "Triangle", rate=0.7, pitch=0.9))
add_p("Pitch Sample Hold", **merge({"snap": 1}, lfo(1, "Step Rnd", rate=6.0, pitch=0.5)))
add_p("Gender Drift", **lfo(1, "Smooth Rnd", rate=0.35, gender=0.8))
add_p("Vowel Sweep", **lfo(1, "Sine", rate=0.5, formant=0.8))
add_p("Two LFO Wobble", **merge(lfo(1, "Sine", rate=0.6, formant=0.6), lfo(2, "Triangle", rate=4.5, pitch=0.04)))
add_p("LFO Sync 1/4 Pitch", **lfo(1, "Triangle", sync=6, pitch=0.25))
add_p("LFO Sync 1/8 Formant", **lfo(1, "Sine", sync=7, formant=0.6))
add_p("LFO Sync 1/16 Gate", **merge({"wet": 0.5}, lfo(1, "Square", sync=8, wet=0.5)))
add_p("LFO Sync Bar Harm", **merge({"harm": 1}, lfo(1, "Saw Up", sync=4, harm=0.3)))
# sequencer: 20 melodic patterns x two speeds, pitch in whole semitones (amount 50 %)
for k in range(20, 40):
    for sy, sn in ((7, "1/8"), (8, "1/16")):
        add_p("Seq Arp %d %s" % (k, sn), **merge(slot(1, on=1, src=2, patt=k, sync=sy, shape=2), amt(1, pitch=0.5)))
for k in range(40, 50):
    add_p("Seq Gate %d" % k, **merge({"wet": 0.0}, slot(1, on=1, src=2, patt=k, sync=8, shape=2), amt(1, wet=1.0)))
for k in (60, 61, 62, 63, 64, 65):
    add_p("Seq Formant %d" % k, **merge(slot(1, on=1, src=2, patt=k, sync=4, shape=1), amt(1, formant=0.6)))
add_p("Seq Random Pitch", **merge(slot(1, on=1, src=2, patt=100, sync=7, shape=2), amt(1, pitch=0.4)))
# other sources
add_p("Follower Gender", **merge(slot(1, on=1, src=3, fgain=3.0, fatt=8, frel=250), amt(1, gender=0.6)))
add_p("Follower Wet", **merge({"wet": 0.4}, slot(1, on=1, src=3, fgain=3.0), amt(1, wet=0.6)))
add_p("Follower Harm", **merge({"harm": 1}, slot(1, on=1, src=3, fgain=3.0), amt(1, harm=0.4)))
add_p("ADSR Formant Sweep", **merge(slot(1, on=1, src=4, atk=400, dec=1500, sus=0.3, rel=500), amt(1, formant=0.7)))
add_p("ADSR Gender Fall", **merge(slot(1, on=1, src=4, atk=5, dec=1200, sus=0.0, rel=200), amt(1, gender=-0.8)))
add_p("Velocity Pitch", **merge({"midi_mode": 0}, slot(1, on=1, src=1, mtype=1), amt(1, pitch=0.25)))
add_p("Mod Wheel Formant", **merge(slot(1, on=1, src=1, mtype=3), amt(1, formant=1.0)))
add_p("Pitch Wheel Bend", **merge({"midi_mode": 1}, slot(1, on=1, src=1, mtype=2), amt(1, pitch=0.2)))
add_p("Aftertouch Wet", **merge({"wet": 0.3}, slot(1, on=1, src=1, mtype=4), amt(1, wet=0.7)))
add_p("Meta Pitch Formant", **merge(slot(1, on=1, src=0, meta=0.5, pol=2), amt(1, pitch=0.3, formant=0.5)))
add_p("Meta Everything", **merge(slot(1, on=1, src=0, meta=0.5, pol=2), amt(1, pitch=0.2, formant=0.4, harm=0.3, detune=0.3, stereo=0.4)))
PRESETS += BANK
assert len(set(n for n, _ in PRESETS)) == len(PRESETS), "preset names must be unique"
PRESET_KEYS_ORDER = [p["key"] for p in P[:N_VOICE] if not p.get("momentary")]
SLOT_KEYS = {n: [p["key"] for p in P[P_SLOT0 + (n - 1) * NF:P_SLOT0 + n * NF]] + ["m%d_wave" % n, "m%d_phase" % n] for n in range(1, 5)}

# ------------------------------------------------------------------------------------------------- layout
L = []
def add(s):
    L.append(s)


def when(view):
    return "when=view:%d" % V[view]


def knobs_row(view, items, cy, xs, r=56, bw=150):
    for (key, label), cx in zip(items, xs):
        add('knob cx=%d cy=%d r=%d label="%s" key=%s bw=%d %s' % (cx, cy, r, label, key, bw, when(view)))


def header(view):
    add("list x=16 y=100 w=640 h=52 cols=4 rows=1 th=52 gap=8 key=nv tsize=26 tweight=700 talign=center when=in_sec:1")
    add("readout cx=965 cy=126 w=600 h=44 label=\"\" key=info tsize=26 tweight=700 when=in_sec:1")


add("# ALPHA for MPC OS -- page layout. GENERATED by tools/gen_params.py, edit that, not this.")
add("# One tab; each page is a mode panel shown while 'view' is at it. Page 0 is the overview: tap a tile to open a page.")
add("# Coordinates are Force Shadow pixels: the plugin area is x 0..1280, y 86..714. Text sizes are the Meta4MPCXXL 0.1.2")
add("# standard: label_scale 1.7 (knob names 35.7 px, values 37.4 px), tile rows 26-32 px Bold, readouts 26-34 px.")
add("scale_names=1")
add("label_scale=1.7")
for k, v in [("bg", "0e0e0e"), ("panel", "0e0e0e"), ("line", "3a3a3a"), ("ink", "ffffff"), ("ink_dim", "ffffff"),
             ("ink_faint", "ffffff"), ("accent", "1ab463"), ("accent_hi", "ffffff"), ("knob_face", "1c1c1c"),
             ("knob_ring", "1ab463"), ("knob_dot", "ffffff"), ("bar", "0e0e0e"), ("seg_active", "1ab463"),
             ("seg_inactive", "1c1c1c"), ("seg_active_tx", "111111"), ("btn_text", "ffffff"), ("tab_on", "ffffff"),
             ("tab_on_tx", "111111"), ("lcd", "1c1c1c"), ("box", "1c1c1c"), ("tile_on", "1ab463")]:
    add("theme_%s=%s" % (k, v))
add("qlinks_track = " + ",".join("ql%d" % (i + 1) for i in range(16)))
add("")
add("[tab ALPHA]")
# ---- overview
add("readout cx=640 cy=126 w=1240 h=44 label=\"\" key=info tsize=30 tweight=700 when=view:0")
add("list x=16 y=160 w=1248 h=540 cols=4 rows=3 th=172 gap=10 key=go tsize=32 tweight=700 talign=center when=view:0")
# ---- section pages
header("")
X4 = [160, 480, 800, 1120]
R1, R2 = 338, 554
knobs_row("pitch", [("pitch", "PITCH"), ("smooth", "SMOOTH"), ("formant", "FORMANT"), ("gender", "GENDER")], R1, X4)
knobs_row("harm", [("harm", "HARM"), ("ratio", "RATIO"), ("fm", "FM")], R1, X4)
knobs_row("alt", [("alt", "ALT"), ("oct", "OCTAVE")], R1, X4)
knobs_row("grain", [("jitter", "JITTER"), ("window", "WINDOW"), ("density", "DENSITY")], R1, X4)
add("toggle cx=160 cy=%d label=\"FREEZE\" key=freeze bw=150 %s" % (R2, when("grain")))
add("toggle cx=480 cy=%d label=\"REVERSE\" key=reverse bw=150 %s" % (R2, when("grain")))
knobs_row("fx", [("smear", "SMEAR"), ("stereo", "STEREO"), ("detune", "DETUNE"), ("wet", "WET")], R1, X4)
# midi
add("list x=16 y=170 w=1248 h=60 cols=5 rows=1 th=60 gap=8 key=lmode tsize=28 tweight=700 talign=center %s" % when("midi"))
knobs_row("midi", [("glide", "GLIDE")], R1 + 40, [160])
add("toggle cx=480 cy=%d label=\"GLIDE M\" key=glide_m bw=150 %s" % (R1 + 40, when("midi")))
for mode in ("m.gated", "p.gated"):
    add('knob cx=800 cy=%d r=56 label="ATTACK" key=m_att bw=150 when=midi_mode:%s' % (R1 + 40, mode))
    add('knob cx=1120 cy=%d r=56 label="RELEASE" key=m_rel bw=150 when=midi_mode:%s' % (R1 + 40, mode))
# mod source page: slot list, on, source list, per-source controls, cross-mod
add("list x=16 y=170 w=520 h=52 cols=4 rows=1 th=52 gap=8 key=lslot tsize=26 tweight=700 talign=center %s" % when("src"))
add("toggle cx=700 cy=164 label=\"ON\" key=ps_on bw=120 %s" % when("src"))
add("list x=16 y=232 w=1248 h=52 cols=6 rows=1 th=52 gap=8 key=lsrc tsize=26 tweight=700 talign=center %s" % when("src"))
def srcknob(state, cx, cy, key, label, r=54):
    add('knob cx=%d cy=%d r=%d label="%s" key=%s bw=150 when=mvs:%d' % (cx, cy, r, label, key, state))
SY = 396
srcknob(1, 160, SY, "ps_meta", "META")
add("list x=330 y=%d w=300 h=60 cols=1 rows=1 th=60 gap=8 key=cpol tsize=28 tweight=700 talign=center when=mvs:1" % (SY - 30))
srcknob(1, 800, SY, "ps_steps", "STEPS")
add("list x=16 y=%d w=520 h=60 cols=1 rows=1 th=60 gap=8 key=cmt tsize=28 tweight=700 talign=center when=mvs:2" % (SY - 30))
add("list x=16 y=%d w=520 h=60 cols=1 rows=1 th=60 gap=8 key=cxs tsize=28 tweight=700 talign=center %s" % (R2 - 4, when("src")))
srcknob(3, 160, SY, "ps_patt", "PATTERN")
srcknob(3, 480, SY, "ps_sync", "SYNC")
srcknob(3, 800, SY, "ps_rate", "RATE")
srcknob(3, 1120, SY, "ps_shape", "SHAPE")
srcknob(4, 160, SY, "ps_fatt", "ATTACK")
srcknob(4, 480, SY, "ps_frel", "RELEASE")
srcknob(4, 800, SY, "ps_fgain", "GAIN")
srcknob(5, 160, SY, "ps_atk", "ATTACK")
srcknob(5, 480, SY, "ps_dec", "DECAY")
srcknob(5, 800, SY, "ps_sus", "SUSTAIN")
srcknob(5, 1120, SY, "ps_rel", "RELEASE")
srcknob(6, 160, SY, "ps_wave", "WAVE")
srcknob(6, 480, SY, "ps_rate", "RATE")
srcknob(6, 800, SY, "ps_sync", "SYNC")
srcknob(6, 1120, SY, "ps_phase", "PHASE")
add("toggle cx=480 cy=%d label=\"RETRIG\" key=ps_retrig bw=150 when=mvs:6" % (R2 + 18))
add('knob cx=1120 cy=%d r=54 label="X AMT" key=ps_xamt bw=150 %s' % (R2 + 1, when("src")))
add("toggle cx=800 cy=%d label=\"RETRIG\" key=ps_retrig bw=150 when=mvs:3" % (R2 + 18))
# amounts: 16 across two rows
XS8 = [90 + 157 * i for i in range(8)]
knobs_row("amt", [("ps_a_" + t[0], t[1]) for t in TARGETS[:8]], R1, XS8, r=52, bw=150)
knobs_row("amt", [("ps_a_" + t[0], t[1]) for t in TARGETS[8:]], R2, XS8, r=52, bw=150)
add("list x=16 y=170 w=520 h=52 cols=4 rows=1 th=52 gap=8 key=lslot tsize=26 tweight=700 talign=center %s" % when("amt"))
# sequencer: 16 steps
add("list x=16 y=170 w=520 h=52 cols=4 rows=1 th=52 gap=8 key=lslot tsize=26 tweight=700 talign=center %s" % when("seq"))
knobs_row("seq", [("seq_s%d" % i, "%02d" % i) for i in range(1, 9)], R1, XS8, r=52, bw=150)
knobs_row("seq", [("seq_s%d" % i, "%02d" % i) for i in range(9, 17)], R2, XS8, r=52, bw=150)
# user presets
knobs_row("pre", [("upre", "SLOT")], R1, [160], r=58)
add("readout cx=800 cy=300 w=820 h=60 label=\"\" key=upre_info tsize=34 tweight=700 %s" % when("pre"))
add("list x=330 y=400 w=920 h=90 cols=3 rows=1 th=90 gap=10 key=upa tsize=32 tweight=700 talign=center %s" % when("pre"))
# global
add("toggle cx=160 cy=%d label=\"SNAP\" key=snap bw=150 %s" % (R1, when("glob")))
add("toggle cx=480 cy=%d label=\"CONS PROT\" key=cons bw=190 %s" % (R1, when("glob")))
knobs_row("glob", [("cons_thr", "VOICED")], R1, [800])
add("list x=16 y=%d w=420 h=70 cols=1 rows=1 th=70 gap=8 key=cdt tsize=30 tweight=700 talign=center %s" % (R2 - 35, when("glob")))
add("list x=700 y=%d w=420 h=70 cols=1 rows=1 th=70 gap=8 key=pnc tsize=30 tweight=700 talign=center %s" % (R2 - 35, when("glob")))
# start page: the six controls that matter most
knobs_row("start", [("pitch", "PITCH"), ("formant", "FORMANT"), ("gender", "GENDER"), ("harm", "HARM")], R1, X4)
knobs_row("start", [("wet", "WET")], R2, [160])
add("list x=330 y=%d w=920 h=70 cols=5 rows=1 th=70 gap=8 key=lmode tsize=28 tweight=700 talign=center %s" % (R2 - 35, when("start")))
# hints: one line that says what a page's controls need (empty when nothing is missing)
for v in ("pitch", "harm", "alt", "fx", "midi", "glob"):
    add('readout cx=640 cy=604 w=1240 h=44 label="" key=hint tsize=26 tweight=700 %s' % when(v))
add("")
# Q-Links: ql1..ql16 follow the page (dynamic name and value), like PS3100XXL
add('qlinks "ALPHA" = ' + ",".join("ql%d" % (i + 1) for i in range(16)))

# ------------------------------------------------------------------------------------------------- write
def write_all():
    json.dump({"name": "ALPHA", "params": P}, open(os.path.join(ROOT, "params.json"), "w"), indent=1)
    open(os.path.join(ROOT, "layout.conf"), "w").write("\n".join(L) + "\n")
    defaults = {p["key"]: p.get("default", 0) for p in P}
    pres = []
    byk = {p["key"]: p for p in P}
    for name, ov in PRESETS:
        vals = {k: defaults[k] for k in PRESET_KEYS_ORDER}
        for n in range(1, 5):
            if any(k.startswith("m%d_" % n) for k in ov):
                for k in SLOT_KEYS[n]:
                    vals[k] = defaults[k]
            else:
                vals["m%d_on" % n] = 0
        for k, v in ov.items():
            assert k in vals, (name, k)
            vals[k] = v
        pres.append({"name": name, "values": {k: (int(v) if "options" in byk[k] else v) for k, v in vals.items()}})
    json.dump({"presets": pres}, open(os.path.join(ROOT, "presets.json"), "w"), indent=0)
    H = ["// GENERATED by tools/gen_params.py: do not edit.", "#pragma once"]
    for i, k in enumerate(KEYS):
        H.append("#define P_%s %d" % (k.upper(), i))
    H += ["#define P_COUNT %d" % len(KEYS), "#define P_N_REAL %d" % N_REAL, "#define P_N_VOICE %d" % N_VOICE, "#define P_WAVE0 %d" % P_WAVE0, "#define P_PHASE0 %d" % P_PHASE0,
          "#define P_SLOT0 %d" % P_SLOT0, "#define P_PS0 %d" % P_PS0, "#define P_SEQ0 %d" % P_SEQ0,
          "#define N_FIELDS %d" % NF, "#define N_TARGETS %d" % NT, "#define N_VIEWS %d" % NV, "#define N_TILES %d" % NTILES,
          "enum { F_ON, F_SRC, F_META, F_POL, F_STEPS, F_MTYPE, F_PATT, F_SYNC, F_RATE, F_RETRIG, F_SHAPE, F_FATT, F_FREL, "
          "F_FGAIN, F_ATK, F_DEC, F_SUS, F_REL, F_XSRC, F_XAMT, F_AMT0 };"]
    for i, v in enumerate(VIEWS):
        H.append("#define V_%s %d" % (v[0].upper(), i))
    TV = dict((v[0], i) for i, v in enumerate(VIEWS))
    H.append("static const char* const kTileText[N_TILES] = {" + ", ".join('"%s"' % VIEWS[TV[t]][1] for t in TILE_ORDER) + "};")
    H.append("static const int kTileView[N_TILES] = {" + ", ".join(str(TV[t]) for t in TILE_ORDER) + "};")
    def arr(name, ctype, vals):
        H.append("static const %s %s[] = {%s};" % (ctype, name, ", ".join(str(v) for v in vals)))
    H.append("static const short kViewQl[N_VIEWS][16] = {" + ", ".join("{" + ", ".join(map(str, r)) + "}" for r in VIEW_QL) + "};")
    H.append("static const short kSrcQl[6][16] = {" + ", ".join("{" + ", ".join(map(str, r)) + "}" for r in VIEW_QL_SRC) + "};")
    arr("kTargetParam", "short", [IDX[t[0]] for t in TARGETS])
    arr("kTargetSpan", "float", ["%.6gf" % float(t[2]) if "." in "%.6g" % float(t[2]) else "%.1ff" % float(t[2]) for t in TARGETS])
    H.append("static const char* const kDivNames[17] = {\"FREE\", " + ", ".join('"%s"' % d for d in DIVS) + "};")
    arr("kDivBeats", "float", [("%.6g" % b if "." in "%.6g" % b else "%.1f" % b) + "f" for b in DIV_BEATS])
    H.append("static const char* const kSrcNames[6] = {" + ", ".join('"%s"' % s for s in SRC) + "};")
    H.append("static const char* const kWaveNames[7] = {" + ", ".join('"%s"' % s for s in WAVE) + "};")
    H.append("static const char* const kMtypeNames[5] = {" + ", ".join('"%s"' % s for s in MTYPE) + "};")
    H.append("static const char* const kModeNames[5] = {" + ", ".join('"%s"' % s for s in MIDIMODE) + "};")
    H.append("static const char* const kDetectNames[3] = {" + ", ".join('"%s"' % s for s in DETECT) + "};")
    H.append("static const char* const kTargetNames[N_TARGETS] = {" + ", ".join('"%s"' % t[1] for t in TARGETS) + "};")
    open(os.path.join(ROOT, "src", "param_ids.h"), "w").write("\n".join(H) + "\n")
    print("params: %d (%d stored), views %d, presets %d" % (len(P), N_REAL, NV, len(PRESETS)))


write_all()
