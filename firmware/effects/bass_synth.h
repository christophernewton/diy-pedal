// Monophonic bass synth driven by your playing.
//
// The pitch tracker reads the note, an oscillator plays it, and a resonant
// low-pass filter opens with your picking strength. Play one note at a time
// and mute cleanly between notes for the tightest tracking.
//
//   Knob 1  Cutoff       60 Hz .. 6 kHz
//   Knob 2  Resonance
//   Knob 3  Envelope     how far picking strength opens the filter
//   Knob 4  Sub          square wave one octave below the oscillator
//   Knob 5  Dry          clean bass mixed back in
//   Toggle 1  Wave: up saw, middle square, down narrow pulse
//   Toggle 2  Octave: up +1, middle as played, down -1
//   Toggle 3  Filter decay: up short, middle medium, down long
#pragma once

#include "../dsp/common.h"
#include "../dsp/pitch_tracker.h"
#include "effect.h"

namespace fx {

class BassSynth final : public Effect {
 public:
  void Init(float sample_rate) {
    sr_ = sample_rate;
    tracker_.Init(sr_);
    amp_env_.Set(0.003f, 0.120f, sr_);
    glide_ = dsp::TimeCoef(0.004f, sr_);
    amp_slew_ = dsp::TimeCoef(0.003f, sr_);
    Reset();
  }

  void Reset() override {
    tracker_.Reset();
    amp_env_.Reset();
    filter_env_.Reset();
    filter1_.Reset();
    filter2_.Reset();
    phase_ = sub_phase_ = 0.0f;
    freq_ = 0.0f;
    amp_ = 0.0f;
    tick_ = 0;
  }

  void Update(const Controls& c) override {
    cutoff_ = dsp::ExpMap(c.knob[0], 60.0f, 6000.0f);
    q_ = dsp::ExpMap(c.knob[1], 0.7f, 10.0f);
    env_octaves_ = c.knob[2] * 5.0f;
    sub_ = c.knob[3];
    dry_ = c.knob[4];

    wave_ = c.toggle[0];
    transpose_ = c.toggle[1] == kUp ? 2.0f
                 : (c.toggle[1] == kDown ? 0.5f : 1.0f);
    const float decay = c.toggle[2] == kUp ? 0.080f
                        : (c.toggle[2] == kMiddle ? 0.250f : 0.700f);
    filter_env_.Set(0.002f, decay, sr_);
  }

  void Process(const float* in, float* out_l, float* out_r,
               size_t size) override {
    for (size_t i = 0; i < size; ++i) {
      const float x = in[i];
      tracker_.Process(x);

      // Follow the tracked note. Jump on the first note, glide after that
      // so small tracking wobbles do not sound like steps.
      if (tracker_.HasPitch()) {
        const float target = tracker_.Frequency() * transpose_;
        freq_ = freq_ <= 0.0f ? target : freq_ + glide_ * (target - freq_);
      }

      // Loudness follows the bass, compressed so quiet notes still speak.
      const float level = amp_env_.Process(x);
      const bool gate = tracker_.HasPitch() && level > kGate;
      float amp_target = gate ? sqrtf(level * 4.0f) : 0.0f;
      if (amp_target > 1.0f) amp_target = 1.0f;
      amp_ += amp_slew_ * (amp_target - amp_);

      const float fenv = dsp::Clamp(filter_env_.Process(x) * 5.0f, 0.0f, 1.0f);
      if (++tick_ >= 8) {
        tick_ = 0;
        const float hz =
            dsp::Clamp(cutoff_ * powf(2.0f, env_octaves_ * fenv), 30.0f,
                       12000.0f);
        filter1_.Set(hz, q_, sr_);
        filter2_.Set(hz, 0.6f, sr_);
      }

      const float dt = freq_ / sr_;
      float osc = 0.0f, sub = 0.0f;
      if (dt > 0.0f) {
        phase_ += dt;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        sub_phase_ += dt * 0.5f;
        if (sub_phase_ >= 1.0f) sub_phase_ -= 1.0f;

        if (wave_ == kUp) {
          osc = 2.0f * phase_ - 1.0f - PolyBlep(phase_, dt);
        } else {
          osc = Pulse(phase_, dt, wave_ == kMiddle ? 0.5f : 0.2f);
        }
        sub = Pulse(sub_phase_, dt * 0.5f, 0.5f);
      }

      filter1_.Process((osc + sub_ * sub) * amp_ * 0.35f);
      filter2_.Process(filter1_.lp);

      out_l[i] = out_r[i] = filter2_.lp + dry_ * x;
    }
  }

 private:
  static constexpr float kGate = 0.004f;

  // Rounds off the oscillator's edges so they do not alias.
  static float PolyBlep(float t, float dt) {
    if (t < dt) {
      t /= dt;
      return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
      t = (t - 1.0f) / dt;
      return t * t + t + t + 1.0f;
    }
    return 0.0f;
  }

  static float Pulse(float phase, float dt, float width) {
    float v = phase < width ? 1.0f : -1.0f;
    v += PolyBlep(phase, dt);
    float falling = phase + 1.0f - width;
    if (falling >= 1.0f) falling -= 1.0f;
    v -= PolyBlep(falling, dt);
    return v;
  }

  float sr_ = 48000.0f;
  dsp::PitchTracker tracker_;
  dsp::EnvFollower amp_env_, filter_env_;
  dsp::Svf filter1_, filter2_;
  float glide_ = 1.0f, amp_slew_ = 1.0f;
  float cutoff_ = 400.0f, q_ = 2.0f, env_octaves_ = 2.0f;
  float sub_ = 0.5f, dry_ = 0.0f, transpose_ = 1.0f;
  int wave_ = kMiddle;
  float phase_ = 0.0f, sub_phase_ = 0.0f, freq_ = 0.0f, amp_ = 0.0f;
  int tick_ = 0;
};

}  // namespace fx
