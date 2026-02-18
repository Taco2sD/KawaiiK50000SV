/**
 * KawaiiFilterTypes.h — Filter Type Definitions for Surge XT sst-filters
 *
 * Maps our flat UI filter index (0–kNumFilterTypes) to sst-filters++
 * (FilterModel, ModelConfig) tuples. Each entry is a musically useful
 * filter configuration curated from Surge XT's ~100+ total combinations.
 *
 * The "subtype" parameter (0–3) cycles through DriveMode or Slope variants
 * for the selected filter model, allowing tonal variation within each type.
 */

#pragma once

#include <sst/filters++.h>
#include <array>

namespace Steinberg {
namespace Vst {
namespace Kawaii {

namespace sfpp = sst::filtersplusplus;

// Each entry in the filter type table
struct FilterTypeEntry
{
    const char*     name;       // UI display name
    sfpp::FilterModel model;
    sfpp::Passband    passband;
    sfpp::Slope       slope;
    sfpp::DriveMode   drive;
    sfpp::FilterSubModel submodel;
    bool needsDelayLine;        // true for Comb filters
};

// All available filter types — curated from Surge XT's library.
// Index 0 is the default (SVF LP — clean, neutral, familiar).
static constexpr int kNumFilterTypes = 33;

inline const std::array<FilterTypeEntry, kNumFilterTypes>& getFilterTypes()
{
    using FM = sfpp::FilterModel;
    using PB = sfpp::Passband;
    using SL = sfpp::Slope;
    using DM = sfpp::DriveMode;
    using SM = sfpp::FilterSubModel;
    static const std::array<FilterTypeEntry, kNumFilterTypes> types = {{
        // --- Cytomic SVF (clean, precise, 9 passbands) ---
        { "SVF LP",       FM::CytomicSVF,    PB::LP,       SL::UNSUPPORTED, DM::UNSUPPORTED, SM::UNSUPPORTED, false },
        { "SVF HP",       FM::CytomicSVF,    PB::HP,       SL::UNSUPPORTED, DM::UNSUPPORTED, SM::UNSUPPORTED, false },
        { "SVF BP",       FM::CytomicSVF,    PB::BP,       SL::UNSUPPORTED, DM::UNSUPPORTED, SM::UNSUPPORTED, false },
        { "SVF Notch",    FM::CytomicSVF,    PB::Notch,    SL::UNSUPPORTED, DM::UNSUPPORTED, SM::UNSUPPORTED, false },
        { "SVF Peak",     FM::CytomicSVF,    PB::Peak,     SL::UNSUPPORTED, DM::UNSUPPORTED, SM::UNSUPPORTED, false },
        { "SVF Allpass",  FM::CytomicSVF,    PB::Allpass,  SL::UNSUPPORTED, DM::UNSUPPORTED, SM::UNSUPPORTED, false },
        { "SVF Bell",     FM::CytomicSVF,    PB::Bell,     SL::UNSUPPORTED, DM::UNSUPPORTED, SM::UNSUPPORTED, false },
        { "SVF LoShelf",  FM::CytomicSVF,    PB::LowShelf, SL::UNSUPPORTED, DM::UNSUPPORTED, SM::UNSUPPORTED, false },
        { "SVF HiShelf",  FM::CytomicSVF,    PB::HighShelf,SL::UNSUPPORTED, DM::UNSUPPORTED, SM::UNSUPPORTED, false },

        // --- Classic (Vember) — Surge's original 12/24dB filters ---
        { "Classic LP12", FM::VemberClassic,  PB::LP,       SL::Slope_12dB,  DM::Clean,       SM::UNSUPPORTED, false },
        { "Classic LP24", FM::VemberClassic,  PB::LP,       SL::Slope_24dB,  DM::Clean,       SM::UNSUPPORTED, false },
        { "Classic HP12", FM::VemberClassic,  PB::HP,       SL::Slope_12dB,  DM::Clean,       SM::UNSUPPORTED, false },
        { "Classic HP24", FM::VemberClassic,  PB::HP,       SL::Slope_24dB,  DM::Clean,       SM::UNSUPPORTED, false },
        { "Classic BP12", FM::VemberClassic,  PB::BP,       SL::Slope_12dB,  DM::Clean,       SM::UNSUPPORTED, false },
        { "Classic BP24", FM::VemberClassic,  PB::BP,       SL::Slope_24dB,  DM::Clean,       SM::UNSUPPORTED, false },
        { "Classic N12",  FM::VemberClassic,  PB::Notch,    SL::Slope_12dB,  DM::Clean,       SM::UNSUPPORTED, false },
        { "Classic N24",  FM::VemberClassic,  PB::Notch,    SL::Slope_24dB,  DM::Clean,       SM::UNSUPPORTED, false },

        // --- Ladder (Moog-style) — fat, warm, musical resonance ---
        { "Ladder LP",    FM::VemberLadder,   PB::LP,       SL::Slope_24dB,  DM::UNSUPPORTED, SM::UNSUPPORTED, false },

        // --- K35 (Korg MS-20) — aggressive, raw, screaming at high reso ---
        { "K35 LP",       FM::K35,            PB::LP,       SL::UNSUPPORTED, DM::K35_None,    SM::UNSUPPORTED, false },
        { "K35 HP",       FM::K35,            PB::HP,       SL::UNSUPPORTED, DM::K35_None,    SM::UNSUPPORTED, false },

        // --- Diode Ladder — Roland-style, asymmetric distortion ---
        { "Diode LP",     FM::DiodeLadder,    PB::LP,       SL::UNSUPPORTED, DM::UNSUPPORTED, SM::UNSUPPORTED, false },

        // --- OB-Xd (Oberheim) — smooth, creamy, classic poly-synth ---
        { "OBXd LP12",   FM::OBXD_2Pole,     PB::LP,       SL::UNSUPPORTED, DM::UNSUPPORTED, SM::UNSUPPORTED, false },
        { "OBXd LP24",   FM::OBXD_4Pole,     PB::LP,       SL::Slope_24dB,  DM::UNSUPPORTED, SM::UNSUPPORTED, false },
        { "OBXd HP",     FM::OBXD_2Pole,     PB::HP,       SL::UNSUPPORTED, DM::UNSUPPORTED, SM::UNSUPPORTED, false },
        { "OBXd BP",     FM::OBXD_2Pole,     PB::BP,       SL::UNSUPPORTED, DM::UNSUPPORTED, SM::UNSUPPORTED, false },
        { "OBXd Notch",  FM::OBXD_2Pole,     PB::Notch,    SL::UNSUPPORTED, DM::UNSUPPORTED, SM::UNSUPPORTED, false },

        // --- Vintage Ladder — another take on Moog, Huovilainen model ---
        { "Vintage LP",   FM::VintageLadder,  PB::LP,       SL::UNSUPPORTED, DM::UNSUPPORTED, SM::RungeKutta,  false },

        // --- CutoffWarp / ResonanceWarp — nonlinear, character filters ---
        { "CWarp LP",     FM::CutoffWarp,     PB::LP,       SL::UNSUPPORTED, DM::Tanh,        SM::Warp_1Stage, false },
        { "RWarp LP",     FM::ResonanceWarp,   PB::LP,       SL::UNSUPPORTED, DM::Tanh,        SM::Warp_1Stage, false },

        // --- TriPole — 3-pole filter with morphing topology ---
        { "TriPole",      FM::TriPole,        PB::LowLowLow,SL::UNSUPPORTED, DM::UNSUPPORTED, SM::UNSUPPORTED, false },

        // --- Comb — pitched resonance, great for Karplus-Strong-style sounds ---
        // Note: Comb model configs use Slope as the only key (no Passband), so
        // we must set Passband to UNSUPPORTED to avoid filtering out all configs.
        { "Comb+",        FM::Comb,           PB::UNSUPPORTED, SL::Comb_Positive_100, DM::UNSUPPORTED, SM::UNSUPPORTED, true },
        { "Comb-",        FM::Comb,           PB::UNSUPPORTED, SL::Comb_Negative_100, DM::UNSUPPORTED, SM::UNSUPPORTED, true },

        // --- Sample & Hold — lo-fi / glitchy ---
        { "S&H",          FM::SampleAndHold,  PB::UNSUPPORTED, SL::UNSUPPORTED, DM::UNSUPPORTED, SM::UNSUPPORTED, false },
    }};
    return types;
}

} // namespace Kawaii
} // namespace Vst
} // namespace Steinberg
