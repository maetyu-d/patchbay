#include "ExampleModules.h"
#include "ModuleCores.h"

namespace
{
constexpr double bpm = 120.0;

class AudioTrackEditorComponent;
class MidiTrackEditorComponent;

float modToUnitRange(float value)
{
    if (value >= 0.0f && value <= 1.0f)
        return value;

    return juce::jlimit(0.0f, 1.0f, 0.5f + 0.5f * value);
}

std::pair<float, float> normaliseLoopPoints(float start, float end)
{
    auto clampedStart = juce::jlimit(0.0f, 0.99f, start);
    auto clampedEnd = juce::jlimit(clampedStart + 0.01f, 1.0f, end);

    if (clampedEnd - clampedStart < 0.01f)
        clampedEnd = juce::jmin(1.0f, clampedStart + 0.01f);

    return { clampedStart, clampedEnd };
}

float midiToFrequency(int midiNote)
{
    return 440.0f * std::pow(2.0f, static_cast<float>(midiNote - 69) / 12.0f);
}

double msToSamples(double sampleRate, float milliseconds)
{
    return juce::jmax(1.0, sampleRate * static_cast<double>(juce::jmax(1.0f, milliseconds)) * 0.001);
}

juce::String encodeAudioBuffer(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    juce::MemoryOutputStream stream;
    stream.writeInt(buffer.getNumChannels());
    stream.writeInt(buffer.getNumSamples());
    stream.writeDouble(sampleRate);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        stream.write(buffer.getReadPointer(channel),
                     static_cast<size_t>(static_cast<size_t>(buffer.getNumSamples()) * sizeof(float)));

    return stream.getMemoryBlock().toBase64Encoding();
}

bool decodeAudioBuffer(const juce::String& encoded, juce::AudioBuffer<float>& buffer, double& sampleRate)
{
    if (encoded.isEmpty())
        return false;

    juce::MemoryBlock memory;
    if (! memory.fromBase64Encoding(encoded))
        return false;

    juce::MemoryInputStream stream(memory, false);
    const auto channels = stream.readInt();
    const auto samples = stream.readInt();
    sampleRate = stream.readDouble();

    if (channels <= 0 || samples <= 0)
        return false;

    buffer.setSize(channels, samples);

    for (int channel = 0; channel < channels; ++channel)
        stream.read(buffer.getWritePointer(channel),
                    static_cast<int>(static_cast<size_t>(samples) * sizeof(float)));

    return true;
}

class OscillatorModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override
    {
        return "Oscillator";
    }

    juce::String getDisplayName() const override
    {
        return "Oscillator";
    }

    juce::Colour getNodeColour() const override { return juce::Colour(0xff4ecdc4); }

    std::vector<PortInfo> getInputPorts() const override
    {
        return {
            { "Pitch", PortKind::modulation },
            { "Level", PortKind::modulation }
        };
    }

    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "Audio", PortKind::audio },
            { "Rate", PortKind::modulation }
        };
    }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "frequency", "Frequency", 40.0f, 1200.0f, 220.0f },
            { "level", "Level", 0.0f, 1.0f, 0.18f }
        };
    }

    void prepare(double newSampleRate, int) override
    {
        core.prepare(newSampleRate);
    }

    void process(NodeRenderContext& context) override
    {
        auto* output = context.audioOutputs.front();
        const auto pitchCv = context.modInputs.size() > 0 ? context.modInputs[0] : 0.0f;
        const auto levelCv = context.modInputs.size() > 1 ? context.modInputs[1] : 0.0f;
        const auto frequency = baseFrequency + (pitchCv * 220.0f);
        const auto level = baseLevel + levelCv;
        core.render(*output, frequency, level);

        if (! context.modOutputs.empty())
            context.modOutputs[0] = juce::jlimit(0.125f, 4.0f, frequency / 220.0f);
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "frequency")
            return baseFrequency;
        if (parameterId == "level")
            return baseLevel;
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "frequency")
            baseFrequency = juce::jlimit(40.0f, 1200.0f, newValue);
        else if (parameterId == "level")
            baseLevel = juce::jlimit(0.0f, 1.0f, newValue);
    }

private:
    OscillatorCore core;
    float baseFrequency = 220.0f;
    float baseLevel = 0.18f;
};

class LfoModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override
    {
        return "LFO";
    }

    juce::String getDisplayName() const override
    {
        return "LFO";
    }

    juce::Colour getNodeColour() const override { return juce::Colour(0xffff9f1c); }

    std::vector<PortInfo> getInputPorts() const override
    {
        return {};
    }

    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "Value", PortKind::modulation },
            { "Rate Hz", PortKind::modulation },
            { "Phase", PortKind::modulation }
        };
    }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "rate", "Rate", 0.05f, 12.0f, 0.85f },
            { "depth", "Depth", 0.0f, 1.0f, 0.45f }
        };
    }

    void prepare(double newSampleRate, int) override
    {
        core.prepare(newSampleRate);
    }

    void process(NodeRenderContext& context) override
    {
        if (context.modOutputs.size() > 0)
            context.modOutputs[0] = core.advanceBlock(context.numSamples, rateHz, depth);
        if (context.modOutputs.size() > 1)
            context.modOutputs[1] = rateHz;
        if (context.modOutputs.size() > 2)
            context.modOutputs[2] = core.getCurrentNormalisedValue();
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "rate")
            return rateHz;
        if (parameterId == "depth")
            return depth;
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "rate")
            rateHz = juce::jlimit(0.05f, 12.0f, newValue);
        else if (parameterId == "depth")
            depth = juce::jlimit(0.0f, 1.0f, newValue);
    }

private:
    LfoCore core;
    float rateHz = 0.85f;
    float depth = 0.45f;
};

class BpmToLfoModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override { return "BpmToLfo"; }
    juce::String getDisplayName() const override { return "BPM to LFO"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xff93c5fd); }
    std::vector<PortInfo> getInputPorts() const override { return {}; }
    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "Rate Hz", PortKind::modulation },
            { "Phase", PortKind::modulation }
        };
    }
    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "bpm", "BPM", 20.0f, 240.0f, 120.0f },
            { "multiplier", "Multiplier", 0.25f, 8.0f, 1.0f }
        };
    }

    void prepare(double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
    }

    void process(NodeRenderContext& context) override
    {
        const auto rateHz = (bpmValue / 60.0f) * multiplier;
        const auto phaseDelta = juce::MathConstants<float>::twoPi * rateHz / static_cast<float>(sampleRate);

        for (int sample = 0; sample < context.numSamples; ++sample)
        {
            phase += phaseDelta;
            if (phase >= juce::MathConstants<float>::twoPi)
                phase -= juce::MathConstants<float>::twoPi;
        }

        if (context.modOutputs.size() > 0)
            context.modOutputs[0] = rateHz;
        if (context.modOutputs.size() > 1)
            context.modOutputs[1] = 0.5f + 0.5f * std::sin(phase);
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "bpm")
            return bpmValue;
        if (parameterId == "multiplier")
            return multiplier;
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "bpm")
            bpmValue = juce::jlimit(20.0f, 240.0f, newValue);
        else if (parameterId == "multiplier")
            multiplier = juce::jlimit(0.25f, 8.0f, newValue);
    }

private:
    double sampleRate = 44100.0;
    float bpmValue = 120.0f;
    float multiplier = 1.0f;
    float phase = 0.0f;
};

class TimeSignatureModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override { return "TimeSignature"; }
    juce::String getDisplayName() const override { return "Time Signature"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xffc4b5fd); }
    std::vector<PortInfo> getInputPorts() const override { return { { "Rate In", PortKind::modulation } }; }
    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "Beat Rate", PortKind::modulation },
            { "Bar Rate", PortKind::modulation },
            { "Bar Phase", PortKind::modulation }
        };
    }
    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "numerator", "Numerator", 1.0f, 12.0f, 4.0f },
            { "denominator", "Denominator", 2.0f, 16.0f, 4.0f }
        };
    }

    void prepare(double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
    }

    void process(NodeRenderContext& context) override
    {
        const auto beatRate = context.modInputs.empty() || context.modInputs[0] <= 0.0f ? 1.0f : context.modInputs[0];
        const auto beatsPerBar = juce::jmax(0.25f, numerator * (4.0f / denominator));
        const auto barRate = beatRate / beatsPerBar;
        const auto phaseDelta = juce::MathConstants<float>::twoPi * barRate / static_cast<float>(sampleRate);

        for (int sample = 0; sample < context.numSamples; ++sample)
        {
            barPhase += phaseDelta;
            if (barPhase >= juce::MathConstants<float>::twoPi)
                barPhase -= juce::MathConstants<float>::twoPi;
        }

        if (context.modOutputs.size() > 0)
            context.modOutputs[0] = beatRate;
        if (context.modOutputs.size() > 1)
            context.modOutputs[1] = barRate;
        if (context.modOutputs.size() > 2)
            context.modOutputs[2] = 0.5f + 0.5f * std::sin(barPhase);
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "numerator")
            return numerator;
        if (parameterId == "denominator")
            return denominator;
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "numerator")
            numerator = juce::jlimit(1.0f, 12.0f, std::round(newValue));
        else if (parameterId == "denominator")
            denominator = juce::jlimit(2.0f, 16.0f, std::round(newValue));
    }

private:
    double sampleRate = 44100.0;
    float numerator = 4.0f;
    float denominator = 4.0f;
    float barPhase = 0.0f;
};

class AdEnvelopeModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override { return "AD"; }
    juce::String getDisplayName() const override { return "AD Envelope"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xfffca311); }

    std::vector<PortInfo> getInputPorts() const override
    {
        return {
            { "Trigger", PortKind::modulation }
        };
    }

    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "Envelope", PortKind::modulation },
            { "Done", PortKind::modulation }
        };
    }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "attackMs", "Attack", 1.0f, 4000.0f, 80.0f },
            { "decayMs", "Decay", 1.0f, 6000.0f, 280.0f },
            { "amount", "Amount", 0.0f, 1.0f, 1.0f }
        };
    }

    void prepare(double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
    }

    void process(NodeRenderContext& context) override
    {
        const auto trigger = ! context.modInputs.empty() && context.modInputs[0] > 0.5f;
        auto firedEnd = false;

        if (trigger && ! lastTriggerHigh)
        {
            stage = Stage::attack;
            value = 0.0f;
        }

        lastTriggerHigh = trigger;

        for (int sample = 0; sample < context.numSamples; ++sample)
        {
            switch (stage)
            {
                case Stage::idle:
                    value = 0.0f;
                    break;
                case Stage::attack:
                    value += static_cast<float>(1.0 / msToSamples(sampleRate, attackMs));
                    if (value >= 1.0f)
                    {
                        value = 1.0f;
                        stage = Stage::decay;
                    }
                    break;
                case Stage::decay:
                    value -= static_cast<float>(1.0 / msToSamples(sampleRate, decayMs));
                    if (value <= 0.0f)
                    {
                        value = 0.0f;
                        stage = Stage::idle;
                        firedEnd = true;
                    }
                    break;
            }
        }

        if (! context.modOutputs.empty())
            context.modOutputs[0] = value * amount;
        if (context.modOutputs.size() > 1)
            context.modOutputs[1] = firedEnd ? 1.0f : 0.0f;
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "attackMs") return attackMs;
        if (parameterId == "decayMs") return decayMs;
        if (parameterId == "amount") return amount;
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "attackMs") attackMs = juce::jlimit(1.0f, 4000.0f, newValue);
        else if (parameterId == "decayMs") decayMs = juce::jlimit(1.0f, 6000.0f, newValue);
        else if (parameterId == "amount") amount = juce::jlimit(0.0f, 1.0f, newValue);
    }

private:
    enum class Stage { idle, attack, decay };

    double sampleRate = 44100.0;
    float attackMs = 80.0f;
    float decayMs = 280.0f;
    float amount = 1.0f;
    float value = 0.0f;
    bool lastTriggerHigh = false;
    Stage stage = Stage::idle;
};

class AdsrEnvelopeModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override { return "ADSR"; }
    juce::String getDisplayName() const override { return "ADSR Envelope"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xffef476f); }

    std::vector<PortInfo> getInputPorts() const override
    {
        return {
            { "Gate", PortKind::modulation }
        };
    }

    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "Envelope", PortKind::modulation },
            { "Done", PortKind::modulation }
        };
    }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "attackMs", "Attack", 1.0f, 4000.0f, 40.0f },
            { "decayMs", "Decay", 1.0f, 6000.0f, 220.0f },
            { "sustain", "Sustain", 0.0f, 1.0f, 0.55f },
            { "releaseMs", "Release", 1.0f, 8000.0f, 420.0f },
            { "amount", "Amount", 0.0f, 1.0f, 1.0f }
        };
    }

    void prepare(double newSampleRate, int) override
    {
        sampleRate = newSampleRate;
    }

    void process(NodeRenderContext& context) override
    {
        const auto gate = ! context.modInputs.empty() && context.modInputs[0] > 0.5f;
        auto firedEnd = false;

        if (gate && ! lastGateHigh)
            stage = Stage::attack;
        else if (! gate && lastGateHigh && stage != Stage::idle)
            stage = Stage::release;

        lastGateHigh = gate;

        for (int sample = 0; sample < context.numSamples; ++sample)
        {
            switch (stage)
            {
                case Stage::idle:
                    value = 0.0f;
                    break;
                case Stage::attack:
                    value += static_cast<float>(1.0 / msToSamples(sampleRate, attackMs));
                    if (value >= 1.0f)
                    {
                        value = 1.0f;
                        stage = Stage::decay;
                    }
                    break;
                case Stage::decay:
                    value -= static_cast<float>((1.0f - sustain) / msToSamples(sampleRate, decayMs));
                    if (value <= sustain)
                    {
                        value = sustain;
                        stage = gate ? Stage::sustain : Stage::release;
                    }
                    break;
                case Stage::sustain:
                    value = sustain;
                    if (! gate)
                        stage = Stage::release;
                    break;
                case Stage::release:
                    value -= static_cast<float>(juce::jmax(0.0001f, value) / msToSamples(sampleRate, releaseMs));
                    if (value <= 0.0005f)
                    {
                        value = 0.0f;
                        stage = Stage::idle;
                        firedEnd = true;
                    }
                    break;
            }
        }

        if (! context.modOutputs.empty())
            context.modOutputs[0] = value * amount;
        if (context.modOutputs.size() > 1)
            context.modOutputs[1] = firedEnd ? 1.0f : 0.0f;
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "attackMs") return attackMs;
        if (parameterId == "decayMs") return decayMs;
        if (parameterId == "sustain") return sustain;
        if (parameterId == "releaseMs") return releaseMs;
        if (parameterId == "amount") return amount;
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "attackMs") attackMs = juce::jlimit(1.0f, 4000.0f, newValue);
        else if (parameterId == "decayMs") decayMs = juce::jlimit(1.0f, 6000.0f, newValue);
        else if (parameterId == "sustain") sustain = juce::jlimit(0.0f, 1.0f, newValue);
        else if (parameterId == "releaseMs") releaseMs = juce::jlimit(1.0f, 8000.0f, newValue);
        else if (parameterId == "amount") amount = juce::jlimit(0.0f, 1.0f, newValue);
    }

private:
    enum class Stage { idle, attack, decay, sustain, release };

    double sampleRate = 44100.0;
    float attackMs = 40.0f;
    float decayMs = 220.0f;
    float sustain = 0.55f;
    float releaseMs = 420.0f;
    float amount = 1.0f;
    float value = 0.0f;
    bool lastGateHigh = false;
    Stage stage = Stage::idle;
};

class FilterModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override { return "Filter"; }
    juce::String getDisplayName() const override { return "Filter"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xff70e000); }

    std::vector<PortInfo> getInputPorts() const override
    {
        return {
            { "Audio In", PortKind::audio },
            { "Cutoff", PortKind::modulation },
            { "Resonance", PortKind::modulation }
        };
    }

    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "Audio Out", PortKind::audio }
        };
    }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "mode", "Type", 0.0f, 3.0f, 0.0f },
            { "cutoff", "Cutoff", 40.0f, 18000.0f, 1200.0f },
            { "resonance", "Resonance", 0.1f, 1.5f, 0.45f },
            { "drive", "Drive", 0.5f, 3.0f, 1.0f }
        };
    }

    void prepare(double newSampleRate, int) override
    {
        sampleRate = newSampleRate;

        for (auto& filter : filters)
            filter.reset();

        updateCoefficients();
    }

    void process(NodeRenderContext& context) override
    {
        auto* input = context.audioInputs[0];
        auto* output = context.audioOutputs[0];
        output->makeCopyOf(*input);

        const auto cutoffUnit = context.modInputs.size() > 0 ? modToUnitRange(context.modInputs[0]) : -1.0f;
        const auto resonanceUnit = context.modInputs.size() > 1 ? modToUnitRange(context.modInputs[1]) : -1.0f;
        const auto effectiveCutoff = cutoffUnit >= 0.0f ? juce::jmap(cutoffUnit, 40.0f, 18000.0f) : cutoff;
        const auto effectiveResonance = resonanceUnit >= 0.0f ? juce::jmap(resonanceUnit, 0.1f, 1.5f) : resonance;

        currentCutoff = juce::jlimit(40.0f, 18000.0f, effectiveCutoff);
        currentResonance = juce::jlimit(0.1f, 1.5f, effectiveResonance);
        updateCoefficients();

        output->applyGain(drive);

        for (int channel = 0; channel < juce::jmin(2, output->getNumChannels()); ++channel)
        {
            auto* channelData = output->getWritePointer(channel);

            for (int sample = 0; sample < output->getNumSamples(); ++sample)
                channelData[sample] = filters[static_cast<size_t>(channel)].processSingleSampleRaw(channelData[sample]);
        }

        output->applyGain(1.0f / juce::jmax(1.0f, drive));
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "mode") return static_cast<float>(mode);
        if (parameterId == "cutoff") return cutoff;
        if (parameterId == "resonance") return resonance;
        if (parameterId == "drive") return drive;
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "mode") mode = juce::jlimit(0, 3, static_cast<int>(std::round(newValue)));
        else if (parameterId == "cutoff") cutoff = juce::jlimit(40.0f, 18000.0f, newValue);
        else if (parameterId == "resonance") resonance = juce::jlimit(0.1f, 1.5f, newValue);
        else if (parameterId == "drive") drive = juce::jlimit(0.5f, 3.0f, newValue);

        currentCutoff = cutoff;
        currentResonance = resonance;
        updateCoefficients();
    }

private:
    void updateCoefficients()
    {
        for (auto& filter : filters)
        {
            switch (mode)
            {
                case 1: filter.setCoefficients(juce::IIRCoefficients::makeHighPass(sampleRate, currentCutoff, currentResonance)); break;
                case 2: filter.setCoefficients(juce::IIRCoefficients::makeBandPass(sampleRate, currentCutoff, currentResonance)); break;
                case 3: filter.setCoefficients(juce::IIRCoefficients::makeNotchFilter(sampleRate, currentCutoff, currentResonance)); break;
                default: filter.setCoefficients(juce::IIRCoefficients::makeLowPass(sampleRate, currentCutoff, currentResonance)); break;
            }
        }
    }

    double sampleRate = 44100.0;
    std::array<juce::IIRFilter, 2> filters;
    int mode = 0;
    float cutoff = 1200.0f;
    float resonance = 0.45f;
    float drive = 1.0f;
    float currentCutoff = 1200.0f;
    float currentResonance = 0.45f;
};

class GainModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override
    {
        return "Gain";
    }

    juce::String getDisplayName() const override
    {
        return "Gain";
    }

    juce::Colour getNodeColour() const override { return juce::Colour(0xff7bd389); }

    std::vector<PortInfo> getInputPorts() const override
    {
        return {
            { "Audio In", PortKind::audio },
            { "Gain CV", PortKind::modulation }
        };
    }

    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "Audio Out", PortKind::audio }
        };
    }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "gain", "Gain", 0.0f, 2.0f, 0.65f }
        };
    }

    void process(NodeRenderContext& context) override
    {
        auto* input = context.audioInputs[0];
        auto* output = context.audioOutputs[0];

        output->makeCopyOf(*input);

        const auto cv = context.modInputs.size() > 0 ? context.modInputs[0] : 0.0f;
        core.process(*output, baseGain + cv);
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        return parameterId == "gain" ? baseGain : 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "gain")
            baseGain = juce::jlimit(0.0f, 2.0f, newValue);
    }

private:
    GainCore core;
    float baseGain = 0.65f;
};

class MathNodeBase : public ModuleNode
{
public:
    std::vector<PortInfo> getInputPorts() const override
    {
        return {
            { "A", PortKind::modulation },
            { "B", PortKind::modulation }
        };
    }

    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "Result", PortKind::modulation }
        };
    }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "aBias", "A Bias", -4.0f, 4.0f, 0.0f },
            { "bBias", "B Bias", -4.0f, 4.0f, 0.0f }
        };
    }

    void process(NodeRenderContext& context) override
    {
        const auto a = (context.modInputs.size() > 0 ? context.modInputs[0] : 0.0f) + aBias;
        const auto b = (context.modInputs.size() > 1 ? context.modInputs[1] : 0.0f) + bBias;

        if (! context.modOutputs.empty())
            context.modOutputs[0] = compute(a, b);
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "aBias")
            return aBias;
        if (parameterId == "bBias")
            return bBias;
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "aBias")
            aBias = juce::jlimit(-4.0f, 4.0f, newValue);
        else if (parameterId == "bBias")
            bBias = juce::jlimit(-4.0f, 4.0f, newValue);
    }

protected:
    virtual float compute(float a, float b) const = 0;

private:
    float aBias = 0.0f;
    float bBias = 0.0f;
};

class AddModule final : public MathNodeBase
{
public:
    juce::String getTypeId() const override { return "Add"; }
    juce::String getDisplayName() const override { return "Add"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xfff59e0b); }

protected:
    float compute(float a, float b) const override { return a + b; }
};

class SubtractModule final : public MathNodeBase
{
public:
    juce::String getTypeId() const override { return "Subtract"; }
    juce::String getDisplayName() const override { return "Subtract"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xfff97316); }

protected:
    float compute(float a, float b) const override { return a - b; }
};

class MultiplyModule final : public MathNodeBase
{
public:
    juce::String getTypeId() const override { return "Multiply"; }
    juce::String getDisplayName() const override { return "Multiply"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xff22c55e); }

protected:
    float compute(float a, float b) const override { return a * b; }
};

class DivideModule final : public MathNodeBase
{
public:
    juce::String getTypeId() const override { return "Divide"; }
    juce::String getDisplayName() const override { return "Divide"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xff38bdf8); }

protected:
    float compute(float a, float b) const override
    {
        return std::abs(b) < 0.0001f ? 0.0f : a / b;
    }
};

class OutputModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override
    {
        return "Output";
    }

    juce::String getDisplayName() const override
    {
        return "Output";
    }

    juce::Colour getNodeColour() const override { return juce::Colour(0xffa0aec0); }

    std::vector<PortInfo> getInputPorts() const override
    {
        return {
            { "Audio In", PortKind::audio }
        };
    }

    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "Monitor", PortKind::audio }
        };
    }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "trim", "Trim", 0.0f, 1.0f, 0.85f }
        };
    }

    void process(NodeRenderContext& context) override
    {
        auto* input = context.audioInputs[0];
        auto* output = context.audioOutputs[0];
        output->makeCopyOf(*input);
        core.process(*output, masterGain);
    }

    bool contributesToMasterOutput() const override
    {
        return true;
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        return parameterId == "trim" ? masterGain : 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "trim")
            masterGain = juce::jlimit(0.0f, 1.0f, newValue);
    }

private:
    OutputCore core;
    float masterGain = 0.85f;
};

class SumModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override { return "Sum"; }
    juce::String getDisplayName() const override { return "Sum"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xfff6bd60); }

    std::vector<PortInfo> getInputPorts() const override
    {
        std::vector<PortInfo> ports;
        ports.reserve(static_cast<size_t>(inputCount));

        for (int index = 0; index < inputCount; ++index)
            ports.push_back({ "In " + juce::String(index + 1), PortKind::audio });


        return ports;
    }

    std::vector<PortInfo> getOutputPorts() const override
    {
        return { { "Mix Out", PortKind::audio } };
    }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "channels", "Inputs", 2.0f, 8.0f, 4.0f },
            { "trim", "Trim", 0.0f, 2.0f, 1.0f }
        };
    }

    void process(NodeRenderContext& context) override
    {
        auto* output = context.audioOutputs.front();
        output->clear();

        for (auto* input : context.audioInputs)
            for (int channel = 0; channel < juce::jmin(output->getNumChannels(), input->getNumChannels()); ++channel)
                output->addFrom(channel, 0, *input, channel, 0, context.numSamples);

        output->applyGain(trim);
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "channels")
            return static_cast<float>(inputCount);
        if (parameterId == "trim")
            return trim;
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "channels")
            inputCount = juce::jlimit(2, 8, static_cast<int>(std::round(newValue)));
        else if (parameterId == "trim")
            trim = juce::jlimit(0.0f, 2.0f, newValue);
    }

private:
    int inputCount = 4;
    float trim = 1.0f;
};

class RouterModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override { return "Router"; }
    juce::String getDisplayName() const override { return "Router"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xff84cc16); }

    std::vector<PortInfo> getInputPorts() const override
    {
        return {
            { "Audio In", PortKind::audio },
            { "CV In", PortKind::modulation },
            { "Step", PortKind::modulation }
        };
    }

    std::vector<PortInfo> getOutputPorts() const override
    {
        std::vector<PortInfo> ports;
        ports.reserve(static_cast<size_t>(destinationCount * 2));

        for (int index = 0; index < destinationCount; ++index)
        {
            ports.push_back({ "Audio " + juce::String(index + 1), PortKind::audio });
            ports.push_back({ "CV " + juce::String(index + 1), PortKind::modulation });
        }

        return ports;
    }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "destinations", "Destinations", 2.0f, 8.0f, 2.0f },
            { "activeRoute", "Active Route", 1.0f, 8.0f, 1.0f }
        };
    }

    void process(NodeRenderContext& context) override
    {
        for (auto* output : context.audioOutputs)
            output->clear();

        std::fill(context.modOutputs.begin(), context.modOutputs.end(), 0.0f);

        const auto triggerValue = context.modInputs.size() > 1 ? context.modInputs[1] : 0.0f;
        const auto triggerHigh = triggerValue > 0.5f;

        if (triggerHigh && ! lastTriggerHigh)
            activeRoute = (activeRoute + 1) % juce::jmax(1, destinationCount);

        lastTriggerHigh = triggerHigh;

        const auto routeIndex = juce::jlimit(0, juce::jmax(0, destinationCount - 1), activeRoute);
        const auto audioOutputIndex = routeIndex;
        const auto modOutputIndex = routeIndex;

        if (! context.audioInputs.empty()
            && juce::isPositiveAndBelow(audioOutputIndex, static_cast<int>(context.audioOutputs.size()))
            && context.audioInputs[0] != nullptr
            && context.audioOutputs[static_cast<size_t>(audioOutputIndex)] != nullptr)
        {
            auto* source = context.audioInputs[0];
            auto* destination = context.audioOutputs[static_cast<size_t>(audioOutputIndex)];

            for (int channel = 0; channel < juce::jmin(source->getNumChannels(), destination->getNumChannels()); ++channel)
                destination->addFrom(channel, 0, *source, channel, 0, context.numSamples);
        }

        if (! context.modInputs.empty()
            && juce::isPositiveAndBelow(modOutputIndex, static_cast<int>(context.modOutputs.size())))
        {
            context.modOutputs[static_cast<size_t>(modOutputIndex)] = context.modInputs[0];
        }
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "destinations")
            return static_cast<float>(destinationCount);
        if (parameterId == "activeRoute")
            return static_cast<float>(activeRoute + 1);
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "destinations")
            destinationCount = juce::jlimit(2, 8, static_cast<int>(std::round(newValue)));
        else if (parameterId == "activeRoute")
            activeRoute = juce::jlimit(0, juce::jmax(0, destinationCount - 1), static_cast<int>(std::round(newValue)) - 1);

        activeRoute = juce::jlimit(0, juce::jmax(0, destinationCount - 1), activeRoute);
    }

private:
    int destinationCount = 2;
    int activeRoute = 0;
    bool lastTriggerHigh = false;
};

class AudioTrackModule final : public ModuleNode
{
public:
    ~AudioTrackModule() override = default;

    AudioTrackModule()
    {
        formatManager.registerBasicFormats();
    }

