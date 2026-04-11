#pragma once

#include "../Engine/ExternalPluginManager.h"
#include "../Engine/ModuleNode.h"

class ExternalPluginModule final : public ModuleNode
{
public:
    ExternalPluginModule();
    explicit ExternalPluginModule(const juce::PluginDescription&);
    ~ExternalPluginModule() override;

    juce::String getTypeId() const override;
    juce::String getDisplayName() const override;
    juce::Colour getNodeColour() const override;
    std::vector<PortInfo> getInputPorts() const override;
    std::vector<PortInfo> getOutputPorts() const override;
    std::vector<NodeParameterSpec> getParameterSpecs() const override;

    void prepare(double sampleRate, int samplesPerBlock) override;
    void process(NodeRenderContext& context) override;

    float getParameterValue(const juce::String& parameterId) const override;
    void setParameterValue(const juce::String& parameterId, float newValue) override;

    juce::ValueTree saveState() const override;
    void loadState(const juce::ValueTree& state) override;
    juce::String getResourcePath() const override;
    juce::Component* getEmbeddedEditor() override;
    juce::Rectangle<int> getEmbeddedEditorBoundsHint() const override;
    bool supportsEmbeddedEditor() const override;
    bool isEditorOpen() const override;
    void setEditorOpen(bool shouldBeOpen) override;
    bool isEditorDetached() const override;
    void setEditorDetached(bool shouldBeDetached) override;
    float getEditorScale() const override;
    void setEditorScale(float scale) override;
    void setPluginIdentifier(const juce::String& identifier);
    juce::String getPluginIdentifier() const;

private:
    void instantiateIfNeeded();
    void syncParametersToPlugin();
    void releasePlugin();
    int getNumAudioInputs() const;
    int getNumAudioOutputs() const;

    juce::PluginDescription description;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::String lastError;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    std::map<juce::String, float, std::less<>> parameterValues;
    juce::MidiBuffer midiBuffer;
    juce::AudioProcessorEditor* editor = nullptr;
    bool editorOpen = false;
    bool editorDetached = false;
    float editorScale = 1.0f;
};
