/**
 * KawaiiVoice.h — 32-Partial Additive Voice with Per-Partial ADSR + ZDF SVF Filter
 *
 * Each voice has 32 sine oscillators in a harmonic series.
 * Each partial has its own:
 *   - Level (gain knob)
 *   - ADSR envelope (independent shaping per harmonic)
 *
 * After the partials are summed, the signal passes through a Cytomic ZDF
 * State Variable Filter (LP/HP/BP/Notch) with its own ADSR envelope,
 * envelope depth, and keyboard tracking.
 *
 * ZDF SVF reference: Andy Simper / Cytomic
 * https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
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
// ADSR Envelope — used for both per-partial amp and the filter
// ============================================================================

class ADSREnvelope
{
public:
    enum Stage { Attack, Decay, Sustain, Release, Idle };

    ADSREnvelope()
        : stage(Idle), currentValue(0.0)
        , attackTime(0.01), decayTime(0.1)
        , sustainLevel(0.7), releaseTime(0.3)
        , sampleRate(44100.0)
    {}

    void setSampleRate(double sr) { sampleRate = sr; }
    void setAttack(double seconds)  { attackTime  = std::max(0.001, seconds); }
    void setDecay(double seconds)   { decayTime   = std::max(0.001, seconds); }
    void setSustain(double level)   { sustainLevel = std::clamp(level, 0.0, 1.0); }
    void setRelease(double seconds) { releaseTime = std::max(0.001, seconds); }

    void noteOn()  { stage = Attack; }
    void noteOff() { if (stage != Idle) stage = Release; }

    double process()
    {
        switch (stage)
        {
            case Attack:
                currentValue += 1.0 / (attackTime * sampleRate);
                if (currentValue >= 1.0) { currentValue = 1.0; stage = Decay; }
                break;
            case Decay:
                currentValue -= (1.0 - sustainLevel) / (decayTime * sampleRate);
                if (currentValue <= sustainLevel) { currentValue = sustainLevel; stage = Sustain; }
                break;
            case Sustain:
                currentValue = sustainLevel;
                break;
            case Release:
                currentValue -= currentValue / (releaseTime * sampleRate);
                if (currentValue <= 0.001) { currentValue = 0.0; stage = Idle; }
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
    double attackTime, decayTime, sustainLevel, releaseTime;
    double sampleRate;
};

// ============================================================================
// Parameter Smoother — one-pole exponential for click-free knob movement
//
// Without smoothing, filter cutoff/resonance jump at block boundaries
// when the user moves a knob, causing audible stepping/clicks.
// This interpolates toward the target value each sample (~5ms time constant).
// ============================================================================

class ParamSmoother
{
public:
    ParamSmoother(double initial = 0.0)
        : current(initial), target(initial), coeff(0.01)
    {}

    void setSampleRate(double sr)
    {
        // ~5ms smoothing time: coeff = 1 - e^(-2π / (time_in_samples))
        // 5ms at sr=44100 ≈ 220.5 samples → coeff ≈ 0.028
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
// Cytomic ZDF State Variable Filter (2-pole, 12dB/oct)
//
// Topology-Preserving Transform (TPT) with trapezoidal integration.
// One computation gives LP, HP, BP, or Notch — selected via mix coefficients.
// Stable under fast modulation, no artifacts from parameter changes.
//
// Reference: Andy Simper, "Linear Trapezoidal Integrated SVF"
// https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
// ============================================================================

class ZdfSvf
{
public:
    ZdfSvf()
        : g(0.0), k(0.0), a1(0.0), a2(0.0), a3(0.0)
        , m0(0.0), m1(0.0), m2(1.0)  // default: lowpass
        , ic1eq(0.0), ic2eq(0.0)
    {}

    // Compute filter coefficients from frequency and Q.
    // Call this every sample (or at least every time cutoff/reso changes).
    // g = tan(π * freq / sampleRate) — the bilinear/trapezoidal warping
    // k = 1/Q — damping factor (higher k = less resonance)
    void setCoefficients(double sampleRate, double freq, double Q)
    {
        g = std::tan(M_PI * std::clamp(freq, 20.0, 20000.0) / sampleRate);
        k = 1.0 / std::max(Q, 0.5);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    // Set the filter type via mix coefficients.
    // The output is: m0*v0 + m1*v1 + m2*v2
    // where v0=input, v1=bandpass, v2=lowpass
    void setType(FilterType type)
    {
        switch (type)
        {
            case kFilterLP:    m0 = 0.0;  m1 = 0.0;  m2 = 1.0;  break;
            case kFilterHP:    m0 = 1.0;  m1 = -k;   m2 = -1.0; break;
            case kFilterBP:    m0 = 0.0;  m1 = k;    m2 = 0.0;  break;
            case kFilterNotch: m0 = 1.0;  m1 = -k;   m2 = 0.0;  break;
            default:           m0 = 0.0;  m1 = 0.0;  m2 = 1.0;  break;
        }
    }

    // Process one sample through the filter.
    // This is the core ZDF tick — two trapezoidal integrators with feedback.
    double processSample(double v0)
    {
        double v3 = v0 - ic2eq;
        double v1 = a1 * ic1eq + a2 * v3;
        double v2 = ic2eq + a2 * ic1eq + a3 * v3;

        // Update integrator states (trapezoidal rule)
        ic1eq = 2.0 * v1 - ic1eq;
        ic2eq = 2.0 * v2 - ic2eq;

        // Mix outputs: m0*input + m1*bandpass + m2*lowpass
        return m0 * v0 + m1 * v1 + m2 * v2;
    }

    void reset()
    {
        ic1eq = 0.0;
        ic2eq = 0.0;
    }

private:
    double g, k, a1, a2, a3;  // filter coefficients
    double m0, m1, m2;        // mix coefficients (select LP/HP/BP/Notch)
    double ic1eq, ic2eq;      // integrator states
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
// KawaiiVoice — 32 partials with independent ADSR + ZDF SVF filter
// ============================================================================

class KawaiiVoice
{
public:
    KawaiiVoice()
        : noteNumber(-1), velocity(0.0), sampleRate(44100.0)
        , cutoffSmoother(20000.0), resoSmoother(0.0)
        , filterEnvDepth(0.0), filterKeytrack(0.0)
        , filterType(kFilterLP)
    {}

    void setSampleRate(double sr)
    {
        sampleRate = sr;
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
        filter.reset();  // clean filter state for new note
        cutoffSmoother.snap();  // no sweep artifact on new note
        resoSmoother.snap();
    }

    void noteOff()
    {
        for (auto& p : partials)
            p.envelope.noteOff();
        filterEnvelope.noteOff();
    }

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

        // 2. Apply ZDF SVF filter with per-sample smoothed parameters
        double envValue = filterEnvelope.process();

        // Smooth cutoff and reso to avoid stepping when user moves knobs
        double smoothedCutoff = cutoffSmoother.process();
        double smoothedReso   = resoSmoother.process();

        // Env depth is bipolar: -1.0 to +1.0 — modulates cutoff by up to ±10kHz
        double envMod = filterEnvDepth * envValue * 10000.0;

        // Keytrack: 0 = no tracking, 1 = full (100 Hz/semitone from C3 = MIDI 60)
        double keyMod = filterKeytrack * (noteNumber - 60) * 100.0;

        double effectiveCutoff = std::clamp(smoothedCutoff + envMod + keyMod, 20.0, 20000.0);

        // Map smoothed resonance (0–1) to Q (0.5–25)
        double Q = 0.5 + smoothedReso * 24.5;

        filter.setCoefficients(sampleRate, effectiveCutoff, Q);
        filter.setType(filterType);
        sample = filter.processSample(sample);

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
    // Cutoff and reso go through smoothers for click-free knob movement
    void setFilterCutoff(double hz)      { cutoffSmoother.setTarget(hz); }
    void setFilterResonance(double res)  { resoSmoother.setTarget(res); }
    void setFilterEnvDepth(double depth) { filterEnvDepth = depth; }
    void setFilterKeytrack(double amt)   { filterKeytrack = amt; }
    void setFilterType(FilterType type)  { filterType = type; }

    void setFilterEnvAttack(double sec)  { filterEnvelope.setAttack(sec); }
    void setFilterEnvDecay(double sec)   { filterEnvelope.setDecay(sec); }
    void setFilterEnvSustain(double lvl) { filterEnvelope.setSustain(lvl); }
    void setFilterEnvRelease(double sec) { filterEnvelope.setRelease(sec); }

    // Public so the processor can set per-partial ADSR and level directly
    std::array<Partial, kMaxPartials> partials;

private:
    int    noteNumber;
    double velocity;
    double sampleRate;

    // Filter state (per-voice)
    ZdfSvf filter;
    ADSREnvelope filterEnvelope;
    ParamSmoother cutoffSmoother;   // smoothed cutoff in Hz
    ParamSmoother resoSmoother;     // smoothed resonance 0–1
    double filterEnvDepth;          // bipolar: -1.0 to +1.0
    double filterKeytrack;          // 0.0 to 1.0
    FilterType filterType;
};

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
