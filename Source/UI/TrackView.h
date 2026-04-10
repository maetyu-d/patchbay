#pragma once

#include "../Engine/PatchGraph.h"
#include <functional>
#include <optional>

class TrackView final : public juce::Component,
                        private juce::ChangeListener
{
public:
    explicit TrackView(PatchGraph& graphToView);
    ~TrackView() override;

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& event) override;

    void setSelectionChangedCallback(std::function<void(std::optional<juce::Uuid>)> callback);
    std::optional<juce::Uuid> getSelectedTrack() const;
    void setSelectedTrack(std::optional<juce::Uuid> trackId);

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    int getRowAtY(int y) const;

    PatchGraph& graph;
    std::function<void(std::optional<juce::Uuid>)> onSelectionChanged;
    std::optional<juce::Uuid> selectedTrack;
};
