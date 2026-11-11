// Stereo phaser.
//
// A chain of first-order all-pass stages swept by a triangle LFO and mixed
// equally with the dry signal, which is how the MXR Phase 90 and EHX Small
// Stone (four stages) and the Mu-Tron Phasor II (six) work. For the Phase 90
// set four stages, mix at noon, centre at noon and depth around 2 o'clock;
// feedback at zero is the early "script" pedal and around 10 o'clock the
// later "block logo" one.
//
//   Knob 1  Rate         0.05 .. 8 Hz
//   Knob 2  Depth        how far the notches sweep
//   Knob 3  Feedback     sharper, more vocal notches
//   Knob 4  Centre       120 Hz .. 2.2 kHz. Higher keeps the low end steadier.
//   Knob 5  Mix          noon gives the deepest notches
//   Toggle 1  Stages: up 6, middle 4, down 8
//   Toggle 2  Stereo: up wide, middle normal, down mono
//   Toggle 3  unused
#pragma once

#include "../dsp/common.h"
#include "effect.h"

namespace fx {

class Phaser final : public Effect {
 public:
  void Init(float sample_rate) {
    sr_ = sample_rate;
    Reset();
  }

  void Reset() override {
    for (int ch = 0; ch < 2; ++ch) {
      for (int s = 0; s < kMaxStages; ++s) z_[ch][s] = 0.0f;
      last_[ch] = 0.0f;
    }
    lfo_.Reset();
  }

  void Update(const Controls& c) override {
    lfo_.SetFreq(dsp::ExpMap(c.knob[0], 0.05f, 8.0f), sr_);
    depth_octaves_ = c.knob[1] * 2.5f;
    feedback_ = c.knob[2] * 0.9f;
    centre_ = dsp::ExpMap(c.knob[3], 120.0f, 2200.0f);
    mix_ = c.knob[4];
    stages_ = c.toggle[0] == kUp ? 6 : (c.toggle[0] == kMiddle ? 4 : 8);
    spread_ = c.toggle[1] == kUp ? 0.5f
              : (c.toggle[1] == kMiddle ? 0.25f : 0.0f);
  }

  void Process(const float* in, float* out_l, float* out_r,
               size_t size) override {
    for (size_t i = 0; i < size; ++i) {
      const float x = in[i];
      lfo_.Advance();
      const float wet_l = Channel(0, x, lfo_.Triangle());
      const float wet_r = Channel(1, x, lfo_.Triangle(spread_));
      out_l[i] = x + mix_ * (wet_l - x);
      out_r[i] = x + mix_ * (wet_r - x);
    }
  }

 private:
  static constexpr int kMaxStages = 8;

  float Channel(int ch, float x, float lfo) {
    const float hz = dsp::Clamp(centre_ * powf(2.0f, depth_octaves_ * lfo),
                                20.0f, 0.4f * sr_);
    const float t = tanf(dsp::kPi * hz / sr_);
    const float a = (t - 1.0f) / (t + 1.0f);

    float y = x + feedback_ * last_[ch];
    for (int s = 0; s < stages_; ++s) {
      const float out = a * y + z_[ch][s];
      z_[ch][s] = y - a * out;
      y = out;
    }
    last_[ch] = y;
    return y;
  }

  float sr_ = 48000.0f;
  dsp::Lfo lfo_;
  float z_[2][kMaxStages] = {};
  float last_[2] = {0.0f, 0.0f};
  float depth_octaves_ = 1.5f, feedback_ = 0.3f, centre_ = 500.0f;
  float mix_ = 0.5f, spread_ = 0.25f;
  int stages_ = 4;
};

}  // namespace fx
