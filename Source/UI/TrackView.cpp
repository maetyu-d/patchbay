#include "TrackView.h"

namespace
{
constexpr int timelineHeight = 30;
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
    auto timeline = rowBounds.removeFromTop(timelineHeight);
    std::vector<NodeSnapshot> tracks;
    for (const auto& node : nodes)
        if (node.isTrack)
            tracks.push_back(node);

    const auto transport = graph.getTransportState();
    g.setColour(juce::Colour(0xff202735));
    g.fillRect(timeline);
    const auto timelineInner = timeline.reduced(10, 6);
    g.setColour(juce::Colour(0xff30394a));
    g.fillRoundedRectangle(timelineInner.toFloat(), 8.0f);

    const auto totalBars = juce::jmax(8, transport.loopEndBar + 3);
    const auto pixelsPerBar = static_cast<float>(timelineInner.getWidth()) / static_cast<float>(totalBars);
    for (int bar = 0; bar < totalBars; ++bar)
    {
        const auto x = timelineInner.getX() + static_cast<int>(std::round(static_cast<float>(bar) * pixelsPerBar));
        g.setColour(bar + 1 >= transport.loopStartBar && bar + 1 < transport.loopEndBar ? juce::Colour(0x44ffd166) : juce::Colour(0xff394355));
        g.drawVerticalLine(x, static_cast<float>(timelineInner.getY() + 2), static_cast<float>(timelineInner.getBottom() - 2));
        g.setColour(juce::Colour(0xffdbe3ec));
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(juce::String(bar + 1), x + 4, timelineInner.getY(), static_cast<int>(pixelsPerBar) - 6, timelineInner.getHeight(), juce::Justification::centredLeft);
    }

    const auto loopStartX = timelineInner.getX() + static_cast<int>(std::round(static_cast<float>(transport.loopStartBar - 1) * pixelsPerBar));
    const auto loopEndX = timelineInner.getX() + static_cast<int>(std::round(static_cast<float>(transport.loopEndBar - 1) * pixelsPerBar));
    g.setColour(juce::Colour(0x33ffd166));
    g.fillRect(juce::Rectangle<int>(loopStartX, timelineInner.getY(), juce::jmax(2, loopEndX - loopStartX), timelineInner.getHeight()));
    g.setColour(juce::Colour(0xffffd166));
    g.drawLine(static_cast<float>(loopStartX), static_cast<float>(timelineInner.getY()),
               static_cast<float>(loopStartX), static_cast<float>(timelineInner.getBottom()), 2.0f);
    g.drawLine(static_cast<float>(loopEndX), static_cast<float>(timelineInner.getY()),
               static_cast<float>(loopEndX), static_cast<float>(timelineInner.getBottom()), 2.0f);

    if (transport.isPlaying)
    {
        const auto barSamples = (60.0 / juce::jmax(1.0, transport.bpm)) * transport.sampleRate * (static_cast<double>(transport.numerator) * (4.0 / static_cast<double>(juce::jmax(1, transport.denominator))));
        const auto currentBarPosition = barSamples > 0.0 ? static_cast<double>(transport.transportSamplePosition) / barSamples : 0.0;
        const auto playheadX = timelineInner.getX() + static_cast<int>(std::round(currentBarPosition * static_cast<double>(pixelsPerBar)));
        g.setColour(juce::Colour(0xffff6b6b));
        g.drawLine(static_cast<float>(playheadX), static_cast<float>(timelineInner.getY()),
                   static_cast<float>(playheadX), static_cast<float>(timelineInner.getBottom()), 2.0f);
    }

