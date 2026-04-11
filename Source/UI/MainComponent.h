#pragma once

#include "../Engine/NodeFactory.h"
#include "../Engine/ExternalPluginManager.h"
#include "../Engine/PatchGraph.h"
#include "PatchCanvas.h"
#include "TrackView.h"
#include <optional>

class MainComponent final : public juce::AudioAppComponent
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;
    void resized() override;
    void paint(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void addModule(const juce::String& type, juce::Point<float> position);
    void scanExternalPlugins();
    void seedDefaultSession();
    void saveSession();
    void loadSession();
    void addAudioTrack();
    void addMidiTrack();
    void toggleTransport();
    void rewindTransport();
    void loadAudioIntoSelectedTrack();
    void rebuildInspector();
    void clearInspectorControls();
    void showNodeInspector(const NodeSnapshot& node);
    void showTrackInspector(const NodeSnapshot& track);
    void toggleEditMode();
    void applyModeState();

    PatchGraph graph;
    PatchCanvas canvas;
    TrackView trackView;

    juce::TextButton saveButton { "Save" };
    juce::TextButton loadButton { "Load" };
    juce::TextButton transportButton { "Stop" };
    juce::TextButton rewindButton { "Rewind" };
    juce::TextButton addAudioTrackButton { "+ Audio" };
    juce::TextButton addMidiTrackButton { "+ MIDI" };
    juce::TextButton scanPluginsButton { "Scan Plugins" };
    juce::TextButton loadTrackClipButton { "Load Clip" };
    juce::ToggleButton trackMuteToggle { "Mute Track" };
    juce::Label hintLabel;
    juce::Label inspectorTitle;

    juce::OwnedArray<juce::Label> inspectorLabels;
    juce::OwnedArray<juce::ComboBox> inspectorComboBoxes;
    juce::OwnedArray<juce::Slider> inspectorSliders;
    juce::OwnedArray<juce::ToggleButton> inspectorToggleButtons;
    juce::OwnedArray<juce::ToggleButton> inspectorStepButtons;
    std::unique_ptr<juce::FileChooser> activeFileChooser;
    ExternalPluginManager& externalPluginManager = ExternalPluginManager::getInstance();

    std::optional<juce::Uuid> selectedNodeId;
    std::optional<juce::Uuid> selectedTrackId;
    bool editMode = false;
};
