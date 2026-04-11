#include "PatchGraph.h"
#include "NodeFactory.h"
#include "../Modules/ExternalPluginModule.h"
#include <queue>

namespace
{
constexpr int stereoChannels = 2;

bool areSocketsCompatible(const SocketRef& source, const SocketRef& destination)
{
    return !source.isInput
        && destination.isInput
        && source.kind == destination.kind
        && source.nodeId != destination.nodeId
        && source.portIndex >= 0
        && destination.portIndex >= 0;
}
} // namespace

juce::Uuid PatchGraph::addNode(std::unique_ptr<ModuleNode> node, juce::Point<float> position)
{
    jassert(node != nullptr);

    const juce::ScopedLock lock(graphLock);

    auto* entry = new NodeEntry();
    entry->id = juce::Uuid();
    entry->typeId = node->getTypeId();
    entry->position = position;
    entry->node = std::move(node);
    entry->node->prepare(currentSampleRate, currentBlockSize);
    const auto id = entry->id;

    nodes.add(entry);
    sendChangeMessage();
    return id;
}

void PatchGraph::removeNode(const juce::Uuid& nodeId)
{
    const juce::ScopedLock lock(graphLock);

    connections.erase(std::remove_if(connections.begin(), connections.end(),
                                     [&nodeId](const auto& connection)
                                     {
                                         return connection.source.nodeId == nodeId
                                             || connection.destination.nodeId == nodeId;
                                     }),
                      connections.end());

    for (int index = nodes.size(); --index >= 0;)
    {
        if (nodes[index]->id == nodeId)
        {
            runtimeStates.erase(nodeId.toString());
            nodes.remove(index);
            break;
        }
    }

    sendChangeMessage();
}

void PatchGraph::setNodePosition(const juce::Uuid& nodeId, juce::Point<float> position)
{
    const juce::ScopedLock lock(graphLock);

    for (auto* node : nodes)
    {
        if (node->id == nodeId)
        {
            node->position = position;
            sendChangeMessage();
            return;
        }
    }
}

bool PatchGraph::connect(const SocketRef& source, const SocketRef& destination)
{
    if (!areSocketsCompatible(source, destination))
        return false;

    const juce::ScopedLock lock(graphLock);

    const auto exists = std::any_of(connections.begin(), connections.end(),
                                    [&source, &destination](const auto& connection)
                                    {
                                        return connection.source.nodeId == source.nodeId
                                            && connection.source.portIndex == source.portIndex
                                            && connection.destination.nodeId == destination.nodeId
                                            && connection.destination.portIndex == destination.portIndex
                                            && connection.source.kind == source.kind;
                                    });

    if (exists)
        return false;

    connections.push_back({ source, destination });
    sendChangeMessage();
    return true;
}

void PatchGraph::disconnect(const GraphConnection& connection)
{
    const juce::ScopedLock lock(graphLock);

    connections.erase(std::remove_if(connections.begin(), connections.end(),
                                     [&connection](const auto& candidate)
                                     {
                                         return candidate.source.nodeId == connection.source.nodeId
                                             && candidate.source.portIndex == connection.source.portIndex
                                             && candidate.destination.nodeId == connection.destination.nodeId
                                             && candidate.destination.portIndex == connection.destination.portIndex
                                             && candidate.source.kind == connection.source.kind;
                                     }),
                      connections.end());

    sendChangeMessage();
}

