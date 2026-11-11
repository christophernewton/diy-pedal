// Stereo plate reverb that keeps the low end tight.
//
// The tank is Jon Dattorro's plate ("Effect Design, Part 1", JAES 1997), the
// published form of the Lexicon-style figure-of-eight reverb that most
// studio-grade pedal reverbs descend from. Delay lengths, diffusion
// coefficients and output taps are the paper's, rescaled from its 29761 Hz
// reference rate. Room and hall are the same tank made smaller or larger.
//
// Only the part of the signal above the low cut is sent into the reverb, so
// the fundamental stays dry, mono and punchy while the upper harmonics
// bloom. The tail keeps ringing after you bypass the pedal.
//
//   Knob 1  Decay        room 0.3 .. 1.5 s, plate 0.5 .. 6 s, hall 1.5 .. 12 s
//   Knob 2  Tone         dark .. bright (damping inside the tank)
//   Knob 3  Mix          reverb level; the dry signal stays at full level
//   Knob 4  Low cut      40 .. 600 Hz, 12 dB/octave, on what feeds the reverb
//   Knob 5  Pre-delay    0 .. 200 ms
//   Toggle 1  Size: up hall, middle plate, down room
//   Toggles 2 and 3 unused
#pragma once

#include "../dsp/common.h"
#include "effect.h"

namespace fx {

class Reverb final : public Effect {
 public:
  // Floats of delay memory the tank needs at 48 kHz.
  static constexpr size_t kTankSize = 60000;

  // The pre-delay buffer needs at least 200 ms plus a few samples.
  void Init(float sample_rate, float* tank_buffer, float* predelay_buffer,
            size_t predelay_size) {
    sr_ = sample_rate;
    rate_scale_ = sr_ / kRefRate;
    float* p = tank_buffer;
    for (int i = 0; i < kLines; ++i) {
      const float scale = i < kAp1 ? rate_scale_ : rate_scale_ * kMaxSize;
      const size_t n = static_cast<size_t>(Length(i) * scale) + 48;
      lines_[i].Init(p, n);
      p += n;
    }
    predelay_.Init(predelay_buffer, predelay_size);
    Reset();
  }

  void Reset() override {
    for (dsp::DelayLine& line : lines_) line.Reset();
    predelay_.Reset();
    low_cut_.Reset();
    bandwidth_.Reset();
    damp_l_.Reset();
    damp_r_.Reset();
    lfo_.Reset();
  }

  void Update(const Controls& c) override {
    float lo, hi, rate;
    if (c.toggle[0] == kUp) {  // hall
      size_ = kMaxSize;
      lo = 1.5f;
      hi = 12.0f;
      excursion_ = 16.0f;
      rate = 0.5f;
    } else if (c.toggle[0] == kMiddle) {  // plate
      size_ = 1.0f;
      lo = 0.5f;
      hi = 6.0f;
      excursion_ = 8.0f;
      rate = 1.0f;
    } else {  // room
      size_ = 0.45f;
      lo = 0.3f;
      hi = 1.5f;
      excursion_ = 4.0f;
      rate = 1.3f;
    }
    lfo_.SetFreq(rate, sr_);
    excursion_ *= rate_scale_;

    // The signal is multiplied by `decay` four times per trip round the
    // tank, so the knob can be marked in seconds of RT60.
    const float rt60 = dsp::ExpMap(c.knob[0], lo, hi);
    const float loop_seconds = kLoopSamples * size_ / kRefRate;
    decay_ = powf(10.0f, -3.0f * loop_seconds / (4.0f * rt60));
    diffusion2_ = dsp::Clamp(decay_ + 0.15f, 0.25f, 0.50f);

    bandwidth_.SetFreq(dsp::ExpMap(c.knob[1], 4000.0f, 18000.0f), sr_);
    const float damp_hz = dsp::ExpMap(c.knob[1], 1500.0f, 12000.0f);
    damp_l_.SetFreq(damp_hz, sr_);
    damp_r_.SetFreq(damp_hz, sr_);

    wet_ = c.knob[2] * c.knob[2] * kWetGain;
    low_cut_.Set(dsp::ExpMap(c.knob[3], 40.0f, 600.0f), 0.7071f, sr_);
    predelay_samples_ = 1.0f + c.knob[4] * 0.200f * sr_;
  }

