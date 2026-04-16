#include "ExampleModules.h"
#include "ModuleCores.h"

namespace
{
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

float computePeakLevel(const juce::AudioBuffer<float>& buffer, int channel)
{
    if (! juce::isPositiveAndBelow(channel, buffer.getNumChannels()) || buffer.getNumSamples() <= 0)
        return 0.0f;

    return buffer.getMagnitude(channel, 0, buffer.getNumSamples());
}

void updateStereoMeter(const juce::AudioBuffer<float>& buffer, juce::Point<float>& meter)
{
    const auto left = computePeakLevel(buffer, 0);
    const auto right = computePeakLevel(buffer, juce::jmin(1, buffer.getNumChannels() - 1));
    meter.x = juce::jmax(left, meter.x * 0.84f);
    meter.y = juce::jmax(right, meter.y * 0.84f);
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

struct AudioArrangementClip
{
    juce::String id;
    juce::String label;
    juce::String filePath;
    juce::AudioBuffer<float> audioData;
    double sampleRate = 44100.0;
    float startBar = 1.0f;
    float lengthBars = 4.0f;
    float sourceStart = 0.0f;
    float sourceEnd = 1.0f;
    float loopStart = 0.0f;
    float loopEnd = 1.0f;
    float fadeIn = 0.0f;
    float fadeOut = 0.0f;
};

struct MidiNoteEvent
{
    juce::String id;
    float startBeat = 0.0f;
    float lengthBeats = 1.0f;
    int noteNumber = 60;
    float velocity = 0.8f;
};

struct MidiArrangementClip
{
    juce::String id;
    juce::String label;
    float startBar = 1.0f;
    float lengthBars = 4.0f;
    float startPoint = 0.0f;
    float endPoint = 1.0f;
    float loopStart = 0.0f;
    float loopEnd = 1.0f;
    std::vector<MidiNoteEvent> notes;
};

double samplesPerBeat(const NodeRenderContext& context)
{
    return context.sampleRate * 60.0 / juce::jmax(1.0, context.bpm);
}

double beatsPerBar(const NodeRenderContext& context)
{
    return static_cast<double>(context.transportNumerator) * (4.0 / static_cast<double>(juce::jmax(1, context.transportDenominator)));
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

class MetronomeModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override { return "Metronome"; }
    juce::String getDisplayName() const override { return "Metronome"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xfffb7185); }

    std::vector<PortInfo> getInputPorts() const override
    {
        return {
            { "Rate In", PortKind::modulation }
        };
    }

    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "Trigger", PortKind::modulation },
            { "Gate", PortKind::modulation },
            { "Phase", PortKind::modulation }
        };
    }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "rateHz", "Rate", 0.05f, 32.0f, 1.0f },
            { "pulseWidth", "Pulse Width", 0.01f, 0.5f, 0.08f }
        };
    }

    void prepare(double newSampleRate, int) override
    {
        sampleRate = juce::jmax(1.0, newSampleRate);
        phase = 0.0;
        gateCounterSamples = 0;
    }

    void process(NodeRenderContext& context) override
    {
        auto trigger = 0.0f;
        const auto rateIn = ! context.modInputs.empty() ? static_cast<double>(context.modInputs[0]) : 0.0;
        const auto effectiveRate = juce::jlimit(0.05, 32.0, rateIn > 0.0 ? rateIn : static_cast<double>(rateHz));
        const auto phaseAdvance = effectiveRate / juce::jmax(1.0, context.sampleRate);
        const auto pulseSamples = juce::jmax(1, static_cast<int>(std::round((1.0 / effectiveRate) * static_cast<double>(context.sampleRate) * pulseWidth)));

        for (int sample = 0; sample < context.numSamples; ++sample)
        {
            phase += phaseAdvance;

            if (phase >= 1.0)
            {
                phase -= std::floor(phase);
                trigger = 1.0f;
                gateCounterSamples = pulseSamples;
            }
        }

        if (gateCounterSamples > 0)
            gateCounterSamples = juce::jmax(0, gateCounterSamples - context.numSamples);

        if (context.modOutputs.size() > 0)
            context.modOutputs[0] = trigger;
        if (context.modOutputs.size() > 1)
            context.modOutputs[1] = gateCounterSamples > 0 ? 1.0f : 0.0f;
        if (context.modOutputs.size() > 2)
            context.modOutputs[2] = static_cast<float>(phase);
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "rateHz")
            return rateHz;
        if (parameterId == "pulseWidth")
            return pulseWidth;
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "rateHz")
            rateHz = juce::jlimit(0.05f, 32.0f, newValue);
        else if (parameterId == "pulseWidth")
            pulseWidth = juce::jlimit(0.01f, 0.5f, newValue);
    }

private:
    double sampleRate = 44100.0;
    double phase = 0.0;
    int gateCounterSamples = 0;
    float rateHz = 1.0f;
    float pulseWidth = 0.08f;
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

class ComparatorModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override { return "Comparator"; }
    juce::String getDisplayName() const override { return "Comparator"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xffa78bfa); }

    std::vector<PortInfo> getInputPorts() const override
    {
        return {
            { "Signal", PortKind::modulation },
            { "Threshold In", PortKind::modulation }
        };
    }

    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "Gate", PortKind::modulation },
            { "Trigger", PortKind::modulation }
        };
    }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "threshold", "Threshold", -1.0f, 1.0f, 0.0f }
        };
    }

    void process(NodeRenderContext& context) override
    {
        const auto signal = ! context.modInputs.empty() ? context.modInputs[0] : 0.0f;
        const auto thresholdIn = context.modInputs.size() > 1 ? context.modInputs[1] : threshold;
        const auto currentHigh = signal >= thresholdIn;
        const auto trigger = currentHigh && ! lastHigh ? 1.0f : 0.0f;

        if (context.modOutputs.size() > 0)
            context.modOutputs[0] = currentHigh ? 1.0f : 0.0f;
        if (context.modOutputs.size() > 1)
            context.modOutputs[1] = trigger;

        lastHigh = currentHigh;
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        return parameterId == "threshold" ? threshold : 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "threshold")
            threshold = juce::jlimit(-1.0f, 1.0f, newValue);
    }

private:
    float threshold = 0.0f;
    bool lastHigh = false;
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

class ChannelStripModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override { return "ChannelStrip"; }
    juce::String getDisplayName() const override { return "Channel Strip"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xff4f46e5); }

    std::vector<PortInfo> getInputPorts() const override
    {
        return {
            { "Audio In", PortKind::audio },
            { "Gain", PortKind::modulation },
            { "Pan", PortKind::modulation },
            { "Send", PortKind::modulation }
        };
    }

    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "Main Out", PortKind::audio },
            { "Send Out", PortKind::audio }
        };
    }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "gain", "Gain", 0.0f, 2.0f, 1.0f },
            { "pan", "Pan", -1.0f, 1.0f, 0.0f },
            { "send", "Send", 0.0f, 1.0f, 0.0f },
            { "mute", "Mute", 0.0f, 1.0f, 0.0f }
        };
    }

    void process(NodeRenderContext& context) override
    {
        auto* input = context.audioInputs[0];
        auto* mainOut = context.audioOutputs[0];
        auto* sendOut = context.audioOutputs[1];
        mainOut->makeCopyOf(*input);
        sendOut->makeCopyOf(*input);

        const auto gainValue = juce::jlimit(0.0f, 2.0f, gain + (context.modInputs.size() > 0 ? context.modInputs[0] : 0.0f));
        const auto panValue = juce::jlimit(-1.0f, 1.0f, pan + (context.modInputs.size() > 1 ? context.modInputs[1] : 0.0f));
        const auto sendValue = juce::jlimit(0.0f, 1.0f, send + (context.modInputs.size() > 2 ? context.modInputs[2] : 0.0f));

        if (mute > 0.5f)
        {
            mainOut->clear();
            sendOut->clear();
            return;
        }

        const auto leftGain = gainValue * juce::jlimit(0.0f, 1.0f, 1.0f - juce::jmax(0.0f, panValue));
        const auto rightGain = gainValue * juce::jlimit(0.0f, 1.0f, 1.0f + juce::jmin(0.0f, panValue));
        mainOut->applyGain(0, 0, mainOut->getNumSamples(), leftGain * (1.0f - sendValue));
        if (mainOut->getNumChannels() > 1)
            mainOut->applyGain(1, 0, mainOut->getNumSamples(), rightGain * (1.0f - sendValue));
        sendOut->applyGain(gainValue * sendValue);
        updateStereoMeter(*mainOut, meterLevels);
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "gain") return gain;
        if (parameterId == "pan") return pan;
        if (parameterId == "send") return send;
        if (parameterId == "mute") return mute;
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "gain") gain = juce::jlimit(0.0f, 2.0f, newValue);
        else if (parameterId == "pan") pan = juce::jlimit(-1.0f, 1.0f, newValue);
        else if (parameterId == "send") send = juce::jlimit(0.0f, 1.0f, newValue);
        else if (parameterId == "mute") mute = newValue > 0.5f ? 1.0f : 0.0f;
    }

    juce::Point<float> getMeterLevels() const override { return meterLevels; }

private:
    float gain = 1.0f;
    float pan = 0.0f;
    float send = 0.0f;
    float mute = 0.0f;
    juce::Point<float> meterLevels;
};

class SendModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override { return "Send"; }
    juce::String getDisplayName() const override { return "Send"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xff0ea5e9); }
    std::vector<PortInfo> getInputPorts() const override
    {
        return { { "Audio In", PortKind::audio }, { "Amount", PortKind::modulation } };
    }
    std::vector<PortInfo> getOutputPorts() const override
    {
        return { { "Dry Out", PortKind::audio }, { "Send Out", PortKind::audio } };
    }
    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return { { "amount", "Amount", 0.0f, 1.0f, 0.5f } };
    }
    void process(NodeRenderContext& context) override
    {
        auto* input = context.audioInputs[0];
        auto* dryOut = context.audioOutputs[0];
        auto* sendOut = context.audioOutputs[1];
        dryOut->makeCopyOf(*input);
        sendOut->makeCopyOf(*input);
        const auto amountValue = juce::jlimit(0.0f, 1.0f, amount + (context.modInputs.empty() ? 0.0f : context.modInputs[0]));
        dryOut->applyGain(1.0f - amountValue);
        sendOut->applyGain(amountValue);
    }
    float getParameterValue(const juce::String& parameterId) const override { return parameterId == "amount" ? amount : 0.0f; }
    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "amount")
            amount = juce::jlimit(0.0f, 1.0f, newValue);
    }
