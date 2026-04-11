#pragma once

#include <JuceHeader.h>

class ExternalPluginManager final : private juce::ChangeListener
{
public:
    static ExternalPluginManager& getInstance();

    void initialise();

    juce::Array<juce::PluginDescription> getKnownPlugins() const;
    juce::StringArray getKnownPluginDisplayNames() const;
    std::optional<juce::PluginDescription> getPluginByIdentifier(const juce::String& identifier) const;
    std::optional<juce::PluginDescription> getPluginByDisplayName(const juce::String& name) const;

    bool scanForPlugins(juce::String& report);
    std::unique_ptr<juce::AudioPluginInstance> createPluginInstance(const juce::PluginDescription& description,
                                                                    double sampleRate,
                                                                    int blockSize,
                                                                    juce::String& errorMessage);

private:
    ExternalPluginManager();
    ~ExternalPluginManager() override;

    void loadKnownPluginList();
    void saveKnownPluginList();
    void setupProperties();
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    bool initialised = false;
    juce::ApplicationProperties properties;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
};
