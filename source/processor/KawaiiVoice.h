/**
 * KawaiiVoice.h — 32-Partial Additive Voice with Per-Partial ADSR + Cytomic SVF
 *
 * Each voice has 32 sine oscillators in a harmonic series.
 * Each partial has its own:
 *   - Level (gain knob)
 *   - ADSR envelope (independent shaping per harmonic)
 *
 * After the partials are summed, the signal passes through a Cytomic SVF
 * filter with 9 modes (LP/HP/BP/Notch/Peak/Allpass/Bell/LowShelf/HighShelf),
 * its own ADSR envelope, envelope depth, and keyboard tracking.
 *
 * Filter: Scalar double-precision port of Surge XT's CytomicSVF.
 * Algorithm: Andy Simper, "Solving the continuous SVF equations using
 *   trapezoidal integration and equivalent currents"
 *   https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
 *
 * Coefficient interpolation: Surge XT's setCoeffForBlock pattern —
 *   target coefficients computed once per 32-sample sub-block, then
 *   linearly interpolated per-sample (a1 += da1). Eliminates zipper noise.
 */

#pragma once

#include <cmath>
#include <algorithm>
#include <array>
#include "../entry/KawaiiCids.h"

namespace Steinberg {
namespace Vst {
namespace Kawaii {

// ============================================================================
// ADSR Envelope — Analog RC-style curves
//
// Real analog synths use capacitor charge/discharge curves, not linear ramps.
// This models those curves using one-pole exponential coefficients:
//
//   Attack:  Concave curve — charges toward an overshoot target (1.5) so the
//            approach to 1.0 has a natural rounded shape, like a capacitor
//            charging through a resistor. coeff = 1 - e^(-1/(t*sr))
//
//   Decay:   Exponential fall toward sustain level — like a cap discharging
//            through a resistor to a voltage rail (the sustain level).
//
//   Release: Exponential fall toward zero — same RC discharge shape.
//
// The overshoot target (kAttackTarget) controls how concave the attack is:
//   - 1.0 = perfectly linear (no overshoot)
//   - 1.5 = gentle analog curve (Moog-ish)
//   - 2.0 = very concave (fast start, slow finish)
// ============================================================================

class ADSREnvelope
{
public:
    enum Stage { Attack, Decay, Sustain, Release, Idle };

    // Overshoot target for attack curve — higher = more concave
    static constexpr double kAttackTarget = 1.5;

    // Threshold for "close enough" to target (avoids infinite asymptote)
    static constexpr double kSilenceThreshold = 0.001;

    ADSREnvelope()
        : stage(Idle), currentValue(0.0)
        , attackCoeff(0.01), decayCoeff(0.01), releaseCoeff(0.01)
        , sustainLevel(0.7)
        , sampleRate(44100.0)
    {}

    void setSampleRate(double sr) { sampleRate = sr; }

    // Convert time in seconds to a one-pole RC coefficient
    // coeff = 1 - e^(-1/(time_in_seconds * sampleRate))
    // Smaller coeff = slower approach, larger = faster
    void setAttack(double seconds)
    {
        seconds = std::max(0.001, seconds);
        attackCoeff = 1.0 - std::exp(-1.0 / (seconds * sampleRate));
    }

    void setDecay(double seconds)
    {
        seconds = std::max(0.001, seconds);
        decayCoeff = 1.0 - std::exp(-1.0 / (seconds * sampleRate));
    }

    void setSustain(double level)
    {
        sustainLevel = std::clamp(level, 0.0, 1.0);
    }

    void setRelease(double seconds)
    {
        seconds = std::max(0.001, seconds);
        releaseCoeff = 1.0 - std::exp(-1.0 / (seconds * sampleRate));
    }

    void noteOn()  { stage = Attack; }
    void noteOff() { if (stage != Idle) stage = Release; }

    double process()
    {
        switch (stage)
        {
            case Attack:
                // Charge toward overshoot target — creates concave curve to 1.0
                currentValue += (kAttackTarget - currentValue) * attackCoeff;
                if (currentValue >= 1.0)
                {
                    currentValue = 1.0;
                    stage = Decay;
                }
                break;

            case Decay:
                // Exponential approach toward sustain level (RC discharge)
                currentValue += (sustainLevel - currentValue) * decayCoeff;
                if (std::abs(currentValue - sustainLevel) < kSilenceThreshold)
                {
                    currentValue = sustainLevel;
                    stage = Sustain;
                }
                break;

            case Sustain:
                currentValue = sustainLevel;
                break;

            case Release:
                // Exponential decay toward zero (RC discharge)
                currentValue += (0.0 - currentValue) * releaseCoeff;
                if (currentValue <= kSilenceThreshold)
                {
                    currentValue = 0.0;
                    stage = Idle;
                }
                break;

            case Idle:
                currentValue = 0.0;
                break;
        }
        return currentValue;
    }

