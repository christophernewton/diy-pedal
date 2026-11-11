// Monophonic pitch tracker tuned for bass guitar.
//
// A bass note often carries more energy in its second harmonic than in its
// fundamental, so the raw signal crosses zero two or three times per cycle
// and a naive detector reads an octave high. This tracker deals with that
// in two steps:
//
//  1. It looks for a repeating pattern in the gaps between zero crossings,
//     so it finds the true period even when there are several crossings per
//     cycle.
//  2. Once it has a pitch it pulls a low-pass filter down onto the note.
//     That strips the harmonics and leaves a clean fundamental, which gives
//     one crossing per cycle and a steady reading.
//
// Besides a frequency estimate it exposes two flip-flops clocked by the
// fundamental. Those are the heart of an analogue-style octave divider and
// respond with no added latency.
#pragma once

#include "common.h"

namespace dsp {

class PitchTracker {
 public:
  void Init(float sample_rate) {
    sr_ = sample_rate;
    dc_.SetFreq(25.0f, sr_);
    env_.Set(0.0005f, 0.050f, sr_);
    fast_.Set(0.001f, 0.010f, sr_);
    slow_.Set(0.030f, 0.300f, sr_);
    Reset();
  }

  void Reset() {
    dc_.Reset();
    lp1_.Reset();
    lp2_.Reset();
    env_.Reset();
    fast_.Reset();
    slow_.Reset();
    cutoff_ = cutoff_target_ = kOpenCutoff;
    ApplyCutoff();
    prev_ = 0.0f;
    armed_ = false;
    trigger_ = false;
    since_cross_ = 0;
    onset_holdoff_ = 0;
    last_frac_ = 0.0f;
    ForgetGaps();
    freq_ = 0.0f;
    has_pitch_ = false;
    ff1_ = ff2_ = false;
    tick_ = 0;
  }

  // Feed one input sample. Returns the fundamental of the note, close to a
  // sine once locked, at roughly the level it has in the input.
  float Process(float x) {
    const float s = dc_.Hp(x);

    // A new pluck: open the filter again so it cannot hide a note that is
    // much higher than the last one.
    const float fast = fast_.Process(s);
    const float slow = slow_.Process(s);
    onset_ = false;
    if (fast > 3.0f * slow && fast > kNoiseFloor && onset_holdoff_ == 0) {
      cutoff_ = cutoff_target_ = kOpenCutoff;
      ApplyCutoff();
      has_pitch_ = false;
      ForgetGaps();
      onset_holdoff_ = static_cast<int>(0.05f * sr_);
      onset_ = true;
    }
    if (onset_holdoff_ > 0) --onset_holdoff_;

    lp1_.Process(s);
    lp2_.Process(lp1_.lp);
    // The filter sits below the note, so it also turns the note down by a
    // known amount. Undo that so levels downstream stay meaningful.
    const float f = lp2_.lp * makeup_;
    const float e = env_.Process(f);

    ++since_cross_;
    trigger_ = false;

    // A crossing only counts after the signal has swung clearly negative,
    // which ignores small wiggles around zero.
    if (f < -0.25f * e) armed_ = true;
    if (armed_ && f > 0.0f && e > kNoiseFloor) {
      armed_ = false;

      // Sub-sample position of the crossing between the last two samples.
      const float frac = (f - prev_) > 0.0f ? -prev_ / (f - prev_) : 0.0f;
      const float gap = static_cast<float>(since_cross_) + frac - last_frac_;
      since_cross_ = 0;
      last_frac_ = frac;

      PushGap(gap);
      const float period = FindPeriod();
      if (period > 0.0f) {
        freq_ = sr_ / period;
        has_pitch_ = true;
        cutoff_target_ = Clamp(kCutoffRatio * freq_, kMinCutoff, kMaxCutoff);
      }

      // Clock the dividers once per cycle of the fundamental. While the
      // signal still has several crossings per cycle, skip the extra ones.
      since_trigger_ += gap;
      if (!has_pitch_ || since_trigger_ > 0.7f * sr_ / freq_) {
        since_trigger_ = 0.0f;
        trigger_ = true;
        ff1_ = !ff1_;
        if (ff1_) ff2_ = !ff2_;
      }
    }

    // No crossing for a long time means the note has gone.
    if (since_cross_ > static_cast<int>(sr_ / kMinHz)) {
      has_pitch_ = false;
      ForgetGaps();
      cutoff_target_ = kOpenCutoff;
      since_cross_ = static_cast<int>(sr_ / kMinHz);
    }

    // Move the cutoff at control rate; tanf per sample would be wasteful.
    if (++tick_ >= 16) {
      tick_ = 0;
      cutoff_ += 0.08f * (cutoff_target_ - cutoff_);
      ApplyCutoff();
    }

    prev_ = f;
    return f;
  }

