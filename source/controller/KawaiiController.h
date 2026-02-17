/**
 * KawaiiController.h — VST3 Edit Controller (Header)
 * ====================================================
 *
 * The Controller is the other half of the VST3 plugin architecture.
 * While the Processor handles audio in real-time, the Controller handles:
 *
 *   1. PARAMETER REGISTRATION — telling the DAW what parameters exist,
 *      their names, ranges, default values, and units.
 *
 *   2. UI MANAGEMENT — creating and managing the plugin's graphical editor
 *      window (Phase 8; returns nullptr for now to use the DAW's generic UI).
 *
 *   3. STATE SYNC — when the DAW loads a saved project, the Controller
 *      receives the saved parameter values and updates the UI accordingly.
 *
 * WHY SEPARATE FROM PROCESSOR?
 *   VST3 enforces this split for thread safety and distribution. The Processor
 *   runs in a real-time audio thread (must never block). The Controller runs
 *   in the UI thread (can take its time). They communicate through the DAW's
 *   message-passing system, never directly. This means the plugin could
 *   theoretically run the Processor on one machine and the Controller on
 *   another (Steinberg's "kDistributable" flag enables this).
 */

#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"  // Base class for controllers
#include "../entry/KawaiiCids.h"                       // Parameter IDs

namespace Steinberg {
namespace Vst {
namespace Kawaii {

class KawaiiController : public EditController
{
public:
    KawaiiController();
    ~KawaiiController() override;

    /**
     * Static factory method — VST3 framework calls this to create the controller.
     * Registered in KawaiiEntry.cpp alongside the Processor factory.
     */
    static FUnknown* createInstance(void*) { return (IEditController*)new KawaiiController; }

    // --- VST3 lifecycle methods ---

    /**
     * Called once when the controller is created.
     * This is where we register all our parameters with the DAW.
     */
    tresult PLUGIN_API initialize(FUnknown* context) override;

    /** Called when the controller is being destroyed. */
    tresult PLUGIN_API terminate() override;

    /**
     * Called when the DAW loads a saved project.
     * The Processor's saved state is passed here so the Controller can
     * update its parameter displays to match.
     *
     * Note the distinction:
     *   - setComponentState(): receives the PROCESSOR's state
     *   - setState(): receives the CONTROLLER's own state (UI layout, etc.)
     * For now they're the same since we have no custom UI state.
     */
    tresult PLUGIN_API setComponentState(IBStream* state) override;
    tresult PLUGIN_API setState(IBStream* state) override;
    tresult PLUGIN_API getState(IBStream* state) override;

    /**
     * Called when the DAW wants to show the plugin's UI window.
     * Returns nullptr for Phase 1 — the DAW will show its own generic
     * parameter editor (basic sliders). Custom UI comes in Phase 8.
     */
    IPlugView* PLUGIN_API createView(FIDString name) override;
};

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
