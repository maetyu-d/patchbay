#include "MainComponent.h"

namespace
{
constexpr int toolbarHeight = 48;
constexpr int trackAreaHeight = 190;
constexpr int minInspectorWidth = 0;
constexpr int maxInspectorWidth = 520;
constexpr int inspectorResizeHandleWidth = 10;

std::optional<NodeSnapshot> findNodeOfType(const std::vector<NodeSnapshot>& nodes, const juce::String& typeId)
{
    for (const auto& node : nodes)
        if (node.typeId == typeId)
            return node;

    return std::nullopt;
}

std::optional<int> findFirstFreeInputPort(const NodeSnapshot& node,
                                          PortKind kind,
                                          const std::vector<GraphConnection>& connections)
{
    const auto portCount = static_cast<int>(std::count_if(node.inputs.begin(), node.inputs.end(),
                                                          [kind](const auto& port) { return port.kind == kind; }));

    for (int portIndex = 0; portIndex < portCount; ++portIndex)
    {
        const auto alreadyUsed = std::any_of(connections.begin(), connections.end(),
                                             [&node, kind, portIndex](const auto& connection)
                                             {
                                                 return connection.destination.nodeId == node.id
                                                     && connection.destination.kind == kind
                                                     && connection.destination.portIndex == portIndex;
                                             });

        if (! alreadyUsed)
            return portIndex;
    }

    return std::nullopt;
}

bool isEssentialNodeParameter(const juce::String& typeId, const juce::String& parameterId)
{
    if (typeId == "Oscillator") return parameterId == "frequency" || parameterId == "level";
    if (typeId == "LFO") return parameterId == "rate" || parameterId == "depth";
    if (typeId == "Filter") return parameterId == "mode" || parameterId == "cutoff" || parameterId == "resonance";
    if (typeId == "Gain") return parameterId == "gain";
    if (typeId == "Output") return parameterId == "trim" || parameterId == "ceiling";
    if (typeId == "Sum") return parameterId == "channels" || parameterId == "trim";
    if (typeId == "Router") return parameterId == "destinations" || parameterId == "activeRoute";
    if (typeId == "TimeSignature") return parameterId == "numerator" || parameterId == "denominator";
    if (typeId == "BpmToLfo") return parameterId == "bpm";
    if (typeId == "AD") return parameterId == "attack" || parameterId == "decay";
    if (typeId == "ADSR") return parameterId == "attack" || parameterId == "decay" || parameterId == "sustain" || parameterId == "release";
    if (typeId == "ChannelStrip") return parameterId == "gain" || parameterId == "pan" || parameterId == "send";
    if (typeId == "Send") return parameterId == "amount";
    if (typeId == "Return") return parameterId == "level";
    if (typeId == "Bus") return parameterId == "channels" || parameterId == "trim";
    return false;
}

bool isEssentialTrackParameter(const juce::String& trackTypeId, const juce::String& parameterId)
{
    if (parameterId == "volume" || parameterId == "pan" || parameterId == "clipStartBar" || parameterId == "clipBars")
        return true;

    if (trackTypeId == "audio")
        return parameterId == "startPoint" || parameterId == "endPoint" || parameterId == "fadeIn" || parameterId == "fadeOut";

    if (trackTypeId == "midi")
        return parameterId == "rootNote" || parameterId == "gain" || parameterId == "startPoint" || parameterId == "endPoint";

    return false;
}

enum class ThreeWayDialogChoice
{
    first,
    second,
    third
};

ThreeWayDialogChoice mapYesNoCancelResult(int result)
{
    const auto usesNativeAlerts = juce::LookAndFeel::getDefaultLookAndFeel().isUsingNativeAlertWindows();

    if (usesNativeAlerts)
    {
        if (result == 0) return ThreeWayDialogChoice::first;
        if (result == 1) return ThreeWayDialogChoice::second;
        return ThreeWayDialogChoice::third;
    }

    if (result == 1) return ThreeWayDialogChoice::first;
    if (result == 2) return ThreeWayDialogChoice::second;
    return ThreeWayDialogChoice::third;
}

ThreeWayDialogChoice showMappedYesNoCancelBox(juce::MessageBoxIconType iconType,
                                              const juce::String& title,
                                              const juce::String& message,
                                              const juce::String& firstButton,
                                              const juce::String& secondButton,
                                              const juce::String& thirdButton,
                                              juce::Component* associatedComponent)
{
    const auto result = juce::AlertWindow::showYesNoCancelBox(iconType,
                                                              title,
                                                              message,
                                                              firstButton,
                                                              secondButton,
                                                              thirdButton,
                                                              associatedComponent,
                                                              nullptr);
    return mapYesNoCancelResult(result);
}
}

