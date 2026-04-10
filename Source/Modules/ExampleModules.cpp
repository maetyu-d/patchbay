#include "ExampleModules.h"
#include "ModuleCores.h"

namespace
{
constexpr double bpm = 120.0;

float midiToFrequency(int midiNote)
{
    return 440.0f * std::pow(2.0f, static_cast<float>(midiNote - 69) / 12.0f);
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
            { "pitchCV", PortKind::modulation },
            { "levelCV", PortKind::modulation }
        };
    }

    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "audioOut", PortKind::audio }
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
            { "value", PortKind::modulation }
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
        context.modOutputs[0] = core.advanceBlock(context.numSamples, rateHz, depth);
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
            { "audioIn", PortKind::audio },
            { "gainCV", PortKind::modulation }
        };
    }

    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "audioOut", PortKind::audio }
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
            { "audioIn", PortKind::audio }
        };
    }

    std::vector<PortInfo> getOutputPorts() const override
    {
        return {
            { "monitor", PortKind::audio }
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

class AudioTrackModule final : public ModuleNode
{
public:
    AudioTrackModule()
    {
        formatManager.registerBasicFormats();
    }

    juce::String getTypeId() const override { return "AudioTrack"; }
    juce::String getDisplayName() const override { return "Audio Track"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xff4d96ff); }

    std::vector<PortInfo> getInputPorts() const override { return {}; }
    std::vector<PortInfo> getOutputPorts() const override { return { { "audioOut", PortKind::audio } }; }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        return {
            { "volume", "Volume", 0.0f, 1.5f, 0.8f },
            { "pan", "Pan", -1.0f, 1.0f, 0.0f },
            { "mute", "Mute", 0.0f, 1.0f, 0.0f }
        };
    }

    void process(NodeRenderContext& context) override
    {
        auto* output = context.audioOutputs.front();
        output->clear();

        if (! context.isPlaying || mute > 0.5f || audioClip.getNumSamples() == 0)
            return;

        const auto ratio = audioClipSampleRate / context.sampleRate;
        const auto leftGain = volume * juce::jlimit(0.0f, 1.0f, 1.0f - juce::jmax(0.0f, pan));
        const auto rightGain = volume * juce::jlimit(0.0f, 1.0f, 1.0f + juce::jmin(0.0f, pan));

        for (int sample = 0; sample < context.numSamples; ++sample)
        {
            const auto clipPosition = std::fmod((static_cast<double>(context.transportSamplePosition + sample) * ratio),
                                                static_cast<double>(audioClip.getNumSamples()));
            const auto clipIndex = static_cast<int>(clipPosition);
            const auto nextIndex = (clipIndex + 1) % audioClip.getNumSamples();
            const auto fraction = static_cast<float>(clipPosition - static_cast<double>(clipIndex));

            const auto left = juce::jmap(fraction, audioClip.getSample(0, clipIndex), audioClip.getSample(0, nextIndex));
            const auto rightChannel = juce::jmin(1, audioClip.getNumChannels() - 1);
            const auto right = juce::jmap(fraction, audioClip.getSample(rightChannel, clipIndex), audioClip.getSample(rightChannel, nextIndex));

            output->addSample(0, sample, left * leftGain);
            if (output->getNumChannels() > 1)
                output->addSample(1, sample, right * rightGain);
        }
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "volume") return volume;
        if (parameterId == "pan") return pan;
        if (parameterId == "mute") return mute;
        return 0.0f;
    }

    void setParameterValue(const juce::String& parameterId, float newValue) override
    {
        if (parameterId == "volume") volume = juce::jlimit(0.0f, 1.5f, newValue);
        else if (parameterId == "pan") pan = juce::jlimit(-1.0f, 1.0f, newValue);
        else if (parameterId == "mute") mute = juce::jlimit(0.0f, 1.0f, newValue);
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

    juce::ValueTree saveState() const override
    {
        auto state = ModuleNode::saveState();
        state.setProperty("filePath", filePath, nullptr);
        return state;
    }

    void loadState(const juce::ValueTree& state) override
    {
        ModuleNode::loadState(state);
        const auto path = state.getProperty("filePath").toString();
        if (path.isNotEmpty())
            loadFile(juce::File(path));
    }

private:
    juce::AudioFormatManager formatManager;
    juce::AudioBuffer<float> audioClip;
    double audioClipSampleRate = 44100.0;
    juce::String filePath;
    float volume = 0.8f;
    float pan = 0.0f;
    float mute = 0.0f;
};

class MidiTrackModule final : public ModuleNode
{
public:
    juce::String getTypeId() const override { return "MidiTrack"; }
    juce::String getDisplayName() const override { return "MIDI Track"; }
    juce::Colour getNodeColour() const override { return juce::Colour(0xff7c5cff); }