private:
    float amount = 0.5f;
};

class ReturnModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override { return "Return"; }
    juce::String getDisplayName() const override { return "Return"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xff22c55e); }
    std::vector<PortInfo> getInputPorts() const override
    {
        return { { "Audio In", PortKind::audio }, { "Level", PortKind::modulation } };
    }
    std::vector<PortInfo> getOutputPorts() const override
    {
        return { { "Audio Out", PortKind::audio } };
    }
    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return { { "level", "Level", 0.0f, 2.0f, 1.0f } };
    }
    void process(NodeRenderContext& context) override
    {
        auto* input = context.audioInputs[0];
        auto* output = context.audioOutputs[0];
        output->makeCopyOf(*input);
        const auto levelValue = juce::jlimit(0.0f, 2.0f, level + (context.modInputs.empty() ? 0.0f : context.modInputs[0]));
        output->applyGain(levelValue);
        updateStereoMeter(*output, meterLevels);
    }
    float getParameterValue(const juce::String& parameterId) const override { return parameterId == "level" ? level : 0.0f; }
    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "level")
            level = juce::jlimit(0.0f, 2.0f, newValue);
    }
    juce::Point<float> getMeterLevels() const override { return meterLevels; }
private:
    float level = 1.0f;
    juce::Point<float> meterLevels;
};

class MeterModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override { return "Meter"; }
    juce::String getDisplayName() const override { return "Meter"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xff14b8a6); }
    std::vector<PortInfo> getInputPorts() const override { return { { "Audio In", PortKind::audio } }; }
    std::vector<PortInfo> getOutputPorts() const override
    {
        return { { "Audio Out", PortKind::audio }, { "Left", PortKind::modulation }, { "Right", PortKind::modulation } };
    }
    void process(NodeRenderContext& context) override
    {
        auto* input = context.audioInputs[0];
        auto* output = context.audioOutputs[0];
        output->makeCopyOf(*input);
        updateStereoMeter(*output, meterLevels);
        if (context.modOutputs.size() > 0) context.modOutputs[0] = meterLevels.x;
        if (context.modOutputs.size() > 1) context.modOutputs[1] = meterLevels.y;
    }
    juce::Point<float> getMeterLevels() const override { return meterLevels; }
private:
    juce::Point<float> meterLevels;
};

class BusModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override { return "Bus"; }
    juce::String getDisplayName() const override { return "Bus"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xfff97316); }
    std::vector<PortInfo> getInputPorts() const override
    {
        std::vector<PortInfo> ports;
        for (int index = 0; index < inputCount; ++index)
            ports.push_back({ "In " + juce::String(index + 1), PortKind::audio });
        return ports;
    }
    std::vector<PortInfo> getOutputPorts() const override
    {
        return { { "Bus Out", PortKind::audio } };
    }
    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return { { "channels", "Inputs", 2.0f, 8.0f, 4.0f }, { "trim", "Trim", 0.0f, 2.0f, 1.0f } };
    }
    void process(NodeRenderContext& context) override
    {
        auto* output = context.audioOutputs[0];
        output->clear();
        for (auto* input : context.audioInputs)
            for (int channel = 0; channel < juce::jmin(output->getNumChannels(), input->getNumChannels()); ++channel)
                output->addFrom(channel, 0, *input, channel, 0, context.numSamples);
        output->applyGain(trim);
        updateStereoMeter(*output, meterLevels);
    }
    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "channels") return static_cast<float>(inputCount);
        if (parameterId == "trim") return trim;
        return 0.0f;
    }
    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "channels") inputCount = juce::jlimit(2, 8, static_cast<int>(std::round(newValue)));
        else if (parameterId == "trim") trim = juce::jlimit(0.0f, 2.0f, newValue);
    }
    juce::Point<float> getMeterLevels() const override { return meterLevels; }
private:
    int inputCount = 4;
    float trim = 1.0f;
    juce::Point<float> meterLevels;
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
            { "trim", "Trim", 0.0f, 1.5f, 0.85f },
            { "ceiling", "Ceiling", 0.1f, 1.0f, 0.92f },
            { "limit", "Limit", 0.0f, 1.0f, 1.0f }
        };
    }

    void process(NodeRenderContext& context) override
    {
        auto* input = context.audioInputs[0];
        auto* output = context.audioOutputs[0];
        output->makeCopyOf(*input);
        core.process(*output, masterGain);

        if (limiterEnabled > 0.5f)
        {
            for (int channel = 0; channel < output->getNumChannels(); ++channel)
            {
                auto* data = output->getWritePointer(channel);
                for (int sample = 0; sample < output->getNumSamples(); ++sample)
                    data[sample] = juce::jlimit(-ceiling, ceiling, data[sample]);
            }
        }

        updateStereoMeter(*output, meterLevels);
    }

    bool contributesToMasterOutput() const override
    {
        return true;
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "trim") return masterGain;
        if (parameterId == "ceiling") return ceiling;
        if (parameterId == "limit") return limiterEnabled;
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "trim")
            masterGain = juce::jlimit(0.0f, 1.5f, newValue);
        else if (parameterId == "ceiling")
            ceiling = juce::jlimit(0.1f, 1.0f, newValue);
        else if (parameterId == "limit")
            limiterEnabled = newValue > 0.5f ? 1.0f : 0.0f;
    }

    juce::Point<float> getMeterLevels() const override { return meterLevels; }

private:
    OutputCore core;
    float masterGain = 0.85f;
    float ceiling = 0.92f;
    float limiterEnabled = 1.0f;
    juce::Point<float> meterLevels;
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
private:
    juce::AudioFormatManager formatManager;
    std::vector<AudioArrangementClip> clips;
    juce::String selectedClipId;
    juce::String activeManualClipId;
    double currentSampleRateForClips = 44100.0;
    float volume = 0.8f;
    float pan = 0.0f;
    float mute = 0.0f;
    float solo = 0.0f;
    float arm = 0.0f;
    float monitor = 1.0f;
    float sync = 1.0f;
    float recordMode = 0.0f;
    float looping = 0.0f;
    bool editorOpen = false;
    bool editorDetached = false;
    float editorScale = 1.0f;
    int64_t lastTransportSamplePosition = 0;
    double lastPlaybackRate = 1.0;
    float lastLoopStart = 0.0f;
    float lastLoopEnd = 1.0f;
    double manualPlaybackSamplePosition = 0.0;
    bool isRunning = false;
    bool lastPlayTriggerHigh = false;
    bool emittedEndTrigger = false;
    bool lastRecordState = false;
    juce::Point<float> meterLevels;
    std::unique_ptr<juce::Component> editorComponent;

