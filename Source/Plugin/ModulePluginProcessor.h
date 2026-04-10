#pragma once

#include <JuceHeader.h>
#include "../Modules/ModuleCores.h"

class ModulePluginProcessor final : public juce::AudioProcessor
{
public:
    ModulePluginProcessor();
    ~ModulePluginProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState();
    juce::StringArray getVisibleParameterIds() const;

private:
    enum class PluginKind
    {
        oscillator,
        lfo,
        gain,
        output
    };

    static PluginKind getPluginKind();
    static juce::String getPluginDisplayName();
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static BusesProperties createBuses();

    juce::AudioProcessorValueTreeState state;
    OscillatorCore oscillatorCore;
    LfoCore lfoCore;
    GainCore gainCore;
    OutputCore outputCore;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