    std::vector<PortInfo> getInputPorts() const override { return {}; }
    std::vector<PortInfo> getOutputPorts() const override { return { { "audioOut", PortKind::audio } }; }

    std::vector<NodeParameterSpec> getParameterSpecs() const override
    {
        std::vector<NodeParameterSpec> specs {
            { "volume", "Volume", 0.0f, 1.5f, 0.8f },
            { "pan", "Pan", -1.0f, 1.0f, 0.0f },
            { "mute", "Mute", 0.0f, 1.0f, 0.0f },
            { "rootNote", "Root Note", 36.0f, 84.0f, 60.0f },
            { "gain", "MIDI Gain", 0.0f, 1.0f, 0.2f }
        };

        for (int step = 0; step < 16; ++step)
            specs.push_back({ "step" + juce::String(step + 1), "S" + juce::String(step + 1), 0.0f, 1.0f, defaultSteps[static_cast<size_t>(step)] ? 1.0f : 0.0f });

        return specs;
    }

    void process(NodeRenderContext& context) override
    {
        auto* output = context.audioOutputs.front();
        output->clear();

        if (! context.isPlaying || mute > 0.5f)
            return;

        constexpr std::array<int, 8> melody = { 0, 2, 4, 7, 9, 7, 4, 2 };
        const auto samplesPerBeat = context.sampleRate * 60.0 / bpm;
        const auto samplesPerStep = samplesPerBeat / 4.0;
        const auto leftGain = volume * juce::jlimit(0.0f, 1.0f, 1.0f - juce::jmax(0.0f, pan));
        const auto rightGain = volume * juce::jlimit(0.0f, 1.0f, 1.0f + juce::jmin(0.0f, pan));

        for (int sample = 0; sample < context.numSamples; ++sample)
        {
            const auto absoluteSample = context.transportSamplePosition + sample;
            const auto step = static_cast<int>(static_cast<double>(absoluteSample) / samplesPerStep) % static_cast<int>(steps.size());
            const auto positionWithinStep = std::fmod(static_cast<double>(absoluteSample), samplesPerStep) / samplesPerStep;

            if (! steps[static_cast<size_t>(step)] || positionWithinStep > 0.78)
                continue;

            const auto note = rootNote + melody[static_cast<size_t>(step % static_cast<int>(melody.size()))];
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
        }
    }

    float getParameterValue(const juce::String& parameterId) const override
    {
        if (parameterId == "volume") return volume;
        if (parameterId == "pan") return pan;
        if (parameterId == "mute") return mute;
        if (parameterId == "rootNote") return static_cast<float>(rootNote);
        if (parameterId == "gain") return gain;
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
        else if (parameterId == "rootNote") rootNote = juce::jlimit(36, 84, static_cast<int>(std::round(newValue)));
        else if (parameterId == "gain") gain = juce::jlimit(0.0f, 1.0f, newValue);
        else if (parameterId.startsWith("step"))
        {
            const auto index = parameterId.fromFirstOccurrenceOf("step", false, false).getIntValue() - 1;
            if (juce::isPositiveAndBelow(index, static_cast<int>(steps.size())))
                steps[static_cast<size_t>(index)] = newValue > 0.5f;
        }
    }

    bool isTrackModule() const override { return true; }
    juce::String getTrackTypeId() const override { return "midi"; }

private:
    std::array<bool, 16> steps { true, false, false, true, false, true, false, false,
                                 true, false, true, false, false, true, false, false };
    static constexpr std::array<bool, 16> defaultSteps { true, false, false, true, false, true, false, false,
                                                         true, false, true, false, false, true, false, false };
    float phase = 0.0f;
    float volume = 0.8f;
    float pan = 0.0f;
    float mute = 0.0f;
    int rootNote = 60;
    float gain = 0.2f;
};
} // namespace

std::unique_ptr<ModuleNode> createOscillatorModule()
{
    return std::make_unique<OscillatorModule>();
}

std::unique_ptr<ModuleNode> createLfoModule()
{
    return std::make_unique<LfoModule>();
}

std::unique_ptr<ModuleNode> createGainModule()
{
    return std::make_unique<GainModule>();
}

std::unique_ptr<ModuleNode> createOutputModule()
{
    return std::make_unique<OutputModule>();
}

std::unique_ptr<ModuleNode> createAudioTrackModule()
{
    return std::make_unique<AudioTrackModule>();
}

std::unique_ptr<ModuleNode> createMidiTrackModule()
{
    return std::make_unique<MidiTrackModule>();
}
