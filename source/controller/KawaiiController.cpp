/**
 * KawaiiController.cpp — K50V: Register per-partial level + ADSR params
 */

#include "KawaiiController.h"
#include "../entry/KawaiiCids.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"

namespace Steinberg {
namespace Vst {
namespace Kawaii {

KawaiiController::KawaiiController() {}
KawaiiController::~KawaiiController() {}

// Helper: convert ASCII to char16 for dynamic parameter names
static void asciiToChar16(const char* src, char16* dst, int dstSize)
{
    int i = 0;
    while (src[i] && i < dstSize - 1)
    {
        dst[i] = static_cast<char16>(src[i]);
        i++;
    }
    dst[i] = 0;
}

tresult PLUGIN_API KawaiiController::initialize(FUnknown* context)
{
    tresult result = EditController::initialize(context);
    if (result != kResultOk)
        return result;

    // --- Global params (same pattern as VulcanSynth) ---

    parameters.addParameter(STR16("Master Volume"), STR16("%"), 0, 0.7,
        ParameterInfo::kCanAutomate, kParamMasterVolume, 0, STR16("Master"));

    parameters.addParameter(STR16("Master Tune"), STR16("cents"), 0, 0.5,
        ParameterInfo::kCanAutomate, kParamMasterTune, 0, STR16("Master"));

    // --- Per-partial: Level + ADSR (16 partials x 5 params = 80 params) ---

    for (int i = 0; i < kMaxPartials; i++)
    {
        char buf[32];
        char16 name[128];
        double defLevel = 1.0 / (i + 1);
        int p = i + 1;  // 1-indexed for display

        char16 shortName[32];

        // Level
        snprintf(buf, sizeof(buf), "P%d Level", p);
        asciiToChar16(buf, name, 128);
        snprintf(buf, sizeof(buf), "P%d", p);
        asciiToChar16(buf, shortName, 32);
        parameters.addParameter(name, STR16("%"), 0, defLevel,
            ParameterInfo::kCanAutomate, partialParam(i, kPartialOffLevel), 0, shortName);

        // Attack
        snprintf(buf, sizeof(buf), "P%d Attack", p);
        asciiToChar16(buf, name, 128);
        parameters.addParameter(name, STR16("ms"), 0, 0.01,
            ParameterInfo::kCanAutomate, partialParam(i, kPartialOffAttack), 0, shortName);

        // Decay
        snprintf(buf, sizeof(buf), "P%d Decay", p);
        asciiToChar16(buf, name, 128);
        parameters.addParameter(name, STR16("ms"), 0, 0.3,
            ParameterInfo::kCanAutomate, partialParam(i, kPartialOffDecay), 0, shortName);

        // Sustain
        snprintf(buf, sizeof(buf), "P%d Sustain", p);
        asciiToChar16(buf, name, 128);
        parameters.addParameter(name, STR16("%"), 0, 0.8,
            ParameterInfo::kCanAutomate, partialParam(i, kPartialOffSustain), 0, shortName);

        // Release
        snprintf(buf, sizeof(buf), "P%d Release", p);
        asciiToChar16(buf, name, 128);
        parameters.addParameter(name, STR16("ms"), 0, 0.3,
            ParameterInfo::kCanAutomate, partialParam(i, kPartialOffRelease), 0, shortName);
    }

    return kResultOk;
}

tresult PLUGIN_API KawaiiController::terminate()
{
    return EditController::terminate();
}

// State sync — flat float array matching the Processor
tresult PLUGIN_API KawaiiController::setComponentState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    for (int32 i = 0; i < kNumParams; i++)
    {
        float value;
        int32 numBytesRead;
        if (state->read(&value, sizeof(float), &numBytesRead) != kResultOk)
            return kResultFalse;
        setParamNormalized(i, value);
    }
    return kResultOk;
}

tresult PLUGIN_API KawaiiController::setState(IBStream* state)
{
    return setComponentState(state);
}

tresult PLUGIN_API KawaiiController::getState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    for (int32 i = 0; i < kNumParams; i++)
    {
        float value = static_cast<float>(getParamNormalized(i));
        int32 numBytesWritten;
        if (state->write(&value, sizeof(float), &numBytesWritten) != kResultOk)
            return kResultFalse;
    }
    return kResultOk;
}

IPlugView* PLUGIN_API KawaiiController::createView(FIDString /*name*/)
{
    return nullptr;
}

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