  void Process(const float* in, float* out_l, float* out_r,
               size_t size) override {
    const float k = rate_scale_ * size_;  // paper samples -> our samples
    for (size_t i = 0; i < size; ++i) {
      const float x = in[i];
      low_cut_.Process(x);
      predelay_.Write(low_cut_.hp);
      float s = bandwidth_.Lp(predelay_.Read(predelay_samples_));

      // Input diffusion smears the pluck before it reaches the tank.
      s = Allpass(kIn1, Length(kIn1) * rate_scale_, 0.75f, s);
      s = Allpass(kIn2, Length(kIn2) * rate_scale_, 0.75f, s);
      s = Allpass(kIn3, Length(kIn3) * rate_scale_, 0.625f, s);
      s = Allpass(kIn4, Length(kIn4) * rate_scale_, 0.625f, s);

      lfo_.Advance();
      const float mod_l = excursion_ * lfo_.Sine();
      const float mod_r = excursion_ * lfo_.Sine(0.25f);

      // The two halves of the tank feed each other.
      const float from_right = lines_[kD4].Read(Length(kD4) * k);
      const float from_left = lines_[kD2].Read(Length(kD2) * k);

      float l = s + decay_ * from_right;
      l = Allpass(kAp1, Length(kAp1) * k + mod_l, -0.70f, l);
      const float l1 = lines_[kD1].Read(Length(kD1) * k);
      lines_[kD1].Write(l);
      l = Allpass(kAp2, Length(kAp2) * k, diffusion2_,
                  decay_ * damp_l_.Lp(l1));
      lines_[kD2].Write(l);

      float r = s + decay_ * from_left;
      r = Allpass(kAp3, Length(kAp3) * k + mod_r, -0.70f, r);
      const float r1 = lines_[kD3].Read(Length(kD3) * k);
      lines_[kD3].Write(r);
      r = Allpass(kAp4, Length(kAp4) * k, diffusion2_,
                  decay_ * damp_r_.Lp(r1));
      lines_[kD4].Write(r);

      const float wet_l = Tap(kD3, 266, k) + Tap(kD3, 2974, k) -
                          Tap(kAp4, 1913, k) + Tap(kD4, 1996, k) -
                          Tap(kD1, 1990, k) - Tap(kAp2, 187, k) -
                          Tap(kD2, 1066, k);
      const float wet_r = Tap(kD1, 353, k) + Tap(kD1, 3627, k) -
                          Tap(kAp2, 1228, k) + Tap(kD2, 2673, k) -
                          Tap(kD3, 2111, k) - Tap(kAp4, 335, k) -
                          Tap(kD4, 121, k);

      out_l[i] = x + wet_ * wet_l;
      out_r[i] = x + wet_ * wet_r;
    }
  }

  bool HasTails() const override { return true; }

 private:
  enum Line {
    kIn1, kIn2, kIn3, kIn4,  // input diffusers
    kAp1, kD1, kAp2, kD2,    // left half of the tank
    kAp3, kD3, kAp4, kD4,    // right half
    kLines
  };
  // Lengths in samples at the paper's reference rate.
  static float Length(int line) {
    static constexpr float kLength[kLines] = {142, 107,  379,  277,
                                              672, 4453, 1800, 3720,
                                              908, 4217, 2656, 3163};
    return kLength[line];
  }
  static constexpr float kLoopSamples = 21589.0f;  // sum of the eight above
  static constexpr float kRefRate = 29761.0f;
  static constexpr float kMaxSize = 1.5f;  // hall, relative to the plate
  static constexpr float kWetGain = 0.9f;

  float Allpass(int line, float length, float g, float x) {
    const float z = lines_[line].Read(length);
    const float w = x - g * z;
    lines_[line].Write(w);
    return z + g * w;
  }

  float Tap(int line, float position, float k) const {
    return lines_[line].Read(position * k);
  }

  float sr_ = 48000.0f, rate_scale_ = 1.6129f;
  dsp::DelayLine lines_[kLines];
  dsp::DelayLine predelay_;
  dsp::Svf low_cut_;
  dsp::OnePole bandwidth_, damp_l_, damp_r_;
  dsp::Lfo lfo_;
  float size_ = 1.0f, decay_ = 0.5f, diffusion2_ = 0.5f, excursion_ = 13.0f;
  float wet_ = 0.3f, predelay_samples_ = 1.0f;
};

}  // namespace fx
