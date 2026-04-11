#include "MainComponent.h"

namespace
{
constexpr int toolbarHeight = 48;
constexpr int trackAreaHeight = 190;
constexpr int inspectorWidth = 300;
}

MainComponent::MainComponent() : canvas(graph), trackView(graph)
{
    auto styleButton = [](juce::Button& button, juce::Colour colour)
    {
        button.setColour(juce::TextButton::buttonColourId, colour);
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    };

    styleButton(saveButton, juce::Colour(0xff2b3a4f));
    styleButton(loadButton, juce::Colour(0xff2b3a4f));
    styleButton(transportButton, juce::Colour(0xff355c50));
    styleButton(rewindButton, juce::Colour(0xff60483b));
    styleButton(addAudioTrackButton, juce::Colour(0xff35576d));
    styleButton(addMidiTrackButton, juce::Colour(0xff5b4d74));
    styleButton(scanPluginsButton, juce::Colour(0xff41644a));
    styleButton(loadTrackClipButton, juce::Colour(0xff35576d));

    externalPluginManager.initialise();

    saveButton.onClick = [this] { saveSession(); };
    loadButton.onClick = [this] { loadSession(); };
    transportButton.onClick = [this] { toggleEditMode(); };
    rewindButton.onClick = [this] { rewindTransport(); };
    addAudioTrackButton.onClick = [this] { addAudioTrack(); };
    addMidiTrackButton.onClick = [this] { addMidiTrack(); };
    scanPluginsButton.onClick = [this] { scanExternalPlugins(); };
    loadTrackClipButton.onClick = [this] { loadAudioIntoSelectedTrack(); };
    trackMuteToggle.onClick = [this]
    {
        if (selectedTrackId.has_value())
            graph.setNodeParameter(*selectedTrackId, "mute", trackMuteToggle.getToggleState() ? 1.0f : 0.0f);
    };

    addAndMakeVisible(saveButton);
    addAndMakeVisible(loadButton);
    addAndMakeVisible(transportButton);
    addAndMakeVisible(rewindButton);
    addAndMakeVisible(addAudioTrackButton);
    addAndMakeVisible(addMidiTrackButton);
    addAndMakeVisible(scanPluginsButton);
    addAndMakeVisible(hintLabel);
    addAndMakeVisible(trackView);
    addAndMakeVisible(canvas);
    addAndMakeVisible(inspectorTitle);
    addAndMakeVisible(loadTrackClipButton);
    addAndMakeVisible(trackMuteToggle);

    hintLabel.setText("Right-click to add nodes, drag sockets to patch, double-click tracks/plugins to open editors, and press Cmd+E to switch Edit and Performance.", juce::dontSendNotification);
    hintLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9fadb9));
    inspectorTitle.setText("Inspector", juce::dontSendNotification);
    inspectorTitle.setColour(juce::Label::textColourId, juce::Colours::white);
    inspectorTitle.setFont(juce::FontOptions(18.0f, juce::Font::bold));

    canvas.setSelectionChangedCallback([this](std::optional<juce::Uuid> nodeId)
    {
        selectedNodeId = std::move(nodeId);
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

    seedDefaultSession();
    applyModeState();
}

MainComponent::~MainComponent()
{
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

    saveButton.setBounds(toolbar.removeFromLeft(80).reduced(4));
    loadButton.setBounds(toolbar.removeFromLeft(80).reduced(4));
    transportButton.setBounds(toolbar.removeFromLeft(80).reduced(4));
    rewindButton.setBounds(toolbar.removeFromLeft(88).reduced(4));
    addAudioTrackButton.setBounds(toolbar.removeFromLeft(96).reduced(4));
    addMidiTrackButton.setBounds(toolbar.removeFromLeft(90).reduced(4));
    scanPluginsButton.setBounds(toolbar.removeFromLeft(104).reduced(4));

    auto inspector = bounds.removeFromRight(inspectorWidth);
    hintLabel.setBounds(bounds.removeFromTop(28));
    trackView.setBounds(bounds.removeFromTop(trackAreaHeight));
    bounds.removeFromTop(8);
    canvas.setBounds(bounds);

    inspectorTitle.setBounds(inspector.removeFromTop(30));
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
    auto inspector = bounds.removeFromRight(static_cast<float>(inspectorWidth));
    bounds.removeFromTop(static_cast<float>(toolbarHeight) + 6.0f);
    bounds.removeFromTop(28.0f);

    g.setColour(juce::Colour(0xff171d28));
    g.fillRoundedRectangle(inspector, 14.0f);
    g.fillRoundedRectangle(bounds.removeFromTop(static_cast<float>(trackAreaHeight)), 14.0f);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress('e', juce::ModifierKeys::commandModifier, 0)
        || key == juce::KeyPress('E', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        toggleEditMode();
        return true;
    }

    return juce::AudioAppComponent::keyPressed(key);
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
        canvas.grabKeyboardFocus();
    }
}

