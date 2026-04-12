#include "PatchCanvas.h"
#include "../Engine/NodeFactory.h"
#include <set>

namespace
{
constexpr auto nodeWidth = 218;
constexpr auto portHeight = 24;
constexpr auto titleHeight = 48;
constexpr auto socketSize = 18;
constexpr auto horizontalPadding = 14;
constexpr auto socketInset = 8;
constexpr auto socketTextGap = 8;
constexpr auto titleBottomGap = 8;
constexpr auto cableHitThickness = 12.0f;
constexpr auto minZoom = 0.5f;
constexpr auto maxZoom = 2.5f;
constexpr auto zoomStep = 0.1f;
constexpr auto panStep = 48.0f;

juce::Colour colourForPortKind(PortKind kind)
{
    return kind == PortKind::audio ? juce::Colour(0xff4ecdc4) : juce::Colour(0xffff9f1c);
}

class SocketButton final : public juce::Component
{
public:
    SocketButton(SocketRef ref,
                 std::function<void(const SocketRef&)> onClickCallback,
                 std::function<juce::Colour(const SocketRef&, bool)> colourResolverCallback)
        : socket(std::move(ref)),
          onClick(std::move(onClickCallback)),
          colourResolver(std::move(colourResolverCallback))
    {
        setInterceptsMouseClicks(true, false);
        setRepaintsOnMouseActivity(true);
    }

    void paint(juce::Graphics& g) override
    {
        const auto colour = colourResolver != nullptr
                                ? colourResolver(socket, isMouseOverOrDragging())
                                : colourForPortKind(socket.kind);
        g.setColour(colour);
        g.fillEllipse(getLocalBounds().toFloat().reduced(3.0f));
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        onClick(socket);
    }

    const SocketRef socket;

private:
    std::function<void(const SocketRef&)> onClick;
    std::function<juce::Colour(const SocketRef&, bool)> colourResolver;
};
} // namespace

class PatchCanvas::DetachedEditorWindow final : public juce::DocumentWindow
{
public:
    DetachedEditorWindow(const NodeSnapshot& snapshot,
                         juce::Component& editorComponent,
                         std::function<void()> closeCallback)
        : juce::DocumentWindow(snapshot.name + " Editor",
                               juce::Colour(0xff10141c),
                               juce::DocumentWindow::closeButton),
          onClose(std::move(closeCallback))
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setContentNonOwned(&editorComponent, false);
        const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        const auto userArea = display != nullptr ? display->userArea.reduced(24) : juce::Rectangle<int>(1200, 800);
        const auto preferredWidth = juce::jmax(360, snapshot.embeddedEditorBounds.getWidth() + 24);
        const auto preferredHeight = juce::jmax(240, snapshot.embeddedEditorBounds.getHeight() + 48);
        const auto fittedWidth = juce::jmin(preferredWidth, userArea.getWidth());
        const auto fittedHeight = juce::jmin(preferredHeight, userArea.getHeight());
        auto fittedBounds = juce::Rectangle<int>(fittedWidth, fittedHeight).withCentre(userArea.getCentre());
        fittedBounds.setPosition(juce::jlimit(userArea.getX(), userArea.getRight() - fittedBounds.getWidth(), fittedBounds.getX()),
                                 juce::jlimit(userArea.getY(), userArea.getBottom() - fittedBounds.getHeight(), fittedBounds.getY()));
        setBounds(fittedBounds);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        if (onClose)
            onClose();
    }

    void bringToFront()
    {
        setVisible(true);
        toFront(true);
    }

private:
    std::function<void()> onClose;
};

