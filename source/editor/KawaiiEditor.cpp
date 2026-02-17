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

// Layout constants — 16:9ish, two groups of 8 partials side by side
static constexpr int kWindowW = 880;
static constexpr int kWindowH = 520;

static constexpr int kKnobSize = 38;
static constexpr int kLabelH = 13;
static constexpr int kRowH = kKnobSize + kLabelH + 5;   // 56px per row
static constexpr int kColW = 76;
static constexpr int kRowLabelW = 34;

// Vertical positions
static constexpr int kTitleY = 8;
static constexpr int kMasterY = 6;
static constexpr int kGridTop = 72;

// Horizontal positions for two groups
static constexpr int kGroup1Left = 16;
static constexpr int kGroup2Left = 464;
static constexpr int kPartialsPerGroup = 8;

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
    frame->setBackgroundColor(CColor(30, 30, 36, 255));

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

    CColor labelColor(190, 190, 200, 255);
    CColor titleColor(255, 160, 210, 255);   // kawaii pink
    CColor headerColor(140, 140, 160, 255);
    CColor knobCorona(255, 140, 200, 255);   // pink arc
    CColor knobTrack(55, 55, 65, 255);       // dark arc track
    CColor knobDot(255, 180, 220, 255);      // pink dot handle
    CColor dividerColor(50, 50, 60, 255);

    // --- Title ---
    CRect titleRect(14, kTitleY, 200, kTitleY + 24);
    auto* title = new CTextLabel(titleRect, "KAWAII K50V");
    title->setFontColor(titleColor);
    title->setBackColor(CColor(0, 0, 0, 0));
    title->setFrameColor(CColor(0, 0, 0, 0));
    title->setFont(kNormalFontBig);
    title->setHoriAlign(CHoriTxtAlign::kLeftText);
    frame->addView(title);

    // --- Helper: create a styled corona knob with label below ---
    auto makeKnob = [&](const char* name, ParamID tag, int x, int y) {
        CRect knobRect(x, y, x + kKnobSize, y + kKnobSize);
        auto* knob = new CKnob(knobRect, this, tag, nullptr, nullptr);

        // Corona arc style
        knob->setDrawStyle(
            CKnob::kCoronaOutline |
            CKnob::kCoronaDrawing |
            CKnob::kHandleCircleDrawing |
            CKnob::kCoronaLineCapButt
        );
        knob->setCoronaInset(3);
        knob->setHandleLineWidth(3.0);
        knob->setCoronaColor(knobCorona);
        knob->setColorShadowHandle(knobTrack);
        knob->setColorHandle(knobDot);

        // Initialize from controller
        if (getController())
            knob->setValue(static_cast<float>(getController()->getParamNormalized(tag)));

        frame->addView(knob);

        // Label below
        CRect labelRect(x - 10, y + kKnobSize + 1, x + kKnobSize + 10, y + kKnobSize + 1 + kLabelH);
        auto* label = new CTextLabel(labelRect, name);
        label->setFontColor(labelColor);
        label->setBackColor(CColor(0, 0, 0, 0));
        label->setFrameColor(CColor(0, 0, 0, 0));
        label->setFont(kNormalFontVerySmall);
        label->setHoriAlign(CHoriTxtAlign::kCenterText);
        frame->addView(label);
    };

    // --- Master knobs (top right area) ---
    makeKnob("Volume", kParamMasterVolume, 720, kMasterY);
    makeKnob("Tune", kParamMasterTune, 810, kMasterY);

    // --- Divider line between master and grid ---
    CRect divRect(10, kGridTop - 14, kWindowW - 10, kGridTop - 13);
    auto* divider = new CView(divRect);
    divider->setBackground(nullptr);
    frame->addView(divider);

    // --- Column labels for each knob ---
    static const char* colLabels[] = {"Level", "Atk", "Dec", "Sus", "Rel"};
    static const int offsets[] = {
        kPartialOffLevel, kPartialOffAttack, kPartialOffDecay,
        kPartialOffSustain, kPartialOffRelease
    };

    // --- Helper: create one group of 8 partials ---
    auto makeGroup = [&](int groupLeft, int startPartial, const char* groupTitle) {
        // Group title
        CRect grpRect(groupLeft, kGridTop - 12, groupLeft + kRowLabelW + 5 * kColW, kGridTop);
        auto* grpLabel = new CTextLabel(grpRect, groupTitle);
        grpLabel->setFontColor(headerColor);
        grpLabel->setBackColor(CColor(0, 0, 0, 0));
        grpLabel->setFrameColor(CColor(0, 0, 0, 0));
        grpLabel->setFont(kNormalFontSmall);
        grpLabel->setHoriAlign(CHoriTxtAlign::kLeftText);
        frame->addView(grpLabel);

        int knobsLeft = groupLeft + kRowLabelW;

        for (int i = 0; i < kPartialsPerGroup; i++)
        {
            int p = startPartial + i;
            int y = kGridTop + i * kRowH;

            // Row label (P1, P2, etc.)
            char rowLabel[8];
            snprintf(rowLabel, sizeof(rowLabel), "P%d", p + 1);
            CRect rowRect(groupLeft, y + 10, groupLeft + kRowLabelW - 2, y + 10 + kLabelH);
            auto* rl = new CTextLabel(rowRect, rowLabel);
            rl->setFontColor(headerColor);
            rl->setBackColor(CColor(0, 0, 0, 0));
            rl->setFrameColor(CColor(0, 0, 0, 0));
            rl->setFont(kNormalFontSmall);
            rl->setHoriAlign(CHoriTxtAlign::kRightText);
            frame->addView(rl);

            // 5 knobs per partial, each with its own label
            for (int c = 0; c < 5; c++)
            {
                int x = knobsLeft + c * kColW;
                ParamID tag = partialParam(p, offsets[c]);
                makeKnob(colLabels[c], tag, x, y);
            }
        }
    };

    makeGroup(kGroup1Left, 0, "Partials 1-8");
    makeGroup(kGroup2Left, 8, "Partials 9-16");
}

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
