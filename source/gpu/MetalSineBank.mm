/**
 * MetalSineBank.mm — Async double-buffered Metal compute for additive synthesis
 *
 * Key change from sync version: the audio thread NEVER blocks on GPU completion.
 *
 * Double-buffer protocol:
 *   processBlock() submits block N to GPU (non-blocking) and returns block N-1's
 *   results (already completed by GPU). The audio thread is free to apply CPU-side
 *   ZDF SVF filtering to the previous results without waiting.
 *
 * Two buffer sets (A and B) ping-pong:
 *   Block 0: submit to A, return silence (no previous)
 *   Block 1: submit to B, return A's results (GPU finished during block 0→1)
 *   Block 2: submit to A, return B's results
 *   ...
 *
 * Completion handler (Metal background thread) sets an atomic flag when GPU finishes.
 * If GPU hasn't finished by the time we need the result (shouldn't happen at normal
 * buffer sizes), we gracefully output silence rather than blocking.
 *
 * Cost: one buffer of latency (~11.6ms at 512 samples / 44.1kHz).
 * The DAW compensates via plugin delay compensation (PDC).
 */

#ifdef __APPLE__

#include "MetalSineBank.h"

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include <cstring>
#include <atomic>

namespace Steinberg {
namespace Vst {
namespace Kawaii {

// ============================================================================
// Metal Shader Source — Per-Voice Additive Synthesis Kernel
// (unchanged from sync version — the GPU work itself is identical)
// ============================================================================

static const char* kSineBankShaderSource = R"metal(
#include <metal_stdlib>
using namespace metal;

struct OscillatorParams {
    float phaseStart;
    float phaseIncrement;
    float level;
    float velocityScale;
};

struct VoiceDescriptor {
    uint startOsc;
    uint numOsc;
    float velocityScale;
    float pad;
};

// One thread per (voice, sample) pair.
// Each thread sums only its voice's oscillators, producing per-voice output.
kernel void sineBankPerVoiceKernel(
    device const OscillatorParams* oscParams [[buffer(0)]],
    device const float* envValues            [[buffer(1)]],
    device float* output                     [[buffer(2)]],
    device const VoiceDescriptor* voiceDescs [[buffer(3)]],
    constant uint& numVoices                 [[buffer(4)]],
    constant uint& numSamples                [[buffer(5)]],
    uint globalThreadId [[thread_position_in_grid]])
{
    uint voiceIdx  = globalThreadId / numSamples;
    uint sampleIdx = globalThreadId % numSamples;
    if (voiceIdx >= numVoices || sampleIdx >= numSamples) return;

    VoiceDescriptor desc = voiceDescs[voiceIdx];

    float sum = 0.0f;
    for (uint i = 0; i < desc.numOsc; i++) {
        uint oscIdx = desc.startOsc + i;
        float env = envValues[oscIdx * numSamples + sampleIdx];
        if (env <= 0.0f) continue;

        float phase = oscParams[oscIdx].phaseStart
                    + float(sampleIdx) * oscParams[oscIdx].phaseIncrement;
        phase = phase - floor(phase);

        sum += metal::sin(2.0f * M_PI_F * phase)
             * oscParams[oscIdx].level
             * env;
    }

    sum *= desc.velocityScale;
    output[voiceIdx * numSamples + sampleIdx] = sum;
}
)metal";

// ============================================================================
// PIMPL — double-buffered Metal state
// ============================================================================

// One complete set of GPU input/output buffers.
// Two of these ping-pong for async double buffering.
struct BufferSet {
    id<MTLBuffer> oscParamsBuf = nil;
    id<MTLBuffer> envValuesBuf = nil;
    id<MTLBuffer> outputBuf    = nil;
    id<MTLBuffer> voiceDescsBuf = nil;

    // Set true by Metal completion handler when GPU finishes this set.
    // Cleared by processBlock before submitting new work to this set.
    std::atomic<bool> gpuDone{false};

    // Dimensions of the dispatch stored in this set (needed to read back results)
    int numVoices  = 0;
    int numSamples = 0;
};

struct MetalSineBank::Impl {
    id<MTLDevice>               device       = nil;
    id<MTLCommandQueue>         commandQueue = nil;
    id<MTLComputePipelineState> pipeline     = nil;

    // Double buffer: two complete buffer sets
    BufferSet sets[2];

    // Index of the set to WRITE to next.
    // The "previous" result is in sets[1 - nextWriteIdx].
    int nextWriteIdx = 0;

    // False until the first dispatch has been submitted.
    // Before that, there's no previous result to retrieve.
    bool hasPreviousResult = false;

