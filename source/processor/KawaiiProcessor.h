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
#include <array>

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

    std::array<KawaiiVoice, kMaxVoices> voices;
    std::array<ParamValue, kNumParams> params;
};

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
