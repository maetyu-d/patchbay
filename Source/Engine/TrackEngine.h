#pragma once

#include <JuceHeader.h>

enum class TrackType
{
    audio,
    midi
};

struct TrackSnapshot
{
    juce::Uuid id;
    juce::String name;
    TrackType type = TrackType::audio;
    juce::Colour colour = juce::Colours::slategrey;
    bool muted = false;
    float volume = 0.8f;
    float pan = 0.0f;
    juce::String clipLabel;
    juce::String filePath;
    bool hasClip = false;
    int rootMidiNote = 60;
    float midiGain = 0.2f;
    juce::Array<int> midiSteps;
};

class TrackEngine final : public juce::ChangeBroadcaster
{
public:
    TrackEngine();

    void prepare(double sampleRate, int samplesPerBlock);
    void render(juce::AudioBuffer<float>& outputBuffer);

    juce::Uuid addAudioTrack();
    juce::Uuid addMidiTrack();

    std::vector<TrackSnapshot> getTracks() const;

    bool setTrackVolume(const juce::Uuid& trackId, float volume);
    bool setTrackPan(const juce::Uuid& trackId, float pan);
    bool setTrackMuted(const juce::Uuid& trackId, bool muted);
    bool setMidiRootNote(const juce::Uuid& trackId, int rootNote);
    bool setMidiGain(const juce::Uuid& trackId, float gain);
    bool setMidiStepActive(const juce::Uuid& trackId, int stepIndex, bool isActive);
    bool loadAudioFile(const juce::Uuid& trackId, const juce::File& file);

    void setPlaying(bool shouldPlay);
    bool isPlaying() const;
    void resetTransport();

    juce::ValueTree createState() const;
    void loadState(const juce::ValueTree& state);

private:
    struct TrackEntry
    {
        juce::Uuid id;
        juce::String name;
        TrackType type = TrackType::audio;
        juce::Colour colour = juce::Colours::slategrey;
        bool muted = false;
        float volume = 0.8f;
        float pan = 0.0f;
        juce::String filePath;
        juce::String clipLabel;
        juce::AudioBuffer<float> audioClip;
        double audioClipSampleRate = 44100.0;
        int rootMidiNote = 60;
        float midiGain = 0.2f;
        std::array<bool, 16> midiSteps {};
        float midiPhase = 0.0f;
    };

    TrackEntry* findTrack(const juce::Uuid& trackId);
    const TrackEntry* findTrack(const juce::Uuid& trackId) const;
    static juce::Colour colourForTrackType(TrackType type, int index);
    TrackSnapshot createSnapshot(const TrackEntry& entry) const;
    bool restoreAudioFile(TrackEntry& entry, const juce::String& path);

    void renderAudioTrack(TrackEntry& track, juce::AudioBuffer<float>& outputBuffer, int64 startSample) const;
    void renderMidiTrack(TrackEntry& track, juce::AudioBuffer<float>& outputBuffer, int64 startSample);

    mutable juce::CriticalSection trackLock;
    juce::OwnedArray<TrackEntry> tracks;
    juce::AudioFormatManager formatManager;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    bool playing = true;
    int64 transportSamplePosition = 0;
};

