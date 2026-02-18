/**
 * KawaiiProcessor.cpp — K50V: 32-partial additive synth with ZDF SVF filter
 *
 * Async double-buffered GPU+CPU pipeline:
 *   Phase 1 (CPU): Pre-compute per-partial ADSR envelopes, build VoiceDescriptors
 *   Phase 2 (GPU): Submit to Metal (non-blocking), retrieve PREVIOUS block's results
 *   Phase 3 (CPU): Per-voice ZDF SVF filter on previous results + mix to stereo
 *
 * The audio thread never blocks on GPU. One buffer of latency, DAW-compensated via PDC.
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

uint32 PLUGIN_API KawaiiProcessor::getLatencySamples()
{
    // Async double buffering adds one buffer of latency when GPU is active.
    // The DAW uses this to shift other tracks forward (plugin delay compensation).
    if (useGPU)
        return static_cast<uint32>(metalSineBank.getLatencySamples());
    return 0;
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
// Async double-buffered GPU+CPU render path
//
// The audio thread NEVER blocks on GPU completion. Instead:
//   Phase 1: Prepare current block's GPU data (ADSR pre-computation)
//   Phase 2: Submit current block to GPU (non-blocking) + retrieve previous results
//   Phase 3: Apply CPU-side ZDF SVF filter to PREVIOUS block's GPU output
//
// One buffer of latency, compensated by DAW via getLatencySamples().
// ============================================================================

void KawaiiProcessor::processBlockGPU(float** outputs, int32 numChannels, int32 numSamples, double masterVol)
{
    double sr = processSetup.sampleRate;

    // =========================================================================
    // Phase 1: CPU — Prepare current block for GPU dispatch
    //
    // Collect oscillators grouped by voice, pre-compute per-sample ADSR
    // envelopes, build VoiceDescriptors, record voice mapping.
    // =========================================================================

    int numOsc = 0;
    int numVoices = 0;
    std::array<int, kMaxVoices> currentVoiceMap;

    for (int v = 0; v < kMaxVoices; v++)
    {
        auto& voice = voices[v];
        if (!voice.isActive()) continue;

        int voiceStartOsc = numOsc;

        for (int p = 0; p < kMaxPartials; p++)
        {
            auto& partial = voice.partials[p];
            if (!partial.envelope.isActive()) continue;

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
            partial.phase -= static_cast<int>(partial.phase);

            numOsc++;
        }

        gpuVoiceDescs[(size_t)numVoices] = {
            static_cast<uint32_t>(voiceStartOsc),
            static_cast<uint32_t>(numOsc - voiceStartOsc),
            static_cast<float>(voice.getVelocity() / static_cast<double>(kMaxPartials)),
            0.0f
        };

        // Record which voices[] index maps to this GPU voice index
        currentVoiceMap[(size_t)numVoices] = v;
        numVoices++;
    }

    // =========================================================================
    // Phase 2: Submit current block to GPU + retrieve previous block's results
    //
    // processBlock is NON-BLOCKING: it commits the current block's command
    // buffer and immediately returns the PREVIOUS block's GPU output.
    // =========================================================================

    int prevNumVoices = 0;
    int prevNumSamples = 0;

    metalSineBank.processBlock(
        gpuOscParams.data(),
        gpuEnvValues.data(),
        numOsc,
        gpuVoiceDescs.data(),
        numVoices,
        numSamples,
        gpuPerVoiceOutput.data(),
        prevNumVoices,
        prevNumSamples
    );

    // =========================================================================
    // Phase 3: CPU — Filter PREVIOUS block's GPU output + mix to stereo
    //
    // Uses prevGpuVoiceMap (saved from the PREVIOUS call) to know which
    // voice[] entry each GPU voice index corresponds to.
    //
    // Sub-block processing (Surge XT pattern):
    //   The buffer is subdivided into 32-sample sub-blocks. At each sub-block
    //   boundary, target filter coefficients are computed from the smoothed
    //   cutoff/resonance/envelope. Within the sub-block, coefficients are
    //   linearly interpolated per-sample (a1 += da1, etc.), eliminating
    //   zipper noise from parameter changes. This means tan() is called
    //   once per sub-block (~1378×/sec) instead of once per sample (~44100×/sec).
    // =========================================================================

    static constexpr int kSubBlockSize = 32;  // ~0.7ms at 44.1kHz — matches Surge XT

    if (prevNumVoices > 0 && prevNumSamples > 0)
    {
        int totalSamples = std::min(prevNumSamples, numSamples);

        for (int i = 0; i < prevNumVoices; i++)
        {
            int vIdx = prevGpuVoiceMap[(size_t)i];
            auto& voice = voices[vIdx];

            float* voiceBuf = &gpuPerVoiceOutput[(size_t)(i * prevNumSamples)];

            // Process in sub-blocks of kSubBlockSize samples
            for (int subStart = 0; subStart < totalSamples; subStart += kSubBlockSize)
            {
                int subEnd = std::min(subStart + kSubBlockSize, totalSamples);
                int subLen = subEnd - subStart;

                // Advance smoothers and envelope to the END of this sub-block
                // to get the target parameter values for coefficient computation.
                // (Evaluating at the end means the interpolation approaches the
                // target by the last sample — matching Surge's convention.)
                double envValue = 0.0, smoothedNorm = 0.0, smoothedReso = 0.0;
                for (int s = 0; s < subLen; s++)
                {
                    envValue     = voice.processFilterEnvelope();
                    smoothedNorm = voice.processFilterCutoffSmooth();
                    smoothedReso = voice.processFilterResoSmooth();
                }

                // Compute effective cutoff Hz using voice helper
                // (exponential Hz mapping + envelope mod + keytrack)
                double effectiveCutoff = voice.computeEffectiveCutoff(smoothedNorm, envValue);

                // Compute target coefficients + set up per-sample interpolation.
                // CytomicSVF expects raw resonance 0..0.98, NOT Q.
                // tan() is called ONCE here, then coefficients ramp linearly
                // across subLen samples via processBlockStep().
                voice.prepareFilterBlock(effectiveCutoff, smoothedReso, subLen);

                // Tight inner loop: filter + mix to stereo
                for (int32 s = subStart; s < subEnd; s++)
                {
                    double sample = static_cast<double>(voiceBuf[s]);
                    sample = voice.filterBlockStep(sample);

                    for (int32 ch = 0; ch < numChannels; ch++)
                        outputs[ch][s] += static_cast<float>(sample * masterVol);
                }
            }
        }

        // Clamp
        for (int32 ch = 0; ch < numChannels; ch++)
            for (int32 s = 0; s < numSamples; s++)
                outputs[ch][s] = std::clamp(outputs[ch][s], -1.0f, 1.0f);
    }

    // Save current voice mapping for the NEXT call's Phase 3
    prevGpuVoiceMap = currentVoiceMap;
    prevGpuNumVoices = numVoices;
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
