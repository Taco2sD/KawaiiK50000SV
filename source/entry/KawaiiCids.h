/**
 * KawaiiCids.h — Component IDs and Parameter Definitions
 *
 * "Kawaii K50V" — 32-partial additive synth with per-partial ADSR
 *                 and Cytomic ZDF SVF filter (LP/HP/BP/Notch).
 *
 * Parameter layout (contiguous IDs — no gaps):
 *   0        Master Volume
 *   1        Master Tune
 *   2-6      Partial 1  (Level, Attack, Decay, Sustain, Release)
 *   7-11     Partial 2
 *   ...
 *   157-161  Partial 32
 *   162      Filter Type (LP=0, HP=1, BP=2, Notch=3)
 *   163      Filter Cutoff
 *   164      Filter Resonance
 *   165      Filter Env Attack
 *   166      Filter Env Decay
 *   167      Filter Env Sustain
 *   168      Filter Env Release
 *   169      Filter Env Depth (bipolar: 0.5 = none)
 *   170      Filter Keytrack
 *   kNumParams = 171
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

static constexpr int kMaxPartials = 32;
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

// Filter parameter base — starts right after the last partial
// partialParam(31, 4) = 2 + 31*5 + 4 = 161, so filter starts at 162
static constexpr int kFilterParamBase = kPartialParamBase + kMaxPartials * kPartialParamStride;

// Filter types for the discrete type selector
enum FilterType : int
{
    kFilterLP    = 0,  // Low-pass (12dB/oct)
    kFilterHP    = 1,  // High-pass (12dB/oct)
    kFilterBP    = 2,  // Band-pass
    kFilterNotch = 3,  // Notch (band-reject)
    kNumFilterTypes = 4
};

enum KawaiiParamID : Vst::ParamID
{
    kParamMasterVolume = 0,
    kParamMasterTune   = 1,

    // partialParam(0, 0)=2  ... partialParam(0, 4)=6
    // partialParam(1, 0)=7  ... partialParam(1, 4)=11
    // ...
    // partialParam(31, 0)=157 ... partialParam(31, 4)=161

    // Filter section (9 params starting at 162)
    kParamFilterType    = kFilterParamBase,      // 162
    kParamFilterCutoff  = kFilterParamBase + 1,  // 163
    kParamFilterReso    = kFilterParamBase + 2,  // 164
    kParamFilterEnvAtk  = kFilterParamBase + 3,  // 165
    kParamFilterEnvDec  = kFilterParamBase + 4,  // 166
    kParamFilterEnvSus  = kFilterParamBase + 5,  // 167
    kParamFilterEnvRel  = kFilterParamBase + 6,  // 168
    kParamFilterEnvDep  = kFilterParamBase + 7,  // 169 (bipolar: 0.5 = no mod)
    kParamFilterKeytrk  = kFilterParamBase + 8,  // 170

    kNumParams = kFilterParamBase + 9            // 171
};

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
