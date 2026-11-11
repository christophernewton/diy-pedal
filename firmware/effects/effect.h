// Common interface for every effect in the pedal.
#pragma once

#include <cstddef>

namespace fx {

// Toggle switch positions as reported by the Hothouse.
enum Toggle { kUp = 0, kMiddle = 1, kDown = 2 };

// What the player has dialled in. Knob 6 is the global output level and is
// handled in main.cpp, so effects see the first five knobs only.
struct Controls {
  float knob[5] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f};  // each 0..1
  int toggle[3] = {kMiddle, kMiddle, kMiddle};
};

class Effect {
 public:
  // Clears filters, delay memory and envelopes. Called when the effect is
  // switched to or re-engaged, so nothing stale leaks into the output.
  virtual void Reset() = 0;

  // Called once per audio block with the current control positions.
  virtual void Update(const Controls& c) = 0;

  // Mono in, stereo out.
  virtual void Process(const float* in, float* out_l, float* out_r,
                       size_t size) = 0;

  // Effects with tails keep running in bypass so the sound rings out.
  virtual bool HasTails() const { return false; }

 protected:
  ~Effect() = default;
};

}  // namespace fx