    bool isActive() const { return stage != Idle; }
    void reset() { stage = Idle; currentValue = 0.0; }

private:
    Stage  stage;
    double currentValue;
    double attackCoeff, decayCoeff, releaseCoeff;
    double sustainLevel;
    double sampleRate;
};

// ============================================================================
// Parameter Smoother — one-pole exponential for click-free knob movement
//
// Without smoothing, filter cutoff/resonance jump at block boundaries
// when the user moves a knob, causing audible stepping/clicks.
// This interpolates toward the target value each sample.
// ~5ms time constant — matches Surge XT's SurgeLag for snappy response.
// ============================================================================

class ParamSmoother
{
public:
    ParamSmoother(double initial = 0.0)
        : current(initial), target(initial), coeff(0.01)
    {}

    void setSampleRate(double sr)
    {
        // ~5ms smoothing time — matches Surge XT's lag behavior.
        // Fast enough to track rapid knob sweeps, slow enough to
        // eliminate per-block stepping artifacts.
        // coeff = 1 - e^(-2π / (time_in_samples))
        // 5ms at sr=44100 ≈ 220 samples → coeff ≈ 0.028
        double timeSamples = 0.005 * sr;
        coeff = 1.0 - std::exp(-2.0 * M_PI / timeSamples);
    }

    void setTarget(double t) { target = t; }

    double process()
    {
        current += (target - current) * coeff;
        return current;
    }

    // Jump to target immediately (e.g. on note-on to avoid filter sweep artifacts)
    void snap() { current = target; }

    double getCurrent() const { return current; }

private:
    double current;
    double target;
    double coeff;
};

// ============================================================================
// Cytomic SVF — Scalar double-precision port of Surge XT's CytomicSVF
//
// Direct port of sst-filters/include/sst/filters/CytomicSVF.h to scalar
// double precision. Same algorithm, same resonance mapping, all 9 modes.
//
// Two processing modes:
//   1. setCoeff() + step()       — immediate coefficient update
//   2. setCoeffForBlock() + processBlockStep() — Surge XT style interpolation
//
// Resonance: 0.0 = no resonance, 0.98 = maximum (self-oscillation)
//   k = 2 - 2*res  (Surge mapping — linear, natural feel)
//
// Algorithm: Andy Simper / Cytomic
// https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
// ============================================================================

class CytomicSVF
{
public:
    enum class Mode : int
    {
        LP, HP, BP, Notch, Peak, Allpass, Bell, LowShelf, HighShelf
    };

    CytomicSVF()
        : ic1eq(0.0), ic2eq(0.0)
        , g(0.0), k(2.0), gk(2.0)
        , a1(0.0), a2(0.0), a3(0.0)
        , m0(0.0), m1(0.0), m2(1.0)
        , da1(0.0), da2(0.0), da3(0.0)
        , dm0(0.0), dm1(0.0), dm2(0.0)
        , firstBlock(true)
    {}

    // -----------------------------------------------------------------
    // Compute filter coefficients from frequency, resonance, and mode.
    //
    // freq: cutoff frequency in Hz
    // res:  resonance 0.0 .. 0.98 (Surge XT mapping: k = 2 - 2*res)
    // srInv: 1.0 / sampleRate
    // bellShelfAmp: only used for Bell/LowShelf/HighShelf modes (linear amplitude)
    // -----------------------------------------------------------------

    void setCoeff(Mode mode, double freq, double res, double srInv,
                  double bellShelfAmp = 1.0)
    {
        // Guard: clamp to below Nyquist for stability
        double conorm = std::clamp(freq * srInv, 0.0, 0.499);
        res = std::clamp(res, 0.0, 0.98);
        bellShelfAmp = std::max(bellShelfAmp, 0.001);

        // g = tan(π * freq / sampleRate) — bilinear/trapezoidal warping
        g = std::tan(M_PI * conorm);

        // k = damping: 2 = no resonance, 0.04 = near self-oscillation
        k = 2.0 - 2.0 * res;

        if (mode == Mode::Bell)
            k /= bellShelfAmp;

        // Derived coefficients
        gk = g + k;
        a1 = 1.0 / (1.0 + g * gk);
        a2 = g * a1;
        a3 = g * a2;

        // Mix coefficients select the output mode (LP/HP/BP/etc.)
        setMixCoeffs(mode, bellShelfAmp);
    }