    juce::String getTypeId() const override { return "AudioTrack"; }
    juce::String getDisplayName() const override { return "Audio Track"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xff4d96ff); }

    std::vector<PortInfo> getInputPorts() const override
    {
        return {
            { "Play", PortKind::modulation },
            { "Speed", PortKind::modulation },
            { "Loop Start", PortKind::modulation },
            { "Loop End", PortKind::modulation }
        };
    }
    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "Audio Out", PortKind::audio },
            { "Done", PortKind::modulation }
        };
    }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "volume", "Volume", 0.0f, 1.5f, 0.8f },
            { "pan", "Pan", -1.0f, 1.0f, 0.0f },
            { "mute", "Mute", 0.0f, 1.0f, 0.0f },
            { "looping", "Loop", 0.0f, 1.0f, 0.0f },
            { "startPoint", "Start", 0.0f, 0.99f, 0.0f },
            { "endPoint", "End", 0.01f, 1.0f, 1.0f },
            { "loopStart", "Loop Start", 0.0f, 0.99f, 0.0f },
            { "loopEnd", "Loop End", 0.01f, 1.0f, 1.0f }
        };
    }

    void process(NodeRenderContext& context) override
    {
        auto* output = context.audioOutputs.front();
        output->clear();
        if (! context.modOutputs.empty())
            context.modOutputs[0] = 0.0f;

        const auto playTriggerHigh = ! context.modInputs.empty() && context.modInputs[0] > 0.5f;
        if (playTriggerHigh && ! lastPlayTriggerHigh)
        {
            isRunning = true;
            playbackSamplePosition = 0.0;
            emittedEndTrigger = false;
        }
        lastPlayTriggerHigh = playTriggerHigh;

        if (! context.isPlaying || mute > 0.5f || audioClip.getNumSamples() == 0 || ! isRunning)
            return;

        const auto rateControl = context.modInputs.size() < 2 || context.modInputs[1] <= 0.0f ? 1.0 : static_cast<double>(context.modInputs[1]);
        const auto playbackRate = juce::jlimit(0.125, 4.0, rateControl);
        lastTransportSamplePosition = static_cast<int64_t>(std::llround(playbackSamplePosition));
        lastPlaybackRate = playbackRate;
        const auto ratio = (audioClipSampleRate / context.sampleRate) * playbackRate;
        auto effectiveLoopStart = loopStart;
        auto effectiveLoopEnd = loopEnd;
        std::tie(startPoint, endPoint) = normaliseLoopPoints(startPoint, endPoint);

        if (context.modInputs.size() > 2)
            effectiveLoopStart = modToUnitRange(context.modInputs[2]);
        if (context.modInputs.size() > 3)
            effectiveLoopEnd = modToUnitRange(context.modInputs[3]);

        std::tie(effectiveLoopStart, effectiveLoopEnd) = normaliseLoopPoints(effectiveLoopStart, effectiveLoopEnd);
        effectiveLoopStart = juce::jlimit(startPoint, juce::jmax(startPoint, endPoint - 0.01f), effectiveLoopStart);
        effectiveLoopEnd = juce::jlimit(effectiveLoopStart + 0.01f, endPoint, effectiveLoopEnd);
        lastLoopStart = effectiveLoopStart;
        lastLoopEnd = effectiveLoopEnd;

        const auto loopStartSample = static_cast<int>(std::floor(effectiveLoopStart * static_cast<float>(audioClip.getNumSamples() - 1)));
        const auto loopEndSample = juce::jlimit(loopStartSample + 1, audioClip.getNumSamples(), static_cast<int>(std::ceil(effectiveLoopEnd * static_cast<float>(audioClip.getNumSamples()))));
        const auto loopLength = juce::jmax(1, loopEndSample - loopStartSample);
        const auto leftGain = volume * juce::jlimit(0.0f, 1.0f, 1.0f - juce::jmax(0.0f, pan));
        const auto rightGain = volume * juce::jlimit(0.0f, 1.0f, 1.0f + juce::jmin(0.0f, pan));
        auto reachedEndThisBlock = false;

        for (int sample = 0; sample < context.numSamples; ++sample)
        {
            if (! isRunning)
                break;

            auto clipPosition = static_cast<double>(loopStartSample) + playbackSamplePosition;

            if (looping > 0.5f)
            {
                while (clipPosition >= static_cast<double>(loopEndSample))
                {
                    clipPosition -= static_cast<double>(loopLength);
                    playbackSamplePosition -= static_cast<double>(loopLength);
                    reachedEndThisBlock = true;
                }
            }
            else if (clipPosition >= static_cast<double>(loopEndSample))
            {
                isRunning = false;
                reachedEndThisBlock = true;
                break;
            }

            const auto clipIndex = static_cast<int>(clipPosition);
            const auto nextIndex = clipIndex + 1 < loopEndSample ? clipIndex + 1 : loopStartSample;
            const auto fraction = static_cast<float>(clipPosition - static_cast<double>(clipIndex));

            const auto left = juce::jmap(fraction, audioClip.getSample(0, clipIndex), audioClip.getSample(0, nextIndex));
            const auto rightChannel = juce::jmin(1, audioClip.getNumChannels() - 1);
            const auto right = juce::jmap(fraction, audioClip.getSample(rightChannel, clipIndex), audioClip.getSample(rightChannel, nextIndex));

            output->addSample(0, sample, left * leftGain);
            if (output->getNumChannels() > 1)
                output->addSample(1, sample, right * rightGain);

            playbackSamplePosition += ratio;
        }

        if (reachedEndThisBlock && ! emittedEndTrigger && ! context.modOutputs.empty())
            context.modOutputs[0] = 1.0f;

        emittedEndTrigger = reachedEndThisBlock && looping <= 0.5f;
        if (looping > 0.5f)
            emittedEndTrigger = false;
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "volume") return volume;
        if (parameterId == "pan") return pan;
        if (parameterId == "mute") return mute;
        if (parameterId == "looping") return looping;
        if (parameterId == "startPoint") return startPoint;
        if (parameterId == "endPoint") return endPoint;
        if (parameterId == "loopStart") return loopStart;
        if (parameterId == "loopEnd") return loopEnd;
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "volume") volume = juce::jlimit(0.0f, 1.5f, newValue);
        else if (parameterId == "pan") pan = juce::jlimit(-1.0f, 1.0f, newValue);
        else if (parameterId == "mute") mute = juce::jlimit(0.0f, 1.0f, newValue);
        else if (parameterId == "looping") looping = newValue > 0.5f ? 1.0f : 0.0f;
        else if (parameterId == "startPoint") startPoint = juce::jlimit(0.0f, 0.99f, newValue);
        else if (parameterId == "endPoint") endPoint = juce::jlimit(0.01f, 1.0f, newValue);
        else if (parameterId == "loopStart") loopStart = juce::jlimit(0.0f, 0.99f, newValue);
        else if (parameterId == "loopEnd") loopEnd = juce::jlimit(0.01f, 1.0f, newValue);

        std::tie(startPoint, endPoint) = normaliseLoopPoints(startPoint, endPoint);
        std::tie(loopStart, loopEnd) = normaliseLoopPoints(loopStart, loopEnd);
    }

    bool isTrackModule() const override { return true; }
    juce::String getTrackTypeId() const override { return "audio"; }
    juce::String getResourcePath() const override { return filePath; }

    bool loadFile(const juce::File& file) override
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader == nullptr)
            return false;

        juce::AudioBuffer<float> clipBuffer(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
        reader->read(&clipBuffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
        audioClip = std::move(clipBuffer);
        audioClipSampleRate = reader->sampleRate;
        filePath = file.getFullPathName();
        return true;
    }

    void paintWaveform(float normalisedX, float normalisedY, float brushSize)
    {
        if (audioClip.getNumSamples() == 0)
            return;

        const auto clampedX = juce::jlimit(0.0f, 1.0f, normalisedX);
        const auto clampedY = juce::jlimit(0.0f, 1.0f, normalisedY);
        const auto targetAmplitude = juce::jmap(clampedY, 1.0f, -1.0f);
        const auto centreSample = juce::jlimit(0, audioClip.getNumSamples() - 1,
                                               static_cast<int>(std::round(clampedX * static_cast<float>(audioClip.getNumSamples() - 1))));
        const auto radiusSamples = juce::jmax(1, static_cast<int>(std::round(brushSize * static_cast<float>(audioClip.getNumSamples()))));

        for (int sample = juce::jmax(0, centreSample - radiusSamples); sample < juce::jmin(audioClip.getNumSamples(), centreSample + radiusSamples + 1); ++sample)
        {
            const auto distance = std::abs(sample - centreSample);
            const auto blend = 1.0f - (static_cast<float>(distance) / static_cast<float>(juce::jmax(1, radiusSamples)));
            const auto strength = blend * 0.85f;

            for (int channel = 0; channel < audioClip.getNumChannels(); ++channel)
            {
                const auto current = audioClip.getSample(channel, sample);
                const auto painted = juce::jlimit(-1.0f, 1.0f, current + (targetAmplitude - current) * strength);
                audioClip.setSample(channel, sample, painted);
            }
        }
    }

    juce::ValueTree saveState() const override
    {
        auto state = ModuleNode::saveState();
        state.setProperty("filePath", filePath, nullptr);
        state.setProperty("audioData", encodeAudioBuffer(audioClip, audioClipSampleRate), nullptr);
        state.setProperty("editorOpen", editorOpen, nullptr);
        state.setProperty("editorDetached", editorDetached, nullptr);
        state.setProperty("editorScale", editorScale, nullptr);
        return state;
    }

    void loadState(const juce::ValueTree& state) override
    {
        ModuleNode::loadState(state);
        const auto encodedAudio = state.getProperty("audioData").toString();
        const auto path = state.getProperty("filePath").toString();

        if (! decodeAudioBuffer(encodedAudio, audioClip, audioClipSampleRate) && path.isNotEmpty())
            loadFile(juce::File(path));

        filePath = path;

        editorOpen = static_cast<bool>(state.getProperty("editorOpen", false));
        editorDetached = static_cast<bool>(state.getProperty("editorDetached", false));
        editorScale = static_cast<float>(state.getProperty("editorScale", 1.0f));
    }

    bool supportsEmbeddedEditor() const override { return true; }
    bool isEditorOpen() const override { return editorOpen; }
    void setEditorOpen(bool shouldBeOpen) override
    {
        editorOpen = shouldBeOpen;
        if (! editorOpen)
            editorDetached = false;
    }
    bool isEditorDetached() const override { return editorDetached; }
    void setEditorDetached(bool shouldBeDetached) override
    {
        editorDetached = shouldBeDetached && editorOpen;
    }
    float getEditorScale() const override { return editorScale; }
    void setEditorScale(float scale) override
    {
        editorScale = juce::jlimit(0.75f, 2.0f, scale);
    }
    juce::Rectangle<int> getEmbeddedEditorBoundsHint() const override
    {
        return { 0, 0,
                 static_cast<int>(std::round(920.0f * editorScale)),
                 static_cast<int>(std::round(420.0f * editorScale)) };
    }
    juce::Component* getEmbeddedEditor() override;

    const juce::AudioBuffer<float>& getClipBuffer() const { return audioClip; }
    juce::String getLoadedFileName() const { return juce::File(filePath).getFileName(); }
    double getClipSampleRate() const { return audioClipSampleRate; }
    int64_t getLastTransportSamplePosition() const { return lastTransportSamplePosition; }
    double getLastPlaybackRate() const { return lastPlaybackRate; }
    float getLoopStart() const { return loopStart; }
    float getLoopEnd() const { return loopEnd; }
    float getLastLoopStart() const { return lastLoopStart; }
    float getLastLoopEnd() const { return lastLoopEnd; }
    float getStartPoint() const { return startPoint; }
    float getEndPoint() const { return endPoint; }

private:
    juce::AudioFormatManager formatManager;
    juce::AudioBuffer<float> audioClip;
    double audioClipSampleRate = 44100.0;
    juce::String filePath;
    float volume = 0.8f;
    float pan = 0.0f;
    float mute = 0.0f;
    float looping = 0.0f;
    float startPoint = 0.0f;
    float endPoint = 1.0f;
    float loopStart = 0.0f;
    float loopEnd = 1.0f;
    bool editorOpen = false;
    bool editorDetached = false;
    float editorScale = 1.0f;
    int64_t lastTransportSamplePosition = 0;
    double lastPlaybackRate = 1.0;
    float lastLoopStart = 0.0f;
    float lastLoopEnd = 1.0f;
    double playbackSamplePosition = 0.0;
    bool isRunning = false;
    bool lastPlayTriggerHigh = false;
    bool emittedEndTrigger = false;
    std::unique_ptr<juce::Component> editorComponent;
};