class PatchCanvas::NodeComponent final : public juce::Component
{
public:
    NodeComponent(NodeSnapshot snapshot,
                  bool editable,
                  float scale,
                  std::function<void(const juce::Uuid&, juce::Point<float>)> moveCallback,
                  std::function<void(const SocketRef&)> socketClickCallback,
                  std::function<void(const juce::Uuid&)> selectCallback,
                  std::function<void(const juce::Uuid&)> openEditorCallback,
                  std::function<juce::Colour(const SocketRef&, bool)> socketColourResolver)
        : node(std::move(snapshot)),
          isEditable(editable),
          zoomScale(scale),
          onMove(std::move(moveCallback)),
          onSocketClick(std::move(socketClickCallback)),
          onSelect(std::move(selectCallback)),
          onOpenEditor(std::move(openEditorCallback)),
          resolveSocketColour(std::move(socketColourResolver))
    {
        buildSockets();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(isSelected ? juce::Colour(0xff303c55) : juce::Colour(0xff212733));
        g.fillRoundedRectangle(bounds, 14.0f);

        g.setColour(isSelected ? juce::Colours::white : juce::Colour(0xffa9b4c2));
        g.drawRoundedRectangle(bounds.reduced(1.0f), 14.0f, isSelected ? 2.0f : 1.0f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(18.0f * zoomScale, juce::Font::bold));
        g.drawText(node.name,
                   scaled(horizontalPadding),
                   scaled(10),
                   getWidth() - scaled(horizontalPadding * 2),
                   scaled(26),
                   juce::Justification::centredLeft);

        g.setFont(juce::FontOptions(11.5f * zoomScale));
        g.setColour(juce::Colour(0xff8c9aab));

        const auto inputTextX = scaled(socketInset + socketSize + socketTextGap);
        const auto outputTextWidth = getWidth() / 2 - scaled(socketInset + socketSize + socketTextGap + horizontalPadding);
        auto y = scaled(titleHeight + titleBottomGap);

        for (const auto& port : node.inputs)
        {
            g.drawText(port.name,
                       inputTextX,
                       y,
                       getWidth() / 2 - inputTextX - scaled(10),
                       scaled(portHeight),
                       juce::Justification::centredLeft);
            y += scaled(portHeight);
        }

        y = scaled(titleHeight + titleBottomGap);

        for (const auto& port : node.outputs)
        {
            g.drawText(port.name,
                       getWidth() / 2,
                       y,
                       outputTextWidth,
                       scaled(portHeight),
                       juce::Justification::centredRight);
            y += scaled(portHeight);
        }

    }

    void resized() override
    {
        auto y = scaled(titleHeight + titleBottomGap) + scaled(2);

        for (auto* socket : inputSockets)
        {
            socket->setBounds(scaled(socketInset), y - scaled(1), scaled(socketSize), scaled(socketSize));
            y += scaled(portHeight);
        }

        y = scaled(titleHeight + titleBottomGap) + scaled(2);

        for (auto* socket : outputSockets)
        {
            socket->setBounds(getWidth() - scaled(socketSize) - scaled(socketInset),
                              y - scaled(1),
                              scaled(socketSize),
                              scaled(socketSize));
            y += scaled(portHeight);
        }

    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        onSelect(node.id);

        if (! isEditable)
            return;

        dragAnchor = event.getPosition();
        dragStartPosition = getPosition();
        dragStartScreenPosition = event.getScreenPosition();
        event.source.enableUnboundedMouseMovement(true, false);
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (! isEditable)
            return;

        auto newBounds = getBounds();
        const auto screenDelta = event.getScreenPosition() - dragStartScreenPosition;
        newBounds.setPosition(dragStartPosition + screenDelta);
        setBounds(newBounds);
        if (auto* parent = getParentComponent())
            parent->repaint();
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        event.source.enableUnboundedMouseMovement(false);
        onMove(node.id, getPosition().toFloat());
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        onSelect(node.id);

        if (node.supportsEditor)
            onOpenEditor(node.id);
    }

    void setSelected(bool selected)
    {
        isSelected = selected;
        repaint();
    }

    juce::Point<float> getSocketPosition(const SocketRef& socket) const
    {
        const auto* lookup = socket.isInput ? &inputSockets : &outputSockets;

        for (auto* button : *lookup)
        {
            if (button->socket.portIndex == socket.portIndex && button->socket.kind == socket.kind)
                return button->getBounds().getCentre().toFloat() + getPosition().toFloat();
        }

        return getLocalBounds().getCentre().toFloat() + getPosition().toFloat();
    }

private:
    int scaled(float value) const
    {
        return juce::jmax(1, static_cast<int>(std::round(value * zoomScale)));
    }