    // -----------------------------------------------------------------
    // Block-based coefficient interpolation (Surge XT pattern).
    //
    // Call once per sub-block (~32 samples). Computes TARGET coefficients,
    // then sets up per-sample linear deltas so processBlockStep() smoothly
    // ramps from the previous coefficients to the new ones.
    //
    // This is THE key to smooth filter sweeps:
    //   - tan() called once per sub-block, not per sample
    //   - Coefficients change at constant rate within sub-block
    //   - No zipper noise, no stepping, no nonlinear coefficient jumps
    // -----------------------------------------------------------------

    void setCoeffForBlock(Mode mode, double freq, double res, double srInv,
                          int blockSize, double bellShelfAmp = 1.0)
    {
        // Save current coefficients as "prior"
        double a1p = a1, a2p = a2, a3p = a3;
        double m0p = m0, m1p = m1, m2p = m2;

        // Compute new target coefficients
        setCoeff(mode, freq, res, srInv, bellShelfAmp);

        // First time: snap to target, no interpolation (no valid prior state)
        if (firstBlock)
        {
            a1p = a1; a2p = a2; a3p = a3;
            m0p = m0; m1p = m1; m2p = m2;
            firstBlock = false;
        }

        // Compute per-sample deltas for linear interpolation
        double inv = 1.0 / blockSize;
        da1 = (a1 - a1p) * inv;
        da2 = (a2 - a2p) * inv;
        da3 = (a3 - a3p) * inv;
        dm0 = (m0 - m0p) * inv;
        dm1 = (m1 - m1p) * inv;
        dm2 = (m2 - m2p) * inv;

        // Reset to prior values — processBlockStep() ramps from here
        a1 = a1p;  a2 = a2p;  a3 = a3p;
        m0 = m0p;  m1 = m1p;  m2 = m2p;
    }

    // -----------------------------------------------------------------
    // Process one sample through the filter (no coefficient advancement).
    // -----------------------------------------------------------------

    double step(double vin)
    {
        double v3 = vin - ic2eq;
        double v1 = a1 * ic1eq + a2 * v3;
        double v2 = ic2eq + a2 * ic1eq + a3 * v3;

        ic1eq = 2.0 * v1 - ic1eq;
        ic2eq = 2.0 * v2 - ic2eq;

        return m0 * vin + m1 * v1 + m2 * v2;
    }

    // -----------------------------------------------------------------
    // Process one sample + advance coefficients by one step.
    // Call in tight loop after setCoeffForBlock().
    // -----------------------------------------------------------------

    double processBlockStep(double vin)
    {
        double out = step(vin);

        // Advance coefficients toward target (linear ramp)
        a1 += da1;  a2 += da2;  a3 += da3;
        m0 += dm0;  m1 += dm1;  m2 += dm2;

        return out;
    }

    void init()
    {
        ic1eq = 0.0;
        ic2eq = 0.0;
        firstBlock = true;
    }

private:
    // Set mix coefficients for each filter mode.
    // Output = m0*vin + m1*v1(bandpass) + m2*v2(lowpass)
    // Exact copy of Surge XT's CytomicSVF mode coefficient logic.
    void setMixCoeffs(Mode mode, double bellShelfAmp)
    {
        switch (mode)
        {
        case Mode::LP:
            m0 = 0.0;  m1 = 0.0;  m2 = 1.0;
            break;
        case Mode::BP:
            m0 = 0.0;  m1 = 1.0;  m2 = 0.0;
            break;
        case Mode::HP:
            m0 = 1.0;  m1 = -k;   m2 = -1.0;
            break;
        case Mode::Notch:
            m0 = 1.0;  m1 = -k;   m2 = 0.0;
            break;
        case Mode::Peak:
            m0 = 1.0;  m1 = -k;   m2 = -2.0;
            break;
        case Mode::Allpass:
            m0 = 1.0;  m1 = -2.0 * k;  m2 = 0.0;
            break;
        case Mode::Bell:
            m0 = 1.0;
            m1 = k * (bellShelfAmp * bellShelfAmp - 1.0);
            m2 = 0.0;
            break;
        case Mode::LowShelf:
            m0 = 1.0;
            m1 = k * (bellShelfAmp - 1.0);
            m2 = bellShelfAmp * bellShelfAmp - 1.0;
            break;
        case Mode::HighShelf:
            m0 = bellShelfAmp * bellShelfAmp;
            m1 = k * (1.0 - bellShelfAmp) * bellShelfAmp;
            m2 = 1.0 - bellShelfAmp * bellShelfAmp;
            break;
        default:
            m0 = 0.0;  m1 = 0.0;  m2 = 1.0;
            break;
        }
    }

