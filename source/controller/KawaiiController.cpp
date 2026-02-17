/**
 * KawaiiController.cpp — K50V: Register 32-partial + filter params
 */

#include "KawaiiController.h"
#include "../entry/KawaiiCids.h"
#include "../params/KawaiiParams.h"
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

    // --- Per-partial: Level + ADSR (32 partials x 5 params = 160 params) ---

    for (int i = 0; i < kMaxPartials; i++)
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

    // --- Filter section (9 params) ---
    using namespace ParamRanges;

    // Filter Type — discrete list param: LP, HP, BP, Notch
    auto* typeParam = new StringListParameter(
        STR16("Filter Type"), kParamFilterType, nullptr, ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
    typeParam->appendString(STR16("Low Pass"));
    typeParam->appendString(STR16("High Pass"));
    typeParam->appendString(STR16("Band Pass"));
    typeParam->appendString(STR16("Notch"));
    parameters.addParameter(typeParam);

    // Cutoff (normalized 0–1, exponential mapping 20Hz–20kHz in processor)
    parameters.addParameter(STR16("Filter Cutoff"), STR16("Hz"), 0, kFilterCutoffDefault,
        ParameterInfo::kCanAutomate, kParamFilterCutoff, 0, STR16("Filter"));

    // Resonance (0–1)
    parameters.addParameter(STR16("Filter Reso"), STR16("%"), 0, kFilterResoDefault,
        ParameterInfo::kCanAutomate, kParamFilterReso, 0, STR16("Filter"));

    // Filter Envelope ADSR
    parameters.addParameter(STR16("Flt Env Atk"), STR16("ms"), 0, 0.01,
        ParameterInfo::kCanAutomate, kParamFilterEnvAtk, 0, STR16("Filter"));

    parameters.addParameter(STR16("Flt Env Dec"), STR16("ms"), 0, 0.3,
        ParameterInfo::kCanAutomate, kParamFilterEnvDec, 0, STR16("Filter"));

    parameters.addParameter(STR16("Flt Env Sus"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kParamFilterEnvSus, 0, STR16("Filter"));

    parameters.addParameter(STR16("Flt Env Rel"), STR16("ms"), 0, 0.3,
        ParameterInfo::kCanAutomate, kParamFilterEnvRel, 0, STR16("Filter"));

    // Env Depth (bipolar: 0.5 = no modulation)
    parameters.addParameter(STR16("Flt Env Depth"), STR16("%"), 0, kFilterEnvDepthDefault,
        ParameterInfo::kCanAutomate, kParamFilterEnvDep, 0, STR16("Filter"));

    // Keytrack (0 = none, 1 = full)
    parameters.addParameter(STR16("Flt Keytrack"), STR16("%"), 0, kFilterKeytrackDefault,
        ParameterInfo::kCanAutomate, kParamFilterKeytrk, 0, STR16("Filter"));

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
