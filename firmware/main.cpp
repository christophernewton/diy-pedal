// Bass multi-effect for the Cleveland Music Co. Hothouse (Daisy Seed).
//
// One effect runs at a time.
//
//   Footswitch 1   tap: next effect      hold: previous effect
//   Footswitch 2   effect on / off
//   Both, 2 s      reboot into DFU mode for flashing
//   LED 1          blinks the number of the selected effect
//   LED 2          lit while the effect is on
//   Knobs 1-5      belong to the selected effect (see effects/*.h)
//   Knob 6         output level of the effect, unity at noon
//   Toggles 1-3    belong to the selected effect
//
// The selected effect and whether it is on are remembered across power
// cycles.

#include "daisy_seed.h"
#include "hothouse.h"
#include "util/PersistentStorage.h"

#include "effects/bass_synth.h"
#include "effects/chorus.h"
#include "effects/compressor.h"
#include "effects/envelope_filter.h"
#include "effects/octaver.h"
#include "effects/phaser.h"
#include "effects/reverb.h"

using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::Led;
using daisy::System;

namespace {

constexpr size_t kBlockSize = 48;
constexpr size_t kChorusBufferSize = 2400;     // 50 ms at 48 kHz
constexpr size_t kPredelayBufferSize = 10000;  // just over 200 ms
constexpr uint32_t kHoldMs = 600;              // footswitch 1 hold time
constexpr uint32_t kSaveDelayMs = 2000;        // settle time before saving

// What survives a power cycle. Stored in the Seed's QSPI flash, which this
// firmware does not otherwise use.
struct Settings {
  uint32_t layout;  // changes if this struct ever does, so old data is ignored
  int32_t effect;
  bool active;

  bool operator!=(const Settings& other) const {
    return layout != other.layout || effect != other.effect ||
           active != other.active;
  }
};
constexpr uint32_t kSettingsLayout = 0xBA550001;

Hothouse hw;
Led led_effect, led_active;
daisy::PersistentStorage<Settings> storage(hw.seed.qspi);

// Large buffers go in the Seed's SDRAM.
float DSY_SDRAM_BSS chorus_buffer[kChorusBufferSize];
float DSY_SDRAM_BSS predelay_buffer[kPredelayBufferSize];
float DSY_SDRAM_BSS reverb_tank[fx::Reverb::kTankSize];

fx::Compressor compressor;
fx::Octaver octaver;
fx::BassSynth bass_synth;
fx::EnvelopeFilter envelope_filter;
fx::Chorus chorus;
fx::Phaser phaser;
fx::Reverb reverb;

// The order here is the order footswitch 1 steps through, and the number
// LED 1 blinks.
fx::Effect* const effects[] = {
    &compressor, &octaver, &bass_synth, &envelope_filter,
    &chorus,     &phaser,  &reverb,
};
constexpr int kNumEffects = sizeof(effects) / sizeof(effects[0]);

int current = 0;      // effect being heard
int pending = -1;     // effect to switch to once the fade-out completes
volatile int selected = 0;      // effect the player has chosen
volatile bool active = false;   // false = bypassed
bool was_running = false;
bool fs1_armed = false;

float engage = 0.0f;  // 0 = dry only, 1 = effect, ramps to avoid clicks
float fade = 1.0f;    // dips to 0 around an effect change
float ramp_step = 0.0f;

volatile int blink_request = 1;  // main loop blinks LED 1 this many times

float send[kBlockSize], wet_l[kBlockSize], wet_r[kBlockSize];

fx::Controls ReadControls() {
  fx::Controls c;
  c.knob[0] = hw.GetKnobValue(Hothouse::KNOB_1);
  c.knob[1] = hw.GetKnobValue(Hothouse::KNOB_2);
  c.knob[2] = hw.GetKnobValue(Hothouse::KNOB_3);
  c.knob[3] = hw.GetKnobValue(Hothouse::KNOB_4);
  c.knob[4] = hw.GetKnobValue(Hothouse::KNOB_5);
  c.toggle[0] = hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_1);
  c.toggle[1] = hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2);
  c.toggle[2] = hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_3);
  return c;
}

void SelectEffect(int index) {
  pending = (index % kNumEffects + kNumEffects) % kNumEffects;
  selected = pending;
  blink_request = pending + 1;
}

