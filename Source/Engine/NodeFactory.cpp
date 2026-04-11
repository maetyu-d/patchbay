#include "NodeFactory.h"
#include "ExternalPluginManager.h"
#include "../Modules/ExampleModules.h"
#include "../Modules/ExternalPluginModule.h"

juce::StringArray NodeFactory::getAvailableTypes()
{
    return { "Oscillator", "LFO", "BpmToLfo", "TimeSignature", "Gain", "Add", "Subtract", "Multiply", "Divide", "Sum", "Plugin", "Output", "AudioTrack", "MidiTrack" };
}

std::unique_ptr<ModuleNode> NodeFactory::create(const juce::String& type)
{
    if (type == "Oscillator")
        return createOscillatorModule();

    if (type == "LFO")
        return createLfoModule();

    if (type == "BpmToLfo")
        return createBpmToLfoModule();

    if (type == "TimeSignature")
        return createTimeSignatureModule();

    if (type == "Gain")
        return createGainModule();

    if (type == "Add")
        return createAddModule();

    if (type == "Subtract")
        return createSubtractModule();

    if (type == "Multiply")
        return createMultiplyModule();

    if (type == "Divide")
        return createDivideModule();

    if (type == "Sum")
        return createSumModule();

    if (type == "Plugin")
        return createExternal();

    if (type == "Output")
        return createOutputModule();

    if (type == "AudioTrack")
        return createAudioTrackModule();

    if (type == "MidiTrack")
        return createMidiTrackModule();

    return {};
}

std::unique_ptr<ModuleNode> NodeFactory::createExternal()
{
    return std::make_unique<ExternalPluginModule>();
}

std::unique_ptr<ModuleNode> NodeFactory::createFromState(const juce::ValueTree& state)
{
    std::unique_ptr<ModuleNode> node;

    if (state.getProperty("type").toString() == "ExternalPlugin")
    {
        if (const auto description = ExternalPluginManager::getInstance().getPluginByIdentifier(state.getChildWithName("PARAMETERS").getProperty("identifier").toString()))
            node = createExternal(*description);
        else
            node = createExternal();
    }
    else
    {
        node = create(state.getProperty("type").toString());
    }

    if (node != nullptr)
        node->loadState(state.getChildWithName("PARAMETERS"));

    return node;
}

std::unique_ptr<ModuleNode> NodeFactory::createExternal(const juce::PluginDescription& description)
{
    return std::make_unique<ExternalPluginModule>(description);
}