public:
    ~AudioTrackModule() override = default;

    AudioTrackModule()
    {
        formatManager.registerBasicFormats();
        addTrackClip(1.0f, 4.0f);
    }

    juce::String getTypeId() const override { return "AudioTrack"; }
    juce::String getDisplayName() const override { return "Audio Track"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xff4d96ff); }

    std::vector<PortInfo> getInputPorts() const override
    {
        return {
            { "Audio In", PortKind::audio },
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
            { "solo", "Solo", 0.0f, 1.0f, 0.0f },
            { "arm", "Arm", 0.0f, 1.0f, 0.0f },
            { "monitor", "Monitor", 0.0f, 1.0f, 1.0f },
            { "sync", "Sync", 0.0f, 1.0f, 1.0f },
            { "clipStartBar", "Clip Start", 1.0f, 128.0f, 1.0f },
            { "clipBars", "Clip Bars", 0.25f, 64.0f, 4.0f },
            { "fadeIn", "Fade In", 0.0f, 1.0f, 0.0f },
            { "fadeOut", "Fade Out", 0.0f, 1.0f, 0.0f },
            { "recordMode", "Record Mode", 0.0f, 1.0f, 0.0f },
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
        currentSampleRateForClips = context.sampleRate;
        output->clear();
        if (! context.modOutputs.empty())
            context.modOutputs[0] = 0.0f;

        meterLevels.x *= 0.82f;
        meterLevels.y *= 0.82f;

        const auto shouldMuteForSolo = context.anySoloActive && ! context.nodeSoloed;
        if (mute > 0.5f || shouldMuteForSolo)
            return;

        const auto playTriggerHigh = ! context.modInputs.empty() && context.modInputs[0] > 0.5f;
        if (playTriggerHigh && ! lastPlayTriggerHigh)
        {
            isRunning = true;
            manualPlaybackSamplePosition = 0.0;
            emittedEndTrigger = false;
            activeManualClipId = getSelectedTrackClipId();
        }
        lastPlayTriggerHigh = playTriggerHigh;

        const auto* inputBuffer = context.audioInputs.empty() ? nullptr : context.audioInputs.front();
        const auto rateControl = context.modInputs.size() < 2 || context.modInputs[1] <= 0.0f ? 1.0 : static_cast<double>(context.modInputs[1]);
        const auto playbackRate = juce::jlimit(0.125, 4.0, rateControl);

        if (arm > 0.5f && context.isRecording && context.isPlaying && inputBuffer != nullptr)
            recordIncomingAudio(context, *inputBuffer, *output);
        lastRecordState = context.isRecording;

        const auto useTransportSync = sync > 0.5f;
        const auto leftGain = volume * juce::jlimit(0.0f, 1.0f, 1.0f - juce::jmax(0.0f, pan));
        const auto rightGain = volume * juce::jlimit(0.0f, 1.0f, 1.0f + juce::jmin(0.0f, pan));

        auto reachedEndThisBlock = false;
        if (! clips.empty() && context.isPlaying)
        {
            if (useTransportSync)
                reachedEndThisBlock |= renderTimelineClips(context, *output, playbackRate, leftGain, rightGain);
            else if (isRunning)
                reachedEndThisBlock |= renderManualClip(context, *output, playbackRate, leftGain, rightGain);
        }

        if (reachedEndThisBlock && ! emittedEndTrigger && ! context.modOutputs.empty())
            context.modOutputs[0] = 1.0f;

        emittedEndTrigger = reachedEndThisBlock && looping <= 0.5f;
        if (looping > 0.5f)
            emittedEndTrigger = false;

        updateStereoMeter(*output, meterLevels);
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "volume") return volume;
        if (parameterId == "pan") return pan;
        if (parameterId == "mute") return mute;
        if (parameterId == "solo") return solo;
        if (parameterId == "arm") return arm;
        if (parameterId == "monitor") return monitor;
        if (parameterId == "sync") return sync;
        if (parameterId == "recordMode") return recordMode;
        if (parameterId == "looping") return looping;

        if (const auto* clip = getSelectedClip())
        {
            if (parameterId == "clipStartBar") return clip->startBar;
            if (parameterId == "clipBars") return clip->lengthBars;
            if (parameterId == "fadeIn") return clip->fadeIn;
            if (parameterId == "fadeOut") return clip->fadeOut;
            if (parameterId == "startPoint") return clip->sourceStart;
            if (parameterId == "endPoint") return clip->sourceEnd;
            if (parameterId == "loopStart") return clip->loopStart;
            if (parameterId == "loopEnd") return clip->loopEnd;
        }

        if (parameterId == "clipStartBar") return 1.0f;
        if (parameterId == "clipBars") return 4.0f;
        if (parameterId == "fadeIn") return 0.0f;
        if (parameterId == "fadeOut") return 0.0f;
        if (parameterId == "startPoint") return 0.0f;
        if (parameterId == "endPoint") return 1.0f;
        if (parameterId == "loopStart") return 0.0f;
        if (parameterId == "loopEnd") return 1.0f;
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "volume") volume = juce::jlimit(0.0f, 1.5f, newValue);
        else if (parameterId == "pan") pan = juce::jlimit(-1.0f, 1.0f, newValue);
        else if (parameterId == "mute") mute = juce::jlimit(0.0f, 1.0f, newValue);
        else if (parameterId == "solo") solo = newValue > 0.5f ? 1.0f : 0.0f;
        else if (parameterId == "arm") arm = newValue > 0.5f ? 1.0f : 0.0f;
        else if (parameterId == "monitor") monitor = newValue > 0.5f ? 1.0f : 0.0f;
        else if (parameterId == "sync") sync = newValue > 0.5f ? 1.0f : 0.0f;
        else if (parameterId == "recordMode") recordMode = newValue > 0.5f ? 1.0f : 0.0f;
        else if (parameterId == "looping") looping = newValue > 0.5f ? 1.0f : 0.0f;

        if (auto* clip = getMutableSelectedClip())
        {
            if (parameterId == "clipStartBar") clip->startBar = juce::jlimit(1.0f, 128.0f, newValue);
            else if (parameterId == "clipBars") clip->lengthBars = juce::jlimit(0.25f, 64.0f, newValue);
            else if (parameterId == "fadeIn") clip->fadeIn = juce::jlimit(0.0f, 1.0f, newValue);
            else if (parameterId == "fadeOut") clip->fadeOut = juce::jlimit(0.0f, 1.0f, newValue);
            else if (parameterId == "startPoint") clip->sourceStart = juce::jlimit(0.0f, 0.99f, newValue);
            else if (parameterId == "endPoint") clip->sourceEnd = juce::jlimit(0.01f, 1.0f, newValue);
            else if (parameterId == "loopStart") clip->loopStart = juce::jlimit(0.0f, 0.99f, newValue);
            else if (parameterId == "loopEnd") clip->loopEnd = juce::jlimit(0.01f, 1.0f, newValue);

            std::tie(clip->sourceStart, clip->sourceEnd) = normaliseLoopPoints(clip->sourceStart, clip->sourceEnd);
            std::tie(clip->loopStart, clip->loopEnd) = normaliseLoopPoints(clip->loopStart, clip->loopEnd);
            clip->loopStart = juce::jlimit(clip->sourceStart, juce::jmax(clip->sourceStart, clip->sourceEnd - 0.01f), clip->loopStart);
            clip->loopEnd = juce::jlimit(clip->loopStart + 0.01f, clip->sourceEnd, clip->loopEnd);
        }
    }

    bool isTrackModule() const override { return true; }
    juce::String getTrackTypeId() const override { return "audio"; }
    juce::String getResourcePath() const override { return getSelectedClip() != nullptr ? getSelectedClip()->filePath : juce::String(); }
    bool isSoloed() const override { return solo > 0.5f; }
    juce::Point<float> getMeterLevels() const override { return meterLevels; }

    std::vector<TrackLaneClipPreview> getTrackLaneClips() const override
    {
        std::vector<TrackLaneClipPreview> previews;
        previews.reserve(clips.size());
        for (const auto& clip : clips)
            previews.push_back({ clip.id, clip.label.isNotEmpty() ? clip.label : defaultClipLabel(clip.filePath), clip.startBar, clip.lengthBars, getNodeColour(), clip.id == selectedClipId });
        return previews;
    }

    juce::String getSelectedTrackClipId() const override
    {
        return selectedClipId;
    }

    bool setSelectedTrackClipId(const juce::String& clipId) override
    {
        if (clipId.isEmpty())
            return false;

        for (const auto& clip : clips)
        {
            if (clip.id == clipId)
            {
                selectedClipId = clipId;
                return true;
            }
        }

        return false;
    }

    bool addTrackClip(float startBar, float lengthBars) override
    {
        AudioArrangementClip clip;
        clip.id = juce::Uuid().toString();
        clip.label = defaultClipLabel();
        clip.startBar = juce::jmax(1.0f, startBar);
        clip.lengthBars = juce::jmax(0.25f, lengthBars);
        clip.sourceStart = 0.0f;
        clip.sourceEnd = 1.0f;
        clip.loopStart = 0.0f;
        clip.loopEnd = 1.0f;
        clips.push_back(clip);
        selectedClipId = clip.id;
        return true;
    }

    bool moveTrackClip(const juce::String& clipId, float startBar) override
    {
        if (auto* clip = findClipById(clipId))
        {
            clip->startBar = juce::jmax(1.0f, startBar);
            return true;
        }
        return false;
    }

    bool resizeTrackClip(const juce::String& clipId, float startBar, float lengthBars) override
    {
        if (auto* clip = findClipById(clipId))
        {
            clip->startBar = juce::jmax(1.0f, startBar);
            clip->lengthBars = juce::jmax(0.25f, lengthBars);
            return true;
        }
        return false;
    }

    bool loadFile(const juce::File& file) override
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader == nullptr)
            return false;

        juce::AudioBuffer<float> clipBuffer(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
        reader->read(&clipBuffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);

        if (clips.empty())
            addTrackClip(1.0f, 4.0f);

        if (auto* clip = getMutableSelectedClip())
        {
            clip->audioData = std::move(clipBuffer);
            clip->sampleRate = reader->sampleRate;
            clip->filePath = file.getFullPathName();
            clip->label = defaultClipLabel(clip->filePath);
        }

        return true;
    }

    void paintWaveform(float normalisedX, float normalisedY, float brushSize)
    {
        auto* clip = getMutableSelectedClip();
        if (clip == nullptr || clip->audioData.getNumSamples() == 0)
            return;

        const auto clampedX = juce::jlimit(0.0f, 1.0f, normalisedX);
        const auto clampedY = juce::jlimit(0.0f, 1.0f, normalisedY);
        const auto targetAmplitude = juce::jmap(clampedY, 1.0f, -1.0f);
        const auto centreSample = juce::jlimit(0, clip->audioData.getNumSamples() - 1,
                                               static_cast<int>(std::round(clampedX * static_cast<float>(clip->audioData.getNumSamples() - 1))));
        const auto radiusSamples = juce::jmax(1, static_cast<int>(std::round(brushSize * static_cast<float>(clip->audioData.getNumSamples()))));

        for (int sample = juce::jmax(0, centreSample - radiusSamples); sample < juce::jmin(clip->audioData.getNumSamples(), centreSample + radiusSamples + 1); ++sample)
        {
            const auto distance = std::abs(sample - centreSample);
            const auto blend = 1.0f - (static_cast<float>(distance) / static_cast<float>(juce::jmax(1, radiusSamples)));
            const auto strength = blend * 0.85f;

            for (int channel = 0; channel < clip->audioData.getNumChannels(); ++channel)
            {
                const auto current = clip->audioData.getSample(channel, sample);
                const auto painted = juce::jlimit(-1.0f, 1.0f, current + (targetAmplitude - current) * strength);
                clip->audioData.setSample(channel, sample, painted);
            }
        }
    }

    juce::ValueTree saveState() const override
    {
        auto state = ModuleNode::saveState();
        state.setProperty("selectedClipId", selectedClipId, nullptr);
        state.setProperty("editorOpen", editorOpen, nullptr);
        state.setProperty("editorDetached", editorDetached, nullptr);
        state.setProperty("editorScale", editorScale, nullptr);

        juce::ValueTree clipState("AUDIO_CLIPS");
        for (const auto& clip : clips)
        {
            juce::ValueTree clipTree("CLIP");
            clipTree.setProperty("id", clip.id, nullptr);
            clipTree.setProperty("label", clip.label, nullptr);
            clipTree.setProperty("filePath", clip.filePath, nullptr);
            clipTree.setProperty("audioData", encodeAudioBuffer(clip.audioData, clip.sampleRate), nullptr);
            clipTree.setProperty("startBar", clip.startBar, nullptr);
            clipTree.setProperty("lengthBars", clip.lengthBars, nullptr);
            clipTree.setProperty("sourceStart", clip.sourceStart, nullptr);
            clipTree.setProperty("sourceEnd", clip.sourceEnd, nullptr);
            clipTree.setProperty("loopStart", clip.loopStart, nullptr);
            clipTree.setProperty("loopEnd", clip.loopEnd, nullptr);
            clipTree.setProperty("fadeIn", clip.fadeIn, nullptr);
            clipTree.setProperty("fadeOut", clip.fadeOut, nullptr);
            clipState.appendChild(clipTree, nullptr);
        }
        state.appendChild(clipState, nullptr);
        return state;
    }

    void loadState(const juce::ValueTree& state) override
    {
        ModuleNode::loadState(state);
        clips.clear();
        if (const auto clipState = state.getChildWithName("AUDIO_CLIPS"); clipState.isValid())
        {
            for (const auto& clipTree : clipState)
            {
                AudioArrangementClip clip;
                clip.id = clipTree.getProperty("id").toString();
                clip.label = clipTree.getProperty("label").toString();
                clip.filePath = clipTree.getProperty("filePath").toString();
                clip.startBar = static_cast<float>(clipTree.getProperty("startBar", 1.0f));
                clip.lengthBars = static_cast<float>(clipTree.getProperty("lengthBars", 4.0f));
                clip.sourceStart = static_cast<float>(clipTree.getProperty("sourceStart", 0.0f));
                clip.sourceEnd = static_cast<float>(clipTree.getProperty("sourceEnd", 1.0f));
                clip.loopStart = static_cast<float>(clipTree.getProperty("loopStart", 0.0f));
                clip.loopEnd = static_cast<float>(clipTree.getProperty("loopEnd", 1.0f));
                clip.fadeIn = static_cast<float>(clipTree.getProperty("fadeIn", 0.0f));
                clip.fadeOut = static_cast<float>(clipTree.getProperty("fadeOut", 0.0f));
                if (! decodeAudioBuffer(clipTree.getProperty("audioData").toString(), clip.audioData, clip.sampleRate) && clip.filePath.isNotEmpty())
                    if (std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(juce::File(clip.filePath))); reader != nullptr)
                    {
                        clip.audioData.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
                        reader->read(&clip.audioData, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
                        clip.sampleRate = reader->sampleRate;
                    }
                clips.push_back(std::move(clip));
            }
        }

        if (clips.empty())
            addTrackClip(1.0f, 4.0f);

        selectedClipId = state.getProperty("selectedClipId", clips.front().id).toString();
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

    const juce::AudioBuffer<float>& getClipBuffer() const
    {
        static juce::AudioBuffer<float> empty;
        if (const auto* clip = getSelectedClip())
            return clip->audioData;
        return empty;
    }
    juce::String getLoadedFileName() const { return getSelectedClip() != nullptr ? juce::File(getSelectedClip()->filePath).getFileName() : juce::String(); }
    double getClipSampleRate() const { return getSelectedClip() != nullptr ? getSelectedClip()->sampleRate : 44100.0; }
    int64_t getLastTransportSamplePosition() const { return lastTransportSamplePosition; }
    double getLastPlaybackRate() const { return lastPlaybackRate; }
    float getLoopStart() const { return getSelectedClip() != nullptr ? getSelectedClip()->loopStart : 0.0f; }
    float getLoopEnd() const { return getSelectedClip() != nullptr ? getSelectedClip()->loopEnd : 1.0f; }
    float getLastLoopStart() const { return lastLoopStart; }
    float getLastLoopEnd() const { return lastLoopEnd; }
    float getStartPoint() const { return getSelectedClip() != nullptr ? getSelectedClip()->sourceStart : 0.0f; }
    float getEndPoint() const { return getSelectedClip() != nullptr ? getSelectedClip()->sourceEnd : 1.0f; }

private:
    juce::String defaultClipLabel(const juce::String& path = {}) const
    {
        return path.isNotEmpty() ? juce::File(path).getFileNameWithoutExtension() : "Audio Clip";
    }
    AudioArrangementClip* findClipById(const juce::String& clipId)
    {
        for (auto& clip : clips)
            if (clip.id == clipId)
                return &clip;
        return nullptr;
    }

    const AudioArrangementClip* findClipById(const juce::String& clipId) const
    {
        for (const auto& clip : clips)
            if (clip.id == clipId)
                return &clip;
        return nullptr;
    }

    AudioArrangementClip* getMutableSelectedClip()
    {
        if (clips.empty())
            return nullptr;
        if (auto* clip = findClipById(selectedClipId))
            return clip;
        selectedClipId = clips.front().id;
        return &clips.front();
    }

    const AudioArrangementClip* getSelectedClip() const
    {
        if (clips.empty())
            return nullptr;
        if (auto* clip = findClipById(selectedClipId))
            return clip;
        return &clips.front();
    }

    void recordIncomingAudio(const NodeRenderContext& context, const juce::AudioBuffer<float>& inputBuffer, juce::AudioBuffer<float>& output)
    {
        if (clips.empty())
            addTrackClip(static_cast<float>(std::floor(static_cast<double>(context.transportSamplePosition) / (samplesPerBeat(context) * beatsPerBar(context)))) + 1.0f, 4.0f);

        auto* clip = getMutableSelectedClip();
        if (clip == nullptr)
            return;

        if (! lastRecordState)
        {
            clip->filePath.clear();
            clip->label = "Recorded Clip";
            if (recordMode < 0.5f)
                clip->audioData.setSize(juce::jmax(2, inputBuffer.getNumChannels()), 0);
        }

        clip->sampleRate = context.sampleRate;
        const auto writeOffset = juce::jmax<int>(0, static_cast<int>(context.transportSamplePosition));
        const auto requiredSamples = writeOffset + context.numSamples;
        const auto currentChannels = juce::jmax(2, inputBuffer.getNumChannels());

        if (clip->audioData.getNumChannels() != currentChannels || clip->audioData.getNumSamples() < requiredSamples)
        {
            juce::AudioBuffer<float> extended(currentChannels, requiredSamples);
            extended.clear();
            if (clip->audioData.getNumSamples() > 0)
                for (int channel = 0; channel < juce::jmin(currentChannels, clip->audioData.getNumChannels()); ++channel)
                    extended.copyFrom(channel, 0, clip->audioData, channel, 0, clip->audioData.getNumSamples());
            clip->audioData = std::move(extended);
        }

        for (int channel = 0; channel < juce::jmin(clip->audioData.getNumChannels(), inputBuffer.getNumChannels()); ++channel)
            clip->audioData.copyFrom(channel, writeOffset, inputBuffer, channel, 0, context.numSamples);

        if (monitor > 0.5f)
            for (int channel = 0; channel < juce::jmin(output.getNumChannels(), inputBuffer.getNumChannels()); ++channel)
                output.addFrom(channel, 0, inputBuffer, channel, 0, context.numSamples);

        const auto barSamples = samplesPerBeat(context) * beatsPerBar(context);
        const auto clipStartSample = juce::jmax(0.0, (static_cast<double>(clip->startBar) - 1.0) * barSamples);
        const auto clipEndSample = static_cast<double>(writeOffset + context.numSamples);
        clip->lengthBars = juce::jmax(0.25f, static_cast<float>((clipEndSample - clipStartSample) / juce::jmax(1.0, barSamples)));
    }

    bool renderTimelineClips(const NodeRenderContext& context,
                             juce::AudioBuffer<float>& output,
                             double effectiveRate,
                             float leftGain,
                             float rightGain)
    {
        bool reachedEnd = false;
        const auto barSamples = samplesPerBeat(context) * beatsPerBar(context);
        lastTransportSamplePosition = context.transportSamplePosition;
        lastPlaybackRate = effectiveRate;

        for (const auto& clip : clips)
        {
            const auto clipStartSample = (static_cast<double>(clip.startBar) - 1.0) * barSamples;
            const auto clipLengthTimelineSamples = static_cast<double>(clip.lengthBars) * barSamples;
            if (clipLengthTimelineSamples <= 0.0)
                continue;

            for (int sample = 0; sample < context.numSamples; ++sample)
            {
                const auto timelineSample = static_cast<double>(context.transportSamplePosition + sample);
                const auto localTimelineSample = timelineSample - clipStartSample;
                if (localTimelineSample < 0.0 || localTimelineSample >= clipLengthTimelineSamples)
                    continue;

                reachedEnd |= renderAudioClipSample(output, sample, clip, localTimelineSample, effectiveRate, leftGain, rightGain);
            }
        }

        return reachedEnd;
    }

    bool renderManualClip(const NodeRenderContext& context,
                          juce::AudioBuffer<float>& output,
                          double effectiveRate,
                          float leftGain,
                          float rightGain)
    {
        const auto* clip = findClipById(activeManualClipId.isNotEmpty() ? activeManualClipId : selectedClipId);
        if (clip == nullptr)
            return false;

        lastTransportSamplePosition = static_cast<int64_t>(std::llround(manualPlaybackSamplePosition));
        lastPlaybackRate = effectiveRate;
        bool reachedEnd = false;

        for (int sample = 0; sample < context.numSamples; ++sample)
        {
            if (! isRunning)
                break;

            reachedEnd |= renderAudioClipSample(output, sample, *clip, manualPlaybackSamplePosition, effectiveRate, leftGain, rightGain);
            manualPlaybackSamplePosition += 1.0;
            if (reachedEnd && looping <= 0.5f)
                isRunning = false;
        }

        return reachedEnd;
    }

    bool renderAudioClipSample(juce::AudioBuffer<float>& output,
                               int outputSample,
                               const AudioArrangementClip& clip,
                               double localTimelineSample,
                               double playbackRate,
                               float leftGain,
                               float rightGain) const
    {
        if (clip.audioData.getNumSamples() <= 1)
            return false;

        auto sourceStart = clip.sourceStart;
        auto sourceEnd = clip.sourceEnd;
        auto loopStartLocal = clip.loopStart;
        auto loopEndLocal = clip.loopEnd;
        std::tie(sourceStart, sourceEnd) = normaliseLoopPoints(sourceStart, sourceEnd);
        std::tie(loopStartLocal, loopEndLocal) = normaliseLoopPoints(loopStartLocal, loopEndLocal);
        loopStartLocal = juce::jlimit(sourceStart, juce::jmax(sourceStart, sourceEnd - 0.01f), loopStartLocal);
        loopEndLocal = juce::jlimit(loopStartLocal + 0.01f, sourceEnd, loopEndLocal);

        const auto sourceStartSample = sourceStart * static_cast<float>(clip.audioData.getNumSamples() - 1);
        const auto sourceEndSample = juce::jmax(sourceStartSample + 1.0f, sourceEnd * static_cast<float>(clip.audioData.getNumSamples()));
        const auto loopStartSample = loopStartLocal * static_cast<float>(clip.audioData.getNumSamples() - 1);
        const auto loopEndSample = juce::jmax(loopStartSample + 1.0f, loopEndLocal * static_cast<float>(clip.audioData.getNumSamples()));
        const auto sampleRateRatio = clip.sampleRate / juce::jmax(1.0, currentSampleRateForClips);
        auto clipPosition = sourceStartSample + localTimelineSample * sampleRateRatio * playbackRate;
        bool reachedEnd = false;

        if (looping > 0.5f)
        {
            const auto loopLength = juce::jmax(1.0f, loopEndSample - loopStartSample);
            while (clipPosition >= loopEndSample)
            {
                clipPosition -= loopLength;
                clipPosition += (loopStartSample - sourceStartSample);
                reachedEnd = true;
            }
        }
        else if (clipPosition >= sourceEndSample)
        {
            return true;
        }

        const auto clipIndex = juce::jlimit(0, clip.audioData.getNumSamples() - 1, static_cast<int>(clipPosition));
        const auto nextIndex = juce::jlimit(0, clip.audioData.getNumSamples() - 1, clipIndex + 1);
        const auto fraction = static_cast<float>(clipPosition - static_cast<double>(clipIndex));
        const auto left = juce::jmap(fraction, clip.audioData.getSample(0, clipIndex), clip.audioData.getSample(0, nextIndex));
        const auto rightChannel = juce::jmin(1, clip.audioData.getNumChannels() - 1);
        const auto right = juce::jmap(fraction, clip.audioData.getSample(rightChannel, clipIndex), clip.audioData.getSample(rightChannel, nextIndex));
        auto fadeGain = 1.0f;
        const auto regionNormalised = juce::jlimit(0.0f, 1.0f, static_cast<float>(localTimelineSample / juce::jmax(1.0, static_cast<double>(clip.lengthBars))));
        if (clip.fadeIn > 0.0f && regionNormalised < clip.fadeIn)
            fadeGain *= juce::jlimit(0.0f, 1.0f, regionNormalised / clip.fadeIn);
        if (clip.fadeOut > 0.0f && regionNormalised > 1.0f - clip.fadeOut)
            fadeGain *= juce::jlimit(0.0f, 1.0f, (1.0f - regionNormalised) / clip.fadeOut);

        output.addSample(0, outputSample, left * leftGain * fadeGain);
        if (output.getNumChannels() > 1)
            output.addSample(1, outputSample, right * rightGain * fadeGain);

        return reachedEnd;
    }

};

