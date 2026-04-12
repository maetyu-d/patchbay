#include "ExternalPluginModule.h"

namespace
{
constexpr double bpm = 120.0;

juce::Colour colourForExternalFormat(const juce::String& format)
{
    if (format == "AudioUnit")
        return juce::Colour(0xfff28482);
    if (format == "VST3")
        return juce::Colour(0xff80ed99);
    return juce::Colour(0xff90caf9);
}

juce::String sanitiseParamId(const juce::String& input)
{
    auto result = input;
    result = result.replaceCharacters(" /.:", "_____");
    return result;
}
} // namespace

ExternalPluginModule::ExternalPluginModule() = default;

ExternalPluginModule::ExternalPluginModule(const juce::PluginDescription& pluginDescription)
    : description(pluginDescription)
{
}

ExternalPluginModule::~ExternalPluginModule()
{
    releasePlugin();
}

juce::String ExternalPluginModule::getTypeId() const
{
    return "ExternalPlugin";
}

juce::String ExternalPluginModule::getDisplayName() const
{
    return description.name.isNotEmpty() ? description.name : "Plugin";
}

juce::Colour ExternalPluginModule::getNodeColour() const
{
    if (description.pluginFormatName.isEmpty())
        return juce::Colour(0xff90caf9);

    return colourForExternalFormat(description.pluginFormatName);
}

std::vector<PortInfo> ExternalPluginModule::getInputPorts() const
{
    std::vector<PortInfo> ports;

    if (description.createIdentifierString().isEmpty())
    {
        ports.push_back({ "audioIn", PortKind::audio });
        return ports;
    }

    for (int index = 0; index < getNumAudioInputs(); ++index)
        ports.push_back({ "audioIn" + juce::String(index + 1), PortKind::audio });

    return ports;
}

std::vector<PortInfo> ExternalPluginModule::getOutputPorts() const
{
    std::vector<PortInfo> ports;

    if (description.createIdentifierString().isEmpty())
    {
        ports.push_back({ "audioOut", PortKind::audio });
        return ports;
    }

    for (int index = 0; index < getNumAudioOutputs(); ++index)
        ports.push_back({ "audioOut" + juce::String(index + 1), PortKind::audio });

    return ports;
}

std::vector<NodeParameterSpec> ExternalPluginModule::getParameterSpecs() const
{
    std::vector<NodeParameterSpec> specs;

    if (instance == nullptr)
        return specs;

    const auto parameterCount = juce::jmin(32, instance->getNumParameters());
    specs.reserve(static_cast<size_t>(parameterCount));

    for (int index = 0; index < parameterCount; ++index)
    {
        if (auto* parameter = instance->getParameters()[index])
        {
            const auto parameterId = sanitiseParamId(parameter->getName(64));
            specs.push_back({ parameterId, parameter->getName(64), 0.0f, 1.0f, parameter->getDefaultValue() });
        }
    }

    return specs;
}

void ExternalPluginModule::prepare(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    instantiateIfNeeded();
}

