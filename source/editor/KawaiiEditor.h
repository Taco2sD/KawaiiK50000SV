#pragma once

#include "public.sdk/source/vst/vstguieditor.h"
#include "../entry/KawaiiCids.h"
#include <unordered_map>

namespace VSTGUI { class CTextLabel; }

namespace Steinberg {
namespace Vst {
namespace Kawaii {

class KawaiiEditor : public VSTGUIEditor, public VSTGUI::IControlListener
{
public:
    KawaiiEditor(void* controller);
    ~KawaiiEditor() override;

    bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) override;
    void PLUGIN_API close() override;

    // IControlListener — called when user moves a knob
    void valueChanged(VSTGUI::CControl* control) override;

private:
    void createControls();
    void updateValueLabel(Vst::ParamID tag, double value);

    // Map from param ID to its value display label, so valueChanged can update it
    std::unordered_map<int32, VSTGUI::CTextLabel*> valueLabels;
};

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