class MidiTrackModule final : public ModuleNode
{
public:
    ~MidiTrackModule() override = default;

    juce::String getTypeId() const override { return "MidiTrack"; }
    juce::String getDisplayName() const override { return "MIDI Track"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xff7c5cff); }

    std::vector<PortInfo> getInputPorts() const override
    {
        return {
            { "Play", PortKind::modulation },
            { "Speed", PortKind::modulation },
            { "Loop Start", PortKind::modulation },
            { "Loop End", PortKind::modulation }
        };
    }
    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "Audio Out", PortKind::audio },
            { "Done", PortKind::modulation }
        };
    }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        std::vector<NodeParameterSpec> specs {
            { "volume", "Volume", 0.0f, 1.5f, 0.8f },
            { "pan", "Pan", -1.0f, 1.0f, 0.0f },
            { "mute", "Mute", 0.0f, 1.0f, 0.0f },
            { "looping", "Loop", 0.0f, 1.0f, 0.0f },
            { "rootNote", "Root Note", 36.0f, 84.0f, 60.0f },
            { "gain", "MIDI Gain", 0.0f, 1.0f, 0.2f },
            { "startPoint", "Start", 0.0f, 0.99f, 0.0f },
            { "endPoint", "End", 0.01f, 1.0f, 1.0f },
            { "loopStart", "Loop Start", 0.0f, 0.99f, 0.0f },
            { "loopEnd", "Loop End", 0.01f, 1.0f, 1.0f }
        };

        for (int step = 0; step < 16; ++step)
            specs.push_back({ "step" + juce::String(step + 1), "S" + juce::String(step + 1), 0.0f, 1.0f, defaultSteps[static_cast<size_t>(step)] ? 1.0f : 0.0f });

        return specs;
    }

    void process(NodeRenderContext& context) override
    {
        auto* output = context.audioOutputs.front();
        output->clear();
        if (! context.modOutputs.empty())
            context.modOutputs[0] = 0.0f;

        const auto playTriggerHigh = ! context.modInputs.empty() && context.modInputs[0] > 0.5f;
        if (playTriggerHigh && ! lastPlayTriggerHigh)
        {
            isRunning = true;
            playbackSamplePosition = 0.0;
            currentStep = 0;
            emittedEndTrigger = false;
        }
        lastPlayTriggerHigh = playTriggerHigh;

        if (! context.isPlaying || mute > 0.5f || ! isRunning)
            return;

        const auto samplesPerBeat = context.sampleRate * 60.0 / bpm;
        const auto rateControl = context.modInputs.size() < 2 || context.modInputs[1] <= 0.0f ? 1.0 : static_cast<double>(context.modInputs[1]);
        const auto playbackRate = juce::jlimit(0.125, 4.0, rateControl);
        auto effectiveLoopStart = loopStart;
        auto effectiveLoopEnd = loopEnd;
        std::tie(startPoint, endPoint) = normaliseLoopPoints(startPoint, endPoint);
        if (context.modInputs.size() > 2)
            effectiveLoopStart = modToUnitRange(context.modInputs[2]);
        if (context.modInputs.size() > 3)
            effectiveLoopEnd = modToUnitRange(context.modInputs[3]);
        std::tie(effectiveLoopStart, effectiveLoopEnd) = normaliseLoopPoints(effectiveLoopStart, effectiveLoopEnd);
        effectiveLoopStart = juce::jlimit(startPoint, juce::jmax(startPoint, endPoint - 0.01f), effectiveLoopStart);
        effectiveLoopEnd = juce::jlimit(effectiveLoopStart + 0.01f, endPoint, effectiveLoopEnd);
        lastLoopStart = effectiveLoopStart;
        lastLoopEnd = effectiveLoopEnd;

        const auto regionStartIndex = juce::jlimit(0, static_cast<int>(steps.size()) - 1, static_cast<int>(std::floor(startPoint * static_cast<float>(steps.size()))));
        const auto regionEndIndex = juce::jlimit(regionStartIndex + 1, static_cast<int>(steps.size()), static_cast<int>(std::ceil(endPoint * static_cast<float>(steps.size()))));
        const auto loopStartIndex = juce::jlimit(regionStartIndex, juce::jmax(regionStartIndex, regionEndIndex - 1), static_cast<int>(std::floor(effectiveLoopStart * static_cast<float>(steps.size()))));
        const auto loopEndIndex = juce::jlimit(loopStartIndex + 1, regionEndIndex, static_cast<int>(std::ceil(effectiveLoopEnd * static_cast<float>(steps.size()))));
        const auto loopStepCount = juce::jmax(1, loopEndIndex - loopStartIndex);
        const auto samplesPerStep = (samplesPerBeat / 4.0) / playbackRate;
        const auto loopLengthSamples = samplesPerStep * loopStepCount;
        const auto leftGain = volume * juce::jlimit(0.0f, 1.0f, 1.0f - juce::jmax(0.0f, pan));
        const auto rightGain = volume * juce::jlimit(0.0f, 1.0f, 1.0f + juce::jmin(0.0f, pan));
        lastTransportSamplePosition = static_cast<int64_t>(std::llround(playbackSamplePosition));
        lastPlaybackRate = playbackRate;
        auto reachedEndThisBlock = false;

        for (int sample = 0; sample < context.numSamples; ++sample)
        {
            if (! isRunning)
                break;

            auto localPlaybackPosition = playbackSamplePosition;

            if (looping > 0.5f)
            {
                while (localPlaybackPosition >= loopLengthSamples)
                {
                    localPlaybackPosition -= loopLengthSamples;
                    playbackSamplePosition -= loopLengthSamples;
                    reachedEndThisBlock = true;
                }
            }
            else if (localPlaybackPosition >= loopLengthSamples)
            {
                isRunning = false;
                reachedEndThisBlock = true;
                break;
            }

            const auto loopStep = juce::jlimit(0, loopStepCount - 1, static_cast<int>(localPlaybackPosition / samplesPerStep));
            const auto step = loopStartIndex + loopStep;
            const auto positionWithinStep = std::fmod(localPlaybackPosition, samplesPerStep) / samplesPerStep;
            currentStep = step;

            if (! steps[static_cast<size_t>(step)] || positionWithinStep > 0.78)
                continue;

            const auto note = rootNote + noteOffsets[static_cast<size_t>(step)];
            const auto frequency = midiToFrequency(note);
            const auto phaseDelta = juce::MathConstants<float>::twoPi * frequency / static_cast<float>(context.sampleRate);
            const auto envelope = static_cast<float>(1.0 - (positionWithinStep / 0.78));
            const auto value = std::sin(phase) * gain * envelope;

            output->addSample(0, sample, value * leftGain);
            if (output->getNumChannels() > 1)
                output->addSample(1, sample, value * rightGain);

            phase += phaseDelta;
            if (phase >= juce::MathConstants<float>::twoPi)
                phase -= juce::MathConstants<float>::twoPi;

            playbackSamplePosition += 1.0;
        }

        if (reachedEndThisBlock && ! emittedEndTrigger && ! context.modOutputs.empty())
            context.modOutputs[0] = 1.0f;

        emittedEndTrigger = reachedEndThisBlock && looping <= 0.5f;
        if (looping > 0.5f)
            emittedEndTrigger = false;
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "volume") return volume;
        if (parameterId == "pan") return pan;
        if (parameterId == "mute") return mute;
        if (parameterId == "looping") return looping;
        if (parameterId == "rootNote") return static_cast<float>(rootNote);
        if (parameterId == "gain") return gain;
        if (parameterId == "startPoint") return startPoint;
        if (parameterId == "endPoint") return endPoint;
        if (parameterId == "loopStart") return loopStart;
        if (parameterId == "loopEnd") return loopEnd;
        if (parameterId.startsWith("step"))
        {
            const auto index = parameterId.fromFirstOccurrenceOf("step", false, false).getIntValue() - 1;
            if (juce::isPositiveAndBelow(index, static_cast<int>(steps.size())))
                return steps[static_cast<size_t>(index)] ? 1.0f : 0.0f;
        }
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "volume") volume = juce::jlimit(0.0f, 1.5f, newValue);
        else if (parameterId == "pan") pan = juce::jlimit(-1.0f, 1.0f, newValue);
        else if (parameterId == "mute") mute = juce::jlimit(0.0f, 1.0f, newValue);
        else if (parameterId == "looping") looping = newValue > 0.5f ? 1.0f : 0.0f;
        else if (parameterId == "rootNote") rootNote = juce::jlimit(36, 84, static_cast<int>(std::round(newValue)));
        else if (parameterId == "gain") gain = juce::jlimit(0.0f, 1.0f, newValue);
        else if (parameterId == "startPoint") startPoint = juce::jlimit(0.0f, 0.99f, newValue);
        else if (parameterId == "endPoint") endPoint = juce::jlimit(0.01f, 1.0f, newValue);
        else if (parameterId == "loopStart") loopStart = juce::jlimit(0.0f, 0.99f, newValue);
        else if (parameterId == "loopEnd") loopEnd = juce::jlimit(0.01f, 1.0f, newValue);
        else if (parameterId.startsWith("step"))
        {
            const auto index = parameterId.fromFirstOccurrenceOf("step", false, false).getIntValue() - 1;
            if (juce::isPositiveAndBelow(index, static_cast<int>(steps.size())))
                steps[static_cast<size_t>(index)] = newValue > 0.5f;
        }

        std::tie(startPoint, endPoint) = normaliseLoopPoints(startPoint, endPoint);
        std::tie(loopStart, loopEnd) = normaliseLoopPoints(loopStart, loopEnd);
    }

    bool isTrackModule() const override { return true; }
    juce::String getTrackTypeId() const override { return "midi"; }

    juce::ValueTree saveState() const override
    {
        auto state = ModuleNode::saveState();
        for (int index = 0; index < static_cast<int>(noteOffsets.size()); ++index)
            state.setProperty("noteOffset" + juce::String(index + 1), noteOffsets[static_cast<size_t>(index)], nullptr);
        state.setProperty("editorOpen", editorOpen, nullptr);
        state.setProperty("editorDetached", editorDetached, nullptr);
        state.setProperty("editorScale", editorScale, nullptr);
        return state;
    }

    void loadState(const juce::ValueTree& state) override
    {
        ModuleNode::loadState(state);
        for (int index = 0; index < static_cast<int>(noteOffsets.size()); ++index)
            noteOffsets[static_cast<size_t>(index)] = static_cast<int>(state.getProperty("noteOffset" + juce::String(index + 1), defaultNoteOffsets[static_cast<size_t>(index)]));
        editorOpen = static_cast<bool>(state.getProperty("editorOpen", false));
        editorDetached = static_cast<bool>(state.getProperty("editorDetached", false));
        editorScale = static_cast<float>(state.getProperty("editorScale", 1.0f));
    }

    bool supportsEmbeddedEditor() const override { return true; }
    bool isEditorOpen() const override { return editorOpen; }
    void setEditorOpen(bool shouldBeOpen) override
    {
        editorOpen = shouldBeOpen;
        if (! editorOpen)
            editorDetached = false;
    }
    bool isEditorDetached() const override { return editorDetached; }
    void setEditorDetached(bool shouldBeDetached) override
    {
        editorDetached = shouldBeDetached && editorOpen;
    }
    float getEditorScale() const override { return editorScale; }
    void setEditorScale(float scale) override
    {
        editorScale = juce::jlimit(0.75f, 2.0f, scale);
    }
    juce::Rectangle<int> getEmbeddedEditorBoundsHint() const override
    {
        return { 0, 0,
                 static_cast<int>(std::round(960.0f * editorScale)),
                 static_cast<int>(std::round(520.0f * editorScale)) };
    }
    juce::Component* getEmbeddedEditor() override;

    int getRootNote() const { return rootNote; }
    int getCurrentStep() const { return currentStep; }
    int64_t getLastTransportSamplePosition() const { return lastTransportSamplePosition; }
    double getLastPlaybackRate() const { return lastPlaybackRate; }
    int getStepCount() const { return static_cast<int>(steps.size()); }
    bool isStepActive(int index) const { return juce::isPositiveAndBelow(index, getStepCount()) ? steps[static_cast<size_t>(index)] : false; }
    int getStepNoteOffset(int index) const { return juce::isPositiveAndBelow(index, getStepCount()) ? noteOffsets[static_cast<size_t>(index)] : 0; }
    float getStartPoint() const { return startPoint; }
    float getEndPoint() const { return endPoint; }
    float getLoopStart() const { return loopStart; }
    float getLoopEnd() const { return loopEnd; }
    float getLastLoopStart() const { return lastLoopStart; }
    float getLastLoopEnd() const { return lastLoopEnd; }
    void setStepState(int index, bool active, int noteOffset)
    {
        if (! juce::isPositiveAndBelow(index, getStepCount()))
            return;

        steps[static_cast<size_t>(index)] = active;
        noteOffsets[static_cast<size_t>(index)] = juce::jlimit(-24, 24, noteOffset);
    }

