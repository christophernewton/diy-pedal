// Offline checks for the DSP, run on the build machine rather than the pedal.
//
//   test_dsp                 run the checks, exit non-zero on failure
//   test_dsp --render DIR    also write a WAV of each effect on a bass riff
//
// The input is a synthetic plucked bass whose second harmonic is louder than
// its fundamental, which is the case that trips up naive pitch detection.

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "effects/bass_synth.h"
#include "effects/chorus.h"
#include "effects/compressor.h"
#include "effects/envelope_filter.h"
#include "effects/octaver.h"
#include "effects/phaser.h"
#include "effects/reverb.h"

namespace {

constexpr float kSr = 48000.0f;
constexpr size_t kBlock = 48;

int failures = 0;

void Check(bool ok, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  printf(ok ? "  ok    " : "  FAIL  ");
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
  if (!ok) ++failures;
}

uint32_t rng_state = 12345;
float Rand01() {
  rng_state = rng_state * 1664525u + 1013904223u;
  return static_cast<float>(rng_state >> 8) / 16777216.0f;
}

// Appends one plucked note to `out`.
void Pluck(std::vector<float>& out, float hz, float seconds, float peak) {
  static const float kHarmonics[] = {0.5f, 1.0f, 0.6f, 0.35f,
                                     0.25f, 0.15f, 0.1f, 0.08f};
  const size_t n = static_cast<size_t>(seconds * kSr);
  std::vector<float> note(n);
  float max = 0.0f;
  for (size_t i = 0; i < n; ++i) {
    const float t = static_cast<float>(i) / kSr;
    float s = 0.0f;
    for (int h = 0; h < 8; ++h) {
      const float f = hz * (h + 1) * (1.0f + 0.0004f * h * h);
      if (f > 0.45f * kSr) break;
      s += kHarmonics[h] * expf(-t * (1.5f + 1.2f * h)) *
           sinf(dsp::kTwoPi * f * t);
    }
    const float attack = t < 0.002f ? t / 0.002f : 1.0f;
    const float release = (n - i) < 240 ? (n - i) / 240.0f : 1.0f;
    note[i] = s * attack * release;
    if (fabsf(note[i]) > max) max = fabsf(note[i]);
  }
  for (size_t i = 0; i < n; ++i) {
    out.push_back(note[i] * peak / max + (Rand01() - 0.5f) * 2e-4f);
  }
}

void Silence(std::vector<float>& out, float seconds) {
  out.insert(out.end(), static_cast<size_t>(seconds * kSr), 0.0f);
}

std::vector<float> Riff() {
  static const float kNotes[] = {41.2f, 41.2f, 49.0f, 55.0f, 82.4f, 73.4f,
                                 55.0f, 49.0f, 41.2f, 61.7f, 82.4f, 110.0f};
  std::vector<float> x;
  for (float hz : kNotes) Pluck(x, hz, 0.38f, 0.4f);
  Pluck(x, 41.2f, 1.6f, 0.4f);
  Silence(x, 2.0f);
  return x;
}

// Signal power at one frequency.
float PowerAt(const std::vector<float>& x, size_t from, size_t to, float hz) {
  const float w = dsp::kTwoPi * hz / kSr;
  const float coeff = 2.0f * cosf(w);
  double s1 = 0.0, s2 = 0.0;
  for (size_t i = from; i < to; ++i) {
    const double s0 = x[i] + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return static_cast<float>((s1 * s1 + s2 * s2 - coeff * s1 * s2) /
                            ((to - from) * (to - from)));
}

struct Stereo {
  std::vector<float> l, r;
};

Stereo Run(fx::Effect& effect, const fx::Controls& c,
           const std::vector<float>& in) {
  Stereo out;
  out.l.resize(in.size());
  out.r.resize(in.size());
  effect.Reset();
  for (size_t i = 0; i + kBlock <= in.size(); i += kBlock) {
    effect.Update(c);
    effect.Process(&in[i], &out.l[i], &out.r[i], kBlock);
  }
  return out;
}

fx::Controls Knobs(float k1, float k2, float k3, float k4, float k5,
                   int t1 = fx::kMiddle, int t2 = fx::kMiddle,
                   int t3 = fx::kMiddle) {
  fx::Controls c;
  c.knob[0] = k1;
  c.knob[1] = k2;
  c.knob[2] = k3;
  c.knob[3] = k4;
  c.knob[4] = k5;
  c.toggle[0] = t1;
  c.toggle[1] = t2;
  c.toggle[2] = t3;
  return c;
}

// ---------------------------------------------------------------- effects

float chorus_buffer[2400];
float predelay_buffer[10000];
float reverb_tank[fx::Reverb::kTankSize];

fx::Compressor compressor;
fx::Octaver octaver;
fx::BassSynth bass_synth;
fx::EnvelopeFilter envelope_filter;
fx::Chorus chorus;
fx::Phaser phaser;
fx::Reverb reverb;

struct Entry {
  const char* name;
  fx::Effect* effect;
  fx::Controls preset;  // used for the rendered demo
  bool rings;           // keeps sounding after the input stops
};

std::vector<Entry> Effects() {
  return {
      {"1-compressor", &compressor, Knobs(0.35f, 0.45f, 0.3f, 0.4f, 1.0f),
       false},
      {"2-octaver", &octaver, Knobs(0.7f, 0.6f, 0.0f, 0.45f, 0.0f), false},
      {"3-bass-synth", &bass_synth, Knobs(0.3f, 0.5f, 0.6f, 0.5f, 0.0f),
       false},
      {"4-envelope-filter", &envelope_filter,
       Knobs(0.45f, 0.3f, 0.6f, 0.4f, 0.8f, fx::kUp, fx::kUp), false},
      {"5-chorus", &chorus, Knobs(0.4f, 0.6f, 0.5f, 0.5f, 0.0f), false},
      {"6-phaser", &phaser, Knobs(0.3f, 0.7f, 0.5f, 0.5f, 0.5f), false},
      {"7-reverb", &reverb, Knobs(0.6f, 0.5f, 0.6f, 0.5f, 0.1f), true},
  };
}

// ------------------------------------------------------------------ tests

void TestPitchTracker() {
  printf("Pitch tracker, single notes\n");
  static const float kNotes[] = {30.87f, 41.2f, 55.0f, 73.42f, 98.0f,
                                 146.83f, 196.0f, 261.63f, 392.0f};
  for (float hz : kNotes) {
    std::vector<float> x;
    Pluck(x, hz, 1.2f, 0.4f);
    dsp::PitchTracker tracker;
    tracker.Init(kSr);
    size_t good = 0, total = 0;
    float lock_ms = -1.0f;
    for (size_t i = 0; i < x.size(); ++i) {
      tracker.Process(x[i]);
      const bool correct =
          tracker.HasPitch() && fabsf(tracker.Frequency() - hz) < 0.03f * hz;
      if (correct && lock_ms < 0.0f) lock_ms = 1000.0f * i / kSr;
      if (i >= static_cast<size_t>(0.15f * kSr) &&
          i < static_cast<size_t>(1.0f * kSr)) {
        ++total;
        if (correct) ++good;
      }
    }
    const float pct = 100.0f * good / total;
    Check(pct >= 95.0f, "%7.2f Hz: correct %5.1f%% of the time, locked in %.0f ms",
          hz, pct, lock_ms);
  }

  printf("Pitch tracker, note changes\n");
  static const float kSeq[] = {41.2f, 110.0f, 49.0f, 196.0f, 36.71f, 82.4f};
  std::vector<float> x;
  for (float hz : kSeq) Pluck(x, hz, 0.6f, 0.4f);
  dsp::PitchTracker tracker;
  tracker.Init(kSr);
  const size_t note_len = static_cast<size_t>(0.6f * kSr);
  for (size_t n = 0; n < 6; ++n) {
    size_t good = 0, total = 0;
    for (size_t i = 0; i < note_len; ++i) {
      tracker.Process(x[n * note_len + i]);
      if (i >= note_len * 3 / 10 && i < note_len * 9 / 10) {
        ++total;
        if (tracker.HasPitch() &&
            fabsf(tracker.Frequency() - kSeq[n]) < 0.03f * kSeq[n]) {
          ++good;
        }
      }
    }
    const float pct = 100.0f * good / total;
    Check(pct >= 95.0f, "note %zu (%.1f Hz): correct %5.1f%% of the time",
          n + 1, kSeq[n], pct);
  }
}

void TestOctaver() {
  printf("Octaver\n");
  std::vector<float> x;
  Pluck(x, 110.0f, 1.0f, 0.4f);
  const size_t a = static_cast<size_t>(0.2f * kSr);
  const size_t b = static_cast<size_t>(0.8f * kSr);

  Stereo one = Run(octaver, Knobs(0.0f, 0.6f, 0.0f, 0.4f, 0.0f), x);
  float p55 = PowerAt(one.l, a, b, 55.0f);
  float p110 = PowerAt(one.l, a, b, 110.0f);
  Check(p55 > 10.0f * p110 && p55 > 1e-5f,
        "one octave down: 55 Hz is %.1f dB above 110 Hz",
        10.0f * log10f(p55 / (p110 + 1e-12f)));

  Stereo two = Run(octaver, Knobs(0.0f, 0.0f, 0.6f, 0.4f, 0.0f), x);
  float p27 = PowerAt(two.l, a, b, 27.5f);
  p55 = PowerAt(two.l, a, b, 55.0f);
  Check(p27 > 10.0f * p55 && p27 > 1e-5f,
        "two octaves down: 27.5 Hz is %.1f dB above 55 Hz",
        10.0f * log10f(p27 / (p55 + 1e-12f)));
}

void TestBassSynth() {
  printf("Bass synth\n");
  std::vector<float> x;
  Pluck(x, 82.41f, 1.0f, 0.4f);
  const size_t a = static_cast<size_t>(0.2f * kSr);
  const size_t b = static_cast<size_t>(0.7f * kSr);

  Stereo out = Run(bass_synth, Knobs(0.7f, 0.1f, 0.0f, 0.0f, 0.0f), x);
  const float fund = PowerAt(out.l, a, b, 82.41f);
  const float below = PowerAt(out.l, a, b, 41.2f);
  const float above = PowerAt(out.l, a, b, 164.82f);
  Check(fund > 1e-4f && fund > 20.0f * below && fund > 20.0f * above,
        "plays the note it hears (82.4 Hz power %.5f, octave below %.6f, "
        "above %.6f)",
        fund, below, above);

  Stereo down = Run(bass_synth,
                    Knobs(0.7f, 0.1f, 0.0f, 0.0f, 0.0f, fx::kMiddle, fx::kDown),
                    x);
  Check(PowerAt(down.l, a, b, 41.2f) > 20.0f * PowerAt(down.l, a, b, 82.41f),
        "octave-down toggle moves the oscillator to 41.2 Hz");
}

void TestCompressor() {
  printf("Compressor\n");
  std::vector<float> x(static_cast<size_t>(2.0f * kSr));
  for (size_t i = 0; i < x.size(); ++i) {
    x[i] = 0.5f * sinf(dsp::kTwoPi * 100.0f * i / kSr);  // -6 dBFS
  }
  // Threshold -26 dB, ratio 4:1, fastest attack, slowest release, no
  // make-up gain. 20 dB over the threshold should come out 15 dB quieter.
  const fx::Controls c = Knobs(0.389f, 0.30103f, 0.0f, 1.0f, 1.0f, fx::kMiddle,
                               fx::kDown);
  Stereo out = Run(compressor, c, x);
  float peak = 0.0f;
  for (size_t i = x.size() - 4800; i < x.size(); ++i) {
    if (fabsf(out.l[i]) > peak) peak = fabsf(out.l[i]);
  }
  const float gr = dsp::LinToDb(peak / 0.5f);
  Check(fabsf(gr + 15.0f) < 2.0f, "gain reduction %.1f dB (expected -15 dB)",
        gr);
}

float Rms(const std::vector<float>& x, float from_s, float to_s) {
  const size_t a = static_cast<size_t>(from_s * kSr);
  const size_t b = static_cast<size_t>(to_s * kSr);
  double sum = 0.0;
  for (size_t i = a; i < b; ++i) sum += x[i] * x[i];
  return static_cast<float>(sqrt(sum / (b - a)));
}

void TestLadder() {
  printf("Ladder filter\n");
  // Two octaves above the cutoff a four-pole filter is 48 dB down.
  dsp::Ladder ladder;
  ladder.Set(200.0f, 0.0f, kSr);
  std::vector<float> y(static_cast<size_t>(kSr));
  for (size_t i = 0; i < y.size(); ++i) {
    y[i] = ladder.Process(0.5f * sinf(dsp::kTwoPi * 800.0f * i / kSr));
  }
  const float db = dsp::LinToDb(Rms(y, 0.5f, 1.0f) / (0.5f * 0.7071f));
  Check(fabsf(db + 48.0f) < 3.0f, "800 Hz through a 200 Hz cutoff: %.1f dB "
        "(expected -48 dB)", db);
}

void TestSynthEnvelope() {
  printf("Bass synth filter envelope\n");
  // A held note: the filter should open on the pluck and close again while
  // the string is still ringing, so the upper harmonics die away early.
  std::vector<float> x;
  Pluck(x, 55.0f, 1.5f, 0.4f);
  Stereo out = Run(bass_synth,
                   Knobs(0.2f, 0.3f, 0.8f, 0.0f, 0.0f, fx::kMiddle, fx::kMiddle,
                         fx::kUp),
                   x);
  const size_t early_a = static_cast<size_t>(0.05f * kSr);
  const size_t early_b = static_cast<size_t>(0.15f * kSr);
  const size_t late_a = static_cast<size_t>(0.6f * kSr);
  const size_t late_b = static_cast<size_t>(0.7f * kSr);
  const float early = PowerAt(out.l, early_a, early_b, 385.0f) /
                      PowerAt(out.l, early_a, early_b, 55.0f);
  const float late = PowerAt(out.l, late_a, late_b, 385.0f) /
                     PowerAt(out.l, late_a, late_b, 55.0f);
  Check(early > 10.0f * late, "7th harmonic against the fundamental: %.1f dB "
        "just after the pluck, %.1f dB later",
        10.0f * log10f(early), 10.0f * log10f(late));
}

void TestReverb() {
  printf("Reverb\n");
  // Decay knob set for an RT60 of 2 s on each programme, bright, no low cut:
  // the tail should lose about 30 dB per second.
  struct Programme {
    const char* name;
    int toggle;
    float knob;
  };
  const Programme programmes[] = {{"plate", fx::kMiddle, 0.5579f},
                                  {"hall", fx::kUp, 0.1383f}};
  std::vector<float> x;
  Pluck(x, 196.0f, 0.3f, 0.5f);
  Silence(x, 3.0f);
  for (const Programme& p : programmes) {
    Stereo out =
        Run(reverb, Knobs(p.knob, 1.0f, 1.0f, 0.0f, 0.0f, p.toggle), x);
    const float drop = dsp::LinToDb(Rms(out.l, 0.5f, 0.7f)) -
                       dsp::LinToDb(Rms(out.l, 1.5f, 1.7f));
    Check(drop > 22.0f && drop < 38.0f,
          "%s set to 2 s: tail falls %.1f dB in one second (expected 30 dB)",
          p.name, drop);
  }
  // Left and right should differ, or it is not a stereo reverb.
  Stereo out = Run(reverb, Knobs(0.5f, 0.6f, 1.0f, 0.3f, 0.0f), x);
  double ll = 0.0, rr = 0.0, lr = 0.0;
  for (size_t i = static_cast<size_t>(0.5f * kSr); i < out.l.size(); ++i) {
    ll += out.l[i] * out.l[i];
    rr += out.r[i] * out.r[i];
    lr += out.l[i] * out.r[i];
  }
  const float corr = static_cast<float>(lr / sqrt(ll * rr + 1e-30));
  Check(fabsf(corr) < 0.5f, "left and right tails are decorrelated (%.2f)",
        corr);
  Check(Rms(out.l, 0.4f, 0.6f) > 0.003f, "tail level %.4f with mix at full",
        Rms(out.l, 0.4f, 0.6f));
}

void TestStability() {
  printf("Stability with random control settings\n");
  std::vector<float> x;
  Pluck(x, 41.2f, 0.5f, 0.9f);
  Pluck(x, 98.0f, 0.5f, 0.9f);
  Pluck(x, 196.0f, 0.5f, 0.9f);
  Silence(x, 1.5f);

  for (const Entry& e : Effects()) {
    bool finite = true;
    float peak = 0.0f, residue = 0.0f;
    for (int trial = 0; trial < 40; ++trial) {
      fx::Controls c;
      for (float& k : c.knob) {
        const float r = Rand01();
        k = r < 0.15f ? 0.0f : (r > 0.85f ? 1.0f : Rand01());
      }
      for (int& t : c.toggle) t = static_cast<int>(Rand01() * 3.0f) % 3;
      Stereo out = Run(*e.effect, c, x);
      for (size_t i = 0; i < x.size(); ++i) {
        if (!std::isfinite(out.l[i]) || !std::isfinite(out.r[i])) {
          finite = false;
        }
        const float m = fmaxf(fabsf(out.l[i]), fabsf(out.r[i]));
        if (m > peak) peak = m;
        if (i > x.size() - 4800 && m > residue) residue = m;
      }
    }
    Check(finite && peak < 8.0f, "%-18s finite, peak %.2f", e.name, peak);
    if (!e.rings) {
      Check(residue < 0.01f, "%-18s falls silent after the input stops (%.5f)",
            e.name, residue);
    }
  }
}

// ----------------------------------------------------------------- render

void WriteWav(const std::string& path, const Stereo& s) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) {
    printf("cannot write %s\n", path.c_str());
    ++failures;
    return;
  }
  const uint32_t frames = static_cast<uint32_t>(s.l.size());
  const uint32_t data_bytes = frames * 4;
  const uint32_t rate = static_cast<uint32_t>(kSr);
  const uint8_t header[44] = {
      'R', 'I', 'F', 'F',
      static_cast<uint8_t>(36 + data_bytes),
      static_cast<uint8_t>((36 + data_bytes) >> 8),
      static_cast<uint8_t>((36 + data_bytes) >> 16),
      static_cast<uint8_t>((36 + data_bytes) >> 24),
      'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
      16, 0, 0, 0, 1, 0, 2, 0,
      static_cast<uint8_t>(rate), static_cast<uint8_t>(rate >> 8),
      static_cast<uint8_t>(rate >> 16), static_cast<uint8_t>(rate >> 24),
      static_cast<uint8_t>(rate * 4), static_cast<uint8_t>((rate * 4) >> 8),
      static_cast<uint8_t>((rate * 4) >> 16),
      static_cast<uint8_t>((rate * 4) >> 24),
      4, 0, 16, 0, 'd', 'a', 't', 'a',
      static_cast<uint8_t>(data_bytes), static_cast<uint8_t>(data_bytes >> 8),
      static_cast<uint8_t>(data_bytes >> 16),
      static_cast<uint8_t>(data_bytes >> 24)};
  fwrite(header, 1, 44, f);
  for (uint32_t i = 0; i < frames; ++i) {
    const int16_t frame[2] = {
        static_cast<int16_t>(dsp::SoftClip(s.l[i]) * 32767.0f),
        static_cast<int16_t>(dsp::SoftClip(s.r[i]) * 32767.0f)};
    fwrite(frame, 2, 2, f);
  }
  fclose(f);
  printf("  wrote %s\n", path.c_str());
}

void Render(const std::string& dir) {
  printf("Rendering demos\n");
  const std::vector<float> riff = Riff();
  WriteWav(dir + "/0-dry.wav", Stereo{riff, riff});
  for (const Entry& e : Effects()) {
    WriteWav(dir + "/" + e.name + ".wav", Run(*e.effect, e.preset, riff));
  }
}

}  // namespace

int main(int argc, char** argv) {
  compressor.Init(kSr);
  octaver.Init(kSr);
  bass_synth.Init(kSr);
  envelope_filter.Init(kSr);
  chorus.Init(kSr, chorus_buffer, 2400);
  phaser.Init(kSr);
  reverb.Init(kSr, reverb_tank, predelay_buffer, 10000);

  TestPitchTracker();
  TestOctaver();
  TestBassSynth();
  TestCompressor();
  TestLadder();
  TestSynthEnvelope();
  TestReverb();
  TestStability();

  if (argc == 3 && std::string(argv[1]) == "--render") Render(argv[2]);

  printf(failures ? "\n%d check(s) FAILED\n" : "\nAll checks passed\n",
         failures);
  return failures ? 1 : 0;
}
