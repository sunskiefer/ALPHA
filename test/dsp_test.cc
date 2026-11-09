// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Host sanity test for ALPHA's DSP: a synthetic voiced signal goes through the real engine; checks the output pitch,
// level, NaN/inf, and every factory preset.  Not a substitute for the Force: no ARM code runs here.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <unistd.h>
extern "C" {
#include "engine.h"
}
extern "C" const mpc_engine_t* mpc_engine(void);
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("FAIL "); } else printf("ok   "); printf(__VA_ARGS__); printf("\n"); } while (0)

static float voice(double t, double f0) {      // glottal-ish pulse train through two resonances (crude vowel)
  static double ph = 0, y1a = 0, y2a = 0, y1b = 0, y2b = 0; (void)t;
  ph += f0 / 44100.0; double x = 0;
  if (ph >= 1.0) { ph -= 1.0; x = 1.0; }
  auto res = [](double in, double f, double bw, double& y1, double& y2) {
    double r = exp(-M_PI * bw / 44100.0), c = 2 * r * cos(2 * M_PI * f / 44100.0);
    double y = in + c * y1 - r * r * y2; y2 = y1; y1 = y; return y; };
  double y = res(x, 700, 90, y1a, y2a) * 0.02 + res(x, 1200, 110, y1b, y2b) * 0.015;
  return (float)y;
}
struct Run { std::vector<float> L, R; bool bad; double ms; };
static Run run(const mpc_engine_t* E, void* s, double f0, double secs, bool silent = false) {
  Run r; r.bad = false; int n = (int)(secs * 44100); r.L.reserve(n); r.R.reserve(n);
  auto t0 = std::chrono::steady_clock::now();
  std::vector<int16_t> in(256), out(256);
  for (int done = 0; done < n; done += 128) {
    for (int i = 0; i < 128; ++i) { float v = silent ? 0.f : voice(0, f0); int16_t q = (int16_t)(v * 32767 * 8); in[2*i] = in[2*i+1] = q; }
    E->process(s, in.data(), out.data(), 128);
    for (int i = 0; i < 128; ++i) { r.L.push_back(out[2*i] / 32768.f); r.R.push_back(out[2*i+1] / 32768.f); }
  }
  r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return r;
}
static double f0_of(const std::vector<float>& x, size_t a, size_t b) {   // normalised autocorrelation, first local peak >= 0.93 of the best, 60..1200 Hz
  double e0 = 0; for (size_t i = a; i < b; ++i) e0 += x[i] * x[i];
  if (e0 < 1e-9) return 0;
  std::vector<double> c(760, 0.0); double best = 0;
  for (int lag = 36; lag < 740; ++lag) { double s = 0; for (size_t i = a; i + lag < b; ++i) s += x[i] * x[i + lag]; c[lag] = s; if (s > best) best = s; }
  for (int lag = 37; lag < 739; ++lag) if (c[lag] >= 0.93 * best && c[lag] >= c[lag-1] && c[lag] >= c[lag+1]) return 44100.0 / lag;
  return 0;
}
static double rms(const std::vector<float>& x, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; ++i) e += x[i]*x[i]; return sqrt(e / (b - a)); }
static bool finite(const std::vector<float>& x) { for (float v : x) if (!std::isfinite(v) || fabsf(v) > 1.0f) return false; return true; }
static void setp(const mpc_engine_t* E, void* s, const char* k, const char* v) { E->set_param(s, k, v); }