std::vector<NodeSnapshot> PatchGraph::getNodes() const
{
    const juce::ScopedLock lock(graphLock);
    std::vector<NodeSnapshot> snapshots;
    snapshots.reserve(static_cast<size_t>(nodes.size()));

    for (auto* entry : nodes)
    {
        snapshots.push_back({
            entry->id,
            entry->typeId,
            entry->node->getDisplayName(),
            entry->node->getNodeColour(),
            entry->position,
            entry->node->getInputPorts(),
            entry->node->getOutputPorts(),
            [&entry]
            {
                std::vector<NodeParameterState> parameters;
                for (const auto& spec : entry->node->getParameterSpecs())
                    parameters.push_back({ spec, entry->node->getParameterValue(spec.id) });
                return parameters;
            }(),
            entry->node->isTrackModule(),
            entry->node->getTrackTypeId(),
            entry->node->getResourcePath(),
            entry->node->getEmbeddedEditorBoundsHint(),
            entry->node->supportsEmbeddedEditor(),
            entry->node->isEditorOpen(),
            entry->node->isEditorDetached(),
            entry->node->getEditorScale()
        });
    }

    return snapshots;
}

std::optional<NodeSnapshot> PatchGraph::getNode(const juce::Uuid& nodeId) const
{
    const auto nodesToInspect = getNodes();

    for (const auto& node : nodesToInspect)
    {
        if (node.id == nodeId)
            return node;
    }

    return std::nullopt;
}

std::vector<GraphConnection> PatchGraph::getConnections() const
{
    const juce::ScopedLock lock(graphLock);
    return connections;
}

bool PatchGraph::setNodeParameter(const juce::Uuid& nodeId, const juce::String& parameterId, float value)
{
    const juce::ScopedLock lock(graphLock);

    for (auto* entry : nodes)
    {
        if (entry->id == nodeId)
        {
            entry->node->setParameterValue(parameterId, value);
            pruneInvalidConnectionsForNode(nodeId);
            sendChangeMessage();
            return true;
        }
    }

    return false;
}

bool PatchGraph::assignExternalPlugin(const juce::Uuid& nodeId, const juce::String& identifier)
{
    const juce::ScopedLock lock(graphLock);

    for (auto* entry : nodes)
    {
        if (entry->id != nodeId)
            continue;

        if (auto* externalNode = dynamic_cast<ExternalPluginModule*>(entry->node.get()))
        {
            externalNode->setPluginIdentifier(identifier);
            externalNode->prepare(currentSampleRate, currentBlockSize);
            pruneInvalidConnectionsForNode(nodeId);
            sendChangeMessage();
            return true;
        }

        return false;
    }

    return false;
}

bool PatchGraph::loadNodeFile(const juce::Uuid& nodeId, const juce::File& file)
{
    const juce::ScopedLock lock(graphLock);

    for (auto* entry : nodes)
    {
        if (entry->id == nodeId)
        {
            const auto loaded = entry->node->loadFile(file);
            if (loaded)
                sendChangeMessage();
            return loaded;
        }
    }

    return false;
}

juce::Component* PatchGraph::getNodeEmbeddedEditor(const juce::Uuid& nodeId)
{
    const juce::ScopedLock lock(graphLock);

    for (auto* entry : nodes)
    {
        if (entry->id == nodeId)
            return entry->node->getEmbeddedEditor();
    }

    return nullptr;
}

bool PatchGraph::setNodeEditorOpen(const juce::Uuid& nodeId, bool isOpen)
{
    const juce::ScopedLock lock(graphLock);

    for (auto* entry : nodes)
    {
        if (entry->id == nodeId)
        {
            entry->node->setEditorOpen(isOpen);
            sendChangeMessage();
            return true;
        }
    }

    return false;
}

bool PatchGraph::setNodeEditorDetached(const juce::Uuid& nodeId, bool isDetached)
{
    const juce::ScopedLock lock(graphLock);

    for (auto* entry : nodes)
    {
        if (entry->id == nodeId)
        {
            entry->node->setEditorDetached(isDetached);
            sendChangeMessage();
            return true;
        }
    }

    return false;
}

bool PatchGraph::setNodeEditorScale(const juce::Uuid& nodeId, float scale)
{
    const juce::ScopedLock lock(graphLock);

    for (auto* entry : nodes)
    {
        if (entry->id == nodeId)
        {
            entry->node->setEditorScale(scale);
            sendChangeMessage();
            return true;
        }
    }

    return false;
}

