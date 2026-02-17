/**
 * MetalSineBank.mm — Metal compute for per-voice additive synthesis
 *
 * Hybrid GPU+CPU architecture:
 *   - One GPU thread per (voice, sample) pair
 *   - Each thread sums only its voice's oscillators → per-voice output
 *   - CPU applies per-voice ZDF SVF filter after GPU dispatch
 *
 * Threading model (one thread per voice×sample) scales well for moderate
 * oscillator counts. When scaling to 2048+ partials per voice, restructure
 * to one-thread-per-oscillator + parallel reduction.
 *
 * Shared memory buffers (MTLResourceStorageModeShared) — zero-copy on Apple Silicon.
 * Synchronous dispatch (waitUntilCompleted) — sub-millisecond for current workload.
 */

#ifdef __APPLE__

#include "MetalSineBank.h"

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include <cstring>

namespace Steinberg {
namespace Vst {
namespace Kawaii {

// ============================================================================
// Metal Shader Source — Per-Voice Additive Synthesis Kernel
//
// Thread mapping: globalThreadId = voiceIdx * numSamples + sampleIdx
// Each thread reads its VoiceDescriptor to find which oscillators belong
// to its voice, sums them, and writes to per-voice output buffer.
// ============================================================================

static const char* kSineBankShaderSource = R"metal(
#include <metal_stdlib>
using namespace metal;

struct OscillatorParams {
    float phaseStart;
    float phaseIncrement;
    float level;
    float velocityScale;   // unused by per-voice kernel (handled by VoiceDescriptor)
};

// Tells each GPU thread which oscillators belong to its voice
struct VoiceDescriptor {
    uint startOsc;          // First oscillator index for this voice
    uint numOsc;            // Number of active oscillators for this voice
    float velocityScale;    // velocity / kMaxPartials (applied once after sum)
    float pad;              // 16-byte alignment
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
    // Decode which voice and which sample this thread handles
    uint voiceIdx  = globalThreadId / numSamples;
    uint sampleIdx = globalThreadId % numSamples;

    // Bounds check — thread grid may be padded beyond actual work
    if (voiceIdx >= numVoices || sampleIdx >= numSamples) return;

    // Read this voice's descriptor
    VoiceDescriptor desc = voiceDescs[voiceIdx];

    // Sum this voice's oscillators at this sample position
    float sum = 0.0f;
    for (uint i = 0; i < desc.numOsc; i++) {
        uint oscIdx = desc.startOsc + i;
        float env = envValues[oscIdx * numSamples + sampleIdx];
        if (env <= 0.0f) continue;  // skip silent partials

        // Compute exact phase at this sample position (no sequential dependency)
        float phase = oscParams[oscIdx].phaseStart
                    + float(sampleIdx) * oscParams[oscIdx].phaseIncrement;
        phase = phase - floor(phase);   // wrap to [0, 1)

        sum += metal::sin(2.0f * M_PI_F * phase)
             * oscParams[oscIdx].level
             * env;
    }

    // Apply velocity scaling (same for all partials in this voice)
    sum *= desc.velocityScale;

    // Write to per-voice output: [voiceIdx * numSamples + sampleIdx]
    output[voiceIdx * numSamples + sampleIdx] = sum;
}
)metal";

// ============================================================================
// PIMPL — hides ObjC types from C++ headers
// ============================================================================

struct MetalSineBank::Impl {
    id<MTLDevice>               device          = nil;
    id<MTLCommandQueue>         commandQueue    = nil;
    id<MTLComputePipelineState> pipeline        = nil;
    id<MTLBuffer>               oscParamsBuf    = nil;
    id<MTLBuffer>               envValuesBuf    = nil;
    id<MTLBuffer>               outputBuf       = nil;  // per-voice: maxVoices * maxBlockSize
    id<MTLBuffer>               voiceDescsBuf   = nil;  // voice descriptors
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

        // Compile shader from source at runtime (avoids .metallib build step)
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

        // Shared memory buffers — CPU and GPU access the same physical memory
        // on Apple Silicon (zero-copy). On Intel Macs, the driver handles transfers.
        MTLResourceOptions opts = MTLResourceStorageModeShared;

        _impl->oscParamsBuf = [_impl->device
            newBufferWithLength:(NSUInteger)(maxOscillators * sizeof(OscillatorParams))
                        options:opts];

        _impl->envValuesBuf = [_impl->device
            newBufferWithLength:(NSUInteger)(maxOscillators * maxBlockSize * sizeof(float))
                        options:opts];

