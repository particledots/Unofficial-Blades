# Unofficial Blades — AU / VST3 (experiment)

A software model of the **Blades dual multimode filter circuit**, built from its published schematic (`blades_v40`). There is no firmware in the
hardware, so this is not a port of code: it is a model of the circuit, written from the drawing. Not a Mutable Instruments product, and not made or
endorsed by the module's makers.

**Status, plainly:**
* Built and tested on Linux with the tests listed below. See the bottom of this file for the macOS build result.
* **Not yet loaded in Logic. Not yet listened to. Nobody has compared it with the hardware.** The tests check that the code does what the circuit
  *equations* (read off the drawing) say, and that the numbers worked out by hand from the drawing came out as calculated. They cannot say whether it sounds
  like the real module. That is for Bryn to judge by ear.
* I read a hand-drawn-style schematic image, not a netlist. Where a reading was uncertain it is listed under "Guesses" below.

## Controls

Two identical filters. Per filter:

| Control | What it does |
|---|---|
| **Frequency** | Pole frequency of the two integrators, 5 Hz to about 24.1 kHz (the top of the range set by the 30k and 220 pF parts). |
| **Resonance** | Gain of the resonance cell. Self-oscillation starts at about **86 %**; the oscillation is limited to about ±5.7 V by the zener pair. |
| **Morph** | LP at 0 %, BP at 50 %, HP at 100 %, smoothly in between (no notch position in this circuit). |
| **Level** | Input level. 62 % of the knob is unity; above that it stays at unity (the drawing's level servo cannot go higher). |
| **Drive** | 0 = clean path. Turning it up crossfades into a path with two diode "fold" stages, which folds the signal back on itself. |

Both together: **Topology** (0 % = filter 1 into filter 2, 50 % = parallel, 100 % = the mix output is filter 1 alone), **Link 2 to 1** with **Offset**
(octaves), **FM 1 / FM 2** (sidechain input moves the frequency, ±100 %, 1 = one octave per volt with 0 dBFS = 5 V), **Input** and **Output** gain.

**Routing buttons** (the hardware has two inputs and three outputs; a plug-in has a stereo bus):
* *Mono in > mix out* (default): both channels in are summed into input 1, input 2 is normalled (fed from the topology section as in the hardware),
  the mix output comes out on both channels.
* *Stereo: L > 1, R > 2*: left goes through filter 1 and comes out on the left, right through filter 2 and comes out on the right (input 2 "plugged in").
  Topology then only matters for the mix output, which is not heard in this mode.
* *Mono in > split out*: one input (summed), filter 1 on the left, filter 2 on the right. Topology still decides what filter 2 is fed with.

The hardware's OUT 1 and OUT 2 jacks are inverted relative to the signal (an inverting buffer follows each filter); the plug-in outputs the non-inverted
signal so that it does not cancel against the dry signal in a mix. The mix output is non-inverted in the hardware too.

## What it models (from the drawing)

* **Input stage:** level VCA, then a ±5.7 V zener clamp, then two op-amp stages whose + input is limited by two antiparallel 1N4148 behind 100k
  (unity gain for small signals; the slope reverses above about 0.5 V, i.e. a fold), then a 10k "A" pot that crossfades the clean and the folded
  signal into an inverting amplifier. The clean path has a gain of 0.72, the folded path up to 15.6 for small signals, so at the top of the Drive
  knob the signal hits the folds almost immediately.
* **Filter core:** state-variable filter. HP = −(IN + LP + 0.1·BP); BP and LP come from two inverting integrators (30k, 220 pF) whose gain is a
  V2164 cell. Resonance is a second V2164 cell feeding BP/15k *into the first integrator's input* (not into the HP summer). So the damping is
  2·Gq − 0.1 and it self-oscillates when Gq < 0.05. A consequence worth knowing: the HP output has a 6 dB/octave skirt below 2·Gq·ωc (12 dB/octave
  above it), unlike a textbook SVF. A zener pair across the first integrator limits BP to about ±5.7 V.
* **Morph:** two V2164 cells whose gains come from servo loops (op-amp, VCA and diode), so that
  `OUT = −(120/62)·[ ga·gb·(BP+HP) − gb·(BP+LP) + LP ]`, with `gb = 2·morph − 0.02` and `ga = 2·morph − 1`, each limited to 0..1. Then a zener-limited
  output amplifier (about ±5.7 V).
* **Topology:** `IN2 = (1−gT1)·F1 + gT1·IN1` (filter 2's normalled input) and `OUT = (1−gT2)·F2 + gT2·F1`, with `gT1 = V/2.5 V`, `gT2 = V/5 V`
  (limited to 0..1) and `V = 5.25·knob − 0.13 V`.
* The delay-free loop inside the filter is solved exactly (zero-delay-feedback form), at 2x oversampling. The plug-in reports about 31 samples of
  latency to the host.

## Guesses (nothing below was checked against the real module)

* 0 dBFS = 5 V peak, and the op-amps clip at ±10.5 V.
* The V2164 control law is taken as −33 mV/dB (the SSM2164's). It sets how the Resonance knob maps to damping (self-oscillation at 86 %).
* The 5.1 V zener pairs are modelled as a smooth limiter that starts bending at 4.5 V and stops at 5.7 V.
* The 1N4148 uses the usual SPICE values (Is = 2.52 nA, n = 1.752). With them the two fold stages load a 1 mV signal by about 3.6 %.
* The Drive pot is a 10k "A" taper; I assumed `fraction = (20^x − 1)/19`, and that "off" (clean) is the counter-clockwise end. Either could be wrong.
* How the servo loops behave when asked for a gain above 1 (taken as: limited to 1).
* The two frequency-control chains (frequency knob, CV, V/oct trim, the switch that links filter 2 to filter 1) are **not** modelled. The Frequency knob is
  simply the pole frequency in Hz. *Link* means "filter 2 = filter 1 × 2^offset", which is the effect of the hardware's link switch, but the offset
  scale is my own (the hardware's offset follows its own knob and circuit).
* FM: 1 = one octave per volt is my choice; the hardware's FM scale depends on its attenuverter and trimmers, which I did not model.
* Level, morph and topology CV inputs, the LED, and the input protection parts are not modelled.
* The 18 pF / 220 pF compensation capacitors and the snubbers (510 Ω + 560 pF) are ignored; they only matter far above the audio band.
* Component values were read from the PDF image and not checked against a parts list. The resonance cell's input resistor (15k) and the output
  gains (120k / 62k) are the ones most worth double-checking.

## Tests (`tests/blades_test.cpp`, no JUCE)

Run in the macOS build and on Linux: 420 checks covering the numbers derived by hand (pot networks, servo gains, topology gains, self-oscillation
threshold), the diode clamp table, the input stage (fold shape, polarity, bounds), the frequency response of all morph positions against the analytic
solution at several frequencies, resonances and sample rates (worst error 0.04 %), slopes, decay and self-oscillation and the limiter, the topology
crossfades, stability with random extreme settings, the oversampler, and the whole unit (DC gain, FM, link).
The reference formulas in the tests use their own literal constants, so a wrong constant in the model is caught. I also broke the model on purpose in
22 different ways (wrong resistor value, flipped sign, swapped crossfade, wrong FM channel, ...) and the tests caught every one.

## Build

GitHub Actions on a macOS runner (`.github/workflows/build.yml`) builds a universal (arm64 + x86_64, macOS 11+) AU and VST3, runs the tests, and packages
`Unofficial-Blades-macOS` with an `INSTALL.txt`.

## Build result (macOS runner)

GitHub Actions run 38657211652: all 420 checks passed on the Mac runner, and the AU and VST3 are universal (x86_64 + arm64, minimum macOS 11.0).
In the speed test, 10 s of audio through one stereo-linked unit took 0.49 s on the runner (about 5 % of one core).
**Not checked:** loading in Logic on the M4 or the Intel Mac, MIDI/automation behaviour, and anything about how it sounds.

## Licence and credit

The model is written from the published hardware design files of Blades (design by Émilie Gillet). The drawing is marked "cc-by-sa"; Mutable Instruments
published its hardware designs under CC BY-SA 3.0, though I did not verify the version from the drawing itself. A plug-in built from the schematic is, in
my reading, an adaptation, so credit is given here and any redistribution should keep the same licence for the modelling code. That is my reading, not
legal advice; it should be settled before the plug-in is shared with anyone. No code from any firmware is used (there is none). JUCE is used under its own
terms (AGPLv3 / commercial).
