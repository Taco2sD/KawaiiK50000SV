/**
 * KawaiiController.cpp — K50V: Register per-partial level + ADSR params
 */

#include "KawaiiController.h"
#include "../entry/KawaiiCids.h"
#include "pluginterfaces/base/ibstream.h"

#include <cstdio>
#include <cstring>

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

    // --- Global ---
    parameters.addParameter(
        STR16("Master Volume"), STR16("%"),
        0, 0.7,
        ParameterInfo::kCanAutomate,
        kParamMasterVolume, 0,
        STR16("Global")
    );

    parameters.addParameter(
        STR16("Master Tune"), STR16("cents"),
        0, 0.5,
        ParameterInfo::kCanAutomate,
        kParamMasterTune, 0,
        STR16("Global")
    );

    // --- Per-partial: Level + ADSR ---
    for (int i = 0; i < kMaxPartials; i++)
    {
        // Build section name: "Partial 1", "Partial 2", etc.
        char secBuf[32];
        snprintf(secBuf, sizeof(secBuf), "Partial %d", i + 1);
        char16 section[32];
        for (int j = 0; j <= static_cast<int>(strlen(secBuf)); j++)
            section[j] = static_cast<char16>(secBuf[j]);

        // Helper to make a name like "P1 Level", "P2 Attack", etc.
        auto addParam = [&](const char* suffix, const char* unit, double def, int offset)
        {
            char nameBuf[32];
            snprintf(nameBuf, sizeof(nameBuf), "P%d %s", i + 1, suffix);
            char16 name[32];
            for (int j = 0; j <= static_cast<int>(strlen(nameBuf)); j++)
                name[j] = static_cast<char16>(nameBuf[j]);

            char16 unitStr[16];
            for (int j = 0; j <= static_cast<int>(strlen(unit)); j++)
                unitStr[j] = static_cast<char16>(unit[j]);

            parameters.addParameter(
                name, unitStr,
                0, def,
                ParameterInfo::kCanAutomate,
                partialParam(i, offset), 0,
                section
            );
        };

        double defaultLevel = 1.0 / (i + 1);  // 1/n rolloff
        addParam("Level",   "%",  defaultLevel, kPartialOffLevel);
        addParam("Attack",  "ms", 0.01,         kPartialOffAttack);
        addParam("Decay",   "ms", 0.3,          kPartialOffDecay);
        addParam("Sustain", "%",  0.8,          kPartialOffSustain);
        addParam("Release", "ms", 0.3,          kPartialOffRelease);
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
    return nullptr;  // Custom UI coming next
}

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