MainComponent::MainComponent() : canvas(graph), trackView(graph)
{
    auto styleButton = [](juce::Button& button, juce::Colour colour)
    {
        button.setColour(juce::TextButton::buttonColourId, colour);
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    };

    styleButton(newButton, juce::Colour(0xff2b3a4f));
    styleButton(saveButton, juce::Colour(0xff2b3a4f));
    styleButton(loadButton, juce::Colour(0xff2b3a4f));
    styleButton(transportButton, juce::Colour(0xff355c50));
    styleButton(playButton, juce::Colour(0xff35576d));
    styleButton(recordButton, juce::Colour(0xff7a2f35));
    styleButton(rewindButton, juce::Colour(0xff60483b));
    styleButton(scanPluginsButton, juce::Colour(0xff41644a));
    styleButton(toggleInspectorButton, juce::Colour(0xff4c5368));
    styleButton(inspectorDetailButton, juce::Colour(0xff2b3a4f));
    styleButton(loadTrackClipButton, juce::Colour(0xff35576d));

    externalPluginManager.initialise();
    graph.addChangeListener(this);
    autosaveFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("PatchBayDAW")
                       .getChildFile("autosave.patchbay");
    autosaveFile.getParentDirectory().createDirectory();

    newButton.onClick = [this] { newSession(); };
    saveButton.onClick = [this] { saveSession(); };
    loadButton.onClick = [this] { loadSession(); };
    transportButton.onClick = [this] { toggleEditMode(); };
    playButton.onClick = [this] { togglePlayback(); };
    recordButton.onClick = [this] { toggleRecording(); };
    rewindButton.onClick = [this] { rewindTransport(); };
    scanPluginsButton.onClick = [this] { scanExternalPlugins(); };
    toggleInspectorButton.onClick = [this] { toggleInspectorCollapsed(); };
    inspectorDetailButton.onClick = [this]
    {
        inspectorShowAdvanced = ! inspectorShowAdvanced;
        rebuildInspector();
    };
    loadTrackClipButton.onClick = [this] { loadAudioIntoSelectedTrack(); };
    trackMuteToggle.onClick = [this]
    {
        if (selectedTrackId.has_value())
            graph.setNodeParameter(*selectedTrackId, "mute", trackMuteToggle.getToggleState() ? 1.0f : 0.0f);
    };

    addAndMakeVisible(newButton);
    addAndMakeVisible(saveButton);
    addAndMakeVisible(loadButton);
    addAndMakeVisible(transportButton);
    addAndMakeVisible(playButton);
    addAndMakeVisible(recordButton);
    addAndMakeVisible(rewindButton);
    addAndMakeVisible(scanPluginsButton);
    addAndMakeVisible(toggleInspectorButton);
    addAndMakeVisible(inspectorDetailButton);
    addAndMakeVisible(transportPositionLabel);
    addAndMakeVisible(bpmLabel);
    addAndMakeVisible(bpmSlider);
    addAndMakeVisible(transportLoopToggle);
    addAndMakeVisible(hintLabel);
    addAndMakeVisible(trackView);
    addAndMakeVisible(canvas);
    addAndMakeVisible(inspectorResizeHandle);
    addAndMakeVisible(inspectorTitle);
    addAndMakeVisible(loadTrackClipButton);
    addAndMakeVisible(trackMuteToggle);

    hintLabel.setText("Patch mode: right-click to add modules, then drag between sockets to connect them.", juce::dontSendNotification);
    hintLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9fadb9));
    transportPositionLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    transportPositionLabel.setJustificationType(juce::Justification::centredLeft);
    bpmLabel.setText("BPM", juce::dontSendNotification);
    bpmLabel.setColour(juce::Label::textColourId, juce::Colour(0xffdbe3ec));
    bpmSlider.setRange(30.0, 240.0, 0.1);
    bpmSlider.setSkewFactorFromMidPoint(120.0);
    bpmSlider.setValue(graph.getTransportState().bpm, juce::dontSendNotification);
    bpmSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 20);
    bpmSlider.onValueChange = [this] { graph.setTransportBpm(bpmSlider.getValue()); };
    transportLoopToggle.onClick = [this] { graph.setTransportLoopEnabled(transportLoopToggle.getToggleState()); };
    inspectorTitle.setText("Selection", juce::dontSendNotification);
    inspectorTitle.setColour(juce::Label::textColourId, juce::Colours::white);
    inspectorTitle.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    inspectorResizeHandle.setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    inspectorResizeHandle.onMouseMoveCallback = [this](const juce::MouseEvent& event) { mouseMove(event.getEventRelativeTo(this)); };
    inspectorResizeHandle.onMouseDownCallback = [this](const juce::MouseEvent& event) { mouseDown(event.getEventRelativeTo(this)); };
    inspectorResizeHandle.onMouseDragCallback = [this](const juce::MouseEvent& event) { mouseDrag(event.getEventRelativeTo(this)); };
    inspectorResizeHandle.onMouseUpCallback = [this](const juce::MouseEvent& event) { mouseUp(event.getEventRelativeTo(this)); };

    canvas.setSelectionChangedCallback([this](std::optional<juce::Uuid> nodeId)
    {
        selectedNodeId = std::move(nodeId);
        inspectorShowAdvanced = false;
        if (selectedNodeId.has_value())
        {
            selectedTrackId.reset();
            trackView.setSelectedTrack(std::nullopt);
        }
        rebuildInspector();
    });

    canvas.setCreateNodeCallback([this](const juce::String& type, juce::Point<float> position)
    {
        addModule(type, position);
    });

    canvas.setToggleEditModeCallback([this]
    {
        toggleEditMode();
    });

    trackView.setSelectionChangedCallback([this](std::optional<juce::Uuid> trackId)
    {
        selectedTrackId = std::move(trackId);
        inspectorShowAdvanced = false;
        if (selectedTrackId.has_value())
        {
            selectedNodeId.reset();
            if (canvas.getSelectedNode().has_value())
                canvas.clearSelection();
        }
        rebuildInspector();
    });

    setAudioChannels(0, 2);
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setSize(1500, 940);
    editMode = true;
    applyModeState();
    maybeRestoreAutosave();
    startTimerHz(24);
}

MainComponent::~MainComponent()
{
    graph.removeChangeListener(this);
    shutdownAudio();
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    graph.prepare(sampleRate, samplesPerBlockExpected);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (bufferToFill.buffer == nullptr)
        return;

    juce::AudioBuffer<float> temp(bufferToFill.buffer->getArrayOfWritePointers(),
                                  bufferToFill.buffer->getNumChannels(),
                                  bufferToFill.startSample,
                                  bufferToFill.numSamples);

    graph.render(temp);
}