juce::ValueTree PatchGraph::createState() const
{
    const juce::ScopedLock lock(graphLock);
    juce::ValueTree state("PATCH_GRAPH");
    state.setProperty("playing", playing, nullptr);

    for (auto* entry : nodes)
    {
        juce::ValueTree node("NODE");
        node.setProperty("id", entry->id.toString(), nullptr);
        node.setProperty("type", entry->typeId, nullptr);
        node.setProperty("x", entry->position.x, nullptr);
        node.setProperty("y", entry->position.y, nullptr);
        node.appendChild(entry->node->saveState(), nullptr);
        state.appendChild(node, nullptr);
    }

    for (const auto& connection : connections)
    {
        juce::ValueTree cable("CONNECTION");
        cable.setProperty("sourceNode", connection.source.nodeId.toString(), nullptr);
        cable.setProperty("sourcePort", connection.source.portIndex, nullptr);
        cable.setProperty("sourceKind", connection.source.kind == PortKind::audio ? "audio" : "modulation", nullptr);
        cable.setProperty("destinationNode", connection.destination.nodeId.toString(), nullptr);
        cable.setProperty("destinationPort", connection.destination.portIndex, nullptr);
        cable.setProperty("destinationKind", connection.destination.kind == PortKind::audio ? "audio" : "modulation", nullptr);
        state.appendChild(cable, nullptr);
    }

    return state;
}

void PatchGraph::loadState(const juce::ValueTree& state)
{
    const juce::ScopedLock lock(graphLock);
    nodes.clear();
    connections.clear();
    runtimeStates.clear();
    playing = static_cast<bool>(state.getProperty("playing", true));
    transportSamplePosition = 0;

    for (const auto& child : state)
    {
        if (! child.hasType("NODE"))
            continue;

        if (auto node = NodeFactory::createFromState(child))
        {
            auto* entry = new NodeEntry();
            entry->id = juce::Uuid(child.getProperty("id").toString());
            entry->typeId = child.getProperty("type").toString();
            entry->position = { static_cast<float>(child.getProperty("x")), static_cast<float>(child.getProperty("y")) };
            entry->node = std::move(node);
            entry->node->prepare(currentSampleRate, currentBlockSize);
            nodes.add(entry);
        }
    }

    for (const auto& child : state)
    {
        if (! child.hasType("CONNECTION"))
            continue;

        const auto sourceKind = child.getProperty("sourceKind").toString() == "modulation" ? PortKind::modulation : PortKind::audio;
        const auto destinationKind = child.getProperty("destinationKind").toString() == "modulation" ? PortKind::modulation : PortKind::audio;

        connections.push_back({
            { juce::Uuid(child.getProperty("sourceNode").toString()), false, static_cast<int>(child.getProperty("sourcePort")), sourceKind },
            { juce::Uuid(child.getProperty("destinationNode").toString()), true, static_cast<int>(child.getProperty("destinationPort")), destinationKind }
        });
    }

    sendChangeMessage();
}

void PatchGraph::prepare(double sampleRate, int samplesPerBlock)
{
    const juce::ScopedLock lock(graphLock);

    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    runtimeStates.clear();

    for (auto* entry : nodes)
        entry->node->prepare(sampleRate, samplesPerBlock);
}

