/**
 * KawaiiProcessor.h — VST3 Audio Processor
 *
 * Stripped to bare minimum to match VulcanSynth's proven working pattern.
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

private:
    void updateParameters();
    void processEvent(const Steinberg::Vst::Event& event);
    void processBlockGPU(float** outputs, int32 numChannels, int32 numSamples, double masterVol);
    void processBlockCPU(float** outputs, int32 numChannels, int32 numSamples, double masterVol);

    std::array<KawaiiVoice, kMaxVoices> voices;
    std::array<ParamValue, kNumParams> params;

    // GPU synthesis — hybrid pipeline (GPU sin+sum per-voice, CPU filter per-voice)
    MetalSineBank metalSineBank;
    bool useGPU = false;
    std::vector<OscillatorParams> gpuOscParams;
    std::vector<float> gpuEnvValues;
    std::vector<VoiceDescriptor> gpuVoiceDescs;      // per-voice oscillator ranges
    std::vector<float> gpuPerVoiceOutput;            // [voiceIdx * maxBlock + sample]
};

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