    int maxOscillators = 0;
    int maxBlockSize   = 0;
    int maxVoices      = 0;
    bool available     = false;
    uint64_t dispatchCount = 0;
};

// ============================================================================
// Public API
// ============================================================================

MetalSineBank::MetalSineBank() : _impl(new Impl) {}

MetalSineBank::~MetalSineBank()
{
    shutdown();
    delete _impl;
}

bool MetalSineBank::init(int maxOscillators, int maxBlockSize, int maxVoices)
{
    _impl->maxOscillators = maxOscillators;
    _impl->maxBlockSize   = maxBlockSize;
    _impl->maxVoices      = maxVoices;

    @autoreleasepool {
        _impl->device = MTLCreateSystemDefaultDevice();
        if (!_impl->device) return false;

        NSError* error = nil;
        NSString* source = [NSString stringWithUTF8String:kSineBankShaderSource];
        id<MTLLibrary> library = [_impl->device newLibraryWithSource:source
                                                             options:nil
                                                               error:&error];
        if (!library) {
            NSLog(@"MetalSineBank: shader compile failed: %@", error);
            return false;
        }

        id<MTLFunction> kernel = [library newFunctionWithName:@"sineBankPerVoiceKernel"];
        if (!kernel) return false;

        _impl->pipeline = [_impl->device newComputePipelineStateWithFunction:kernel
                                                                       error:&error];
        if (!_impl->pipeline) return false;

        _impl->commandQueue = [_impl->device newCommandQueue];
        if (!_impl->commandQueue) return false;

        // Allocate TWO complete buffer sets for double buffering.
        // While GPU processes set A, CPU reads from set B (previous result).
        MTLResourceOptions opts = MTLResourceStorageModeShared;

        for (int i = 0; i < 2; i++)
        {
            auto& set = _impl->sets[i];

            set.oscParamsBuf = [_impl->device
                newBufferWithLength:(NSUInteger)(maxOscillators * sizeof(OscillatorParams))
                            options:opts];

            set.envValuesBuf = [_impl->device
                newBufferWithLength:(NSUInteger)(maxOscillators * maxBlockSize * sizeof(float))
                            options:opts];

            set.outputBuf = [_impl->device
                newBufferWithLength:(NSUInteger)(maxVoices * maxBlockSize * sizeof(float))
                            options:opts];

            set.voiceDescsBuf = [_impl->device
                newBufferWithLength:(NSUInteger)(maxVoices * sizeof(VoiceDescriptor))
                            options:opts];

            if (!set.oscParamsBuf || !set.envValuesBuf ||
                !set.outputBuf || !set.voiceDescsBuf)
                return false;

            set.gpuDone.store(false);
            set.numVoices = 0;
            set.numSamples = 0;
        }

        _impl->nextWriteIdx = 0;
        _impl->hasPreviousResult = false;
        _impl->available = true;

        NSLog(@"[KawaiiGPU] Metal init OK (async double-buffer) — device: %@, "
              "maxThreads: %lu, 2× buffers allocated",
              _impl->device.name,
              (unsigned long)_impl->pipeline.maxTotalThreadsPerThreadgroup);
    }
    return true;
}

void MetalSineBank::processBlock(
    const OscillatorParams* oscParams,
    const float* envValues,
    int numOscillators,
    const VoiceDescriptor* voiceDescs,
    int numVoices,
    int numSamples,
    float* prevOutput,
    int& outPrevNumVoices,
    int& outPrevNumSamples)
{
    outPrevNumVoices  = 0;
    outPrevNumSamples = 0;

    if (!_impl->available) return;

    // =========================================================================
    // Step 1: Retrieve PREVIOUS block's GPU results (if available)
    //
    // The previous dispatch is in sets[1 - nextWriteIdx]. If the GPU has
    // finished (gpuDone == true), copy the results out. If not (shouldn't
    // happen at normal buffer sizes), output silence — never block.
    // =========================================================================

    if (_impl->hasPreviousResult)
    {
        int readIdx = 1 - _impl->nextWriteIdx;
        auto& readSet = _impl->sets[readIdx];

        if (readSet.gpuDone.load(std::memory_order_acquire))
        {
            outPrevNumVoices  = readSet.numVoices;
            outPrevNumSamples = readSet.numSamples;

            if (outPrevNumVoices > 0 && outPrevNumSamples > 0)
            {
                // Read directly from shared memory (zero-copy on Apple Silicon)
                memcpy(prevOutput, readSet.outputBuf.contents,
                       (size_t)(outPrevNumVoices * outPrevNumSamples) * sizeof(float));
            }
        }
        else
        {
            // GPU not done yet — graceful degradation: output silence.
            // This should rarely happen; log it for diagnostics.
            NSLog(@"[KawaiiGPU] WARNING: GPU not finished for previous block "
                  "(dispatch #%llu) — outputting silence",
                  _impl->dispatchCount);
        }
    }

    // =========================================================================
    // Step 2: Submit CURRENT block to GPU (non-blocking)
    //
    // Write data into the next buffer set, encode command buffer, commit with
    // a completion handler that sets gpuDone. The audio thread returns
    // immediately after commit — no waitUntilCompleted!
    // =========================================================================

    if (numVoices == 0 || numSamples == 0)
    {
        // Nothing to dispatch. Mark this slot as done with zero output.
        auto& writeSet = _impl->sets[_impl->nextWriteIdx];
        writeSet.numVoices = 0;
        writeSet.numSamples = 0;
        writeSet.gpuDone.store(true, std::memory_order_release);
        _impl->hasPreviousResult = true;
        _impl->nextWriteIdx = 1 - _impl->nextWriteIdx;
        return;
    }

    @autoreleasepool {
        int writeIdx = _impl->nextWriteIdx;
        auto& writeSet = _impl->sets[writeIdx];

        // Mark as not done (GPU hasn't started yet)
        writeSet.gpuDone.store(false, std::memory_order_release);
        writeSet.numVoices  = numVoices;
        writeSet.numSamples = numSamples;

        // Write data into this set's shared Metal buffers
        memcpy(writeSet.oscParamsBuf.contents, oscParams,
               (size_t)numOscillators * sizeof(OscillatorParams));
        memcpy(writeSet.envValuesBuf.contents, envValues,
               (size_t)numOscillators * numSamples * sizeof(float));
        memcpy(writeSet.voiceDescsBuf.contents, voiceDescs,
               (size_t)numVoices * sizeof(VoiceDescriptor));

        // Encode compute command
        id<MTLCommandBuffer> cmdBuf = [_impl->commandQueue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];

        [enc setComputePipelineState:_impl->pipeline];
        [enc setBuffer:writeSet.oscParamsBuf   offset:0 atIndex:0];
        [enc setBuffer:writeSet.envValuesBuf   offset:0 atIndex:1];
        [enc setBuffer:writeSet.outputBuf      offset:0 atIndex:2];
        [enc setBuffer:writeSet.voiceDescsBuf  offset:0 atIndex:3];

        uint32_t nv = (uint32_t)numVoices;
        uint32_t ns = (uint32_t)numSamples;
        [enc setBytes:&nv length:sizeof(uint32_t) atIndex:4];
        [enc setBytes:&ns length:sizeof(uint32_t) atIndex:5];

        NSUInteger totalThreads = (NSUInteger)(numVoices * numSamples);
        NSUInteger tgSize = MIN(totalThreads,
                                _impl->pipeline.maxTotalThreadsPerThreadgroup);

        [enc dispatchThreads:MTLSizeMake(totalThreads, 1, 1)
       threadsPerThreadgroup:MTLSizeMake(tgSize, 1, 1)];

        [enc endEncoding];

        // Completion handler: fires on a Metal-internal thread when GPU finishes.
        // Captures writeIdx by value (safe — it's an int, not a pointer to stack).
        // Captures _impl by value (raw pointer — safe as long as shutdown() drains
        // the queue before deallocating).
        auto* impl = _impl;
        [cmdBuf addCompletedHandler:^(id<MTLCommandBuffer> /*buffer*/) {
            impl->sets[writeIdx].gpuDone.store(true, std::memory_order_release);
        }];

        // Submit to GPU — returns immediately! Audio thread is NOT blocked.
        [cmdBuf commit];

        _impl->dispatchCount++;
        if (_impl->dispatchCount == 1 || _impl->dispatchCount % 10000 == 0) {
            NSLog(@"[KawaiiGPU] async dispatch #%llu — %d voices × %d osc × %d samples",
                  _impl->dispatchCount, numVoices, numOscillators, numSamples);
        }
    }

    // Flip to other buffer set for next call
    _impl->nextWriteIdx = 1 - _impl->nextWriteIdx;
    _impl->hasPreviousResult = true;
}

void MetalSineBank::shutdown()
{
    if (!_impl || !_impl->available) return;

    // Drain the command queue: submit an empty command buffer and wait.
    // This ensures all in-flight GPU work completes before we release buffers,
    // preventing completion handlers from accessing freed memory.
    @autoreleasepool {
        id<MTLCommandBuffer> fence = [_impl->commandQueue commandBuffer];
        [fence commit];
        [fence waitUntilCompleted];
    }

    for (auto& set : _impl->sets)
    {
        set.oscParamsBuf  = nil;
        set.envValuesBuf  = nil;
        set.outputBuf     = nil;
        set.voiceDescsBuf = nil;
        set.gpuDone.store(false);
    }
    _impl->pipeline      = nil;
    _impl->commandQueue  = nil;
    _impl->device        = nil;
    _impl->available     = false;
    _impl->hasPreviousResult = false;
}

bool MetalSineBank::isAvailable() const
{
    return _impl && _impl->available;
}

int MetalSineBank::getLatencySamples() const
{
    if (!_impl || !_impl->available) return 0;
    return _impl->maxBlockSize;  // one buffer of latency from double buffering
}

}}} // namespaces

#endif // __APPLE__
