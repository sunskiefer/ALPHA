// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// ALPHA's DSP building blocks (all original code; the public Polyverse Manipulator manual only supplied the control list).
//
//   PitchDetector   normalised square difference function (McLeod's NSDF) on a decimated copy of the input
//   Hilbert         Olli Niemitalo's two-path all-pass pair (the coefficients are published, musicdsp.org), used as a
//                   single-sideband frequency shifter: the harmonic shifter and its FM
//   Smear           loops a short captured segment with cross-fades
//   Detune          four slow delay-line pitch shifters (two cross-faded taps each)
//   Widener         mono-compatible pseudo stereo: a decorrelated copy added to the left and taken from the right
//   Grains          the pitch-synchronous grain engine: analysis marks follow the voice, grains are cut around them,
//                   resampled for the formant, windowed and overlap-added at the new pitch spacing
//   Patterns        the sequencer's 100 patterns, made by code, and its five shapes
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace alpha {

const int kSR = 44100;
const int kInN = 1 << 14, kInMask = kInN - 1;       // input history: 371 ms
const int kOutN = 1 << 13, kOutMask = kOutN - 1;    // grain accumulator
const float kPi = 3.14159265358979f;

inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int Clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Rng {
  uint32_t s;
  void Seed(uint32_t x) { s = x ? x : 0x9e3779b9u; }
  uint32_t Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  float Uni() { return (Next() >> 8) * (1.0f / 16777216.0f); }          // 0..1
  float Bi() { return Uni() * 2.0f - 1.0f; }                            // -1..1
};

// ------------------------------------------------------------------------------------------------ pitch detector
// Detect range (manual): Low 25-700 Hz, Mid 40-1000 Hz (default), High 80-4000 Hz.  The input is decimated by D so the
// correlation stays cheap: Low D=8, Mid D=4, High D=2.  One analysis per call (every 128 input samples).
struct PitchDetector {
  static const int kMaxW = 600;
  static const int kHist = 2048;
  float hist[kHist];
  long hn;                      // decimated samples written
  int D, acc_n;
  float acc;
  int minlag, maxlag, W;
  float x[kMaxW], cs[kMaxW + 1], nsdf[kMaxW];
  float period;                 // last measured period in input samples (0 = none)
  float clarity;                // the NSDF peak, 0..1
  float rms;

  void Init(int range) {
    memset(this, 0, sizeof *this);
    Configure(range);
  }
  void Configure(int range) {
    static const int Ds[3] = {8, 4, 2};
    static const float lo[3] = {25.f, 40.f, 80.f}, hi[3] = {700.f, 1000.f, 4000.f};
    D = Ds[range];
    maxlag = (int)(kSR / lo[range] / D) + 1;
    minlag = (int)(kSR / hi[range] / D);
    if (minlag < 3) minlag = 3;
    W = 2 * maxlag;
    if (W > kMaxW) W = kMaxW;
    acc = 0; acc_n = 0; hn = 0;
    period = 0; clarity = 0;
  }
  void Push(float s) {
    acc += s;
    if (++acc_n == D) {
      hist[hn & (kHist - 1)] = acc / D;
      ++hn;
      acc = 0; acc_n = 0;
    }
  }
  // returns true when period/clarity are fresh
  bool Analyse() {
    if (hn < W + 4) { clarity = 0; return false; }
    float e = 0;
    for (int i = 0; i < W; ++i) {
      x[i] = hist[(hn - W + i) & (kHist - 1)];
      e += x[i] * x[i];
    }
    rms = sqrtf(e / W);
    if (rms < 0.0008f) { clarity = 0; period = 0; return true; }
    cs[0] = 0;
    for (int i = 0; i < W; ++i) cs[i + 1] = cs[i] + x[i] * x[i];
    int top = maxlag < W - 8 ? maxlag : W - 8;
    for (int tau = 0; tau <= top; ++tau) {
      const float* a = x;
      const float* b = x + tau;
      int n = W - tau, j = 0;
      float r0 = 0, r1 = 0, r2 = 0, r3 = 0;                 // four accumulators: faster on ARM and the compiler can use NEON
      for (; j + 4 <= n; j += 4) { r0 += a[j] * b[j]; r1 += a[j + 1] * b[j + 1]; r2 += a[j + 2] * b[j + 2]; r3 += a[j + 3] * b[j + 3]; }
      for (; j < n; ++j) r0 += a[j] * b[j];
      float r = (r0 + r1) + (r2 + r3);
      float m = cs[W - tau] + (cs[W] - cs[tau]);
      nsdf[tau] = m > 1e-12f ? 2.0f * r / m : 0.0f;
    }
    // key maxima: one per positive lobe after the first zero crossing
    int pos = minlag > 1 ? minlag : 1;
    int start0 = 1;
    while (start0 < top && nsdf[start0] > 0) ++start0;       // leave the lobe at lag 0
    if (pos < start0) pos = start0;
    int pk[24]; float pv[24]; int np = 0;
    float best = 0;
    while (pos <= top && np < 24) {
      while (pos <= top && nsdf[pos] <= 0) ++pos;
      if (pos > top) break;
      int bi = pos; float bv = nsdf[pos];
      while (pos <= top && nsdf[pos] > 0) {
        if (nsdf[pos] > bv) { bv = nsdf[pos]; bi = pos; }
        ++pos;
      }
      if (bi > minlag) { pk[np] = bi; pv[np] = bv; ++np; if (bv > best) best = bv; }
    }
    if (np == 0) { clarity = 0; return true; }
    int sel = 0;
    for (int i = 0; i < np; ++i) if (pv[i] >= 0.86f * best) { sel = i; break; }
    int t = pk[sel];
    float lag = (float)t;
    if (t > 1 && t < top) {                                  // parabolic interpolation
      float a = nsdf[t - 1], b = nsdf[t], c = nsdf[t + 1];
      float den = a - 2 * b + c;
      if (fabsf(den) > 1e-9f) lag += Clampf(0.5f * (a - c) / den, -0.5f, 0.5f);
    }
    period = lag * D;
    clarity = pv[sel];
    return true;
  }
};

// ------------------------------------------------------------------------------------------------ Hilbert / shifter
// Two chains of four all-pass sections H(z) = (a - z^-2)/(1 - a z^-2), a = c^2; path 1 is delayed by one sample, so the
// pair is 90 degrees apart (checked 50 Hz to 15 kHz: 89..91 degrees).  b leads a by 90: analytic signal = a - j b.
struct Hilbert {
  float c1[4], c2[4];
  float x1[4], x2[4], y1[4], y2[4];     // path 1 sections: x[n-1], x[n-2], y[n-1], y[n-2]
  float u1[4], u2[4], v1[4], v2[4];     // path 2
  float dly;                            // path 1 input delayed by one sample
  void Init() {
    static const float k1[4] = {0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f};
    static const float k2[4] = {0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f};
    memset(this, 0, sizeof *this);
    for (int i = 0; i < 4; ++i) { c1[i] = k1[i] * k1[i]; c2[i] = k2[i] * k2[i]; }
  }
  void Process(float in, float* a, float* b) {
    float s = dly;
    dly = in;
    for (int i = 0; i < 4; ++i) {
      float y = c1[i] * (s + y2[i]) - x2[i];
      x2[i] = x1[i]; x1[i] = s; y2[i] = y1[i]; y1[i] = y; s = y;
    }
    float t = in;
    for (int i = 0; i < 4; ++i) {
      float y = c2[i] * (t + v2[i]) - u2[i];
      u2[i] = u1[i]; u1[i] = t; v2[i] = v1[i]; v1[i] = y; t = y;
    }
    *a = s; *b = t;
  }
};

struct FreqShifter {
  Hilbert h;
  float ph, phm;         // shift oscillator phase, FM modulator phase (radians)
  void Init() { h.Init(); ph = 0; phm = 0; }
  // shift_hz: + moves up.  fm_hz: peak deviation of the shift, modulated at fm_rate_hz.
  float Process(float in, float shift_hz, float fm_hz, float fm_rate_hz) {
    float a, b;
    h.Process(in, &a, &b);
    float f = shift_hz;
    if (fm_hz > 0.0f) {
      f += fm_hz * sinf(phm);
      phm += 2.0f * kPi * fm_rate_hz / kSR;
      if (phm > 2.0f * kPi) phm -= 2.0f * kPi;
    }
    ph += 2.0f * kPi * f / kSR;
    if (ph > kPi) ph -= 2.0f * kPi; else if (ph < -kPi) ph += 2.0f * kPi;
    return a * cosf(ph) + b * sinf(ph);      // up: Re((a - jb) e^{j ph})
  }
};

// ------------------------------------------------------------------------------------------------ smear
struct Smear {
  static const int kN = 16384, kM = kN - 1;
  static const int kSeg = 2048, kXf = 512;
  float buf[kN];
  long w;
  struct Loop { long cap; int p; bool on; } cur, prev;
  int since;                // output samples since the last capture
  float fade;               // 0..1 from prev to cur
  void Init() { memset(this, 0, sizeof *this); fade = 1.0f; }
  float ReadLoop(Loop& l) {
    if (!l.on) return 0.0f;
    float v = buf[(l.cap + l.p) & kM];
    if (l.p >= kSeg - kXf) {            // cross-fade the end into the start of the segment
      float f = (float)(l.p - (kSeg - kXf)) / kXf;
      v = v * (1.0f - f) + buf[(l.cap + l.p - (kSeg - kXf)) & kM] * f;
    }
    if (++l.p >= kSeg) l.p = kXf;
    return v;
  }
  float Process(float in, float amount) {
    buf[w & kM] = in;
    ++w;
    if (amount <= 0.002f) { cur.on = prev.on = false; since = 0; return in; }
    int hold = (int)(kSeg * (1.0f + 15.0f * amount));
    if (!cur.on || since >= hold) {
      prev = cur;
      cur.cap = w - kSeg; if (cur.cap < 0) cur.cap = 0;
      cur.p = 0; cur.on = true;
      since = 0; fade = prev.on ? 0.0f : 1.0f;
    }
    ++since;
    float a = ReadLoop(prev), b = ReadLoop(cur);
    if (fade < 1.0f) { fade += 1.0f / kXf; if (fade > 1.0f) fade = 1.0f; }
    float wet = a * (1.0f - fade) + b * fade;
    float m = amount * 10.0f; if (m > 1.0f) m = 1.0f;
    return in * (1.0f - m) + wet * m;
  }
};

// ------------------------------------------------------------------------------------------------ detune
struct Detune {
  static const int kN = 8192, kM = kN - 1, kD = 2048;
  float buf[kN];
  long w;
  float ph[4];
  void Init() {
    memset(this, 0, sizeof *this);
    for (int i = 0; i < 4; ++i) ph[i] = 0.25f * i;
  }
  float Tap(float delay) {
    float p = (float)w - delay;
    long i = (long)floorf(p);
    float f = p - i;
    return buf[i & kM] * (1.0f - f) + buf[(i + 1) & kM] * f;
  }
  // in: mono; out: left and right with `amount` of the four shifted voices added
  void Process(float in, float amount, float* l, float* r) {
    buf[w & kM] = in;
    ++w;
    if (amount <= 0.001f) { *l = *r = in; return; }
    static const float cents[4] = {1.0f, -1.0f, 0.5f, -0.5f};
    float v[4];
    for (int i = 0; i < 4; ++i) {
      float ratio = exp2f(cents[i] * amount * 22.0f / 1200.0f);
      ph[i] += (1.0f - ratio) / kD;
      if (ph[i] >= 1.0f) ph[i] -= 1.0f; else if (ph[i] < 0.0f) ph[i] += 1.0f;
      float p2 = ph[i] + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
      float s1 = sinf(kPi * ph[i]), s2 = sinf(kPi * p2);
      v[i] = Tap(2.0f + ph[i] * kD) * s1 * s1 + Tap(2.0f + p2 * kD) * s2 * s2;
    }
    float g = amount * 0.7f, norm = 1.0f / (1.0f + 0.6f * g);
    *l = (in + g * (0.8f * v[0] + 0.5f * v[3] + 0.2f * (v[1] + v[2]))) * norm;
    *r = (in + g * (0.8f * v[1] + 0.5f * v[2] + 0.2f * (v[0] + v[3]))) * norm;
  }
};

// ------------------------------------------------------------------------------------------------ stereo widener
struct Widener {
  static const int kN = 1024, kM = kN - 1;
  float b[3][kN];
  int w;
  void Init() { memset(this, 0, sizeof *this); }
  float Allpass(int k, int delay, float in) {
    const float g = 0.55f;
    float d = b[k][(w - delay) & kM];
    float v = in + g * d;
    b[k][w & kM] = v;
    return d - g * v;
  }
  // mono-compatible: l + r is unchanged
  void Process(float* l, float* r, float amount) {
    float mid = 0.5f * (*l + *r);
    float s = Allpass(0, 221, mid);
    s = Allpass(1, 347, s);
    s = Allpass(2, 509, s);
    w = (w + 1) & kM;
    float side = amount * 0.9f * s;
    *l += side;
    *r -= side;
  }
};

// ------------------------------------------------------------------------------------------------ grains
// Reads the input history with 4-point Hermite interpolation.
inline float Hermite(const float* buf, long pos_i, float f) {
  float xm1 = buf[(pos_i - 1) & kInMask], x0 = buf[pos_i & kInMask], x1 = buf[(pos_i + 1) & kInMask],
        x2 = buf[(pos_i + 2) & kInMask];
  float c = 0.5f * (x1 - xm1), v = x0 - x1, w = c + v, a = w + v + 0.5f * (x2 - x0), bn = w + a;
  return ((a * f - bn) * f + c) * f + x0;
}

struct Grains {
  float in[kInN];
  float out[kOutN];
  long n;                                     // samples written; the newest is n - 1
  long marks[96];                             // analysis marks (absolute sample positions), ring
  int nm;                                     // marks written
  long last_mark;
  float T;                                    // smoothed period in samples (held when unvoiced)

  void Init() {
    memset(this, 0, sizeof *this);
    T = 441.0f;
    last_mark = -1;
  }
  void Push(float x) { in[n & kInMask] = x; ++n; }
  long Cur() const { return n - 1; }

  // Follow the voice: the next mark is one period after the last, nudged to the strongest peak near that spot.
  void TrackMarks() {
    long cur = Cur();
    if (last_mark < 0) { last_mark = cur - (long)T; return; }
    int q = (int)(T * 0.25f);
    if (q < 2) q = 2;
    for (int guard = 0; guard < 4; ++guard) {
      long expect = last_mark + (long)(T + 0.5f);
      if (expect + q > cur) return;                       // not enough look-ahead yet
      if (expect < cur - kInN / 2) { last_mark = cur - (long)T; return; }
      long best = expect; float bv = -1;
      for (long p = expect - q; p <= expect + q; ++p) {
        float v = fabsf(in[p & kInMask]) + 0.5f * fabsf(in[(p - 1) & kInMask]) + 0.5f * fabsf(in[(p + 1) & kInMask]);
        if (v > bv) { bv = v; best = p; }
      }
      last_mark = best;
      marks[nm % 96] = best;
      ++nm;
    }
  }
  // the newest mark that is at most `limit` (absolute), skipping `skip` newer usable ones; -1 if none
  long FindMark(long limit, int skip) const {
    int found = 0;
    for (int i = nm - 1; i >= 0 && i > nm - 90; --i) {
      long m = marks[i % 96];
      if (m <= limit) {
        if (found == skip) return m;
        ++found;
      }
    }
    return -1;
  }
  // Add one grain: centred at output position `centre`, cut around source position `c`, resampled by `fr` (source samples
  // per output sample: > 1 raises the formants), half-length `hl` output samples, Hann window, scaled by `gain`.
  void AddGrain(long centre, long c, float fr, int hl, float gain, bool reverse) {
    if (hl < 2) return;
    float inv = kPi / hl;
    float dir = reverse ? -fr : fr;
    float pos = (float)c - dir * hl;
    const float cd = cosf(inv), sd = sinf(inv);               // the window by rotation: cos(pi) at the start, no cosf per sample
    float wc = -1.0f, ws = 0.0f;
    for (int j = -hl; j <= hl; ++j, pos += dir) {
      float w = 0.5f + 0.5f * wc;
      { float nc = wc * cd - ws * sd; ws = ws * cd + wc * sd; wc = nc; }
      long pi = (long)floorf(pos);
      out[(centre + j) & kOutMask] += gain * w * Hermite(in, pi, pos - pi);
    }
  }
  float Emit(int delay) {
    long idx = (Cur() - delay) & kOutMask;
    float v = out[idx];
    out[idx] = 0.0f;
    return v;
  }
};

// ------------------------------------------------------------------------------------------------ sequencer patterns
// 100 patterns of 16 steps, -1..1, made by code (so nothing is stored or copied):
//   0-9 basic shapes   10-19 repeated shapes   20-39 melodic (steps are whole semitones / 12)   40-59 rhythms (euclid)
//   60-79 swells and drops   80-99 seeded random   (a pattern number above 99 is 'rndm': a fresh random pattern)
inline void MakePattern(int idx, float* s, Rng* rnd) {
  Rng r; r.Seed(0x1234567u + idx * 7919u);
  for (int i = 0; i < 16; ++i) {
    float t = i / 16.0f, v = 0;
    if (idx < 0 || idx > 99) { v = Clampf(roundf(rnd->Bi() * 12.0f) / 12.0f * (rnd->Uni() < 0.5f ? 1.0f : 0.6f), -1, 1); s[i] = v; continue; }
    if (idx < 10) {
      switch (idx) {
        case 0: v = -1 + 2 * t; break;
        case 1: v = 1 - 2 * t; break;
        case 2: v = 1 - fabsf(2 * t * 2 - 1) * 2 + 0.0f; v = 2 * (t < 0.5f ? 2 * t : 2 - 2 * t) - 1; break;
        case 3: v = -(2 * (t < 0.5f ? 2 * t : 2 - 2 * t) - 1); break;
        case 4: v = sinf(2 * kPi * t); break;
        case 5: v = cosf(2 * kPi * t); break;
        case 6: v = sinf(kPi * t) * 2 - 1; break;
        case 7: v = t < 0.5f ? 1.0f : -1.0f; break;
        case 8: v = t < 0.25f ? 1.0f : -1.0f; break;
        default: v = -1 + 2 * (floorf(t * 4) / 3.0f); break;
      }
    } else if (idx < 20) {
      int k = idx - 10;                                     // cycles per pattern 2,2,4,4,... of shapes
      float cyc = (float)(2 << (k / 4 > 2 ? 2 : k / 4));
      float p = t * cyc; p -= floorf(p);
      switch (k % 4) {
        case 0: v = -1 + 2 * p; break;
        case 1: v = 1 - 2 * p; break;
        case 2: v = 2 * (p < 0.5f ? 2 * p : 2 - 2 * p) - 1; break;
        default: v = sinf(2 * kPi * p); break;
      }
    } else if (idx < 40) {
      static const int sets[10][4] = {{0, 4, 7, 12}, {0, 3, 7, 12}, {0, 7, 12, 7}, {0, 5, 7, 12}, {0, 12, 0, 7},
                                      {0, 2, 4, 7}, {0, 3, 5, 7}, {0, 4, 7, 11}, {0, 7, 10, 12}, {0, -5, 0, 7}};
      int k = idx - 20, set = k % 10, mode = k / 10;      // mode 0 up, 1 up-down
      int step = i % 4;
      if (mode == 1 && (i / 4) % 2 == 1) step = 3 - step;
      int semis = sets[set][step] + ((i / 8) % 2 == 1 ? (mode == 0 ? 0 : -12) : 0);
      v = Clampf(semis / 12.0f, -1, 1);
    } else if (idx < 60) {
      int k = idx - 40, hits = 2 + (k * 13) / 20 + (k % 3), len = 16;
      int acc = 0, on = 0;                                  // Bresenham euclid, rotated by k % 4
      for (int j = 0; j <= (i + (k % 4)) % len; ++j) { acc += hits; if (acc >= len) { acc -= len; on = 1; } else on = 0; }
      v = on ? 1.0f : 0.0f;
      if (k >= 10) v = on ? (i % 4 == 0 ? 1.0f : 0.6f) : 0.0f;
    } else if (idx < 80) {
      int k = idx - 60;
      float p = (float)(i % (4 << (k % 3))) / (4 << (k % 3));
      switch (k % 4) {
        case 0: v = p; break;                               // swell
        case 1: v = 1 - p; break;                           // drop
        case 2: v = 0.5f * sinf(2 * kPi * p) + 0.5f * (1 - p); break;
        default: v = (i % 2) ? 0.25f : 1.0f; break;
      }
      v = v * 2 - 1;
      if (k >= 10) v *= (k % 5 + 1) * 0.2f;
    } else {
      float q = roundf(r.Bi() * 12.0f) / 12.0f;
      v = (idx % 3 == 0) ? q : Clampf(roundf(r.Bi() * 4.0f) / 4.0f, -1, 1);
    }
    s[i] = Clampf(v, -1, 1);
  }
}

// value of a pattern at `phase` (0..1 of one cycle) for shapes 0-4 (manual: 0 rounded, 1 lines, 2 steps, 3 drops, 4 punchy drops)
inline float PatternValue(const float* s, float phase, int shape) {
  float pos = phase * 16.0f;
  int i = (int)pos; if (i > 15) i = 15; if (i < 0) i = 0;
  float f = pos - i, a = s[i], b = s[(i + 1) & 15];
  switch (shape) {
    case 0: return a + (b - a) * (0.5f - 0.5f * cosf(kPi * f));
    case 1: return a + (b - a) * f;
    case 2: return a;
    case 3: return a * (1.0f - f);
    default: { float d = 1.0f - f; return a * d * d * d; }
  }
}

}  // namespace alpha