    // Integrator states
    double ic1eq, ic2eq;

    // Filter coefficients
    double g, k, gk;
    double a1, a2, a3;

    // Mix coefficients (select LP/HP/BP/etc.)
    double m0, m1, m2;

    // Per-sample deltas for block-based coefficient interpolation
    double da1, da2, da3;
    double dm0, dm1, dm2;
    bool firstBlock;
};

// ============================================================================
// Partial — one sine oscillator + its own ADSR + level
// ============================================================================

struct Partial
{
    // Oscillator state
    double phase = 0.0;
    double frequency = 0.0;

    // Per-partial controls (set by processor from params)
    double level = 1.0;
    ADSREnvelope envelope;

    double process(double sampleRate)
    {
        if (!envelope.isActive())
            return 0.0;

        double envValue = envelope.process();
        double output = std::sin(2.0 * M_PI * phase) * level * envValue;

        phase += frequency / sampleRate;
        if (phase >= 1.0)
            phase -= 1.0;

        return output;
    }

    void reset()
    {
        phase = 0.0;
        envelope.reset();
    }
};

// ============================================================================
// KawaiiVoice — 32 partials with independent ADSR + Cytomic SVF filter
// ============================================================================

class KawaiiVoice
{
public:
    KawaiiVoice()
        : noteNumber(-1), velocity(0.0), sampleRate(44100.0)
        , cutoffSmoother(1.0), resoSmoother(0.0)
        , filterEnvDepth(0.0), filterKeytrack(0.0)
        , filterMode(CytomicSVF::Mode::LP)
    {}

    void setSampleRate(double sr)
    {
        sampleRate = sr;
        srInv = 1.0 / sr;
        for (auto& p : partials)
            p.envelope.setSampleRate(sr);
        filterEnvelope.setSampleRate(sr);
        cutoffSmoother.setSampleRate(sr);
        resoSmoother.setSampleRate(sr);
    }

    void noteOn(int note, double vel)
    {
        noteNumber = note;
        velocity = vel;

        double fundamental = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        double nyquist = sampleRate / 2.0;

        for (int i = 0; i < kMaxPartials; i++)
        {
            double freq = fundamental * (i + 1);
            partials[i].frequency = (freq < nyquist) ? freq : 0.0;
            partials[i].phase = 0.0;
            partials[i].envelope.noteOn();
        }

        // Start filter envelope on note-on
        filterEnvelope.noteOn();
        filter.init();   // clean filter state for new note
        cutoffSmoother.snap();  // no sweep artifact on new note
        resoSmoother.snap();
    }

    void noteOff()
    {
        for (auto& p : partials)
            p.envelope.noteOff();
        filterEnvelope.noteOff();
    }

    // CPU path: per-sample processing with immediate coefficient updates
    void process(double* outLeft, double* outRight)
    {
        // 1. Sum all partials (each has its own level × ADSR)
        double sum = 0.0;
        for (auto& p : partials)
        {
            sum += p.process(sampleRate);
        }

        // Scale by velocity; normalize by partial count to prevent clipping
        double sample = sum * velocity / static_cast<double>(kMaxPartials);

        // 2. Apply Cytomic SVF filter with per-sample smoothed parameters
        double envValue = filterEnvelope.process();
        double smoothedNorm = cutoffSmoother.process();
        double smoothedReso = resoSmoother.process();

        double cutoffHz = computeEffectiveCutoff(smoothedNorm, envValue);
        double res = std::clamp(smoothedReso, 0.0, 0.98);

        filter.setCoeff(filterMode, cutoffHz, res, srInv);
        sample = filter.step(sample);

        *outLeft  = sample;
        *outRight = sample;
    }

    bool isActive() const
    {
        for (const auto& p : partials)
            if (p.envelope.isActive()) return true;
        return false;
    }