void HandleFootswitches() {
  const daisy::Switch& fs1 = hw.switches[Hothouse::FOOTSWITCH_1];
  const daisy::Switch& fs2 = hw.switches[Hothouse::FOOTSWITCH_2];

  // Footswitch 2 acts on the press so bypass feels immediate.
  if (fs2.RisingEdge() && !fs1.Pressed()) active = !active;

  // Footswitch 1 acts on release (tap) or after a hold, never both.
  // Pressing both switches is the DFU gesture, so it cancels the action.
  if (fs1.RisingEdge()) fs1_armed = true;
  if (fs2.Pressed()) fs1_armed = false;
  if (fs1_armed && fs1.Pressed() && fs1.TimeHeldMs() >= kHoldMs) {
    fs1_armed = false;
    SelectEffect((pending >= 0 ? pending : current) - 1);
  }
  if (fs1_armed && fs1.FallingEdge()) {
    fs1_armed = false;
    SelectEffect((pending >= 0 ? pending : current) + 1);
  }
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  hw.ProcessAllControls();
  HandleFootswitches();

  const fx::Controls controls = ReadControls();
  const float knob6 = hw.GetKnobValue(Hothouse::KNOB_6);
  const float level = knob6 * knob6 * 4.0f;  // unity at noon, +12 dB max

  // Nothing audible is running while bypassed, so switch straight away.
  // The exception is an effect whose tail may still be ringing: that one
  // takes the fade below.
  if (pending >= 0 && engage <= 0.0f && !effects[current]->HasTails()) {
    current = pending;
    pending = -1;
    effects[current]->Reset();
    fade = 1.0f;
  }

  fx::Effect* effect = effects[current];
  const bool tails = effect->HasTails();
  const bool running = active || engage > 0.0f || tails;

  // Coming back from a full stop: start from a clean state.
  if (running && !was_running) effect->Reset();
  was_running = running;

  if (!running) {
    for (size_t i = 0; i < size; ++i) out[0][i] = out[1][i] = in[0][i];
    return;
  }

  // Ramp the bypass crossfade and feed the effect. An effect with tails is
  // fed a fading input instead, so what is already inside it rings out.
  float dry_gain[kBlockSize];
  for (size_t i = 0; i < size; ++i) {
    const float target = active ? 1.0f : 0.0f;
    if (engage < target) {
      engage = engage + ramp_step > target ? target : engage + ramp_step;
    } else if (engage > target) {
      engage = engage - ramp_step < target ? target : engage - ramp_step;
    }
    dry_gain[i] = 1.0f - engage;
    send[i] = tails ? in[0][i] * engage : in[0][i];
  }

  effect->Update(controls);
  effect->Process(send, wet_l, wet_r, size);

  for (size_t i = 0; i < size; ++i) {
    // Fade out, swap effects at silence, fade back in.
    if (pending >= 0) {
      fade -= ramp_step;
      if (fade <= 0.0f) {
        fade = 0.0f;
        current = pending;
        pending = -1;
        effects[current]->Reset();
        // The rest of this block still holds the old effect's audio, so
        // keep it silent; the new effect starts on the next block.
        for (size_t j = i; j < size; ++j) {
          out[0][j] = out[1][j] = in[0][j] * dry_gain[j];
        }
        return;
      }
    } else if (fade < 1.0f) {
      fade = fade + ramp_step > 1.0f ? 1.0f : fade + ramp_step;
    }

    const float wet_gain = (tails ? 1.0f : engage) * fade * level;
    const float dry = in[0][i] * dry_gain[i];
    out[0][i] = dsp::SoftClip(wet_l[i] * wet_gain + dry);
    out[1][i] = dsp::SoftClip(wet_r[i] * wet_gain + dry);
  }
}

// Blinks LED 1 `count` times, then leaves it off. Called every few ms.
void UpdateEffectLed() {
  static int count = 0;
  static uint32_t started = 0;

  const int request = blink_request;
  if (request > 0) {
    blink_request = 0;
    count = request;
    started = System::GetNow();
  }

  bool on = false;
  if (count > 0) {
    const uint32_t elapsed = System::GetNow() - started;
    const uint32_t slot = elapsed / 300;
    if (slot >= static_cast<uint32_t>(count)) {
      count = 0;
    } else {
      on = (elapsed % 300) < 130;
    }
  }
  led_effect.Set(on ? 1.0f : 0.0f);
  led_effect.Update();
}

// Restores the effect and on/off state from the last session.
void LoadSettings() {
  const Settings defaults = {kSettingsLayout, 0, false};
  storage.Init(defaults);
  const Settings& saved = storage.GetSettings();
  // Flash that has never held our settings, or held another firmware's,
  // can contain anything, so only trust values that make sense.
  if (saved.layout == kSettingsLayout && saved.effect >= 0 &&
      saved.effect < kNumEffects) {
    current = saved.effect;
    active = saved.active;
  }
  selected = current;
  blink_request = current + 1;
}

// Writes the state to flash once it has stopped changing. Waiting keeps
// the flash from being rewritten for every tap while stepping through the
// effects. Writing takes a moment, so it happens here in the main loop and
// never in the audio callback.
void SaveSettingsWhenSettled() {
  static int seen_effect = -1;
  static bool seen_active = false;
  static uint32_t changed_at = 0;
  static bool dirty = false;

  const int effect = selected;
  const bool on = active;
  if (effect != seen_effect || on != seen_active) {
    // The first pass only records the state that was just loaded.
    dirty = seen_effect >= 0;
    seen_effect = effect;
    seen_active = on;
    changed_at = System::GetNow();
  }
  if (dirty && System::GetNow() - changed_at >= kSaveDelayMs) {
    dirty = false;
    Settings& s = storage.GetSettings();
    s.layout = kSettingsLayout;
    s.effect = effect;
    s.active = on;
    storage.Save();  // skips the write if nothing differs from flash
  }
}

}  // namespace

int main() {
  hw.Init();
  hw.SetAudioBlockSize(kBlockSize);
  hw.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
  const float sample_rate = hw.AudioSampleRate();

  ramp_step = 1.0f / (0.005f * sample_rate);  // 5 ms fades

  compressor.Init(sample_rate);
  octaver.Init(sample_rate);
  bass_synth.Init(sample_rate);
  envelope_filter.Init(sample_rate);
  chorus.Init(sample_rate, chorus_buffer, kChorusBufferSize);
  phaser.Init(sample_rate);
  reverb.Init(sample_rate, reverb_tank, predelay_buffer, kPredelayBufferSize);

  LoadSettings();

  led_effect.Init(hw.seed.GetPin(Hothouse::LED_1), false);
  led_active.Init(hw.seed.GetPin(Hothouse::LED_2), false);

  hw.StartAdc();
  hw.StartAudio(AudioCallback);

  while (true) {
    hw.DelayMs(5);
    UpdateEffectLed();
    led_active.Set(active ? 1.0f : 0.0f);
    led_active.Update();
    SaveSettingsWhenSettled();
    hw.CheckResetToBootloader();
  }
}