void ExternalPluginModule::process(NodeRenderContext& context)
{
    instantiateIfNeeded();

    for (auto* output : context.audioOutputs)
        output->clear();

    if (context.audioOutputs.empty())
        return;

    if (instance == nullptr)
    {
        if (! context.audioInputs.empty())
            context.audioOutputs.front()->makeCopyOf(*context.audioInputs.front());
        return;
    }

    auto numChannels = juce::jmax(getNumAudioInputs(), getNumAudioOutputs());
    numChannels = juce::jmax(1, numChannels);
    juce::AudioBuffer<float> pluginBuffer(numChannels, context.numSamples);
    pluginBuffer.clear();

    for (int inputIndex = 0; inputIndex < juce::jmin(getNumAudioInputs(), static_cast<int>(context.audioInputs.size())); ++inputIndex)
    {
        auto* input = context.audioInputs[static_cast<size_t>(inputIndex)];
        const auto sourceChannel = juce::jmin(inputIndex, input->getNumChannels() - 1);
        pluginBuffer.copyFrom(inputIndex, 0, *input, sourceChannel, 0, context.numSamples);
    }

    syncParametersToPlugin();
    midiBuffer.clear();

    if (instance->acceptsMidi() && getNumAudioInputs() == 0 && context.isPlaying)
    {
        const auto samplesPerBeat = context.sampleRate * 60.0 / bpm;
        const auto noteLength = static_cast<int>(samplesPerBeat * 0.6);
        const auto beatIndex = static_cast<int>(std::floor(static_cast<double>(context.transportSamplePosition) / samplesPerBeat)) % 4;
        const int note = beatIndex == 3 ? 67 : 60 + beatIndex;

        for (int sample = 0; sample < context.numSamples; ++sample)
        {
            const auto absoluteSample = context.transportSamplePosition + sample;
            const auto positionInBeat = static_cast<int>(std::fmod(static_cast<double>(absoluteSample), samplesPerBeat));

            if (positionInBeat == 0)
                midiBuffer.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 96), sample);
            else if (positionInBeat == noteLength)
                midiBuffer.addEvent(juce::MidiMessage::noteOff(1, note), sample);
        }
    }

    instance->processBlock(pluginBuffer, midiBuffer);

    for (int outputIndex = 0; outputIndex < juce::jmin(getNumAudioOutputs(), static_cast<int>(context.audioOutputs.size())); ++outputIndex)
    {
        auto* output = context.audioOutputs[static_cast<size_t>(outputIndex)];
        const auto destChannel = juce::jmin(outputIndex, output->getNumChannels() - 1);
        output->copyFrom(destChannel, 0, pluginBuffer, outputIndex, 0, context.numSamples);
    }
}

float ExternalPluginModule::getParameterValue(const juce::String& parameterId) const
{
    if (const auto it = parameterValues.find(parameterId); it != parameterValues.end())
        return it->second;

    return 0.0f;
}

void ExternalPluginModule::setParameterValue(const juce::String& parameterId, float newValue)
{
    parameterValues[parameterId] = juce::jlimit(0.0f, 1.0f, newValue);
}

juce::ValueTree ExternalPluginModule::saveState() const
{
    auto state = ModuleNode::saveState();
    state.setProperty("identifier", description.createIdentifierString(), nullptr);
    state.setProperty("editorOpen", editorOpen, nullptr);
    state.setProperty("editorDetached", editorDetached, nullptr);
    state.setProperty("editorScale", editorScale, nullptr);

    if (instance != nullptr)
    {
        juce::MemoryBlock pluginState;
        instance->getStateInformation(pluginState);
        state.setProperty("pluginState", pluginState.toBase64Encoding(), nullptr);
    }

    return state;
}

void ExternalPluginModule::loadState(const juce::ValueTree& state)
{
    ModuleNode::loadState(state);
    editorOpen = static_cast<bool>(state.getProperty("editorOpen", false));
    editorDetached = static_cast<bool>(state.getProperty("editorDetached", false));
    editorScale = static_cast<float>(state.getProperty("editorScale", 1.0f));
    instantiateIfNeeded();

    if (instance != nullptr)
    {
        const auto encodedState = state.getProperty("pluginState").toString();
        if (encodedState.isNotEmpty())
        {
            juce::MemoryBlock pluginState;
            pluginState.fromBase64Encoding(encodedState);
            instance->setStateInformation(pluginState.getData(), static_cast<int>(pluginState.getSize()));
        }
    }
}

juce::String ExternalPluginModule::getResourcePath() const
{
    return description.createIdentifierString();
}

juce::Component* ExternalPluginModule::getEmbeddedEditor()
{
    instantiateIfNeeded();

    if (instance == nullptr || ! instance->hasEditor() || ! editorOpen)
        return nullptr;

    if (editor == nullptr)
        editor = instance->createEditorIfNeeded();

    if (editor != nullptr)
    {
        effectiveEditorScale = editorScale;

        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto userArea = display->userArea.reduced(36);
            const auto editorWidth = juce::jmax(1, editor->getWidth());
            const auto editorHeight = juce::jmax(1, editor->getHeight());
            const auto widthScale = static_cast<float>(userArea.getWidth()) / static_cast<float>(editorWidth);
            const auto heightScale = static_cast<float>(userArea.getHeight()) / static_cast<float>(editorHeight);
            effectiveEditorScale = juce::jlimit(0.5f, 2.0f, juce::jmin(editorScale, juce::jmin(widthScale, heightScale)));
        }

        editor->setScaleFactor(effectiveEditorScale);
    }

    return editor;
}