void MainComponent::releaseResources()
{
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    auto toolbar = bounds.removeFromTop(toolbarHeight);

    newButton.setBounds(toolbar.removeFromLeft(76).reduced(4));
    saveButton.setBounds(toolbar.removeFromLeft(80).reduced(4));
    loadButton.setBounds(toolbar.removeFromLeft(80).reduced(4));
    transportButton.setBounds(toolbar.removeFromLeft(92).reduced(4));
    playButton.setBounds(toolbar.removeFromLeft(78).reduced(4));
    recordButton.setBounds(toolbar.removeFromLeft(86).reduced(4));
    rewindButton.setBounds(toolbar.removeFromLeft(88).reduced(4));
    scanPluginsButton.setBounds(toolbar.removeFromLeft(132).reduced(4));
    toggleInspectorButton.setBounds(toolbar.removeFromLeft(104).reduced(4));
    transportLoopToggle.setBounds(toolbar.removeFromLeft(64).reduced(4));
    bpmLabel.setBounds(toolbar.removeFromLeft(38).reduced(4));
    bpmSlider.setBounds(toolbar.removeFromLeft(180).reduced(4));
    transportPositionLabel.setBounds(toolbar.removeFromLeft(160).reduced(4));

    const auto activeInspectorWidth = inspectorCollapsed ? 0 : inspectorPanelWidth;
    auto inspector = bounds.removeFromRight(activeInspectorWidth);
    hintLabel.setBounds(bounds.removeFromTop(28));
    trackView.setBounds(bounds.removeFromTop(trackAreaHeight));
    bounds.removeFromTop(8);
    inspectorResizeHandle.setBounds((inspectorCollapsed ? getWidth() - 12 : inspector.getX() - inspectorResizeHandleWidth / 2),
                                    toolbarHeight + 32,
                                    inspectorResizeHandleWidth,
                                    getHeight() - toolbarHeight - 44);
    canvas.setBounds(bounds);

    const auto inspectorVisible = ! inspectorCollapsed;
    inspectorTitle.setVisible(inspectorVisible);
    inspectorDetailButton.setVisible(inspectorVisible && inspectorHasAdvanced);
    loadTrackClipButton.setVisible(loadTrackClipButton.isVisible() && inspectorVisible);
    trackMuteToggle.setVisible(trackMuteToggle.isVisible() && inspectorVisible);
    for (auto* combo : inspectorComboBoxes) combo->setVisible(inspectorVisible);
    for (auto* toggle : inspectorToggleButtons) toggle->setVisible(inspectorVisible);
    for (auto* label : inspectorLabels) label->setVisible(inspectorVisible);
    for (auto* slider : inspectorSliders) slider->setVisible(inspectorVisible);
    for (auto* button : inspectorStepButtons) button->setVisible(inspectorVisible);

    if (! inspectorVisible)
    {
        inspectorTitle.setBounds({});
        inspectorDetailButton.setBounds({});
        loadTrackClipButton.setBounds({});
        trackMuteToggle.setBounds({});
        for (auto* combo : inspectorComboBoxes) combo->setBounds({});
        for (auto* toggle : inspectorToggleButtons) toggle->setBounds({});
        for (auto* label : inspectorLabels) label->setBounds({});
        for (auto* slider : inspectorSliders) slider->setBounds({});
        for (auto* button : inspectorStepButtons) button->setBounds({});
        return;
    }

    auto inspectorHeader = inspector.removeFromTop(30);
    inspectorTitle.setBounds(inspectorHeader.removeFromLeft(juce::jmax(120, inspectorHeader.getWidth() - (inspectorHasAdvanced ? 112 : 0))));
    if (inspectorHasAdvanced)
        inspectorDetailButton.setBounds(inspectorHeader.reduced(4, 0));
    else
        inspectorDetailButton.setBounds({});
    if (loadTrackClipButton.isVisible())
        loadTrackClipButton.setBounds(inspector.removeFromTop(34).reduced(4));
    else
        loadTrackClipButton.setBounds({});

    if (trackMuteToggle.isVisible())
        trackMuteToggle.setBounds(inspector.removeFromTop(28).reduced(6, 2));
    else
        trackMuteToggle.setBounds({});

    for (auto* combo : inspectorComboBoxes)
        combo->setBounds(inspector.removeFromTop(30).reduced(6, 2));

    for (auto* toggle : inspectorToggleButtons)
        toggle->setBounds(inspector.removeFromTop(30).reduced(6, 2));

    for (int index = 0; index < inspectorLabels.size(); ++index)
    {
        inspectorLabels[index]->setBounds(inspector.removeFromTop(20).reduced(4, 0));
        if (index < inspectorSliders.size())
            inspectorSliders[index]->setBounds(inspector.removeFromTop(54).reduced(4, 2));
    }

    auto stepRows = inspector.removeFromTop(132);
    const auto rowHeight = 30;
    const auto columns = 4;
    const auto cellWidth = juce::jmax(20, stepRows.getWidth() / columns);

    for (int index = 0; index < inspectorStepButtons.size(); ++index)
    {
        const auto row = index / columns;
        const auto column = index % columns;
        inspectorStepButtons[index]->setBounds(stepRows.getX() + column * cellWidth + 2,
                                               stepRows.getY() + row * rowHeight,
                                               cellWidth - 4,
                                               rowHeight - 4);
    }
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff10141c));

    auto bounds = getLocalBounds().toFloat().reduced(12.0f);
    auto inspector = bounds.removeFromRight(static_cast<float>(inspectorCollapsed ? 0 : inspectorPanelWidth));
    bounds.removeFromTop(static_cast<float>(toolbarHeight) + 6.0f);
    bounds.removeFromTop(28.0f);

    if (! inspectorCollapsed)
    {
        g.setColour(juce::Colour(0xff171d28));
        g.fillRoundedRectangle(inspector, 14.0f);
    }
    g.fillRoundedRectangle(bounds.removeFromTop(static_cast<float>(trackAreaHeight)), 14.0f);

    if (graph.getNodes().empty())
    {
        auto emptyState = canvas.getBounds().toFloat().reduced(48.0f);
        g.setColour(juce::Colours::white.withAlpha(0.96f));
        g.setFont(juce::FontOptions(28.0f, juce::Font::bold));
        g.drawText("Start with one module.", emptyState.removeFromTop(38), juce::Justification::centred);

        g.setColour(juce::Colour(0xff9fb0c4));
        g.setFont(juce::FontOptions(15.0f));
        g.drawText("Right-click anywhere in the canvas to add tracks, sound sources, effects, or control modules.",
                   emptyState.removeFromTop(26),
                   juce::Justification::centred);
        g.drawText("Tracks are modules too: add an Audio Track or MIDI Track from the same menu.",
                   emptyState.removeFromTop(24),
                   juce::Justification::centred);
    }

}

