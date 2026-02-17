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

    // --- DIAGNOSTIC: Only 2 global params to test if Ableton shows them ---

    parameters.addParameter(STR16("Master Volume"), STR16("%"), 0, 0.7,
        ParameterInfo::kCanAutomate, kParamMasterVolume, 0, STR16("Master"));

    parameters.addParameter(STR16("Master Tune"), STR16("cents"), 0, 0.5,
        ParameterInfo::kCanAutomate, kParamMasterTune, 0, STR16("Master"));

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

    // DIAGNOSTIC: only read the 2 registered params
    for (int32 i = 0; i < 2; i++)
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

    // DIAGNOSTIC: only write the 2 registered params
    for (int32 i = 0; i < 2; i++)
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
