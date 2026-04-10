#include "MainComponent.h"

namespace
{
constexpr int toolbarHeight = 48;
constexpr int trackAreaHeight = 190;
constexpr int inspectorWidth = 300;
}

MainComponent::MainComponent() : canvas(graph), trackView(graph)
{
    for (const auto& type : NodeFactory::getAvailableTypes())
    {
        auto* button = new juce::TextButton(type);
        button->onClick = [this, type] { addModule(type); };
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff243041));
        button->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        moduleButtons.add(button);
        addAndMakeVisible(button);
    }

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
    styleButton(loadTrackClipButton, juce::Colour(0xff35576d));

    saveButton.onClick = [this] { saveSession(); };
    loadButton.onClick = [this] { loadSession(); };
    transportButton.onClick = [this] { toggleTransport(); };
    rewindButton.onClick = [this] { rewindTransport(); };
    addAudioTrackButton.onClick = [this] { addAudioTrack(); };
    addMidiTrackButton.onClick = [this] { addMidiTrack(); };
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
    addAndMakeVisible(hintLabel);
    addAndMakeVisible(trackView);
    addAndMakeVisible(canvas);
    addAndMakeVisible(inspectorTitle);
    addAndMakeVisible(loadTrackClipButton);
    addAndMakeVisible(trackMuteToggle);

    hintLabel.setText("Standalone patching plus Logic-style audio and MIDI tracks. Select a node or track to edit it on the right.", juce::dontSendNotification);
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
    setSize(1500, 940);

    seedDefaultSession();
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

    for (auto* button : moduleButtons)
        button->setBounds(toolbar.removeFromLeft(100).reduced(4));

    saveButton.setBounds(toolbar.removeFromLeft(80).reduced(4));
    loadButton.setBounds(toolbar.removeFromLeft(80).reduced(4));
    transportButton.setBounds(toolbar.removeFromLeft(80).reduced(4));
    rewindButton.setBounds(toolbar.removeFromLeft(88).reduced(4));
    addAudioTrackButton.setBounds(toolbar.removeFromLeft(96).reduced(4));
    addMidiTrackButton.setBounds(toolbar.removeFromLeft(90).reduced(4));

    auto inspector = bounds.removeFromRight(inspectorWidth);
    hintLabel.setBounds(bounds.removeFromTop(28));
    trackView.setBounds(bounds.removeFromTop(trackAreaHeight));
    bounds.removeFromTop(8);
    canvas.setBounds(bounds);

    inspectorTitle.setBounds(inspector.removeFromTop(30));
    loadTrackClipButton.setBounds(inspector.removeFromTop(34).reduced(4));
    trackMuteToggle.setBounds(inspector.removeFromTop(28).reduced(6, 2));

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

void MainComponent::addModule(const juce::String& type)
{
    if (auto node = NodeFactory::create(type))
    {
        const auto offset = static_cast<float>(graph.getNodes().size()) * 28.0f;
        graph.addNode(std::move(node), { 60.0f + offset, 120.0f + offset });
        canvas.grabKeyboardFocus();
    }
}

void MainComponent::seedDefaultSession()
{
    const auto oscillator = graph.addNode(NodeFactory::create("Oscillator"), { 70.0f, 120.0f });
    const auto lfo = graph.addNode(NodeFactory::create("LFO"), { 70.0f, 310.0f });
    const auto audioTrack = graph.addNode(NodeFactory::create("AudioTrack"), { 300.0f, 60.0f });
    const auto midiTrack = graph.addNode(NodeFactory::create("MidiTrack"), { 300.0f, 330.0f });
    const auto gain = graph.addNode(NodeFactory::create("Gain"), { 620.0f, 180.0f });
    const auto output = graph.addNode(NodeFactory::create("Output"), { 900.0f, 180.0f });

    graph.connect({ oscillator, false, 0, PortKind::audio }, { gain, true, 0, PortKind::audio });
    graph.connect({ lfo, false, 0, PortKind::modulation }, { gain, true, 0, PortKind::modulation });
    graph.connect({ audioTrack, false, 0, PortKind::audio }, { gain, true, 0, PortKind::audio });
    graph.connect({ midiTrack, false, 0, PortKind::audio }, { gain, true, 0, PortKind::audio });
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
    inspectorSliders.clear();
    inspectorStepButtons.clear();
}

void MainComponent::showNodeInspector(const NodeSnapshot& node)
{
    inspectorTitle.setText("Node: " + node.name, juce::dontSendNotification);

    for (const auto& parameter : node.parameters)
    {
        auto* label = new juce::Label();
        label->setText(parameter.spec.name, juce::dontSendNotification);
        label->setColour(juce::Label::textColourId, juce::Colour(0xffdbe3ec));
        addAndMakeVisible(label);
        inspectorLabels.add(label);

        auto* slider = new juce::Slider();
        slider->setRange(parameter.spec.minValue, parameter.spec.maxValue, 0.001);
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
