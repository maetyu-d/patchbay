#pragma once

#include "../Engine/PatchGraph.h"
#include <functional>
#include <optional>

class TrackView final : public juce::Component,
                        private juce::ChangeListener,
                        private juce::Timer
{
public:
    explicit TrackView(PatchGraph& graphToView);
    ~TrackView() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    void setSelectionChangedCallback(std::function<void(std::optional<juce::Uuid>)> callback);
    std::optional<juce::Uuid> getSelectedTrack() const;
    void setSelectedTrack(std::optional<juce::Uuid> trackId);

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    int getRowAtY(int y) const;
    int xToBar(int x) const;

    PatchGraph& graph;
    std::function<void(std::optional<juce::Uuid>)> onSelectionChanged;
    std::optional<juce::Uuid> selectedTrack;
    enum class TransportDragTarget
    {
        none,
        loopStart,
        loopEnd
    };
    enum class ClipDragTarget
    {
        none,
        move,
        resizeLeft,
        resizeRight
    };
    TransportDragTarget transportDragTarget = TransportDragTarget::none;
    ClipDragTarget clipDragTarget = ClipDragTarget::none;
    std::optional<juce::Uuid> draggedTrackId;
    juce::String draggedClipId;
    float dragStartBar = 1.0f;
    float dragStartLength = 4.0f;
    int dragStartX = 0;
};
