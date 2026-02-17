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

// Layout constants — 4 groups of 8 partials + filter section
static constexpr int kWindowW = 1280;
static constexpr int kWindowH = 560;

// Reduced knob sizes to fit 4 groups across
static constexpr int kKnobSize = 28;
static constexpr int kLabelH = 11;
static constexpr int kRowH = kKnobSize + kLabelH + 4;   // 43px per row
static constexpr int kColW = 56;
static constexpr int kRowLabelW = 28;

// Vertical positions
static constexpr int kTitleY = 8;
static constexpr int kMasterY = 6;
static constexpr int kGridTop = 72;

// 4 groups of 8 partials across the window
static constexpr int kPartialsPerGroup = 8;
static constexpr int kGroupGap = 12;   // gap between groups
static constexpr int kGroupW = kRowLabelW + 5 * kColW;  // 28 + 280 = 308px

// Group horizontal positions (16 + 308 + 12 + 308 + 12 + 308 + 12 + 308 + 16 = 1300 ≈ 1280)
static constexpr int kMarginLeft = 8;
static constexpr int kGroup1Left = kMarginLeft;
static constexpr int kGroup2Left = kGroup1Left + kGroupW + kGroupGap;
static constexpr int kGroup3Left = kGroup2Left + kGroupW + kGroupGap;
static constexpr int kGroup4Left = kGroup3Left + kGroupW + kGroupGap;

