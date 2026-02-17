/**
 * KawaiiVoice.h — 16-Partial Additive Voice with Per-Partial ADSR
 *
 * Each voice has 16 sine oscillators in a harmonic series.
 * Each partial has its own:
 *   - Level (gain knob)
 *   - ADSR envelope (independent shaping per harmonic)
 *
 * This means you can make the fundamental sustain forever while the
 * upper harmonics decay quickly (organ-like), or have a bright attack
 * that mellows out (piano-like), etc.
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
// ADSR Envelope — one per partial
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
// KawaiiVoice — 16 partials, each with independent ADSR
// ============================================================================

class KawaiiVoice
{
public:
    KawaiiVoice()
        : noteNumber(-1), velocity(0.0), sampleRate(44100.0)
    {}

    void setSampleRate(double sr)
    {
        sampleRate = sr;
        for (auto& p : partials)
            p.envelope.setSampleRate(sr);
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
    }

    void noteOff()
    {
        for (auto& p : partials)
            p.envelope.noteOff();
    }

    void process(double* outLeft, double* outRight)
    {
        double sum = 0.0;
        for (auto& p : partials)
        {
            sum += p.process(sampleRate);
        }

        // Scale by velocity; normalize by partial count to prevent clipping
        double sample = sum * velocity / static_cast<double>(kMaxPartials);

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

    // Public so the processor can set per-partial ADSR and level directly
    std::array<Partial, kMaxPartials> partials;

private:
    int    noteNumber;
    double velocity;
    double sampleRate;
};

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
