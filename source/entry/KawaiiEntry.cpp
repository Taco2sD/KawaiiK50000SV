/**
 * KawaiiEntry.cpp — VST3 Plugin Factory
 * =======================================
 *
 * This is the "front door" of the plugin. When a DAW scans for VST3 plugins,
 * it loads the shared library (the .vst3 bundle) and looks for a function called
 * GetPluginFactory(). That function returns a "factory" object that tells the DAW:
 *
 *   "Here's what I can create for you:
 *    1. A KawaiiProcessor (audio processing) — identified by ProcessorUID
 *    2. A KawaiiController (parameters/UI) — identified by ControllerUID"
 *
 * The DAW then uses these factory entries to create instances of our classes
 * whenever it needs them (when the user adds the plugin to a track, when
 * loading a saved project, etc.).
 *
 * MACROS USED:
 *   BEGIN_FACTORY_DEF / END_FACTORY — Steinberg macros that generate the
 *     GetPluginFactory() function and the factory object.
 *   DEF_CLASS2 — Registers one class (Processor or Controller) with the factory.
 *   INLINE_UID_FROM_FUID — Converts our FUID objects to the inline format
 *     that the macro expects.
 *
 * InitModule() / DeinitModule() — Called once when the .vst3 bundle is loaded/
 *   unloaded. We don't need to do anything in them (no global state to set up),
 *   but they must exist or the plugin won't load.
 */

#include "../processor/KawaiiProcessor.h"
#include "../controller/KawaiiController.h"
#include "../entry/KawaiiCids.h"
#include "public.sdk/source/main/pluginfactory.h"

// The name shown to the user in the DAW's plugin list.
#define stringPluginName "Kawaii K50000SV"

using namespace Steinberg::Vst;
using namespace Steinberg::Vst::Kawaii;

// ============================================================================
// Module Init / Deinit
// ============================================================================
// These functions are called once when the VST3 bundle is loaded into memory
// (InitModule) and when it's unloaded (DeinitModule). They're required by the
// VST3 SDK even if you don't need them. Returning true means "success".

bool InitModule()   { return true; }
bool DeinitModule() { return true; }

// ============================================================================
// Plugin Factory
// ============================================================================
// This macro block generates the GetPluginFactory() function that the DAW calls.

BEGIN_FACTORY_DEF(
    "XenonBug",                           // Vendor name (shown in DAW plugin info)
    "https://github.com/Taco2sD",         // Vendor URL
    "mailto:xenonbug@example.com"          // Contact email
)

    // Register the PROCESSOR class.
    // This is the component that does the actual audio work.
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(ProcessorUID),     // Unique ID for this class
        PClassInfo::kManyInstances,              // The DAW can create multiple instances
        kVstAudioEffectClass,                    // This is an audio effect (includes instruments)
        stringPluginName,                        // Display name
        Vst::kDistributable,                     // Processor can run separately from Controller
        Vst::PlugType::kInstrumentSynth,         // It's a synthesizer instrument
        "1.0.0",                                 // Plugin version
        kVstVersionString,                       // VST3 SDK version we're built against
        KawaiiProcessor::createInstance           // Factory function to create instances
    )

    // Register the CONTROLLER class.
    // This is the component that manages parameters and UI.
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(ControllerUID),    // Unique ID for this class
        PClassInfo::kManyInstances,
        kVstComponentControllerClass,            // This is a component controller
        stringPluginName "Controller",           // Display name (appends "Controller")
        0,                                       // No special flags for controllers
        "",                                      // No sub-categories
        "1.0.0",
        kVstVersionString,
        KawaiiController::createInstance
    )

END_FACTORY
