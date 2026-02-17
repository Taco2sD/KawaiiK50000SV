/**
 * KawaiiCids.h — Component IDs and Parameter Definitions
 *
 * "Kawaii K50V" — 16-partial additive synth with per-partial ADSR.
 *
 * Parameter layout (contiguous IDs — no gaps):
 *   0      Master Volume
 *   1      Master Tune
 *   2-6    Partial 1  (Level, Attack, Decay, Sustain, Release)
 *   7-11   Partial 2
 *   ...
 *   77-81  Partial 16
 *   kNumParams = 82
 */

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Steinberg {
namespace Vst {
namespace Kawaii {

// Component UIDs — never change these
static const FUID ProcessorUID(0xA1B2C3D4, 0xE5F60718, 0x293A4B5C, 0x6D7E8F90);
static const FUID ControllerUID(0x09F8E7D6, 0xC5B4A392, 0x81706F5E, 0x4D3C2B1A);

static constexpr int kMaxPartials = 16;
static constexpr int kMaxVoices = 6;

// Per-partial parameter addressing — starts right after the 2 globals
static constexpr int kPartialParamBase   = 2;
static constexpr int kPartialParamStride = 5;

// Offsets within each partial's 5-param block
static constexpr int kPartialOffLevel   = 0;
static constexpr int kPartialOffAttack  = 1;
static constexpr int kPartialOffDecay   = 2;
static constexpr int kPartialOffSustain = 3;
static constexpr int kPartialOffRelease = 4;

// Helper: get param ID for partial N, offset O
inline constexpr Vst::ParamID partialParam(int partial, int offset)
{
    return static_cast<Vst::ParamID>(kPartialParamBase + partial * kPartialParamStride + offset);
}

enum KawaiiParamID : Vst::ParamID
{
    kParamMasterVolume = 0,
    kParamMasterTune   = 1,

    // partialParam(0, 0)=2  ... partialParam(0, 4)=6
    // partialParam(1, 0)=7  ... partialParam(1, 4)=11
    // ...
    // partialParam(15, 0)=77 ... partialParam(15, 4)=81

    kNumParams = 82
};

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