void MainComponent::timerCallback()
{
    const auto transport = graph.getTransportState();
    const auto beatsPerBar = static_cast<double>(transport.numerator) * (4.0 / static_cast<double>(juce::jmax(1, transport.denominator)));
    const auto samplesPerBeat = (60.0 / juce::jmax(1.0, transport.bpm)) * transport.sampleRate;
    const auto totalBeats = samplesPerBeat > 0.0 ? static_cast<double>(transport.transportSamplePosition) / samplesPerBeat : 0.0;
    const auto bar = static_cast<int>(std::floor(totalBeats / juce::jmax(0.25, beatsPerBar))) + 1;
    const auto beat = static_cast<int>(std::floor(std::fmod(totalBeats, juce::jmax(0.25, beatsPerBar)))) + 1;
    transportPositionLabel.setText(juce::String(bar) + "." + juce::String(beat), juce::dontSendNotification);
    bpmSlider.setValue(transport.bpm, juce::dontSendNotification);
    transportLoopToggle.setToggleState(transport.loopEnabled, juce::dontSendNotification);
    playButton.setButtonText(transport.isPlaying ? "Stop" : "Start");
    recordButton.setButtonText(transport.isRecording ? "Recording" : "Record");
    recordButton.setColour(juce::TextButton::buttonColourId, transport.isRecording ? juce::Colour(0xffc2414b) : juce::Colour(0xff7a2f35));

    if (dirty)
    {
        ++autosaveTickCounter;
        if (autosaveTickCounter >= 24 * 10)
        {
            autosaveTickCounter = 0;
            writeAutosaveSnapshot();
        }
    }
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source != &graph || suppressDirtyTracking)
        return;

    dirty = true;
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress('e', juce::ModifierKeys::commandModifier, 0)
        || key == juce::KeyPress('E', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        toggleEditMode();
        return true;
    }

    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
        return graph.undo();

    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)
        || key == juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0))
        return graph.redo();

    return juce::AudioAppComponent::keyPressed(key);
}

void MainComponent::mouseMove(const juce::MouseEvent& event)
{
    const auto dividerX = getWidth() - inspectorPanelWidth - 12;
    const auto isOverDivider = std::abs(event.x - dividerX) <= inspectorResizeHandleWidth
        && event.y > toolbarHeight + 24;
    setMouseCursor(isOverDivider || resizingInspector ? juce::MouseCursor::LeftRightResizeCursor
                                                      : juce::MouseCursor::NormalCursor);
}

void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    const auto dividerX = getWidth() - inspectorPanelWidth - 12;
    resizingInspector = std::abs(event.x - dividerX) <= inspectorResizeHandleWidth
        && event.y > toolbarHeight + 24;
}

void MainComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (! resizingInspector)
        return;

    inspectorPanelWidth = juce::jlimit(minInspectorWidth,
                                       juce::jmin(maxInspectorWidth, getWidth() - 360),
                                       getWidth() - event.x - 12);
    inspectorCollapsed = inspectorPanelWidth <= 0;
    if (! inspectorCollapsed)
        lastExpandedInspectorWidth = inspectorPanelWidth;
    toggleInspectorButton.setButtonText(inspectorCollapsed ? "Show Inspector" : "Hide Inspector");
    resized();
    repaint();
}

void MainComponent::mouseUp(const juce::MouseEvent&)
{
    resizingInspector = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void MainComponent::addModule(const juce::String& type, juce::Point<float> position)
{
    if (auto node = NodeFactory::create(type))
    {
        const auto nodeId = graph.addNode(std::move(node), position);
        selectedNodeId = nodeId;
        selectedTrackId.reset();
        trackView.setSelectedTrack(std::nullopt);
        canvas.setSelectedNode(nodeId);
        if (type == "AudioTrack")
            autoWireTrackNode(nodeId, false);
        else if (type == "MidiTrack")
            autoWireTrackNode(nodeId, true);
        canvas.grabKeyboardFocus();
        rebuildInspector();
    }
}

void MainComponent::scanExternalPlugins()
{
    juce::String report;
    externalPluginManager.scanForPlugins(report);
    hintLabel.setText("Plug-in scan finished. Add a Plugin module from the canvas menu to use a scanned AU or VST3.", juce::dontSendNotification);
}

void MainComponent::seedDefaultSession() {}

void MainComponent::newSession()
{
    if (confirmAbandonChanges("Start New Session?",
                              "The current patch has unsaved changes. Save it, discard it, or cancel creating a new blank session.")
        == CloseDecision::cancel)
        return;

    juce::ValueTree state("PATCHBAY_SESSION");
    juce::ValueTree graphState("PATCH_GRAPH");
    graphState.setProperty("playing", false, nullptr);
    graphState.setProperty("bpm", 120.0, nullptr);
    graphState.setProperty("numerator", 4, nullptr);
    graphState.setProperty("denominator", 4, nullptr);
    graphState.setProperty("loopEnabled", false, nullptr);
    graphState.setProperty("loopStartBar", 1, nullptr);
    graphState.setProperty("loopEndBar", 5, nullptr);
    state.appendChild(graphState, nullptr);

    currentSessionFile = juce::File();
    loadSessionState(state);
    dirty = false;
    autosaveTickCounter = 0;
    clearAutosaveSnapshot();
}

void MainComponent::saveSession()
{
    saveSessionAsync({});
}

void MainComponent::saveSessionAsync(std::function<void(bool)> callback)
{
    activeFileChooser = std::make_unique<juce::FileChooser>("Save PatchBay session", juce::File(), "*.patchbay", true, false, this);
    activeFileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                       | juce::FileBrowserComponent::canSelectFiles
                                       | juce::FileBrowserComponent::warnAboutOverwriting,
                                   [this, completion = std::move(callback)](const juce::FileChooser& chooser) mutable
                                   {
                                       const auto file = chooser.getResult();
                                       activeFileChooser.reset();

                                       if (file == juce::File())
                                       {
                                           if (completion)
                                               completion(false);
                                           return;
                                       }

                                       saveSessionToFile(file);

                                       if (completion)
                                           completion(! dirty);
                                   });
}

void MainComponent::loadSession()
{
    activeFileChooser = std::make_unique<juce::FileChooser>("Load PatchBay session", juce::File(), "*.patchbay", true, false, this);
    activeFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [this](const juce::FileChooser& chooser)
                                   {
                                       const auto file = chooser.getResult();
                                       activeFileChooser.reset();

                                       if (file == juce::File())
                                           return;

                                       std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
                                       if (xml == nullptr)
                                           return;

                                       const auto state = juce::ValueTree::fromXml(*xml);
                                       if (! state.isValid())
                                           return;

                                       loadSessionFromFile(file);
                                   });
}

