#pragma once

#include <JuceHeader.h>

class ModulePluginProcessor;

class ModulePluginEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ModulePluginEditor(ModulePluginProcessor&);
    ~ModulePluginEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ModulePluginProcessor& moduleProcessor;
    juce::Label titleLabel;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> sliderLabels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
};
