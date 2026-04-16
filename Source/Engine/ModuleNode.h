#pragma once

#include <JuceHeader.h>

enum class PortKind
{
    audio,
    modulation
};

struct PortInfo
{
    juce::String name;
    PortKind kind {};
};

struct NodeParameterSpec
{
    juce::String id;
    juce::String name;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
};

struct NodeParameterState
{
    NodeParameterSpec spec;
    float value = 0.0f;
};

struct NodeRenderContext
{
    std::vector<juce::AudioBuffer<float>*> audioInputs;
    std::vector<juce::AudioBuffer<float>*> audioOutputs;
    std::vector<float> modInputs;
    std::vector<float> modOutputs;
    double sampleRate = 44100.0;
    int numSamples = 0;
    bool isPlaying = true;
    bool isRecording = false;
    int64_t transportSamplePosition = 0;
    double bpm = 120.0;
    int transportNumerator = 4;
    int transportDenominator = 4;
    bool transportLooping = false;
    int64_t transportLoopStartSample = 0;
    int64_t transportLoopEndSample = 0;
    bool anySoloActive = false;
    bool nodeSoloed = false;
};

class ModuleNode
{
public:
    virtual ~ModuleNode() = default;

    virtual juce::String getTypeId() const = 0;
    virtual juce::String getDisplayName() const = 0;
    virtual juce::Colour getNodeColour() const
    {
        return juce::Colour(0xff4a5568);
    }

    virtual std::vector<PortInfo> getInputPorts() const = 0;
    virtual std::vector<PortInfo> getOutputPorts() const = 0;
    virtual std::vector<NodeParameterSpec> getParameterSpecs() const
    {
        return {};
    }

    virtual void prepare(double sampleRate, int samplesPerBlock)
    {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
    }

    virtual void process(NodeRenderContext& context) = 0;

    virtual float getParameterValue(const juce::String& parameterId) const
    {
        juce::ignoreUnused(parameterId);
        return 0.0f;
    }

    virtual void setParameterValue(const juce::String& parameterId, float newValue)
    {
        juce::ignoreUnused(parameterId, newValue);
    }

    virtual bool contributesToMasterOutput() const
    {
        return false;
    }

    virtual bool isSoloed() const
    {
        return false;
    }

    virtual juce::Point<float> getMeterLevels() const
    {
        return {};
    }

    virtual bool isTrackModule() const
    {
        return false;
    }

    virtual juce::String getTrackTypeId() const
    {
        return {};
    }

    virtual juce::String getResourcePath() const
    {
        return {};
    }

    virtual bool loadFile(const juce::File& file)
    {
        juce::ignoreUnused(file);
        return false;
    }

    virtual juce::Component* getEmbeddedEditor()
    {
        return nullptr;
    }

    virtual juce::Rectangle<int> getEmbeddedEditorBoundsHint() const
    {
        return {};
    }

    virtual bool supportsEmbeddedEditor() const
    {
        return false;
    }

    virtual bool isEditorOpen() const
    {
        return false;
    }

    virtual void setEditorOpen(bool shouldBeOpen)
    {
        juce::ignoreUnused(shouldBeOpen);
    }

    virtual bool isEditorDetached() const
    {
        return false;
    }

    virtual void setEditorDetached(bool shouldBeDetached)
    {
        juce::ignoreUnused(shouldBeDetached);
    }

    virtual float getEditorScale() const
    {
        return 1.0f;
    }

    virtual void setEditorScale(float scale)
    {
        juce::ignoreUnused(scale);
    }

    virtual juce::ValueTree saveState() const
    {
        juce::ValueTree state("PARAMETERS");

        for (const auto& spec : getParameterSpecs())
        {
            juce::ValueTree parameter("PARAMETER");
            parameter.setProperty("id", spec.id, nullptr);
            parameter.setProperty("value", getParameterValue(spec.id), nullptr);
            state.appendChild(parameter, nullptr);
        }

        return state;
    }

    virtual void loadState(const juce::ValueTree& state)
    {
        for (const auto& parameter : state)
        {
            if (parameter.hasType("PARAMETER"))
                setParameterValue(parameter["id"].toString(), static_cast<float>(parameter["value"]));
        }
    }
};
