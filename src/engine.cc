// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// ALPHA for MPC OS: a granular vocal transformer, built on the mpc-vst-plugins engine interface (wrapper/engine.h).
//
// Signal flow (the order the public Polyverse Manipulator manual describes, with ALPHA's own DSP):
//   input (mono sum) -> Smear -> pitch-synchronous grains (Pitch, Formant, Smooth Grains, Alternator, MIDI voices,
//   Jitter, Window, Density, Freeze, Reverse) -> Harmonic shifter (Harmonics, Ratio, FM) -> Detune -> Stereo
//   -> Dry/Wet (the dry path is delayed to line up) -> limiter.   Consonant protect passes unvoiced sounds around the
//   grains.  Four modulation slots (Meta Knob, MIDI, Sequencer, Follower, ADSR) move 16 targets.
//
// Pages: one tab, a 'view' parameter picks the page (the overview tiles open them), and Q-Links ql1..ql16 follow the
// page, like PS3100XXL.  Q-Links, taps and the data wheel use the gesture rules learned on a Force (PlateauXXL 0.4):
// a switch flips once per turn, an option list moves one step per 0.2 s, a trigger fires once.
//
// Latency (measured with a click through the x86 build of this engine, wet 0, limiter included): Detect High 21 ms,
// Mid 33 ms, Low 58 ms.  The grains look back about one period, so the wet path is that much later than the dry path
// by an amount that follows the voice; the dry path is delayed by a fixed one-period guess (300 samples, a 147 Hz voice).
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <new>

#include <pthread.h>
#include <sys/stat.h>
#include <time.h>

#include "denormals.h"
#include "dsp.h"
#include "limiter.h"
#include "param_ids.h"

extern "C" {
#include "engine.h"
#include "params.h"
}

namespace {

using namespace alpha;

const int kBlock = 128;
const int kMaxUser = 9999;
const int kStateMax = 8192;
const uint64_t kStepLockFrames = 8820;    // 0.2 s: the shortest gap between two one-step moves of an option list
const uint64_t kGestureFrames = 15435;    // 0.35 s: sets of a switch or trigger closer than this are one turn
const int kFreezeN = 8192;

static_assert(P_COUNT <= NPARAMS, "params.h is out of date: run tools/gen_params.py, then build");

struct Voice {
  int note;            // -1 none
  bool gate;
  float vel;
  float env;           // 0..1, per block
  float lv;            // smoothed log2 of the output frequency (abs) or of the pitch ratio (rel)
  bool abs;            // note-driven (lv is log2 Hz) or follows the voice (lv is log2 ratio)
  bool active;
  double next[2];      // next grain centre, per stream (main, sub-octave)
  unsigned k[2];       // grains made, per stream
  long order;          // note-on order, for stealing
  float tout;          // synthesis spacing in samples (block value)
};

struct Instance {
  Grains g;
  PitchDetector pd;
  Smear smear;
  Detune det;
  Widener wid;
  FreqShifter fs;
  plateau::Limiter lim;
  Rng rng;

  float param[P_COUNT];
  float seqv[4][16];

  // dry path and voiced flags, aligned with the output
  float dryl[kInN], dryr[kInN];
  unsigned char vf[kInN];
  float fz[kFreezeN];
  bool frozen;
  float frozen_T;

  int range;            // detect range in use
  int E;                // output hold, samples
  int align;            // dry delay, samples
  float align_f;        // ALIGN: the dry delay in use (follows the voice's period), samples
  float tune_corr, tune_tgt;   // TUNE: pitch correction now / wanted, semitones
  bool voiced;
  float vmix;
  int jumps;
  bool have_pitch;
  int an_tick;
  float f0_out;         // output pitch of voice 0, Hz

  Voice v[4];
  int held[16], nheld;
  long note_order;
  float mod_note, mod_vel, mod_pw, mod_mw, mod_at;
  bool any_gate;

  // modulation state
  float seq_phase[4];
  float adsr_v[4];
  float lfo_a[4], lfo_b[4];                      // the random LFO waves: the value just left and the one ahead
  int adsr_stage[4];    // 0 idle, 1 attack, 2 decay, 3 sustain, 4 release
  float fol_env;
  float src_val[4];
  float mod[N_TARGETS];
  float beat;
  float bpm;
  bool restart_pending;
  bool retrig_pending;

  // effective (modulated) values for the block
  float e_pitch, e_smooth, e_formant, e_harm, e_ratio, e_fm, e_alt, e_oct, e_smear, e_stereo, e_detune, e_wet, e_jitter,
      e_window, e_density, e_gender;

  // UI
  int view;
  uint64_t frames;
  uint64_t last_touch[P_COUNT], last_step[P_COUNT];
  bool loading;
  volatile int display_rev;
  volatile int panic_pending;

