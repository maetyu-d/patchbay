#include "ModulePluginProcessor.h"
#include "ModulePluginEditor.h"

namespace
{
using Parameter = juce::AudioParameterFloat;
} // namespace

ModulePluginProcessor::ModulePluginProcessor()
    : juce::AudioProcessor(createBuses()),
      state(*this, nullptr, "STATE", createParameterLayout())
{
}

void ModulePluginProcessor::prepareToPlay(double sampleRate, int)
{
    oscillatorCore.prepare(sampleRate);
    lfoCore.prepare(sampleRate);
}

void ModulePluginProcessor::releaseResources()
{
}

bool ModulePluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto kind = getPluginKind();

    if (kind == PluginKind::oscillator || kind == PluginKind::lfo)
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainInputChannelSet().isDisabled();

    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ModulePluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    switch (getPluginKind())
    {
        case PluginKind::oscillator:
            oscillatorCore.render(buffer,
                                  *state.getRawParameterValue("frequency"),
                                  *state.getRawParameterValue("level"));
            break;

        case PluginKind::lfo:
            lfoCore.renderAsAudio(buffer,
                                  *state.getRawParameterValue("rate"),
                                  *state.getRawParameterValue("depth"));
            break;

        case PluginKind::gain:
            gainCore.process(buffer, *state.getRawParameterValue("gain"));
            break;

        case PluginKind::output:
            outputCore.process(buffer, *state.getRawParameterValue("trim"));
            break;
    }
}

juce::AudioProcessorEditor* ModulePluginProcessor::createEditor()
{
    return new ModulePluginEditor(*this);
}

bool ModulePluginProcessor::hasEditor() const
{
    return true;
}

const juce::String ModulePluginProcessor::getName() const
{
    return getPluginDisplayName();
}

bool ModulePluginProcessor::acceptsMidi() const
{
    return false;
}

bool ModulePluginProcessor::producesMidi() const
{
    return false;
}

bool ModulePluginProcessor::isMidiEffect() const
{
    return false;
}

double ModulePluginProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int ModulePluginProcessor::getNumPrograms()
{
    return 1;
}

int ModulePluginProcessor::getCurrentProgram()
{
    return 0;
}

void ModulePluginProcessor::setCurrentProgram(int)
{
}

const juce::String ModulePluginProcessor::getProgramName(int)
{
    return {};
}

void ModulePluginProcessor::changeProgramName(int, const juce::String&)
{
}

void ModulePluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void ModulePluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        state.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorValueTreeState& ModulePluginProcessor::getState()
{
    return state;
}

juce::StringArray ModulePluginProcessor::getVisibleParameterIds() const
{
    switch (getPluginKind())
    {
        case PluginKind::oscillator: return { "frequency", "level" };
        case PluginKind::lfo:        return { "rate", "depth" };
        case PluginKind::gain:       return { "gain" };
        case PluginKind::output:     return { "trim" };
    }

    return {};
}

ModulePluginProcessor::PluginKind ModulePluginProcessor::getPluginKind()
{
#if defined(PATCHBAY_PLUGIN_OSCILLATOR)
    return PluginKind::oscillator;
#elif defined(PATCHBAY_PLUGIN_LFO)
    return PluginKind::lfo;
#elif defined(PATCHBAY_PLUGIN_GAIN)
    return PluginKind::gain;
#else
    return PluginKind::output;
#endif
}

juce::String ModulePluginProcessor::getPluginDisplayName()
{
    switch (getPluginKind())
    {
        case PluginKind::oscillator: return "PatchBay Oscillator";
        case PluginKind::lfo:        return "PatchBay LFO";
        case PluginKind::gain:       return "PatchBay Gain";
        case PluginKind::output:     return "PatchBay Output";
    }

    return "PatchBay Module";
}

juce::AudioProcessorValueTreeState::ParameterLayout ModulePluginProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;

    switch (getPluginKind())
    {
        case PluginKind::oscillator:
            parameters.push_back(std::make_unique<Parameter>("frequency", "Frequency", juce::NormalisableRange<float>(40.0f, 2000.0f, 0.01f, 0.3f), 220.0f));
            parameters.push_back(std::make_unique<Parameter>("level", "Level", 0.0f, 1.0f, 0.18f));
            break;

        case PluginKind::lfo:
            parameters.push_back(std::make_unique<Parameter>("rate", "Rate", juce::NormalisableRange<float>(0.05f, 20.0f, 0.001f, 0.3f), 0.85f));
            parameters.push_back(std::make_unique<Parameter>("depth", "Depth", 0.0f, 1.0f, 0.45f));
            break;

        case PluginKind::gain:
            parameters.push_back(std::make_unique<Parameter>("gain", "Gain", 0.0f, 2.0f, 0.65f));
            break;

        case PluginKind::output:
            parameters.push_back(std::make_unique<Parameter>("trim", "Trim", 0.0f, 1.0f, 0.85f));
            break;
    }

    return { parameters.begin(), parameters.end() };
}

ModulePluginProcessor::BusesProperties ModulePluginProcessor::createBuses()
{
    const auto kind = getPluginKind();

    if (kind == PluginKind::oscillator || kind == PluginKind::lfo)
        return BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true);

    return BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ModulePluginProcessor();
}