private:
    std::array<bool, 16> steps { true, false, false, true, false, true, false, false,
                                 true, false, true, false, false, true, false, false };
    std::array<int, 16> noteOffsets { 0, 2, 4, 7, 9, 7, 4, 2,
                                      0, 2, 4, 7, 9, 7, 4, 2 };
    static constexpr std::array<bool, 16> defaultSteps { true, false, false, true, false, true, false, false,
                                                         true, false, true, false, false, true, false, false };
    static constexpr std::array<int, 16> defaultNoteOffsets { 0, 2, 4, 7, 9, 7, 4, 2,
                                                              0, 2, 4, 7, 9, 7, 4, 2 };
    float phase = 0.0f;
    float volume = 0.8f;
    float pan = 0.0f;
    float mute = 0.0f;
    float looping = 0.0f;
    int rootNote = 60;
    float gain = 0.2f;
    float startPoint = 0.0f;
    float endPoint = 1.0f;
    float loopStart = 0.0f;
    float loopEnd = 1.0f;
    int currentStep = 0;
    bool editorOpen = false;
    bool editorDetached = false;
    float editorScale = 1.0f;
    int64_t lastTransportSamplePosition = 0;
    double lastPlaybackRate = 1.0;
    float lastLoopStart = 0.0f;
    float lastLoopEnd = 1.0f;
    double playbackSamplePosition = 0.0;
    bool isRunning = false;
    bool lastPlayTriggerHigh = false;
    bool emittedEndTrigger = false;
    std::unique_ptr<juce::Component> editorComponent;
};