  // user presets
  char preset_dir[512];
  pthread_mutex_t mu;
  char* save_text;
  int save_slot, load_slot;
  char* loaded_text;
  char* retired_text;
  volatile unsigned char used[(kMaxUser + 8) / 8 + 1];
  volatile int status;      // 0 idle, 1 saved, 2 loaded, 3 save failed, 4 empty, 5 load failed
  volatile int status_slot;
  pthread_t worker;
  bool worker_running;
  volatile bool worker_quit;
  pthread_mutex_t wmu;
  pthread_cond_t wcv;
};

// ------------------------------------------------------------------------------------------------ parameter helpers
float MinOf(int p) { return PARAMS[p].nopts > 0 ? 0.0f : PARAMS[p].min; }
float MaxOf(int p) { return PARAMS[p].nopts > 0 ? static_cast<float>(PARAMS[p].nopts - 1) : PARAMS[p].max; }
float DefaultValue(int p) {
  if (PARAMS[p].nopts > 0) return floorf(PARAMS[p].def * (PARAMS[p].nopts - 1) + 0.5f);
  return PARAMS[p].min + PARAMS[p].def * (PARAMS[p].max - PARAMS[p].min);
}
int FindParam(const char* key, size_t len) {
  for (int p = 0; p < P_COUNT; ++p)
    if (strlen(PARAMS[p].key) == len && !strncmp(key, PARAMS[p].key, len)) return p;
  return -1;
}
inline int Opt(const Instance* s, int p) { return static_cast<int>(s->param[p] + 0.5f); }
inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
void Bump(Instance* s) { __atomic_add_fetch(&s->display_rev, 1, __ATOMIC_RELAXED); }

int Slot(const Instance* s) { return Clampi(Opt(s, P_MSLOT), 0, 3); }
// a page parameter (ps_*, seq_s*) stands for the selected slot's: -> the stored param index, or -1 (seq step)
int Redirect(const Instance* s, int p) {
  if (p >= P_PS0 && p < P_PS0 + N_FIELDS) return P_SLOT0 + Slot(s) * N_FIELDS + (p - P_PS0);
  if (p == P_PS_WAVE) return P_WAVE0 + Slot(s);
  if (p == P_PS_PHASE) return P_PHASE0 + Slot(s);
  return p;
}
int LsrcIdx(int p) {
  if (p >= P_LSRC_1 && p <= P_LSRC_5) return p - P_LSRC_1;
  return p == P_LSRC_6 ? 5 : -1;
}
int StepIndex(int p) { return (p >= P_SEQ0 && p < P_SEQ0 + 16) ? p - P_SEQ0 : -1; }
inline float Field(const Instance* s, int slot, int f) { return s->param[P_SLOT0 + slot * N_FIELDS + f]; }

float EngineRange(int range, int* E) {
  static const int Es[3] = {2200, 1100, 560};
  *E = Es[range];
  return 0;
}

// ------------------------------------------------------------------------------------------------ pattern loading
void LoadPattern(Instance* s, int slot) {
  MakePattern(Opt(s, P_SLOT0 + slot * N_FIELDS + F_PATT), s->seqv[slot], &s->rng);
}

// ------------------------------------------------------------------------------------------------ user presets
void PresetPath(const Instance* s, int slot, char* buf, size_t len) {
  snprintf(buf, len, "%s/User %03d.txt", s->preset_dir, slot + 1);
}
inline bool Used(const Instance* s, int slot) { return (s->used[slot >> 3] >> (slot & 7)) & 1; }
inline void SetUsed(Instance* s, int slot, bool on) {
  if (on) s->used[slot >> 3] |= (unsigned char)(1u << (slot & 7));
  else s->used[slot >> 3] &= (unsigned char)~(1u << (slot & 7));
}
void ScanPresets(Instance* s) {
  char path[600];
  for (int i = 0; i < kMaxUser; ++i) {
    struct stat st;
    PresetPath(s, i, path, sizeof path);
    SetUsed(s, i, stat(path, &st) == 0 && S_ISREG(st.st_mode));
  }
}
void SetStatus(Instance* s, int slot, int st) {
  s->status_slot = slot;
  s->status = st;
  Bump(s);
}
bool WriteText(const char* dir, const char* path, const char* text) {
  mkdir(dir, 0777);
  char tmp[640];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE* f = fopen(tmp, "w");
  if (!f) return false;
  size_t len = strlen(text);
  bool ok = fwrite(text, 1, len, f) == len;
  ok = (fclose(f) == 0) && ok;
  if (ok) ok = rename(tmp, path) == 0;
  if (!ok) remove(tmp);
  return ok;
}
char* ReadText(const char* path) {
  FILE* f = fopen(path, "r");
  if (!f) return NULL;
  char* buf = static_cast<char*>(malloc(kStateMax));
  size_t len = buf ? fread(buf, 1, kStateMax - 1, f) : 0;
  fclose(f);
  if (!buf) return NULL;
  buf[len] = 0;
  return buf;
}
void PresetWork(Instance* s) {
  free(__atomic_exchange_n(&s->retired_text, (char*)NULL, __ATOMIC_ACQ_REL));
  pthread_mutex_lock(&s->mu);
  char* text = s->save_text;
  int save_slot = s->save_slot, load_slot = s->load_slot;
  s->save_text = NULL;
  s->load_slot = -1;
  pthread_mutex_unlock(&s->mu);
  char path[600];
  if (text) {
    PresetPath(s, save_slot, path, sizeof path);
    bool ok = WriteText(s->preset_dir, path, text);
    free(text);
    if (ok) SetUsed(s, save_slot, true);
    SetStatus(s, save_slot, ok ? 1 : 3);
  }
  if (load_slot >= 0) {
    PresetPath(s, load_slot, path, sizeof path);
    char* loaded = ReadText(path);
    if (!loaded) {
      SetUsed(s, load_slot, false);
      SetStatus(s, load_slot, 4);
    } else {
      s->status_slot = load_slot;
      free(__atomic_exchange_n(&s->loaded_text, loaded, __ATOMIC_ACQ_REL));
    }
  }
}
void* WorkerLoop(void* arg) {
  Instance* s = static_cast<Instance*>(arg);
  ScanPresets(s);
  Bump(s);
  while (!s->worker_quit) {
    pthread_mutex_lock(&s->wmu);
    timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += 50000000;
    if (until.tv_nsec >= 1000000000) { until.tv_sec += 1; until.tv_nsec -= 1000000000; }
    if (!s->worker_quit) pthread_cond_timedwait(&s->wcv, &s->wmu, &until);
    pthread_mutex_unlock(&s->wmu);
    PresetWork(s);
  }
  return NULL;
}

// ------------------------------------------------------------------------------------------------ state
bool StateSkips(int p) {
  if (p >= P_WAVE0 && p < P_PHASE0 + 4) return false;          // the LFO wave and phase are stored
  if (p >= P_ALIGN && p <= P_TUNE_AMOUNT) return false;        // 1.2.0: ALIGN and TUNE are stored
  return p >= P_N_REAL && p != P_MSLOT && p != P_UPRE;
}

int SaveState(const Instance* s, char* buf, int buf_len) {
  int len = 0;
  for (int p = 0; p < P_COUNT && len < buf_len - 40; ++p) {
    if (StateSkips(p) || PARAMS[p].momentary) continue;
    if (s->param[p] == DefaultValue(p)) continue;                  // a missing key is the default
    len += snprintf(buf + len, buf_len - len, "%s=%g;", PARAMS[p].key, s->param[p]);
  }
  for (int n = 0; n < 4 && len < buf_len - 200; ++n) {
    len += snprintf(buf + len, buf_len - len, "sq%d=", n + 1);
    for (int i = 0; i < 16; ++i) len += snprintf(buf + len, buf_len - len, "%s%g", i ? "," : "", s->seqv[n][i]);
    len += snprintf(buf + len, buf_len - len, ";");
  }
  return len < buf_len ? len : buf_len - 1;
}

void SetParam(void* inst, const char* key, const char* val);
void ResetParams(Instance* s, bool keep_ui) {
  for (int p = 0; p < P_COUNT; ++p) {
    if (keep_ui && (p == P_MSLOT || p == P_UPRE || p == P_VIEW)) continue;
    s->param[p] = DefaultValue(p);
  }
  for (int n = 0; n < 4; ++n) LoadPattern(s, n);
}

void LoadState(Instance* s, const char* state, bool preset) {
  char item[320];
  s->loading = true;
  ResetParams(s, true);
  while (*state) {
    size_t n = strcspn(state, ";");
    if (n < sizeof item) {
      memcpy(item, state, n);
      item[n] = 0;
      char* eq = strchr(item, '=');
      if (eq) {
        *eq = 0;
        const char* val = eq + 1;
        if (!strncmp(item, "sq", 2) && item[2] >= '1' && item[2] <= '4' && !item[3]) {
          int slot = item[2] - '1';
          const char* c = val;
          for (int i = 0; i < 16 && *c; ++i) {
            s->seqv[slot][i] = Clamp(static_cast<float>(atof(c)), -1.0f, 1.0f);
            const char* q = strchr(c, ',');
            if (!q) break;
            c = q + 1;
          }
        } else if (strcmp(item, "state") && !(preset && (!strcmp(item, "upre") || !strcmp(item, "mslot")))) {
          int p = FindParam(item, strlen(item));
          if (p >= 0 && !PARAMS[p].momentary) s->param[p] = Clamp(static_cast<float>(atof(val)), MinOf(p), MaxOf(p));
        }
      }
    }
    state += n;
    if (*state == ';') ++state;
  }
  s->loading = false;
  Bump(s);
}

void RequestSave(Instance* s) {
  char* text = static_cast<char*>(malloc(kStateMax));
  if (!text) return;
  SaveState(s, text, kStateMax);
  pthread_mutex_lock(&s->mu);
  free(s->save_text);
  s->save_text = text;
  s->save_slot = Clampi(Opt(s, P_UPRE) - 1, 0, kMaxUser - 1);
  pthread_mutex_unlock(&s->mu);
}
void RequestLoad(Instance* s) {
  pthread_mutex_lock(&s->mu);
  s->load_slot = Clampi(Opt(s, P_UPRE) - 1, 0, kMaxUser - 1);
  pthread_mutex_unlock(&s->mu);
}

// ------------------------------------------------------------------------------------------------ DSP state
void ResetVoices(Instance* s) {
  for (int i = 0; i < 4; ++i) {
    Voice& v = s->v[i];
    memset(&v, 0, sizeof v);
    v.note = -1;
  }
  s->nheld = 0;
  s->note_order = 0;
}

void ResetDsp(Instance* s) {
  float keepT = s->g.T;
  s->g.Init();
  s->g.T = keepT;
  s->pd.Init(s->range);
  s->smear.Init();
  s->det.Init();
  s->wid.Init();
  s->fs.Init();
  s->lim.Init();
  memset(s->dryl, 0, sizeof s->dryl);
  memset(s->dryr, 0, sizeof s->dryr);
  memset(s->vf, 0, sizeof s->vf);
  s->frozen = false;
  s->voiced = false;
  s->vmix = 0;
  s->jumps = 0;
  for (int i = 0; i < 4; ++i) {
    s->seq_phase[i] = 0; s->adsr_v[i] = 0; s->adsr_stage[i] = 0; s->src_val[i] = 0;
  }
  s->fol_env = 0;
  ResetVoices(s);
}

void* Create(const char* data_dir) {
  Instance* s = new (std::nothrow) Instance();
  if (!s) return NULL;
  memset(s, 0, sizeof *s);
  s->rng.Seed(0xA17Fu ^ (uint32_t)time(NULL));
  ResetParams(s, false);
  s->range = Opt(s, P_DETECT);
  EngineRange(s->range, &s->E);
  s->align = s->E + 300;
  s->g.Init();
  ResetDsp(s);
  s->bpm = 120.0f;
  s->view = 0;
  snprintf(s->preset_dir, sizeof s->preset_dir, "%s", data_dir ? data_dir : "presets");
  pthread_mutex_init(&s->mu, NULL);
  pthread_mutex_init(&s->wmu, NULL);
  pthread_cond_init(&s->wcv, NULL);
  s->load_slot = -1;
  s->status_slot = -1;
  s->worker_running = pthread_create(&s->worker, NULL, WorkerLoop, s) == 0;
  return s;
}

void Destroy(void* inst) {
  Instance* s = static_cast<Instance*>(inst);
  if (!s) return;
  if (s->worker_running) {
    pthread_mutex_lock(&s->wmu);
    s->worker_quit = true;
    pthread_cond_signal(&s->wcv);
    pthread_mutex_unlock(&s->wmu);
    pthread_join(s->worker, NULL);
  }
  pthread_cond_destroy(&s->wcv);
  pthread_mutex_destroy(&s->wmu);
  pthread_mutex_destroy(&s->mu);
  free(s->save_text);
  free(s->loaded_text);
  free(s->retired_text);
  delete s;
}

// ------------------------------------------------------------------------------------------------ MIDI
inline float NoteHz(int n) { return 440.0f * exp2f((n - 69) / 12.0f); }

void NoteOn(Instance* s, int note, int vel) {
  int mode = Opt(s, P_MIDI_MODE);
  s->mod_note = (note - 60) / 12.0f;
  s->mod_vel = vel / 127.0f;
  s->retrig_pending = true;
  for (int i = 0; i < 4; ++i) s->adsr_stage[i] = 1;
  if (mode == 0) return;
  if (s->nheld < 16) s->held[s->nheld++] = note;
  bool poly = mode == 2 || mode == 4;
  int vi = 0;
  if (poly) {
    vi = -1;
    for (int i = 0; i < 4; ++i) if (!s->v[i].gate) { vi = i; break; }
    if (vi < 0) { long o = s->v[0].order; vi = 0; for (int i = 1; i < 4; ++i) if (s->v[i].order < o) { o = s->v[i].order; vi = i; } }
  }
  Voice& v = s->v[vi];
  v.note = note; v.gate = true; v.vel = vel / 127.0f; v.order = ++s->note_order;
}

void NoteOff(Instance* s, int note) {
  int mode = Opt(s, P_MIDI_MODE);
  for (int i = 0; i < s->nheld; ++i) if (s->held[i] == note) { memmove(s->held + i, s->held + i + 1, (s->nheld - i - 1) * sizeof(int)); --s->nheld; break; }
  if (mode == 0) return;
  bool poly = mode == 2 || mode == 4;
  if (poly) {
    for (int i = 0; i < 4; ++i) if (s->v[i].gate && s->v[i].note == note) { s->v[i].gate = false; }
  } else {
    Voice& v = s->v[0];
    if (v.gate && v.note == note) {
      if (s->nheld > 0) { v.note = s->held[s->nheld - 1]; }       // fall back to the previous held note
      else v.gate = false;
    }
  }
}

void Midi(void* inst, const uint8_t* m, int len) {
  Instance* s = static_cast<Instance*>(inst);
  if (len < 2) return;
  int st = m[0] & 0xF0, a = m[1] & 0x7F, b = len > 2 ? (m[2] & 0x7F) : 0;
  if (st == 0x90 && b > 0) NoteOn(s, a, b);
  else if (st == 0x80 || st == 0x90) NoteOff(s, a);
  else if (st == 0xE0) s->mod_pw = ((b << 7 | a) - 8192) / 8192.0f;
  else if (st == 0xD0) s->mod_at = a / 127.0f;
  else if (st == 0xA0) s->mod_at = b / 127.0f;
  else if (st == 0xB0) {
    if (a == 1) s->mod_mw = b / 127.0f;
    else if (a == 123) { s->nheld = 0; for (int i = 0; i < 4; ++i) s->v[i].gate = false; }
    else if (a == 120) s->panic_pending = 1;
  }
}

// ------------------------------------------------------------------------------------------------ UI / views
int NextView(int v, int dir) {
  int n = v <= 0 ? 1 : v + dir;
  if (n < 1) n = N_VIEWS - 1;
  if (n >= N_VIEWS) n = 1;
  return n;
}
void SetView(Instance* s, int v) {
  v = Clampi(v, 0, N_VIEWS - 1);
  s->view = v;
  s->param[P_VIEW] = (float)v;
  s->param[P_IN_SEC] = v > 0 ? 1.0f : 0.0f;
  Bump(s);
}
int QlTarget(const Instance* s, int i) {
  if (s->view == V_SRC) return kSrcQl[Clampi(Opt(s, P_SLOT0 + Slot(s) * N_FIELDS + F_SRC), 0, 5)][i];
  return kViewQl[s->view][i];
}

// A parameter set from a touch, a Q-Link, the data wheel or the host: the gesture rules, then the value.
void SetTarget(Instance* s, int p, float v, bool qlink) {
  int pr = Redirect(s, p);
  int si = StepIndex(p);
  uint64_t now = s->frames ? s->frames : 1;
  if (si >= 0) { s->seqv[Slot(s)][si] = Clamp(v, -1.0f, 1.0f); Bump(s); return; }
  if (PARAMS[pr].nopts > 0) {
    int n = PARAMS[pr].nopts;
    int want = Clampi(static_cast<int>(v + 0.5f), 0, n - 1);
    if (n == 2) {
      s->param[pr] = (float)want;               // a switch is set to the value it is sent (presets and automation set it too)
    } else {
      if (qlink && !s->loading && n > 2) {          // a Q-Link turn: one step per 0.2 s
        int from = Opt(s, pr);
        if (want - from == 1 || from - want == 1) {
          if (s->last_step[pr] != 0 && s->frames - s->last_step[pr] < kStepLockFrames) { Bump(s); return; }
          s->last_step[pr] = now;
        }
      }
      s->param[pr] = (float)want;
    }
  } else {
    s->param[pr] = Clamp(v, MinOf(pr), MaxOf(pr));
  }
  if (pr >= P_SLOT0 && pr < P_SLOT0 + 4 * N_FIELDS && (pr - P_SLOT0) % N_FIELDS == F_PATT && !s->loading)
    LoadPattern(s, (pr - P_SLOT0) / N_FIELDS);
  if (pr == P_FREEZE) {
    bool on = Opt(s, pr) != 0;
    if (on && !s->frozen) {
      long c = s->g.FindMark(s->g.Cur() - (long)(s->g.T * 1.6f) - 4, 0);
      if (c >= 0) {
        for (int j = 0; j < kFreezeN; ++j) s->fz[j] = s->g.in[(c - kFreezeN / 2 + j) & kInMask];
        s->frozen_T = s->g.T;
        s->frozen = true;
      } else s->param[pr] = 0;
    } else if (!on) s->frozen = false;
  }
  if (pr == P_MIDI_MODE) { for (int i = 0; i < 4; ++i) { s->v[i].gate = false; } s->nheld = 0; }
  Bump(s);
}

void CycleParam(Instance* s, int p, int n, int skip_self_slot) {
  int cur = Opt(s, p), nx = (cur + 1) % n;
  if (skip_self_slot >= 0 && nx == skip_self_slot + 1) nx = (nx + 1) % n;
  s->param[p] = (float)nx;
  Bump(s);
}

void Panic(Instance* s) {
  s->param[P_FREEZE] = 0;
  ResetDsp(s);
  s->range = Opt(s, P_DETECT);
  s->pd.Configure(s->range);
  EngineRange(s->range, &s->E);
  s->align = s->E + 300;
  Bump(s);
}

void SetParam(void* inst, const char* key, const char* val) {
  Instance* s = static_cast<Instance*>(inst);
  float v = static_cast<float>(atof(val));
  if (!strcmp(key, "state")) { LoadState(s, val, false); return; }
  if (!strcmp(key, "lfo_bpm")) { if (v >= 20.0f && v <= 400.0f) s->bpm = v; return; }
  if (!strcmp(key, "transport")) { if (atoi(val) == 1) s->restart_pending = true; return; }
  int p = FindParam(key, strlen(key));
  if (p < 0) return;
  uint64_t now = s->frames ? s->frames : 1;
  if (p >= P_QL1 && p <= P_QL16) {
    int t = QlTarget(s, p - P_QL1);
    if (t < 0) return;
    int tr = Redirect(s, t);
    float x = Clamp(v, 0.0f, 1.0f);
    if (StepIndex(t) >= 0) { SetTarget(s, t, -1.0f + 2.0f * x, true); return; }
    float lo = MinOf(tr), hi = MaxOf(tr);
    SetTarget(s, t, lo + x * (hi - lo), true);
    return;
  }
  if (p == P_VIEW) { SetView(s, static_cast<int>(v + 0.5f)); return; }
  if (p == P_IN_SEC || p == P_MVS) return;                      // follow the view
  if (PARAMS[p].momentary) {
    if (s->loading || v <= 0.0f) return;
    bool tap = v > 0.5f;
    bool in_gesture = s->last_touch[p] != 0 && now - s->last_touch[p] < kGestureFrames;
    s->last_touch[p] = now;
    if (!tap && in_gesture) return;
    if (p == P_PANIC || p == P_PNC_1) { s->panic_pending = 1; return; }
    if (p >= P_NV_1 && p <= P_NV_3) {
      if (p == P_NV_1) SetView(s, 0);
      else SetView(s, NextView(s->view, p == P_NV_3 ? 1 : -1));
      return;
    }
    if (p >= P_GO_1 && p < P_GO_1 + N_TILES) {
      if (s->view == 0 && kTileView[p - P_GO_1] >= 0) SetView(s, kTileView[p - P_GO_1]);
      return;
    }
    if (p >= P_LSLOT_1 && p <= P_LSLOT_4) { s->param[P_MSLOT] = (float)(p - P_LSLOT_1); Bump(s); return; }
    if (LsrcIdx(p) >= 0) { s->param[P_SLOT0 + Slot(s) * N_FIELDS + F_SRC] = (float)LsrcIdx(p); Bump(s); return; }
    if (p == P_NV_4) { s->panic_pending = 1; return; }
    if (p >= P_LMODE_1 && p <= P_LMODE_5) { SetTarget(s, P_MIDI_MODE, (float)(p - P_LMODE_1), false); return; }
    if (p == P_CPOL_1) { CycleParam(s, P_SLOT0 + Slot(s) * N_FIELDS + F_POL, 3, -1); return; }
    if (p == P_CMT_1) { CycleParam(s, P_SLOT0 + Slot(s) * N_FIELDS + F_MTYPE, 5, -1); return; }
    if (p == P_CXS_1) { CycleParam(s, P_SLOT0 + Slot(s) * N_FIELDS + F_XSRC, 5, Slot(s)); return; }
    if (p == P_CDT_1) { CycleParam(s, P_DETECT, 3, -1); return; }
    if (p == P_UPA_1) { RequestLoad(s); pthread_cond_signal(&s->wcv); return; }
    if (p == P_UPA_2) { RequestSave(s); pthread_cond_signal(&s->wcv); return; }
    if (p == P_UPA_3) {
      for (int i = 0; i < kMaxUser; ++i) if (!Used(s, i)) { s->param[P_UPRE] = (float)(i + 1); break; }
      Bump(s);
      return;
    }
    return;
  }
  SetTarget(s, p, v, false);
}

// ------------------------------------------------------------------------------------------------ text
const char* NoteName(float hz, char* buf) {
  static const char* const nn[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  int n = (int)floorf(69.0f + 12.0f * log2f(hz / 440.0f) + 0.5f);
  if (n < 0) n = 0;
  snprintf(buf, 8, "%s%d", nn[n % 12], n / 12 - 1);
  return buf;
}

void Upper(char* s) { for (; *s; ++s) if (*s >= 'a' && *s <= 'z') *s -= 32; }

int Display(const Instance* s, int p, char* buf, int len) {
  int pr = Redirect(s, p);
  int si = StepIndex(p);
  if (si >= 0) return snprintf(buf, len, "%+.0f %%", s->seqv[Slot(s)][si] * 100.0f);
  float v = s->param[pr];
  switch (pr) {
    case P_PITCH: case P_FORMANT: return snprintf(buf, len, "%+.1f st", v);
    case P_HARM: return snprintf(buf, len, "%+.2f", v);
    case P_RATIO: return snprintf(buf, len, "%.2f", v);
    case P_ALT: return snprintf(buf, len, "%.1f st", v);
    case P_OCT: return snprintf(buf, len, "%+.1f", v);
    case P_DENSITY: case P_GENDER: return snprintf(buf, len, "%+.0f %%", v * 100.0f);
    case P_M_ATT: case P_M_REL: return snprintf(buf, len, "%.0f ms", v);
    case P_UPRE: return snprintf(buf, len, "%d", (int)(v + 0.5f));
    case P_TUNE_SCALE: return snprintf(buf, len, "%s", kScaleNames[Clampi((int)(v + 0.5f), 0, N_SCALES - 1)]);
    case P_TUNE_KEY: return snprintf(buf, len, "%s", kKeyNames[Clampi((int)(v + 0.5f), 0, 11)]);
  }
  if (pr >= P_SLOT0 && pr < P_SLOT0 + 4 * N_FIELDS) {
    int f = (pr - P_SLOT0) % N_FIELDS;
    switch (f) {
      case F_META: case F_SUS: case F_XAMT: return snprintf(buf, len, "%.0f %%", v * 100.0f);
      case F_STEPS: return (int)(v + 0.5f) == 0 ? snprintf(buf, len, "--") : snprintf(buf, len, "%d", (int)(v + 0.5f));
      case F_PATT: return (int)(v + 0.5f) >= 100 ? snprintf(buf, len, "RNDM") : snprintf(buf, len, "%d", (int)(v + 0.5f));
      case F_SYNC: return snprintf(buf, len, "%s", kDivNames[Clampi((int)(v + 0.5f), 0, 16)]);
      case F_RATE: return snprintf(buf, len, "%.2f Hz", v);
      case F_SHAPE: return snprintf(buf, len, "%d", (int)(v + 0.5f));
      case F_FATT: case F_FREL: case F_ATK: case F_DEC: case F_REL: return snprintf(buf, len, "%.0f ms", v);
      case F_FGAIN: return snprintf(buf, len, "%.1f", v);
      default: if (f >= F_AMT0) return snprintf(buf, len, "%+.0f %%", v * 100.0f);
    }
  }
  if (PARAMS[pr].nopts > 0) return snprintf(buf, len, "%s", PARAMS[pr].opts[Clampi((int)(v + 0.5f), 0, PARAMS[pr].nopts - 1)]);
  if (PARAMS[pr].min == 0.0f && PARAMS[pr].max == 1.0f) return snprintf(buf, len, "%.0f %%", v * 100.0f);
  return snprintf(buf, len, "%g", v);
}

int Info(const Instance* s, char* buf, int len) {
  char nn[8];
  if (s->frozen) return snprintf(buf, len, "FROZEN   LATENCY %.0f MS", (s->E + 364) * 1000.0f / kSR);
  if (Opt(s, P_TUNE)) {
    char sc[40];
    snprintf(sc, sizeof sc, "%s %s", kKeyNames[Clampi(Opt(s, P_TUNE_KEY), 0, 11)], kScaleNames[Clampi(Opt(s, P_TUNE_SCALE), 0, N_SCALES - 1)]);
    Upper(sc);
    if (s->have_pitch && s->voiced) return snprintf(buf, len, "TUNE %s   F0 %.0f HZ  %s", sc, kSR / s->g.T, NoteName(kSR / s->g.T, nn));
    return snprintf(buf, len, "TUNE %s   UNVOICED", sc);
  }
  if (s->have_pitch && s->voiced)
    return snprintf(buf, len, "F0 %.0f HZ  %s  VOICED   LAT %.0f MS", kSR / s->g.T, NoteName(kSR / s->g.T, nn),
                    (s->E + 364) * 1000.0f / kSR);
  return snprintf(buf, len, "F0 --  UNVOICED   LAT %.0f MS", (s->E + 364) * 1000.0f / kSR);
}

int Hint(const Instance* s, char* buf, int len) {
  const char* t = " ";
  switch (s->view) {
    case V_PITCH: if (!s->voiced) t = "NO VOICE FOUND: SING OR SPEAK INTO THE INPUT"; break;
    case V_HARM: if (s->param[P_HARM] == 0.0f) t = "RATIO AND FM DO NOTHING UNTIL HARM IS NOT 0"; break;
    case V_ALT: if (s->param[P_ALT] < 0.005f) t = "ALT IS 0: ALTERNATOR AND OCTAVE DO NOTHING"; break;
    case V_FX: t = s->param[P_WET] >= 0.999f ? "FULL WET: ONLY THE EFFECT IS HEARD" : "BELOW 100 % WET THE ORIGINAL VOICE IS MIXED IN"; break;
    case V_MIDI: { int m = Opt(s, P_MIDI_MODE);
      t = m == 0 ? "MIDI OFF: THE PITCH KNOB DECIDES" : (m >= 3 ? "GATED: SOUND ONLY WHILE A NOTE IS HELD" : " "); break; }
    case V_GLOB: t = Opt(s, P_CONS) ? "CONS PROT ON: S, T AND BREATH PASS DRY" : "CONS PROT OFF: FULL WET IS ONLY THE EFFECT"; break;
  }
  return snprintf(buf, len, "%s", t);
}

int Text(const Instance* s, int p, bool on, char* buf, int len) {
  // list tiles: the row's text, or whether it is lit
  if (p == P_NV_4) return on ? snprintf(buf, len, "0") : snprintf(buf, len, "PANIC");
  if (p >= P_NV_1 && p <= P_NV_3) {
    if (on) return snprintf(buf, len, "0");
    static const char* const t[3] = {"OVERVIEW", "<", ">"};
    return snprintf(buf, len, "%s", t[p - P_NV_1]);
  }
  if (p >= P_GO_1 && p < P_GO_1 + N_TILES) {
    if (on) return snprintf(buf, len, "0");
    return snprintf(buf, len, "%s", s->view == 0 ? kTileText[p - P_GO_1] : "");
  }
  if (p >= P_LSLOT_1 && p <= P_LSLOT_4) {
    if (on) return snprintf(buf, len, "%d", Slot(s) == p - P_LSLOT_1 ? 1 : 0);
    return snprintf(buf, len, "SLOT %d", p - P_LSLOT_1 + 1);
  }
  if (LsrcIdx(p) >= 0) {
    int i = LsrcIdx(p);
    if (on) return snprintf(buf, len, "%d", Opt(s, P_SLOT0 + Slot(s) * N_FIELDS + F_SRC) == i ? 1 : 0);
    snprintf(buf, len, "%s", kSrcNames[i]); Upper(buf); return (int)strlen(buf);
  }
  if (p >= P_LMODE_1 && p <= P_LMODE_5) {
    int i = p - P_LMODE_1;
    if (on) return snprintf(buf, len, "%d", Opt(s, P_MIDI_MODE) == i ? 1 : 0);
    snprintf(buf, len, "%s", kModeNames[i]); Upper(buf); return (int)strlen(buf);
  }
  if (p == P_CPOL_1) {
    if (on) return snprintf(buf, len, "0");
    static const char* const pn[3] = {"+  0 TO 100", "-  0 TO -100", "B  -100 TO 100"};
    return snprintf(buf, len, "POLARITY %s", pn[Clampi(Opt(s, P_SLOT0 + Slot(s) * N_FIELDS + F_POL), 0, 2)]);
  }
  if (p == P_CMT_1) {
    if (on) return snprintf(buf, len, "0");
    snprintf(buf, len, "TYPE %s", kMtypeNames[Clampi(Opt(s, P_SLOT0 + Slot(s) * N_FIELDS + F_MTYPE), 0, 4)]);
    Upper(buf); return (int)strlen(buf);
  }
  if (p == P_CXS_1) {
    if (on) return snprintf(buf, len, "0");
    int x = Opt(s, P_SLOT0 + Slot(s) * N_FIELDS + F_XSRC);
    return x == 0 ? snprintf(buf, len, "X-MOD  OFF") : snprintf(buf, len, "X-MOD  SLOT %d", x);
  }
  if (p == P_CDT_1) {
    if (on) return snprintf(buf, len, "0");
    snprintf(buf, len, "DETECT %s", kDetectNames[Clampi(Opt(s, P_DETECT), 0, 2)]); Upper(buf); return (int)strlen(buf);
  }
  if (p == P_PNC_1) return on ? snprintf(buf, len, "0") : snprintf(buf, len, "PANIC");
  if (p >= P_UPA_1 && p <= P_UPA_3) {
    if (on) return snprintf(buf, len, "0");
    static const char* const t[3] = {"LOAD", "SAVE", "NEXT FREE"};
    return snprintf(buf, len, "%s", t[p - P_UPA_1]);
  }
  return 0;
}

int GetParam(void* inst, const char* key, char* buf, int buf_len) {
  Instance* s = static_cast<Instance*>(inst);
  if (!strcmp(key, "state")) return SaveState(s, buf, buf_len);
  if (!strcmp(key, "display_rev")) return snprintf(buf, buf_len, "%d", s->display_rev);
  size_t kl = strlen(key);
  int suffix = 0;       // 1 = _display, 2 = _name, 3 = _on
  if (kl > 8 && !strcmp(key + kl - 8, "_display")) { suffix = 1; kl -= 8; }
  else if (kl > 5 && !strcmp(key + kl - 5, "_name")) { suffix = 2; kl -= 5; }
  else if (kl > 3 && !strcmp(key + kl - 3, "_on")) { suffix = 3; kl -= 3; }
  int p = FindParam(key, kl);
  if (p < 0 && suffix == 3) { p = FindParam(key, strlen(key)); suffix = 0; }     // a real key that ends in _on (m1_on ...)
  if (p < 0) return 0;
  if (p >= P_QL1 && p <= P_QL16) {
    int t = QlTarget(s, p - P_QL1);
    if (suffix == 2) {
      if (t < 0) return snprintf(buf, buf_len, "-");
      int tt = Redirect(s, t);
      if (t >= P_PS0 && t < P_PS0 + N_FIELDS && (t - P_PS0) >= F_AMT0) return snprintf(buf, buf_len, "%s", kTargetNames[(t - P_PS0) - F_AMT0]);
      if (StepIndex(t) >= 0) return snprintf(buf, buf_len, "STEP %d", StepIndex(t) + 1);
      return snprintf(buf, buf_len, "%s", PARAMS[t >= P_PS0 && t < P_PS0 + N_FIELDS ? t : tt].name);
    }
    if (suffix == 1) { if (t < 0) return snprintf(buf, buf_len, " "); return Display(s, t, buf, buf_len); }
    if (t < 0) return snprintf(buf, buf_len, "0");
    if (StepIndex(t) >= 0) return snprintf(buf, buf_len, "%g", (s->seqv[Slot(s)][StepIndex(t)] + 1.0f) * 0.5f);
    int tr = Redirect(s, t);
    float lo = MinOf(tr), hi = MaxOf(tr);
    return snprintf(buf, buf_len, "%g", hi > lo ? (s->param[tr] - lo) / (hi - lo) : 0.0f);
  }
  if (suffix == 3) { if (PARAMS[p].string_display) return Text(s, p, true, buf, buf_len); return 0; }
  if (suffix == 2) return 0;
  if (suffix == 1) return Display(s, p, buf, buf_len);
  if (p == P_INFO) return Info(s, buf, buf_len);
  if (p == P_HINT) return Hint(s, buf, buf_len);
  if (p == P_UPRE_INFO) {
    int slot = Clampi(Opt(s, P_UPRE) - 1, 0, kMaxUser - 1);
    int st = s->status_slot == slot ? s->status : 0;
    static const char* const k[6] = {NULL, "SAVED", "LOADED", "SAVE FAILED", "EMPTY", "LOAD FAILED"};
    if (st > 0 && st < 6) return snprintf(buf, buf_len, "USER %d: %s", slot + 1, k[st]);
    return snprintf(buf, buf_len, "USER %d: %s", slot + 1, Used(s, slot) ? "STORED" : "EMPTY");
  }
  if (p == P_VIEW) return snprintf(buf, buf_len, "%d", s->view);
  if (p == P_IN_SEC) return snprintf(buf, buf_len, "%d", s->view > 0 ? 1 : 0);
  if (p == P_MVS) return snprintf(buf, buf_len, "%d", s->view == V_SRC ? 1 + Clampi(Opt(s, P_SLOT0 + Slot(s) * N_FIELDS + F_SRC), 0, 5) : 0);
  if (PARAMS[p].momentary && PARAMS[p].string_display) return Text(s, p, false, buf, buf_len);
  if (PARAMS[p].momentary) return snprintf(buf, buf_len, "0");
  int si = StepIndex(p);
  if (si >= 0) return snprintf(buf, buf_len, "%g", s->seqv[Slot(s)][si]);
  int pr = Redirect(s, p);
  if (PARAMS[pr].nopts > 0) return snprintf(buf, buf_len, "%d", Opt(s, pr));
  return snprintf(buf, buf_len, "%g", s->param[pr]);
}

void Render(void*, int16_t* out_lr, int frames) { memset(out_lr, 0, sizeof(int16_t) * 2 * frames); }

// ------------------------------------------------------------------------------------------------ modulation
inline float Snap(float v, bool on, float step) { return on ? roundf(v / step) * step : v; }

void ComputeBlock(Instance* s, int frames) {
  const float dt = frames / (float)kSR;
  const bool snap = Opt(s, P_SNAP) != 0;
  // ---- sources
  float raw[4];
  for (int i = 0; i < 4; ++i) {
    float val = 0;
    int src = Opt(s, P_SLOT0 + i * N_FIELDS + F_SRC);
    switch (src) {
      case 0: {
        float m = Field(s, i, F_META);
        int st = (int)(Field(s, i, F_STEPS) + 0.5f);
        if (st > 1) m = roundf(m * (st - 1)) / (st - 1);
        int pol = Opt(s, P_SLOT0 + i * N_FIELDS + F_POL);
        val = pol == 0 ? m : (pol == 1 ? -m : 2.0f * m - 1.0f);
        break;
      }
      case 1:
        switch (Opt(s, P_SLOT0 + i * N_FIELDS + F_MTYPE)) {
          case 0: val = s->mod_note; break;
          case 1: val = s->mod_vel; break;
          case 2: val = s->mod_pw; break;
          case 3: val = s->mod_mw; break;
          default: val = s->mod_at; break;
        }
        break;
      case 2: {
        int sync = (int)(Field(s, i, F_SYNC) + 0.5f);
        bool retrig = Opt(s, P_SLOT0 + i * N_FIELDS + F_RETRIG) != 0;
        float cyc_per_sample;
        if (sync > 0) {
          float beats = kDivBeats[Clampi(sync - 1, 0, 15)];
          cyc_per_sample = s->bpm / 60.0f / kSR / beats;
          if (!retrig) s->seq_phase[i] = fmodf(s->beat / beats, 1.0f);
          else s->seq_phase[i] += cyc_per_sample * frames;
        } else {
          cyc_per_sample = Field(s, i, F_RATE) / kSR;
          s->seq_phase[i] += cyc_per_sample * frames;
        }
        if (retrig && s->retrig_pending) s->seq_phase[i] = 0;
        s->seq_phase[i] -= floorf(s->seq_phase[i]);
        val = PatternValue(s->seqv[i], s->seq_phase[i], Clampi((int)(Field(s, i, F_SHAPE) + 0.5f), 0, 4));
        break;
      }
      case 3: {
        float db = 20.0f * log10f(s->fol_env + 1e-6f);
        val = Clamp((db + 48.0f) / 48.0f * (Field(s, i, F_FGAIN) * 0.5f), 0.0f, 1.0f);
        break;
      }
      case 5: {                                              // LFO: free (Hz) or tempo-synced, 7 waves, phase offset, retrigger on a note
        int sync = (int)(Field(s, i, F_SYNC) + 0.5f);
        bool retrig = Opt(s, P_SLOT0 + i * N_FIELDS + F_RETRIG) != 0;
        float before = s->seq_phase[i];
        if (sync > 0) {
          float beats = kDivBeats[Clampi(sync - 1, 0, 15)];
          if (!retrig) s->seq_phase[i] = fmodf(s->beat / beats, 1.0f);
          else s->seq_phase[i] += s->bpm / 60.0f / kSR / beats * frames;
        } else {
          s->seq_phase[i] += Field(s, i, F_RATE) / kSR * frames;
        }
        if (retrig && s->retrig_pending) { s->seq_phase[i] = 0; s->lfo_a[i] = 0; s->lfo_b[i] = s->rng.Bi(); before = 0; }
        s->seq_phase[i] -= floorf(s->seq_phase[i]);
        if (s->seq_phase[i] < before) { s->lfo_a[i] = s->lfo_b[i]; s->lfo_b[i] = s->rng.Bi(); }
        float ph = s->seq_phase[i] + s->param[P_PHASE0 + i];
        ph -= floorf(ph);
        switch (Clampi((int)(s->param[P_WAVE0 + i] + 0.5f), 0, 6)) {
          case 0: val = sinf(2.0f * kPi * ph); break;
          case 1: val = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;
          case 2: val = 2.0f * ph - 1.0f; break;
          case 3: val = 1.0f - 2.0f * ph; break;
          case 4: val = ph < 0.5f ? 1.0f : -1.0f; break;
          case 5: { float f = s->seq_phase[i]; f = f * f * (3.0f - 2.0f * f); val = s->lfo_a[i] + (s->lfo_b[i] - s->lfo_a[i]) * f; break; }
          default: val = s->lfo_b[i]; break;
        }
        break;
      }
      default: {
        float a = Field(s, i, F_ATK) * 0.001f, d = Field(s, i, F_DEC) * 0.001f, su = Field(s, i, F_SUS),
              r = Field(s, i, F_REL) * 0.001f;
        float& e = s->adsr_v[i];
        int& st = s->adsr_stage[i];
        if (!s->any_gate && st > 0 && st < 4) st = 4;
        if (st == 1) { e += dt / (a > 0.0005f ? a : 0.0005f); if (e >= 1.0f) { e = 1.0f; st = 2; } }
        else if (st == 2) { e -= dt / (d > 0.0005f ? d : 0.0005f) * (1.0f - su); if (e <= su) { e = su; st = 3; } }
        else if (st == 3) e = su;
        else if (st == 4) { e -= dt / (r > 0.0005f ? r : 0.0005f); if (e <= 0.0f) { e = 0.0f; st = 0; } }
        val = e;
      }
    }
    raw[i] = val;
  }
  s->retrig_pending = false;
  for (int t = 0; t < N_TARGETS; ++t) s->mod[t] = 0;
  for (int i = 0; i < 4; ++i) {
    if (!Opt(s, P_SLOT0 + i * N_FIELDS + F_ON)) { s->src_val[i] = 0; continue; }
    float v = raw[i];
    int x = Opt(s, P_SLOT0 + i * N_FIELDS + F_XSRC);
    float xa = Field(s, i, F_XAMT);
    if (x > 0 && x - 1 != i && xa > 0.0f) v *= (1.0f - xa) + xa * Clamp(fabsf(raw[x - 1]), 0.0f, 1.0f);
    s->src_val[i] = v;
    for (int t = 0; t < N_TARGETS; ++t) s->mod[t] += Field(s, i, F_AMT0 + t) * v;
  }
  // ---- effective values
  const float* p = s->param;
  float gender = Clamp(p[P_GENDER] + s->mod[15], -1.0f, 1.0f);
  s->e_gender = gender;
  s->e_pitch = Snap(p[P_PITCH], snap, 1.0f) + s->mod[0] * 24.0f + gender * 3.0f;
  s->e_smooth = Clamp(p[P_SMOOTH] + s->mod[1], 0.0f, 1.0f);
  s->e_formant = Snap(p[P_FORMANT], snap, 1.0f) + s->mod[2] * 12.0f + gender * 6.0f;
  s->e_harm = Clamp(Snap(p[P_HARM], snap, 1.0f) + s->mod[3] * 8.0f, -8.0f, 8.0f);
  s->e_ratio = Clamp(p[P_RATIO] + s->mod[4], 0.0f, 1.0f);
  s->e_fm = Clamp(p[P_FM] + s->mod[5], 0.0f, 1.0f);
  s->e_alt = Clamp(p[P_ALT] + s->mod[6] * 12.0f, 0.0f, 12.0f);
  s->e_oct = Clamp(p[P_OCT] + s->mod[7] * 8.0f, -8.0f, 8.0f);
  s->e_smear = Clamp(p[P_SMEAR] + s->mod[8], 0.0f, 1.0f);
  s->e_stereo = Clamp(p[P_STEREO] + s->mod[9], 0.0f, 1.0f);
  s->e_detune = Clamp(p[P_DETUNE] + s->mod[10], 0.0f, 1.0f);
  s->e_wet = Clamp(p[P_WET] + s->mod[11], 0.0f, 1.0f);
  s->e_jitter = Clamp(p[P_JITTER] + s->mod[12], 0.0f, 1.0f);
  s->e_window = Clamp(p[P_WINDOW] + s->mod[13], 0.0f, 1.0f);
  s->e_density = Clamp(p[P_DENSITY] + s->mod[14], -1.0f, 1.0f);
  s->e_oct = s->e_oct;
  // ---- TUNE: move the voice to the nearest note of the scale (the correction is added to the pitch)
  if (Opt(s, P_TUNE)) {
    if (s->have_pitch && s->voiced) {
      float midi = 69.0f + 12.0f * log2f((kSR / s->g.T) / 440.0f);
      int key = Clampi(Opt(s, P_TUNE_KEY), 0, 11), sc = Clampi(Opt(s, P_TUNE_SCALE), 0, N_SCALES - 1);
      unsigned mask = kScaleMask[sc];
      float best = midi, bd = 1e9f;
      int base = (int)floorf(midi) - 7;
      for (int n = base; n <= base + 14; ++n) {
        int pc = ((n - key) % 12 + 12) % 12;
        if (!((mask >> pc) & 1u)) continue;
        float d = fabsf((float)n - midi);
        if (d < bd) { bd = d; best = (float)n; }
      }
      s->tune_tgt = (best - midi) * Clamp(p[P_TUNE_AMOUNT], 0.0f, 1.0f);
    }
    float sp = Clamp(p[P_TUNE_SPEED], 0.0f, 1.0f);
    float tau = 0.001f + 0.4f * (1.0f - sp) * (1.0f - sp);
    s->tune_corr += (s->tune_tgt - s->tune_corr) * (1.0f - expf(-dt / tau));
  } else { s->tune_tgt = 0.0f; s->tune_corr = 0.0f; }
  s->e_pitch += s->tune_corr;
}

void UpdateVoices(Instance* s, int frames) {
  const float dt = frames / (float)kSR;
  int mode = Opt(s, P_MIDI_MODE);
  bool gated = mode == 3 || mode == 4, poly = mode == 2 || mode == 4;
  float glide = s->param[P_GLIDE];
  float tau = glide * glide * 1.5f;
  float f0 = kSR / (s->frozen ? s->frozen_T : s->g.T);
  s->any_gate = false;
  float att = gated ? s->param[P_M_ATT] * 0.001f : 0.003f, rel = gated ? s->param[P_M_REL] * 0.001f : 0.003f;
  int ngate = 0;
  for (int i = 0; i < 4; ++i) if (s->v[i].gate) { ++ngate; s->any_gate = true; }
  for (int i = 0; i < 4; ++i) {
    Voice& v = s->v[i];
    bool want = false, use_abs = false;
    float target = 0;
    if (mode == 0) { want = (i == 0); }
    else if (!poly) {
      if (i == 0) { if (v.gate) { want = true; use_abs = true; } else want = !gated; }
    } else {
      if (v.gate) { want = true; use_abs = true; }
      else if (i == 0 && !gated && ngate == 0) want = true;
    }
    float envT = want ? 1.0f : 0.0f;
    if (gated && !v.gate && use_abs == false) envT = 0.0f;
    float r = envT > v.env ? att : rel;
    v.env += (envT - v.env) * (1.0f - expf(-dt / (r > 0.0005f ? r : 0.0005f)));
    if (fabsf(v.env - envT) < 0.002f) v.env = envT;
    bool was_active = v.active;
    v.active = v.env > 0.001f || want;
    if (use_abs) target = log2f(NoteHz(v.note)) + s->e_pitch / 12.0f;
    else target = s->e_pitch / 12.0f;
    if (v.abs != use_abs || !was_active) { v.lv = target; v.abs = use_abs; }
    bool snapg = tau < 0.002f || (s->param[P_GLIDE_M] && !use_abs);
    if (snapg) v.lv = target;
    else v.lv += (target - v.lv) * (1.0f - expf(-dt / tau));
    float T = s->frozen ? s->frozen_T : s->g.T;
    v.tout = v.abs ? kSR / exp2f(v.lv) : T / exp2f(v.lv);
    v.tout = Clamp(v.tout, 20.0f, 6000.0f);
    if (!was_active && v.active) { v.next[0] = v.next[1] = (double)s->g.Cur(); v.k[0] = v.k[1] = 0; }
  }
  (void)f0;
  Voice& v0 = s->v[0];
  Voice* lead = &v0;
  for (int i = 0; i < 4; ++i) if (s->v[i].active && s->v[i].env > 0.01f) { lead = &s->v[i]; break; }
  s->f0_out = kSR / lead->tout;
}

// ------------------------------------------------------------------------------------------------ process
inline int16_t ToShort(float x) {
  x *= 32768.0f;
  return static_cast<int16_t>(x > 32767.0f ? 32767.0f : (x < -32768.0f ? -32768.0f : x));
}

void SpawnGrains(Instance* s, Voice& v, int stream, int nv_gain) {
  Grains& g = s->g;
  const long cur = g.Cur();
  float T = s->frozen ? s->frozen_T : g.T;
  float fr = exp2f(Clamp(s->e_formant, -12.0f, 12.0f) / 12.0f);
  float ws = 0.5f + s->e_window;
  while ((double)cur >= v.next[stream]) {
    float rm = 1.0f;
    if (s->e_alt > 0.005f) {
      int hold = 1 << Clampi((int)floorf(s->e_oct + 0.5f), 0, 8);
      int sign = ((v.k[stream] / (unsigned)hold) & 1u) ? 1 : -1;
      rm = exp2f(sign * s->e_alt / 12.0f);
    }
    if (stream == 1) rm *= exp2f(s->e_oct);
    float to = v.tout / rm;
    if (s->e_jitter > 0.0f) to *= 1.0f + 0.25f * s->e_jitter * s->rng.Bi();
    to = Clamp(to, 24.0f, 8000.0f);
    float H0 = T * ws;
    float Ht = H0 > to * ws ? H0 : to * ws;
    float H = H0 + 0.5f * s->e_smooth * (Ht - H0);       // Smooth widens the grain towards the spacing: less gap, less exact pitch
    float hl = H / fr;
    float cap = (float)(s->E - 3);
    if (hl > cap) hl = cap;
    if (hl < 8.0f) hl = 8.0f;
    float gain = Clamp(to / hl, 0.15f, 2.5f) * v.env * (nv_gain > 1 ? 1.0f / sqrtf((float)nv_gain) : 1.0f);
    float d = s->e_density;
    bool skip = false;
    if (d < 0.0f) {
      float pdrop = -d * 0.75f;
      if (s->rng.Uni() < pdrop) skip = true;
      gain *= 1.0f / (1.0f - 0.5f * pdrop);
    }
    bool reverse = Opt(s, P_REVERSE) != 0;
    if (!skip) {
      if (s->frozen) {
        // the frozen grain: a copy of the input around one mark, read with the same grain code
        long centre = cur;
        float inv = kPi / hl;
        float dir = reverse ? -fr : fr;
        float pos = (float)(kFreezeN / 2) - dir * hl;
        const float cd = cosf(inv), sd = sinf(inv);
        float wc = -1.0f, ws2 = 0.0f;
        for (int j = -(int)hl; j <= (int)hl; ++j, pos += dir) {
          float w = 0.5f + 0.5f * wc;
          { float nc = wc * cd - ws2 * sd; ws2 = ws2 * cd + wc * sd; wc = nc; }
          long pi = (long)floorf(pos);
          float f = pos - pi;
          float xm1 = s->fz[(pi - 1) & (kFreezeN - 1)], x0 = s->fz[pi & (kFreezeN - 1)], x1 = s->fz[(pi + 1) & (kFreezeN - 1)],
                x2 = s->fz[(pi + 2) & (kFreezeN - 1)];
          float c = 0.5f * (x1 - xm1), vv = x0 - x1, ww = c + vv, a = ww + vv + 0.5f * (x2 - x0), bn = ww + a;
          g.out[(centre + j) & kOutMask] += gain * w * (((a * f - bn) * f + c) * f + x0);
        }
      } else {
        long c = g.FindMark(cur - (long)(hl * fr) - 3, 0);
        if (c >= 0) {
          g.AddGrain(cur, c, fr, (int)hl, gain, reverse);
          if (d > 0.0f) {                                       // thicker: a second grain, half a spacing later, from the mark before
            long c2 = g.FindMark(cur - (long)(hl * fr) - 3, 1);
            if (c2 >= 0) g.AddGrain(cur + (long)(to * 0.5f), c2, fr, (int)hl, gain * d * 0.8f, reverse);
          }
        }
      }
    }
    v.next[stream] += to;
    ++v.k[stream];
  }
}

static inline float g_T_safe(const Instance* s) { return Clamp(s->g.T, 24.0f, 1800.0f); }

void ProcessBlock(Instance* s, const int16_t* in_lr, int16_t* out_lr, int frames) {
  if (s->panic_pending) { s->panic_pending = 0; Panic(s); }
  char* preset = __atomic_exchange_n(&s->loaded_text, (char*)NULL, __ATOMIC_ACQ_REL);
  if (preset) {
    LoadState(s, preset, true);
    SetStatus(s, s->status_slot, 2);
    free(__atomic_exchange_n(&s->retired_text, preset, __ATOMIC_ACQ_REL));
  }
  // detect range
  int range = Clampi(Opt(s, P_DETECT), 0, 2);
  if (range != s->range) {
    s->range = range;
    EngineRange(range, &s->E);
    s->align = s->E + 300;
    s->pd.Configure(range);
    memset(s->g.out, 0, sizeof s->g.out);
    for (int i = 0; i < 4; ++i) { s->v[i].active = false; }
  }
  if (s->restart_pending) { s->beat = 0; s->restart_pending = false; }
  ComputeBlock(s, frames);
  UpdateVoices(s, frames);

  int nv_gain = 0;
  for (int i = 0; i < 4; ++i) if (s->v[i].active && s->v[i].env > 0.01f) ++nv_gain;
  const bool cons = Opt(s, P_CONS) != 0;
  const float fol_a = 1.0f - expf(-1.0f / (0.004f * kSR));
  float shift_hz = s->e_harm * s->e_ratio * s->f0_out;
  float fm_hz = s->e_harm != 0.0f ? s->e_fm * s->f0_out * 0.5f : 0.0f;
  float fm_rate = s->f0_out * Clamp(s->e_ratio, 0.25f, 1.0f);
  float wet_c = cosf(s->e_wet * 0.5f * kPi), wet_s = sinf(s->e_wet * 0.5f * kPi);
  bool shifting = s->e_harm != 0.0f;
  const bool align_on = Opt(s, P_ALIGN) != 0;
  const float align_tgt = (float)s->E + (s->have_pitch ? g_T_safe(s) : 300.0f);
  float ol[kBlock], orr[kBlock];
  Grains& g = s->g;

  for (int i = 0; i < frames; ++i) {
    float l = in_lr[2 * i] * (1.0f / 32768.0f), r = in_lr[2 * i + 1] * (1.0f / 32768.0f);
    float m = 0.5f * (l + r);
    float a = fabsf(m);
    s->fol_env += (a > s->fol_env ? fol_a : fol_a * 0.02f) * (a - s->fol_env);
    long cur1 = g.n;                                  // index the new sample will have
    s->dryl[cur1 & kInMask] = l;
    s->dryr[cur1 & kInMask] = r;
    s->vf[cur1 & kInMask] = s->voiced ? 1 : 0;
    float sm = s->smear.Process(m, s->e_smear);
    s->pd.Push(sm);
    g.Push(sm);
    g.TrackMarks();
    for (int vi = 0; vi < 4; ++vi) {
      Voice& v = s->v[vi];
      if (!v.active) continue;
      SpawnGrains(s, v, 0, nv_gain);
      if (s->e_alt > 0.005f && s->e_oct < -0.5f) SpawnGrains(s, v, 1, nv_gain);
      else v.next[1] = (double)g.Cur();
    }
    float wetm = g.Emit(s->E);
    // consonant protect: unvoiced sounds go around the grains
    long ai;
    float dl, dr;
    if (align_on) {          // ALIGN: the dry path is delayed by what the wet path really is (hold + one period), followed smoothly
      s->align_f += (align_tgt - s->align_f) * 0.0004f;
      float pos = (float)g.Cur() - s->align_f;
      long i0 = (long)floorf(pos);
      float fr = pos - (float)i0;
      dl = s->dryl[i0 & kInMask] * (1.0f - fr) + s->dryl[(i0 + 1) & kInMask] * fr;
      dr = s->dryr[i0 & kInMask] * (1.0f - fr) + s->dryr[(i0 + 1) & kInMask] * fr;
      ai = (i0 + (fr > 0.5f ? 1 : 0)) & kInMask;
    } else {
      s->align_f = (float)s->align;
      ai = (g.Cur() - s->align) & kInMask;
      dl = s->dryl[ai]; dr = s->dryr[ai];
    }
    if (cons) {
      float target = s->vf[ai] ? 1.0f : 0.0f;
      s->vmix += (target - s->vmix) * 0.01f;
      wetm = wetm * s->vmix + 0.5f * (dl + dr) * (1.0f - s->vmix);
    } else s->vmix = 1.0f;
    float h = wetm;
    if (shifting) h = s->fs.Process(wetm, shift_hz, fm_hz, fm_rate);
    float dl2, dr2;
    s->det.Process(h, s->e_detune, &dl2, &dr2);
    if (s->e_stereo > 0.001f) s->wid.Process(&dl2, &dr2, s->e_stereo);
    ol[i] = dl * wet_c + dl2 * wet_s;
    orr[i] = dr * wet_c + dr2 * wet_s;
  }
  // pitch analysis for the next block
  s->an_tick ^= 1;                                       // pitch analysis every second block (5.8 ms): half the CPU
  if (s->an_tick && s->pd.Analyse()) {
    bool v = s->pd.period > 0.0f && s->pd.clarity >= s->param[P_CONS_THR];
    if (v) {
      float Tm = s->pd.period;
      if (!s->have_pitch) { g.T = Tm; s->have_pitch = true; s->jumps = 0; }
      else if (fabsf(logf(Tm / g.T)) < 0.12f) { g.T += 0.35f * (Tm - g.T); s->jumps = 0; }
      else if (++s->jumps >= 3) { g.T = Tm; s->jumps = 0; }
      g.T = Clamp(g.T, 24.0f, 1800.0f);
    }
    s->voiced = v;
  }
  s->beat += (float)(s->bpm / 60.0 * frames / kSR);
  if (s->beat > 1e6f) s->beat = fmodf(s->beat, 6144.0f);
  s->lim.Process(ol, orr, frames, 0.0f, -0.3f, 80.0f, (float)kSR);
  for (int i = 0; i < frames; ++i) {
    out_lr[2 * i] = ToShort(ol[i]);
    out_lr[2 * i + 1] = ToShort(orr[i]);
  }
}

void Process(void* inst, const int16_t* in_lr, int16_t* out_lr, int frames) {
  Instance* s = static_cast<Instance*>(inst);
  ScopedDenormalDisable no_denormals;
  for (int off = 0; off < frames; off += kBlock) {
    int n = frames - off < kBlock ? frames - off : kBlock;
    ProcessBlock(s, in_lr + 2 * off, out_lr + 2 * off, n);
  }
  s->frames += frames;
}

const mpc_engine_t kEngine = {Create, Destroy, Midi, SetParam, GetParam, Render, Process};

}  // namespace

extern "C" const mpc_engine_t* mpc_engine(void) { return &kEngine; }