    void buildSockets()
    {
        auto addPortButton = [this](bool isInput, int kindIndex, const PortInfo& info, juce::OwnedArray<SocketButton>& collection)
        {
            auto buttonSocket = SocketRef { node.id, isInput, kindIndex, info.kind };
            auto* button = new SocketButton(buttonSocket, onSocketClick, resolveSocketColour);
            collection.add(button);
            addAndMakeVisible(button);
        };

        auto audioInputIndex = 0;
        auto modInputIndex = 0;

        for (const auto& input : node.inputs)
            addPortButton(true, input.kind == PortKind::audio ? audioInputIndex++ : modInputIndex++, input, inputSockets);

        auto audioOutputIndex = 0;
        auto modOutputIndex = 0;

        for (const auto& output : node.outputs)
            addPortButton(false, output.kind == PortKind::audio ? audioOutputIndex++ : modOutputIndex++, output, outputSockets);
    }

    NodeSnapshot node;
    bool isEditable = false;
    float zoomScale = 1.0f;
    std::function<void(const juce::Uuid&, juce::Point<float>)> onMove;
    std::function<void(const SocketRef&)> onSocketClick;
    std::function<void(const juce::Uuid&)> onSelect;
    std::function<void(const juce::Uuid&)> onOpenEditor;
    std::function<juce::Colour(const SocketRef&, bool)> resolveSocketColour;
    juce::OwnedArray<SocketButton> inputSockets;
    juce::OwnedArray<SocketButton> outputSockets;
    juce::Point<int> dragAnchor;
    juce::Point<int> dragStartPosition;
    juce::Point<int> dragStartScreenPosition;
    bool isSelected = false;
};

PatchCanvas::PatchCanvas(PatchGraph& graphToEdit) : graph(graphToEdit)
{
    graph.addChangeListener(this);
    setWantsKeyboardFocus(true);
    rebuildNodes();
}

PatchCanvas::~PatchCanvas()
{
    detachedEditors.clear();
    graph.removeChangeListener(this);
}

void PatchCanvas::setSelectionChangedCallback(std::function<void(std::optional<juce::Uuid>)> callback)
{
    onSelectionChanged = std::move(callback);
}

void PatchCanvas::setCreateNodeCallback(std::function<void(const juce::String&, juce::Point<float>)> callback)
{
    onCreateNode = std::move(callback);
}

void PatchCanvas::setToggleEditModeCallback(std::function<void()> callback)
{
    onToggleEditMode = std::move(callback);
}

void PatchCanvas::setEditMode(bool shouldEdit)
{
    editMode = shouldEdit;
    pendingSocket.reset();
    selectedConnection.reset();
    repaint();
}

std::optional<juce::Uuid> PatchCanvas::getSelectedNode() const
{
    return selectedNode;
}

void PatchCanvas::setSelectedNode(std::optional<juce::Uuid> nodeId)
{
    selectedNode = std::move(nodeId);

    for (auto* node : nodeComponents)
        node->setSelected(selectedNode.has_value() && node->getComponentID() == selectedNode->toString());

    if (onSelectionChanged)
        onSelectionChanged(selectedNode);

    repaint();
}

void PatchCanvas::clearSelection()
{
    setSelectedNode(std::nullopt);
}

