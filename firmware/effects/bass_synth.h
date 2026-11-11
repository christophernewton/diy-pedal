// Monophonic bass synth driven by your playing.
//
// The pitch tracker reads the note and oscillators play it through a
// four-pole ladder low-pass, the recipe behind the Minimoog and Taurus bass
// sounds that bass synth pedals chase. Each pluck fires a filter envelope
// that opens with your picking strength and then closes at its own pace,
// like a synth's decay stage, while loudness keeps following the string.
// Play one note at a time and mute cleanly between notes for the tightest
// tracking.
//
//   Knob 1  Cutoff       40 Hz .. 5 kHz resting cutoff. It also rises half
//                        as fast as the note, so high notes do not go dull.
//   Knob 2  Resonance    up to just short of self-oscillation
//   Knob 3  Envelope     how far a pluck opens the filter, 0 .. 5 octaves
//   Knob 4  Sub          square wave one octave below the oscillator
//   Knob 5  Dry          clean bass mixed back in
//   Toggle 1  Wave: up two saws detuned 10 cents apart (fat, slowly moving),
//             middle square, down narrow pulse
//   Toggle 2  Octave: up +1, middle as played, down -1
//   Toggle 3  Filter decay: up short (60 ms), middle medium (180 ms),
//             down long (500 ms)
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
    fenv_attack_ = dsp::TimeCoef(0.001f, sr_);
    Reset();
  }

  void Reset() override {
    tracker_.Reset();
    amp_env_.Reset();
    filter_.Reset();
    phase_ = phase2_ = sub_phase_ = 0.0f;
    freq_ = 0.0f;
    amp_ = 0.0f;
    fenv_ = peak_ = 0.0f;
    attack_left_ = waiting_ = 0;
    tick_ = 0;
  }

  void Update(const Controls& c) override {
    cutoff_ = dsp::ExpMap(c.knob[0], 40.0f, 5000.0f);
    resonance_ = c.knob[1] * 3.8f;
    env_octaves_ = c.knob[2] * 5.0f;
    sub_ = c.knob[3];
    dry_ = c.knob[4];

    wave_ = c.toggle[0];
    transpose_ = c.toggle[1] == kUp ? 2.0f
                 : (c.toggle[1] == kDown ? 0.5f : 1.0f);
    const float decay = c.toggle[2] == kUp ? 0.060f
                        : (c.toggle[2] == kMiddle ? 0.180f : 0.500f);
    fenv_decay_ = dsp::TimeCoef(decay, sr_);
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

      // Filter envelope. A pluck is measured while the tracker finds the
      // note; when the oscillator starts, the envelope jumps to the picking
      // strength and then decays on its own.
      if (tracker_.Onset()) {
        waiting_ = static_cast<int>(0.150f * sr_);
        peak_ = 0.0f;
      }
      if (waiting_ > 0) {
        --waiting_;
        const float strength = dsp::Clamp(level * 5.0f, 0.0f, 1.0f);
        if (strength > peak_) peak_ = strength;
        if (tracker_.HasPitch()) {
          waiting_ = 0;
          attack_left_ = static_cast<int>(0.004f * sr_);
        }
      }
      if (attack_left_ > 0) {
        --attack_left_;
        if (peak_ > fenv_) fenv_ += fenv_attack_ * (peak_ - fenv_);
      } else {
        fenv_ -= fenv_decay_ * fenv_;
      }

      if (++tick_ >= 8) {
        tick_ = 0;
        const float track =
            freq_ > 0.0f ? dsp::Clamp(sqrtf(freq_ / 82.4f), 0.5f, 2.5f) : 1.0f;
        filter_.Set(dsp::Clamp(cutoff_ * track * powf(2.0f, env_octaves_ * fenv_),
                               30.0f, 12000.0f),
                    resonance_, sr_);
      }

      const float dt = freq_ / sr_;
      float osc = 0.0f, sub = 0.0f;
      if (dt > 0.0f) {
        sub_phase_ = Wrap(sub_phase_ + dt * 0.5f);
        if (wave_ == kUp) {
          const float dt1 = dt * kDetuneUp, dt2 = dt * kDetuneDown;
          phase_ = Wrap(phase_ + dt1);
          phase2_ = Wrap(phase2_ + dt2);
          osc = 0.7f * Saw(phase_, dt1) + 0.35f * Saw(phase2_, dt2);
        } else {
          phase_ = Wrap(phase_ + dt);
          osc = Pulse(phase_, dt, wave_ == kMiddle ? 0.5f : 0.2f);
        }
        sub = Pulse(sub_phase_, dt * 0.5f, 0.5f);
      }

      // Ladder resonance thins the low end; give some of it back.
      const float drive = 0.35f * (1.0f + 0.5f * resonance_);
      const float wet = filter_.Process((osc + sub_ * sub) * amp_ * drive);

      out_l[i] = out_r[i] = wet + dry_ * x;
    }
  }

 private:
  static constexpr float kGate = 0.004f;
  static constexpr float kDetuneUp = 1.00289f;    // +5 cents
  static constexpr float kDetuneDown = 0.99712f;  // -5 cents

  static float Wrap(float phase) {
    return phase >= 1.0f ? phase - 1.0f : phase;
  }

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

  static float Saw(float phase, float dt) {
    return 2.0f * phase - 1.0f - PolyBlep(phase, dt);
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
  dsp::EnvFollower amp_env_;
  dsp::Ladder filter_;
  float glide_ = 1.0f, amp_slew_ = 1.0f;
  float fenv_attack_ = 1.0f, fenv_decay_ = 0.001f, fenv_ = 0.0f;
  float peak_ = 0.0f;
  int attack_left_ = 0, waiting_ = 0;
  float cutoff_ = 400.0f, resonance_ = 1.0f, env_octaves_ = 2.0f;
  float sub_ = 0.5f, dry_ = 0.0f, transpose_ = 1.0f;
  int wave_ = kMiddle;
  float phase_ = 0.0f, phase2_ = 0.0f, sub_phase_ = 0.0f;
  float freq_ = 0.0f, amp_ = 0.0f;
  int tick_ = 0;
};

}  // namespace fx
