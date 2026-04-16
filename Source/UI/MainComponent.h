#pragma once

#include "../Engine/NodeFactory.h"
#include "../Engine/ExternalPluginManager.h"
#include "../Engine/PatchGraph.h"
#include "PatchCanvas.h"
#include "TrackView.h"
#include <optional>

class InspectorResizeHandle final : public juce::Component
{
public:
    std::function<void(const juce::MouseEvent&)> onMouseMoveCallback;
    std::function<void(const juce::MouseEvent&)> onMouseDownCallback;
    std::function<void(const juce::MouseEvent&)> onMouseDragCallback;
    std::function<void(const juce::MouseEvent&)> onMouseUpCallback;

    void paint(juce::Graphics& g) override
    {
        g.setColour(isMouseOverOrDragging() ? juce::Colour(0xff4f6b8a) : juce::Colour(0xff2b3647));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(3.0f, 6.0f), 2.0f);
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        if (onMouseMoveCallback)
            onMouseMoveCallback(event);
        repaint();
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (onMouseDownCallback)
            onMouseDownCallback(event);
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (onMouseDragCallback)
            onMouseDragCallback(event);
        repaint();
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (onMouseUpCallback)
            onMouseUpCallback(event);
        repaint();
    }
};

class MainComponent final : public juce::AudioAppComponent,
                            private juce::Timer
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
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    void timerCallback() override;
    void addModule(const juce::String& type, juce::Point<float> position);
    void scanExternalPlugins();
    void seedDefaultSession();
    void saveSession();
    void loadSession();
    void addAudioTrack();
    void addMidiTrack();
    void togglePlayback();
    void toggleRecording();
    void rewindTransport();
    void loadAudioIntoSelectedTrack();
    void rebuildInspector();
    void clearInspectorControls();
    void showNodeInspector(const NodeSnapshot& node);
    void showTrackInspector(const NodeSnapshot& track);
    void autoWireTrackNode(const juce::Uuid& trackId, bool isMidiTrack);
    void toggleInspectorCollapsed();
    void toggleEditMode();
    void applyModeState();

    PatchGraph graph;
    PatchCanvas canvas;
    TrackView trackView;

    juce::TextButton saveButton { "Save" };
    juce::TextButton loadButton { "Open" };
    juce::TextButton transportButton { "Edit" };
    juce::TextButton playButton { "Play" };
    juce::TextButton recordButton { "Rec" };
    juce::TextButton rewindButton { "Rewind" };
    juce::TextButton addAudioTrackButton { "Audio" };
    juce::TextButton addMidiTrackButton { "MIDI" };
    juce::TextButton scanPluginsButton { "Scan" };
    juce::TextButton toggleInspectorButton { "Hide" };
    juce::TextButton loadTrackClipButton { "Load" };
    juce::ToggleButton trackMuteToggle { "Mute" };
    juce::Label transportPositionLabel;
    juce::Label bpmLabel;
    juce::Slider bpmSlider;
    juce::ToggleButton transportLoopToggle { "Loop" };
    InspectorResizeHandle inspectorResizeHandle;
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
    int inspectorPanelWidth = 300;
    int lastExpandedInspectorWidth = 300;
    bool resizingInspector = false;
    bool inspectorCollapsed = false;
};
