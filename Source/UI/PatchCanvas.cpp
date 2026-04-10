#include "PatchCanvas.h"

namespace
{
constexpr auto nodeWidth = 180;
constexpr auto portHeight = 22;
constexpr auto titleHeight = 34;

juce::Colour colourForPortKind(PortKind kind)
{
    return kind == PortKind::audio ? juce::Colour(0xff4ecdc4) : juce::Colour(0xffff9f1c);
}

class SocketButton final : public juce::Component
{
public:
    SocketButton(SocketRef ref, std::function<void(const SocketRef&)> onClickCallback)
        : socket(std::move(ref)), onClick(std::move(onClickCallback))
    {
        setInterceptsMouseClicks(true, false);
        setRepaintsOnMouseActivity(true);
    }

    void paint(juce::Graphics& g) override
    {
        const auto colour = colourForPortKind(socket.kind)
                                .withMultipliedBrightness(isMouseOverOrDragging() ? 1.2f : 1.0f);
        g.setColour(colour);
        g.fillEllipse(getLocalBounds().toFloat().reduced(2.0f));
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        onClick(socket);
    }

    const SocketRef socket;

private:
    std::function<void(const SocketRef&)> onClick;
};
} // namespace

class PatchCanvas::NodeComponent final : public juce::Component
{
public:
    NodeComponent(NodeSnapshot snapshot,
                  std::function<void(const juce::Uuid&, juce::Point<float>)> moveCallback,
                  std::function<void(const SocketRef&)> socketClickCallback,
                  std::function<void(const juce::Uuid&)> selectCallback)
        : node(std::move(snapshot)),
          onMove(std::move(moveCallback)),
          onSocketClick(std::move(socketClickCallback)),
          onSelect(std::move(selectCallback))
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
        g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        g.drawText(node.name, 14, 8, getWidth() - 28, 20, juce::Justification::centredLeft);

        g.setFont(juce::FontOptions(12.0f));
        g.setColour(juce::Colour(0xff8c9aab));

        auto y = static_cast<int>(titleHeight);

        for (const auto& port : node.inputs)
        {
            g.drawText(port.name, 18, y, getWidth() / 2 - 22, static_cast<int>(portHeight), juce::Justification::centredLeft);
            y += static_cast<int>(portHeight);
        }

        y = static_cast<int>(titleHeight);

        for (const auto& port : node.outputs)
        {
            g.drawText(port.name, getWidth() / 2, y, getWidth() / 2 - 18, static_cast<int>(portHeight), juce::Justification::centredRight);
            y += static_cast<int>(portHeight);
        }
    }

    void resized() override
    {
        auto y = static_cast<int>(titleHeight) + 4;

        for (auto* socket : inputSockets)
        {
            socket->setBounds(6, y, 12, 12);
            y += static_cast<int>(portHeight);
        }

        y = static_cast<int>(titleHeight) + 4;

        for (auto* socket : outputSockets)
        {
            socket->setBounds(getWidth() - 18, y, 12, 12);
            y += static_cast<int>(portHeight);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        dragAnchor = event.getPosition();
        onSelect(node.id);
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        auto newBounds = getBounds();
        newBounds.setPosition(newBounds.getPosition() + event.getPosition() - dragAnchor);
        setBounds(newBounds);
        if (auto* parent = getParentComponent())
            parent->repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        onMove(node.id, getPosition().toFloat());
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
    void buildSockets()
    {
        auto addPortButton = [this](bool isInput, int kindIndex, const PortInfo& info, juce::OwnedArray<SocketButton>& collection)
        {
            auto buttonSocket = SocketRef { node.id, isInput, kindIndex, info.kind };
            auto* button = new SocketButton(buttonSocket, onSocketClick);
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
    std::function<void(const juce::Uuid&, juce::Point<float>)> onMove;
    std::function<void(const SocketRef&)> onSocketClick;
    std::function<void(const juce::Uuid&)> onSelect;
    juce::OwnedArray<SocketButton> inputSockets;
    juce::OwnedArray<SocketButton> outputSockets;
    juce::Point<int> dragAnchor;
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
    graph.removeChangeListener(this);
}

void PatchCanvas::setSelectionChangedCallback(std::function<void(std::optional<juce::Uuid>)> callback)
{
    onSelectionChanged = std::move(callback);
}

std::optional<juce::Uuid> PatchCanvas::getSelectedNode() const
{
    return selectedNode;
}

void PatchCanvas::clearSelection()
{
    selectedNode.reset();

    for (auto* node : nodeComponents)
        node->setSelected(false);

    if (onSelectionChanged)
        onSelectionChanged(std::nullopt);

    repaint();
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
        const auto source = getSocketCanvasPosition(connection.source);
        const auto destination = getSocketCanvasPosition(connection.destination);

        juce::Path cable;
        cable.startNewSubPath(source);

        const auto controlOffset = juce::jmax(60.0f, std::abs(destination.x - source.x) * 0.4f);
        cable.cubicTo(source.translated(controlOffset, 0.0f),
                      destination.translated(-controlOffset, 0.0f),
                      destination);

        g.setColour(colourForPortKind(connection.source.kind).withAlpha(0.9f));
        g.strokePath(cable, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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

bool PatchCanvas::keyPressed(const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selectedNode.has_value())
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
    nodeComponents.clear();

    for (const auto& snapshot : nodes)
    {
        auto* component = new NodeComponent(snapshot,
                                            [this](const juce::Uuid& id, juce::Point<float> position)
                                            {
                                                graph.setNodePosition(id, position);
                                            },
                                            [this](const SocketRef& socket)
                                            {
                                                handleSocketClicked(socket);
                                            },
                                            [this](const juce::Uuid& id)
                                            {
                                                selectedNode = id;

                                                for (auto* node : nodeComponents)
                                                    node->setSelected(node->getComponentID() == id.toString());

                                                if (onSelectionChanged)
                                                    onSelectionChanged(selectedNode);
                                            });

        component->setComponentID(snapshot.id.toString());
        component->setBounds(static_cast<int>(snapshot.position.x),
                             static_cast<int>(snapshot.position.y),
                             nodeWidth,
                             static_cast<int>(titleHeight + portHeight * static_cast<float>(juce::jmax(snapshot.inputs.size(), snapshot.outputs.size())) + 12.0f));
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

void PatchCanvas::handleSocketClicked(const SocketRef& socket)
{
    if (!pendingSocket.has_value())
    {
        pendingSocket = socket;
        repaint();
        return;
    }

    const auto first = *pendingSocket;
    pendingSocket.reset();

    if (first.isInput == socket.isInput)
    {
        repaint();
        return;
    }

    const auto source = first.isInput ? socket : first;
    const auto destination = first.isInput ? first : socket;
    graph.connect(source, destination);
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