void PatchCanvas::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff131720));

    g.setColour(juce::Colour(0xff1e2532));

    for (int x = 0; x < getWidth(); x += 24)
        g.drawVerticalLine(x, 0.0f, static_cast<float>(getHeight()));

    for (int y = 0; y < getHeight(); y += 24)
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));

    for (const auto& connection : graph.getConnections())
    {
        const auto cable = createCablePath(connection);
        const auto isSelected = selectedConnection.has_value() && connectionsMatch(*selectedConnection, connection);

        g.setColour((isSelected ? juce::Colours::white : colourForPortKind(connection.source.kind))
                        .withAlpha(isSelected ? 0.95f : 0.9f));
        g.strokePath(cable, juce::PathStrokeType(isSelected ? 5.0f : 3.5f,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    if (pendingSocket.has_value())
    {
        const auto source = getSocketCanvasPosition(*pendingSocket);
        const auto current = getMouseXYRelative().toFloat();

        juce::Path cable;
        cable.startNewSubPath(source);
        cable.cubicTo(source.translated(90.0f, 0.0f), current.translated(-90.0f, 0.0f), current);

        g.setColour(colourForPortKind(pendingSocket->kind).withAlpha(0.5f));
        g.strokePath(cable, juce::PathStrokeType(2.0f));
    }
}

void PatchCanvas::resized()
{
    rebuildNodes();
}

void PatchCanvas::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
        return;

    if (! editMode)
    {
        selectedConnection.reset();
        return;
    }

    if (const auto hitConnection = findConnectionAt(event.position))
    {
        selectedConnection = hitConnection;
        selectedNode.reset();

        for (auto* node : nodeComponents)
            node->setSelected(false);

        if (onSelectionChanged)
            onSelectionChanged(std::nullopt);

        repaint();
        return;
    }

    selectedConnection.reset();
    clearSelection();
}

void PatchCanvas::mouseUp(const juce::MouseEvent& event)
{
    if (! editMode || ! event.mods.isPopupMenu() || onCreateNode == nullptr)
        return;

    juce::PopupMenu menu;
    auto itemId = 1;
    const auto menuPosition = event.getPosition().toFloat();

    for (const auto& type : NodeFactory::getAvailableTypes())
        menu.addItem(itemId++, type);

    const auto popupArea = juce::Rectangle<int>(event.getScreenPosition().x, event.getScreenPosition().y, 1, 1);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(popupArea),
                       [this, menuPosition](int result)
                       {
                           if (result <= 0)
                               return;

                           const auto types = NodeFactory::getAvailableTypes();
                           const auto index = result - 1;

                           if (juce::isPositiveAndBelow(index, types.size()))
                               onCreateNode(types[index], screenToWorld(menuPosition));
                       });
}

