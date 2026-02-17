/**
 * KawaiiController.cpp — K50V: Register per-partial level + ADSR params
 */

#include "KawaiiController.h"
#include "../entry/KawaiiCids.h"
#include "../editor/KawaiiEditor.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

namespace Steinberg {
namespace Vst {
namespace Kawaii {

KawaiiController::KawaiiController() {}
KawaiiController::~KawaiiController() {}

tresult PLUGIN_API KawaiiController::initialize(FUnknown* context)
{
    tresult result = EditController::initialize(context);
    if (result != kResultOk)
        return result;

    // --- Global params ---

    parameters.addParameter(STR16("Master Volume"), STR16("%"), 0, 0.7,
        ParameterInfo::kCanAutomate, kParamMasterVolume, 0, STR16("Master"));

    parameters.addParameter(STR16("Master Tune"), STR16("cents"), 0, 0.5,
        ParameterInfo::kCanAutomate, kParamMasterTune, 0, STR16("Master"));

    // --- Per-partial: Level + ADSR (16 partials x 5 params = 80 params) ---
    // Matching the old working Macro loop pattern exactly

    for (int i = 0; i < kMaxPartials; i++)  // 16 partials = 82 params
    {
        int p = i + 1;
        double defLevel = 1.0 / (i + 1);
        char tempName[32];
        char16 name[32];

        // Level
        snprintf(tempName, sizeof(tempName), "P%d Level", p);
        for (int j = 0; j <= (int)strlen(tempName); j++)
            name[j] = static_cast<char16>(tempName[j]);
        parameters.addParameter(name, STR16("%"), 0, defLevel,
            ParameterInfo::kCanAutomate, partialParam(i, kPartialOffLevel), 0, STR16("Partials"));

        // Attack
        snprintf(tempName, sizeof(tempName), "P%d Attack", p);
        for (int j = 0; j <= (int)strlen(tempName); j++)
            name[j] = static_cast<char16>(tempName[j]);
        parameters.addParameter(name, STR16("ms"), 0, 0.01,
            ParameterInfo::kCanAutomate, partialParam(i, kPartialOffAttack), 0, STR16("Partials"));

        // Decay
        snprintf(tempName, sizeof(tempName), "P%d Decay", p);
        for (int j = 0; j <= (int)strlen(tempName); j++)
            name[j] = static_cast<char16>(tempName[j]);
        parameters.addParameter(name, STR16("ms"), 0, 0.3,
            ParameterInfo::kCanAutomate, partialParam(i, kPartialOffDecay), 0, STR16("Partials"));

        // Sustain
        snprintf(tempName, sizeof(tempName), "P%d Sustain", p);
        for (int j = 0; j <= (int)strlen(tempName); j++)
            name[j] = static_cast<char16>(tempName[j]);
        parameters.addParameter(name, STR16("%"), 0, 0.8,
            ParameterInfo::kCanAutomate, partialParam(i, kPartialOffSustain), 0, STR16("Partials"));

        // Release
        snprintf(tempName, sizeof(tempName), "P%d Release", p);
        for (int j = 0; j <= (int)strlen(tempName); j++)
            name[j] = static_cast<char16>(tempName[j]);
        parameters.addParameter(name, STR16("ms"), 0, 0.3,
            ParameterInfo::kCanAutomate, partialParam(i, kPartialOffRelease), 0, STR16("Partials"));
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

IPlugView* PLUGIN_API KawaiiController::createView(FIDString name)
{
    if (FIDStringsEqual(name, ViewType::kEditor))
        return new KawaiiEditor(this);
    return nullptr;
}

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
