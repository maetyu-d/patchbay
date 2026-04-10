#include "ModuleCores.h"

void OscillatorCore::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
}

void OscillatorCore::render(juce::AudioBuffer<float>& output, float frequency, float level)
{
    output.clear();

    const auto safeFrequency = juce::jlimit(20.0f, 18000.0f, frequency);
    const auto safeLevel = juce::jlimit(0.0f, 1.0f, level);
    const auto phaseDelta = juce::MathConstants<float>::twoPi * safeFrequency / static_cast<float>(sampleRate);

    for (int sample = 0; sample < output.getNumSamples(); ++sample)
    {
        const auto value = std::sin(phase) * safeLevel;

        for (int channel = 0; channel < output.getNumChannels(); ++channel)
            output.setSample(channel, sample, value);

        phase += phaseDelta;

        if (phase >= juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;
    }
}

void LfoCore::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
}

float LfoCore::advanceBlock(int numSamples, float rateHz, float depth)
{
    auto accumulated = 0.0f;
    const auto phaseDelta = juce::MathConstants<float>::twoPi * juce::jlimit(0.01f, 100.0f, rateHz) / static_cast<float>(sampleRate);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        accumulated += std::sin(phase);
        phase += phaseDelta;

        if (phase >= juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;
    }

    return juce::jlimit(0.0f, 1.0f, depth) * (accumulated / static_cast<float>(numSamples));
}

void LfoCore::renderAsAudio(juce::AudioBuffer<float>& output, float rateHz, float depth)
{
    output.clear();

    const auto phaseDelta = juce::MathConstants<float>::twoPi * juce::jlimit(0.01f, 100.0f, rateHz) / static_cast<float>(sampleRate);
    const auto safeDepth = juce::jlimit(0.0f, 1.0f, depth);

    for (int sample = 0; sample < output.getNumSamples(); ++sample)
    {
        const auto value = std::sin(phase) * safeDepth;

        for (int channel = 0; channel < output.getNumChannels(); ++channel)
            output.setSample(channel, sample, value);

        phase += phaseDelta;

        if (phase >= juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;
    }
}

void GainCore::process(juce::AudioBuffer<float>& buffer, float gain) const
{
    buffer.applyGain(juce::jlimit(0.0f, 2.0f, gain));
}

void OutputCore::process(juce::AudioBuffer<float>& buffer, float gain) const
{
    buffer.applyGain(juce::jlimit(0.0f, 1.0f, gain));
}

