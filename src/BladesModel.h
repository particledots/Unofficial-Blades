// BladesModel.h - a circuit model of the Blades dual multimode filter (schematic "blades_v40"), written from the published drawing.
// No JUCE in here, so it can be tested on its own. Header only.
//
// There is no firmware in the hardware, so this is a model of the circuit, not a port of code. What is modelled (values read off the drawing;
// the README lists what was NOT checked):
//
//  Per filter (there are two identical ones):
//   * Input stage:  level VCA (gain 0..1)  ->  +-5.7 V zener clamp  ->  two "diode fold" stages (op-amp whose + input is limited by two
//     antiparallel 1N4148 behind 100k)  ->  a drive pot that crossfades the clean signal and the folded signal into an inverting amplifier.
//   * Filter core: a state-variable filter. HP = -(IN + LP + 0.1*BP); two inverting VCA+integrators (30k, 220p) give BP and LP.
//     Resonance is a V2164 cell that feeds BP back into the first integrator's input (damping = 2*Gq - 0.1, so it self-oscillates when the
//     cell's gain Gq falls below 0.05). A pair of 5.1 V zeners across the first integrator limits BP to about +-5.7 V.
//   * Morph: two V2164 cells whose gains come from servo loops, so that  OUT = -(120/62) * [ ga*gb*(BP+HP) - gb*(BP+LP) + LP ]
//     (LP -> BP -> HP as the knob turns). The output amplifier is limited by zeners to about +-5.7 V.
//  Both filters together (topology section):
//   * Filter 2's input is normalled:  IN2 = (1-gT1)*F1 + gT1*IN1   (serial -> parallel input)
//   * The mix output:                 OUT = (1-gT2)*F2 + gT2*F1
//   with gT1 = V/2.5 V and gT2 = V/5 V (each limited to 0..1), V = the topology knob's voltage, 0 .. about 5.1 V.
//
// The only delay-free loop is inside the filter core, which is solved exactly (TPT / zero-delay-feedback form), so no one-sample delay is added.
// The op-amps clip at +-kRail volts. Audio level: 1.0 in the plug-in = kVoltsPerFS volts.
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace blades {

constexpr double kPi          = 3.14159265358979323846;
constexpr double kVoltsPerFS  = 5.0;      // ASSUMPTION: 0 dBFS = 5 V peak (not from the drawing)
constexpr double kRail        = 10.5;     // ASSUMPTION: op-amp output swing on +-12 V
constexpr double kR_int       = 30e3;     // R132 / R131 (into the VCA, then the integrator)
constexpr double kC_int       = 220e-12;  // C62 / C63
constexpr double kStageMaxHz  = 1.0 / (2.0 * kPi * kR_int * kC_int);   // about 24.1 kHz (VCA gain 1)
constexpr double kRes0        = 30e3 / 300e3;   // R115 / R122: BP into the HP summer. Alone this would be negative damping.
constexpr double kResCell     = 30e3 / 15e3;    // R132 / R142: the resonance cell feeds BP/15k into the 30k integrator input
constexpr double kVcA         = 0.66 / 2.302585092994046;   // V2164 control law, ASSUMPTION: -33 mV/dB  =>  G = exp(-Vc / 0.2866 V)
constexpr double kResVcMax    = (20e3 / 100e3) * 5.0;       // IC19D: Vc = (R148/R155) * |knob voltage|, knob 0..-5 V  => 0..1 V
constexpr double kZenerKnee   = 4.5;      // ASSUMPTION: where the 5.1 V zener pairs begin to conduct noticeably
constexpr double kZenerMax    = 5.7;      // 5.1 V + one forward drop
constexpr double kMorphOutK   = 120.0 / 62.0;   // R153 / R173 etc.
constexpr double kDiodeIs     = 2.52e-9;  // 1N4148 SPICE values
constexpr double kDiodeNVt    = 1.752 * 0.02585;

inline double clipRail(double v) { return std::max(-kRail, std::min(kRail, v)); }
inline double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }

// smooth limiter: identity up to the knee, then bends towards +-vmax (continuous slope at the knee)
inline double softClamp(double v, double knee, double vmax) {
    const double a = std::fabs(v);
    if (a <= knee) return v;
    const double r = vmax - knee;
    const double o = knee + r * std::tanh((a - knee) / r);
    return v < 0.0 ? -o : o;
}