void MainComponent::scanExternalPlugins()
{
    juce::String report;
    externalPluginManager.scanForPlugins(report);
    hintLabel.setText("Plugin scan complete. " + report.replaceCharacters("\n", " "), juce::dontSendNotification);
}

void MainComponent::seedDefaultSession()
{
    const auto bpmToLfo = graph.addNode(NodeFactory::create("BpmToLfo"), { 60.0f, 70.0f });
    const auto timeSignature = graph.addNode(NodeFactory::create("TimeSignature"), { 330.0f, 70.0f });
    const auto lfo = graph.addNode(NodeFactory::create("LFO"), { 60.0f, 290.0f });
    const auto oscillator = graph.addNode(NodeFactory::create("Oscillator"), { 60.0f, 520.0f });
    const auto audioTrack = graph.addNode(NodeFactory::create("AudioTrack"), { 620.0f, 60.0f });
    const auto midiTrack = graph.addNode(NodeFactory::create("MidiTrack"), { 620.0f, 330.0f });
    const auto sum = graph.addNode(NodeFactory::create("Sum"), { 970.0f, 250.0f });
    const auto gain = graph.addNode(NodeFactory::create("Gain"), { 1240.0f, 250.0f });
    const auto output = graph.addNode(NodeFactory::create("Output"), { 1500.0f, 250.0f });

    graph.setNodeParameter(bpmToLfo, "bpm", 60.0f);
    graph.setNodeParameter(bpmToLfo, "multiplier", 1.0f);
    graph.setNodeParameter(timeSignature, "numerator", 5.0f);
    graph.setNodeParameter(timeSignature, "denominator", 4.0f);
    graph.setNodeParameter(lfo, "rate", 0.18f);
    graph.setNodeParameter(lfo, "depth", 1.0f);
    graph.setNodeParameter(oscillator, "frequency", 220.0f);
    graph.setNodeParameter(oscillator, "level", 0.14f);
    graph.setNodeParameter(midiTrack, "rootNote", 48.0f);
    graph.setNodeParameter(midiTrack, "gain", 0.22f);
    graph.setNodeParameter(midiTrack, "startPoint", 0.0f);
    graph.setNodeParameter(midiTrack, "endPoint", 0.75f);
    graph.setNodeParameter(midiTrack, "loopStart", 0.125f);
    graph.setNodeParameter(midiTrack, "loopEnd", 0.625f);
    graph.setNodeParameter(audioTrack, "startPoint", 0.1f);
    graph.setNodeParameter(audioTrack, "endPoint", 0.92f);
    graph.setNodeParameter(audioTrack, "loopStart", 0.2f);
    graph.setNodeParameter(audioTrack, "loopEnd", 0.68f);
    graph.setNodeParameter(sum, "channels", 3.0f);
    graph.setNodeParameter(sum, "trim", 0.72f);
    graph.setNodeParameter(gain, "gain", 0.86f);

    graph.connect({ bpmToLfo, false, 0, PortKind::modulation }, { timeSignature, true, 0, PortKind::modulation });
    graph.connect({ bpmToLfo, false, 0, PortKind::modulation }, { audioTrack, true, 0, PortKind::modulation });
    graph.connect({ timeSignature, false, 0, PortKind::modulation }, { midiTrack, true, 0, PortKind::modulation });
    graph.connect({ lfo, false, 2, PortKind::modulation }, { audioTrack, true, 1, PortKind::modulation });
    graph.connect({ timeSignature, false, 2, PortKind::modulation }, { audioTrack, true, 2, PortKind::modulation });
    graph.connect({ lfo, false, 2, PortKind::modulation }, { midiTrack, true, 1, PortKind::modulation });
    graph.connect({ timeSignature, false, 2, PortKind::modulation }, { midiTrack, true, 2, PortKind::modulation });

    graph.connect({ audioTrack, false, 0, PortKind::audio }, { sum, true, 0, PortKind::audio });
    graph.connect({ midiTrack, false, 0, PortKind::audio }, { sum, true, 1, PortKind::audio });
    graph.connect({ oscillator, false, 0, PortKind::audio }, { sum, true, 2, PortKind::audio });
    graph.connect({ sum, false, 0, PortKind::audio }, { gain, true, 0, PortKind::audio });
    graph.connect({ lfo, false, 0, PortKind::modulation }, { gain, true, 1, PortKind::modulation });
    graph.connect({ gain, false, 0, PortKind::audio }, { output, true, 0, PortKind::audio });
    rebuildInspector();
}