bool PatchCanvas::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress('e', juce::ModifierKeys::commandModifier, 0)
        || key == juce::KeyPress('E', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        if (onToggleEditMode)
            onToggleEditMode();
        return true;
    }

    const auto textCharacter = key.getTextCharacter();

    if (textCharacter == '=' || textCharacter == '+')
    {
        adjustZoom(zoomStep);
        return true;
    }

    if (textCharacter == '-' || textCharacter == '_')
    {
        adjustZoom(-zoomStep);
        return true;
    }

    if (key == juce::KeyPress::leftKey)
    {
        viewOffset += { panStep, 0.0f };
        rebuildNodes();
        return true;
    }

    if (key == juce::KeyPress::rightKey)
    {
        viewOffset += { -panStep, 0.0f };
        rebuildNodes();
        return true;
    }

    if (key == juce::KeyPress::upKey)
    {
        viewOffset += { 0.0f, panStep };
        rebuildNodes();
        return true;
    }

    if (key == juce::KeyPress::downKey)
    {
        viewOffset += { 0.0f, -panStep };
        rebuildNodes();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        pendingSocket.reset();
        selectedConnection.reset();
        repaint();
        return true;
    }

    if (editMode && (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selectedConnection.has_value())
    {
        graph.disconnect(*selectedConnection);
        selectedConnection.reset();
        return true;
    }

    if (editMode && (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selectedNode.has_value())
    {
        graph.removeNode(*selectedNode);
        selectedNode.reset();
        pendingSocket.reset();
        if (onSelectionChanged)
            onSelectionChanged(std::nullopt);
        return true;
    }

    return false;
}

void PatchCanvas::rebuildNodes()
{
    const auto nodes = graph.getNodes();
    syncDetachedEditors(nodes);
    nodeComponents.clear();

    for (const auto& snapshot : nodes)
    {
        auto* component = new NodeComponent(snapshot,
                                            editMode,
                                            zoomScale,
                                            [this](const juce::Uuid& id, juce::Point<float> position)
                                            {
                                                graph.setNodePosition(id, screenToWorld(position));
                                            },
                                            [this](const SocketRef& socket)
                                            {
                                                handleSocketClicked(socket);
                                            },
                                            [this](const juce::Uuid& id)
                                            {
                                                selectedNode = id;
                                                selectedConnection.reset();

                                                for (auto* node : nodeComponents)
                                                    node->setSelected(node->getComponentID() == id.toString());

                                                if (onSelectionChanged)
                                                    onSelectionChanged(selectedNode);
                                            },
                                            [this](const juce::Uuid& id)
                                            {
                                                openDetachedEditorForNode(id);
                                            },
                                            [this](const SocketRef& socket, bool isHovered)
                                            {
                                                auto colour = colourForPortKind(socket.kind);

                                                if (! editMode)
                                                    return colour.withAlpha(0.45f);

                                                if (! pendingSocket.has_value())
                                                    return colour.withMultipliedBrightness(isHovered ? 1.25f : 1.0f);

                                                if (pendingSocket->nodeId == socket.nodeId
                                                    && pendingSocket->isInput == socket.isInput
                                                    && pendingSocket->portIndex == socket.portIndex
                                                    && pendingSocket->kind == socket.kind)
                                                    return juce::Colours::white;

                                                const auto compatible = pendingSocket->isInput != socket.isInput
                                                                     && pendingSocket->kind == socket.kind
                                                                     && pendingSocket->nodeId != socket.nodeId;

                                                if (compatible)
                                                    return colour.withMultipliedBrightness(isHovered ? 1.45f : 1.2f);

                                                return colour.withAlpha(0.22f);
                                            });

        component->setComponentID(snapshot.id.toString());
        const auto screenPosition = worldToScreen(snapshot.position);
        const auto nodeHeight = titleHeight + portHeight * static_cast<float>(juce::jmax(snapshot.inputs.size(), snapshot.outputs.size())) + 12.0f;
        component->setBounds(static_cast<int>(std::round(screenPosition.x)),
                             static_cast<int>(std::round(screenPosition.y)),
                             static_cast<int>(std::round(static_cast<float>(nodeWidth) * zoomScale)),
                             static_cast<int>(std::round(nodeHeight * zoomScale)));
        component->setSelected(selectedNode.has_value() && *selectedNode == snapshot.id);
        addAndMakeVisible(component);
        nodeComponents.add(component);
    }

    repaint();
}

void PatchCanvas::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &graph)
        rebuildNodes();
}

void PatchCanvas::syncDetachedEditors(const std::vector<NodeSnapshot>& nodes)
{
    std::set<juce::String, std::less<>> activeIds;

    for (const auto& snapshot : nodes)
    {
        if (! snapshot.supportsEditor || ! snapshot.editorOpen || ! snapshot.editorDetached)
            continue;

        const auto key = snapshot.id.toString();
        activeIds.insert(key);

        if (detachedEditors.find(key) == detachedEditors.end())
        {
            if (auto* editor = graph.getNodeEmbeddedEditor(snapshot.id))
            {
                detachedEditors[key] = std::make_unique<DetachedEditorWindow>(
                    snapshot,
                    *editor,
                    [this, nodeId = snapshot.id]
                    {
                        graph.setNodeEditorDetached(nodeId, false);
                        graph.setNodeEditorOpen(nodeId, false);
                    });
            }
        }
    }

    for (auto it = detachedEditors.begin(); it != detachedEditors.end();)
    {
        if (activeIds.count(it->first) == 0)
            it = detachedEditors.erase(it);
        else
            ++it;
    }
}

void PatchCanvas::openDetachedEditorForNode(const juce::Uuid& nodeId)
{
    const auto key = nodeId.toString();

    if (const auto it = detachedEditors.find(key); it != detachedEditors.end())
    {
        it->second->bringToFront();
        return;
    }

    graph.setNodeEditorOpen(nodeId, true);
    graph.setNodeEditorDetached(nodeId, true);
}