juce::Rectangle<int> ExternalPluginModule::getEmbeddedEditorBoundsHint() const
{
    if (editor != nullptr)
        return { 0, 0,
                 static_cast<int>(std::round(static_cast<float>(editor->getWidth()) * effectiveEditorScale)),
                 static_cast<int>(std::round(static_cast<float>(editor->getHeight()) * effectiveEditorScale)) };

    return { 0, 0,
             static_cast<int>(std::round(320.0f * effectiveEditorScale)),
             static_cast<int>(std::round(220.0f * effectiveEditorScale)) };
}

bool ExternalPluginModule::supportsEmbeddedEditor() const
{
    return description.createIdentifierString().isNotEmpty();
}

bool ExternalPluginModule::isEditorOpen() const
{
    return editorOpen;
}

void ExternalPluginModule::setEditorOpen(bool shouldBeOpen)
{
    editorOpen = shouldBeOpen;

    if (! editorOpen)
        editorDetached = false;
}

bool ExternalPluginModule::isEditorDetached() const
{
    return editorDetached;
}

void ExternalPluginModule::setEditorDetached(bool shouldBeDetached)
{
    editorDetached = shouldBeDetached && editorOpen;
}

float ExternalPluginModule::getEditorScale() const
{
    return editorScale;
}

void ExternalPluginModule::setEditorScale(float scale)
{
    editorScale = juce::jlimit(0.5f, 2.0f, scale);
    effectiveEditorScale = editorScale;

    if (editor != nullptr)
        editor->setScaleFactor(effectiveEditorScale);
}

void ExternalPluginModule::setPluginIdentifier(const juce::String& identifier)
{
    if (identifier == getPluginIdentifier())
        return;

    releasePlugin();
    parameterValues.clear();
    lastError.clear();
    editorOpen = false;
    editorDetached = false;

    if (identifier.isEmpty())
    {
        description = {};
        return;
    }

    if (const auto plugin = ExternalPluginManager::getInstance().getPluginByIdentifier(identifier))
    {
        description = *plugin;
        instantiateIfNeeded();
    }
    else
    {
        description = {};
        lastError = "Plugin not found in scanned catalogue.";
    }
}

juce::String ExternalPluginModule::getPluginIdentifier() const
{
    return description.createIdentifierString();
}

void ExternalPluginModule::instantiateIfNeeded()
{
    if (instance != nullptr || description.createIdentifierString().isEmpty())
        return;

    juce::String error;
    instance = ExternalPluginManager::getInstance().createPluginInstance(description, currentSampleRate, currentBlockSize, error);
    lastError = error;

    if (instance != nullptr)
    {
        instance->enableAllBuses();
        instance->setRateAndBufferSizeDetails(currentSampleRate, currentBlockSize);
        instance->prepareToPlay(currentSampleRate, currentBlockSize);

        for (int index = 0; index < juce::jmin(32, instance->getNumParameters()); ++index)
        {
            if (auto* parameter = instance->getParameters()[index])
                parameterValues[sanitiseParamId(parameter->getName(64))] = parameter->getValue();
        }

        if (editor != nullptr)
            editor->setScaleFactor(effectiveEditorScale);
    }
}

void ExternalPluginModule::releasePlugin()
{
    editor = nullptr;

    if (instance != nullptr)
    {
        instance->releaseResources();
        instance.reset();
    }
}

void ExternalPluginModule::syncParametersToPlugin()
{
    if (instance == nullptr)
        return;

    for (int index = 0; index < juce::jmin(32, instance->getNumParameters()); ++index)
    {
        if (auto* parameter = instance->getParameters()[index])
        {
            const auto parameterId = sanitiseParamId(parameter->getName(64));
            if (const auto it = parameterValues.find(parameterId); it != parameterValues.end())
                parameter->setValueNotifyingHost(it->second);
        }
    }
}

int ExternalPluginModule::getNumAudioInputs() const
{
    return description.numInputChannels > 0 ? juce::jmin(2, description.numInputChannels) : 0;
}

int ExternalPluginModule::getNumAudioOutputs() const
{
    return description.numOutputChannels > 0 ? juce::jmin(2, description.numOutputChannels) : 1;
}