// Two antiparallel 1N4148 behind a 100k resistor to ground (R66 / R65): the voltage V at the diodes when the resistor is driven by vin.
// Solved once into a table:  (vin - V)/100k = 2*Is*sinh(V/(n*Vt)).  Odd function.
class DiodeClamp {
public:
    static const DiodeClamp& get() { static const DiodeClamp d; return d; }
    double vp(double vin) const {
        const double a = std::fabs(vin);
        double r;
        if (a >= kMax - kStep) r = tab[tab.size() - 2];   // beyond the table the diodes are fully on: V stays near its last value
        else { const double x = a / kStep; const int i = (int) x; const double fr = x - i; r = tab[i] + fr * (tab[i + 1] - tab[i]); }
        return vin < 0.0 ? -r : r;
    }
private:
    static constexpr double kMax = 16.0, kStep = 0.002;
    std::vector<double> tab;
    DiodeClamp() {
        const int n = (int) (kMax / kStep) + 2;
        tab.resize(n);
        for (int i = 0; i < n; ++i) {
            const double a = i * kStep;
            double lo = 0.0, hi = std::min(a, 1.2);
            for (int it = 0; it < 80; ++it) {
                const double u = 0.5 * (lo + hi);
                const double f = (a - u) / 100e3 - 2.0 * kDiodeIs * std::sinh(u / kDiodeNVt);
                if (f > 0.0) lo = u; else hi = u;
            }
            tab[i] = 0.5 * (lo + hi);
        }
    }
};

// 2x oversampler: linear-phase half-band-style FIR (windowed sinc, cutoff fs/4 of the 2x rate). Same as in the other plug-ins of the collection.
class Oversampler2x {
public:
    static constexpr int kTaps = 63;                 // odd, symmetric
    Oversampler2x() {
        const int m = (kTaps - 1) / 2;
        double sum = 0.0;
        for (int i = 0; i < kTaps; ++i) {
            double x = i - m;
            double s = (x == 0.0) ? 0.5 : std::sin(kPi * x * 0.5) / (kPi * x);
            double w = 0.42 - 0.5 * std::cos(2.0 * kPi * i / (kTaps - 1)) + 0.08 * std::cos(4.0 * kPi * i / (kTaps - 1));
            h[i] = s * w; sum += h[i];
        }
        for (int i = 0; i < kTaps; ++i) h[i] *= 2.0 / sum;
        reset();
    }
    void reset() { upBuf.assign(kTaps, 0.0); dnBuf.assign(kTaps, 0.0); upPos = dnPos = 0; }
    static constexpr int latencySamples() { return 31; }   // host-rate samples: up (15.5) + down (15.5)
    void up(double x, double& o0, double& o1) {
        push(upBuf, upPos, x);  o0 = conv(upBuf, upPos);
        push(upBuf, upPos, 0.0); o1 = conv(upBuf, upPos);
    }
    double down(double a, double b) {
        push(dnBuf, dnPos, a);
        const double r = 0.5 * conv(dnBuf, dnPos);
        push(dnBuf, dnPos, b);
        return r;
    }
private:
    double h[kTaps];
    std::vector<double> upBuf, dnBuf;
    int upPos = 0, dnPos = 0;
    void push(std::vector<double>& b, int& p, double x) { p = (p + 1) % kTaps; b[p] = x; }
    double conv(const std::vector<double>& b, int p) const {
        double a = 0.0;
        for (int i = 0; i < kTaps; ++i) a += h[i] * b[(p - i + kTaps) % kTaps];
        return a;
    }
};

// ---------- one filter's settings, turned into the numbers the circuit uses ----------
struct FilterCoefs {
    double g = 0.0;      // TPT integrator gain  tan(pi*f/fs)  (set with setFrequency)
    double gq = 1.0;     // gain of the resonance V2164 cell (1 = no resonance; self-oscillation below 0.05)
    double ga = 0.0, gb = 0.0;   // morph VCA gains
    double gIn = 1.0;    // input level VCA gain
    double ka = 0.0;     // clean path:  IN = -ka*x1 - kb*y2
    double kb = 0.0;     // folded path
};

// Pole frequency of both integrators, in Hz, at sample rate fs.
inline double hzToG(double hz, double fs) {
    const double f = std::max(1.0, std::min(hz, kStageMaxHz));
    return std::tan(kPi * std::min(f, 0.45 * fs) / fs);
}

// Resistance of the drive pot (10k "A" taper) on the folded side, as a fraction of 10k. ASSUMPTION: the taper and which end is "off" are guesses.
inline double driveTaper(double u) { return (std::pow(20.0, clamp01(u)) - 1.0) / 19.0; }

inline double par(double a, double b) { return (a <= 0.0 || b <= 0.0) ? 0.0 : a * b / (a + b); }