juce::Path PatchCanvas::createCablePath(const GraphConnection& connection) const
{
    const auto source = getSocketCanvasPosition(connection.source);
    const auto destination = getSocketCanvasPosition(connection.destination);

    juce::Path cable;
    cable.startNewSubPath(source);

    const auto controlOffset = juce::jmax(60.0f, std::abs(destination.x - source.x) * 0.4f);
    cable.cubicTo(source.translated(controlOffset, 0.0f),
                  destination.translated(-controlOffset, 0.0f),
                  destination);
    return cable;
}

std::optional<GraphConnection> PatchCanvas::findConnectionAt(juce::Point<float> point) const
{
    for (const auto& connection : graph.getConnections())
    {
        const auto cable = createCablePath(connection);

        if (cable.getBounds().expanded(cableHitThickness).contains(point))
            if (cable.intersectsLine({ point.translated(-cableHitThickness, 0.0f),
                                       point.translated(cableHitThickness, 0.0f) })
                || cable.intersectsLine({ point.translated(0.0f, -cableHitThickness),
                                          point.translated(0.0f, cableHitThickness) }))
                return connection;
    }

    return std::nullopt;
}

bool PatchCanvas::connectionsMatch(const GraphConnection& lhs, const GraphConnection& rhs)
{
    return lhs.source.nodeId == rhs.source.nodeId
        && lhs.source.portIndex == rhs.source.portIndex
        && lhs.source.kind == rhs.source.kind
        && lhs.destination.nodeId == rhs.destination.nodeId
        && lhs.destination.portIndex == rhs.destination.portIndex
        && lhs.destination.kind == rhs.destination.kind;
}

juce::Point<float> PatchCanvas::worldToScreen(juce::Point<float> worldPoint) const
{
    return { worldPoint.x * zoomScale + viewOffset.x,
             worldPoint.y * zoomScale + viewOffset.y };
}

juce::Point<float> PatchCanvas::screenToWorld(juce::Point<float> screenPoint) const
{
    return { (screenPoint.x - viewOffset.x) / zoomScale,
             (screenPoint.y - viewOffset.y) / zoomScale };
}

void PatchCanvas::adjustZoom(float zoomDelta)
{
    const auto oldZoom = zoomScale;
    const auto newZoom = juce::jlimit(minZoom, maxZoom, zoomScale + zoomDelta);

    if (std::abs(newZoom - oldZoom) < 0.0001f)
        return;

    const auto viewCentre = getLocalBounds().toFloat().getCentre();
    const auto worldCentre = screenToWorld(viewCentre);
    zoomScale = newZoom;
    viewOffset = { viewCentre.x - worldCentre.x * zoomScale,
                   viewCentre.y - worldCentre.y * zoomScale };
    rebuildNodes();
}

void PatchCanvas::handleSocketClicked(const SocketRef& socket)
{
    if (! editMode)
        return;

    if (!pendingSocket.has_value())
    {
        pendingSocket = socket;
        selectedConnection.reset();
        repaint();
        return;
    }

    const auto first = *pendingSocket;

    if (first.nodeId == socket.nodeId
        && first.isInput == socket.isInput
        && first.portIndex == socket.portIndex
        && first.kind == socket.kind)
    {
        pendingSocket.reset();
        repaint();
        return;
    }

    if (first.isInput == socket.isInput || first.kind != socket.kind || first.nodeId == socket.nodeId)
    {
        pendingSocket = socket;
        repaint();
        return;
    }

    pendingSocket.reset();

    const auto source = first.isInput ? socket : first;
    const auto destination = first.isInput ? first : socket;
    graph.connect(source, destination);
    selectedConnection.reset();
    repaint();
}

juce::Point<float> PatchCanvas::getSocketCanvasPosition(const SocketRef& socket) const
{
    for (auto* node : nodeComponents)
    {
        if (node->getComponentID() == socket.nodeId.toString())
            return node->getSocketPosition(socket);
    }

    return {};
}