void MainComponent::saveSessionToFile(const juce::File& file)
{
    auto target = file;
    if (target.getFileExtension() != ".patchbay")
        target = target.withFileExtension(".patchbay");

    if (auto xml = createSessionState().createXml())
    {
        xml->writeTo(target);
        currentSessionFile = target;
        dirty = false;
        autosaveTickCounter = 0;
        clearAutosaveSnapshot();
    }
}

void MainComponent::loadSessionFromFile(const juce::File& file)
{
    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
    if (xml == nullptr)
        return;

    const auto state = juce::ValueTree::fromXml(*xml);
    if (! state.isValid())
        return;

    currentSessionFile = file;
    loadSessionState(state);
    dirty = false;
    autosaveTickCounter = 0;
    clearAutosaveSnapshot();
}

void MainComponent::togglePlayback()
{
    const auto shouldPlay = ! graph.isPlaying();
    const auto previousSuppression = suppressDirtyTracking;
    suppressDirtyTracking = true;
    graph.setPlaying(shouldPlay);
    suppressDirtyTracking = previousSuppression;
}

void MainComponent::toggleRecording()
{
    if (editMode)
        toggleEditMode();

    const auto previousSuppression = suppressDirtyTracking;
    suppressDirtyTracking = true;
    graph.setRecording(! graph.isRecording());
    suppressDirtyTracking = previousSuppression;
}

void MainComponent::rewindTransport()
{
    const auto previousSuppression = suppressDirtyTracking;
    suppressDirtyTracking = true;
    graph.resetTransport();
    suppressDirtyTracking = previousSuppression;
}

void MainComponent::loadAudioIntoSelectedTrack()
{
    if (! selectedTrackId.has_value())
        return;

    const auto trackId = *selectedTrackId;
    activeFileChooser = std::make_unique<juce::FileChooser>("Load audio clip", juce::File(), "*.wav;*.aif;*.aiff;*.mp3", true, false, this);
    activeFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [this, trackId](const juce::FileChooser& chooser)
                                   {
                                       const auto file = chooser.getResult();
                                       activeFileChooser.reset();

                                       if (file == juce::File())
                                           return;

                                       graph.loadNodeFile(trackId, file);
                                       rebuildInspector();
                                   });
}

void MainComponent::rebuildInspector()
{
    clearInspectorControls();

    loadTrackClipButton.setVisible(false);
    trackMuteToggle.setVisible(false);

    if (selectedNodeId.has_value())
    {
        if (const auto node = graph.getNode(*selectedNodeId))
            showNodeInspector(*node);
    }
    else if (selectedTrackId.has_value())
    {
        const auto tracks = graph.getNodes();
        for (const auto& track : tracks)
        {
            if (track.id == *selectedTrackId && track.isTrack)
            {
                showTrackInspector(track);
                break;
            }
        }
    }
    else
    {
        inspectorTitle.setText("Selection", juce::dontSendNotification);
    }

    resized();
}

juce::ValueTree MainComponent::createSessionState() const
{
    juce::ValueTree state("PATCHBAY_SESSION");
    state.appendChild(graph.createState(), nullptr);
    if (currentSessionFile.existsAsFile())
        state.setProperty("sessionFile", currentSessionFile.getFullPathName(), nullptr);
    return state;
}

void MainComponent::loadSessionState(const juce::ValueTree& state)
{
    const auto previousSuppression = suppressDirtyTracking;
    suppressDirtyTracking = true;
    graph.loadState(state.getChildWithName("PATCH_GRAPH"));
    suppressDirtyTracking = previousSuppression;

    selectedNodeId.reset();
    selectedTrackId.reset();
    canvas.clearSelection();
    trackView.setSelectedTrack(std::nullopt);
    rebuildInspector();
}

void MainComponent::writeAutosaveSnapshot()
{
    if (! dirty)
        return;

    if (auto xml = createSessionState().createXml())
        xml->writeTo(autosaveFile);
}

void MainComponent::clearAutosaveSnapshot()
{
    if (autosaveFile.existsAsFile())
        autosaveFile.deleteFile();
}

void MainComponent::maybeRestoreAutosave()
{
    if (! autosaveFile.existsAsFile())
        return;

    const auto choice = showMappedYesNoCancelBox(juce::AlertWindow::QuestionIcon,
                                                 "Restore Autosave?",
                                                 "An autosave snapshot was found. Restore it, discard it, or cancel startup changes?",
                                                 "Restore",
                                                 "Discard",
                                                 "Cancel",
                                                 this);

    if (choice == ThreeWayDialogChoice::first)
        loadSessionFromFile(autosaveFile);
    else if (choice == ThreeWayDialogChoice::second)
        clearAutosaveSnapshot();
}

MainComponent::CloseDecision MainComponent::attemptWindowClose()
{
    const auto decision = confirmAbandonChanges("Save Changes?",
                                                "The current patch has unsaved changes.");

    if (decision == CloseDecision::cancel)
        return CloseDecision::cancel;

    clearAutosaveSnapshot();
    return decision;
}

