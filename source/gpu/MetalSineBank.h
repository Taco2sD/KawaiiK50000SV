/**
 * MetalSineBank.h — GPU-accelerated additive synthesis via Metal compute
 *
 * Hybrid GPU+CPU pipeline:
 *   - GPU computes sin() × level × envelope per-voice (not globally mixed)
 *   - CPU pre-computes ADSR envelopes per-sample and passes them to GPU
 *   - CPU applies per-voice ZDF SVF filter to each voice's GPU output
 *
 * Uses Apple Silicon's unified memory for zero-copy CPU↔GPU buffer sharing.
 * PIMPL pattern hides ObjC Metal types from C++ translation units.
 */

#pragma once

#ifdef __APPLE__

#include <cstdint>

namespace Steinberg {
namespace Vst {
namespace Kawaii {

// Per-oscillator data sent to GPU each block (16 bytes, naturally aligned)
struct OscillatorParams {
    float phaseStart;       // Current phase [0, 1)
    float phaseIncrement;   // frequency / sampleRate
    float level;            // Partial level [0, 1]
    float velocityScale;    // velocity / kMaxPartials (unused by per-voice kernel)
};

// Per-voice metadata for the per-voice GPU kernel (16 bytes, aligned).
// Tells the kernel which oscillators belong to each voice, so it can
// sum per-voice instead of globally.
struct VoiceDescriptor {
    uint32_t startOsc;      // First oscillator index in oscParams/envValues
    uint32_t numOsc;        // Number of active oscillators for this voice
    float velocityScale;    // velocity / kMaxPartials (applied once after sum)
    float pad;              // Padding to 16-byte alignment
};

class MetalSineBank {
public:
    MetalSineBank();
    ~MetalSineBank();

    // Call from setActive(true). Returns false if Metal is unavailable.
    bool init(int maxOscillators, int maxBlockSize, int maxVoices);

    // GPU dispatch with per-voice output.
    // oscParams/envValues: oscillators grouped by voice (voice 0's partials, then voice 1's, etc.)
    // envValues layout: [oscillator * numSamples + sampleIdx]
    // voiceDescs: each voice's oscillator range within the flat arrays
    // output layout: [voiceIdx * numSamples + sampleIdx] — per-voice mono streams
    void processBlock(
        const OscillatorParams* oscParams,
        const float* envValues,
        int numOscillators,
        const VoiceDescriptor* voiceDescs,
        int numVoices,
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