int main() {
  const mpc_engine_t* E = mpc_engine();
  double f0 = 150;
  { void* s = E->create("/tmp/alpha_test_presets"); setp(E,s,"wet","1"); setp(E,s,"cons","0");
    Run r = run(E, s, f0, 2.5); size_t a = 44100, b = 44100 + 8192;
    double f = f0_of(r.L, a, b); CHECK(fabs(f - 150) < 6, "pitch 0: output f0 %.1f Hz (want 150), rms %.4f", f, rms(r.L, a, b));
    CHECK(finite(r.L) && finite(r.R), "finite and within full scale");
    E->destroy(s); }
  { void* s = E->create("/tmp/alpha_test_presets"); setp(E,s,"wet","1"); setp(E,s,"cons","0"); setp(E,s,"snap","0"); setp(E,s,"pitch","-12");
    Run r = run(E, s, f0, 2.5); size_t a = 44100, b = 44100 + 8192; double e0=0; for (size_t i=a;i<b;++i) e0+=r.L[i]*r.L[i];
    for (int lag : {294, 588}) { double c=0; for (size_t i=a;i+lag<b;++i) c+=r.L[i]*r.L[i+lag]; printf("info -12 corr lag %d = %.3f\n", lag, c/e0); }
    int peaks=0; for (size_t i=a+1;i<b-1;++i) if (fabsf(r.L[i])>0.5f*0.0f+0.1f && fabsf(r.L[i])>=fabsf(r.L[i-1]) && fabsf(r.L[i])>fabsf(r.L[i+1])) ++peaks; printf("info peaks %d in %zu samples\n", peaks, b-a); { double mx=0; for(size_t i=a;i<b;++i) mx=fmax(mx,fabs(r.L[i])); printf("info env max %.3f; pulses>0.6max at:", mx); long last=-1000; for(size_t i=a;i<a+3000;++i) if(fabs(r.L[i])>0.6*mx && (long)i-last>100){printf(" %zu",i-a); last=i;} printf("\n"); } E->destroy(s); }
  struct Case { const char* v; double want; } cs[] = {{"12", 300}, {"-12", 75}, {"7", 150 * pow(2, 7.0/12)}, {"-5", 150 * pow(2, -5.0/12)}};
  for (auto& c : cs) { void* s = E->create("/tmp/alpha_test_presets"); setp(E,s,"wet","1"); setp(E,s,"cons","0"); setp(E,s,"snap","0"); setp(E,s,"pitch",c.v);
    Run r = run(E, s, f0, 2.5); size_t a = 44100, b = 44100 + 8192; double f = f0_of(r.L, a, b);
    CHECK(fabs(f / c.want - 1) < 0.04 && rms(r.L, a, b) > 0.001, "pitch %s st: f0 %.1f Hz (want %.1f), rms %.4f", c.v, f, c.want, rms(r.L, a, b)); E->destroy(s); }
  { void* s = E->create("/tmp/alpha_test_presets"); setp(E,s,"wet","1"); setp(E,s,"cons","0"); setp(E,s,"formant","6");
    Run r = run(E, s, f0, 2.5); double f = f0_of(r.L, 44100, 44100 + 8192);
    CHECK(fabs(f - 150) < 6, "formant +6 keeps f0: %.1f Hz", f); E->destroy(s); }
  { void* s = E->create("/tmp/alpha_test_presets"); Run r = run(E, s, f0, 0.5, true);
    CHECK(finite(r.L) && rms(r.L, 0, r.L.size()) < 1e-6, "silence in -> silence out"); E->destroy(s); }
  { void* s = E->create("/tmp/alpha_test_presets"); setp(E,s,"wet","0");
    Run r = run(E, s, f0, 1.0); double lat = -1; // find the dry delay: first big sample
    Run q = run(E, s, f0, 1.0); (void)q; CHECK(rms(r.L, 30000, 40000) > 0.001, "wet 0: dry passes (rms %.4f)", rms(r.L, 30000, 40000)); (void)lat; E->destroy(s); }

  { void* s = E->create("/tmp/alpha_test_presets"); setp(E,s,"wet","1"); setp(E,s,"cons","0"); setp(E,s,"midi_mode","1"); setp(E,s,"snap","0");
    uint8_t on[3] = {0x90, 57, 100}; E->midi(s, on, 3);
    Run r = run(E, s, f0, 2.5); double f = f0_of(r.L, 44100, 44100 + 8192);
    CHECK(fabs(f / 220.0 - 1) < 0.04, "MIDI mono A3: output f0 %.1f Hz (want 220)", f);
    uint8_t off[3] = {0x80, 57, 0}; E->midi(s, off, 3); Run r2 = run(E, s, f0, 1.0);
    CHECK(f0_of(r2.L, 20000, 28000) > 0, "mono keeps sounding after note-off (ungated)"); E->destroy(s); }
  { void* s = E->create("/tmp/alpha_test_presets"); setp(E,s,"wet","1"); setp(E,s,"cons","0"); setp(E,s,"midi_mode","3"); setp(E,s,"m_att","5"); setp(E,s,"m_rel","20");
    Run r = run(E, s, f0, 1.5); CHECK(rms(r.L, 30000, 40000) < 0.002, "M.Gated: silent with no note (rms %.5f)", rms(r.L, 30000, 40000));
    uint8_t on[3] = {0x90, 57, 100}; E->midi(s, on, 3); Run r2 = run(E, s, f0, 1.5);
    CHECK(rms(r2.L, 30000, 40000) > 0.05, "M.Gated: sounds with a note (rms %.4f)", rms(r2.L, 30000, 40000));
    uint8_t off[3] = {0x80, 57, 0}; E->midi(s, off, 3); Run r3 = run(E, s, f0, 1.5);
    CHECK(rms(r3.L, 30000, 40000) < 0.002, "M.Gated: silent after note-off (rms %.5f)", rms(r3.L, 30000, 40000)); E->destroy(s); }
  { void* s = E->create("/tmp/alpha_test_presets"); setp(E,s,"wet","1"); setp(E,s,"cons","0");
    Run r = run(E, s, f0, 1.5); setp(E,s,"freeze","1"); Run q = run(E, s, f0, 0.5, true); // freeze, then the input stops
    double a = rms(q.L, 4000, 20000); double f = f0_of(q.L, 8000, 16192);
    CHECK(a > 0.01 && fabs(f - 150) < 8, "Freeze: sound held after input stops (rms %.4f, f0 %.1f)", a, f);
    setp(E,s,"freeze","0"); Run z = run(E, s, f0, 0.5, true); CHECK(rms(z.L, 15000, 21000) < 0.002, "Freeze off: decays to silence (rms %.5f)", rms(z.L, 15000, 21000)); E->destroy(s); }
  { void* s = E->create("/tmp/alpha_test_presets"); setp(E,s,"wet","1"); setp(E,s,"cons","0"); setp(E,s,"harm","2"); setp(E,s,"ratio","1");
    Run r = run(E, s, f0, 2.0); CHECK(finite(r.L) && rms(r.L, 44100, 60000) > 0.01, "harmonic shifter: runs, rms %.4f", rms(r.L, 44100, 60000)); E->destroy(s); }
  { // latency: a click through a dry path (wet 0) and through the grains (wet 1)
    for (int range = 0; range < 3; ++range) { void* s = E->create("/tmp/alpha_test_presets"); char d[4]; snprintf(d,4,"%d",range); setp(E,s,"detect",d); setp(E,s,"cons","0");
      setp(E,s,"wet","0"); std::vector<int16_t> in(256), out(256); int first = -1, pos = 0; long maxl=0;
      for (int b = 0; b < 200; ++b) { for (int i = 0; i < 128; ++i) in[2*i]=in[2*i+1]=(b==100&&i==0)?20000:0; E->process(s,in.data(),out.data(),128);
        for (int i = 0; i < 128; ++i) { if (first<0 && abs(out[2*i])>2000) first = b*128+i; } (void)pos; (void)maxl; }
      printf("info detect %d: dry click latency %d samples = %.1f ms\n", range, first - 100*128, (first-100*128)*1000.0/44100); E->destroy(s); } }

  { // state round trip and user presets (worker thread, files)
    system("rm -rf /tmp/alpha_test_presets2"); void* s = E->create("/tmp/alpha_test_presets2");
    setp(E,s,"pitch","5"); setp(E,s,"formant","-3"); setp(E,s,"m2_on","1"); setp(E,s,"m2_a_wet","0.4"); setp(E,s,"m3_patt","25"); setp(E,s,"upre","7");
    char st[8192]; int n = E->get_param(s, "state", st, sizeof st); CHECK(n > 20 && n < 8000, "state is %d bytes", n);
    void* t = E->create("/tmp/alpha_test_presets2"); E->set_param(t, "state", st); char b[64];
    E->get_param(t, "pitch", b, 64); bool ok1 = atof(b) == 5; E->get_param(t, "m2_a_wet", b, 64); bool ok2 = fabs(atof(b) - 0.4) < 1e-6; E->get_param(t, "m3_patt", b, 64); bool ok3 = atoi(b) == 25;
    char st2[8192]; E->get_param(t, "state", st2, sizeof st2);
    CHECK(ok1 && ok2 && ok3 && !strcmp(st, st2), "state restores pitch, slot amount, pattern; saving it again gives the same text");
    setp(E,s,"upa_2","1"); usleep(300000); E->get_param(s, "upre_info", b, 64); CHECK(strstr(b, "SAVED"), "user preset save: \"%s\"", b);
    setp(E,s,"pitch","-9"); setp(E,s,"m2_on","0"); setp(E,s,"upa_1","1"); usleep(300000);
    std::vector<int16_t> in(256, 0), out(256); for (int k = 0; k < 4; ++k) E->process(s, in.data(), out.data(), 128);
    E->get_param(s, "pitch", b, 64); bool p1 = atof(b) == 5; E->get_param(s, "m2_on", b, 64); bool p2 = atoi(b) == 1;
    E->get_param(s, "upre_info", b, 64); CHECK(p1 && p2, "user preset load restores the sound (pitch ok %d, m2_on ok %d, info \"%s\")", p1, p2, b);
    E->destroy(t); E->destroy(s); }

  { // LFO source: a 5 Hz sine on pitch must swing the output pitch by about +-2.9 st
    void* s = E->create("/tmp/alpha_test_presets"); setp(E,s,"wet","1"); setp(E,s,"cons","0"); setp(E,s,"snap","0");
    setp(E,s,"m1_on","1"); setp(E,s,"m1_src","5"); setp(E,s,"m1_wave","0"); setp(E,s,"m1_sync","0"); setp(E,s,"m1_rate","3"); setp(E,s,"m1_a_pitch","0.12");
    Run r = run(E, s, 150, 3.0); std::vector<double> fs;
    for (size_t a = 44100; a + 2048 < r.L.size(); a += 1024) { double f = f0_of(r.L, a, a + 2048); if (f > 90 && f < 400) fs.push_back(f); }
    std::sort(fs.begin(), fs.end()); double lo = fs[fs.size() / 10], hi = fs[fs.size() * 9 / 10];
    double st = 12 * log2(hi / lo);
    CHECK(st > 3.0 && st < 8.5, "LFO sine on pitch: f0 10th..90th percentile %.1f..%.1f Hz (%.1f st, want 3..8.5; peak-to-peak is 5.8)", lo, hi, st);
    E->destroy(s); }
  { // all 7 LFO waves stay finite and in range
    for (int w = 0; w < 7; ++w) { void* s = E->create("/tmp/alpha_test_presets"); char b[8]; snprintf(b,8,"%d",w);
      setp(E,s,"m1_on","1"); setp(E,s,"m1_src","5"); setp(E,s,"m1_wave",b); setp(E,s,"m1_sync","0"); setp(E,s,"m1_rate","8"); setp(E,s,"m1_a_formant","1"); setp(E,s,"m1_a_pitch","1");
      Run r = run(E, s, 150, 1.5); CHECK(finite(r.L) && finite(r.R), "LFO wave %d: finite, inside full scale", w); E->destroy(s); } }
  { // Panic tile (header) and the Panic trigger: voices off, freeze off, sound stops
    void* s = E->create("/tmp/alpha_test_presets"); setp(E,s,"wet","1"); setp(E,s,"cons","0"); setp(E,s,"midi_mode","4"); setp(E,s,"m_att","5"); setp(E,s,"m_rel","2000");
    uint8_t on[3] = {0x90, 57, 100}; E->midi(s, on, 3); Run r = run(E, s, f0, 1.0); setp(E,s,"freeze","1");
    setp(E,s,"nv_4","1"); Run q = run(E, s, 0, 0.6, true); char b[16]; E->get_param(s, "freeze", b, 16);
    CHECK(rms(q.L, 20000, 26000) < 0.002 && atoi(b) == 0, "Panic tile: stops voices, clears freeze (tail rms %.5f, freeze %s)", rms(q.L, 20000, 26000), b); E->destroy(s); }
  { // the Start page and the tiles: 12 tiles, START first, the header has 4 tiles
    void* s = E->create("/tmp/alpha_test_presets"); char b[64];
    E->get_param(s, "go_1", b, 64); bool t1 = !strcmp(b, "START"); E->get_param(s, "go_12", b, 64); bool t12 = !strcmp(b, "GLOBAL");
    E->get_param(s, "nv_4", b, 64); bool pn = !strcmp(b, "PANIC");
    setp(E,s,"go_1","1"); E->get_param(s, "view", b, 64); bool v12 = atoi(b) == 12;
    setp(E,s,"ql1","0.75"); E->get_param(s, "pitch", b, 64); bool q1 = fabs(atof(b) - 12.0) < 0.01;
    CHECK(t1 && t12 && pn && v12 && q1, "overview: START first, GLOBAL last, PANIC in the header, START opens view 12, its Q-Link 1 is Pitch");
    E->destroy(s); }
  { // hints
    void* s = E->create("/tmp/alpha_test_presets"); char b[96]; setp(E,s,"view","2"); E->get_param(s, "hint", b, 96);
    bool h1 = strstr(b, "HARM") != 0; setp(E,s,"harm","3"); E->get_param(s, "hint", b, 96); bool h2 = !strcmp(b, " ");
    CHECK(h1 && h2, "hint on the Harmonics page: shown when Harm is 0, empty when it is not"); E->destroy(s); }
  // every factory preset: finite, no runaway, some signal
  { FILE* f = fopen("presets.json", "r"); std::string txt; char buf[4096]; size_t k; while ((k = fread(buf,1,sizeof buf,f))>0) txt.append(buf,k); fclose(f);
    // crude parse: each {"name": ..., "values": {...}}
    size_t p = 0; int n = 0, bad = 0;
    while ((p = txt.find("\"name\"", p)) != std::string::npos) {
      size_t q1 = txt.find('"', txt.find(':', p) + 1), q2 = txt.find('"', q1 + 1); std::string name = txt.substr(q1 + 1, q2 - q1 - 1);
      size_t vb = txt.find('{', q2), ve = txt.find('}', vb); std::string vals = txt.substr(vb + 1, ve - vb - 1);
      void* s = E->create("/tmp/alpha_test_presets");
      size_t i = 0; while ((i = vals.find('"', i)) != std::string::npos) { size_t j = vals.find('"', i + 1); std::string key = vals.substr(i + 1, j - i - 1);
        size_t c = vals.find(':', j); size_t e = vals.find_first_of(",}", c); std::string v = vals.substr(c + 1, e == std::string::npos ? std::string::npos : e - c - 1);
        while (!v.empty() && (v[0]==' '||v[0]=='"')) v.erase(0,1); while (!v.empty() && (v.back()==' '||v.back()=='"'||v.back()=='\n')) v.pop_back();
        setp(E, s, key.c_str(), v.c_str()); i = (e == std::string::npos) ? vals.size() : e + 1; }
      // MIDI note for the MIDI presets
      uint8_t on[3] = {0x90, 57, 100}; E->midi(s, on, 3);
      Run r = run(E, s, f0, 2.0); bool ok = finite(r.L) && finite(r.R);
      if (!ok) { ++bad; printf("FAIL preset \"%s\": non-finite or over full scale\n", name.c_str()); }
      ++n; E->destroy(s); p = ve; }
    CHECK(bad == 0, "%d factory presets render finite and inside full scale", n); }
  // CPU: worst case settings
  { void* s = E->create("/tmp/alpha_test_presets"); setp(E,s,"midi_mode","4"); setp(E,s,"harm","3"); setp(E,s,"fm","0.5"); setp(E,s,"alt","5"); setp(E,s,"oct","-1");
    setp(E,s,"smear","0.5"); setp(E,s,"detune","1"); setp(E,s,"stereo","1"); setp(E,s,"density","1"); setp(E,s,"detect","0");
    for (int n : {48, 52, 55, 59}) { uint8_t on[3] = {0x90, (uint8_t)n, 100}; E->midi(s, on, 3); }
    Run r = run(E, s, 90, 5.0); double per_block = r.ms / (5.0 * 44100 / 128);
    CHECK(finite(r.L), "worst case finite"); printf("info x86 worst case: %.1f us per 128-frame block (budget 2902 us on the Force; this is a desktop CPU, NOT the Force)\n", per_block * 1000);
    E->destroy(s); }
  { void* s = E->create("/tmp/alpha_test_presets"); Run r = run(E, s, 150, 5.0); printf("info x86 default settings: %.1f us per 128-frame block\n", r.ms / (5.0 * 44100 / 128) * 1000); E->destroy(s); }
  { void* s = E->create("/tmp/alpha_test_presets"); setp(E,s,"pitch","7"); setp(E,s,"detune","0.5"); setp(E,s,"stereo","0.5"); setp(E,s,"harm","1"); Run r = run(E, s, 150, 5.0); printf("info x86 typical (pitch+detune+stereo+harmonic): %.1f us per 128-frame block\n", r.ms / (5.0 * 44100 / 128) * 1000); E->destroy(s); }
  printf("%s\n", fails ? "FAILED" : "PASSED");
  return fails != 0;
}