void MainComponent::attemptWindowCloseAsync(std::function<void(CloseDecision)> callback)
{
    if (! dirty)
    {
        clearAutosaveSnapshot();
        if (callback)
            callback(CloseDecision::proceed);
        return;
    }

    auto* dialog = new juce::AlertWindow("Save Changes?",
                                         "The current patch has unsaved changes.",
                                         juce::AlertWindow::WarningIcon,
                                         this);
    dialog->addButton("Save", 1);
    dialog->addButton("Discard", 2);
    dialog->addButton("Cancel", 3, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->centreAroundComponent(this, 420, 180);
    dialog->enterModalState(true,
                            juce::ModalCallbackFunction::create([this, completion = std::move(callback)](int result) mutable
                            {
                                if (result == 1)
                                {
                                    if (currentSessionFile == juce::File())
                                    {
                                        saveSessionAsync([this, saveCompletion = std::move(completion)](bool saved) mutable
                                        {
                                            if (! saved)
                                            {
                                                if (saveCompletion)
                                                    saveCompletion(CloseDecision::cancel);
                                                return;
                                            }

                                            clearAutosaveSnapshot();
                                            if (saveCompletion)
                                                saveCompletion(CloseDecision::proceed);
                                        });
                                        return;
                                    }

                                    saveSessionToFile(currentSessionFile);
                                    if (! dirty)
                                    {
                                        clearAutosaveSnapshot();
                                        if (completion)
                                            completion(CloseDecision::proceed);
                                    }
                                    else if (completion)
                                    {
                                        completion(CloseDecision::cancel);
                                    }

                                    return;
                                }

                                if (result == 2)
                                {
                                    clearAutosaveSnapshot();
                                    if (completion)
                                        completion(CloseDecision::discardAndQuit);
                                    return;
                                }

                                if (completion)
                                    completion(CloseDecision::cancel);
                            }),
                            true);
}

void MainComponent::prepareForQuit()
{
    stopTimer();
    canvas.closeDetachedEditors();
    shutdownAudio();
}

MainComponent::CloseDecision MainComponent::confirmAbandonChanges(const juce::String& title, const juce::String& message)
{
    if (! dirty)
        return CloseDecision::proceed;

    const auto choice = showMappedYesNoCancelBox(juce::AlertWindow::WarningIcon,
                                                 title,
                                                 message,
                                                 "Save",
                                                 "Discard",
                                                 "Cancel",
                                                 this);

    if (choice == ThreeWayDialogChoice::first)
    {
        if (currentSessionFile == juce::File())
            saveSession();
        else
            saveSessionToFile(currentSessionFile);

        return dirty ? CloseDecision::cancel : CloseDecision::proceed;
    }

    if (choice == ThreeWayDialogChoice::second)
        return CloseDecision::discardAndQuit;

    return CloseDecision::cancel;
}

void MainComponent::clearInspectorControls()
{
    inspectorLabels.clear();
    inspectorComboBoxes.clear();
    inspectorSliders.clear();
    inspectorToggleButtons.clear();
    inspectorStepButtons.clear();
    inspectorHasAdvanced = false;
    inspectorDetailButton.setVisible(false);
}

void MainComponent::showNodeInspector(const NodeSnapshot& node)
{
    inspectorTitle.setText(node.name, juce::dontSendNotification);

    if (node.typeId == "ExternalPlugin")
    {
        auto* pluginBox = new juce::ComboBox();
        pluginBox->addItem("Unassigned", 1);

        auto selectedItemId = 1;
        auto itemId = 2;

        for (const auto& displayName : externalPluginManager.getKnownPluginDisplayNames())
        {
            pluginBox->addItem(displayName, itemId);

            if (const auto description = externalPluginManager.getPluginByDisplayName(displayName))
                if (description->createIdentifierString() == node.resourcePath)
                    selectedItemId = itemId;

            ++itemId;
        }

        pluginBox->setTextWhenNothingSelected("Choose plugin");
        pluginBox->setSelectedId(selectedItemId, juce::dontSendNotification);
        pluginBox->onChange = [this, id = node.id, pluginBox]
        {
            if (pluginBox->getSelectedId() <= 1)
                graph.assignExternalPlugin(id, {});
            else if (const auto description = externalPluginManager.getPluginByDisplayName(pluginBox->getText()))
                graph.assignExternalPlugin(id, description->createIdentifierString());

            rebuildInspector();
        };
        addAndMakeVisible(pluginBox);
        inspectorComboBoxes.add(pluginBox);

        if (node.supportsEditor)
        {
            auto* helpLabel = new juce::Label();
            helpLabel->setText("Double-click to open.", juce::dontSendNotification);
            helpLabel->setColour(juce::Label::textColourId, juce::Colour(0xff9fadb9));
            helpLabel->setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(helpLabel);
            inspectorLabels.add(helpLabel);

            auto* zoomLabel = new juce::Label();
            zoomLabel->setText("Zoom", juce::dontSendNotification);
            zoomLabel->setColour(juce::Label::textColourId, juce::Colour(0xffdbe3ec));
            addAndMakeVisible(zoomLabel);
            inspectorLabels.add(zoomLabel);

            auto* zoomSlider = new juce::Slider();
            zoomSlider->setRange(0.5, 2.0, 0.01);
            zoomSlider->setSkewFactorFromMidPoint(1.0);
            zoomSlider->setValue(node.editorScale, juce::dontSendNotification);
            zoomSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
            zoomSlider->onValueChange = [this, id = node.id, zoomSlider]
            {
                graph.setNodeEditorScale(id, static_cast<float>(zoomSlider->getValue()));
            };
            addAndMakeVisible(zoomSlider);
            inspectorSliders.add(zoomSlider);
        }
    }

    if (node.typeId != "ExternalPlugin" && node.supportsEditor)
    {
        auto* zoomLabel = new juce::Label();
        zoomLabel->setText("Zoom", juce::dontSendNotification);
        zoomLabel->setColour(juce::Label::textColourId, juce::Colour(0xffdbe3ec));
        addAndMakeVisible(zoomLabel);
        inspectorLabels.add(zoomLabel);

        auto* zoomSlider = new juce::Slider();
        zoomSlider->setRange(0.5, 2.0, 0.01);
        zoomSlider->setSkewFactorFromMidPoint(1.0);
        zoomSlider->setValue(node.editorScale, juce::dontSendNotification);
        zoomSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
        zoomSlider->onValueChange = [this, id = node.id, zoomSlider]
        {
            graph.setNodeEditorScale(id, static_cast<float>(zoomSlider->getValue()));
        };
        addAndMakeVisible(zoomSlider);
        inspectorSliders.add(zoomSlider);
    }

    auto addEnumBox = [this, &node](const juce::String& labelText,
                                    const juce::StringArray& choices,
                                    int selectedIndex,
                                    const juce::String& parameterId)
    {
        auto* label = new juce::Label();
        label->setText(labelText, juce::dontSendNotification);
        label->setColour(juce::Label::textColourId, juce::Colour(0xffdbe3ec));
        addAndMakeVisible(label);
        inspectorLabels.add(label);

        auto* combo = new juce::ComboBox();
        for (int i = 0; i < choices.size(); ++i)
            combo->addItem(choices[i], i + 1);
        combo->setSelectedId(selectedIndex + 1, juce::dontSendNotification);
        combo->onChange = [this, id = node.id, parameterId, combo]
        {
            graph.setNodeParameter(id, parameterId, static_cast<float>(combo->getSelectedId() - 1));
        };
        addAndMakeVisible(combo);
        inspectorComboBoxes.add(combo);
    };

    std::vector<NodeParameterState> visibleParameters;
    visibleParameters.reserve(node.parameters.size());

    for (const auto& parameter : node.parameters)
    {
        const auto shouldShow = inspectorShowAdvanced
            || isEssentialNodeParameter(node.typeId, parameter.spec.id)
            || (visibleParameters.size() < (node.typeId == "ExternalPlugin" ? 6 : 4));

        if (shouldShow)
            visibleParameters.push_back(parameter);
    }

    inspectorHasAdvanced = visibleParameters.size() < node.parameters.size();
    inspectorDetailButton.setButtonText(inspectorShowAdvanced ? "Less" : "More");

    for (const auto& parameter : visibleParameters)
    {
        if (node.typeId == "Filter" && parameter.spec.id == "mode")
        {
            addEnumBox("Type", { "Low-pass", "High-pass", "Band-pass", "Notch" }, static_cast<int>(std::round(parameter.value)), "mode");
            continue;
        }

        auto* label = new juce::Label();
        label->setText(parameter.spec.name, juce::dontSendNotification);
        label->setColour(juce::Label::textColourId, juce::Colour(0xffdbe3ec));
        addAndMakeVisible(label);
        inspectorLabels.add(label);

        auto* slider = new juce::Slider();
        const auto interval = (parameter.spec.id == "channels"
                               || parameter.spec.id == "rootNote"
                               || parameter.spec.id == "destinations"
                               || parameter.spec.id == "activeRoute"
                               || parameter.spec.id == "numerator"
                               || parameter.spec.id == "denominator") ? 1.0 : 0.001;
        slider->setRange(parameter.spec.minValue, parameter.spec.maxValue, interval);
        slider->setValue(parameter.value, juce::dontSendNotification);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
        slider->onValueChange = [this, id = node.id, parameterId = parameter.spec.id, slider]
        {
            graph.setNodeParameter(id, parameterId, static_cast<float>(slider->getValue()));
        };

        addAndMakeVisible(slider);
        inspectorSliders.add(slider);
    }
}

void MainComponent::autoWireTrackNode(const juce::Uuid& trackId, bool isMidiTrack)
{
    const auto nodes = graph.getNodes();
    const auto connections = graph.getConnections();
    const auto trackSnapshot = graph.getNode(trackId);

    if (! trackSnapshot.has_value())
        return;

    if (const auto lfo = findNodeOfType(nodes, "LFO"))
        graph.connect({ lfo->id, false, 2, PortKind::modulation }, { trackId, true, isMidiTrack ? 4 : 2, PortKind::modulation });

    if (const auto timeSignature = findNodeOfType(nodes, "TimeSignature"))
    {
        graph.connect({ timeSignature->id, false, 2, PortKind::modulation }, { trackId, true, isMidiTrack ? 5 : 3, PortKind::modulation });
        graph.connect({ timeSignature->id, false, isMidiTrack ? 0 : 1, PortKind::modulation }, { trackId, true, isMidiTrack ? 3 : 1, PortKind::modulation });
    }
    else if (const auto bpmToLfo = findNodeOfType(nodes, "BpmToLfo"))
    {
        graph.connect({ bpmToLfo->id, false, 0, PortKind::modulation }, { trackId, true, isMidiTrack ? 3 : 1, PortKind::modulation });
    }

    if (const auto sum = findNodeOfType(nodes, "Sum"))
    {
        if (const auto freeAudioPort = findFirstFreeInputPort(*sum, PortKind::audio, connections))
        {
            graph.connect({ trackId, false, 0, PortKind::audio }, { sum->id, true, *freeAudioPort, PortKind::audio });
            return;
        }
    }

    if (const auto filter = findNodeOfType(nodes, "Filter"))
    {
        if (const auto freeAudioPort = findFirstFreeInputPort(*filter, PortKind::audio, connections))
        {
            graph.connect({ trackId, false, 0, PortKind::audio }, { filter->id, true, *freeAudioPort, PortKind::audio });
            return;
        }
    }

    if (const auto output = findNodeOfType(nodes, "Output"))
        graph.connect({ trackId, false, 0, PortKind::audio }, { output->id, true, 0, PortKind::audio });
}

void MainComponent::showTrackInspector(const NodeSnapshot& track)
{
    inspectorTitle.setText(track.name, juce::dontSendNotification);
    loadTrackClipButton.setVisible(track.trackTypeId == "audio");
    trackMuteToggle.setVisible(true);
    auto* loopToggle = new juce::ToggleButton("Loop");
    auto* soloToggle = new juce::ToggleButton("Solo");
    auto* armToggle = new juce::ToggleButton("Arm");
    auto* syncToggle = new juce::ToggleButton("Sync");
    loopToggle->setVisible(true);
    loopToggle->onClick = [this, id = track.id, loopToggle]
    {
        graph.setNodeParameter(id, "looping", loopToggle->getToggleState() ? 1.0f : 0.0f);
    };
    soloToggle->onClick = [this, id = track.id, soloToggle]
    {
        graph.setNodeParameter(id, "solo", soloToggle->getToggleState() ? 1.0f : 0.0f);
    };
    armToggle->onClick = [this, id = track.id, armToggle]
    {
        graph.setNodeParameter(id, "arm", armToggle->getToggleState() ? 1.0f : 0.0f);
    };
    syncToggle->onClick = [this, id = track.id, syncToggle]
    {
        graph.setNodeParameter(id, "sync", syncToggle->getToggleState() ? 1.0f : 0.0f);
    };
    addAndMakeVisible(loopToggle);
    addAndMakeVisible(soloToggle);
    addAndMakeVisible(armToggle);
    addAndMakeVisible(syncToggle);
    inspectorToggleButtons.add(loopToggle);
    inspectorToggleButtons.add(soloToggle);
    inspectorToggleButtons.add(armToggle);
    inspectorToggleButtons.add(syncToggle);

    juce::ToggleButton* monitorToggle = nullptr;
    if (track.trackTypeId == "audio")
    {
        monitorToggle = new juce::ToggleButton("Monitor");
        monitorToggle->onClick = [this, id = track.id, monitorToggle]
        {
            graph.setNodeParameter(id, "monitor", monitorToggle->getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible(monitorToggle);
        inspectorToggleButtons.add(monitorToggle);
    }

    std::vector<NodeParameterState> visibleTrackParameters;
    visibleTrackParameters.reserve(track.parameters.size());

    for (const auto& parameter : track.parameters)
    {
        if (track.trackTypeId == "midi" && parameter.spec.id.startsWith("step"))
            continue;

        if (parameter.spec.id == "mute"
            || parameter.spec.id == "solo"
            || parameter.spec.id == "arm"
            || parameter.spec.id == "sync"
            || parameter.spec.id == "monitor"
            || parameter.spec.id == "looping")
            continue;

        const auto shouldShow = inspectorShowAdvanced
            || isEssentialTrackParameter(track.trackTypeId, parameter.spec.id)
            || visibleTrackParameters.size() < 4;

        if (shouldShow)
            visibleTrackParameters.push_back(parameter);
    }

    const auto hasHiddenSteps = std::any_of(track.parameters.begin(), track.parameters.end(),
                                            [](const auto& parameter) { return parameter.spec.id.startsWith("step"); });
    inspectorHasAdvanced = visibleTrackParameters.size() < (track.parameters.size() - 5 - (track.trackTypeId == "audio" ? 1 : 0)) || hasHiddenSteps;
    inspectorDetailButton.setButtonText(inspectorShowAdvanced ? "Less" : "More");

    for (const auto& parameter : track.parameters)
    {
        if (parameter.spec.id == "mute")
        {
            trackMuteToggle.setToggleState(parameter.value > 0.5f, juce::dontSendNotification);
        }
        else if (parameter.spec.id == "looping")
        {
            loopToggle->setToggleState(parameter.value > 0.5f, juce::dontSendNotification);
        }
        else if (parameter.spec.id == "solo")
        {
            soloToggle->setToggleState(parameter.value > 0.5f, juce::dontSendNotification);
        }
        else if (parameter.spec.id == "arm")
        {
            armToggle->setToggleState(parameter.value > 0.5f, juce::dontSendNotification);
        }
        else if (parameter.spec.id == "sync")
        {
            syncToggle->setToggleState(parameter.value > 0.5f, juce::dontSendNotification);
        }
        else if (parameter.spec.id == "monitor" && monitorToggle != nullptr)
        {
            monitorToggle->setToggleState(parameter.value > 0.5f, juce::dontSendNotification);
        }
    }

    auto addSlider = [this](const juce::String& name,
                            double min,
                            double max,
                            double value,
                            std::function<void(double)> onChange)
    {
        auto* label = new juce::Label();
        label->setText(name, juce::dontSendNotification);
        label->setColour(juce::Label::textColourId, juce::Colour(0xffdbe3ec));
        addAndMakeVisible(label);
        inspectorLabels.add(label);

        auto* slider = new juce::Slider();
        slider->setRange(min, max, 0.001);
        slider->setValue(value, juce::dontSendNotification);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
        slider->onValueChange = [slider, callback = std::move(onChange)] { callback(slider->getValue()); };
        addAndMakeVisible(slider);
        inspectorSliders.add(slider);
    };

    for (const auto& parameter : visibleTrackParameters)
    {
        addSlider(parameter.spec.name,
                  parameter.spec.minValue,
                  parameter.spec.maxValue,
                  parameter.value,
                  [this, id = track.id, parameterId = parameter.spec.id](double value)
                  {
                      graph.setNodeParameter(id, parameterId, static_cast<float>(value));
                  });
    }

    if (track.trackTypeId == "midi" && inspectorShowAdvanced)
    {
        for (const auto& parameter : track.parameters)
        {
            if (! parameter.spec.id.startsWith("step"))
                continue;

            auto* button = new juce::ToggleButton(parameter.spec.name);
            button->setToggleState(parameter.value > 0.5f, juce::dontSendNotification);
            button->onClick = [this, id = track.id, parameterId = parameter.spec.id, button]
            {
                graph.setNodeParameter(id, parameterId, button->getToggleState() ? 1.0f : 0.0f);
            };
            addAndMakeVisible(button);
            inspectorStepButtons.add(button);
        }
    }
}

void MainComponent::toggleInspectorCollapsed()
{
    if (inspectorCollapsed)
    {
        inspectorCollapsed = false;
        inspectorPanelWidth = juce::jmax(240, lastExpandedInspectorWidth);
    }
    else
    {
        inspectorCollapsed = true;
        lastExpandedInspectorWidth = juce::jmax(240, inspectorPanelWidth);
        inspectorPanelWidth = 0;
    }

    toggleInspectorButton.setButtonText(inspectorCollapsed ? "Show Inspector" : "Hide Inspector");
    rebuildInspector();
    resized();
    repaint();
}

void MainComponent::toggleEditMode()
{
    editMode = ! editMode;
    applyModeState();
}

void MainComponent::applyModeState()
{
    if (editMode)
    {
        const auto previousSuppression = suppressDirtyTracking;
        suppressDirtyTracking = true;
        graph.setRecording(false);
        graph.setPlaying(false);
        suppressDirtyTracking = previousSuppression;
    }

    canvas.setEditMode(editMode);
    transportButton.setButtonText(editMode ? "Patch Mode" : "Play Mode");
    playButton.setEnabled(! editMode);
    recordButton.setEnabled(! editMode);
    rewindButton.setEnabled(! editMode);
    transportLoopToggle.setEnabled(! editMode);
    bpmSlider.setEnabled(! editMode);
    hintLabel.setText(editMode
                          ? "Patch mode: right-click to add modules, then drag between sockets to connect them."
                          : "Play mode: the patch is locked so you can focus on transport and performance.",
                      juce::dontSendNotification);
}
