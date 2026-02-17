#include "KawaiiEditor.h"
#include "../controller/KawaiiController.h"
#include "vstgui/vstgui.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/cframe.h"

namespace Steinberg {
namespace Vst {
namespace Kawaii {

using namespace VSTGUI;

// Layout constants — 4 groups of 8 partials + filter strip at bottom
static constexpr int kWindowW = 1280;
static constexpr int kWindowH = 520;

// Partial knob sizes
static constexpr int kKnobSize = 28;
static constexpr int kLabelH = 11;
static constexpr int kRowH = kKnobSize + kLabelH + 4;   // 43px per row
static constexpr int kColW = 56;
static constexpr int kRowLabelW = 28;

// Vertical positions
static constexpr int kTitleY = 8;
static constexpr int kMasterY = 6;
static constexpr int kGridTop = 52;   // partials start closer to top

// 4 groups of 8 partials across
static constexpr int kPartialsPerGroup = 8;
static constexpr int kGroupGap = 12;
static constexpr int kGroupW = kRowLabelW + 5 * kColW;  // 308px

static constexpr int kMarginLeft = 8;
static constexpr int kGroup1Left = kMarginLeft;
static constexpr int kGroup2Left = kGroup1Left + kGroupW + kGroupGap;
static constexpr int kGroup3Left = kGroup2Left + kGroupW + kGroupGap;
static constexpr int kGroup4Left = kGroup3Left + kGroupW + kGroupGap;

// Filter section — horizontal strip below the partial grid
static constexpr int kGridBottom = kGridTop + kPartialsPerGroup * kRowH;  // 52 + 344 = 396
static constexpr int kFilterY = kGridBottom + 14;  // 410
static constexpr int kFilterKnobSize = 32;
static constexpr int kFilterColW = 62;

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
    CColor filterCorona(100, 200, 255, 255); // blue arc for filter
    CColor filterDot(140, 220, 255, 255);    // blue dot for filter
    CColor filterBg(38, 38, 48, 255);        // slightly lighter bg for filter strip

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

    auto partialKnob = [&](const char* name, ParamID tag, int x, int y) {
        makeKnob(name, tag, x, y, kKnobSize, knobCorona, knobDot);
    };

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

    // --- Master knobs (top area, after title) ---
    partialKnob("Volume", kParamMasterVolume, 180, kMasterY);
    partialKnob("Tune", kParamMasterTune, 250, kMasterY);

    // ===================================================================
    // PARTIALS GRID — 4 groups of 8
    // ===================================================================

    static const char* colLabels[] = {"Level", "Atk", "Dec", "Sus", "Rel"};
    static const int offsets[] = {
        kPartialOffLevel, kPartialOffAttack, kPartialOffDecay,
        kPartialOffSustain, kPartialOffRelease
    };

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

    makeGroup(kGroup1Left, 0,  "Partials 1-8");
    makeGroup(kGroup2Left, 8,  "Partials 9-16");
    makeGroup(kGroup3Left, 16, "Partials 17-24");
    makeGroup(kGroup4Left, 24, "Partials 25-32");

    // ===================================================================
    // FILTER SECTION — horizontal strip at the bottom
    // ===================================================================

    // Background strip for filter area
    CRect filterStripRect(0, kFilterY - 8, kWindowW, kWindowH);
    auto* filterStrip = new CView(filterStripRect);
    filterStrip->setBackground(nullptr);
    frame->addView(filterStrip);

    // "FILTER" label
    CRect fltTitleRect(16, kFilterY - 2, 100, kFilterY + 14);
    auto* fltTitle = new CTextLabel(fltTitleRect, "FILTER");
    fltTitle->setFontColor(filterCorona);
    fltTitle->setBackColor(CColor(0, 0, 0, 0));
    fltTitle->setFrameColor(CColor(0, 0, 0, 0));
    fltTitle->setFont(kNormalFontSmall);
    fltTitle->setHoriAlign(CHoriTxtAlign::kLeftText);
    frame->addView(fltTitle);

    // Filter type — dropdown selector (COptionMenu) showing LP/HP/BP/Notch
    int typeX = 80;
    CRect menuRect(typeX, kFilterY, typeX + 80, kFilterY + 22);
    auto* typeMenu = new COptionMenu(menuRect, this, kParamFilterType);
    typeMenu->addEntry("Low Pass");
    typeMenu->addEntry("High Pass");
    typeMenu->addEntry("Band Pass");
    typeMenu->addEntry("Notch");
    typeMenu->setFontColor(filterCorona);
    typeMenu->setBackColor(CColor(45, 45, 55, 255));
    typeMenu->setFrameColor(CColor(70, 70, 85, 255));
    typeMenu->setFont(kNormalFontSmall);
    // Initialize from controller
    if (getController())
    {
        float normType = static_cast<float>(getController()->getParamNormalized(kParamFilterType));
        int typeIndex = static_cast<int>(normType * 3 + 0.5f);
        typeMenu->setCurrent(typeIndex);
        typeMenu->setValue(normType);
    }
    frame->addView(typeMenu);

    // "Type" label below menu
    CRect typeLblRect(typeX, kFilterY + 24, typeX + 80, kFilterY + 24 + kLabelH);
    auto* typeLbl = new CTextLabel(typeLblRect, "Type");
    typeLbl->setFontColor(labelColor);
    typeLbl->setBackColor(CColor(0, 0, 0, 0));
    typeLbl->setFrameColor(CColor(0, 0, 0, 0));
    typeLbl->setFont(kNormalFontVerySmall);
    typeLbl->setHoriAlign(CHoriTxtAlign::kCenterText);
    frame->addView(typeLbl);

    // Filter knobs — all in one row after the type selector
    int fKnobStart = 180;
    int fKnobY = kFilterY - 4;

    filterKnob("Cutoff",  kParamFilterCutoff,  fKnobStart,                    fKnobY);
    filterKnob("Reso",    kParamFilterReso,    fKnobStart + kFilterColW,      fKnobY);
    filterKnob("Depth",   kParamFilterEnvDep,  fKnobStart + kFilterColW * 2,  fKnobY);
    filterKnob("Key",     kParamFilterKeytrk,  fKnobStart + kFilterColW * 3,  fKnobY);

    // Gap, then filter envelope ADSR
    int fEnvStart = fKnobStart + kFilterColW * 4 + 20;

    // "Env" label before the ADSR knobs
    CRect envLblRect(fEnvStart - 4, kFilterY - 2, fEnvStart + 30, kFilterY + 14);
    auto* envLbl = new CTextLabel(envLblRect, "ENV");
    envLbl->setFontColor(CColor(80, 170, 220, 255));
    envLbl->setBackColor(CColor(0, 0, 0, 0));
    envLbl->setFrameColor(CColor(0, 0, 0, 0));
    envLbl->setFont(kNormalFontVerySmall);
    envLbl->setHoriAlign(CHoriTxtAlign::kLeftText);
    frame->addView(envLbl);

    int fEnvKnobStart = fEnvStart + 32;
    filterKnob("Atk",  kParamFilterEnvAtk,  fEnvKnobStart,                   fKnobY);
    filterKnob("Dec",  kParamFilterEnvDec,  fEnvKnobStart + kFilterColW,     fKnobY);
    filterKnob("Sus",  kParamFilterEnvSus,  fEnvKnobStart + kFilterColW * 2, fKnobY);
    filterKnob("Rel",  kParamFilterEnvRel,  fEnvKnobStart + kFilterColW * 3, fKnobY);
}

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
