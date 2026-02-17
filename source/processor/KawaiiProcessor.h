/**
 * KawaiiProcessor.h — VST3 Audio Processor
 *
 * Hybrid GPU+CPU pipeline with async double buffering.
 * GPU computes per-voice sin+sum (non-blocking), CPU applies per-voice filter.
 * One buffer of latency reported to DAW for PDC.
 */

#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "../entry/KawaiiCids.h"
#include "KawaiiVoice.h"
#include "../gpu/MetalSineBank.h"
#include <array>
#include <vector>

namespace Steinberg {
namespace Vst {
namespace Kawaii {

class KawaiiProcessor : public AudioEffect
{
public:
    KawaiiProcessor();
    ~KawaiiProcessor() override;

    static FUnknown* createInstance(void*) { return (IAudioProcessor*)new KawaiiProcessor; }

    tresult PLUGIN_API initialize(FUnknown* context) override;
    tresult PLUGIN_API terminate() override;
    tresult PLUGIN_API setActive(TBool state) override;
    tresult PLUGIN_API process(ProcessData& data) override;
    tresult PLUGIN_API setupProcessing(ProcessSetup& newSetup) override;
    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) override;
    tresult PLUGIN_API setState(IBStream* state) override;
    tresult PLUGIN_API getState(IBStream* state) override;

    // Report latency from async double buffering so DAW can compensate
    uint32 PLUGIN_API getLatencySamples() override;

private:
    void updateParameters();
    void processEvent(const Steinberg::Vst::Event& event);
    void processBlockGPU(float** outputs, int32 numChannels, int32 numSamples, double masterVol);
    void processBlockCPU(float** outputs, int32 numChannels, int32 numSamples, double masterVol);

    std::array<KawaiiVoice, kMaxVoices> voices;
    std::array<ParamValue, kNumParams> params;

    // GPU synthesis — async double-buffered hybrid pipeline
    MetalSineBank metalSineBank;
    bool useGPU = false;
    std::vector<OscillatorParams> gpuOscParams;
    std::vector<float> gpuEnvValues;
    std::vector<VoiceDescriptor> gpuVoiceDescs;
    std::vector<float> gpuPerVoiceOutput;  // receives PREVIOUS block's GPU results

    // Voice mapping for the PREVIOUS GPU dispatch.
    // Needed so Phase 3 (filter) knows which voice[] entry each GPU voice index
    // corresponds to, even if voice activity changed since the dispatch.
    std::array<int, kMaxVoices> prevGpuVoiceMap;  // prevGpuVoiceMap[gpuIdx] = voices[] index
    int prevGpuNumVoices = 0;
};

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