void MainComponent::saveSession()
{
    activeFileChooser = std::make_unique<juce::FileChooser>("Save PatchBay session", juce::File(), "*.patchbay", true, false, this);
    activeFileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                       | juce::FileBrowserComponent::canSelectFiles
                                       | juce::FileBrowserComponent::warnAboutOverwriting,
                                   [this](const juce::FileChooser& chooser)
                                   {
                                       const auto file = chooser.getResult();
                                       activeFileChooser.reset();

                                       if (file == juce::File())
                                           return;

                                       juce::ValueTree state("PATCHBAY_SESSION");
                                       state.appendChild(graph.createState(), nullptr);

                                       if (auto xml = state.createXml())
                                           xml->writeTo(file);
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

                                       graph.loadState(state.getChildWithName("PATCH_GRAPH"));

                                       selectedNodeId.reset();
                                       selectedTrackId.reset();
                                       canvas.clearSelection();
                                       trackView.setSelectedTrack(std::nullopt);
                                       rebuildInspector();
                                   });
}

void MainComponent::addAudioTrack()
{
    selectedTrackId = graph.addNode(NodeFactory::create("AudioTrack"), { 240.0f, 70.0f + static_cast<float>(graph.getNodes().size() * 18) });
    selectedNodeId.reset();
    canvas.clearSelection();
    trackView.setSelectedTrack(selectedTrackId);
    rebuildInspector();
}

void MainComponent::addMidiTrack()
{
    selectedTrackId = graph.addNode(NodeFactory::create("MidiTrack"), { 240.0f, 240.0f + static_cast<float>(graph.getNodes().size() * 18) });
    selectedNodeId.reset();
    canvas.clearSelection();
    trackView.setSelectedTrack(selectedTrackId);
    rebuildInspector();
}

void MainComponent::toggleTransport()
{
    const auto shouldPlay = ! graph.isPlaying();
    graph.setPlaying(shouldPlay);
    transportButton.setButtonText(shouldPlay ? "Stop" : "Play");
}

void MainComponent::rewindTransport()
{
    graph.resetTransport();
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
        inspectorTitle.setText("Inspector", juce::dontSendNotification);
    }

    resized();
}

void MainComponent::clearInspectorControls()
{
    inspectorLabels.clear();
    inspectorComboBoxes.clear();
    inspectorSliders.clear();
    inspectorToggleButtons.clear();
    inspectorStepButtons.clear();
}

void MainComponent::showNodeInspector(const NodeSnapshot& node)
{
    inspectorTitle.setText("Node: " + node.name, juce::dontSendNotification);

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

        pluginBox->setTextWhenNothingSelected("Select hosted plugin");
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
            helpLabel->setText("Double-click the node to open the plugin window.", juce::dontSendNotification);
            helpLabel->setColour(juce::Label::textColourId, juce::Colour(0xff9fadb9));
            helpLabel->setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(helpLabel);
            inspectorLabels.add(helpLabel);

            auto* zoomLabel = new juce::Label();
            zoomLabel->setText("Window Zoom", juce::dontSendNotification);
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
        zoomLabel->setText("Editor Zoom", juce::dontSendNotification);
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

    for (const auto& parameter : node.parameters)
    {
        auto* label = new juce::Label();
        label->setText(parameter.spec.name, juce::dontSendNotification);
        label->setColour(juce::Label::textColourId, juce::Colour(0xffdbe3ec));
        addAndMakeVisible(label);
        inspectorLabels.add(label);

        auto* slider = new juce::Slider();
        const auto interval = (parameter.spec.id == "channels" || parameter.spec.id == "rootNote") ? 1.0 : 0.001;
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

void MainComponent::showTrackInspector(const NodeSnapshot& track)
{
    inspectorTitle.setText("Track: " + track.name, juce::dontSendNotification);
    loadTrackClipButton.setVisible(track.trackTypeId == "audio");
    trackMuteToggle.setVisible(true);
    for (const auto& parameter : track.parameters)
        if (parameter.spec.id == "mute")
            trackMuteToggle.setToggleState(parameter.value > 0.5f, juce::dontSendNotification);

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

    for (const auto& parameter : track.parameters)
    {
        if (track.trackTypeId == "midi" && parameter.spec.id.startsWith("step"))
            continue;

        if (parameter.spec.id == "mute")
            continue;

        addSlider(parameter.spec.name,
                  parameter.spec.minValue,
                  parameter.spec.maxValue,
                  parameter.value,
                  [this, id = track.id, parameterId = parameter.spec.id](double value)
                  {
                      graph.setNodeParameter(id, parameterId, static_cast<float>(value));
                  });
    }

    if (track.trackTypeId == "midi")
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

void MainComponent::toggleEditMode()
{
    editMode = ! editMode;
    applyModeState();
}

void MainComponent::applyModeState()
{
    graph.setPlaying(! editMode);
    canvas.setEditMode(editMode);
    transportButton.setButtonText(editMode ? "Edit Mode" : "Performance");
    hintLabel.setText(editMode
                          ? "Edit mode: patching is enabled and audio is stopped. Press Cmd+E to switch to performance mode."
                          : "Performance mode: audio is running and patch editing is locked. Press Cmd+E to switch to edit mode.",
                      juce::dontSendNotification);
}
