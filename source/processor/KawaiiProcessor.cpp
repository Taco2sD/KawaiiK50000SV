/**
 * KawaiiProcessor.cpp — K50V: 16-partial additive synth with per-partial ADSR
 */

#include "KawaiiProcessor.h"
#include "../params/KawaiiParams.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/base/ibstream.h"

#include <cstring>
#include <algorithm>

namespace Steinberg {
namespace Vst {
namespace Kawaii {

KawaiiProcessor::KawaiiProcessor()
{
    setControllerClass(ControllerUID);

    params.fill(0.0);
    params[kParamMasterVolume] = 0.7;

    // Default per-partial params: all partials on with 1/n rolloff, shared ADSR
    for (int i = 0; i < kMaxPartials; i++)
    {
        params[partialParam(i, kPartialOffLevel)]   = 1.0 / (i + 1);  // 1/n rolloff
        params[partialParam(i, kPartialOffAttack)]  = 0.01;
        params[partialParam(i, kPartialOffDecay)]   = 0.3;
        params[partialParam(i, kPartialOffSustain)] = 0.8;
        params[partialParam(i, kPartialOffRelease)] = 0.3;
    }
}

KawaiiProcessor::~KawaiiProcessor()
{
}

tresult PLUGIN_API KawaiiProcessor::initialize(FUnknown* context)
{
    tresult result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;

    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    addEventInput(STR16("Event In"), 1);

    return kResultOk;
}

tresult PLUGIN_API KawaiiProcessor::terminate()
{
    return AudioEffect::terminate();
}

tresult PLUGIN_API KawaiiProcessor::setActive(TBool state)
{
    if (state)
    {
        for (auto& voice : voices)
            voice.setSampleRate(processSetup.sampleRate);
    }
    else
    {
        for (auto& voice : voices)
            for (auto& p : voice.partials)
                p.reset();
    }

    return AudioEffect::setActive(state);
}

tresult PLUGIN_API KawaiiProcessor::setupProcessing(ProcessSetup& newSetup)
{
    return AudioEffect::setupProcessing(newSetup);
}

tresult PLUGIN_API KawaiiProcessor::canProcessSampleSize(int32 symbolicSampleSize)
{
    if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
        return kResultTrue;
    return kResultFalse;
}

void KawaiiProcessor::updateParameters()
{
    using namespace ParamRanges;

    for (auto& voice : voices)
    {
        for (int i = 0; i < kMaxPartials; i++)
        {
            // Level
            voice.partials[i].level = params[partialParam(i, kPartialOffLevel)];

            // ADSR (convert normalized 0-1 to real seconds)
            double aSec = normalizedToMs(params[partialParam(i, kPartialOffAttack)],  kEnvAttackMin,  kEnvAttackMax) / 1000.0;
            double dSec = normalizedToMs(params[partialParam(i, kPartialOffDecay)],   kEnvDecayMin,   kEnvDecayMax) / 1000.0;
            double sLvl = params[partialParam(i, kPartialOffSustain)];
            double rSec = normalizedToMs(params[partialParam(i, kPartialOffRelease)], kEnvReleaseMin, kEnvReleaseMax) / 1000.0;

            voice.partials[i].envelope.setAttack(aSec);
            voice.partials[i].envelope.setDecay(dSec);
            voice.partials[i].envelope.setSustain(sLvl);
            voice.partials[i].envelope.setRelease(rSec);
        }
    }
}

void KawaiiProcessor::processEvent(const Event& event)
{
    switch (event.type)
    {
        case Event::kNoteOnEvent:
        {
            if (event.noteOn.velocity == 0.0f)
            {
                for (auto& voice : voices)
                    if (voice.getNoteNumber() == event.noteOn.pitch && voice.isActive())
                        voice.noteOff();
            }
            else
            {
                KawaiiVoice* target = nullptr;
                for (auto& voice : voices)
                {
                    if (!voice.isActive())
                    {
                        target = &voice;
                        break;
                    }
                }
                if (!target)
                    target = &voices[0];

                target->noteOn(event.noteOn.pitch, event.noteOn.velocity);
            }
            break;
        }

        case Event::kNoteOffEvent:
        {
            for (auto& voice : voices)
                if (voice.getNoteNumber() == event.noteOff.pitch && voice.isActive())
                    voice.noteOff();
            break;
        }
    }
}

tresult PLUGIN_API KawaiiProcessor::process(ProcessData& data)
{
    // Parameter changes
    if (data.inputParameterChanges)
    {
        int32 numParamsChanged = data.inputParameterChanges->getParameterCount();
        for (int32 i = 0; i < numParamsChanged; i++)
        {
            IParamValueQueue* paramQueue = data.inputParameterChanges->getParameterData(i);
            if (paramQueue)
            {
                ParamValue value;
                int32 sampleOffset;
                int32 numPoints = paramQueue->getPointCount();
                if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
                {
                    ParamID id = paramQueue->getParameterId();
                    if (id < kNumParams)
                        params[id] = value;
                }
            }
        }
    }

    // MIDI events
    if (data.inputEvents)
    {
        int32 numEvents = data.inputEvents->getEventCount();
        for (int32 i = 0; i < numEvents; i++)
        {
            Event event;
            if (data.inputEvents->getEvent(i, event) == kResultOk)
                processEvent(event);
        }
    }

    updateParameters();

    if (data.numOutputs == 0)
        return kResultOk;

    int32 numChannels = data.outputs[0].numChannels;
    int32 numSamples = data.numSamples;
    float** outputs = data.outputs[0].channelBuffers32;

    if (data.symbolicSampleSize == kSample64)
        return kResultFalse;

    for (int32 ch = 0; ch < numChannels; ch++)
        memset(outputs[ch], 0, numSamples * sizeof(float));

    double masterVol = params[kParamMasterVolume];
    for (auto& voice : voices)
    {
        if (!voice.isActive())
            continue;

        for (int32 i = 0; i < numSamples; i++)
        {
            double outL = 0.0, outR = 0.0;
            voice.process(&outL, &outR);

            for (int32 ch = 0; ch < numChannels; ch++)
                outputs[ch][i] += static_cast<float>((ch == 0 ? outL : outR) * masterVol);
        }
    }

    for (int32 ch = 0; ch < numChannels; ch++)
        for (int32 i = 0; i < numSamples; i++)
            outputs[ch][i] = std::clamp(outputs[ch][i], -1.0f, 1.0f);

    return kResultOk;
}

// State: flat float array
tresult PLUGIN_API KawaiiProcessor::setState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    for (auto& param : params)
    {
        float value;
        int32 numBytesRead;
        if (state->read(&value, sizeof(float), &numBytesRead) != kResultOk)
            return kResultFalse;
        param = value;
    }
    return kResultOk;
}

tresult PLUGIN_API KawaiiProcessor::getState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    for (const auto& param : params)
    {
        float value = static_cast<float>(param);
        int32 numBytesWritten;
        if (state->write(&value, sizeof(float), &numBytesWritten) != kResultOk)
            return kResultFalse;
    }
    return kResultOk;
}

}}} // namespaces
