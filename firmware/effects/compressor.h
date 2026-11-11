// Compressor for bass, with the time constants of the studio FET limiter
// (UREI 1176) that the best-regarded bass compressor pedals are built on,
// and the two additions those pedals make for bass: a high-pass in the
// detector and a parallel dry blend.
//
//   Knob 1  Threshold   -40 dB .. -4 dB
//   Knob 2  Ratio       2:1 .. 20:1 (noon is about 6:1)
//   Knob 3  Attack      0.1 ms .. 30 ms. Slower lets the pluck through.
//   Knob 4  Release     50 ms .. 1.1 s. Like the 1176, it recovers quickly
//                       after a single peak and up to three times more slowly
//                       after sustained compression, so it does not pump.
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
    sustain_coef_ = dsp::TimeCoef(0.400f, sr_);
    Reset();
  }

  void Reset() override {
    sidechain_.Reset();
    gr_db_ = 0.0f;
    sustain_db_ = 0.0f;
  }

  void Update(const Controls& c) override {
    threshold_db_ = -40.0f + 36.0f * c.knob[0];
    slope_ = 1.0f - 1.0f / dsp::ExpMap(c.knob[1], 2.0f, 20.0f);
    attack_ = dsp::TimeCoef(dsp::ExpMap(c.knob[2], 0.0001f, 0.030f), sr_);
    release_ = dsp::TimeCoef(dsp::ExpMap(c.knob[3], 0.050f, 1.100f), sr_);
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
      // The release stretches with how hard the compressor has been working
      // over the last few hundred milliseconds.
      sustain_db_ += sustain_coef_ * (gr_db_ - sustain_db_);
      const float release =
          release_ / (1.0f + 2.0f * dsp::Clamp(sustain_db_ * -0.1f, 0.0f, 1.0f));
      gr_db_ += (target < gr_db_ ? attack_ : release) * (target - gr_db_);

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
  float sustain_coef_ = 0.0f, sustain_db_ = 0.0f;
};

}  // namespace fx