// reso, morph, level, drive: 0..1 knobs.
inline FilterCoefs makeCoefs(double hz, double reso, double morph, double level, double drive, double fs) {
    FilterCoefs c;
    c.g  = hzToG(hz, fs);
    c.gq = std::exp(-(clamp01(reso) * kResVcMax) / kVcA);
    // morph servos (IC12D/IC18D and IC12C/IC18C): gain = (V_pot/100k - offset current) / 25 uA, V_pot = 5 V * knob, limited to 0..1
    const double m = clamp01(morph);
    c.gb = clamp01(2.0 * m - 0.02);
    c.ga = clamp01(2.0 * m - 1.0);
    // input level servo (IC4A/IC2B): gain = (V_pot/62k - 0.5 uA) / 50 uA
    c.gIn = clamp01(5.0 * clamp01(level) / 3.1 - 0.01);
    // drive pot: it ties the wiper to ground; node A (clean, fed through 30k, into 62k) and node B (folded, 5.1k, into 5.1k)
    const double rb = 10e3 * driveTaper(drive), ra = 10e3 - rb;
    const double Ap = par(ra, 62e3), Bp = par(rb, 5.1e3);
    c.ka = (200e3 / 62e3)  * (Ap / (30e3 + Ap));
    c.kb = (200e3 / 5.1e3) * (Bp / (5.1e3 + Bp));
    return c;
}

// the input stage up to the filter's input (volts in, volts out)
inline double inputStage(double raw, const FilterCoefs& c) {
    const DiodeClamp& d = DiodeClamp::get();
    const double x1 = softClamp(-c.gIn * raw, kZenerKnee, kZenerMax);    // IC2C with its zener clamp
    const double y1 = clipRail(1.3 * d.vp(x1) - 0.3 * x1);                // IC6B: -0.3*in + 1.3*(+ input, limited by the diodes)
    const double y2 = clipRail(2.0 * d.vp(y1) - y1);                      // IC6A: -1.0*in + 2.0*(+ input, limited by the diodes)
    return clipRail(-c.ka * x1 - c.kb * y2);                              // IC2A with the drive pot
}

// the state-variable filter core
class Svf {
public:
    void reset() { sb = sl = 0.0; }
    void step(double in, const FilterCoefs& c, double& hp, double& bp, double& lp) {
        const double g = c.g;
        double b = (g * (in + sl) + sb) / (1.0 + g * g + g * (2.0 * c.gq - kRes0));
        b = softClamp(b, kZenerKnee, kZenerMax);      // the zener pair across the first integrator
        const double l = clipRail(sl - g * b);
        const double h = clipRail(-(in + l + kRes0 * b));
        sb = 2.0 * b - sb;
        sl = 2.0 * l - sl;
        hp = h; bp = b; lp = l;
    }
private:
    double sb = 0.0, sl = 0.0;
};

// morph mixer and output amplifier (the node called OUT_n/4.nA in the drawing, before the inverting buffer)
inline double morphOut(double hp, double bp, double lp, const FilterCoefs& c) {
    const double v = -kMorphOutK * (c.ga * c.gb * (bp + hp) - c.gb * (bp + lp) + lp);
    return softClamp(v, kZenerKnee, kZenerMax);
}

// topology knob (0..1) -> the two crossfade gains
struct TopoGains { double gT1 = 0.0, gT2 = 0.0; };
inline TopoGains topology(double knob) {
    // IC3A: V = 100k * (5 V * knob / 95.3k - 5 V / 3.9M)
    const double v = 100e3 * (5.0 * clamp01(knob) / 95.3e3 - 5.0 / 3.9e6);
    TopoGains t;
    t.gT1 = clamp01(v / 2.5);
    t.gT2 = clamp01(v / 5.0);
    return t;
}

// both filters and the topology section, one (oversampled) sample at a time. Volts in, volts out.
class Engine {
public:
    void reset() { f1.reset(); f2.reset(); }
    // raw1/raw2: signals at the two input jacks. in2plugged: true = raw2 is used; false = input 2 is normalled (see header).
    // F1/F2 are the two filters' outputs (the non-inverted node; the hardware's OUT_1/OUT_2 jacks are the same signal inverted).
    void step(double raw1, double raw2, bool in2plugged, const FilterCoefs& c1, const FilterCoefs& c2, const TopoGains& t,
              double& F1, double& F2, double& mix) {
        double hp, bp, lp;
        f1.step(inputStage(raw1, c1), c1, hp, bp, lp);
        F1 = morphOut(hp, bp, lp, c1);
        const double r2 = in2plugged ? raw2 : clipRail((1.0 - t.gT1) * F1 + t.gT1 * raw1);
        f2.step(inputStage(r2, c2), c2, hp, bp, lp);
        F2 = morphOut(hp, bp, lp, c2);
        mix = clipRail((1.0 - t.gT2) * F2 + t.gT2 * F1);
    }
private:
    Svf f1, f2;
};