class AudioTrackEditorComponent final : public juce::Component,
                                        private juce::Timer
{
public:
    explicit AudioTrackEditorComponent(AudioTrackModule& owner) : module(owner)
    {
        addAndMakeVisible(titleLabel);
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        titleLabel.setFont(juce::FontOptions(15.5f, juce::Font::bold));
        titleLabel.setJustificationType(juce::Justification::centredLeft);

        addAndMakeVisible(infoLabel);
        infoLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8894a5));
        infoLabel.setJustificationType(juce::Justification::centredLeft);

        addAndMakeVisible(zoomLabel);
        zoomLabel.setText("Zoom", juce::dontSendNotification);
        zoomLabel.setColour(juce::Label::textColourId, juce::Colour(0xffdbe3ec));
        addAndMakeVisible(zoomSlider);
        zoomSlider.setRange(1.0, 24.0, 0.1);
        zoomSlider.setValue(horizontalZoom, juce::dontSendNotification);
        zoomSlider.onValueChange = [this] { horizontalZoom = static_cast<float>(zoomSlider.getValue()); repaint(); };
        startTimerHz(20);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff111722));

        auto content = getLocalBounds().reduced(16).withTrimmedTop(58);
        g.setColour(juce::Colour(0xff1a2230));
        g.fillRoundedRectangle(content.toFloat(), 12.0f);

        const auto& clip = module.getClipBuffer();
        if (clip.getNumSamples() == 0)
        {
            g.setColour(juce::Colour(0xff8fa1b5));
            g.drawText("Load an audio file onto the track to edit it here.", content, juce::Justification::centred);
            return;
        }

        auto waveformArea = content.reduced(12);
        const auto midY = waveformArea.getCentreY();
        const auto waveformWidth = static_cast<float>(waveformArea.getWidth());
        const auto samplesPerPixel = juce::jmax(1.0f, static_cast<float>(clip.getNumSamples()) / (waveformWidth * horizontalZoom));
        const auto startX = waveformArea.getX() + static_cast<int>(std::round(module.getStartPoint() * waveformWidth));
        const auto endX = waveformArea.getX() + static_cast<int>(std::round(module.getEndPoint() * waveformWidth));
        const auto loopStartX = waveformArea.getX() + static_cast<int>(std::round(module.getLoopStart() * waveformWidth));
        const auto loopEndX = waveformArea.getX() + static_cast<int>(std::round(module.getLoopEnd() * waveformWidth));

        g.setColour(juce::Colour(0xff2b3647));
        for (int line = 0; line < 5; ++line)
        {
            const auto y = waveformArea.getY() + (waveformArea.getHeight() * line) / 4;
            g.drawHorizontalLine(y, static_cast<float>(waveformArea.getX()), static_cast<float>(waveformArea.getRight()));
        }

        juce::Path waveform;
        waveform.startNewSubPath(static_cast<float>(waveformArea.getX()), static_cast<float>(midY));

        for (int x = 0; x < waveformArea.getWidth(); ++x)
        {
            const auto xPosition = static_cast<float>(x);
            const auto startSample = static_cast<int>(std::floor(xPosition * samplesPerPixel));
            const auto endSample = juce::jmin(clip.getNumSamples(), static_cast<int>(std::ceil((xPosition + 1.0f) * samplesPerPixel)));
            float peak = 0.0f;

            for (int sample = startSample; sample < endSample; ++sample)
                peak = juce::jmax(peak, std::abs(clip.getSample(0, sample)));

            const auto y = juce::jmap(peak, 0.0f, 1.0f, static_cast<float>(midY), static_cast<float>(waveformArea.getY()));
            waveform.lineTo(static_cast<float>(waveformArea.getX() + x), y);
        }

        for (int x = waveformArea.getWidth() - 1; x >= 0; --x)
        {
            const auto xPosition = static_cast<float>(x);
            const auto startSample = static_cast<int>(std::floor(xPosition * samplesPerPixel));
            const auto endSample = juce::jmin(clip.getNumSamples(), static_cast<int>(std::ceil((xPosition + 1.0f) * samplesPerPixel)));
            float peak = 0.0f;

            for (int sample = startSample; sample < endSample; ++sample)
                peak = juce::jmax(peak, std::abs(clip.getSample(0, sample)));

            const auto y = juce::jmap(peak, 0.0f, 1.0f, static_cast<float>(midY), static_cast<float>(waveformArea.getBottom()));
            waveform.lineTo(static_cast<float>(waveformArea.getX() + x), y);
        }

        waveform.closeSubPath();
        g.setColour(juce::Colour(0xff62c6ff).withAlpha(0.9f));
        g.fillPath(waveform);

        g.setColour(juce::Colour(0x22ffffff));
        g.fillRect(juce::Rectangle<int>(startX, waveformArea.getY(), juce::jmax(1, endX - startX), waveformArea.getHeight()));
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.drawLine(static_cast<float>(startX), static_cast<float>(waveformArea.getY()),
                   static_cast<float>(startX), static_cast<float>(waveformArea.getBottom()), 2.0f);
        g.drawLine(static_cast<float>(endX), static_cast<float>(waveformArea.getY()),
                   static_cast<float>(endX), static_cast<float>(waveformArea.getBottom()), 2.0f);

        g.setColour(juce::Colour(0x33ffd166));
        g.fillRect(juce::Rectangle<int>(loopStartX, waveformArea.getY(), juce::jmax(1, loopEndX - loopStartX), waveformArea.getHeight()));
        g.setColour(juce::Colour(0xffffd166));
        g.drawLine(static_cast<float>(loopStartX), static_cast<float>(waveformArea.getY()),
                   static_cast<float>(loopStartX), static_cast<float>(waveformArea.getBottom()), 2.0f);
        g.drawLine(static_cast<float>(loopEndX), static_cast<float>(waveformArea.getY()),
                   static_cast<float>(loopEndX), static_cast<float>(waveformArea.getBottom()), 2.0f);

        const auto playheadPosition = static_cast<double>(module.getLastTransportSamplePosition()) * module.getLastPlaybackRate();
        const auto playheadNormalised = std::fmod(playheadPosition, static_cast<double>(clip.getNumSamples()))
                                     / static_cast<double>(clip.getNumSamples());
        const auto playheadX = waveformArea.getX() + static_cast<int>(std::round(playheadNormalised * static_cast<double>(waveformArea.getWidth())));
        g.setColour(juce::Colour(0xffff6b6b));
        g.drawLine(static_cast<float>(playheadX), static_cast<float>(waveformArea.getY()),
                   static_cast<float>(playheadX), static_cast<float>(waveformArea.getBottom()), 2.0f);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(16);
        auto header = bounds.removeFromTop(58);
        titleLabel.setBounds(header.removeFromTop(24));
        infoLabel.setBounds(header.removeFromTop(20));
        auto zoomRow = header.removeFromTop(26);
        zoomLabel.setBounds(zoomRow.removeFromLeft(110));
        zoomSlider.setBounds(zoomRow.reduced(4, 0));
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        auto waveformArea = getLocalBounds().reduced(16).withTrimmedTop(58).reduced(12);
        if (! waveformArea.contains(event.getPosition()))
            return;

        const auto waveformWidth = static_cast<float>(waveformArea.getWidth());
        const auto startX = waveformArea.getX() + static_cast<int>(std::round(module.getStartPoint() * waveformWidth));
        const auto endX = waveformArea.getX() + static_cast<int>(std::round(module.getEndPoint() * waveformWidth));
        const auto loopStartX = waveformArea.getX() + static_cast<int>(std::round(module.getLoopStart() * waveformWidth));
        const auto loopEndX = waveformArea.getX() + static_cast<int>(std::round(module.getLoopEnd() * waveformWidth));

        if (std::abs(event.x - startX) < 10)
            dragTarget = DragTarget::regionStart;
        else if (std::abs(event.x - endX) < 10)
            dragTarget = DragTarget::regionEnd;
        else if (std::abs(event.x - loopStartX) < 10)
            dragTarget = DragTarget::loopStart;
        else if (std::abs(event.x - loopEndX) < 10)
            dragTarget = DragTarget::loopEnd;
        else
        {
            dragTarget = DragTarget::waveformPaint;
            paintWaveformAt(event.getPosition(), waveformArea);
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragTarget == DragTarget::none)
            return;

        auto waveformArea = getLocalBounds().reduced(16).withTrimmedTop(58).reduced(12);
        const auto normalised = juce::jlimit(0.0f, 1.0f, static_cast<float>(event.x - waveformArea.getX()) / static_cast<float>(juce::jmax(1, waveformArea.getWidth())));

        if (dragTarget == DragTarget::regionStart)
            module.setParameterValue("startPoint", normalised);
        else if (dragTarget == DragTarget::regionEnd)
            module.setParameterValue("endPoint", normalised);
        else if (dragTarget == DragTarget::loopStart)
            module.setParameterValue("loopStart", normalised);
        else if (dragTarget == DragTarget::loopEnd)
            module.setParameterValue("loopEnd", normalised);
        else if (dragTarget == DragTarget::waveformPaint)
            paintWaveformAt(event.getPosition(), waveformArea);

        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragTarget = DragTarget::none;
    }

private:
    enum class DragTarget
    {
        none,
        regionStart,
        regionEnd,
        loopStart,
        loopEnd,
        waveformPaint
    };

    void paintWaveformAt(juce::Point<int> position, juce::Rectangle<int> waveformArea)
    {
        const auto normalisedX = juce::jlimit(0.0f, 1.0f,
                                              static_cast<float>(position.x - waveformArea.getX()) / static_cast<float>(juce::jmax(1, waveformArea.getWidth())));
        const auto normalisedY = juce::jlimit(0.0f, 1.0f,
                                              static_cast<float>(position.y - waveformArea.getY()) / static_cast<float>(juce::jmax(1, waveformArea.getHeight())));
        module.paintWaveform(normalisedX, normalisedY, 0.0025f / juce::jmax(1.0f, horizontalZoom));
    }

    void timerCallback() override
    {
        titleLabel.setText(module.getLoadedFileName().isNotEmpty() ? module.getLoadedFileName() : "Audio", juce::dontSendNotification);
        infoLabel.setText("Drag markers. Paint the waveform.", juce::dontSendNotification);
        repaint();
    }

    AudioTrackModule& module;
    juce::Label titleLabel;
    juce::Label infoLabel;
    juce::Label zoomLabel;
    juce::Slider zoomSlider;
    float horizontalZoom = 1.0f;
    DragTarget dragTarget = DragTarget::none;
};