void PatchGraph::render(juce::AudioBuffer<float>& outputBuffer)
{
    const juce::ScopedLock lock(graphLock);
    outputBuffer.clear();

    const auto renderOrder = buildRenderOrder();

    for (const auto* entry : renderOrder)
    {
        auto& runtime = getOrCreateRuntimeState(*entry, outputBuffer.getNumSamples());

        for (auto& buffer : runtime.audioInputs)
            buffer.clear();

        for (auto& buffer : runtime.audioOutputs)
            buffer.clear();

        std::fill(runtime.modInputs.begin(), runtime.modInputs.end(), 0.0f);
        std::fill(runtime.modOutputs.begin(), runtime.modOutputs.end(), 0.0f);

        for (const auto& connection : connections)
        {
            if (connection.destination.nodeId != entry->id)
                continue;

            const auto stateIt = runtimeStates.find(connection.source.nodeId.toString());
            if (stateIt == runtimeStates.end())
                continue;

            auto& sourceState = stateIt->second;

            if (connection.source.kind == PortKind::audio)
            {
                if (juce::isPositiveAndBelow(connection.destination.portIndex, static_cast<int>(runtime.audioInputs.size()))
                    && juce::isPositiveAndBelow(connection.source.portIndex, static_cast<int>(sourceState.audioOutputs.size())))
                {
                    auto& target = runtime.audioInputs[static_cast<size_t>(connection.destination.portIndex)];
                    auto& source = sourceState.audioOutputs[static_cast<size_t>(connection.source.portIndex)];

                    for (int channel = 0; channel < juce::jmin(target.getNumChannels(), source.getNumChannels()); ++channel)
                        target.addFrom(channel, 0, source, channel, 0, outputBuffer.getNumSamples());
                }
            }
            else
            {
                if (juce::isPositiveAndBelow(connection.destination.portIndex, static_cast<int>(runtime.modInputs.size()))
                    && juce::isPositiveAndBelow(connection.source.portIndex, static_cast<int>(sourceState.modOutputs.size())))
                {
                    runtime.modInputs[static_cast<size_t>(connection.destination.portIndex)]
                        += sourceState.modOutputs[static_cast<size_t>(connection.source.portIndex)];
                }
            }
        }

        NodeRenderContext context;
        context.sampleRate = currentSampleRate;
        context.numSamples = outputBuffer.getNumSamples();
        context.modInputs = runtime.modInputs;
        context.modOutputs = runtime.modOutputs;
        context.isPlaying = playing;
        context.transportSamplePosition = transportSamplePosition;

        for (auto& buffer : runtime.audioInputs)
            context.audioInputs.push_back(&buffer);

        for (auto& buffer : runtime.audioOutputs)
            context.audioOutputs.push_back(&buffer);

        entry->node->process(context);

        runtime.modOutputs = context.modOutputs;

        if (entry->node->contributesToMasterOutput() && !runtime.audioOutputs.empty())
        {
            auto& master = runtime.audioOutputs.front();

            for (int channel = 0; channel < juce::jmin(outputBuffer.getNumChannels(), master.getNumChannels()); ++channel)
                outputBuffer.addFrom(channel, 0, master, channel, 0, outputBuffer.getNumSamples());
        }
    }

    if (playing)
        transportSamplePosition += outputBuffer.getNumSamples();
}

PatchGraph::RuntimeState& PatchGraph::getOrCreateRuntimeState(const NodeEntry& entry, int blockSize)
{
    auto& state = runtimeStates[entry.id.toString()];

    const auto inputPorts = entry.node->getInputPorts();
    const auto outputPorts = entry.node->getOutputPorts();

    const auto audioInputCount = static_cast<int>(std::count_if(inputPorts.begin(), inputPorts.end(),
                                                                [](const auto& port)
                                                                {
                                                                    return port.kind == PortKind::audio;
                                                                }));

    const auto audioOutputCount = static_cast<int>(std::count_if(outputPorts.begin(), outputPorts.end(),
                                                                 [](const auto& port)
                                                                 {
                                                                     return port.kind == PortKind::audio;
                                                                 }));

    const auto modInputCount = static_cast<int>(inputPorts.size()) - audioInputCount;
    const auto modOutputCount = static_cast<int>(outputPorts.size()) - audioOutputCount;

    auto resizeAudioVector = [blockSize](auto& buffers, int count)
    {
        buffers.resize(static_cast<size_t>(count));

        for (auto& buffer : buffers)
            buffer.setSize(stereoChannels, blockSize, false, false, true);
    };

    resizeAudioVector(state.audioInputs, audioInputCount);
    resizeAudioVector(state.audioOutputs, audioOutputCount);
    state.modInputs.resize(static_cast<size_t>(modInputCount), 0.0f);
    state.modOutputs.resize(static_cast<size_t>(modOutputCount), 0.0f);
    return state;
}

