#include "NodeFactory.h"
#include "ExternalPluginManager.h"
#include "../Modules/ExampleModules.h"
#include "../Modules/ExternalPluginModule.h"

std::vector<NodeMenuSection> NodeFactory::getMenuSections()
{
    return {
        { "Tracks", { "AudioTrack", "MidiTrack" } },
        { "Sound", { "Oscillator", "Plugin", "Filter", "Gain", "Output" } },
        { "Control", { "LFO", "Metronome", "Comparator", "BpmToLfo", "TimeSignature", "AD", "ADSR" } },
        { "Mix", { "Sum", "ChannelStrip", "Send", "Return", "Meter", "Bus", "Router" } },
        { "Math", { "Add", "Subtract", "Multiply", "Divide" } }
    };
}

juce::StringArray NodeFactory::getAvailableTypes()
{
    juce::StringArray all;

    for (const auto& section : getMenuSections())
        all.addArray(section.types);

    return all;
}

juce::String NodeFactory::getDisplayNameForType(const juce::String& type)
{
    if (type == "AudioTrack") return "Audio Track";
    if (type == "MidiTrack") return "MIDI Track";
    if (type == "BpmToLfo") return "BPM to LFO";
    if (type == "TimeSignature") return "Time Signature";
    if (type == "ChannelStrip") return "Channel Strip";
    if (type == "AD") return "AD Envelope";
    if (type == "ADSR") return "ADSR Envelope";
    return type;
}

std::unique_ptr<ModuleNode> NodeFactory::create(const juce::String& type)
{
    if (type == "Oscillator")
        return createOscillatorModule();

    if (type == "LFO")
        return createLfoModule();

    if (type == "Metronome")
        return createMetronomeModule();

    if (type == "Comparator")
        return createComparatorModule();

    if (type == "BpmToLfo")
        return createBpmToLfoModule();

    if (type == "TimeSignature")
        return createTimeSignatureModule();

    if (type == "AD")
        return createAdEnvelopeModule();

    if (type == "ADSR")
        return createAdsrEnvelopeModule();

    if (type == "Filter")
        return createFilterModule();

    if (type == "ChannelStrip")
        return createChannelStripModule();

    if (type == "Send")
        return createSendModule();

    if (type == "Return")
        return createReturnModule();

    if (type == "Meter")
        return createMeterModule();

    if (type == "Bus")
        return createBusModule();

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

    if (type == "Router")
        return createRouterModule();

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
