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
    juce::Rectangle<int> embeddedEditorBounds;
    bool supportsEditor = false;
    bool editorOpen = false;
    bool editorDetached = false;
    float editorScale = 1.0f;
    juce::Point<float> meterLevels;
    std::vector<TrackLaneClipPreview> trackClips;
    juce::String selectedClipId;
};

struct TransportState
{
    bool isPlaying = false;
    bool isRecording = false;
    double bpm = 120.0;
    int numerator = 4;
    int denominator = 4;
    bool loopEnabled = false;
    int loopStartBar = 1;
    int loopEndBar = 5;
    double sampleRate = 44100.0;
    int64_t transportSamplePosition = 0;
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
    bool addTrackClip(const juce::Uuid& nodeId, float startBar, float lengthBars);
    bool moveTrackClip(const juce::Uuid& nodeId, const juce::String& clipId, float startBar);
    bool resizeTrackClip(const juce::Uuid& nodeId, const juce::String& clipId, float startBar, float lengthBars);
    bool setSelectedTrackClip(const juce::Uuid& nodeId, const juce::String& clipId);
    bool assignExternalPlugin(const juce::Uuid& nodeId, const juce::String& identifier);
    juce::Component* getNodeEmbeddedEditor(const juce::Uuid& nodeId);
    bool setNodeEditorOpen(const juce::Uuid& nodeId, bool isOpen);
    bool setNodeEditorDetached(const juce::Uuid& nodeId, bool isDetached);
    bool setNodeEditorScale(const juce::Uuid& nodeId, float scale);

    juce::ValueTree createState() const;
    void loadState(const juce::ValueTree& state);

    void prepare(double sampleRate, int samplesPerBlock);
    void render(juce::AudioBuffer<float>& outputBuffer);
    void setPlaying(bool shouldPlay);
    bool isPlaying() const;
    void setRecording(bool shouldRecord);
    bool isRecording() const;
    void resetTransport();
    void setTransportBpm(double newBpm);
    void setTransportTimeSignature(int numerator, int denominator);
    void setTransportLoopEnabled(bool shouldLoop);
    void setTransportLoopBars(int startBar, int endBar);
    TransportState getTransportState() const;
    bool canUndo() const;
    bool canRedo() const;
    bool undo();
    bool redo();

private:
    struct NodeEntry
    {
        juce::Uuid id;
        juce::String typeId;
        juce::String displayName;
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
    juce::String makeUniqueNodeName(const juce::String& baseName) const;
    void pruneInvalidConnectionsForNode(const juce::Uuid& nodeId);
    void pushUndoSnapshot();
    void restoreFromState(const juce::ValueTree& state);
    juce::ValueTree createStateUnlocked() const;

    std::vector<const NodeEntry*> buildRenderOrder() const;

    mutable juce::CriticalSection graphLock;
    juce::OwnedArray<NodeEntry> nodes;
    std::vector<GraphConnection> connections;
    std::map<juce::String, RuntimeState, std::less<>> runtimeStates;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    bool playing = true;
    bool recording = false;
    int64 transportSamplePosition = 0;
    double transportBpm = 120.0;
    int transportNumerator = 4;
    int transportDenominator = 4;
    bool transportLoopEnabled = false;
    int transportLoopStartBar = 1;
    int transportLoopEndBar = 5;
    bool historySuspended = false;
    std::vector<juce::ValueTree> undoStack;
    std::vector<juce::ValueTree> redoStack;
};
