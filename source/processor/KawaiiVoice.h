/**
 * KawaiiVoice.h — 32-Partial Additive Voice with Surge XT sst-filters
 *
 * Each voice has 32 sine oscillators in a harmonic series.
 * Each partial has its own:
 *   - Level (gain knob)
 *   - ADSR envelope (independent shaping per harmonic)
 *
 * After the partials are summed, the signal passes through one of
 * Surge XT's 33 filter types via the sst-filters++ library, with its
 * own ADSR envelope, envelope depth, and keyboard tracking.
 *
 * The sst-filters++ Filter uses SIMD-based QuadFilterUnit internally
 * (SSE on x86, NEON on ARM via SIMDE). We use processMonoSample()
 * for single-voice processing — one Filter instance per voice.
 *
 * Coefficient interpolation is handled by the library: coefficients
 * are computed once per 32-sample sub-block, then linearly interpolated
 * per-sample via internal deltaC mechanism. This is the same approach
 * Surge XT uses for zipper-free filter sweeps.
 */

#pragma once

#include <cmath>
#include <algorithm>
#include <array>
#include <vector>
#include "../entry/KawaiiCids.h"
#include "../params/KawaiiFilterTypes.h"

namespace Steinberg {
namespace Vst {
namespace Kawaii {

// Sub-block size for filter coefficient updates — matches Surge XT's BLOCK_SIZE.
// Coefficients recomputed every 32 samples (~0.7ms at 44.1kHz, ~1378×/sec).
static constexpr int kFilterBlockSize = 32;

// ============================================================================
// ADSR Envelope — Analog RC-style curves
// ============================================================================

class ADSREnvelope
{
public:
    enum Stage { Attack, Decay, Sustain, Release, Idle };

    static constexpr double kAttackTarget = 1.5;
    static constexpr double kSilenceThreshold = 0.001;

    ADSREnvelope()
        : stage(Idle), currentValue(0.0)
        , attackCoeff(0.01), decayCoeff(0.01), releaseCoeff(0.01)
        , sustainLevel(0.7)
        , sampleRate(44100.0)
    {}

    void setSampleRate(double sr) { sampleRate = sr; }

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
                currentValue += (kAttackTarget - currentValue) * attackCoeff;
                if (currentValue >= 1.0)
                {
                    currentValue = 1.0;
                    stage = Decay;
                }
                break;

            case Decay:
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
        double timeSamples = 0.005 * sr;
        coeff = 1.0 - std::exp(-2.0 * M_PI / timeSamples);
    }

    void setTarget(double t) { target = t; }

    double process()
    {
        current += (target - current) * coeff;
        return current;
    }

    void snap() { current = target; }
    double getCurrent() const { return current; }

private:
    double current;
    double target;
    double coeff;
};

// ============================================================================
// Partial — one sine oscillator + its own ADSR + level
// ============================================================================

struct Partial
{
    double phase = 0.0;
    double frequency = 0.0;
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
// KawaiiVoice — 32 partials + Surge XT sst-filters
//
// Uses sst::filtersplusplus::Filter which wraps QuadFilterUnit (4-wide SIMD).
// We use processMonoSample() — only voice 0 of the SIMD quad is active.
// Coefficient interpolation is built into the library's per-sample processing.
// ============================================================================

class KawaiiVoice
{
public:
    KawaiiVoice()
        : noteNumber(-1), velocity(0.0), sampleRate(44100.0)
        , cutoffSmoother(1.0), resoSmoother(0.0)
        , filterEnvDepth(0.0), filterKeytrack(0.0)
        , currentFilterTypeIndex(-1), currentFilterSubType(-1)
        , filterBlockPos(0)
    {
        // Default filter: SVF LP (index 0)
        configureFilter(0, 0);
    }

    void setSampleRate(double sr)
    {
        sampleRate = sr;
        for (auto& p : partials)
            p.envelope.setSampleRate(sr);
        filterEnvelope.setSampleRate(sr);
        cutoffSmoother.setSampleRate(sr);
        resoSmoother.setSampleRate(sr);

        // Set the filter's sample rate and sub-block size.
        // The library uses this for coefficient delta computation:
        // dC[i] = (targetC[i] - currentC[i]) / blockSize
        filter.setSampleRateAndBlockSize(sr, kFilterBlockSize);
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

        filterEnvelope.noteOn();
        // Reset all SIMD voice filter registers without losing sampleRate.
        // All 4 voices are active (not using setMono), so reset all of them
        // to prevent stale state from the previous note bleeding through.
        for (int v = 0; v < 4; v++)
            filter.resetVoice(v);
        cutoffSmoother.snap();
        resoSmoother.snap();
        filterBlockPos = 0;   // Reset sub-block position
    }

    void noteOff()
    {
        for (auto& p : partials)
            p.envelope.noteOff();
        filterEnvelope.noteOff();
    }

