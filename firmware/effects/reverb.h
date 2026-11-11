// Stereo reverb that keeps the low end tight.
//
// Only the part of the signal above the low cut is sent into the reverb, so
// the fundamental stays dry and punchy while the upper harmonics bloom. The
// tail keeps ringing after you bypass the pedal.
//
//   Knob 1  Decay
//   Knob 2  Tone         dark .. bright
//   Knob 3  Mix          reverb level; the dry signal stays at full level
//   Knob 4  Low cut      20 .. 800 Hz, applied to what feeds the reverb
//   Knob 5  Pre-delay    0 .. 200 ms
//   Toggle 1  Size: up hall (very long), middle plate, down room (short)
//   Toggles 2 and 3 unused
#pragma once

#include "../dsp/common.h"
#include "Effects/reverbsc.h"
#include "effect.h"

namespace fx {

class Reverb final : public Effect {
 public:
  // The pre-delay buffer needs at least 200 ms plus a few samples.
  void Init(float sample_rate, daisysp::ReverbSc* verb, float* predelay_buffer,
            size_t predelay_size) {
    sr_ = sample_rate;
    verb_ = verb;
    predelay_.Init(predelay_buffer, predelay_size);
    Reset();
  }

  void Reset() override {
    verb_->Init(sr_);
    predelay_.Reset();
    low_cut_.Reset();
  }

  void Update(const Controls& c) override {
    float lo, hi;
    if (c.toggle[0] == kUp) {
      lo = 0.85f;
      hi = 0.995f;
    } else if (c.toggle[0] == kMiddle) {
      lo = 0.60f;
      hi = 0.96f;
    } else {
      lo = 0.30f;
      hi = 0.80f;
    }
    verb_->SetFeedback(lo + (hi - lo) * c.knob[0]);
    verb_->SetLpFreq(dsp::ExpMap(c.knob[1], 800.0f, 16000.0f));
    wet_ = c.knob[2] * c.knob[2] * 1.5f;
    low_cut_.SetFreq(dsp::ExpMap(c.knob[3], 20.0f, 800.0f), sr_);
    predelay_samples_ = 1.0f + c.knob[4] * 0.200f * sr_;
  }

  void Process(const float* in, float* out_l, float* out_r,
               size_t size) override {
    for (size_t i = 0; i < size; ++i) {
      const float x = in[i];
      predelay_.Write(low_cut_.Hp(x));
      const float send = predelay_.Read(predelay_samples_);

      float wet_l, wet_r;
      verb_->Process(send, send, &wet_l, &wet_r);

      out_l[i] = x + wet_ * wet_l;
      out_r[i] = x + wet_ * wet_r;
    }
  }

  bool HasTails() const override { return true; }

 private:
  float sr_ = 48000.0f;
  daisysp::ReverbSc* verb_ = nullptr;
  dsp::DelayLine predelay_;
  dsp::OnePole low_cut_;
  float wet_ = 0.3f, predelay_samples_ = 1.0f;
};

}  // namespace fx