// Plug-in settings in the plug-in's units (the processor smooths them and passes them in).
struct Params {
    double hz1 = 1000.0, hz2 = 1000.0;           // pole frequency of each filter
    double reso1 = 0.2, reso2 = 0.2, morph1 = 0.0, morph2 = 0.0, level1 = 0.62, level2 = 0.62, drive1 = 0.0, drive2 = 0.0;
    double topo = 0.0;
    bool   link = false;                         // filter 2's frequency follows filter 1's, shifted by offsetOct
    double offsetOct = 0.0;
};

// What the plug-in uses: two filters at 2x oversampling.
// FM: fm1Oct / fm2Oct are frequency offsets in octaves (positive = higher), exponential like the hardware's V/oct summing. They are delayed by the
// oversampler's latency so that they line up with the audio they belong to.
class Unit {
public:
    static constexpr int kFmDelay = 31;     // = Oversampler2x::latencySamples()
    static_assert(kFmDelay == Oversampler2x::latencySamples(), "FM delay must equal the oversampler latency");
    void reset() {
        osIn1.reset(); osIn2.reset(); osMix.reset(); osO1.reset(); osO2.reset(); eng.reset();
        for (int k = 0; k < 2; ++k) { for (double& d : fmBuf[k]) d = 0.0; fmPrev[k] = 0.0; }
        fmPos = 0;
    }
    // in1/in2 in plug-in units (1.0 = kVoltsPerFS volts), already multiplied by the input gain.
    void process(double in1, double in2, bool in2plugged, const Params& p, double hostFs, double fm1Oct, double fm2Oct, double outGainLin,
                 float& mixOut, float& out1, float& out2) {
        // FM, delayed to match the audio path
        double fmA[2], fmB[2];
        const double fmIn[2] = { fm1Oct, fm2Oct };
        const int readPos = (fmPos - kFmDelay + kFmBufLen) % kFmBufLen;
        for (int k = 0; k < 2; ++k) {
            fmBuf[k][fmPos] = std::max(-8.0, std::min(8.0, fmIn[k]));
            const double cur = fmBuf[k][readPos];
            fmA[k] = 0.5 * (fmPrev[k] + cur); fmB[k] = cur; fmPrev[k] = cur;
        }
        fmPos = (fmPos + 1) % kFmBufLen;
        double a1, b1, a2, b2;
        osIn1.up(in1 * kVoltsPerFS, a1, b1);
        osIn2.up(in2 * kVoltsPerFS, a2, b2);
        const double fs2 = 2.0 * hostFs;
        FilterCoefs c1 = makeCoefs(p.hz1, p.reso1, p.morph1, p.level1, p.drive1, fs2);
        FilterCoefs c2 = makeCoefs(p.hz2, p.reso2, p.morph2, p.level2, p.drive2, fs2);
        const TopoGains t = topology(p.topo);
        double F1a, F2a, ma, F1b, F2b, mb;
        // first of the two oversampled samples, then the second
        setFreqs(c1, c2, p, fmA, fs2);  eng.step(a1, a2, in2plugged, c1, c2, t, F1a, F2a, ma);
        setFreqs(c1, c2, p, fmB, fs2);  eng.step(b1, b2, in2plugged, c1, c2, t, F1b, F2b, mb);
        const double k = outGainLin / kVoltsPerFS;
        mixOut = (float) (osMix.down(ma, mb) * k);
        out1   = (float) (osO1.down(F1a, F1b) * k);
        out2   = (float) (osO2.down(F2a, F2b) * k);
    }
private:
    static void setFreqs(FilterCoefs& c1, FilterCoefs& c2, const Params& p, const double* fm, double fs2) {
        const double h1 = p.hz1 * std::exp2(fm[0]);
        const double h2 = p.link ? h1 * std::exp2(p.offsetOct + fm[1]) : p.hz2 * std::exp2(fm[1]);
        c1.g = hzToG(h1, fs2);
        c2.g = hzToG(h2, fs2);
    }
    static constexpr int kFmBufLen = 64;
    Oversampler2x osIn1, osIn2, osMix, osO1, osO2;
    Engine eng;
    double fmBuf[2][kFmBufLen] = {};
    int fmPos = 0;
    double fmPrev[2] = { 0.0, 0.0 };
};

} // namespace blades