    // CPU path: per-sample processing (no GPU)
    void process(double* outLeft, double* outRight)
    {
        // 1. Sum all partials
        double sum = 0.0;
        for (auto& p : partials)
            sum += p.process(sampleRate);

        double sample = sum * velocity / static_cast<double>(kMaxPartials);

        // 2. Apply sst-filter with sub-block coefficient updates
        double envValue = filterEnvelope.process();
        double smoothedNorm = cutoffSmoother.process();
        double smoothedReso = resoSmoother.process();

        // At sub-block boundary: recompute filter coefficients
        if (filterBlockPos == 0)
        {
            double cutoffHz = computeEffectiveCutoff(smoothedNorm, envValue);
            // Convert Hz to sst-filters note units: A440 = 0, each unit = 1 semitone
            float noteVal = static_cast<float>(12.0 * std::log2(std::max(cutoffHz, 1.0) / 440.0));
            float reso = static_cast<float>(std::clamp(smoothedReso, 0.0, 1.0));

            // Make coefficients for ALL 4 SIMD voices (all active, matching library pattern)
            for (int v = 0; v < 4; v++)
                filter.makeCoefficients(v, noteVal, reso);
            filter.prepareBlock();
        }

        // Process through sst-filter (mono, voice 0 only)
        float out = filter.processMonoSample(static_cast<float>(sample));

        filterBlockPos++;
        if (filterBlockPos >= kFilterBlockSize)
        {
            filter.concludeBlock();
            filterBlockPos = 0;
        }

        *outLeft  = static_cast<double>(out);
        *outRight = static_cast<double>(out);
    }

    bool isActive() const
    {
        for (const auto& p : partials)
            if (p.envelope.isActive()) return true;
        return false;
    }

    int getNoteNumber() const { return noteNumber; }
    double getVelocity() const { return velocity; }

    // --- Filter parameter setters ---
    void setFilterCutoffNorm(double norm) { cutoffSmoother.setTarget(norm); }
    void setFilterResonance(double res)  { resoSmoother.setTarget(res); }
    void setFilterEnvDepth(double depth) { filterEnvDepth = depth; }
    void setFilterKeytrack(double amt)   { filterKeytrack = amt; }

    void setFilterEnvAttack(double sec)  { filterEnvelope.setAttack(sec); }
    void setFilterEnvDecay(double sec)   { filterEnvelope.setDecay(sec); }
    void setFilterEnvSustain(double lvl) { filterEnvelope.setSustain(lvl); }
    void setFilterEnvRelease(double sec) { filterEnvelope.setRelease(sec); }

    // Configure the sst-filter from our type index + subtype.
    // Only calls prepareInstance() when the type actually changes.
    void setFilterConfig(int typeIndex, int subType)
    {
        if (typeIndex == currentFilterTypeIndex && subType == currentFilterSubType)
            return;  // No change — skip (prepareInstance is not realtime-safe)

        configureFilter(typeIndex, subType);
    }

    // --- Hybrid GPU+CPU pipeline helpers ---

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
        double baseCutoffHz = 20.0 * std::pow(1000.0, smoothedNorm);
        double envMod = filterEnvDepth * envValue * 10000.0;
        double keyMod = filterKeytrack * (noteNumber - 60) * 100.0;
        return std::clamp(baseCutoffHz + envMod + keyMod, 20.0, 20000.0);
    }

    // --- Sub-block coefficient interpolation for GPU path ---
    // Called once per sub-block with the target cutoff and resonance.
    void prepareFilterBlock(double cutoffHz, double reso)
    {
        float noteVal = static_cast<float>(12.0 * std::log2(std::max(cutoffHz, 1.0) / 440.0));
        float r = static_cast<float>(std::clamp(reso, 0.0, 1.0));

        // Make coefficients for ALL 4 SIMD voices (all active, matching library pattern)
        for (int v = 0; v < 4; v++)
            filter.makeCoefficients(v, noteVal, r);
        filter.prepareBlock();
    }

    // Process one sample through the filter (call exactly kFilterBlockSize times
    // after prepareFilterBlock, then call concludeFilterBlock).
    float filterBlockStep(float sample)
    {
        return filter.processMonoSample(sample);
    }

    // End the current sub-block (call after kFilterBlockSize samples processed)
    void concludeFilterBlock()
    {
        filter.concludeBlock();
    }

    // Public so the processor can set per-partial ADSR and level directly
    std::array<Partial, kMaxPartials> partials;

private:
    int    noteNumber;
    double velocity;
    double sampleRate;

    // sst-filters++ Filter instance (wraps QuadFilterUnit + CoefficientMaker)
    sfpp::Filter filter;

    // Filter ADSR envelope and parameter smoothers
    ADSREnvelope filterEnvelope;
    ParamSmoother cutoffSmoother;
    ParamSmoother resoSmoother;
    double filterEnvDepth;
    double filterKeytrack;

    // Cached filter config to avoid redundant prepareInstance() calls
    int currentFilterTypeIndex;
    int currentFilterSubType;

    // Sub-block position counter for CPU path
    int filterBlockPos;

    // Delay line memory for Comb filters (managed per-voice)
    std::vector<float> delayLineMemory;

