// Compressor for bass.
//
//   Knob 1  Threshold   -40 dB .. -4 dB
//   Knob 2  Ratio       1.5:1 .. 20:1
//   Knob 3  Attack      1 ms .. 100 ms
//   Knob 4  Release     30 ms .. 800 ms
//   Knob 5  Blend       dry .. fully compressed (parallel compression)
//   Toggle 1  Sidechain high-pass: up 150 Hz, middle off, down 80 Hz.
//             Stops the low strings from pumping the whole signal.
//   Toggle 2  Make-up gain: up +12 dB, middle +6 dB, down 0 dB
//   Toggle 3  unused
#pragma once

#include "../dsp/common.h"
#include "effect.h"

namespace fx {

class Compressor final : public Effect {
 public:
  void Init(float sample_rate) {
    sr_ = sample_rate;
    Reset();
  }

  void Reset() override {
    sidechain_.Reset();
    gr_db_ = 0.0f;
  }

  void Update(const Controls& c) override {
    threshold_db_ = -40.0f + 36.0f * c.knob[0];
    slope_ = 1.0f - 1.0f / dsp::ExpMap(c.knob[1], 1.5f, 20.0f);
    attack_ = dsp::TimeCoef(dsp::ExpMap(c.knob[2], 0.001f, 0.100f), sr_);
    release_ = dsp::TimeCoef(dsp::ExpMap(c.knob[3], 0.030f, 0.800f), sr_);
    blend_ = c.knob[4];

    const float hp = c.toggle[0] == kUp ? 150.0f
                     : (c.toggle[0] == kDown ? 80.0f : 15.0f);
    sidechain_.SetFreq(hp, sr_);

    makeup_db_ = c.toggle[1] == kUp ? 12.0f
                 : (c.toggle[1] == kMiddle ? 6.0f : 0.0f);
  }

  void Process(const float* in, float* out_l, float* out_r,
               size_t size) override {
    for (size_t i = 0; i < size; ++i) {
      const float x = in[i];
      const float level_db = dsp::LinToDb(fabsf(sidechain_.Hp(x)));

      // Gain computer with a soft knee.
      const float over = level_db - threshold_db_;
      float target;
      if (over <= -kKneeDb * 0.5f) {
        target = 0.0f;
      } else if (over >= kKneeDb * 0.5f) {
        target = -slope_ * over;
      } else {
        const float t = over + kKneeDb * 0.5f;
        target = -slope_ * t * t / (2.0f * kKneeDb);
      }

      // More reduction follows the attack time, recovery the release time.
      gr_db_ += (target < gr_db_ ? attack_ : release_) * (target - gr_db_);

      const float wet = x * dsp::DbToLin(gr_db_ + makeup_db_);
      out_l[i] = out_r[i] = x + blend_ * (wet - x);
    }
  }

  // Current gain reduction in dB, zero or negative.
  float GainReductionDb() const { return gr_db_; }

 private:
  static constexpr float kKneeDb = 6.0f;

  float sr_ = 48000.0f;
  dsp::OnePole sidechain_;
  float threshold_db_ = -20.0f, slope_ = 0.75f;
  float attack_ = 0.01f, release_ = 0.001f;
  float blend_ = 1.0f, makeup_db_ = 0.0f;
  float gr_db_ = 0.0f;
};

}  // namespace fx
