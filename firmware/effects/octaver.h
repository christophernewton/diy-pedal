// Analogue-style octaver.
//
// The sub octaves are made the way the classic octave pedals do it: the
// fundamental is flipped in polarity every cycle (one octave down) or every
// second cycle (two octaves down). The subs therefore follow your picking
// dynamics and respond instantly. Play one note at a time.
//
//   Knob 1  Dry level
//   Knob 2  One octave down
//   Knob 3  Two octaves down
//   Knob 4  Sub tone     dark and round .. open and growly
//   Knob 5  Octave up    (full-wave rectified, a little gritty by nature)
//   Toggles unused
#pragma once

#include "../dsp/common.h"
#include "../dsp/pitch_tracker.h"
#include "effect.h"

namespace fx {

class Octaver final : public Effect {
 public:
  void Init(float sample_rate) {
    sr_ = sample_rate;
    tracker_.Init(sr_);
    up_dc_.SetFreq(30.0f, sr_);
    up_tone_.SetFreq(2500.0f, sr_);
    Reset();
  }

  void Reset() override {
    tracker_.Reset();
    tone1_.Reset();
    tone2_.Reset();
    up_dc_.Reset();
    up_tone_.Reset();
  }

  void Update(const Controls& c) override {
    dry_ = c.knob[0] * 1.2f;
    sub1_ = c.knob[1] * 2.0f;
    sub2_ = c.knob[2] * 2.0f;
    const float tone = dsp::ExpMap(c.knob[3], 70.0f, 1500.0f);
    tone1_.Set(tone, 0.7071f, sr_);
    tone2_.Set(tone, 0.7071f, sr_);
    up_ = c.knob[4] * 1.5f;
  }

  void Process(const float* in, float* out_l, float* out_r,
               size_t size) override {
    for (size_t i = 0; i < size; ++i) {
      const float x = in[i];
      const float fund = tracker_.Process(x);

      tone1_.Process(fund * tracker_.Div2());
      tone2_.Process(fund * tracker_.Div4());

      const float up = up_tone_.Lp(up_dc_.Hp(fabsf(x)));

      out_l[i] = out_r[i] =
          dry_ * x + sub1_ * tone1_.lp + sub2_ * tone2_.lp + up_ * up;
    }
  }

 private:
  float sr_ = 48000.0f;
  dsp::PitchTracker tracker_;
  dsp::Svf tone1_, tone2_;
  dsp::OnePole up_dc_, up_tone_;
  float dry_ = 1.0f, sub1_ = 1.0f, sub2_ = 0.0f, up_ = 0.0f;
};

}  // namespace fx