class MidiTrackModule final : public ModuleNode
{
public:
    ~MidiTrackModule() override = default;

    MidiTrackModule()
    {
        addTrackClip(1.0f, 4.0f);
        if (auto* clip = getMutableSelectedClip())
        {
            clip->notes.push_back({ juce::Uuid().toString(), 0.0f, 1.0f, 60, 0.85f });
            clip->notes.push_back({ juce::Uuid().toString(), 1.5f, 0.5f, 64, 0.72f });
            clip->notes.push_back({ juce::Uuid().toString(), 2.0f, 1.0f, 67, 0.9f });
        }
    }

    juce::String getTypeId() const override { return "MidiTrack"; }
    juce::String getDisplayName() const override { return "MIDI Track"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xff7c5cff); }

    std::vector<PortInfo> getInputPorts() const override
    {
        return {
            { "Gate In", PortKind::modulation },
            { "Pitch In", PortKind::modulation },
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
            { "solo", "Solo", 0.0f, 1.0f, 0.0f },
            { "arm", "Arm", 0.0f, 1.0f, 0.0f },
            { "sync", "Sync", 0.0f, 1.0f, 1.0f },
            { "clipStartBar", "Clip Start", 1.0f, 128.0f, 1.0f },
            { "clipBars", "Clip Bars", 0.25f, 64.0f, 4.0f },
            { "looping", "Loop", 0.0f, 1.0f, 0.0f },
            { "rootNote", "Root Note", 24.0f, 96.0f, 60.0f },
            { "gain", "MIDI Gain", 0.0f, 1.0f, 0.2f },
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

        meterLevels.x *= 0.82f;
        meterLevels.y *= 0.82f;

        const auto shouldMuteForSolo = context.anySoloActive && ! context.nodeSoloed;
        if (mute > 0.5f || shouldMuteForSolo)
            return;

        const auto gateInput = context.modInputs.size() > 0 ? context.modInputs[0] : 0.0f;
        const auto pitchInput = context.modInputs.size() > 1 ? context.modInputs[1] : 0.0f;
        const auto playTriggerHigh = context.modInputs.size() > 2 && context.modInputs[2] > 0.5f;
        if (playTriggerHigh && ! lastPlayTriggerHigh)
        {
            isRunning = true;
            manualPlaybackSamples = 0.0;
            activeManualClipId = getSelectedTrackClipId();
            emittedEndTrigger = false;
        }
        lastPlayTriggerHigh = playTriggerHigh;

        if (arm > 0.5f && context.isRecording && context.isPlaying)
            recordIncomingMidi(context, gateInput, pitchInput);
        else
            clearRecordingNote();

        const auto rateControl = context.modInputs.size() < 4 || context.modInputs[3] <= 0.0f ? 1.0 : static_cast<double>(context.modInputs[3]);
        const auto playbackRate = juce::jlimit(0.125, 4.0, rateControl);
        const auto leftGain = volume * juce::jlimit(0.0f, 1.0f, 1.0f - juce::jmax(0.0f, pan));
        const auto rightGain = volume * juce::jlimit(0.0f, 1.0f, 1.0f + juce::jmin(0.0f, pan));
        bool reachedEnd = false;

        if (context.isPlaying)
        {
            if (sync > 0.5f)
                reachedEnd |= renderTimeline(context, *output, playbackRate, leftGain, rightGain);
            else if (isRunning)
                reachedEnd |= renderManual(context, *output, playbackRate, leftGain, rightGain);
        }

        if (reachedEnd && ! emittedEndTrigger && ! context.modOutputs.empty())
            context.modOutputs[0] = 1.0f;

        emittedEndTrigger = reachedEnd && looping <= 0.5f;
        if (looping > 0.5f)
            emittedEndTrigger = false;

        updateStereoMeter(*output, meterLevels);
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "volume") return volume;
        if (parameterId == "pan") return pan;
        if (parameterId == "mute") return mute;
        if (parameterId == "solo") return solo;
        if (parameterId == "arm") return arm;
        if (parameterId == "sync") return sync;
        if (parameterId == "looping") return looping;
        if (parameterId == "rootNote") return static_cast<float>(rootNote);
        if (parameterId == "gain") return gain;

        if (const auto* clip = getSelectedClip())
        {
            if (parameterId == "clipStartBar") return clip->startBar;
            if (parameterId == "clipBars") return clip->lengthBars;
            if (parameterId == "startPoint") return clip->startPoint;
            if (parameterId == "endPoint") return clip->endPoint;
            if (parameterId == "loopStart") return clip->loopStart;
            if (parameterId == "loopEnd") return clip->loopEnd;
        }

        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "volume") volume = juce::jlimit(0.0f, 1.5f, newValue);
        else if (parameterId == "pan") pan = juce::jlimit(-1.0f, 1.0f, newValue);
        else if (parameterId == "mute") mute = juce::jlimit(0.0f, 1.0f, newValue);
        else if (parameterId == "solo") solo = newValue > 0.5f ? 1.0f : 0.0f;
        else if (parameterId == "arm") arm = newValue > 0.5f ? 1.0f : 0.0f;
        else if (parameterId == "sync") sync = newValue > 0.5f ? 1.0f : 0.0f;
        else if (parameterId == "looping") looping = newValue > 0.5f ? 1.0f : 0.0f;
        else if (parameterId == "rootNote") rootNote = juce::jlimit(24, 96, static_cast<int>(std::round(newValue)));
        else if (parameterId == "gain") gain = juce::jlimit(0.0f, 1.0f, newValue);

        if (auto* clip = getMutableSelectedClip())
        {
            if (parameterId == "clipStartBar") clip->startBar = juce::jmax(1.0f, newValue);
            else if (parameterId == "clipBars") clip->lengthBars = juce::jmax(0.25f, newValue);
            else if (parameterId == "startPoint") clip->startPoint = juce::jlimit(0.0f, 0.99f, newValue);
            else if (parameterId == "endPoint") clip->endPoint = juce::jlimit(0.01f, 1.0f, newValue);
            else if (parameterId == "loopStart") clip->loopStart = juce::jlimit(0.0f, 0.99f, newValue);
            else if (parameterId == "loopEnd") clip->loopEnd = juce::jlimit(0.01f, 1.0f, newValue);

            std::tie(clip->startPoint, clip->endPoint) = normaliseLoopPoints(clip->startPoint, clip->endPoint);
            std::tie(clip->loopStart, clip->loopEnd) = normaliseLoopPoints(clip->loopStart, clip->loopEnd);
            clip->loopStart = juce::jlimit(clip->startPoint, juce::jmax(clip->startPoint, clip->endPoint - 0.01f), clip->loopStart);
            clip->loopEnd = juce::jlimit(clip->loopStart + 0.01f, clip->endPoint, clip->loopEnd);
        }
    }

    bool isTrackModule() const override { return true; }
    juce::String getTrackTypeId() const override { return "midi"; }
    bool isSoloed() const override { return solo > 0.5f; }
    juce::Point<float> getMeterLevels() const override { return meterLevels; }

    std::vector<TrackLaneClipPreview> getTrackLaneClips() const override
    {
        std::vector<TrackLaneClipPreview> previews;
        previews.reserve(clips.size());
        for (const auto& clip : clips)
            previews.push_back({ clip.id, clip.label.isNotEmpty() ? clip.label : "MIDI Clip", clip.startBar, clip.lengthBars, getNodeColour(), clip.id == selectedClipId });
        return previews;
    }

    juce::String getSelectedTrackClipId() const override
    {
        return selectedClipId;
    }

    bool setSelectedTrackClipId(const juce::String& clipId) override
    {
        if (clipId.isEmpty())
            return false;

        for (const auto& clip : clips)
        {
            if (clip.id == clipId)
            {
                selectedClipId = clipId;
                return true;
            }
        }
        return false;
    }

    bool addTrackClip(float startBar, float lengthBars) override
    {
        MidiArrangementClip clip;
        clip.id = juce::Uuid().toString();
        clip.label = "MIDI Clip";
        clip.startBar = juce::jmax(1.0f, startBar);
        clip.lengthBars = juce::jmax(0.25f, lengthBars);
        clips.push_back(clip);
        selectedClipId = clip.id;
        return true;
    }

    bool moveTrackClip(const juce::String& clipId, float startBar) override
    {
        if (auto* clip = findClipById(clipId))
        {
            clip->startBar = juce::jmax(1.0f, startBar);
            return true;
        }
        return false;
    }

    bool resizeTrackClip(const juce::String& clipId, float startBar, float lengthBars) override
    {
        if (auto* clip = findClipById(clipId))
        {
            clip->startBar = juce::jmax(1.0f, startBar);
            clip->lengthBars = juce::jmax(0.25f, lengthBars);
            return true;
        }
        return false;
    }

    juce::ValueTree saveState() const override
    {
        auto state = ModuleNode::saveState();
        state.setProperty("selectedClipId", selectedClipId, nullptr);
        state.setProperty("editorOpen", editorOpen, nullptr);
        state.setProperty("editorDetached", editorDetached, nullptr);
        state.setProperty("editorScale", editorScale, nullptr);

        juce::ValueTree clipState("MIDI_CLIPS");
        for (const auto& clip : clips)
        {
            juce::ValueTree clipTree("CLIP");
            clipTree.setProperty("id", clip.id, nullptr);
            clipTree.setProperty("label", clip.label, nullptr);
            clipTree.setProperty("startBar", clip.startBar, nullptr);
            clipTree.setProperty("lengthBars", clip.lengthBars, nullptr);
            clipTree.setProperty("startPoint", clip.startPoint, nullptr);
            clipTree.setProperty("endPoint", clip.endPoint, nullptr);
            clipTree.setProperty("loopStart", clip.loopStart, nullptr);
            clipTree.setProperty("loopEnd", clip.loopEnd, nullptr);
            for (const auto& note : clip.notes)
            {
                juce::ValueTree noteTree("NOTE");
                noteTree.setProperty("id", note.id, nullptr);
                noteTree.setProperty("startBeat", note.startBeat, nullptr);
                noteTree.setProperty("lengthBeats", note.lengthBeats, nullptr);
                noteTree.setProperty("noteNumber", note.noteNumber, nullptr);
                noteTree.setProperty("velocity", note.velocity, nullptr);
                clipTree.appendChild(noteTree, nullptr);
            }
            clipState.appendChild(clipTree, nullptr);
        }
        state.appendChild(clipState, nullptr);
        return state;
    }

    void loadState(const juce::ValueTree& state) override
    {
        ModuleNode::loadState(state);
        clips.clear();
        if (const auto clipState = state.getChildWithName("MIDI_CLIPS"); clipState.isValid())
        {
            for (const auto& clipTree : clipState)
            {
                MidiArrangementClip clip;
                clip.id = clipTree.getProperty("id").toString();
                clip.label = clipTree.getProperty("label").toString();
                clip.startBar = static_cast<float>(clipTree.getProperty("startBar", 1.0f));
                clip.lengthBars = static_cast<float>(clipTree.getProperty("lengthBars", 4.0f));
                clip.startPoint = static_cast<float>(clipTree.getProperty("startPoint", 0.0f));
                clip.endPoint = static_cast<float>(clipTree.getProperty("endPoint", 1.0f));
                clip.loopStart = static_cast<float>(clipTree.getProperty("loopStart", 0.0f));
                clip.loopEnd = static_cast<float>(clipTree.getProperty("loopEnd", 1.0f));
                for (const auto& noteTree : clipTree)
                {
                    if (! noteTree.hasType("NOTE"))
                        continue;
                    clip.notes.push_back({
                        noteTree.getProperty("id").toString(),
                        static_cast<float>(noteTree.getProperty("startBeat", 0.0f)),
                        static_cast<float>(noteTree.getProperty("lengthBeats", 1.0f)),
                        static_cast<int>(noteTree.getProperty("noteNumber", 60)),
                        static_cast<float>(noteTree.getProperty("velocity", 0.8f))
                    });
                }
                clips.push_back(std::move(clip));
            }
        }

        if (clips.empty())
            addTrackClip(1.0f, 4.0f);

        selectedClipId = state.getProperty("selectedClipId", clips.front().id).toString();
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

    const MidiArrangementClip* getSelectedClip() const
    {
        if (clips.empty())
            return nullptr;
        for (const auto& clip : clips)
            if (clip.id == selectedClipId)
                return &clip;
        return &clips.front();
    }

    MidiArrangementClip* getMutableSelectedClip()
    {
        if (clips.empty())
            return nullptr;
        for (auto& clip : clips)
            if (clip.id == selectedClipId)
                return &clip;
        selectedClipId = clips.front().id;
        return &clips.front();
    }

    int getRootNote() const { return rootNote; }
    int64_t getLastTransportSamplePosition() const { return lastTransportSamplePosition; }
    double getLastPlaybackRate() const { return lastPlaybackRate; }
    float getStartPoint() const { return getSelectedClip() != nullptr ? getSelectedClip()->startPoint : 0.0f; }
    float getEndPoint() const { return getSelectedClip() != nullptr ? getSelectedClip()->endPoint : 1.0f; }
    float getLoopStart() const { return getSelectedClip() != nullptr ? getSelectedClip()->loopStart : 0.0f; }
    float getLoopEnd() const { return getSelectedClip() != nullptr ? getSelectedClip()->loopEnd : 1.0f; }
    float getLastLoopStart() const { return lastLoopStart; }
    float getLastLoopEnd() const { return lastLoopEnd; }
    double getSelectedClipPlayheadBeat() const { return lastSelectedClipBeat; }
    float getSelectedClipLengthBars() const { return getSelectedClip() != nullptr ? getSelectedClip()->lengthBars : 4.0f; }
    float getSelectedClipLengthBeats() const { return getSelectedClipLengthBars() * 4.0f; }

    const std::vector<MidiNoteEvent>& getSelectedClipNotes() const
    {
        static const std::vector<MidiNoteEvent> empty;
        if (const auto* clip = getSelectedClip())
            return clip->notes;
        return empty;
    }

    void addOrUpdateNote(const juce::String& noteId, float startBeat, float lengthBeats, int noteNumber, float velocity)
    {
        if (auto* clip = getMutableSelectedClip())
        {
            auto clampedVelocity = juce::jlimit(0.05f, 1.0f, velocity);
            auto clampedLength = juce::jmax(0.125f, lengthBeats);
            auto clampedStart = juce::jmax(0.0f, startBeat);
            for (auto& note : clip->notes)
            {
                if (note.id == noteId)
                {
                    note.startBeat = clampedStart;
                    note.lengthBeats = clampedLength;
                    note.noteNumber = juce::jlimit(24, 108, noteNumber);
                    note.velocity = clampedVelocity;
                    return;
                }
            }

            clip->notes.push_back({ noteId.isNotEmpty() ? noteId : juce::Uuid().toString(),
                                    clampedStart,
                                    clampedLength,
                                    juce::jlimit(24, 108, noteNumber),
                                    clampedVelocity });
        }
    }

    void removeNote(const juce::String& noteId)
    {
        if (auto* clip = getMutableSelectedClip())
        {
            clip->notes.erase(std::remove_if(clip->notes.begin(), clip->notes.end(),
                                             [&noteId](const auto& note) { return note.id == noteId; }),
                              clip->notes.end());
        }
    }

private:
    MidiArrangementClip* findClipById(const juce::String& clipId)
    {
        for (auto& clip : clips)
            if (clip.id == clipId)
                return &clip;
        return nullptr;
    }

    void clearRecordingNote()
    {
        lastRecordGateHigh = false;
        recordingNoteId.clear();
        recordingNoteStartBeat = 0.0f;
    }

    void recordIncomingMidi(const NodeRenderContext& context, float gateInput, float pitchInput)
    {
        auto* clip = getMutableSelectedClip();
        if (clip == nullptr)
            return;

        const auto gateHigh = gateInput > 0.5f;
        const auto barSamples = samplesPerBeat(context) * beatsPerBar(context);
        const auto clipStartSample = (static_cast<double>(clip->startBar) - 1.0) * barSamples;
        const auto relativeBeat = juce::jmax(0.0, (static_cast<double>(context.transportSamplePosition) - clipStartSample) / juce::jmax(1.0, samplesPerBeat(context)));
        const auto noteNumber = juce::jlimit(24, 108, static_cast<int>(std::round(static_cast<float>(rootNote) + pitchInput * 12.0f)));

        if (gateHigh && ! lastRecordGateHigh)
        {
            recordingNoteId = juce::Uuid().toString();
            recordingNoteStartBeat = static_cast<float>(relativeBeat);
            addOrUpdateNote(recordingNoteId, recordingNoteStartBeat, 0.25f, noteNumber, std::abs(gateInput));
        }
        else if (gateHigh && lastRecordGateHigh && recordingNoteId.isNotEmpty())
        {
            addOrUpdateNote(recordingNoteId, recordingNoteStartBeat, static_cast<float>(juce::jmax(0.125, relativeBeat - static_cast<double>(recordingNoteStartBeat))), noteNumber, std::abs(gateInput));
        }
        else if (! gateHigh && lastRecordGateHigh && recordingNoteId.isNotEmpty())
        {
            addOrUpdateNote(recordingNoteId, recordingNoteStartBeat, static_cast<float>(juce::jmax(0.125, relativeBeat - static_cast<double>(recordingNoteStartBeat))), noteNumber, std::abs(gateInput));
            recordingNoteId.clear();
        }

        lastRecordGateHigh = gateHigh;
    }

    bool renderTimeline(const NodeRenderContext& context,
                        juce::AudioBuffer<float>& output,
                        double playbackRate,
                        float leftGain,
                        float rightGain)
    {
        bool reachedEnd = false;
        const auto barSamples = samplesPerBeat(context) * beatsPerBar(context);
        lastTransportSamplePosition = context.transportSamplePosition;
        lastPlaybackRate = playbackRate;
        lastSelectedClipBeat = 0.0;

        for (const auto& clip : clips)
        {
            const auto clipStartSample = (static_cast<double>(clip.startBar) - 1.0) * barSamples;
            const auto clipLengthSamples = static_cast<double>(clip.lengthBars) * barSamples;
            if (clipLengthSamples <= 0.0)
                continue;

            for (int sample = 0; sample < context.numSamples; ++sample)
            {
                const auto timelineSample = static_cast<double>(context.transportSamplePosition + sample);
                const auto relativeTimelineSample = timelineSample - clipStartSample;
                if (relativeTimelineSample < 0.0 || relativeTimelineSample >= clipLengthSamples)
                    continue;

                const auto localBeat = computeClipBeat(clip, context, relativeTimelineSample, playbackRate, reachedEnd);
                if (clip.id == selectedClipId)
                    lastSelectedClipBeat = localBeat;
                renderMidiAtBeat(output, sample, clip, localBeat, leftGain, rightGain);
            }
        }

        return reachedEnd;
    }

    bool renderManual(const NodeRenderContext& context,
                      juce::AudioBuffer<float>& output,
                      double playbackRate,
                      float leftGain,
                      float rightGain)
    {
        auto* clip = findClipById(activeManualClipId.isNotEmpty() ? activeManualClipId : selectedClipId);
        if (clip == nullptr)
            return false;

        bool reachedEnd = false;
        lastTransportSamplePosition = static_cast<int64_t>(std::llround(manualPlaybackSamples));
        lastPlaybackRate = playbackRate;
        for (int sample = 0; sample < context.numSamples; ++sample)
        {
            if (! isRunning)
                break;

            const auto localBeat = computeClipBeat(*clip, context, manualPlaybackSamples, playbackRate, reachedEnd);
            lastSelectedClipBeat = localBeat;
            renderMidiAtBeat(output, sample, *clip, localBeat, leftGain, rightGain);
            manualPlaybackSamples += 1.0;
            if (reachedEnd && looping <= 0.5f)
                isRunning = false;
        }

        return reachedEnd;
    }

    double computeClipBeat(const MidiArrangementClip& clip,
                           const NodeRenderContext& context,
                           double timelineSamplesIntoClip,
                           double playbackRate,
                           bool& reachedEnd)
    {
        const auto clipBeats = static_cast<double>(clip.lengthBars) * beatsPerBar(context);
        const auto startBeat = static_cast<double>(clip.startPoint) * clipBeats;
        const auto endBeat = static_cast<double>(clip.endPoint) * clipBeats;
        auto loopStartBeat = static_cast<double>(clip.loopStart) * clipBeats;
        auto loopEndBeat = static_cast<double>(clip.loopEnd) * clipBeats;
        loopStartBeat = juce::jlimit(startBeat, juce::jmax(startBeat, endBeat - 0.01), loopStartBeat);
        loopEndBeat = juce::jlimit(loopStartBeat + 0.01, endBeat, loopEndBeat);
        lastLoopStart = static_cast<float>(loopStartBeat / juce::jmax(0.0001, clipBeats));
        lastLoopEnd = static_cast<float>(loopEndBeat / juce::jmax(0.0001, clipBeats));

        auto localBeat = startBeat + (timelineSamplesIntoClip / juce::jmax(1.0, samplesPerBeat(context))) * playbackRate;
        if (looping > 0.5f)
        {
            const auto loopLength = juce::jmax(0.01, loopEndBeat - loopStartBeat);
            while (localBeat >= loopEndBeat)
            {
                localBeat -= loopLength;
                localBeat += loopStartBeat - startBeat;
                reachedEnd = true;
            }
        }
        else if (localBeat >= endBeat)
        {
            reachedEnd = true;
            return -1.0;
        }

        return localBeat;
    }

    void renderMidiAtBeat(juce::AudioBuffer<float>& output,
                          int sample,
                          const MidiArrangementClip& clip,
                          double localBeat,
                          float leftGain,
                          float rightGain) const
    {
        if (localBeat < 0.0)
            return;

        float mixed = 0.0f;
        for (const auto& note : clip.notes)
        {
            if (localBeat < static_cast<double>(note.startBeat)
                || localBeat >= static_cast<double>(note.startBeat + note.lengthBeats))
                continue;

            const auto noteProgress = (localBeat - static_cast<double>(note.startBeat)) / static_cast<double>(juce::jmax(0.125f, note.lengthBeats));
            const auto frequency = midiToFrequency(note.noteNumber);
            const auto phase = juce::MathConstants<double>::twoPi * frequency * ((localBeat - static_cast<double>(note.startBeat)) * 60.0 / 120.0);
            const auto envelope = static_cast<float>(1.0 - juce::jlimit(0.0, 1.0, noteProgress));
            mixed += static_cast<float>(std::sin(phase)) * gain * note.velocity * envelope;
        }

        mixed = juce::jlimit(-1.0f, 1.0f, mixed);
        output.addSample(0, sample, mixed * leftGain);
        if (output.getNumChannels() > 1)
            output.addSample(1, sample, mixed * rightGain);
    }

    std::vector<MidiArrangementClip> clips;
    juce::String selectedClipId;
    juce::String activeManualClipId;
    juce::String recordingNoteId;
    float recordingNoteStartBeat = 0.0f;
    float volume = 0.8f;
    float pan = 0.0f;
    float mute = 0.0f;
    float solo = 0.0f;
    float arm = 0.0f;
    float sync = 1.0f;
    float looping = 0.0f;
    int rootNote = 60;
    float gain = 0.2f;
    bool editorOpen = false;
    bool editorDetached = false;
    float editorScale = 1.0f;
    int64_t lastTransportSamplePosition = 0;
    double lastPlaybackRate = 1.0;
    float lastLoopStart = 0.0f;
    float lastLoopEnd = 1.0f;
    double lastSelectedClipBeat = 0.0;
    double manualPlaybackSamples = 0.0;
    bool isRunning = false;
    bool lastPlayTriggerHigh = false;
    bool lastRecordGateHigh = false;
    bool emittedEndTrigger = false;
    juce::Point<float> meterLevels;
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
        setWantsKeyboardFocus(true);
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
        bounds.removeFromTop(52);

        const auto keyWidth = 76;
        auto keyboardArea = bounds.removeFromLeft(keyWidth);
        auto rulerArea = bounds.removeFromTop(20);
        auto velocityArea = bounds.removeFromBottom(72);
        auto gridArea = bounds;
        const auto rows = 36;
        const auto totalBeats = juce::jmax(1.0f, module.getSelectedClipLengthBeats());
        const auto rowHeight = juce::jmax(12, gridArea.getHeight() / rows);
        const auto pixelsPerBeat = static_cast<float>(gridArea.getWidth()) / totalBeats;
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
        for (int beat = 0; beat <= static_cast<int>(std::ceil(totalBeats)); ++beat)
        {
            const auto x = gridArea.getX() + static_cast<int>(std::round(static_cast<float>(beat) * pixelsPerBeat));
            g.setColour((beat % 4) == 0 ? juce::Colour(0xff3c4a60) : juce::Colour(0xff263244));
            g.drawVerticalLine(x, static_cast<float>(rulerArea.getY()), static_cast<float>(gridArea.getBottom()));
        }
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
            const auto midiNote = module.getRootNote() + (rows - row - 1) - 18;
            const auto black = juce::MidiMessage::isMidiNoteBlack(midiNote);
            auto rowArea = juce::Rectangle<int>(keyboardArea.getX(), gridArea.getY() + row * rowHeight, keyboardArea.getWidth(), rowHeight);
            g.setColour(black ? juce::Colour(0xff202a38) : juce::Colour(0xffdbe3ec));
            g.fillRect(rowArea);
            g.setColour(black ? juce::Colour(0xff6f8194) : juce::Colour(0xff49586c));
            g.drawRect(rowArea);
            g.drawText(juce::MidiMessage::getMidiNoteName(midiNote, true, true, 3),
                       rowArea.reduced(8, 0), juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff2b3647));
            g.drawHorizontalLine(gridArea.getY() + row * rowHeight, static_cast<float>(gridArea.getX()), static_cast<float>(gridArea.getRight()));
        }

        const auto currentBeat = module.getSelectedClipPlayheadBeat();
        const auto playheadX = gridArea.getX() + static_cast<int>(std::round(currentBeat * pixelsPerBeat));
        g.setColour(juce::Colour(0x55ff6b6b));
        g.drawLine(static_cast<float>(playheadX), static_cast<float>(rulerArea.getY()),
                   static_cast<float>(playheadX), static_cast<float>(gridArea.getBottom()), 2.0f);

        for (const auto& note : module.getSelectedClipNotes())
        {
            const auto row = rows - 1 - (note.noteNumber - (module.getRootNote() - 18));
            if (! juce::isPositiveAndBelow(row, rows))
                continue;

            auto noteRect = juce::Rectangle<float>(static_cast<float>(gridArea.getX()) + note.startBeat * pixelsPerBeat + 1.0f,
                                                   static_cast<float>(gridArea.getY() + row * rowHeight + 2),
                                                   juce::jmax(8.0f, note.lengthBeats * pixelsPerBeat - 2.0f),
                                                   static_cast<float>(rowHeight - 4));
            noteRect.setRight(juce::jmin(noteRect.getRight(), static_cast<float>(gridArea.getRight() - 2)));
            g.setColour(juce::Colour(0xff6c5ce7).withAlpha(0.35f + 0.65f * note.velocity));
            g.fillRoundedRectangle(noteRect, 5.0f);
            g.setColour(note.id == selectedNoteId ? juce::Colour(0xffdbeafe) : juce::Colour(0x44ffffff));
            g.drawRoundedRectangle(noteRect, 5.0f, note.id == selectedNoteId ? 2.0f : 1.0f);
        }

        g.setColour(juce::Colour(0xff1a2230));
        g.fillRoundedRectangle(velocityArea.toFloat(), 10.0f);
        g.setColour(juce::Colour(0xff9fb0c4));
        g.drawText("Velocity", velocityArea.removeFromTop(18), juce::Justification::centredLeft);
        auto barsArea = velocityArea.reduced(4, 8);
        for (const auto& note : module.getSelectedClipNotes())
        {
            auto bar = juce::Rectangle<int>(barsArea.getX() + static_cast<int>(std::round(note.startBeat * pixelsPerBeat)) + 2,
                                            barsArea.getBottom() - static_cast<int>(std::round(note.velocity * static_cast<float>(barsArea.getHeight()))),
                                            juce::jmax(6, static_cast<int>(std::round(juce::jmax(0.25f, juce::jmin(1.0f, note.lengthBeats)) * pixelsPerBeat)) - 4),
                                            static_cast<int>(std::round(note.velocity * static_cast<float>(barsArea.getHeight()))));
            g.setColour(note.id == selectedNoteId ? juce::Colour(0xff5eead4) : juce::Colour(0xff334155));
            g.fillRoundedRectangle(bar.toFloat(), 4.0f);
        }
    }


    bool keyPressed(const juce::KeyPress& key) override
    {
        if (selectedNoteId.isEmpty())
            return false;

        if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
        {
            module.removeNote(selectedNoteId);
            selectedNoteId.clear();
            repaint();
            return true;
        }

        if (key == juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0))
        {
            if (const auto note = findSelectedNote())
            {
                selectedNoteId = juce::Uuid().toString();
                module.addOrUpdateNote(selectedNoteId, note->startBeat + snapBeats, note->lengthBeats, note->noteNumber, note->velocity);
                repaint();
                return true;
            }
        }

        if (const auto note = findSelectedNote())
        {
            auto startBeat = note->startBeat;
            auto lengthBeats = note->lengthBeats;
            auto noteNumber = note->noteNumber;
            auto velocity = note->velocity;

            if (key.getKeyCode() == juce::KeyPress::leftKey)
                startBeat -= snapBeats;
            else if (key.getKeyCode() == juce::KeyPress::rightKey)
                startBeat += snapBeats;
            else if (key.getKeyCode() == juce::KeyPress::upKey)
                noteNumber += 1;
            else if (key.getKeyCode() == juce::KeyPress::downKey)
                noteNumber -= 1;
            else
                return false;

            module.addOrUpdateNote(selectedNoteId, juce::jmax(0.0f, startBeat), lengthBeats, juce::jlimit(24, 108, noteNumber), velocity);
            repaint();
            return true;
        }

        return false;
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
        auto velocityArea = area.removeFromBottom(72);
        auto gridArea = area;
        const auto rows = 36;
        const auto totalBeats = juce::jmax(1.0f, module.getSelectedClipLengthBeats());

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
        {
            if (velocityArea.contains(event.getPosition()))
            {
                selectedNoteId = findNoteAtVelocityLane(event.x, velocityArea, gridArea, totalBeats);
                adjustVelocityAt(event.getPosition(), velocityArea);
            }
            return;
        }

        const auto rowHeight = juce::jmax(12, gridArea.getHeight() / rows);
        const auto pixelsPerBeat = static_cast<float>(gridArea.getWidth()) / totalBeats;
        if (const auto hit = findNoteAt(event.getPosition(), gridArea, rows, rowHeight, pixelsPerBeat))
        {
            selectedNoteId = hit->id;
            selectedVelocity = hit->velocity;
            noteDragMode = std::abs(event.x - static_cast<int>(std::round(static_cast<float>(gridArea.getX()) + (hit->startBeat + hit->lengthBeats) * pixelsPerBeat))) < 10
                ? NoteDragMode::resize
                : NoteDragMode::move;
            dragOriginBeat = hit->startBeat;
            dragOriginLength = hit->lengthBeats;
            dragOriginNote = hit->noteNumber;
            dragStartPosition = event.getPosition();
            if (event.mods.isRightButtonDown())
            {
                module.removeNote(hit->id);
                selectedNoteId.clear();
                noteDragMode = NoteDragMode::none;
            }
        }
        else
        {
            selectedNoteId = juce::Uuid().toString();
            dragStartPosition = event.getPosition();
            createNoteAt(event.getPosition(), gridArea, rows, rowHeight, pixelsPerBeat);
            noteDragMode = NoteDragMode::resize;
        }
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        auto area = getLocalBounds().reduced(16).withTrimmedTop(52);
        area.removeFromTop(20);
        auto velocityArea = area.removeFromBottom(72);
        auto gridArea = area;
        const auto rows = 36;
        const auto totalBeats = juce::jmax(1.0f, module.getSelectedClipLengthBeats());
        const auto pixelsPerBeat = static_cast<float>(gridArea.getWidth()) / totalBeats;
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

        if (velocityArea.contains(event.getPosition()) && selectedNoteId.isNotEmpty())
        {
            adjustVelocityAt(event.getPosition(), velocityArea);
            return;
        }

        if (gridArea.contains(event.getPosition()) && selectedNoteId.isNotEmpty())
        {
            const auto deltaBeat = static_cast<float>(event.x - dragStartPosition.x) / juce::jmax(1.0f, pixelsPerBeat);
            const auto deltaRows = (event.y - dragStartPosition.y) / juce::jmax(1, rowHeight);
            const auto targetNote = juce::jlimit(24, 108, dragOriginNote - deltaRows);
            if (noteDragMode == NoteDragMode::move)
                module.addOrUpdateNote(selectedNoteId, snapBeat(dragOriginBeat + deltaBeat), dragOriginLength, targetNote, selectedVelocity);
            else if (noteDragMode == NoteDragMode::resize)
                module.addOrUpdateNote(selectedNoteId, dragOriginBeat, juce::jmax(snapBeats, snapBeat(dragOriginLength + deltaBeat)), dragOriginNote, selectedVelocity);
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragTarget = DragTarget::none;
        noteDragMode = NoteDragMode::none;
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

    enum class NoteDragMode
    {
        none,
        move,
        resize
    };

    struct NoteHit
    {
        juce::String id;
        float startBeat = 0.0f;
        float lengthBeats = 1.0f;
        int noteNumber = 60;
        float velocity = 0.8f;
    };

    std::optional<NoteHit> findNoteAt(juce::Point<int> position,
                                      juce::Rectangle<int> gridArea,
                                      int rows,
                                      int rowHeight,
                                      float pixelsPerBeat) const
    {
        for (const auto& note : module.getSelectedClipNotes())
        {
            const auto row = rows - 1 - (note.noteNumber - (module.getRootNote() - 18));
            if (! juce::isPositiveAndBelow(row, rows))
                continue;

            auto rect = juce::Rectangle<int>(gridArea.getX() + static_cast<int>(std::round(note.startBeat * pixelsPerBeat)),
                                             gridArea.getY() + row * rowHeight,
                                             juce::jmax(8, static_cast<int>(std::round(note.lengthBeats * pixelsPerBeat))),
                                             rowHeight);
            if (rect.contains(position))
                return NoteHit { note.id, note.startBeat, note.lengthBeats, note.noteNumber, note.velocity };
        }
        return std::nullopt;
    }

    void createNoteAt(juce::Point<int> position,
                      juce::Rectangle<int> gridArea,
                      int rows,
                      int rowHeight,
                      float pixelsPerBeat)
    {
        const auto startBeat = snapBeat(juce::jmax(0.0f, static_cast<float>(position.x - gridArea.getX()) / juce::jmax(1.0f, pixelsPerBeat)));
        const auto row = juce::jlimit(0, rows - 1, (position.y - gridArea.getY()) / rowHeight);
        const auto noteNumber = module.getRootNote() + (rows - row - 1) - 18;
        selectedVelocity = 0.8f;
        dragOriginBeat = startBeat;
        dragOriginLength = 1.0f;
        dragOriginNote = noteNumber;
        module.addOrUpdateNote(selectedNoteId, startBeat, 1.0f, noteNumber, selectedVelocity);
    }

    juce::String findNoteAtVelocityLane(int x,
                                        juce::Rectangle<int> velocityArea,
                                        juce::Rectangle<int> gridArea,
                                        float totalBeats) const
    {
        auto barsArea = velocityArea.withTrimmedTop(18).reduced(4, 8);
        juce::ignoreUnused(gridArea);
        const auto pixelsPerBeat = static_cast<float>(barsArea.getWidth()) / juce::jmax(1.0f, totalBeats);
        for (const auto& note : module.getSelectedClipNotes())
        {
            auto rect = juce::Rectangle<int>(barsArea.getX() + static_cast<int>(std::round(note.startBeat * pixelsPerBeat)),
                                             barsArea.getY(),
                                             juce::jmax(6, static_cast<int>(std::round(juce::jmax(0.25f, juce::jmin(1.0f, note.lengthBeats)) * pixelsPerBeat))),
                                             barsArea.getHeight());
            if (rect.contains(x, rect.getCentreY()))
                return note.id;
        }
        return {};
    }


    float snapBeat(float beat) const
    {
        return std::round(beat / snapBeats) * snapBeats;
    }
    void adjustVelocityAt(juce::Point<int> position, juce::Rectangle<int> velocityArea)
    {
        if (selectedNoteId.isEmpty())
            return;

        const auto normalised = juce::jlimit(0.05f, 1.0f,
                                             1.0f - static_cast<float>(position.y - velocityArea.getY()) / static_cast<float>(juce::jmax(1, velocityArea.getHeight())));
        selectedVelocity = normalised;
        if (const auto note = findSelectedNote())
            module.addOrUpdateNote(selectedNoteId, note->startBeat, note->lengthBeats, note->noteNumber, normalised);
        repaint();
    }

    std::optional<NoteHit> findSelectedNote() const
    {
        for (const auto& note : module.getSelectedClipNotes())
            if (note.id == selectedNoteId)
                return NoteHit { note.id, note.startBeat, note.lengthBeats, note.noteNumber, note.velocity };
        return std::nullopt;
    }

    void timerCallback() override
    {
        titleLabel.setText("Piano Roll", juce::dontSendNotification);
        infoLabel.setText("Free-position notes with keyboard edit: delete, Cmd+D duplicate, arrows nudge.", juce::dontSendNotification);
        repaint();
    }

    MidiTrackModule& module;
    juce::Label titleLabel;
    juce::Label infoLabel;
    DragTarget dragTarget = DragTarget::none;
    NoteDragMode noteDragMode = NoteDragMode::none;
    juce::Point<int> dragStartPosition;
    juce::String selectedNoteId;
    float dragOriginBeat = 0.0f;
    float dragOriginLength = 1.0f;
    int dragOriginNote = 60;
    float selectedVelocity = 0.8f;
    float snapBeats = 0.25f;
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

std::unique_ptr<ModuleNode> createMetronomeModule()
{
    return std::make_unique<MetronomeModule>();
}

std::unique_ptr<ModuleNode> createBpmToLfoModule()
{
    return std::make_unique<BpmToLfoModule>();
}

std::unique_ptr<ModuleNode> createComparatorModule()
{
    return std::make_unique<ComparatorModule>();
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

std::unique_ptr<ModuleNode> createChannelStripModule()
{
    return std::make_unique<ChannelStripModule>();
}

std::unique_ptr<ModuleNode> createSendModule()
{
    return std::make_unique<SendModule>();
}

std::unique_ptr<ModuleNode> createReturnModule()
{
    return std::make_unique<ReturnModule>();
}

std::unique_ptr<ModuleNode> createMeterModule()
{
    return std::make_unique<MeterModule>();
}

std::unique_ptr<ModuleNode> createBusModule()
{
    return std::make_unique<BusModule>();
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