    for (size_t index = 0; index < tracks.size(); ++index)
    {
        auto bounds = rowBounds.removeFromTop(rowHeight);
        const auto fullRow = bounds;
        const auto& track = tracks[index];
        const auto isSelected = selectedTrack.has_value() && *selectedTrack == track.id;
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
        const auto rowTotalBars = juce::jmax(8, transport.loopEndBar + 4);
        const auto rowPixelsPerBar = static_cast<float>(regions.getWidth()) / static_cast<float>(rowTotalBars);

        auto drawSmallButton = [&g](juce::Rectangle<int> rect, juce::String text, bool active, juce::Colour activeColour)
        {
            g.setColour(active ? activeColour : juce::Colour(0x33273340));
            g.fillRoundedRectangle(rect.toFloat(), 4.0f);
            g.setColour(active ? juce::Colours::white : juce::Colour(0xff9fb0c4));
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText(text, rect, juce::Justification::centred);
        };

        for (const auto& clip : track.trackClips)
        {
            auto region = juce::Rectangle<int>(regions.getX() + static_cast<int>(std::round((clip.startBar - 1.0f) * rowPixelsPerBar)),
                                               regions.getY() + 8,
                                               juce::jmax(48, static_cast<int>(std::round(clip.lengthBars * rowPixelsPerBar))),
                                               regions.getHeight() - 16);
            region = region.getIntersection(regions.reduced(0, 2));
            g.setColour((track.trackTypeId == "audio" ? juce::Colour(0xff4d96ff) : juce::Colour(0xff5bd17f)).withAlpha(clip.selected ? 0.95f : 0.75f));
            g.fillRoundedRectangle(region.toFloat(), 8.0f);
            g.setColour(track.trackTypeId == "audio" ? juce::Colours::white : juce::Colour(0xff0f141d));
            g.drawText(clip.label, region.reduced(10, 6), juce::Justification::centredLeft);
            if (clip.selected)
            {
                g.setColour(juce::Colour(0xccffffff));
                g.drawRoundedRectangle(region.toFloat(), 8.0f, 2.0f);
            }
        }

        g.setColour(juce::Colour(0x22ffffff));
        for (int bar = 0; bar < rowTotalBars; ++bar)
        {
            const auto x = regions.getX() + static_cast<int>(std::round(static_cast<float>(bar) * rowPixelsPerBar));
            g.drawVerticalLine(x, static_cast<float>(regions.getY()), static_cast<float>(regions.getBottom()));
        }

        auto getParam = [&track](const juce::String& id)
        {
            for (const auto& parameter : track.parameters)
                if (parameter.spec.id == id)
                    return parameter.value;
            return 0.0f;
        };

        drawSmallButton(muteButtonBounds(headerRect), "M", getParam("mute") > 0.5f, juce::Colour(0xffc2410c));
        drawSmallButton(soloButtonBounds(headerRect), "S", getParam("solo") > 0.5f, juce::Colour(0xffca8a04));
        drawSmallButton(armButtonBounds(headerRect), "R", getParam("arm") > 0.5f, juce::Colour(0xffdc2626));

        const auto meters = meterBounds(fullRow);
        g.setColour(juce::Colour(0x22101822));
        g.fillRoundedRectangle(meters.toFloat(), 5.0f);
        const auto leftHeight = static_cast<int>(std::round(track.meterLevels.x * static_cast<float>(meters.getHeight())));
        const auto rightHeight = static_cast<int>(std::round(track.meterLevels.y * static_cast<float>(meters.getHeight())));
        g.setColour(juce::Colour(0xff42d392));
        g.fillRoundedRectangle(juce::Rectangle<int>(meters.getX() + 4, meters.getBottom() - leftHeight, 7, leftHeight).toFloat(), 2.0f);
        g.fillRoundedRectangle(juce::Rectangle<int>(meters.getX() + 15, meters.getBottom() - rightHeight, 7, rightHeight).toFloat(), 2.0f);

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

    auto masterRow = rowBounds.removeFromTop(rowHeight);
    g.setColour(juce::Colour(0xff141a24));
    g.fillRect(masterRow);
    auto masterHeader = masterRow.removeFromLeft(headerWidth);
    g.setColour(juce::Colour(0xff2d3748));
    g.fillRoundedRectangle(masterHeader.toFloat().reduced(6.0f), 10.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    g.drawText("Master", masterHeader.reduced(14, 10).removeFromTop(20), juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffdbe3ec));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("Output / limiter", masterHeader.reduced(14, 10).removeFromBottom(18), juce::Justification::centredLeft);
}

void TrackView::mouseDown(const juce::MouseEvent& event)
{
    if (event.y >= timelineHeight)
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
        auto row = juce::Rectangle<int>(0, timelineHeight + index * rowHeight, getWidth(), rowHeight);
        row.removeFromLeft(headerWidth);
        auto regions = row.reduced(8, 10);
        const auto transport = graph.getTransportState();
        const auto rowTotalBars = juce::jmax(8, transport.loopEndBar + 4);
        const auto rowPixelsPerBar = static_cast<float>(regions.getWidth()) / static_cast<float>(rowTotalBars);

        for (const auto& clip : track.trackClips)
        {
            auto clipBounds = juce::Rectangle<int>(regions.getX() + static_cast<int>(std::round((clip.startBar - 1.0f) * rowPixelsPerBar)),
                                                   regions.getY() + 8,
                                                   juce::jmax(48, static_cast<int>(std::round(clip.lengthBars * rowPixelsPerBar))),
                                                   regions.getHeight() - 16);
            clipBounds = clipBounds.getIntersection(regions.reduced(0, 2));
            if (! clipBounds.contains(event.getPosition()))
                continue;

            graph.setSelectedTrackClip(track.id, clip.clipId);
            selectedTrack = track.id;
            if (onSelectionChanged)
                onSelectionChanged(selectedTrack);

            draggedTrackId = track.id;
            draggedClipId = clip.clipId;
            dragStartBar = clip.startBar;
            dragStartLength = clip.lengthBars;
            dragStartX = event.x;
            clipDragTarget = std::abs(event.x - clipBounds.getX()) < 8 ? ClipDragTarget::resizeLeft
                             : std::abs(event.x - clipBounds.getRight()) < 8 ? ClipDragTarget::resizeRight
                                                                              : ClipDragTarget::move;
            repaint();
            return;
        }

        return;
    }

