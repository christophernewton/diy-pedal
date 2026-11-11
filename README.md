# Bass multi-effect for the Hothouse

Firmware for a [Cleveland Music Co. Hothouse](https://clevelandmusicco.com/products/hothouse-digital-signal-processing-platform-kit) pedal with a Daisy Seed 3, voiced for bass guitar. Seven effects, one at a time, stepped through with the left footswitch.

Everything builds inside Docker. The only thing needed on the Mac besides Docker is `dfu-util` for flashing (`brew install dfu-util`).

## Build and flash

```sh
make build   # compiles firmware/build/bass_fx.bin (first run builds the Docker image, a few minutes)
make flash   # builds, then flashes over USB-C
```

The Seed has to be in DFU mode before `make flash`:

- First time: hold **BOOT** on the Seed, press and release **RESET**, release **BOOT**.
- After that: hold both footswitches for two seconds. The LEDs flash alternately and the pedal reboots into DFU mode, so the enclosure can stay closed.

Flashing runs on the host because Docker Desktop on macOS cannot pass USB devices into a container. If `dfu-util` is not an option, upload `firmware/build/bass_fx.bin` at [flash.daisy.audio](https://flash.daisy.audio) in Chrome.

Other targets: `make test` runs the offline DSP checks, `make demo` renders each effect on a synthetic bass riff to `build/demo/*.wav`, `make shell` opens the build container, `make clean` removes build output.

## Playing it

| Control | What it does |
|---|---|
| Footswitch 1, tap | Next effect |
| Footswitch 1, hold | Previous effect |
| Footswitch 2 | Effect on or off |
| Both footswitches, 2 s | Reboot into DFU mode |
| LED 1 | Blinks the number of the selected effect |
| LED 2 | Lit while the effect is on |
| Knob 6 | Output level of the effect, unity at noon, up to +12 dB |
| Knobs 1 to 5, toggles 1 to 3 | Belong to the selected effect, see below |

The pedal remembers the selected effect and whether it was on, and comes back that way at the next power-up. It saves two seconds after your last footswitch press, so wait a moment before pulling the power. A brand new pedal starts bypassed on effect 1. Plug the bass into the left input; the output is on both jacks, and the chorus, phaser and reverb are stereo if you use both.

The knobs are read live, so changing effect picks up wherever the knobs are sitting.

## The effects

| # | Effect | Knob 1 | Knob 2 | Knob 3 | Knob 4 | Knob 5 |
|---|---|---|---|---|---|---|
| 1 | Compressor | Threshold | Ratio | Attack | Release | Blend |
| 2 | Octaver | Dry | Octave down | Two octaves down | Sub tone | Octave up |
| 3 | Bass synth | Cutoff | Resonance | Envelope | Sub | Dry |
| 4 | Envelope filter | Sensitivity | Frequency | Resonance | Decay | Mix |
| 5 | Chorus | Rate | Depth | Mix | Crossover | Feedback |
| 6 | Phaser | Rate | Depth | Feedback | Centre | Mix |
| 7 | Reverb | Decay | Tone | Mix | Low cut | Pre-delay |

| # | Effect | Toggle 1 (up / middle / down) | Toggle 2 (up / middle / down) | Toggle 3 (up / middle / down) |
|---|---|---|---|---|
| 1 | Compressor | Sidechain high-pass 150 Hz / off / 80 Hz | Make-up +12 / +6 / 0 dB | |
| 2 | Octaver | | | |
| 3 | Bass synth | Saw / square / pulse | Octave +1 / 0 / -1 | Filter decay short / medium / long |
| 4 | Envelope filter | Low-pass / band-pass / high-pass | Wide up / gentle up / down | |
| 5 | Chorus | Flanger / chorus / vibrato | Stereo wide / normal / mono | |
| 6 | Phaser | 6 / 4 / 8 stages | Stereo wide / normal / mono | |
| 7 | Reverb | Hall / plate / room | | |

A few things are done specifically for bass. The chorus splits the signal and leaves everything below the crossover untouched, so the low end stays solid. The reverb only feeds the part above its low cut into the tank, and its tail rings out after you bypass. The compressor's sidechain filter stops the low strings pumping the whole signal. The octaver and synth are monophonic: play one note at a time and mute cleanly between notes.

Full notes on each effect are in the comment at the top of its file in `firmware/effects/`.

## What the effects are modelled on

Each effect follows the pedal or circuit most often named as the reference for that sound on bass. Where the figures came from a manual, a schematic or a paper they are used as published; where nothing was published the values are estimates to tune by ear.

| Effect | Reference | What was taken from it |
|---|---|---|
| Compressor | UREI 1176, Origin Effects Cali76 Compact Bass, Empress Bass Compressor | Attack down to 0.1 ms, release 50 ms to 1.1 s that slows after sustained compression, ratios up to 20:1, a high-pass in the detector, a parallel dry blend |
| Octaver | Boss OC-2 | The signal path: rectify the note, flip its polarity every cycle, then a resonant third-order low-pass at 325 Hz (one octave down) and 153 Hz (two octaves down), worked out from the circuit's component values |
| Bass synth | Minimoog and Moog Taurus bass patches, EHX Bass Micro Synth | Four-pole ladder low-pass, two saws detuned 10 cents apart, square sub oscillator, a filter envelope fired by each pluck |
| Envelope filter | Mu-Tron III, MXR M82 Bass Envelope Filter | State variable filter with low, band and high-pass and up or down drive; the M82's 76 Hz to 3.2 kHz sweep, resonance and sensitivity ranges |
| Chorus | Boss CE-2B, Roland Juno-60, MXR M83 Bass Chorus Deluxe | Triangle LFO, 2 to 8 ms delay, wet signal rolled off at 6.6 kHz, lows kept out of the wet path |
| Phaser | MXR Phase 90, EHX Small Stone, Mu-Tron Phasor II | Four or six all-pass stages, triangle LFO, equal dry and wet mix, optional feedback |
| Reverb | Dattorro plate (the published Lexicon-style tank) | The whole topology, delay lengths, diffusion coefficients and output taps from the 1997 paper |

These are built from published behaviour and circuit values. None of it has been compared against the real pedals by ear or by measurement.

## Layout

```
Dockerfile            toolchain image: Arm GCC, libDaisy v9.0.0, DaisySP
Makefile              host entry points (build, test, demo, flash)
scripts/flash.sh      dfu-util wrapper
firmware/main.cpp     footswitches, LEDs, bypass, effect switching
firmware/effects/     one header per effect
firmware/dsp/         shared filters, delay line, pitch tracker
firmware/hothouse.*   hardware layer from HothouseExamples, unmodified
tests/test_dsp.cpp    offline checks and the demo renderer
```

To add an effect, write a class in `firmware/effects/` that implements `fx::Effect`, initialise it in `main()` and add it to the `effects[]` array in `main.cpp`. The array order is the footswitch order.

The image pins libDaisy v9.0.0 because that is the first release that detects the Seed 3. HothouseExamples itself still pins a March 2024 libDaisy that predates the board.

## Licence

GPL-3.0, inherited from the Hothouse hardware layer (`firmware/hothouse.*`, Cleveland Music Co.).
