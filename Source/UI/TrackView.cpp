#include "TrackView.h"

namespace
{
constexpr int rowHeight = 58;
constexpr int headerWidth = 220;
}

TrackView::TrackView(PatchGraph& graphToView) : graph(graphToView)
{
    graph.addChangeListener(this);
}

TrackView::~TrackView()
{
    graph.removeChangeListener(this);
}

void TrackView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff171b24));

    const auto nodes = graph.getNodes();
    auto rowBounds = getLocalBounds();
    std::vector<NodeSnapshot> tracks;
    for (const auto& node : nodes)
        if (node.isTrack)
            tracks.push_back(node);

    for (size_t index = 0; index < tracks.size(); ++index)
    {
        auto bounds = rowBounds.removeFromTop(rowHeight);
        const auto& track = tracks[index];
        const auto isSelected = selectedTrack.has_value() && *selectedTrack == track.id;

        g.setColour(isSelected ? juce::Colour(0xff253149) : juce::Colour(0xff1d2430));
        g.fillRect(bounds);

        auto header = bounds.removeFromLeft(headerWidth);
        g.setColour(track.colour.withAlpha(0.9f));
        g.fillRoundedRectangle(header.toFloat().reduced(6.0f), 10.0f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        g.drawText(track.name, header.reduced(14, 8).removeFromTop(20), juce::Justification::centredLeft);

        g.setFont(juce::FontOptions(11.5f));
        g.setColour(juce::Colour(0xffeff4fa));
        g.drawText(track.trackTypeId == "audio" ? "Audio Track" : "MIDI Track",
                   header.reduced(14, 8).removeFromBottom(18),
                   juce::Justification::centredLeft);

        auto regions = bounds.reduced(8, 10);
        g.setColour(juce::Colour(0xff10141c));
        g.fillRoundedRectangle(regions.toFloat(), 10.0f);

        const auto regionWidth = juce::jmax(120, regions.getWidth() / 3);
        auto region = regions.removeFromLeft(regionWidth);
        region.reduce(8, 8);

        if (track.trackTypeId == "audio")
        {
            g.setColour(track.resourcePath.isNotEmpty() ? juce::Colour(0xff4d96ff) : juce::Colour(0xff455065));
            g.fillRoundedRectangle(region.toFloat(), 8.0f);
            g.setColour(juce::Colours::white);
            g.drawText(track.resourcePath.isNotEmpty() ? juce::File(track.resourcePath).getFileNameWithoutExtension() : "Load audio clip",
                       region.reduced(10, 6),
                       juce::Justification::centredLeft);
        }
        else
        {
            g.setColour(juce::Colour(0xff5bd17f));
            g.fillRoundedRectangle(region.toFloat(), 8.0f);
            g.setColour(juce::Colour(0xff0f141d));
            g.drawText("Pattern Region", region.reduced(10, 6), juce::Justification::centredLeft);

            auto steps = region.reduced(12, 12);
            const auto stepWidth = juce::jmax(6, steps.getWidth() / 16);
            juce::HashMap<juce::String, float> params;
            for (const auto& parameter : track.parameters)
                params.set(parameter.spec.id, parameter.value);

            for (int step = 0; step < 16; ++step)
            {
                auto stepRect = steps.removeFromLeft(stepWidth).reduced(1, 8);
                const auto active = params["step" + juce::String(step + 1)] > 0.5f;
                g.setColour(active ? juce::Colour(0xff0d2f19) : juce::Colour(0x33102218));
                g.fillRoundedRectangle(stepRect.toFloat(), 3.0f);
            }
        }

        g.setColour(juce::Colour(0x22ffffff));
        g.drawHorizontalLine(bounds.getBottom(), 0.0f, static_cast<float>(getWidth()));
    }
}

void TrackView::mouseUp(const juce::MouseEvent& event)
{
    const auto index = getRowAtY(event.y);
    const auto nodes = graph.getNodes();
    std::vector<NodeSnapshot> tracks;
    for (const auto& node : nodes)
        if (node.isTrack)
            tracks.push_back(node);

    if (! juce::isPositiveAndBelow(index, static_cast<int>(tracks.size())))
        return;

    selectedTrack = tracks[static_cast<size_t>(index)].id;
    if (onSelectionChanged)
        onSelectionChanged(selectedTrack);

    repaint();
}

void TrackView::setSelectionChangedCallback(std::function<void(std::optional<juce::Uuid>)> callback)
{
    onSelectionChanged = std::move(callback);
}

std::optional<juce::Uuid> TrackView::getSelectedTrack() const
{
    return selectedTrack;
}

void TrackView::setSelectedTrack(std::optional<juce::Uuid> trackId)
{
    selectedTrack = std::move(trackId);
    repaint();
}

void TrackView::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &graph)
        repaint();
}

int TrackView::getRowAtY(int y) const
{
    return y / rowHeight;
}
