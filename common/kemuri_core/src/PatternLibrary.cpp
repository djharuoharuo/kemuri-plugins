#include "kemuri_core/PatternLibrary.h"

// パターンデータは docs/reference/kemuri_generator.js の
// PREMIER_PATTERNS / DILLA_PATTERNS / NINTH_PATTERNS / PETEROCK_PATTERNS を
// そのまま移植したもの。pos/dur/pitch トークンは 1:1 対応。
namespace kemuri::core
{

namespace
{
    // 数値トークン（コード根からの半音オフセット）
    PatternNote P (double pos, double dur, int n)
    {
        return { pos, dur, PitchToken { Tok::Number, n } };
    }
    // 名前付きトークン（"5th" / "octave" / "approach-1" …）
    PatternNote Q (double pos, double dur, const char* s)
    {
        return { pos, dur, pitchTokenFromString (s) };
    }
} // namespace

const std::vector<Pattern>& premierPatterns()
{
    static const std::vector<Pattern> lib = {
        { "PRM_mass_appeal", 0, 0.0, { P(0,0.75,0), P(2.5,0.4,0), P(3,0.75,0) } },
        { "PRM_full_clip",   0, 0.0, { P(0,0.4,0), P(1,0.4,0), P(1.5,0.4,0), P(2.5,0.4,0), P(3,0.4,0) } },
        { "PRM_nas_pedal",   0, 0.0, { P(0,1.5,0), Q(2,0.4,"octave"), P(2.5,1.25,0) } },
        { "PRM_steez_r5",    0, 0.0, { P(0,0.5,0), Q(1.5,0.5,"5th"), P(2.5,0.5,0), P(3,0.9,0) } },
        { "PRM_b7_stab",     0, 0.0, { P(0,0.5,0), P(1.5,0.4,0), Q(2.5,0.4,"b7"), P(3,0.75,0) } },
        { "PRM_basic_pump",  0, 0.0, { P(0,0.5,0), P(1.5,0.5,0), P(3,0.75,0) } },
        { "PRM_octave_call", 0, 0.0, { P(0,0.5,0), Q(2,0.5,"octave"), P(2.5,0.5,0) } },
        { "PRM_chromatic_walk", 0, 0.0, { P(0,1.5,0), P(2,0.5,0), Q(2.5,0.5,"p2"), Q(3,0.5,"p3"), Q(3.5,0.5,"5th") } },
        { "PRM_pedal_sparse", 0, 0.0, { P(0,1.0,0), P(2.5,0.5,0), P(3,0.5,0) } },
        { "PRM_anticipate_3", 0, 0.0, { P(0,0.5,0), P(1,0.5,0), P(2.75,0.25,0), P(3,1.0,0) } },
        { "PRM_5th_color",   0, 0.0, { P(0,0.5,0), Q(1.5,0.5,"5th"), P(2,0.5,0), P(3,1.0,0) } },
        { "PRM_swing_skip",  0, 0.0, { P(0,0.5,0), P(2,0.5,0), P(2.5,0.25,0), P(3.5,0.5,0) } },
    };
    return lib;
}

const std::vector<Pattern>& dillaPatterns()
{
    static const std::vector<Pattern> lib = {
        { "DLA_stutter_pair", 0, 0.05, { P(0,0.25,0), P(0.25,0.25,0), P(1.5,0.25,0), P(1.75,0.25,0), P(2.5,0.5,0), P(3.25,0.5,0) } },
        { "DLA_offbeat_float", 0, 0.06, { P(0.5,0.4,0), P(1.5,0.4,0), Q(2.5,0.4,"b7"), P(3.5,0.4,0) } },
        { "DLA_b7_roll",      0, 0.05, { P(0,0.75,0), Q(1,0.5,"b7"), P(1.75,0.25,0), P(2.5,1.0,0) } },
        { "DLA_late_sub",     0, 0.07, { P(0.1,1.6,0), Q(2.1,0.4,"low5"), P(2.6,1.2,0) } },
        { "DLA_anticipation", 0, 0.04, { P(0,0.5,0), P(1.5,0.5,0), P(3,0.5,0) } },
        { "DLA_octave_drop",  0, 0.04, { Q(0,0.25,"octave"), P(0.25,0.75,0), P(2,0.5,0), P(3.5,0.5,0) } },
        { "DLA_skip_2",       0, 0.05, { P(0,0.75,0), P(1.75,0.25,0), P(2,0.5,0), P(3,0.5,0) } },
        { "DLA_dotted_pulse", 0, 0.06, { P(0,0.75,0), P(0.75,0.75,0), Q(1.5,0.5,"octave"), P(3,1.0,0) } },
        { "DLA_late_drop",    0, 0.05, { P(0.25,0.5,0), P(2,0.5,0), P(2.5,0.5,0), P(3.75,0.25,0) } },
        { "DLA_dub_octave",   0, 0.04, { P(0,0.5,0), Q(0.5,0.5,"octave"), P(2,0.5,0), Q(2.5,0.5,"octave") } },
        { "DLA_sub_lean",     0, 0.06, { P(0,1.25,0), P(1.5,0.25,0), P(2.25,0.5,0), P(3,1.0,0) } },
    };
    return lib;
}

const std::vector<Pattern>& ninthPatterns()
{
    static const std::vector<Pattern> lib = {
        { "9TH_soul_6th",    54, 0.0, { P(0,0.5,0), Q(1,0.5,"6th"), Q(1.5,0.5,"5th"), P(2.5,0.5,0), Q(3.5,0.5,"octave") } },
        { "9TH_gospel_walk", 54, 0.0, { P(0,0.75,0), Q(1.5,0.25,"3rd"), Q(2,0.25,"4th"), Q(2.5,1.0,"5th"), P(3.5,0.5,0) } },
        { "9TH_call_answer", 54, 0.0, { P(0,0.5,0), P(1,0.5,0), Q(2,0.4,"5th"), Q(2.5,0.4,"6th"), Q(3,0.9,"octave") } },
        { "9TH_and3_bounce", 54, 0.0, { P(0,0.75,0), P(1.5,0.4,0), Q(2.5,0.4,"octave"), Q(3,0.4,"5th"), P(3.5,0.4,0) } },
        { "9TH_melodic_5th", 0, 0.0,  { P(0,0.5,0), Q(1.5,0.5,"5th"), P(2,0.5,0), Q(3.5,0.5,"octave") } },
        { "9TH_third_arpeg", 0, 0.0,  { P(0,0.5,0), Q(1,0.5,"3rd"), Q(2,0.5,"5th"), P(3,1.0,0) } },
        { "9TH_walk_up",     0, 0.0,  { P(0,0.5,0), Q(1.5,0.5,"p2"), Q(2,0.5,"3rd"), Q(3,0.5,"5th"), P(3.5,0.5,0) } },
        { "9TH_soul_pump",   0, 0.0,  { P(0,0.5,0), P(1.5,0.5,0), Q(2,0.5,"5th"), P(3,0.5,0), Q(3.5,0.5,"octave") } },
        { "9TH_octave_pulse",0, 0.0,  { P(0,0.5,0), Q(1,0.5,"octave"), P(2,0.5,0), Q(3,0.5,"octave") } },
        { "9TH_neighbor_tone",0, 0.0, { P(0,0.5,0), Q(1,0.25,"p2"), P(1.25,0.75,0), Q(2.5,0.5,"5th"), P(3.5,0.5,0) } },
        { "9TH_root_5_oct",  0, 0.0,  { P(0,0.5,0), Q(0.75,0.5,"5th"), Q(2,0.5,"octave"), Q(2.75,0.5,"5th"), P(3.5,0.5,0) } },
    };
    return lib;
}

const std::vector<Pattern>& peteRockPatterns()
{
    static const std::vector<Pattern> lib = {
        { "PTR_troy_roll",   58, 0.0, { P(0,0.75,0), Q(0.75,0.25,"b7"), Q(1,0.75,"5th"), P(2,0.75,0), Q(3,0.5,"6th"), Q(3.5,0.5,"5th") } },
        { "PTR_world_yours", 57, 0.0, { P(0,1.25,0), Q(1.5,0.5,"b7"), P(2,1.0,0), Q(3.5,0.5,"low5") } },
        { "PTR_horn_answer", 58, 0.0, { P(0,0.4,0), P(1.75,0.25,0), Q(2,0.5,"b7"), Q(2.5,0.5,"5th"), Q(3.5,0.5,"approach-1") } },
        { "PTR_double_kick", 57, 0.0, { P(0,0.25,0), P(0.25,0.4,0), P(1.5,0.5,0), P(2,0.25,0), P(2.25,0.4,0), Q(3,0.75,"5th") } },
        { "PTR_jazzy_walkup",58, 0.0, { P(0,0.5,0), Q(1,0.5,"3rd"), Q(2,0.5,"4th"), Q(3,0.5,"5th"), Q(3.5,0.5,"6th") } },
        { "PTR_low5_lean",   57, 0.0, { P(0,0.75,0), Q(1.5,0.5,"low5"), P(2.5,0.5,0), Q(3,0.9,"b7") } },
        { "PTR_soul_glide",  57, 0.0, { P(0,1.75,0), Q(2,0.5,"5th"), Q(2.5,0.5,"6th"), Q(3,1.0,"5th") } },
        { "PTR_push_and4",   58, 0.0, { P(0,0.5,0), P(1,0.4,0), Q(2,0.5,"b7"), Q(2.75,0.25,"5th"), Q(3.5,0.5,"approach-2") } },
        { "PTR_16th_shuffle",58, 0.0, { P(0,0.25,0), P(0.75,0.25,0), Q(1.5,0.25,"octave"), P(1.75,0.25,0), P(2.5,0.25,0), Q(3.25,0.25,"b7"), P(3.75,0.25,0) } },
        { "PTR_half_call",   57, 0.0, { P(0,0.5,0), P(0.75,0.25,0), P(1.5,0.5,0), Q(2.5,0.5,"b7"), Q(3,0.5,"6th"), Q(3.5,0.5,"5th") } },
    };
    return lib;
}

} // namespace kemuri::core
