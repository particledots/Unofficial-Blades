// Tests of the Blades model (no JUCE). Build:  c++ -std=c++17 -O2 -Isrc tests/blades_test.cpp -o blades_test && ./blades_test
//
// What these tests can and cannot say: they check that the code does what the circuit EQUATIONS (read off the drawing, see BladesModel.h) say,
// by comparing with an independent analytic solution, and that the numbers derived by hand from the drawing came out as calculated.
// They cannot say whether the equations match the real module's sound. The reference formulas below use their own literal constants
// (not the ones from the model header), so a wrong constant in the model is caught.
#include "BladesModel.h"
#include <complex>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
using namespace blades;
using Cx = std::complex<double>;

static int checks = 0, fails = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; printf("FAIL line %d: %s  ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }
static const double PI = 3.14159265358979323846;

// ---------- independent reference values (hand calculations from the drawing) ----------
static const double K0 = 0.1;                    // 30k / 300k
static const double KOUT = 120.0 / 62.0;         // 1.935484
static const double KA_CLEAN = 0.71942;          // (200/62) * (8.6111k / (30k + 8.6111k))  with the pot fully on the clean side
static const double KB_FOLD = 15.6247;           // (200/5.1) * (3.3775k / (5.1k + 3.3775k))  with the pot fully on the folded side
static const double VCA = 0.66 / std::log(10.0); // V per e-fold of the V2164 gain

// Complex gain of a small sine through a processing function (one input sample -> one output sample).
static Cx measure(const std::function<double(double)>& proc, double fs, double f, double amp, double warmSecs, int cycles = 40) {
    const long warm = (long) (warmSecs * fs);
    const long N = (long) std::llround(cycles * fs / f);
    double sr = 0, si = 0;
    for (long i = 0; i < warm + N; ++i) {
        const double ph = 2.0 * PI * f * (double) i / fs;
        const double y = proc(amp * std::sin(ph));
        if (i >= warm) { sr += y * std::sin(ph); si += y * std::cos(ph); }
    }
    return Cx(sr, si) * (2.0 / (N * amp));
}

// Analytic transfer function of one filter (input stage clean path -> filter -> morph), using the bilinear-warped frequency that the
// zero-delay-feedback integrators realise exactly.
static Cx reference(const FilterCoefs& c, double hz, double fs, double f) {
    const double wc = 2.0 * fs * std::tan(PI * hz / fs);
    const Cx s(0.0, 2.0 * fs * std::tan(PI * f / fs));
    const double d = 2.0 * c.gq - K0;
    const double inGain = c.ka * c.gIn;                         // IN = -ka * x1 = ka * gIn * raw   (small signal, clean path)
    const Cx D = s * s + d * wc * s + wc * wc;
    const Cx LP = -inGain * wc * wc / D;
    const Cx BP = -s * LP / wc;
    const Cx HP = -(inGain + LP + K0 * BP);
    return -KOUT * (c.ga * c.gb * (BP + HP) - c.gb * (BP + LP) + LP);
}

static double settleSecs(double hz, double gq) {
    const double d = std::max(2.0 * gq - K0, 0.02);
    return std::min(4.0, 16.0 * 2.0 / (d * 2.0 * PI * hz));
}

// ---------------------------------------------------------------------------------------------------------------------------------------
static void testConstants() {
    printf("-- numbers derived by hand from the drawing\n");
    FilterCoefs c = makeCoefs(1000, 0, 0, 0.62, 0.0, 96000);
    CHECK(std::fabs(c.ka - KA_CLEAN) < 0.0005, "ka(drive 0) = %g, expected %g", c.ka, KA_CLEAN);
    CHECK(c.kb == 0.0, "kb(drive 0) = %g, expected 0", c.kb);
    c = makeCoefs(1000, 0, 0, 0.62, 1.0, 96000);
    CHECK(std::fabs(c.kb - KB_FOLD) < 0.005, "kb(drive 1) = %g, expected %g", c.kb, KB_FOLD);
    CHECK(c.ka == 0.0, "ka(drive 1) = %g, expected 0", c.ka);
    // resonance cell gain and the self-oscillation threshold
    c = makeCoefs(1000, 0.0, 0, 0.62, 0, 96000);
    CHECK(std::fabs(c.gq - 1.0) < 1e-12, "gq(reso 0) = %g", c.gq);
    for (double r : { 0.3, 0.6, 1.0 }) {
        c = makeCoefs(1000, r, 0, 0.62, 0, 96000);
        CHECK(std::fabs(c.gq - std::exp(-r * 1.0 / VCA)) < 1e-9, "gq(%g) = %g", r, c.gq);
    }
    const double rThresh = VCA * std::log(20.0) / 1.0;      // where 2*gq - 0.1 = 0
    c = makeCoefs(1000, rThresh, 0, 0.62, 0, 96000);
    CHECK(std::fabs(2.0 * c.gq - K0) < 1e-6, "threshold: 2gq - 0.1 = %g at reso %g", 2.0 * c.gq - K0, rThresh);
    CHECK(rThresh > 0.85 && rThresh < 0.87, "self-oscillation threshold %g (hand calculation 0.859)", rThresh);
    // morph servo gains
    struct M { double m, ga, gb; } ms[] = { {0.0, 0, 0}, {0.25, 0, 0.48}, {0.5, 0, 0.98}, {0.75, 0.5, 1.0}, {1.0, 1.0, 1.0} };
    for (auto& e : ms) {
        c = makeCoefs(1000, 0, e.m, 0.62, 0, 96000);
        CHECK(std::fabs(c.ga - e.ga) < 1e-9 && std::fabs(c.gb - e.gb) < 1e-9, "morph %g: ga %g gb %g (expected %g %g)", e.m, c.ga, c.gb, e.ga, e.gb);
    }
    // input level servo: unity at 62 % of the knob (V_pot = 3.1 V), nothing above
    c = makeCoefs(1000, 0, 0, 0.0, 0, 96000);   CHECK(c.gIn == 0.0, "level 0 gives %g", c.gIn);
    c = makeCoefs(1000, 0, 0, 0.31, 0, 96000);  CHECK(std::fabs(c.gIn - 0.49) < 1e-9, "level 0.31 gives %g (expected 0.49)", c.gIn);
    c = makeCoefs(1000, 0, 0, 0.8, 0, 96000);   CHECK(c.gIn == 1.0, "level 0.8 gives %g (expected 1)", c.gIn);
    // topology: V = 5.2466*knob - 0.1282
    TopoGains t = topology(0.0);  CHECK(t.gT1 == 0.0 && t.gT2 == 0.0, "topology 0: %g %g", t.gT1, t.gT2);
    t = topology(0.5);            CHECK(std::fabs(t.gT1 - 0.9981) < 0.001 && std::fabs(t.gT2 - 0.4990) < 0.001, "topology 0.5: %g %g", t.gT1, t.gT2);
    t = topology(1.0);            CHECK(t.gT1 == 1.0 && t.gT2 == 1.0, "topology 1: %g %g", t.gT1, t.gT2);
    t = topology(0.25);           CHECK(std::fabs(t.gT1 - 0.4734) < 0.001 && std::fabs(t.gT2 - 0.2367) < 0.001, "topology 0.25: %g %g", t.gT1, t.gT2);
    // stage maximum frequency
    CHECK(std::fabs(kStageMaxHz - 24118.0) < 20.0, "stage max %g Hz (1/(2*pi*30k*220p) = 24118)", kStageMaxHz);
}

static void testDiode() {
    printf("-- diode clamp table\n");
    const DiodeClamp& d = DiodeClamp::get();
    for (double v : { 0.01, 0.1, 0.3, 0.5, 1.0, 3.0, 10.0 }) {
        const double u = d.vp(v);
        const double resid = (v - u) / 100e3 - 2.0 * 2.52e-9 * std::sinh(u / (1.752 * 0.02585));
        CHECK(std::fabs(resid) < 1e-7 * std::max(1.0, v / 1.0) * 0.01 + 2e-9, "vp(%g) = %g, residual current %g", v, u, resid);
        CHECK(std::fabs(d.vp(-v) + u) < 1e-12, "not odd at %g", v);
        CHECK(u <= v + 1e-12, "vp(%g) = %g exceeds the input", v, u);
    }
    CHECK(d.vp(5.0) > 0.40 && d.vp(5.0) < 0.50, "vp(5 V) = %g (about 0.45 expected: 50 uA through two 1N4148 SPICE models)", d.vp(5.0));
    // small signal: the diode pair looks like n*Vt/(2*Is) = 9 Mohm in parallel with nothing, behind 100k => a factor 1/(1 + 100k*2*Is/(n*Vt))
    const double pf = 1.0 / (1.0 + 100e3 * 2.0 * 2.52e-9 / (1.752 * 0.02585));
    CHECK(std::fabs(d.vp(0.001) - 0.001 * pf) < 2e-7, "vp(1 mV) = %g, expected %g", d.vp(0.001), 0.001 * pf);
}

static void testInputStage() {
    printf("-- input stage (level, folds, drive)\n");
    FilterCoefs c = makeCoefs(1000, 0, 0, 0.62, 0.0, 96000);
    CHECK(std::fabs(inputStage(1.0, c) - KA_CLEAN * c.gIn) < 0.001, "clean path: 1 V -> %g, expected %g", inputStage(1.0, c), KA_CLEAN * c.gIn);
    CHECK(std::fabs(inputStage(-2.0, c) + 2.0 * KA_CLEAN * c.gIn) < 0.002, "clean path polarity at -2 V");
    c = makeCoefs(1000, 0, 0, 0.62, 1.0, 96000);
    const double small = inputStage(0.001, c);
    // the two fold stages are not exactly unity for tiny signals (the diodes load the 100k a little): y1 = (1.3p - 0.3) x, y2 = (2p - 1) y1
    const double pf = 1.0 / (1.0 + 100e3 * 2.0 * 2.52e-9 / (1.752 * 0.02585));
    const double foldSmall = (1.3 * pf - 0.3) * (2.0 * pf - 1.0);
    CHECK(std::fabs(small / (KB_FOLD * c.gIn * 0.001 * foldSmall) - 1.0) < 0.003, "folded path, small signal: %g, expected %g", small, KB_FOLD * c.gIn * 0.001 * foldSmall);
    // wavefold (hand calculation at drive 1: about +2.9 V at 0.4 V in, back through zero near 1.7 V, about -2.8 V at 3 V in)
    double peak = 0, peakAt = 0;
    for (double v = 0; v <= 1.5; v += 0.01) { const double y = inputStage(v, c); if (y > peak) { peak = y; peakAt = v; } }
    CHECK(peakAt > 0.2 && peakAt < 0.8, "first fold peak at %g V", peakAt);
    CHECK(peak > 2.5 && peak < 3.3, "first fold peak height %g V", peak);
    CHECK(inputStage(2.0, c) < -0.5, "no fold-back: value at 2 V in is %g", inputStage(2.0, c));
    CHECK(inputStage(3.0, c) < -2.0, "second excursion: value at 3 V in is %g", inputStage(3.0, c));
    bool odd = true, bounded = true;
    for (double v = 0; v <= 12.0; v += 0.037) {
        if (std::fabs(inputStage(-v, c) + inputStage(v, c)) > 1e-9) odd = false;
        if (std::fabs(inputStage(v, c)) > kRail + 1e-9) bounded = false;
    }
    CHECK(odd, "input stage is not an odd function"); CHECK(bounded, "input stage exceeds the rails");
    // the level servo scales the signal into the stage
    FilterCoefs lo = makeCoefs(1000, 0, 0, 0.31, 0.0, 96000);
    CHECK(std::fabs(inputStage(1.0, lo) / inputStage(1.0, makeCoefs(1000, 0, 0, 0.8, 0.0, 96000)) - 0.49) < 0.002, "level scaling");
    // drive between: both paths contribute (kb > 0 and ka > 0)
    c = makeCoefs(1000, 0, 0, 0.62, 0.5, 96000);
    CHECK(c.ka > 0.0 && c.kb > 0.0 && c.ka < KA_CLEAN && c.kb < KB_FOLD, "drive 0.5: ka %g kb %g", c.ka, c.kb);
    // monotonic: more drive -> more small-signal gain
    double prev = -1; bool mono = true;
    for (double u = 0; u <= 1.0001; u += 0.1) { const double y = inputStage(0.001, makeCoefs(1000, 0, 0, 0.62, u, 96000)); if (u > 0.35 && y < prev) mono = false; prev = y; }
    CHECK(mono, "small-signal gain not rising with drive");
}

static void testFrequencyResponse() {
    printf("-- filter frequency response against the analytic solution (LP/BP/HP and in between, several Q, sample rates)\n");
    int n = 0; double worst = 0;
    for (double fs : { 88200.0, 192000.0 })
    for (double hz : { 300.0, 3000.0 })
    for (double reso : { 0.0, 0.5, 0.8 })
    for (double morph : { 0.0, 0.25, 0.5, 0.75, 1.0 }) {
        FilterCoefs c = makeCoefs(hz, reso, morph, 0.62, 0.0, fs);
        for (double mult : { 0.25, 0.7, 1.0, 1.4, 3.0 }) {
            Engine e; e.reset();
            const double f = hz * mult;
            const Cx m = measure([&](double x) { double F1, F2, mx; e.step(x, 0, true, c, c, TopoGains{}, F1, F2, mx); return F1; },
                                 fs, f, 0.002, settleSecs(hz, c.gq));
            const Cx r = reference(c, hz, fs, f);
            const double err = std::abs(m - r) / std::max(std::abs(r), 1e-9);
            worst = std::max(worst, err); ++n;
            CHECK(err < 0.005, "fs %g hz %g reso %g morph %g f %g: measured %g,%g expected %g,%g (rel err %g)", fs, hz, reso, morph, f, m.real(), m.imag(), r.real(), r.imag(), err);
        }
    }
    printf("   %d points, worst relative error %.2g\n", n, worst);
    // against the true (un-warped) analog response, at moderate frequencies
    {
        const double fs = 96000, hz = 1000;
        FilterCoefs c = makeCoefs(hz, 0.3, 0.0, 0.62, 0.0, fs);
        for (double f : { 100.0, 500.0, 1000.0, 2000.0 }) {
            Engine e; e.reset();
            const Cx m = measure([&](double x) { double F1, F2, mx; e.step(x, 0, true, c, c, TopoGains{}, F1, F2, mx); return F1; }, fs, f, 0.01, 0.4);
            const double wc = 2 * PI * hz; const Cx s(0, 2 * PI * f); const double d = 2.0 * c.gq - K0;
            const Cx ref = KOUT * (c.ka * c.gIn) * wc * wc / (s * s + d * wc * s + wc * wc);
            CHECK(std::abs(m - ref) / std::abs(ref) < 0.01, "analog LP at %g Hz: measured %g expected %g", f, std::abs(m), std::abs(ref));
        }
    }
}

static void testSlopes() {
    printf("-- slopes (frequencies far below the sample rate so the warping of the digital integrators does not matter)\n");
    const double fs = 192000, hz = 100;
    auto gainAt = [&](double morph, double reso, double f) {
        FilterCoefs c = makeCoefs(hz, reso, morph, 0.62, 0.0, fs);
        Engine e; e.reset();
        return std::abs(measure([&](double x) { double F1, F2, mx; e.step(x, 0, true, c, c, TopoGains{}, F1, F2, mx); return F1; }, fs, f, 0.05, 0.3));
    };
    double lp = db(gainAt(0.0, 0.0, 1600.0)) - db(gainAt(0.0, 0.0, 3200.0));
    CHECK(std::fabs(lp - 12.0) < 0.3, "LP slope %g dB/oct (expected 12)", lp);
    // BP = s*wc/D: +6 dB/oct below, -6 dB/oct above the peak (morph 0.5 still has 2 % of LP in it, so only the upper side is clean)
    double bpd = db(gainAt(0.5, 0.0, 1600.0)) - db(gainAt(0.5, 0.0, 3200.0));
    CHECK(std::fabs(bpd - 6.0) < 0.3, "BP upper slope %g dB/oct (expected 6)", bpd);
    // HP = -s*(s + 2*Gq*wc)/D: the resonance cell leaks into the first integrator, so HP has a 6 dB/oct skirt below 2*Gq*wc, 12 dB/oct above it.
    // (Follows from the drawing: the cell's output joins the integrator input, not the HP summer.)
    double hp6 = db(gainAt(1.0, 0.0, 2.0)) - db(gainAt(1.0, 0.0, 4.0));
    CHECK(std::fabs(-hp6 - 6.0) < 0.3, "HP low-frequency slope %g dB/oct (expected +6 at Q 0)", -hp6);
    // with high resonance (2*Gq*wc is small) the skirt is 12 dB/oct over most of the range below the cutoff
    double hp12 = db(gainAt(1.0, 0.9, 40.0)) - db(gainAt(1.0, 0.9, 80.0));
    CHECK(hp12 < -3.0, "HP at reso 0.9, 40 -> 80 Hz changes by %g dB (expected a rise of more than 3 dB)", hp12);
}

static void testSelfOscillation() {
    printf("-- resonance: decay, self-oscillation, limiter\n");
    const double fs = 96000, hz = 1000;
    auto run = [&](double reso, double& startAmp, double& endAmp, double& freqEst, double& peak, bool& finite) {
        FilterCoefs c = makeCoefs(hz, reso, 0.5, 0.62, 0.0, fs);
        Svf f; f.reset(); double hp, bp, lp;
        startAmp = endAmp = peak = 0; finite = true;
        std::vector<double> bpv;
        const int n = (int) (2.0 * fs);
        for (int i = 0; i < n; ++i) {
            f.step(i == 0 ? 0.5 : 0.0, c, hp, bp, lp);
            if (!std::isfinite(bp) || !std::isfinite(hp) || !std::isfinite(lp)) finite = false;
            bpv.push_back(bp); peak = std::max(peak, std::fabs(bp));
        }
        const int w = (int) (0.05 * fs);
        for (int i = 0; i < w; ++i) { startAmp = std::max(startAmp, std::fabs(bpv[(int) (0.02 * fs) + i])); endAmp = std::max(endAmp, std::fabs(bpv[n - 1 - i])); }
        int zc = 0; for (int i = n - 1 - (int) (0.5 * fs); i < n - 1; ++i) if (bpv[i] <= 0 && bpv[i + 1] > 0) ++zc;
        freqEst = zc / 0.5;
    };
    double s, e, fq, pk; bool fin;
    run(0.60, s, e, fq, pk, fin);  CHECK(fin && e < s * 0.01, "reso 0.60 should ring out: %g -> %g", s, e);
    run(0.80, s, e, fq, pk, fin);  CHECK(fin && e < s * 0.5, "reso 0.80 should decay (Q about 44): %g -> %g", s, e);
    run(0.90, s, e, fq, pk, fin);  CHECK(fin && e > 3.0 && pk < 5.75, "reso 0.90 should oscillate and be limited: end %g peak %g", e, pk);
    CHECK(std::fabs(fq - hz) < 0.03 * hz, "oscillation frequency %g Hz (expected about %g)", fq, hz);
    run(1.00, s, e, fq, pk, fin);  CHECK(fin && e > 3.0 && pk < 5.75, "reso 1.00 should oscillate and be limited: end %g peak %g", e, pk);
}

static void testTopology() {
    printf("-- topology (DC-level checks of the crossfades)\n");
    const double fs = 96000;
    FilterCoefs c1 = makeCoefs(2000, 0.0, 0.0, 0.62, 0.0, fs), c2 = makeCoefs(2000, 0.0, 0.0, 0.40, 0.0, fs);
    const double H1 = KOUT * KA_CLEAN * c1.gIn, H2 = KOUT * KA_CLEAN * c2.gIn;      // DC gain of each filter (LP, independent of Q)
    for (double knob : { 0.0, 0.25, 0.5, 0.75, 1.0 }) {
        for (int plugged = 0; plugged < 2; ++plugged) {
            Engine e; e.reset(); const TopoGains t = topology(knob);
            double F1 = 0, F2 = 0, mx = 0;
            const double raw1 = 0.4, raw2 = 0.2;
            for (int i = 0; i < (int) (0.3 * fs); ++i) e.step(raw1, raw2, plugged != 0, c1, c2, t, F1, F2, mx);
            // independent expectation
            const double eF1 = H1 * raw1;
            const double in2 = plugged ? raw2 : (1.0 - std::min(1.0, std::max(0.0, (5.2466 * knob - 0.1282) / 2.5))) * eF1 + std::min(1.0, std::max(0.0, (5.2466 * knob - 0.1282) / 2.5)) * raw1;
            const double eF2 = H2 * in2;
            const double g2 = std::min(1.0, std::max(0.0, (5.2466 * knob - 0.1282) / 5.0));
            const double eMix = (1.0 - g2) * eF2 + g2 * eF1;
            CHECK(std::fabs(F1 - eF1) < 0.002, "knob %g plugged %d: F1 %g expected %g", knob, plugged, F1, eF1);
            CHECK(std::fabs(F2 - eF2) < 0.002, "knob %g plugged %d: F2 %g expected %g", knob, plugged, F2, eF2);
            CHECK(std::fabs(mx - eMix) < 0.002, "knob %g plugged %d: mix %g expected %g", knob, plugged, mx, eMix);
        }
    }
    // end points in words
    { Engine e; e.reset(); double F1 = 0, F2 = 0, mx = 0; const TopoGains t = topology(0.0);
      for (int i = 0; i < 20000; ++i) e.step(0.3, 0, false, c1, c1, t, F1, F2, mx);
      CHECK(std::fabs(mx - F2) < 1e-9 && std::fabs(F2 - H1 * F1) < 0.002, "topology 0 should be serial: mix = F2 = H*F1"); }
    { Engine e; e.reset(); double F1 = 0, F2 = 0, mx = 0; const TopoGains t = topology(1.0);
      for (int i = 0; i < 20000; ++i) e.step(0.3, 0, false, c1, c2, t, F1, F2, mx);
      CHECK(std::fabs(mx - F1) < 1e-9, "topology 1: mix should be filter 1 alone"); }
}

static void testStability() {
    printf("-- stability under extreme settings\n");
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    int bad = 0; double worstOut = 0;
    for (int trial = 0; trial < 40; ++trial) {
        const double fs = (trial % 2) ? 96000.0 : 176400.0;
        const double hz1 = 5.0 * std::pow(24000.0 / 5.0, u(rng)), hz2 = 5.0 * std::pow(24000.0 / 5.0, u(rng));
        FilterCoefs c1 = makeCoefs(hz1, u(rng) < 0.5 ? 1.0 : u(rng), u(rng), u(rng), u(rng), fs);
        FilterCoefs c2 = makeCoefs(hz2, u(rng) < 0.5 ? 1.0 : u(rng), u(rng), u(rng), u(rng), fs);
        const TopoGains t = topology(u(rng));
        Engine e; e.reset();
        const double amp = 12.0 * u(rng);
        for (int i = 0; i < (int) (1.5 * fs); ++i) {
            const double x = (i < fs * 0.75) ? amp * (2.0 * u(rng) - 1.0) : (i % 2000 < 1000 ? amp : -amp);
            double F1, F2, mx; e.step(x, x * 0.5, (trial % 3) == 0, c1, c2, t, F1, F2, mx);
            worstOut = std::max(worstOut, std::max(std::fabs(mx), std::max(std::fabs(F1), std::fabs(F2))));
            if (!std::isfinite(mx) || !std::isfinite(F1) || !std::isfinite(F2) || std::fabs(mx) > kRail + 1e-9 || std::fabs(F1) > 5.71 || std::fabs(F2) > 5.71) { ++bad; break; }
        }
    }
    CHECK(bad == 0, "%d of 40 random trials went non-finite or past the limits", bad);
    printf("   largest output seen %.3f V\n", worstOut);
}

static void testOversampler() {
    printf("-- oversampler\n");
    Oversampler2x os; const double fs = 48000.0;
    for (double f : { 100.0, 1000.0, 8000.0 }) {
        os.reset(); double s = 0, e = 0; const int n = 9600;
        std::vector<double> in(n), outv(n);
        for (int i = 0; i < n; ++i) { in[i] = std::sin(2 * PI * f * i / fs); double a, b; os.up(in[i], a, b); outv[i] = os.down(a, b); }
        const int lat = Oversampler2x::latencySamples();
        for (int i = n / 2; i < n; ++i) { s += outv[i] * outv[i]; e += in[i - lat] * in[i - lat]; }
        CHECK(std::fabs(db(std::sqrt(s / e))) < 0.5, "gain at %g Hz = %g dB", f, db(std::sqrt(s / e)));
        double d = 0; for (int i = n / 2; i < n; ++i) d += (outv[i] - in[i - lat]) * (outv[i] - in[i - lat]);
        CHECK(std::sqrt(d / e) < 0.05, "shape/latency mismatch at %g Hz: %g", f, std::sqrt(d / e));
    }
}

// the whole plug-in path (oversampling, FM, link, output scaling)
static void testUnit() {
    printf("-- whole unit: DC gain, outputs, FM, link\n");
    const double fs = 48000;
    // DC gain: LP, no resonance, level unity, topology 1 => mix = filter 1
    {
        Unit u; u.reset(); Params p; p.hz1 = p.hz2 = 3000; p.reso1 = p.reso2 = 0; p.morph1 = p.morph2 = 0; p.topo = 1.0;
        float mx = 0, o1 = 0, o2 = 0;
        for (int i = 0; i < 20000; ++i) u.process(0.08, 0.04, true, p, fs, 0, 0, 1.0, mx, o1, o2);
        FilterCoefs c = makeCoefs(3000, 0, 0, 0.62, 0, 96000);
        const double H = KOUT * KA_CLEAN * c.gIn;
        CHECK(std::fabs(o1 - H * 0.08) < 0.0015, "o1 = %g, expected %g", o1, H * 0.08);
        CHECK(std::fabs(o2 - H * 0.04) < 0.0015, "o2 = %g, expected %g (input 2 plugged)", o2, H * 0.04);
        CHECK(std::fabs(mx - o1) < 1e-6, "mix = %g should equal o1 = %g at topology 1", mx, o1);
        // output gain
        for (int i = 0; i < 100; ++i) u.process(0.08, 0.04, true, p, fs, 0, 0, 0.5, mx, o1, o2);
        CHECK(std::fabs(o1 - 0.5 * H * 0.08) < 0.0015, "output gain: o1 = %g", o1);
    }
    // FM: +1 octave on filter 1 should equal setting the frequency twice as high
    {
        Params a, b; a.hz1 = 1000; b.hz1 = 2000; a.reso1 = b.reso1 = 0.3; a.morph1 = b.morph1 = 0; a.topo = b.topo = 1.0;
        Unit ua, ub; ua.reset(); ub.reset();
        double eN = 0, eD = 0;
        for (int i = 0; i < 24000; ++i) {
            const double x = 0.02 * std::sin(2 * PI * 1500.0 * i / fs);
            float m1, a1, a2, m2, b1, b2;
            ua.process(x, x, false, a, fs, 1.0, 0, 1.0, m1, a1, a2);
            ub.process(x, x, false, b, fs, 0.0, 0, 1.0, m2, b1, b2);
            if (i > 12000) { eN += (a1 - b1) * (a1 - b1); eD += b1 * b1; }
        }
        CHECK(std::sqrt(eN / eD) < 0.01, "FM +1 octave differs from doubling the frequency by %g", std::sqrt(eN / eD));
    }
    // FM on filter 2 (not linked): +1 octave on FM 2 should equal setting filter 2 twice as high, and leave filter 1 alone
    {
        Params a, b; a.hz1 = b.hz1 = 900; a.hz2 = 700; b.hz2 = 1400; a.reso1 = a.reso2 = b.reso1 = b.reso2 = 0.3; a.topo = b.topo = 1.0;
        Unit ua, ub; ua.reset(); ub.reset(); double eN = 0, eD = 0, e1 = 0;
        for (int i = 0; i < 24000; ++i) {
            const double x = 0.02 * std::sin(2 * PI * 1100.0 * i / fs);
            float m1, a1, a2, m2, b1, b2;
            ua.process(x, x, true, a, fs, 0, 1.0, 1.0, m1, a1, a2);
            ub.process(x, x, true, b, fs, 0, 0.0, 1.0, m2, b1, b2);
            if (i > 12000) { eN += (a2 - b2) * (a2 - b2); eD += b2 * b2; e1 += (a1 - b1) * (a1 - b1); }
        }
        CHECK(std::sqrt(eN / eD) < 0.01, "FM 2 +1 octave differs from doubling filter 2's frequency by %g", std::sqrt(eN / eD));
        CHECK(e1 < 1e-12, "FM 2 changed filter 1 (%g)", e1);
    }
    // link: filter 2 follows filter 1 at +1 octave
    {
        Params a, b; a.hz1 = 800; a.link = true; a.offsetOct = 1.0; a.reso1 = a.reso2 = 0.3; a.topo = 1.0;
        b.hz1 = 800; b.hz2 = 1600; b.reso1 = b.reso2 = 0.3; b.topo = 1.0;
        Unit ua, ub; ua.reset(); ub.reset(); double eN = 0, eD = 0;
        for (int i = 0; i < 24000; ++i) {
            const double x = 0.02 * std::sin(2 * PI * 1200.0 * i / fs);
            float m1, a1, a2, m2, b1, b2;
            ua.process(x, x, true, a, fs, 0, 0, 1.0, m1, a1, a2);
            ub.process(x, x, true, b, fs, 0, 0, 1.0, m2, b1, b2);
            if (i > 12000) { eN += (a2 - b2) * (a2 - b2); eD += b2 * b2; }
        }
        CHECK(std::sqrt(eN / eD) < 1e-6, "link +1 oct differs from setting filter 2 to 1600 Hz by %g", std::sqrt(eN / eD));
    }
    // link with FM: FM 1 moves both filters; FM 2 moves only filter 2 (on top of the offset)
    {
        Params a, b; a.hz1 = 800; a.link = true; a.offsetOct = 0.5; a.reso1 = a.reso2 = 0.3; a.topo = 1.0;
        b.hz1 = 800 * 2.0; b.hz2 = 800 * 2.0 * std::pow(2.0, 0.5 + 0.25); b.reso1 = b.reso2 = 0.3; b.topo = 1.0;
        Unit ua, ub; ua.reset(); ub.reset(); double eN = 0, eD = 0, eN1 = 0, eD1 = 0;
        for (int i = 0; i < 24000; ++i) {
            const double x = 0.02 * std::sin(2 * PI * 1200.0 * i / fs);
            float m1, a1, a2, m2, b1, b2;
            ua.process(x, x, true, a, fs, 1.0, 0.25, 1.0, m1, a1, a2);         // FM 1 = +1 oct, FM 2 = +0.25 oct
            ub.process(x, x, true, b, fs, 0.0, 0.0, 1.0, m2, b1, b2);          // same thing set with the knobs (filter 2 = 2 * 800 * 2^(0.5 + 0.25))
            if (i > 12000) { eN += (a2 - b2) * (a2 - b2); eD += b2 * b2; eN1 += (a1 - b1) * (a1 - b1); eD1 += b1 * b1; }
        }
        CHECK(std::sqrt(eN1 / eD1) < 1e-6, "linked FM: filter 1 differs by %g", std::sqrt(eN1 / eD1));
        CHECK(std::sqrt(eN / eD) < 1e-6, "linked FM: filter 2 differs by %g", std::sqrt(eN / eD));
    }
}

static void testSpeed() {
    Unit u; u.reset(); Params p; p.hz1 = 1500; p.hz2 = 700; p.reso1 = p.reso2 = 0.7; p.morph1 = 0.3; p.morph2 = 0.8; p.drive1 = 0.8; p.drive2 = 0.5; p.topo = 0.3;
    float mx, o1, o2; const int n = 48000 * 10;
    auto t0 = std::chrono::steady_clock::now();
    double acc = 0;
    for (int i = 0; i < n; ++i) { u.process(0.3 * std::sin(0.05 * i), 0.0, false, p, 48000.0, 0, 0, 1.0, mx, o1, o2); acc += mx; }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("-- speed: 10 s of audio in %.3f s (%.1f %% of one core for one instance, stereo-linked unit); checksum %g\n", secs, 100.0 * secs / 10.0, acc);
}

int main() {
    testConstants();
    testDiode();
    testInputStage();
    testOversampler();
    testFrequencyResponse();
    testSlopes();
    testSelfOscillation();
    testTopology();
    testStability();
    testUnit();
    testSpeed();
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
