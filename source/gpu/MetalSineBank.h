/**
 * MetalSineBank.h — GPU-accelerated additive synthesis via Metal compute
 *
 * Async double-buffered GPU+CPU pipeline:
 *   - Audio thread submits current block to GPU (non-blocking)
 *   - Audio thread reads back PREVIOUS block's GPU results (already complete)
 *   - CPU applies per-voice ZDF SVF filter to previous results
 *   - Cost: one buffer of latency, reported to DAW for PDC
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

    // Async double-buffered dispatch.
    // Submits current block's data to GPU (non-blocking) AND returns the
    // PREVIOUS block's GPU results. On first call, previous output is zeroed.
    //
    // oscParams/envValues: oscillators grouped by voice
    // envValues layout: [oscillator * numSamples + sampleIdx]
    // voiceDescs: each voice's oscillator range
    //
    // prevOutput: receives PREVIOUS block's per-voice output
    //             layout: [voiceIdx * prevNumSamples + sampleIdx]
    // outPrevNumVoices/outPrevNumSamples: dimensions of previous output
    void processBlock(
        const OscillatorParams* oscParams,
        const float* envValues,
        int numOscillators,
        const VoiceDescriptor* voiceDescs,
        int numVoices,
        int numSamples,
        float* prevOutput,
        int& outPrevNumVoices,
        int& outPrevNumSamples
    );

    // Call from setActive(false). Drains in-flight GPU work before releasing.
    void shutdown();

    bool isAvailable() const;

    // Latency introduced by double buffering (= maxBlockSize samples)
    int getLatencySamples() const;

private:
    struct Impl;
    Impl* _impl;
};

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg

#endif // __APPLE__