const PatchGraph::NodeEntry* PatchGraph::findNode(const juce::Uuid& nodeId) const
{
    for (auto* node : nodes)
    {
        if (node->id == nodeId)
            return node;
    }

    return nullptr;
}

void PatchGraph::pruneInvalidConnectionsForNode(const juce::Uuid& nodeId)
{
    const auto* entry = findNode(nodeId);
    if (entry == nullptr)
        return;

    const auto inputPorts = entry->node->getInputPorts();
    const auto outputPorts = entry->node->getOutputPorts();

    connections.erase(std::remove_if(connections.begin(), connections.end(),
                                     [&nodeId, &inputPorts, &outputPorts](const auto& connection)
                                     {
                                         if (connection.destination.nodeId == nodeId)
                                         {
                                             return ! juce::isPositiveAndBelow(connection.destination.portIndex, static_cast<int>(inputPorts.size()))
                                                 || inputPorts[static_cast<size_t>(connection.destination.portIndex)].kind != connection.destination.kind;
                                         }

                                         if (connection.source.nodeId == nodeId)
                                         {
                                             return ! juce::isPositiveAndBelow(connection.source.portIndex, static_cast<int>(outputPorts.size()))
                                                 || outputPorts[static_cast<size_t>(connection.source.portIndex)].kind != connection.source.kind;
                                         }

                                         return false;
                                     }),
                      connections.end());
}

std::vector<const PatchGraph::NodeEntry*> PatchGraph::buildRenderOrder() const
{
    std::vector<const NodeEntry*> ordered;
    ordered.reserve(static_cast<size_t>(nodes.size()));

    std::map<juce::String, int, std::less<>> indegree;
    std::map<juce::String, std::vector<juce::String>, std::less<>> adjacency;

    for (auto* entry : nodes)
        indegree[entry->id.toString()] = 0;

    for (const auto& connection : connections)
    {
        const auto sourceKey = connection.source.nodeId.toString();
        const auto destKey = connection.destination.nodeId.toString();

        adjacency[sourceKey].push_back(destKey);
        ++indegree[destKey];
    }

    std::queue<juce::String> ready;

    for (const auto& [nodeId, degree] : indegree)
    {
        if (degree == 0)
            ready.push(nodeId);
    }

    while (!ready.empty())
    {
        const auto currentId = ready.front();
        ready.pop();

        if (const auto* entry = findNode(juce::Uuid(currentId)))
            ordered.push_back(entry);

        for (const auto& nextId : adjacency[currentId])
        {
            auto& degree = indegree[nextId];
            --degree;

            if (degree == 0)
                ready.push(nextId);
        }
    }

    if (ordered.size() != static_cast<size_t>(nodes.size()))
    {
        for (auto* entry : nodes)
        {
            const auto alreadyIncluded = std::find_if(ordered.begin(), ordered.end(),
                                                      [entry](const auto* candidate)
                                                      {
                                                          return candidate->id == entry->id;
                                                      });

            if (alreadyIncluded == ordered.end())
                ordered.push_back(entry);
        }
    }

    return ordered;
}

void PatchGraph::setPlaying(bool shouldPlay)
{
    const juce::ScopedLock lock(graphLock);
    playing = shouldPlay;
    sendChangeMessage();
}

bool PatchGraph::isPlaying() const
{
    const juce::ScopedLock lock(graphLock);
    return playing;
}

void PatchGraph::resetTransport()
{
    const juce::ScopedLock lock(graphLock);
    transportSamplePosition = 0;
    sendChangeMessage();
}
