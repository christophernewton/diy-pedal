// Envelope filter (auto-wah). Dig in and the filter sweeps.
//
//   Knob 1  Sensitivity  set it so a hard pluck just reaches the top
//   Knob 2  Frequency    where the sweep starts, 60 Hz .. 1.2 kHz
//   Knob 3  Resonance
//   Knob 4  Decay        how quickly the filter falls back, 40 .. 600 ms
//   Knob 5  Mix          dry .. filtered. Keep some dry for a solid low end.
//   Toggle 1  Filter: up low-pass, middle band-pass, down high-pass
//   Toggle 2  Sweep: up wide and upward, middle gentle and upward,
//             down downward
//   Toggle 3  unused
#pragma once

#include "../dsp/common.h"
#include "effect.h"

namespace fx {

class EnvelopeFilter final : public Effect {
 public:
  void Init(float sample_rate) {
    sr_ = sample_rate;
    Reset();
  }

  void Reset() override {
    env_.Reset();
    filter_.Reset();
  }

  void Update(const Controls& c) override {
    sensitivity_ = dsp::ExpMap(c.knob[0], 1.0f, 40.0f);
    base_ = dsp::ExpMap(c.knob[1], 60.0f, 1200.0f);
    q_ = dsp::ExpMap(c.knob[2], 0.7f, 8.0f);
    env_.Set(0.003f, dsp::ExpMap(c.knob[3], 0.040f, 0.600f), sr_);
    mix_ = c.knob[4];
    type_ = c.toggle[0];
    sweep_ = c.toggle[1];
    // A resonant peak is loud; pull the filtered signal back as Q rises.
    comp_ = 1.0f / sqrtf(q_ / 0.7f);
  }

  void Process(const float* in, float* out_l, float* out_r,
               size_t size) override {
    for (size_t i = 0; i < size; ++i) {
      const float x = in[i];
      const float e = dsp::Clamp(env_.Process(x) * sensitivity_, 0.0f, 1.0f);

      float hz;
      if (sweep_ == kUp) {
        hz = base_ * powf(2.0f, 4.0f * e);
      } else if (sweep_ == kMiddle) {
        hz = base_ * powf(2.0f, 2.5f * e);
      } else {
        hz = base_ * 8.0f * powf(2.0f, -3.0f * e);
      }
      filter_.Set(dsp::Clamp(hz, 30.0f, 10000.0f), q_, sr_);
      filter_.Process(x);

      const float wet = (type_ == kUp ? filter_.lp
                         : (type_ == kMiddle ? filter_.bp : filter_.hp)) *
                        comp_;
      out_l[i] = out_r[i] = x + mix_ * (wet - x);
    }
  }

 private:
  float sr_ = 48000.0f;
  dsp::EnvFollower env_;
  dsp::Svf filter_;
  float sensitivity_ = 8.0f, base_ = 200.0f, q_ = 3.0f, mix_ = 0.7f;
  float comp_ = 1.0f;
  int type_ = kUp, sweep_ = kUp;
};

}  // namespace fx