    int getNoteNumber() const { return noteNumber; }
    double getVelocity() const { return velocity; }

    // --- Filter parameter setters (called by processor each block) ---
    void setFilterCutoffNorm(double norm) { cutoffSmoother.setTarget(norm); }
    void setFilterResonance(double res)  { resoSmoother.setTarget(res); }
    void setFilterEnvDepth(double depth) { filterEnvDepth = depth; }
    void setFilterKeytrack(double amt)   { filterKeytrack = amt; }

    // Map our 4 UI filter types to CytomicSVF modes
    void setFilterType(FilterType type)
    {
        switch (type)
        {
            case kFilterLP:    filterMode = CytomicSVF::Mode::LP;    break;
            case kFilterHP:    filterMode = CytomicSVF::Mode::HP;    break;
            case kFilterBP:    filterMode = CytomicSVF::Mode::BP;    break;
            case kFilterNotch: filterMode = CytomicSVF::Mode::Notch; break;
            default:           filterMode = CytomicSVF::Mode::LP;    break;
        }
    }

    void setFilterEnvAttack(double sec)  { filterEnvelope.setAttack(sec); }
    void setFilterEnvDecay(double sec)   { filterEnvelope.setDecay(sec); }
    void setFilterEnvSustain(double lvl) { filterEnvelope.setSustain(lvl); }
    void setFilterEnvRelease(double sec) { filterEnvelope.setRelease(sec); }

    // --- Hybrid GPU+CPU pipeline helpers ---
    // When GPU computes per-voice partial sums, the processor drives the
    // filter from outside. These expose per-sample filter state advancement
    // that normally happens inside process().

    // Advance filter envelope by one sample, return envelope value
    double processFilterEnvelope() { return filterEnvelope.process(); }

    // Advance cutoff smoother by one sample, return smoothed normalized cutoff
    double processFilterCutoffSmooth() { return cutoffSmoother.process(); }

    // Advance resonance smoother by one sample, return smoothed resonance
    double processFilterResoSmooth() { return resoSmoother.process(); }

    // Read-only access to filter modulation depths
    double getFilterEnvDepth() const { return filterEnvDepth; }
    double getFilterKeytrack() const { return filterKeytrack; }

    // Compute effective cutoff Hz from smoothed normalized cutoff + modulation
    double computeEffectiveCutoff(double smoothedNorm, double envValue) const
    {
        // 20 * 1000^norm: norm=0 → 20 Hz, norm=0.5 → 632 Hz, norm=1 → 20 kHz
        double baseCutoffHz = 20.0 * std::pow(1000.0, smoothedNorm);

        // Env depth is bipolar: -1.0 to +1.0 — modulates cutoff by up to ±10kHz
        double envMod = filterEnvDepth * envValue * 10000.0;

        // Keytrack: 0 = no tracking, 1 = full (100 Hz/semitone from C3 = MIDI 60)
        double keyMod = filterKeytrack * (noteNumber - 60) * 100.0;

        return std::clamp(baseCutoffHz + envMod + keyMod, 20.0, 20000.0);
    }

    // --- Sub-block coefficient interpolation (Surge XT pattern) ---
    // Call once per sub-block (~32 samples) to set up per-sample
    // coefficient interpolation for zipper-free filter sweeps.
    void prepareFilterBlock(double cutoffHz, double res, int blockSize)
    {
        filter.setCoeffForBlock(filterMode, cutoffHz,
                                std::clamp(res, 0.0, 0.98),
                                srInv, blockSize);
    }

    // Process one sample through the filter with coefficient interpolation.
    // Call in tight loop after prepareFilterBlock().
    double filterBlockStep(double sample)
    {
        return filter.processBlockStep(sample);
    }

    // Public so the processor can set per-partial ADSR and level directly
    std::array<Partial, kMaxPartials> partials;

private:
    int    noteNumber;
    double velocity;
    double sampleRate;
    double srInv = 1.0 / 44100.0;

    // Filter state (per-voice)
    CytomicSVF filter;
    ADSREnvelope filterEnvelope;
    ParamSmoother cutoffSmoother;   // smoothed cutoff in normalized 0–1
    ParamSmoother resoSmoother;     // smoothed resonance 0–1
    double filterEnvDepth;          // bipolar: -1.0 to +1.0
    double filterKeytrack;          // 0.0 to 1.0
    CytomicSVF::Mode filterMode;
};

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
