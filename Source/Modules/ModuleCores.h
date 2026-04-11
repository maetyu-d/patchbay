#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

class OscillatorCore
{
public:
    void prepare(double newSampleRate);
    void render(juce::AudioBuffer<float>& output, float frequency, float level);

private:
    double sampleRate = 44100.0;
    float phase = 0.0f;
};

class LfoCore
{
public:
    void prepare(double newSampleRate);
    float advanceBlock(int numSamples, float rateHz, float depth);
    void renderAsAudio(juce::AudioBuffer<float>& output, float rateHz, float depth);
    float getCurrentNormalisedValue() const;

private:
    double sampleRate = 44100.0;
    float phase = 0.0f;
};

class GainCore
{
public:
    void process(juce::AudioBuffer<float>& buffer, float gain) const;
};

class OutputCore
{
public:
    void process(juce::AudioBuffer<float>& buffer, float gain) const;
};