  // True on the sample where a new cycle of the fundamental begins.
  bool Trigger() const { return trigger_; }
  // True on the sample where a new pluck is detected.
  bool Onset() const { return onset_; }
  // Detected fundamental in Hz. Holds its last value while HasPitch() is
  // false.
  float Frequency() const { return freq_; }
  bool HasPitch() const { return has_pitch_; }
  // Square waves one and two octaves below the input, as +/-1.
  float Div2() const { return ff1_ ? 1.0f : -1.0f; }
  float Div4() const { return ff2_ ? 1.0f : -1.0f; }

 private:
  static constexpr float kMinHz = 26.0f;  // below a low B (30.9 Hz)
  static constexpr float kMaxHz = 700.0f;
  // The cutoff sits on the detected pitch: the fundamental loses 6 dB, the
  // second harmonic 25 dB. Going lower cleans the signal further but makes
  // the filter ring every time it retunes, which upsets the low notes.
  static constexpr float kCutoffRatio = 1.0f;
  static constexpr float kMinCutoff = 28.0f;
  static constexpr float kMaxCutoff = 450.0f;
  static constexpr float kOpenCutoff = 220.0f;
  static constexpr float kMaxMakeup = 4.0f;
  static constexpr float kNoiseFloor = 0.001f;
  static constexpr int kGaps = 6;

  void ApplyCutoff() {
    lp1_.Set(cutoff_, 0.7071f, sr_);
    lp2_.Set(cutoff_, 0.7071f, sr_);
    // Two cascaded second-order Butterworth stages lose 1 + (f/fc)^4.
    float makeup = 1.0f;
    if (freq_ > 0.0f) {
      const float r = freq_ / cutoff_;
      makeup = Clamp(1.0f + r * r * r * r, 1.0f, kMaxMakeup);
    }
    makeup_ = makeup;
  }

  void ForgetGaps() {
    for (int i = 0; i < kGaps; ++i) gaps_[i] = 0.0f;
    since_trigger_ = 0.0f;
  }

  void PushGap(float gap) {
    for (int i = kGaps - 1; i > 0; --i) gaps_[i] = gaps_[i - 1];
    gaps_[0] = gap;
  }

  // Looks for a period made of one, two or three crossing gaps that matches
  // the period before it. Returns it in samples, or 0 if nothing repeats.
  float FindPeriod() const {
    for (int n = 1; n <= 3; ++n) {
      float recent = 0.0f, earlier = 0.0f;
      bool filled = true;
      for (int i = 0; i < n; ++i) {
        if (gaps_[i] <= 0.0f || gaps_[i + n] <= 0.0f) filled = false;
        recent += gaps_[i];
        earlier += gaps_[i + n];
      }
      if (!filled) return 0.0f;
      if (fabsf(recent - earlier) < 0.04f * recent) {
        const float period = 0.5f * (recent + earlier);
        const float hz = sr_ / period;
        return (hz > kMinHz && hz < kMaxHz) ? period : 0.0f;
      }
    }
    return 0.0f;
  }

  float sr_ = 48000.0f;
  OnePole dc_;
  Svf lp1_, lp2_;
  EnvFollower env_, fast_, slow_;
  float cutoff_ = kOpenCutoff, cutoff_target_ = kOpenCutoff;
  float makeup_ = 1.0f;
  float prev_ = 0.0f;
  bool armed_ = false;
  bool trigger_ = false;
  bool onset_ = false;
  int since_cross_ = 0;
  int onset_holdoff_ = 0;
  float last_frac_ = 0.0f;
  float gaps_[kGaps] = {};
  float since_trigger_ = 0.0f;
  float freq_ = 0.0f;
  bool has_pitch_ = false;
  bool ff1_ = false, ff2_ = false;
  int tick_ = 0;
};

}  // namespace dsp