    // Configure the sst-filter from type table + subtype.
    //
    // Uses availableModelConfigurations() to get the EXACT valid configs
    // for the model, then filters to the desired passband/slope/drive from
    // our filter type table. The subtype index selects among the remaining
    // valid variants (cycling through different drive modes, slopes, or
    // submodels depending on what the model supports).
    //
    // IMPORTANT: We keep all 4 SIMD voices active and make coefficients for
    // all 4. This matches the library test patterns and prevents undefined
    // behavior (NaN/Inf) in inactive SIMD lanes from filter functions that
    // perform division (K35, etc.). We only read lane 0 via processMonoSample.
    void configureFilter(int typeIndex, int subType)
    {
        const auto& types = getFilterTypes();
        typeIndex = std::clamp(typeIndex, 0, kNumFilterTypes - 1);
        subType = std::clamp(subType, 0, 3);

        const auto& entry = types[(size_t)typeIndex];

        // 1. Set the model
        filter.setFilterModel(entry.model);

        // 2. Get ALL valid configurations for this model (sorted for determinism)
        auto allConfigs = sfpp::Filter::availableModelConfigurations(entry.model, true);

        // 3. Filter to configs matching our desired passband (and slope if specified)
        //    This gives us only configs that are actually valid for the passband
        //    we want, avoiding cross-passband contamination.
        std::vector<sfpp::ModelConfig> matching;
        for (const auto& cfg : allConfigs)
        {
            // Must match passband (unless table entry is UNSUPPORTED = don't care)
            if (entry.passband != sfpp::Passband::UNSUPPORTED && cfg.pt != entry.passband)
                continue;

            // For Comb filters: match slope (the primary dimension)
            if (entry.slope != sfpp::Slope::UNSUPPORTED && cfg.st != sfpp::Slope::UNSUPPORTED
                && cfg.st != entry.slope)
                continue;

            matching.push_back(cfg);
        }

        // 4. If we got matches, use the subtype index to select among them.
        //    If no matches (shouldn't happen), fall back to all configs.
        sfpp::ModelConfig chosen;
        if (!matching.empty())
        {
            int idx = subType % (int)matching.size();
            chosen = matching[(size_t)idx];
        }
        else if (!allConfigs.empty())
        {
            // Safety fallback: use closestValidModelTo with our desired config
            sfpp::ModelConfig desired(entry.passband, entry.slope, entry.drive, entry.submodel);
            chosen = sfpp::closestValidModelTo(entry.model, desired);
        }
        else
        {
            // No configs at all — fall back to SVF LP
            filter.setFilterModel(sfpp::FilterModel::CytomicSVF);
            chosen = sfpp::ModelConfig(sfpp::Passband::LP);
        }

        // 5. Set the validated configuration atomically
        filter.setModelConfiguration(chosen);

        // 6. Allocate delay line memory for Comb filters (all 4 SIMD voices)
        auto dlSize = sfpp::Filter::requiredDelayLinesSizes(entry.model, chosen);
        if (dlSize > 0)
        {
            // Need delay lines for all 4 SIMD voices since all are active
            delayLineMemory.resize(dlSize * 4, 0.0f);
            std::fill(delayLineMemory.begin(), delayLineMemory.end(), 0.0f);
            for (int v = 0; v < 4; v++)
                filter.provideDelayLine(v, delayLineMemory.data() + dlSize * v);
        }

        // 7. Validate and initialize the filter.
        //    Keep all 4 SIMD voices active (the default) — matching library
        //    test patterns. This ensures all lanes have valid coefficients
        //    during processing, preventing NaN/Inf from division-by-zero in
        //    filters like K35. We read only lane 0 via processMonoSample().
        bool ok = filter.prepareInstance();

        // If prepareInstance failed, fall back to SVF LP (always valid)
        if (!ok)
        {
            filter.setFilterModel(sfpp::FilterModel::CytomicSVF);
            filter.setModelConfiguration(sfpp::ModelConfig(sfpp::Passband::LP));
            filter.prepareInstance();
        }

        // 8. Re-apply sample rate and block size after prepareInstance's reset().
        //    While reset() preserves maker sampleRates, this ensures the payload
        //    and qfuState are in perfect sync with the current sample rate.
        filter.setSampleRateAndBlockSize(sampleRate, kFilterBlockSize);

        // 9. Prime the filter with initial coefficients for ALL 4 SIMD voices.
        //    Uses a safe initial cutoff (A440 = noteVal 0) and zero resonance.
        //    This ensures all SIMD lanes have valid non-zero coefficients before
        //    any audio processing begins, preventing NaN from uninitialized state.
        for (int v = 0; v < 4; v++)
            filter.makeCoefficients(v, 0.f, 0.f);
        filter.prepareBlock();
        filter.concludeBlock();

        // 10. Reset sub-block position so the CPU path starts a fresh block
        filterBlockPos = 0;

        currentFilterTypeIndex = typeIndex;
        currentFilterSubType = subType;
    }
};

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
