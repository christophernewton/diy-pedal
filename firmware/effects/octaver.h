// Analogue-style octaver that follows the Boss OC-2 signal path.
//
// The OC-2 never plays a synthesised wave. It rectifies your own signal,
// flips its polarity every cycle to get one octave down, and runs the result
// through a slightly resonant third-order low-pass near 325 Hz. Two octaves
// down is that filtered voice flipped again and filtered near 153 Hz. The
// subs therefore follow your picking dynamics and respond instantly, and the
// fixed filters give the round, slightly synthy tone. Filter values are
// worked out from the component values of the OC-2 circuit. Play one note
// at a time.
//
//   Knob 1  Dry level
//   Knob 2  One octave down
//   Knob 3  Two octaves down
//   Knob 4  Sub tone     noon is the OC-2. Lower is deeper and smoother,
//                        higher lets more midrange growl through.
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
    pre_.SetFreq(700.0f, sr_);
    up_dc_.SetFreq(30.0f, sr_);
    up_tone_.SetFreq(2500.0f, sr_);
    Reset();
  }

  void Reset() override {
    tracker_.Reset();
    pre_.Reset();
    pole1_.Reset();
    pole2_.Reset();
    tone1_.Reset();
    tone2_.Reset();
    up_dc_.Reset();
    up_tone_.Reset();
  }

  void Update(const Controls& c) override {
    dry_ = c.knob[0] * 1.2f;
    sub1_ = c.knob[1] * kSub1Gain;
    sub2_ = c.knob[2] * kSub2Gain;
    // Each voice: a real pole plus a second-order section with Q 1.58.
    const float tone = dsp::ExpMap(c.knob[3], 0.4f, 3.0f);
    pole1_.SetFreq(329.0f * tone, sr_);
    tone1_.Set(324.0f * tone, 1.58f, sr_);
    pole2_.SetFreq(154.0f * tone, sr_);
    tone2_.Set(153.0f * tone, 1.58f, sr_);
    up_ = c.knob[4] * 1.5f;
  }

  void Process(const float* in, float* out_l, float* out_r,
               size_t size) override {
    for (size_t i = 0; i < size; ++i) {
      const float x = in[i];
      tracker_.Process(x);

      // Half-wave rectify, then flip polarity every cycle. The tracker's
      // dividers switch just after each positive half-cycle has ended, so
      // the flip lands where the rectified signal is at zero.
      const float band = pre_.Lp(x);
      const float rect = band > 0.0f ? band : 0.0f;
      tone1_.Process(pole1_.Lp(rect * tracker_.Div2()));
      tone2_.Process(pole2_.Lp(tone1_.lp * tracker_.Div4()));

      const float up = up_tone_.Lp(up_dc_.Hp(fabsf(x)));

      out_l[i] = out_r[i] =
          dry_ * x + sub1_ * tone1_.lp + sub2_ * tone2_.lp + up_ * up;
    }
  }

 private:
  float sr_ = 48000.0f;
  dsp::PitchTracker tracker_;
  dsp::Svf tone1_, tone2_;
  // Chosen so that with a knob at noon the voice is about as loud as the
  // note that made it.
  static constexpr float kSub1Gain = 4.0f;
  static constexpr float kSub2Gain = 4.0f;

  dsp::OnePole pre_, pole1_, pole2_, up_dc_, up_tone_;
  float dry_ = 1.0f, sub1_ = 1.0f, sub2_ = 0.0f, up_ = 0.0f;
};

}  // namespace fx
