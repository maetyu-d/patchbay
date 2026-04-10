#pragma once

#include "../Engine/PatchGraph.h"
#include <functional>
#include <optional>

class PatchCanvas final : public juce::Component,
                          private juce::ChangeListener
{
public:
    explicit PatchCanvas(PatchGraph& graphToEdit);
    ~PatchCanvas() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void setSelectionChangedCallback(std::function<void(std::optional<juce::Uuid>)> callback);
    std::optional<juce::Uuid> getSelectedNode() const;
    void clearSelection();

private:
    class NodeComponent;

    void rebuildNodes();
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void handleSocketClicked(const SocketRef& socket);
    juce::Point<float> getSocketCanvasPosition(const SocketRef& socket) const;

    PatchGraph& graph;
    juce::OwnedArray<NodeComponent> nodeComponents;
    std::optional<SocketRef> pendingSocket;
    std::optional<juce::Uuid> selectedNode;
    std::function<void(std::optional<juce::Uuid>)> onSelectionChanged;
};
