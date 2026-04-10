#pragma once

#include "ModuleNode.h"
#include <map>
#include <optional>

struct SocketRef
{
    juce::Uuid nodeId;
    bool isInput = false;
    int portIndex = -1;
    PortKind kind = PortKind::audio;
};

struct GraphConnection
{
    SocketRef source;
    SocketRef destination;
};

struct NodeSnapshot
{
    juce::Uuid id;
    juce::String typeId;
    juce::String name;
    juce::Colour colour = juce::Colour(0xff4a5568);
    juce::Point<float> position;
    std::vector<PortInfo> inputs;
    std::vector<PortInfo> outputs;
    std::vector<NodeParameterState> parameters;
    bool isTrack = false;
    juce::String trackTypeId;
    juce::String resourcePath;
};

class PatchGraph final : public juce::ChangeBroadcaster
{
public:
    PatchGraph() = default;

    juce::Uuid addNode(std::unique_ptr<ModuleNode> node, juce::Point<float> position);
    void removeNode(const juce::Uuid& nodeId);
    void setNodePosition(const juce::Uuid& nodeId, juce::Point<float> position);

    bool connect(const SocketRef& source, const SocketRef& destination);
    void disconnect(const GraphConnection& connection);

    std::vector<NodeSnapshot> getNodes() const;
    std::optional<NodeSnapshot> getNode(const juce::Uuid& nodeId) const;
    std::vector<GraphConnection> getConnections() const;
    bool setNodeParameter(const juce::Uuid& nodeId, const juce::String& parameterId, float value);
    bool loadNodeFile(const juce::Uuid& nodeId, const juce::File& file);

    juce::ValueTree createState() const;
    void loadState(const juce::ValueTree& state);

    void prepare(double sampleRate, int samplesPerBlock);
    void render(juce::AudioBuffer<float>& outputBuffer);
    void setPlaying(bool shouldPlay);
    bool isPlaying() const;
    void resetTransport();

private:
    struct NodeEntry
    {
        juce::Uuid id;
        juce::String typeId;
        juce::Point<float> position;
        std::unique_ptr<ModuleNode> node;
    };

    struct RuntimeState
    {
        std::vector<juce::AudioBuffer<float>> audioInputs;
        std::vector<juce::AudioBuffer<float>> audioOutputs;
        std::vector<float> modInputs;
        std::vector<float> modOutputs;
    };

    RuntimeState& getOrCreateRuntimeState(const NodeEntry& node, int blockSize);
    const NodeEntry* findNode(const juce::Uuid& nodeId) const;

    std::vector<const NodeEntry*> buildRenderOrder() const;

    mutable juce::CriticalSection graphLock;
    juce::OwnedArray<NodeEntry> nodes;
    std::vector<GraphConnection> connections;
    std::map<juce::String, RuntimeState, std::less<>> runtimeStates;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    bool playing = true;
    int64 transportSamplePosition = 0;
};
