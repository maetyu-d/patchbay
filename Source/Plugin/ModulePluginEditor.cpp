#include "ModulePluginEditor.h"
#include "ModulePluginProcessor.h"

ModulePluginEditor::ModulePluginEditor(ModulePluginProcessor& processorToEdit)
    : juce::AudioProcessorEditor(processorToEdit), moduleProcessor(processorToEdit)
{
    titleLabel.setText(moduleProcessor.getName(), juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    for (const auto& parameterId : moduleProcessor.getVisibleParameterIds())
    {
        auto* parameter = moduleProcessor.getState().getParameter(parameterId);

        if (parameter == nullptr)
            continue;

        auto* slider = new juce::Slider();
        slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 20);
        slider->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff4ecdc4));
        slider->setColour(juce::Slider::thumbColourId, juce::Colour(0xffff9f1c));
        addAndMakeVisible(slider);
        sliders.add(slider);

        auto* label = new juce::Label();
        label->setText(parameter->getName(32), juce::dontSendNotification);
        label->setJustificationType(juce::Justification::centred);
        label->setColour(juce::Label::textColourId, juce::Colour(0xffdbe3ec));
        addAndMakeVisible(label);
        sliderLabels.add(label);

        attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            moduleProcessor.getState(),
            parameterId,
            *slider));
    }

    setSize(420, 220);
}

void ModulePluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff10141c));

    juce::ColourGradient wash(juce::Colour(0xff1c2533), 0.0f, 0.0f, juce::Colour(0xff0d1118), 0.0f, static_cast<float>(getHeight()), false);
    g.setGradientFill(wash);
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 18.0f);

    g.setColour(juce::Colour(0xff344154));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 18.0f, 1.5f);
}

void ModulePluginEditor::resized()
{
    auto bounds = getLocalBounds().reduced(18);
    titleLabel.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(8);

    if (sliders.isEmpty())
        return;

    const auto widthPerSlider = bounds.getWidth() / sliders.size();

    for (int index = 0; index < sliders.size(); ++index)
    {
        auto area = bounds.removeFromLeft(widthPerSlider);
        sliderLabels[index]->setBounds(area.removeFromTop(20));
        sliders[index]->setBounds(area.reduced(8));
    }
}