// Filter section — in the header area, right side
static constexpr int kFilterKnobSize = 32;
static constexpr int kFilterColW = 62;
static constexpr int kFilterLabelH = 11;

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
    CColor filterCorona(100, 200, 255, 255); // blue arc for filter section
    CColor filterDot(140, 220, 255, 255);    // blue dot handle for filter

    // --- Helper: create a styled corona knob with label below ---
    auto makeKnob = [&](const char* name, ParamID tag, int x, int y,
                        int size, CColor corona, CColor dot) {
        CRect knobRect(x, y, x + size, y + size);
        auto* knob = new CKnob(knobRect, this, tag, nullptr, nullptr);

        knob->setDrawStyle(
            CKnob::kCoronaOutline |
            CKnob::kCoronaDrawing |
            CKnob::kHandleCircleDrawing |
            CKnob::kCoronaLineCapButt
        );
        knob->setCoronaInset(2);
        knob->setHandleLineWidth(2.5);
        knob->setCoronaColor(corona);
        knob->setColorShadowHandle(knobTrack);
        knob->setColorHandle(dot);

        if (getController())
            knob->setValue(static_cast<float>(getController()->getParamNormalized(tag)));

        frame->addView(knob);

        // Label below
        CRect labelRect(x - 10, y + size + 1, x + size + 10, y + size + 1 + kLabelH);
        auto* label = new CTextLabel(labelRect, name);
        label->setFontColor(labelColor);
        label->setBackColor(CColor(0, 0, 0, 0));
        label->setFrameColor(CColor(0, 0, 0, 0));
        label->setFont(kNormalFontVerySmall);
        label->setHoriAlign(CHoriTxtAlign::kCenterText);
        frame->addView(label);
    };

    // Shorthand for partial knobs (pink, small)
    auto partialKnob = [&](const char* name, ParamID tag, int x, int y) {
        makeKnob(name, tag, x, y, kKnobSize, knobCorona, knobDot);
    };

    // Shorthand for filter knobs (blue, slightly larger)
    auto filterKnob = [&](const char* name, ParamID tag, int x, int y) {
        makeKnob(name, tag, x, y, kFilterKnobSize, filterCorona, filterDot);
    };

    // --- Title ---
    CRect titleRect(14, kTitleY, 200, kTitleY + 24);
    auto* title = new CTextLabel(titleRect, "KAWAII K50V");
    title->setFontColor(titleColor);
    title->setBackColor(CColor(0, 0, 0, 0));
    title->setFrameColor(CColor(0, 0, 0, 0));
    title->setFont(kNormalFontBig);
    title->setHoriAlign(CHoriTxtAlign::kLeftText);
    frame->addView(title);

    // --- Master knobs (upper left, after title) ---
    partialKnob("Volume", kParamMasterVolume, 180, kMasterY);
    partialKnob("Tune", kParamMasterTune, 250, kMasterY);

    // --- Filter section (upper right area) ---
    // Two rows of filter knobs: row 1 = Type, Cutoff, Reso, Depth, Key
    //                           row 2 = Atk, Dec, Sus, Rel
    {
        int filterLeft = 770;
        int filterRow1Y = kMasterY;
        int filterRow2Y = kMasterY + kFilterKnobSize + kFilterLabelH + 6;

        // Filter section label
        CRect fltLabelRect(filterLeft - 4, kTitleY, filterLeft + 80, kTitleY + 16);
        auto* fltLabel = new CTextLabel(fltLabelRect, "FILTER");
        fltLabel->setFontColor(filterCorona);
        fltLabel->setBackColor(CColor(0, 0, 0, 0));
        fltLabel->setFrameColor(CColor(0, 0, 0, 0));
        fltLabel->setFont(kNormalFontSmall);
        fltLabel->setHoriAlign(CHoriTxtAlign::kLeftText);
        frame->addView(fltLabel);

        // Row 1: Type, Cutoff, Reso, Depth, Keytrk
        filterKnob("Type",  kParamFilterType,    filterLeft,                  filterRow1Y);
        filterKnob("Cutoff", kParamFilterCutoff,  filterLeft + kFilterColW,    filterRow1Y);
        filterKnob("Reso",  kParamFilterReso,    filterLeft + kFilterColW * 2, filterRow1Y);
        filterKnob("Depth", kParamFilterEnvDep,  filterLeft + kFilterColW * 3, filterRow1Y);
        filterKnob("Key",   kParamFilterKeytrk,  filterLeft + kFilterColW * 4, filterRow1Y);

        // Row 2: Filter envelope ADSR
        filterKnob("F.Atk", kParamFilterEnvAtk,  filterLeft + kFilterColW * 0.5, filterRow2Y);
        filterKnob("F.Dec", kParamFilterEnvDec,  filterLeft + kFilterColW * 1.5, filterRow2Y);
        filterKnob("F.Sus", kParamFilterEnvSus,  filterLeft + kFilterColW * 2.5, filterRow2Y);
        filterKnob("F.Rel", kParamFilterEnvRel,  filterLeft + kFilterColW * 3.5, filterRow2Y);
    }

    // --- Divider line between header and partials grid ---
    CRect divRect(8, kGridTop - 8, kWindowW - 8, kGridTop - 7);
    auto* divider = new CView(divRect);
    divider->setBackground(nullptr);
    frame->addView(divider);

    // --- Column labels for partial knobs ---
    static const char* colLabels[] = {"Level", "Atk", "Dec", "Sus", "Rel"};
    static const int offsets[] = {
        kPartialOffLevel, kPartialOffAttack, kPartialOffDecay,
        kPartialOffSustain, kPartialOffRelease
    };

    // --- Helper: create one group of 8 partials ---
    auto makeGroup = [&](int groupLeft, int startPartial, const char* groupTitle) {
        // Group title
        CRect grpRect(groupLeft, kGridTop - 12, groupLeft + kGroupW, kGridTop);
        auto* grpLabel = new CTextLabel(grpRect, groupTitle);
        grpLabel->setFontColor(headerColor);
        grpLabel->setBackColor(CColor(0, 0, 0, 0));
        grpLabel->setFrameColor(CColor(0, 0, 0, 0));
        grpLabel->setFont(kNormalFontVerySmall);
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
            CRect rowRect(groupLeft, y + 6, groupLeft + kRowLabelW - 2, y + 6 + kLabelH);
            auto* rl = new CTextLabel(rowRect, rowLabel);
            rl->setFontColor(headerColor);
            rl->setBackColor(CColor(0, 0, 0, 0));
            rl->setFrameColor(CColor(0, 0, 0, 0));
            rl->setFont(kNormalFontVerySmall);
            rl->setHoriAlign(CHoriTxtAlign::kRightText);
            frame->addView(rl);

            // 5 knobs per partial
            for (int c = 0; c < 5; c++)
            {
                int x = knobsLeft + c * kColW;
                ParamID tag = partialParam(p, offsets[c]);
                partialKnob(colLabels[c], tag, x, y);
            }
        }
    };

    // 4 groups of 8 partials
    makeGroup(kGroup1Left, 0,  "Partials 1-8");
    makeGroup(kGroup2Left, 8,  "Partials 9-16");
    makeGroup(kGroup3Left, 16, "Partials 17-24");
    makeGroup(kGroup4Left, 24, "Partials 25-32");
}

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