    const auto transport = graph.getTransportState();
    auto timeline = getLocalBounds().removeFromTop(timelineHeight).reduced(10, 6);
    const auto totalBars = juce::jmax(8, transport.loopEndBar + 3);
    const auto pixelsPerBar = static_cast<float>(timeline.getWidth()) / static_cast<float>(totalBars);
    const auto loopStartX = timeline.getX() + static_cast<int>(std::round(static_cast<float>(transport.loopStartBar - 1) * pixelsPerBar));
    const auto loopEndX = timeline.getX() + static_cast<int>(std::round(static_cast<float>(transport.loopEndBar - 1) * pixelsPerBar));

    if (std::abs(event.x - loopStartX) < 10)
        transportDragTarget = TransportDragTarget::loopStart;
    else if (std::abs(event.x - loopEndX) < 10)
        transportDragTarget = TransportDragTarget::loopEnd;
    else
        transportDragTarget = TransportDragTarget::none;
}

void TrackView::mouseDrag(const juce::MouseEvent& event)
{
    if (transportDragTarget != TransportDragTarget::none)
    {
        const auto transport = graph.getTransportState();
        auto startBar = transport.loopStartBar;
        auto endBar = transport.loopEndBar;
        const auto bar = xToBar(event.x);

        if (transportDragTarget == TransportDragTarget::loopStart)
            startBar = juce::jlimit(1, juce::jmax(1, endBar - 1), bar);
        else
            endBar = juce::jmax(startBar + 1, bar);

        graph.setTransportLoopBars(startBar, endBar);
        return;
    }

    if (clipDragTarget == ClipDragTarget::none || ! draggedTrackId.has_value() || draggedClipId.isEmpty())
        return;

    const auto transport = graph.getTransportState();
    auto timelineRow = getLocalBounds().withTrimmedTop(timelineHeight);
    juce::ignoreUnused(timelineRow);
    const auto rowTotalBars = juce::jmax(8, transport.loopEndBar + 4);
    const auto regionWidth = static_cast<float>(getWidth() - headerWidth - 16);
    const auto barsPerPixel = static_cast<float>(rowTotalBars) / juce::jmax(1.0f, regionWidth);
    const auto deltaBars = static_cast<float>(event.x - dragStartX) * barsPerPixel;

    if (clipDragTarget == ClipDragTarget::move)
        graph.moveTrackClip(*draggedTrackId, draggedClipId, dragStartBar + deltaBars);
    else if (clipDragTarget == ClipDragTarget::resizeLeft)
        graph.resizeTrackClip(*draggedTrackId, draggedClipId, dragStartBar + deltaBars, dragStartLength - deltaBars);
    else if (clipDragTarget == ClipDragTarget::resizeRight)
        graph.resizeTrackClip(*draggedTrackId, draggedClipId, dragStartBar, dragStartLength + deltaBars);
}

