#pragma once

#include "Nodes.h"

namespace rig
{
/** A ready-made setting for a built-in block: the parameters it changes (by id); the rest stay at
    their defaults. Classic, well-known starting points (a slapback, a Tube Screamer-style push,
    a Leslie on slow...). */
struct FactoryPreset
{
    juce::String name, blurb;
    std::vector<std::pair<juce::String, float>> values;
};

const std::vector<FactoryPreset>& factoryPresets (NodeType);
class Block;
/** Sets a block to a preset: its defaults (what's loaded and the NAM trims stay), the preset's model with its stock settings, then its values. */
void applyFactoryPreset (Block&, const FactoryPreset&);

} // namespace rig
