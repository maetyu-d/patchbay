#include "TrackView.h"

namespace
{
constexpr int rowHeight = 58;
constexpr int headerWidth = 220;

juce::Rectangle<int> muteButtonBounds(juce::Rectangle<int> header) { return { header.getX() + 12, header.getBottom() - 24, 24, 16 }; }
juce::Rectangle<int> soloButtonBounds(juce::Rectangle<int> header) { return { header.getX() + 40, header.getBottom() - 24, 24, 16 }; }
juce::Rectangle<int> armButtonBounds(juce::Rectangle<int> header) { return { header.getX() + 68, header.getBottom() - 24, 24, 16 }; }
juce::Rectangle<int> meterBounds(juce::Rectangle<int> row) { return { row.getRight() - 44, row.getY() + 10, 26, row.getHeight() - 20 }; }
}

TrackView::TrackView(PatchGraph& graphToView) : graph(graphToView)
{
    graph.addChangeListener(this);
    startTimerHz(24);
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
        const auto fullRow = bounds;
        const auto& track = tracks[index];
        const auto isSelected = selectedTrack.has_value() && *selectedTrack == track.id;
        juce::HashMap<juce::String, float> params;
        for (const auto& parameter : track.parameters)
            params.set(parameter.spec.id, parameter.value);

        g.setColour(isSelected ? juce::Colour(0xff253149) : juce::Colour(0xff1d2430));
        g.fillRect(bounds);

        auto header = bounds.removeFromLeft(headerWidth);
        const auto headerRect = header;
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

        const auto regionWidth = juce::jmax(120, regions.getWidth() / 2);
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
            for (int step = 0; step < 16; ++step)
            {
                auto stepRect = steps.removeFromLeft(stepWidth).reduced(1, 8);
                const auto active = params["step" + juce::String(step + 1)] > 0.5f;
                g.setColour(active ? juce::Colour(0xff0d2f19) : juce::Colour(0x33102218));
                g.fillRoundedRectangle(stepRect.toFloat(), 3.0f);
            }
        }

        auto drawSmallButton = [&g](juce::Rectangle<int> rect, juce::String text, bool active, juce::Colour activeColour)
        {
            g.setColour(active ? activeColour : juce::Colour(0x33273340));
            g.fillRoundedRectangle(rect.toFloat(), 4.0f);
            g.setColour(active ? juce::Colours::white : juce::Colour(0xff9fb0c4));
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText(text, rect, juce::Justification::centred);
        };

        drawSmallButton(muteButtonBounds(headerRect), "M", params["mute"] > 0.5f, juce::Colour(0xffc2410c));
        drawSmallButton(soloButtonBounds(headerRect), "S", params["solo"] > 0.5f, juce::Colour(0xffca8a04));
        drawSmallButton(armButtonBounds(headerRect), "R", params["arm"] > 0.5f, juce::Colour(0xffdc2626));

        const auto meters = meterBounds(fullRow);
        g.setColour(juce::Colour(0x22101822));
        g.fillRoundedRectangle(meters.toFloat(), 5.0f);
        const auto leftHeight = static_cast<int>(std::round(track.meterLevels.x * static_cast<float>(meters.getHeight())));
        const auto rightHeight = static_cast<int>(std::round(track.meterLevels.y * static_cast<float>(meters.getHeight())));
        g.setColour(juce::Colour(0xff42d392));
        g.fillRoundedRectangle(juce::Rectangle<int>(meters.getX() + 4, meters.getBottom() - leftHeight, 7, leftHeight).toFloat(), 2.0f);
        g.fillRoundedRectangle(juce::Rectangle<int>(meters.getX() + 15, meters.getBottom() - rightHeight, 7, rightHeight).toFloat(), 2.0f);

        const auto transport = graph.getTransportState();
        if (transport.isPlaying)
        {
            const auto barSamples = (60.0 / juce::jmax(1.0, transport.bpm)) * transport.sampleRate * (static_cast<double>(transport.numerator) * (4.0 / static_cast<double>(juce::jmax(1, transport.denominator))));
            const auto loopBars = juce::jmax(1, transport.loopEndBar - transport.loopStartBar);
            const auto cycleLength = juce::jmax(1.0, barSamples * static_cast<double>(loopBars));
            const auto cyclePosition = std::fmod(static_cast<double>(transport.transportSamplePosition), cycleLength) / cycleLength;
            const auto playheadX = regions.getX() + static_cast<int>(std::round(cyclePosition * static_cast<double>(regions.getWidth())));
            g.setColour(juce::Colour(0x88ff6b6b));
            g.drawLine(static_cast<float>(playheadX), static_cast<float>(fullRow.getY() + 8),
                       static_cast<float>(playheadX), static_cast<float>(fullRow.getBottom() - 8), 1.6f);
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

    const auto& track = tracks[static_cast<size_t>(index)];
    auto row = juce::Rectangle<int>(0, index * rowHeight, getWidth(), rowHeight);
    auto header = row.removeFromLeft(headerWidth);

    auto toggleParameter = [this, &track](const juce::String& parameterId)
    {
        for (const auto& parameter : track.parameters)
        {
            if (parameter.spec.id == parameterId)
            {
                graph.setNodeParameter(track.id, parameterId, parameter.value > 0.5f ? 0.0f : 1.0f);
                return;
            }
        }
    };

    if (muteButtonBounds(header).contains(event.getPosition()))
        toggleParameter("mute");
    else if (soloButtonBounds(header).contains(event.getPosition()))
        toggleParameter("solo");
    else if (armButtonBounds(header).contains(event.getPosition()))
        toggleParameter("arm");

    selectedTrack = track.id;
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

void TrackView::timerCallback()
{
    if (graph.isPlaying() || graph.isRecording())
        repaint();
}

int TrackView::getRowAtY(int y) const
{
    return y / rowHeight;
}
