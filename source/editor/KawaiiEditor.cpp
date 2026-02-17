#include "KawaiiEditor.h"
#include "../controller/KawaiiController.h"
#include "vstgui/vstgui.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cframe.h"

namespace Steinberg {
namespace Vst {
namespace Kawaii {

using namespace VSTGUI;

// Layout constants
static constexpr int kWindowW = 420;
static constexpr int kWindowH = 980;
static constexpr int kKnobSize = 40;
static constexpr int kLabelH = 14;
static constexpr int kRowH = kKnobSize + kLabelH + 4;  // knob + label + padding
static constexpr int kColW = 65;
static constexpr int kGridLeft = 50;   // space for row labels
static constexpr int kGridTop = 100;   // space for title + master + column headers
static constexpr int kMasterY = 40;

KawaiiEditor::KawaiiEditor(void* controller)
    : VSTGUIEditor(static_cast<EditController*>(controller))
{
    ViewRect viewSize(0, 0, kWindowW, kWindowH);
    rect = viewSize;
}

KawaiiEditor::~KawaiiEditor() {}

bool PLUGIN_API KawaiiEditor::open(void* parent, const PlatformType& platformType)
{
    CRect frameSize(0, 0, kWindowW, kWindowH);
    frame = new CFrame(frameSize, this);
    frame->setBackgroundColor(CColor(35, 35, 40, 255));

    createControls();

    if (frame && frame->open(parent, platformType))
        return true;

    return false;
}

void PLUGIN_API KawaiiEditor::close()
{
    if (frame)
    {
        frame->forget();
        frame = nullptr;
    }
}

void KawaiiEditor::valueChanged(CControl* control)
{
    ParamID tag = control->getTag();
    ParamValue value = control->getValue();

    if (getController())
    {
        getController()->setParamNormalized(tag, value);
        getController()->performEdit(tag, value);
    }
}

void KawaiiEditor::createControls()
{
    if (!frame) return;

    CColor labelColor(200, 200, 210, 255);
    CColor titleColor(255, 180, 220, 255);  // kawaii pink
    CColor headerColor(160, 160, 180, 255);

    // --- Title ---
    CRect titleRect(10, 8, kWindowW - 10, 30);
    auto* title = new CTextLabel(titleRect, "KAWAII K50V");
    title->setFontColor(titleColor);
    title->setBackColor(CColor(0, 0, 0, 0));
    title->setFrameColor(CColor(0, 0, 0, 0));
    title->setFont(kNormalFontBig);
    title->setHoriAlign(CHoriTxtAlign::kLeftText);
    frame->addView(title);

    // --- Helper: create a knob with label below ---
    auto makeKnob = [&](const char* name, ParamID tag, int x, int y) {
        // Knob
        CRect knobRect(x, y, x + kKnobSize, y + kKnobSize);
        auto* knob = new CKnob(knobRect, this, tag, nullptr, nullptr);

        // Initialize from controller
        if (getController())
            knob->setValue(static_cast<float>(getController()->getParamNormalized(tag)));

        frame->addView(knob);

        // Label below
        CRect labelRect(x - 8, y + kKnobSize + 1, x + kKnobSize + 8, y + kKnobSize + 1 + kLabelH);
        auto* label = new CTextLabel(labelRect, name);
        label->setFontColor(labelColor);
        label->setBackColor(CColor(0, 0, 0, 0));
        label->setFrameColor(CColor(0, 0, 0, 0));
        label->setFont(kNormalFontVerySmall);
        label->setHoriAlign(CHoriTxtAlign::kCenterText);
        frame->addView(label);
    };

    // --- Master knobs ---
    makeKnob("Volume", kParamMasterVolume, 30, kMasterY);
    makeKnob("Tune", kParamMasterTune, 110, kMasterY);

    // --- Column headers ---
    const char* colHeaders[] = {"Level", "Attack", "Decay", "Sustain", "Release"};
    for (int c = 0; c < 5; c++)
    {
        int x = kGridLeft + c * kColW;
        CRect hdrRect(x - 5, kGridTop - 18, x + kKnobSize + 5, kGridTop - 2);
        auto* hdr = new CTextLabel(hdrRect, colHeaders[c]);
        hdr->setFontColor(headerColor);
        hdr->setBackColor(CColor(0, 0, 0, 0));
        hdr->setFrameColor(CColor(0, 0, 0, 0));
        hdr->setFont(kNormalFontSmall);
        hdr->setHoriAlign(CHoriTxtAlign::kCenterText);
        frame->addView(hdr);
    }

    // --- Per-partial knob grid (16 rows x 5 columns) ---
    for (int p = 0; p < kMaxPartials; p++)
    {
        int y = kGridTop + p * kRowH;

        // Row label (P1, P2, ... P16)
        char rowLabel[8];
        snprintf(rowLabel, sizeof(rowLabel), "P%d", p + 1);
        CRect rowRect(2, y + 10, kGridLeft - 4, y + 10 + kLabelH);
        auto* rl = new CTextLabel(rowRect, rowLabel);
        rl->setFontColor(headerColor);
        rl->setBackColor(CColor(0, 0, 0, 0));
        rl->setFrameColor(CColor(0, 0, 0, 0));
        rl->setFont(kNormalFontSmall);
        rl->setHoriAlign(CHoriTxtAlign::kRightText);
        frame->addView(rl);

        // 5 knobs per partial
        static const int offsets[] = {
            kPartialOffLevel, kPartialOffAttack, kPartialOffDecay,
            kPartialOffSustain, kPartialOffRelease
        };

        for (int c = 0; c < 5; c++)
        {
            int x = kGridLeft + c * kColW;
            ParamID tag = partialParam(p, offsets[c]);
            makeKnob("", tag, x, y);  // no per-knob label, column header suffices
        }
    }
}

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
