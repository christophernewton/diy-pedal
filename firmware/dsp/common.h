// Small DSP building blocks shared by the effects.
//
// Nothing in here touches the hardware, so the same code compiles for the
// Daisy and for the offline tests in tests/.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstring>

namespace dsp {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float Clamp(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

inline float DbToLin(float db) { return powf(10.0f, db * 0.05f); }

inline float LinToDb(float x) { return 20.0f * log10f(x < 1e-6f ? 1e-6f : x); }

// Maps a 0..1 knob onto [lo, hi] exponentially, which suits frequencies and
// times.
inline float ExpMap(float knob, float lo, float hi) {
  return lo * powf(hi / lo, Clamp(knob, 0.0f, 1.0f));
}

// One-pole smoothing coefficient for a time constant in seconds.
inline float TimeCoef(float seconds, float sample_rate) {
  return 1.0f - expf(-1.0f / (seconds * sample_rate));
}

// Transparent below the threshold, rounds off smoothly above it, never
// exceeds +/-1. Keeps a hot active bass from hitting hard digital clipping.
inline float SoftClip(float x) {
  constexpr float t = 0.75f;
  const float a = fabsf(x);
  if (a <= t) return x;
  const float y = t + (1.0f - t) * tanhf((a - t) / (1.0f - t));
  return x < 0.0f ? -y : y;
}

class OnePole {
 public:
  void SetFreq(float hz, float sample_rate) {
    a_ = 1.0f - expf(-kTwoPi * hz / sample_rate);
  }
  float Lp(float x) {
    z_ += a_ * (x - z_);
    return z_;
  }
  float Hp(float x) { return x - Lp(x); }
  void Reset() { z_ = 0.0f; }

 private:
  float a_ = 1.0f;
  float z_ = 0.0f;
};

// State variable filter in the trapezoidal (TPT) form. It stays stable when
// the cutoff is swept every sample, which the envelope filter and synth do.
class Svf {
 public:
  void Set(float hz, float q, float sample_rate) {
    hz = Clamp(hz, 10.0f, 0.45f * sample_rate);
    const float g = tanf(kPi * hz / sample_rate);
    k_ = 1.0f / q;
    a1_ = 1.0f / (1.0f + g * (g + k_));
    a2_ = g * a1_;
    a3_ = g * a2_;
  }
  void Process(float x) {
    const float v3 = x - ic2_;
    const float v1 = a1_ * ic1_ + a2_ * v3;
    const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
    ic1_ = 2.0f * v1 - ic1_;
    ic2_ = 2.0f * v2 - ic2_;
    lp = v2;
    bp = v1;
    hp = x - k_ * v1 - v2;
  }
  void Reset() { ic1_ = ic2_ = lp = bp = hp = 0.0f; }

  float lp = 0.0f, bp = 0.0f, hp = 0.0f;

 private:
  float k_ = 1.0f, a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
  float ic1_ = 0.0f, ic2_ = 0.0f;
};

// Four-pole (24 dB/octave) transistor-ladder style low-pass, the filter of
// the Minimoog and Taurus. Zero-delay-feedback form, so it stays in tune and
// stable while the cutoff is swept. Resonance runs 0..4; 4 is the edge of
// self-oscillation.
class Ladder {
 public:
  void Set(float hz, float resonance, float sample_rate) {
    hz = Clamp(hz, 10.0f, 0.45f * sample_rate);
    const float g = tanf(kPi * hz / sample_rate);
    g_ = g / (1.0f + g);
    k_ = resonance;
  }
  float Process(float x) {
    const float g2 = g_ * g_;
    const float feedback =
        (g2 * g_ * s_[0] + g2 * s_[1] + g_ * s_[2] + s_[3]) * (1.0f - g_);
    // Soft clipping inside the loop keeps a high-resonance peak in check,
    // as the transistors do in the real circuit.
    float u = SoftClip((x - k_ * feedback) / (1.0f + k_ * g2 * g2));
    for (float& s : s_) {
      const float v = (u - s) * g_;
      u = v + s;
      s = u + v;
    }
    return u;
  }
  void Reset() { s_[0] = s_[1] = s_[2] = s_[3] = 0.0f; }

 private:
  float g_ = 0.0f, k_ = 0.0f;
  float s_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

// Peak envelope follower with separate attack and release times.
class EnvFollower {
 public:
  void Set(float attack_s, float release_s, float sample_rate) {
    att_ = TimeCoef(attack_s, sample_rate);
    rel_ = TimeCoef(release_s, sample_rate);
  }
  float Process(float x) {
    const float a = fabsf(x);
    env_ += (a > env_ ? att_ : rel_) * (a - env_);
    return env_;
  }
  float Value() const { return env_; }
  void Reset() { env_ = 0.0f; }

 private:
  float att_ = 1.0f, rel_ = 1.0f, env_ = 0.0f;
};

// LFO. Phase runs 0..1 so a second output can be read at an offset.
class Lfo {
 public:
  void SetFreq(float hz, float sample_rate) { inc_ = hz / sample_rate; }
  void Advance() {
    phase_ += inc_;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
  }
  float Sine(float offset = 0.0f) const {
    return sinf(kTwoPi * (phase_ + offset));
  }
  // Triangle, -1..1, as the classic chorus and phaser pedals use.
  float Triangle(float offset = 0.0f) const {
    float p = phase_ + offset;
    if (p >= 1.0f) p -= 1.0f;
    return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
  }
  void Reset() { phase_ = 0.0f; }

 private:
  float phase_ = 0.0f, inc_ = 0.0f;
};

// Delay line over memory owned by the caller. On the Daisy the big buffers
// live in SDRAM, which only main.cpp knows how to place.
class DelayLine {
 public:
  void Init(float* buffer, size_t size) {
    buf_ = buffer;
    size_ = size;
    Reset();
  }
  void Reset() {
    memset(buf_, 0, size_ * sizeof(float));
    write_ = 0;
  }
  void Write(float x) {
    buf_[write_] = x;
    if (++write_ >= size_) write_ = 0;
  }
  // Reads `delay` samples behind the write head, linearly interpolated.
  float Read(float delay) const {
    delay = Clamp(delay, 1.0f, static_cast<float>(size_ - 2));
    const size_t whole = static_cast<size_t>(delay);
    const float frac = delay - static_cast<float>(whole);
    size_t i0 = write_ + size_ - whole;
    if (i0 >= size_) i0 -= size_;
    const size_t i1 = (i0 == 0) ? size_ - 1 : i0 - 1;
    return buf_[i0] + frac * (buf_[i1] - buf_[i0]);
  }

 private:
  float* buf_ = nullptr;
  size_t size_ = 0;
  size_t write_ = 0;
};

}  // namespace dsp
