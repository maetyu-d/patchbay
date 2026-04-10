#include "NodeFactory.h"
#include "../Modules/ExampleModules.h"

juce::StringArray NodeFactory::getAvailableTypes()
{
    return { "Oscillator", "LFO", "Gain", "Output", "AudioTrack", "MidiTrack" };
}

std::unique_ptr<ModuleNode> NodeFactory::create(const juce::String& type)
{
    if (type == "Oscillator")
        return createOscillatorModule();

    if (type == "LFO")
        return createLfoModule();

    if (type == "Gain")
        return createGainModule();

    if (type == "Output")
        return createOutputModule();

    if (type == "AudioTrack")
        return createAudioTrackModule();

    if (type == "MidiTrack")
        return createMidiTrackModule();

    return {};
}

std::unique_ptr<ModuleNode> NodeFactory::createFromState(const juce::ValueTree& state)
{
    auto node = create(state.getProperty("type").toString());

    if (node != nullptr)
        node->loadState(state.getChildWithName("PARAMETERS"));

    return node;
}
