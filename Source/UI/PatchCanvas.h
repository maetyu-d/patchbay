#pragma once

#include "../Engine/PatchGraph.h"
#include <functional>
#include <map>
#include <optional>

class PatchCanvas final : public juce::Component,
                          private juce::ChangeListener
{
public:
    explicit PatchCanvas(PatchGraph& graphToEdit);
    ~PatchCanvas() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void setSelectionChangedCallback(std::function<void(std::optional<juce::Uuid>)> callback);
    void setCreateNodeCallback(std::function<void(const juce::String&, juce::Point<float>)> callback);
    void setToggleEditModeCallback(std::function<void()> callback);
    void setEditMode(bool shouldEdit);
    std::optional<juce::Uuid> getSelectedNode() const;
    void setSelectedNode(std::optional<juce::Uuid> nodeId);
    void clearSelection();
    void closeDetachedEditors();

private:
    class NodeComponent;
    class DetachedEditorWindow;

    void rebuildNodes();
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void handleSocketClicked(const SocketRef& socket);
    juce::Point<float> getSocketCanvasPosition(const SocketRef& socket) const;
    void syncDetachedEditors(const std::vector<NodeSnapshot>& nodes);
    void openDetachedEditorForNode(const juce::Uuid& nodeId);
    juce::Path createCablePath(const GraphConnection& connection) const;
    juce::String describeConnection(const GraphConnection& connection) const;
    std::optional<GraphConnection> findConnectionAt(juce::Point<float> point) const;
    static bool connectionsMatch(const GraphConnection& lhs, const GraphConnection& rhs);
    juce::Point<float> worldToScreen(juce::Point<float> worldPoint) const;
    juce::Point<float> screenToWorld(juce::Point<float> screenPoint) const;
    void adjustZoom(float zoomDelta);

    PatchGraph& graph;
    juce::OwnedArray<NodeComponent> nodeComponents;
    std::map<juce::String, std::unique_ptr<DetachedEditorWindow>, std::less<>> detachedEditors;
    std::optional<SocketRef> pendingSocket;
    std::optional<juce::Uuid> selectedNode;
    std::optional<GraphConnection> selectedConnection;
    std::optional<GraphConnection> hoveredConnection;
    std::function<void(std::optional<juce::Uuid>)> onSelectionChanged;
    std::function<void(const juce::String&, juce::Point<float>)> onCreateNode;
    std::function<void()> onToggleEditMode;
    bool editMode = false;
    float zoomScale = 1.0f;
    juce::Point<float> viewOffset { 0.0f, 0.0f };
};
