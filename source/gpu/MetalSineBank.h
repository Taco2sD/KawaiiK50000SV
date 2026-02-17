/**
 * MetalSineBank.h — GPU-accelerated additive synthesis via Metal compute
 *
 * Uses Apple Silicon's unified memory for zero-copy CPU↔GPU buffer sharing.
 * The GPU computes sin() × level × envelope for all oscillators in parallel;
 * ADSR envelopes are computed per-sample on CPU and passed to the GPU.
 *
 * PIMPL pattern hides ObjC Metal types from C++ translation units.
 */

#pragma once

#ifdef __APPLE__

#include <cstdint>

namespace Steinberg {
namespace Vst {
namespace Kawaii {

// Per-oscillator data sent to GPU each block
struct OscillatorParams {
    float phaseStart;       // Current phase [0, 1)
    float phaseIncrement;   // frequency / sampleRate
    float level;            // Partial level [0, 1]
    float velocityScale;    // velocity / kMaxPartials
};

class MetalSineBank {
public:
    MetalSineBank();
    ~MetalSineBank();

    // Call from setActive(true). Returns false if Metal is unavailable.
    bool init(int maxOscillators, int maxBlockSize);

    // GPU dispatch. envValues layout: [oscillator * numSamples + sample].
    // Output is mono (caller duplicates to stereo).
    void processBlock(
        const OscillatorParams* oscParams,
        const float* envValues,
        int numOscillators,
        float* output,
        int numSamples
    );

    // Call from setActive(false).
    void shutdown();

    bool isAvailable() const;

private:
    struct Impl;
    Impl* _impl;
};

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg

#endif // __APPLE__
