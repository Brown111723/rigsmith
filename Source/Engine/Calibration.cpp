// Level calibration tables. Regenerate with:  RigsmithTests <models> --calibrate
// (a strummed guitar at -12 dBFS peak through every model at its stock settings: drives and fuzzes
// are trimmed to +4 dB RMS against bypass, the rotary speaker to level)
#include "Nodes.h"
#include <map>

namespace rig
{
static const float overdriveTrims[][3] {
    { -10.3f, -9.4f, 0.0f, },   // Tube Screamer (TS808 / TS9)
    { -8.6f, 0.0f, 0.0f, },   // Klon Centaur
    { -9.8f, 0.0f, 0.0f, },   // Super OverDrive SD-1
    { 0.0f, 0.0f, 0.0f, },   // OCD (Obsessive Compulsive Drive)
    { -3.1f, -2.5f, -2.1f, },   // Timmy
    { -7.9f, 0.0f, 0.0f, },   // Soul Food
    { -4.5f, -5.0f, 0.0f, },   // Morning Glory
    { -9.3f, 0.0f, 0.0f, },   // OD-1 OverDrive
    { -4.5f, -4.8f, -1.6f, },   // King of Tone
    { -2.7f, 0.0f, 0.0f, },   // Lightspeed
};

static const float distortionTrims[][3] {
    { -2.0f, 0.0f, 0.0f, },   // DS-1 Distortion
    { -10.6f, -10.1f, -11.2f, },   // RAT 2
    { -10.3f, -9.4f, 0.0f, },   // Tube Screamer (TS808 / TS9)
    { -3.1f, -1.7f, -4.7f, },   // Big Muff Pi
    { -5.1f, 0.0f, 0.0f, },   // MT-2 Metal Zone
    { -2.2f, -4.1f, 0.0f, },   // HM-2 Heavy Metal
    { -2.6f, 0.0f, 0.0f, },   // Shredmaster
    { 0.0f, 0.0f, 0.0f, },   // OCD (Obsessive Compulsive Drive)
    { -1.4f, 0.0f, 0.0f, },   // Distortion+
    { -8.0f, 0.0f, 0.0f, },   // BE-OD
};

static const float bassTrims[][3] {
    { -6.1f, 0.0f, 0.0f, },   // SansAmp Bass Driver DI
    { -5.6f, -6.1f, -4.8f, },   // Microtubes B3K
    { -7.0f, -7.6f, -6.2f, },   // Microtubes B7K Ultra
    { -7.6f, -7.0f, 0.0f, },   // Alpha-Omega
    { -7.1f, -4.3f, -9.0f, },   // SVT preamp
    { -4.6f, -2.1f, 0.0f, },   // M80 Bass D.I.+
    { -4.9f, -4.0f, -4.4f, },   // Bass Big Muff Pi
    { -5.4f, 0.0f, 0.0f, },   // ODB-3 Bass OverDrive
    { -5.2f, -7.0f, 0.0f, },   // Tone Hammer
    { -7.3f, -10.6f, 0.0f, },   // Brassmaster
};

float pedalTrimDb (NodeType t, int model, int voice)
{
    const auto* table = t == NodeType::Overdrive ? overdriveTrims : t == NodeType::BassDrive ? bassTrims : distortionTrims;
    const int count = (int) (t == NodeType::Overdrive ? std::size (overdriveTrims) : t == NodeType::BassDrive ? std::size (bassTrims) : std::size (distortionTrims));
    if (model < 0 || model >= count || voice < 0 || voice > 2) return 0.0f;
    return table[model][voice];
}

float fxTrimDb (NodeType t, int model)
{
    static const std::map<NodeType, std::vector<float>> table {
        { nodeTypeFromString ("fuzz"), { -10.9f, -11.4f, -13.1f, -13.7f, -14.0f, -2.2f, -5.1f, -13.6f, -11.7f, -13.0f, } },
        { nodeTypeFromString ("phaser"), { 2.1f, 2.4f, 2.0f, 2.3f, 2.5f, 2.3f, 2.2f, 2.4f, } },
        { nodeTypeFromString ("vibe"), { 3.3f, 2.8f, 3.1f, 3.1f, 3.0f, 3.6f, } },
        { nodeTypeFromString ("rotary"), { 6.1f, 6.1f, 6.4f, 6.3f, 6.0f, 5.5f, } },
    };
    if (const auto it = table.find (t); it != table.end() && model >= 0 && model < (int) it->second.size())
        return it->second[(size_t) model];
    return 0.0f;
}
} // namespace rig
