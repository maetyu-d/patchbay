#pragma once

#include "../Engine/NodeFactory.h"
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

private:
    void addModule(const juce::String& type);
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

    PatchGraph graph;
    PatchCanvas canvas;
    TrackView trackView;

    juce::OwnedArray<juce::TextButton> moduleButtons;
    juce::TextButton saveButton { "Save" };
    juce::TextButton loadButton { "Load" };
    juce::TextButton transportButton { "Stop" };
    juce::TextButton rewindButton { "Rewind" };
    juce::TextButton addAudioTrackButton { "+ Audio" };
    juce::TextButton addMidiTrackButton { "+ MIDI" };
    juce::TextButton loadTrackClipButton { "Load Clip" };
    juce::ToggleButton trackMuteToggle { "Mute Track" };
    juce::Label hintLabel;
    juce::Label inspectorTitle;

    juce::OwnedArray<juce::Label> inspectorLabels;
    juce::OwnedArray<juce::Slider> inspectorSliders;
    juce::OwnedArray<juce::ToggleButton> inspectorStepButtons;
    std::unique_ptr<juce::FileChooser> activeFileChooser;

    std::optional<juce::Uuid> selectedNodeId;
    std::optional<juce::Uuid> selectedTrackId;
};
