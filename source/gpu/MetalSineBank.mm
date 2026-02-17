/**
 * MetalSineBank.mm — Metal compute implementation for additive synthesis
 *
 * Architecture:
 *   - Shader compiled from source at init time (avoids .metallib build step)
 *   - Shared memory buffers (MTLResourceStorageModeShared) — zero-copy on Apple Silicon
 *   - One GPU thread per audio sample; each thread sums all oscillators
 *   - Synchronous dispatch (waitUntilCompleted) — sub-millisecond for current workload
 *
 * Threading model (one thread per sample) is optimal for moderate oscillator counts.
 * When scaling to 2048+ partials, restructure to one-thread-per-oscillator + reduction.
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
// Metal Shader Source (compiled at runtime)
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

// One thread per audio sample. Each thread sums contributions from all oscillators.
kernel void sineBankKernel(
    device const OscillatorParams* oscParams [[buffer(0)]],
    device const float* envValues            [[buffer(1)]],
    device float* output                     [[buffer(2)]],
    constant uint& numOscillators            [[buffer(3)]],
    constant uint& numSamples                [[buffer(4)]],
    uint sampleIdx [[thread_position_in_grid]])
{
    if (sampleIdx >= numSamples) return;

    float sum = 0.0f;
    for (uint osc = 0; osc < numOscillators; osc++) {
        float env = envValues[osc * numSamples + sampleIdx];
        if (env <= 0.0f) continue;

        // Compute exact phase at this sample position (avoids sequential dependency)
        float phase = oscParams[osc].phaseStart
                    + float(sampleIdx) * oscParams[osc].phaseIncrement;
        phase = phase - floor(phase);   // wrap to [0, 1)

        sum += metal::sin(2.0f * M_PI_F * phase)
             * oscParams[osc].level
             * env
             * oscParams[osc].velocityScale;
    }
    output[sampleIdx] = sum;
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
    id<MTLBuffer>               outputBuf       = nil;
    int maxOscillators = 0;
    int maxBlockSize   = 0;
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

bool MetalSineBank::init(int maxOscillators, int maxBlockSize)
{
    _impl->maxOscillators = maxOscillators;
    _impl->maxBlockSize   = maxBlockSize;

    @autoreleasepool {
        _impl->device = MTLCreateSystemDefaultDevice();
        if (!_impl->device) return false;

        // Compile shader from source
        NSError* error = nil;
        NSString* source = [NSString stringWithUTF8String:kSineBankShaderSource];
        id<MTLLibrary> library = [_impl->device newLibraryWithSource:source
                                                             options:nil
                                                               error:&error];
        if (!library) {
            NSLog(@"MetalSineBank: shader compile failed: %@", error);
            return false;
        }

        id<MTLFunction> kernel = [library newFunctionWithName:@"sineBankKernel"];
        if (!kernel) return false;

        _impl->pipeline = [_impl->device newComputePipelineStateWithFunction:kernel
                                                                       error:&error];
        if (!_impl->pipeline) return false;

        _impl->commandQueue = [_impl->device newCommandQueue];
        if (!_impl->commandQueue) return false;

        // Shared memory buffers — CPU and GPU access the same physical memory
        MTLResourceOptions opts = MTLResourceStorageModeShared;

        _impl->oscParamsBuf = [_impl->device
            newBufferWithLength:(NSUInteger)(maxOscillators * sizeof(OscillatorParams))
                        options:opts];

        _impl->envValuesBuf = [_impl->device
            newBufferWithLength:(NSUInteger)(maxOscillators * maxBlockSize * sizeof(float))
                        options:opts];

        _impl->outputBuf = [_impl->device
            newBufferWithLength:(NSUInteger)(maxBlockSize * sizeof(float))
                        options:opts];

        if (!_impl->oscParamsBuf || !_impl->envValuesBuf || !_impl->outputBuf)
            return false;

        _impl->available = true;

        NSLog(@"[KawaiiGPU] Metal init OK — device: %@, maxThreads: %lu, "
              "oscBuf: %luB, envBuf: %luB, outBuf: %luB",
              _impl->device.name,
              (unsigned long)_impl->pipeline.maxTotalThreadsPerThreadgroup,
              (unsigned long)_impl->oscParamsBuf.length,
              (unsigned long)_impl->envValuesBuf.length,
              (unsigned long)_impl->outputBuf.length);
    }
    return true;
}

void MetalSineBank::processBlock(
    const OscillatorParams* oscParams,
    const float* envValues,
    int numOscillators,
    float* output,
    int numSamples)
{
    if (!_impl->available || numOscillators == 0 || numSamples == 0) {
        memset(output, 0, (size_t)numSamples * sizeof(float));
        return;
    }

    @autoreleasepool {
        // Write into shared Metal buffers
        memcpy(_impl->oscParamsBuf.contents, oscParams,
               (size_t)numOscillators * sizeof(OscillatorParams));
        memcpy(_impl->envValuesBuf.contents, envValues,
               (size_t)numOscillators * numSamples * sizeof(float));

        // Encode compute command
        id<MTLCommandBuffer> cmdBuf = [_impl->commandQueue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];

        [enc setComputePipelineState:_impl->pipeline];
        [enc setBuffer:_impl->oscParamsBuf offset:0 atIndex:0];
        [enc setBuffer:_impl->envValuesBuf offset:0 atIndex:1];
        [enc setBuffer:_impl->outputBuf    offset:0 atIndex:2];

        uint32_t numOsc  = (uint32_t)numOscillators;
        uint32_t numSamp = (uint32_t)numSamples;
        [enc setBytes:&numOsc  length:sizeof(uint32_t) atIndex:3];
        [enc setBytes:&numSamp length:sizeof(uint32_t) atIndex:4];

        // One thread per sample
        NSUInteger tgSize = MIN((NSUInteger)numSamples,
                                _impl->pipeline.maxTotalThreadsPerThreadgroup);
        [enc dispatchThreads:MTLSizeMake((NSUInteger)numSamples, 1, 1)
       threadsPerThreadgroup:MTLSizeMake(tgSize, 1, 1)];

        [enc endEncoding];
        [cmdBuf commit];
        [cmdBuf waitUntilCompleted];

        // Read result from shared memory
        memcpy(output, _impl->outputBuf.contents, (size_t)numSamples * sizeof(float));

        _impl->dispatchCount++;
        // Log first dispatch and every 10000th thereafter
        if (_impl->dispatchCount == 1 || _impl->dispatchCount % 10000 == 0) {
            NSLog(@"[KawaiiGPU] dispatch #%llu — %d oscillators × %d samples on GPU",
                  _impl->dispatchCount, numOscillators, numSamples);
        }
    }
}

void MetalSineBank::shutdown()
{
    if (!_impl) return;
    _impl->oscParamsBuf  = nil;
    _impl->envValuesBuf  = nil;
    _impl->outputBuf     = nil;
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
