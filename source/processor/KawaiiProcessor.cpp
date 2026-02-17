/**
 * KawaiiProcessor.cpp — K50V: 32-partial additive synth with ZDF SVF filter
 *
 * Hybrid GPU+CPU pipeline:
 *   Phase 1 (CPU): Pre-compute per-partial ADSR envelopes, build VoiceDescriptors
 *   Phase 2 (GPU): Metal compute — sin() × level × env per-voice summation
 *   Phase 3 (CPU): Per-voice ZDF SVF filter + mix to stereo output
 *
 * Falls back to pure CPU path if Metal is unavailable.
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

    // Filter defaults: fully open LP, no modulation
    params[kParamFilterType]    = 0.0;   // LP
    params[kParamFilterCutoff]  = ParamRanges::kFilterCutoffDefault;  // 1.0 = 20kHz (fully open)
    params[kParamFilterReso]    = ParamRanges::kFilterResoDefault;    // 0.0 = no resonance
    params[kParamFilterEnvAtk]  = 0.01;
    params[kParamFilterEnvDec]  = 0.3;
    params[kParamFilterEnvSus]  = 0.0;
    params[kParamFilterEnvRel]  = 0.3;
    params[kParamFilterEnvDep]  = ParamRanges::kFilterEnvDepthDefault;  // 0.5 = no modulation
    params[kParamFilterKeytrk]  = ParamRanges::kFilterKeytrackDefault;  // 0.0 = no tracking
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

        int maxOsc = kMaxVoices * kMaxPartials;
        int maxBlock = (int)processSetup.maxSamplesPerBlock;
        if (maxBlock <= 0) maxBlock = 4096;

        // Initialize Metal with per-voice support
        bool gpuOk = metalSineBank.init(maxOsc, maxBlock, kMaxVoices);

        // Allocate CPU-side buffers for hybrid GPU+CPU pipeline
        gpuOscParams.resize((size_t)maxOsc);
        gpuEnvValues.resize((size_t)(maxOsc * maxBlock));
        gpuVoiceDescs.resize(kMaxVoices);
        gpuPerVoiceOutput.resize((size_t)(kMaxVoices * maxBlock));

        // Enable GPU if Metal initialized successfully
        useGPU = gpuOk && metalSineBank.isAvailable();
    }
    else
    {
        metalSineBank.shutdown();
        useGPU = false;

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

    // --- Filter params (shared across all voices) ---
    // Pass normalized cutoff directly — voice smooths in normalized space
    // then converts to Hz per-sample for perceptually uniform sweeps
    double filterCutoffNorm = params[kParamFilterCutoff];
    double filterReso       = params[kParamFilterReso];

    // Filter type: discrete 0–3 mapped from normalized 0–1
    int filterTypeInt = static_cast<int>(params[kParamFilterType] * (kNumFilterTypes - 1) + 0.5);
    FilterType filterType = static_cast<FilterType>(std::clamp(filterTypeInt, 0, kNumFilterTypes - 1));

    // Filter envelope ADSR (same exponential time mapping as partial envelopes)
    double fAtk = normalizedToMs(params[kParamFilterEnvAtk], kEnvAttackMin, kEnvAttackMax) / 1000.0;
    double fDec = normalizedToMs(params[kParamFilterEnvDec], kEnvDecayMin, kEnvDecayMax) / 1000.0;
    double fSus = params[kParamFilterEnvSus];
    double fRel = normalizedToMs(params[kParamFilterEnvRel], kEnvReleaseMin, kEnvReleaseMax) / 1000.0;

    // Env depth: normalized 0–1 → bipolar -1 to +1 (0.5 = no modulation)
    double filterEnvDepth = (params[kParamFilterEnvDep] - 0.5) * 2.0;

    double filterKeytrack = params[kParamFilterKeytrk];

    for (auto& voice : voices)
    {
        // --- Per-partial params ---
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

        // --- Filter params ---
        voice.setFilterCutoffNorm(filterCutoffNorm);
        voice.setFilterResonance(filterReso);
        voice.setFilterType(filterType);
        voice.setFilterEnvAttack(fAtk);
        voice.setFilterEnvDecay(fDec);
        voice.setFilterEnvSustain(fSus);
        voice.setFilterEnvRelease(fRel);
        voice.setFilterEnvDepth(filterEnvDepth);
        voice.setFilterKeytrack(filterKeytrack);
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

// ============================================================================
// Hybrid GPU+CPU render path
//
// Phase 1 (CPU): Collect oscillator params grouped by voice, pre-compute
//                ADSR envelopes per-sample, build VoiceDescriptors.
// Phase 2 (GPU): Metal compute — parallel sin() + per-voice summation.
// Phase 3 (CPU): Read per-voice GPU output, apply ZDF SVF filter per-voice,
//                mix filtered voices to stereo output.
// ============================================================================

void KawaiiProcessor::processBlockGPU(float** outputs, int32 numChannels, int32 numSamples, double masterVol)
{
    double sr = processSetup.sampleRate;
    int numOsc = 0;       // Total oscillators across all active voices
    int numVoices = 0;    // Number of active voices sent to GPU

    // =========================================================================
    // Phase 1: CPU — Collect oscillators grouped by voice, pre-compute ADSR
    // =========================================================================

    for (int v = 0; v < kMaxVoices; v++)
    {
        auto& voice = voices[v];
        if (!voice.isActive()) continue;

        int voiceStartOsc = numOsc;

        for (int p = 0; p < kMaxPartials; p++)
        {
            auto& partial = voice.partials[p];
            if (!partial.envelope.isActive()) continue;

            // Pack oscillator params (velocityScale unused by per-voice kernel)
            gpuOscParams[(size_t)numOsc] = {
                static_cast<float>(partial.phase),
                static_cast<float>(partial.frequency / sr),
                static_cast<float>(partial.level),
                1.0f  // unused — velocity applied via VoiceDescriptor
            };

            // Run ADSR forward per-sample on CPU, capturing values for GPU.
            // (ADSR is sequential/stateful — cannot be parallelized on GPU.)
            for (int32 s = 0; s < numSamples; s++)
                gpuEnvValues[(size_t)(numOsc * numSamples + s)] =
                    static_cast<float>(partial.envelope.process());

            // Advance phase on CPU (double precision for accuracy)
            partial.phase += numSamples * (partial.frequency / sr);
            partial.phase -= static_cast<int>(partial.phase);  // wrap to [0, 1)

            numOsc++;
        }

        // Build voice descriptor for GPU
        gpuVoiceDescs[(size_t)numVoices] = {
            static_cast<uint32_t>(voiceStartOsc),
            static_cast<uint32_t>(numOsc - voiceStartOsc),
            static_cast<float>(voice.getVelocity() / static_cast<double>(kMaxPartials)),
            0.0f  // padding
        };
        numVoices++;
    }

    if (numVoices == 0) return;

    // =========================================================================
    // Phase 2: GPU — Parallel sin() computation + per-voice summation
    //
    // Each GPU thread handles one (voice, sample) pair.
    // Output: gpuPerVoiceOutput[voiceIdx * numSamples + sampleIdx]
    // =========================================================================

    metalSineBank.processBlock(
        gpuOscParams.data(),
        gpuEnvValues.data(),
        numOsc,
        gpuVoiceDescs.data(),
        numVoices,
        gpuPerVoiceOutput.data(),
        numSamples
    );

    // =========================================================================
    // Phase 3: CPU — Per-voice ZDF SVF filter + mix to stereo
    //
    // Each voice's GPU output is a pre-summed mono stream (sin × level × env
    // × velocityScale). We apply the same filter chain as KawaiiVoice::process()
    // but reading from the GPU buffer instead of computing sin() on CPU.
    // =========================================================================

    int gpuVoiceIdx = 0;
    for (int v = 0; v < kMaxVoices; v++)
    {
        auto& voice = voices[v];
        if (!voice.isActive()) continue;

        float* voiceBuf = &gpuPerVoiceOutput[(size_t)(gpuVoiceIdx * numSamples)];

        for (int32 s = 0; s < numSamples; s++)
        {
            double sample = static_cast<double>(voiceBuf[s]);

            // Advance filter state per-sample (same logic as KawaiiVoice::process)
            double envValue    = voice.processFilterEnvelope();
            double smoothedNorm = voice.processFilterCutoffSmooth();
            double smoothedReso = voice.processFilterResoSmooth();

            // Convert smoothed normalized cutoff to Hz (exponential mapping)
            // 20 * 1000^norm: norm=0 → 20 Hz, norm=0.5 → 632 Hz, norm=1 → 20 kHz
            double baseCutoffHz = 20.0 * std::pow(1000.0, smoothedNorm);

            // Env depth is bipolar: -1 to +1 — modulates cutoff by up to ±10kHz
            double envMod = voice.getFilterEnvDepth() * envValue * 10000.0;

            // Keytrack: 0 = no tracking, 1 = full (100 Hz/semitone from C3 = MIDI 60)
            double keyMod = voice.getFilterKeytrack() * (voice.getNoteNumber() - 60) * 100.0;

            double effectiveCutoff = std::clamp(baseCutoffHz + envMod + keyMod, 20.0, 20000.0);
            double Q = 0.5 + smoothedReso * 24.5;

            sample = voice.applyFilter(sample, effectiveCutoff, Q);

            // Mix into stereo output
            for (int32 ch = 0; ch < numChannels; ch++)
                outputs[ch][s] += static_cast<float>(sample * masterVol);
        }

        gpuVoiceIdx++;
    }

    // Clamp final output
    for (int32 ch = 0; ch < numChannels; ch++)
        for (int32 s = 0; s < numSamples; s++)
            outputs[ch][s] = std::clamp(outputs[ch][s], -1.0f, 1.0f);
}

// ============================================================================
// CPU render path — includes per-voice ZDF SVF filter
// ============================================================================

void KawaiiProcessor::processBlockCPU(float** outputs, int32 numChannels, int32 numSamples, double masterVol)
{
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
}

// ============================================================================
// VST3 process callback
// ============================================================================

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
        memset(outputs[ch], 0, (size_t)numSamples * sizeof(float));

    double masterVol = params[kParamMasterVolume];

    if (useGPU)
        processBlockGPU(outputs, numChannels, numSamples, masterVol);
    else
        processBlockCPU(outputs, numChannels, numSamples, masterVol);

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
