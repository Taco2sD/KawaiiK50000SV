/**
 * KawaiiParams.h — Parameter Scaling Utilities
 * ==============================================
 *
 * VST3 parameters are always stored as "normalized" values between 0.0 and 1.0.
 * But real-world audio parameters are measured in different units:
 *   - Frequency in Hz (e.g., 20 Hz to 20,000 Hz)
 *   - Time in milliseconds (e.g., 1 ms to 10,000 ms)
 *   - Amplitude in decibels (e.g., -60 dB to 0 dB)
 *
 * This file provides functions to convert back and forth between normalized
 * values and real-world units. These conversions use EXPONENTIAL scaling
 * because human perception of pitch and volume is logarithmic — a slider
 * that goes linearly from 20 Hz to 20,000 Hz would spend 99% of its travel
 * in the high frequencies where our ears can barely tell the difference.
 * Exponential mapping spreads the perceptually useful range evenly across
 * the slider.
 *
 * The formula: realValue = min * (max / min) ^ normalized
 *   - When normalized = 0.0, result = min * 1 = min
 *   - When normalized = 1.0, result = min * (max/min) = max
 *   - When normalized = 0.5, result = geometric midpoint (e.g., 632 Hz for 20–20k)
 */

#pragma once

#include <cmath>

namespace Steinberg {
namespace Vst {
namespace Kawaii {

// ============================================================================
// NORMALIZED <-> REAL-WORLD CONVERSION FUNCTIONS
// ============================================================================

/**
 * Convert a normalized value (0.0–1.0) to a time in milliseconds.
 *
 * Uses exponential mapping so the slider feels perceptually linear:
 * small turns at the bottom = small time changes (1ms to 10ms),
 * same-sized turns at the top = large time changes (1000ms to 10000ms).
 *
 * @param normalized  The slider position (0.0 to 1.0)
 * @param minMs       The minimum time in milliseconds (at normalized=0)
 * @param maxMs       The maximum time in milliseconds (at normalized=1)
 * @return            The corresponding time in milliseconds
 */
inline double normalizedToMs(double normalized, double minMs, double maxMs)
{
    // pow(ratio, normalized) sweeps exponentially from 1.0 to ratio
    // Multiplying by minMs shifts the range from [1, ratio] to [min, max]
    return minMs * std::pow(maxMs / minMs, normalized);
}

/**
 * Convert a time in milliseconds back to a normalized value (0.0–1.0).
 * This is the inverse of normalizedToMs — used when the DAW tells us
 * a real-world value and we need to store it as normalized.
 *
 * Uses the logarithm to invert the exponential mapping.
 *
 * @param ms          The time in milliseconds
 * @param minMs       The minimum time in milliseconds
 * @param maxMs       The maximum time in milliseconds
 * @return            The corresponding normalized value (0.0 to 1.0)
 */
inline double msToNormalized(double ms, double minMs, double maxMs)
{
    // log(ms/min) / log(max/min) is the inverse of min * (max/min)^normalized
    return std::log(ms / minMs) / std::log(maxMs / minMs);
}

/**
 * Convert a normalized value (0.0–1.0) to a frequency in Hz.
 * Same exponential mapping as time — essential for frequency because
 * each musical octave is a *doubling* of frequency.
 */
inline double normalizedToHz(double normalized, double minHz, double maxHz)
{
    return minHz * std::pow(maxHz / minHz, normalized);
}

/**
 * Convert a frequency in Hz back to a normalized value (0.0–1.0).
 * Inverse of normalizedToHz.
 */
inline double hzToNormalized(double hz, double minHz, double maxHz)
{
    return std::log(hz / minHz) / std::log(maxHz / minHz);
}

// ============================================================================
// PARAMETER RANGES
// ============================================================================
//
// These constants define the real-world min/max for each parameter.
// They're used together with the conversion functions above.
// Collected in a namespace so they don't pollute the global scope.

namespace ParamRanges
{
    // --- Envelope times (in milliseconds) ---
    // These ranges are generous enough for both snappy percussion and slow pads.

    constexpr double kEnvAttackMin  = 1.0;      // 1 ms minimum attack
    constexpr double kEnvAttackMax  = 5000.0;    // 5 seconds maximum attack
    constexpr double kEnvDecayMin   = 1.0;       // 1 ms minimum decay
    constexpr double kEnvDecayMax   = 10000.0;   // 10 seconds maximum decay
    constexpr double kEnvReleaseMin = 1.0;       // 1 ms minimum release
    constexpr double kEnvReleaseMax = 10000.0;   // 10 seconds maximum release

    // --- Master volume ---
    constexpr double kMasterVolMin  = 0.0;       // Silence
    constexpr double kMasterVolMax  = 1.0;       // Unity gain

    // --- Filter cutoff frequency (Hz) ---
    // Exponential mapping: knob at 0 = 20 Hz, knob at 1 = 20 kHz
    constexpr double kFilterCutoffMin = 20.0;
    constexpr double kFilterCutoffMax = 20000.0;

    // --- Filter cutoff default (normalized) ---
    // Default fully open so the filter is transparent until user adjusts it
    constexpr double kFilterCutoffDefault = 1.0;

    // --- Filter resonance ---
    // Stored as 0–1 normalized. Mapped to Q in the voice:
    //   Q = 0.5 + reso * 24.5  (range: 0.5 to 25)
    constexpr double kFilterResoDefault = 0.0;

    // --- Filter envelope depth ---
    // Bipolar: 0.0 = full negative, 0.5 = no modulation, 1.0 = full positive
    constexpr double kFilterEnvDepthDefault = 0.5;

    // --- Filter keytrack ---
    // 0 = no tracking, 1 = full tracking (100 Hz/semitone from C3)
    constexpr double kFilterKeytrackDefault = 0.0;
}

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