void TrackView::mouseUp(const juce::MouseEvent& event)
{
    if (transportDragTarget != TransportDragTarget::none)
    {
        transportDragTarget = TransportDragTarget::none;
        return;
    }

    if (clipDragTarget != ClipDragTarget::none)
    {
        clipDragTarget = ClipDragTarget::none;
        draggedTrackId.reset();
        draggedClipId.clear();
        return;
    }

    if (event.y < timelineHeight)
        return;

    const auto index = getRowAtY(event.y);
    const auto nodes = graph.getNodes();
    std::vector<NodeSnapshot> tracks;
    for (const auto& node : nodes)
        if (node.isTrack)
            tracks.push_back(node);

    if (! juce::isPositiveAndBelow(index, static_cast<int>(tracks.size())))
        return;

    const auto& track = tracks[static_cast<size_t>(index)];
    auto row = juce::Rectangle<int>(0, timelineHeight + index * rowHeight, getWidth(), rowHeight);
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

void TrackView::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (event.y < timelineHeight)
        return;

    const auto index = getRowAtY(event.y);
    const auto nodes = graph.getNodes();
    std::vector<NodeSnapshot> tracks;
    for (const auto& node : nodes)
        if (node.isTrack)
            tracks.push_back(node);

    if (! juce::isPositiveAndBelow(index, static_cast<int>(tracks.size())))
        return;

    const auto& track = tracks[static_cast<size_t>(index)];
    auto row = juce::Rectangle<int>(0, timelineHeight + index * rowHeight, getWidth(), rowHeight);
    row.removeFromLeft(headerWidth);
    auto regions = row.reduced(8, 10);
    if (! regions.contains(event.getPosition()))
        return;

    const auto transport = graph.getTransportState();
    const auto rowTotalBars = juce::jmax(8, transport.loopEndBar + 4);
    const auto rowPixelsPerBar = static_cast<float>(regions.getWidth()) / static_cast<float>(rowTotalBars);
    const auto startBar = 1.0f + static_cast<float>(event.x - regions.getX()) / juce::jmax(1.0f, rowPixelsPerBar);
    graph.addTrackClip(track.id, startBar, 4.0f);
    selectedTrack = track.id;
    if (onSelectionChanged)
        onSelectionChanged(selectedTrack);
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
    return (y - timelineHeight) / rowHeight;
}

int TrackView::xToBar(int x) const
{
    const auto transport = graph.getTransportState();
    auto timeline = getLocalBounds().removeFromTop(timelineHeight).reduced(10, 6);
    const auto totalBars = juce::jmax(8, transport.loopEndBar + 3);
    const auto barWidth = static_cast<float>(timeline.getWidth()) / static_cast<float>(totalBars);
    const auto barIndex = juce::jlimit(0, totalBars - 1, static_cast<int>(std::floor(static_cast<float>(x - timeline.getX()) / juce::jmax(1.0f, barWidth))));
    return barIndex + 1;
}
