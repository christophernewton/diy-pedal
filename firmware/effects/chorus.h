// Bass chorus with a crossover.
//
// Modulating the lowest notes makes a bass sound seasick and thin, so the
// signal is split first: everything below the crossover stays untouched and
// centred, only the part above it is chorused, in stereo.
//
//   Knob 1  Rate         0.05 .. 8 Hz
//   Knob 2  Depth
//   Knob 3  Mix
//   Knob 4  Crossover    40 .. 800 Hz. Turn it down to chorus nearly everything.
//   Knob 5  Feedback     adds the metallic sweep, strongest in flanger mode
//   Toggle 1  Mode: up flanger, middle chorus, down vibrato (no dry highs)
//   Toggle 2  Stereo: up wide, middle normal, down mono
//   Toggle 3  unused
#pragma once

#include "../dsp/common.h"
#include "effect.h"

namespace fx {

class Chorus final : public Effect {
 public:
  // Needs at least 40 ms of buffer.
  void Init(float sample_rate, float* buffer, size_t buffer_size) {
    sr_ = sample_rate;
    delay_.Init(buffer, buffer_size);
    slew_ = dsp::TimeCoef(0.050f, sr_);
    Reset();
  }

  void Reset() override {
    delay_.Reset();
    crossover_.Reset();
    lfo_.Reset();
    last_wet_ = 0.0f;
  }

  void Update(const Controls& c) override {
    lfo_.SetFreq(dsp::ExpMap(c.knob[0], 0.05f, 8.0f), sr_);
    const float depth = c.knob[1];
    mix_ = c.knob[2];
    crossover_.SetFreq(dsp::ExpMap(c.knob[3], 40.0f, 800.0f), sr_);
    feedback_ = c.knob[4] * 0.85f;

    const int mode = c.toggle[0];
    float centre_ms, swing_ms;
    if (mode == kUp) {  // flanger
      centre_ms = 3.0f;
      swing_ms = 2.6f;
    } else if (mode == kMiddle) {  // chorus
      centre_ms = 14.0f;
      swing_ms = 6.0f;
    } else {  // vibrato
      centre_ms = 6.0f;
      swing_ms = 3.0f;
    }
    centre_target_ = centre_ms * 0.001f * sr_;
    swing_target_ = swing_ms * 0.001f * sr_ * depth;

    if (mode == kDown) {
      dry_gain_ = 0.0f;
      wet_gain_ = 1.0f;
    } else {
      dry_gain_ = sqrtf(1.0f - mix_);
      wet_gain_ = sqrtf(mix_);
    }

    spread_ = c.toggle[1] == kUp ? 0.5f
              : (c.toggle[1] == kMiddle ? 0.25f : 0.0f);
  }

  void Process(const float* in, float* out_l, float* out_r,
               size_t size) override {
    for (size_t i = 0; i < size; ++i) {
      const float x = in[i];
      const float low = crossover_.Lp(x);
      const float high = x - low;

      // Slew the delay time so a mode change bends rather than clicks.
      centre_ += slew_ * (centre_target_ - centre_);
      swing_ += slew_ * (swing_target_ - swing_);

      delay_.Write(high + feedback_ * last_wet_);
      lfo_.Advance();
      const float wet_l = delay_.Read(centre_ + swing_ * lfo_.Sine());
      const float wet_r = delay_.Read(centre_ + swing_ * lfo_.Sine(spread_));
      last_wet_ = wet_l;

      out_l[i] = low + dry_gain_ * high + wet_gain_ * wet_l;
      out_r[i] = low + dry_gain_ * high + wet_gain_ * wet_r;
    }
  }

 private:
  float sr_ = 48000.0f;
  dsp::DelayLine delay_;
  dsp::OnePole crossover_;
  dsp::Lfo lfo_;
  float slew_ = 1.0f;
  float centre_ = 600.0f, centre_target_ = 600.0f;
  float swing_ = 0.0f, swing_target_ = 0.0f;
  float mix_ = 0.5f, dry_gain_ = 0.7f, wet_gain_ = 0.7f;
  float feedback_ = 0.0f, spread_ = 0.25f, last_wet_ = 0.0f;
};

}  // namespace fx