        // Per-voice output: one mono stream per voice (was: single mono stream)
        _impl->outputBuf = [_impl->device
            newBufferWithLength:(NSUInteger)(maxVoices * maxBlockSize * sizeof(float))
                        options:opts];

        // Voice descriptors: one per voice
        _impl->voiceDescsBuf = [_impl->device
            newBufferWithLength:(NSUInteger)(maxVoices * sizeof(VoiceDescriptor))
                        options:opts];

        if (!_impl->oscParamsBuf || !_impl->envValuesBuf ||
            !_impl->outputBuf || !_impl->voiceDescsBuf)
            return false;

        _impl->available = true;

        NSLog(@"[KawaiiGPU] Metal init OK — device: %@, maxThreads: %lu, "
              "oscBuf: %luB, envBuf: %luB, outBuf: %luB (per-voice: %d voices)",
              _impl->device.name,
              (unsigned long)_impl->pipeline.maxTotalThreadsPerThreadgroup,
              (unsigned long)_impl->oscParamsBuf.length,
              (unsigned long)_impl->envValuesBuf.length,
              (unsigned long)_impl->outputBuf.length,
              maxVoices);
    }
    return true;
}

void MetalSineBank::processBlock(
    const OscillatorParams* oscParams,
    const float* envValues,
    int numOscillators,
    const VoiceDescriptor* voiceDescs,
    int numVoices,
    float* output,
    int numSamples)
{
    if (!_impl->available || numVoices == 0 || numSamples == 0) {
        memset(output, 0, (size_t)(numVoices * numSamples) * sizeof(float));
        return;
    }

    @autoreleasepool {
        // Write into shared Metal buffers (zero-copy on Apple Silicon)
        memcpy(_impl->oscParamsBuf.contents, oscParams,
               (size_t)numOscillators * sizeof(OscillatorParams));
        memcpy(_impl->envValuesBuf.contents, envValues,
               (size_t)numOscillators * numSamples * sizeof(float));
        memcpy(_impl->voiceDescsBuf.contents, voiceDescs,
               (size_t)numVoices * sizeof(VoiceDescriptor));

        // Encode compute command
        id<MTLCommandBuffer> cmdBuf = [_impl->commandQueue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];

        [enc setComputePipelineState:_impl->pipeline];
        [enc setBuffer:_impl->oscParamsBuf   offset:0 atIndex:0];
        [enc setBuffer:_impl->envValuesBuf   offset:0 atIndex:1];
        [enc setBuffer:_impl->outputBuf      offset:0 atIndex:2];
        [enc setBuffer:_impl->voiceDescsBuf  offset:0 atIndex:3];

        uint32_t nv = (uint32_t)numVoices;
        uint32_t ns = (uint32_t)numSamples;
        [enc setBytes:&nv length:sizeof(uint32_t) atIndex:4];
        [enc setBytes:&ns length:sizeof(uint32_t) atIndex:5];

        // Total threads: numVoices × numSamples
        // Each thread handles one (voice, sample) pair
        NSUInteger totalThreads = (NSUInteger)(numVoices * numSamples);
        NSUInteger tgSize = MIN(totalThreads,
                                _impl->pipeline.maxTotalThreadsPerThreadgroup);

        [enc dispatchThreads:MTLSizeMake(totalThreads, 1, 1)
       threadsPerThreadgroup:MTLSizeMake(tgSize, 1, 1)];

        [enc endEncoding];
        [cmdBuf commit];
        [cmdBuf waitUntilCompleted];

        // Read per-voice results from shared memory
        memcpy(output, _impl->outputBuf.contents,
               (size_t)(numVoices * numSamples) * sizeof(float));

        _impl->dispatchCount++;
        if (_impl->dispatchCount == 1 || _impl->dispatchCount % 10000 == 0) {
            NSLog(@"[KawaiiGPU] dispatch #%llu — %d voices × %d osc × %d samples (per-voice)",
                  _impl->dispatchCount, numVoices, numOscillators, numSamples);
        }
    }
}

void MetalSineBank::shutdown()
{
    if (!_impl) return;
    _impl->oscParamsBuf  = nil;
    _impl->envValuesBuf  = nil;
    _impl->outputBuf     = nil;
    _impl->voiceDescsBuf = nil;
    _impl->pipeline      = nil;
    _impl->commandQueue  = nil;
    _impl->device        = nil;
    _impl->available     = false;
}

bool MetalSineBank::isAvailable() const
{
    return _impl && _impl->available;
}

}}} // namespaces

#endif // __APPLE__