class MidiTrackEditorComponent final : public juce::Component,
                                       private juce::Timer
{
public:
    explicit MidiTrackEditorComponent(MidiTrackModule& owner) : module(owner)
    {
        addAndMakeVisible(titleLabel);
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        titleLabel.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        titleLabel.setJustificationType(juce::Justification::centredLeft);

        addAndMakeVisible(infoLabel);
        infoLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9fb0c4));
        infoLabel.setJustificationType(juce::Justification::centredLeft);
        startTimerHz(20);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff111722));

        auto bounds = getLocalBounds().reduced(16);
        auto header = bounds.removeFromTop(52);
        juce::ignoreUnused(header);

        const auto keyWidth = 76;
        auto keyboardArea = bounds.removeFromLeft(keyWidth);
        auto rulerArea = bounds.removeFromTop(20);
        auto gridArea = bounds;
        const auto rows = 24;
        const auto columns = module.getStepCount();
        const auto rowHeight = juce::jmax(12, gridArea.getHeight() / rows);
        const auto columnWidth = juce::jmax(18, gridArea.getWidth() / columns);
        const auto gridWidth = static_cast<float>(gridArea.getWidth());
        const auto startX = gridArea.getX() + static_cast<int>(std::round(module.getStartPoint() * gridWidth));
        const auto endX = gridArea.getX() + static_cast<int>(std::round(module.getEndPoint() * gridWidth));
        const auto loopStartX = gridArea.getX() + static_cast<int>(std::round(module.getLoopStart() * gridWidth));
        const auto loopEndX = gridArea.getX() + static_cast<int>(std::round(module.getLoopEnd() * gridWidth));

        g.setColour(juce::Colour(0xff1a2230));
        g.fillRect(rulerArea);
        g.setColour(juce::Colour(0x22ffffff));
        g.fillRect(juce::Rectangle<int>(startX, rulerArea.getY(), juce::jmax(1, endX - startX), rulerArea.getHeight()));
        g.setColour(juce::Colour(0x33ffd166));
        g.fillRect(juce::Rectangle<int>(loopStartX, rulerArea.getY(), juce::jmax(1, loopEndX - loopStartX), rulerArea.getHeight()));
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.drawLine(static_cast<float>(startX), static_cast<float>(rulerArea.getY()),
                   static_cast<float>(startX), static_cast<float>(gridArea.getBottom()), 2.0f);
        g.drawLine(static_cast<float>(endX), static_cast<float>(rulerArea.getY()),
                   static_cast<float>(endX), static_cast<float>(gridArea.getBottom()), 2.0f);
        g.setColour(juce::Colour(0xffffd166));
        g.drawLine(static_cast<float>(loopStartX), static_cast<float>(rulerArea.getY()),
                   static_cast<float>(loopStartX), static_cast<float>(gridArea.getBottom()), 2.0f);
        g.drawLine(static_cast<float>(loopEndX), static_cast<float>(rulerArea.getY()),
                   static_cast<float>(loopEndX), static_cast<float>(gridArea.getBottom()), 2.0f);

        for (int row = 0; row < rows; ++row)
        {
            const auto midiNote = module.getRootNote() + (rows - row - 1) - 12;
            const auto black = juce::MidiMessage::isMidiNoteBlack(midiNote);
            auto rowArea = juce::Rectangle<int>(keyboardArea.getX(), gridArea.getY() + row * rowHeight, keyboardArea.getWidth(), rowHeight);
            g.setColour(black ? juce::Colour(0xff202a38) : juce::Colour(0xffdbe3ec));
            g.fillRect(rowArea);
            g.setColour(black ? juce::Colour(0xff6f8194) : juce::Colour(0xff49586c));
            g.drawRect(rowArea);
            g.drawText(juce::MidiMessage::getMidiNoteName(midiNote, true, true, 3),
                       rowArea.reduced(8, 0), juce::Justification::centredLeft);
        }

        for (int column = 0; column < columns; ++column)
        {
            for (int row = 0; row < rows; ++row)
            {
                const auto midiNote = module.getRootNote() + (rows - row - 1) - 12;
                auto cell = juce::Rectangle<int>(gridArea.getX() + column * columnWidth,
                                                 gridArea.getY() + row * rowHeight,
                                                 columnWidth,
                                                 rowHeight);
                const auto noteOffset = midiNote - module.getRootNote();
                const auto active = module.isStepActive(column) && module.getStepNoteOffset(column) == noteOffset;
                const auto currentStep = module.getCurrentStep() == column;

                g.setColour(currentStep ? juce::Colour(0x2238bdf8) : juce::Colour(0xff151d29));
                g.fillRect(cell);

                if (active)
                {
                    g.setColour(juce::Colour(0xff6c5ce7));
                    g.fillRoundedRectangle(cell.reduced(2).toFloat(), 5.0f);
                }

                g.setColour(juce::Colour(0xff2b3647));
                g.drawRect(cell);
            }
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(16);
        titleLabel.setBounds(bounds.removeFromTop(24));
        infoLabel.setBounds(bounds.removeFromTop(20));
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        auto area = getLocalBounds().reduced(16).withTrimmedTop(52);
        auto rulerArea = area.removeFromTop(20);
        auto gridArea = area;
        const auto rows = 24;
        const auto columns = module.getStepCount();

        if (rulerArea.contains(event.getPosition()))
        {
            const auto gridWidth = static_cast<float>(gridArea.getWidth());
            const auto startX = gridArea.getX() + static_cast<int>(std::round(module.getStartPoint() * gridWidth));
            const auto endX = gridArea.getX() + static_cast<int>(std::round(module.getEndPoint() * gridWidth));
            const auto loopStartX = gridArea.getX() + static_cast<int>(std::round(module.getLoopStart() * gridWidth));
            const auto loopEndX = gridArea.getX() + static_cast<int>(std::round(module.getLoopEnd() * gridWidth));
            if (std::abs(event.x - startX) < 10)
                dragTarget = DragTarget::regionStart;
            else if (std::abs(event.x - endX) < 10)
                dragTarget = DragTarget::regionEnd;
            else if (std::abs(event.x - loopStartX) < 10)
                dragTarget = DragTarget::loopStart;
            else if (std::abs(event.x - loopEndX) < 10)
                dragTarget = DragTarget::loopEnd;
            return;
        }

        if (! gridArea.contains(event.getPosition()))
            return;

        const auto columnWidth = juce::jmax(18, gridArea.getWidth() / columns);
        const auto rowHeight = juce::jmax(12, gridArea.getHeight() / rows);
        paintNoteAt(event.getPosition(), gridArea, columns, rows, columnWidth, rowHeight, event.mods.isRightButtonDown());
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        auto area = getLocalBounds().reduced(16).withTrimmedTop(52);
        area.removeFromTop(20);
        auto gridArea = area;
        const auto rows = 24;
        const auto columns = module.getStepCount();
        const auto columnWidth = juce::jmax(18, gridArea.getWidth() / columns);
        const auto rowHeight = juce::jmax(12, gridArea.getHeight() / rows);

        if (dragTarget == DragTarget::regionStart
            || dragTarget == DragTarget::regionEnd
            || dragTarget == DragTarget::loopStart
            || dragTarget == DragTarget::loopEnd)
        {
            const auto normalised = juce::jlimit(0.0f, 1.0f, static_cast<float>(event.x - gridArea.getX()) / static_cast<float>(juce::jmax(1, gridArea.getWidth())));
            if (dragTarget == DragTarget::regionStart)
                module.setParameterValue("startPoint", normalised);
            else if (dragTarget == DragTarget::regionEnd)
                module.setParameterValue("endPoint", normalised);
            else if (dragTarget == DragTarget::loopStart)
                module.setParameterValue("loopStart", normalised);
            else
                module.setParameterValue("loopEnd", normalised);
            repaint();
            return;
        }

        if (gridArea.contains(event.getPosition()))
            paintNoteAt(event.getPosition(), gridArea, columns, rows, columnWidth, rowHeight, event.mods.isRightButtonDown());
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragTarget = DragTarget::none;
    }

private:
    enum class DragTarget
    {
        none,
        regionStart,
        regionEnd,
        loopStart,
        loopEnd
    };

    void paintNoteAt(juce::Point<int> position,
                     juce::Rectangle<int> gridArea,
                     int columns,
                     int rows,
                     int columnWidth,
                     int rowHeight,
                     bool erase)
    {
        const auto column = juce::jlimit(0, columns - 1, (position.x - gridArea.getX()) / columnWidth);
        const auto row = juce::jlimit(0, rows - 1, (position.y - gridArea.getY()) / rowHeight);
        const auto midiNote = module.getRootNote() + (rows - row - 1) - 12;
        const auto noteOffset = midiNote - module.getRootNote();
        module.setStepState(column, ! erase, noteOffset);
        repaint();
    }

    void timerCallback() override
    {
        titleLabel.setText("Piano Roll", juce::dontSendNotification);
        infoLabel.setText("Logic-style piano roll with draggable start/end, loop points, and note painting.", juce::dontSendNotification);
        repaint();
    }

    MidiTrackModule& module;
    juce::Label titleLabel;
    juce::Label infoLabel;
    DragTarget dragTarget = DragTarget::none;
};

juce::Component* AudioTrackModule::getEmbeddedEditor()
{
    if (! editorOpen)
        return nullptr;

    if (editorComponent == nullptr)
        editorComponent = std::make_unique<AudioTrackEditorComponent>(*this);

    return editorComponent.get();
}

juce::Component* MidiTrackModule::getEmbeddedEditor()
{
    if (! editorOpen)
        return nullptr;

    if (editorComponent == nullptr)
        editorComponent = std::make_unique<MidiTrackEditorComponent>(*this);

    return editorComponent.get();
}
} // namespace

std::unique_ptr<ModuleNode> createOscillatorModule()
{
    return std::make_unique<OscillatorModule>();
}

std::unique_ptr<ModuleNode> createLfoModule()
{
    return std::make_unique<LfoModule>();
}

std::unique_ptr<ModuleNode> createBpmToLfoModule()
{
    return std::make_unique<BpmToLfoModule>();
}

std::unique_ptr<ModuleNode> createTimeSignatureModule()
{
    return std::make_unique<TimeSignatureModule>();
}

std::unique_ptr<ModuleNode> createAdEnvelopeModule()
{
    return std::make_unique<AdEnvelopeModule>();
}

std::unique_ptr<ModuleNode> createAdsrEnvelopeModule()
{
    return std::make_unique<AdsrEnvelopeModule>();
}

std::unique_ptr<ModuleNode> createFilterModule()
{
    return std::make_unique<FilterModule>();
}

std::unique_ptr<ModuleNode> createGainModule()
{
    return std::make_unique<GainModule>();
}

std::unique_ptr<ModuleNode> createAddModule()
{
    return std::make_unique<AddModule>();
}

std::unique_ptr<ModuleNode> createSubtractModule()
{
    return std::make_unique<SubtractModule>();
}

std::unique_ptr<ModuleNode> createMultiplyModule()
{
    return std::make_unique<MultiplyModule>();
}

std::unique_ptr<ModuleNode> createDivideModule()
{
    return std::make_unique<DivideModule>();
}

std::unique_ptr<ModuleNode> createOutputModule()
{
    return std::make_unique<OutputModule>();
}

std::unique_ptr<ModuleNode> createSumModule()
{
    return std::make_unique<SumModule>();
}

std::unique_ptr<ModuleNode> createRouterModule()
{
    return std::make_unique<RouterModule>();
}

std::unique_ptr<ModuleNode> createAudioTrackModule()
{
    return std::make_unique<AudioTrackModule>();
}

std::unique_ptr<ModuleNode> createMidiTrackModule()
{
    return std::make_unique<MidiTrackModule>();
}
