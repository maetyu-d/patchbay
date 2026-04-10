#include "TrackEngine.h"

namespace
{
constexpr int stereoChannels = 2;
constexpr double bpm = 120.0;

juce::String defaultTrackName(TrackType type, int index)
{
    return type == TrackType::audio ? "Audio " + juce::String(index + 1)
                                    : "MIDI " + juce::String(index + 1);
}

float midiToFrequency(int midiNote)
{
    return 440.0f * std::pow(2.0f, static_cast<float>(midiNote - 69) / 12.0f);
}
} // namespace

TrackEngine::TrackEngine()
{
    formatManager.registerBasicFormats();
}

void TrackEngine::prepare(double sampleRate, int samplesPerBlock)
{
    const juce::ScopedLock lock(trackLock);
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
}

void TrackEngine::render(juce::AudioBuffer<float>& outputBuffer)
{
    const juce::ScopedLock lock(trackLock);

    const auto startSample = transportSamplePosition;

    for (auto* track : tracks)
    {
        if (track->muted)
            continue;

        if (track->type == TrackType::audio)
            renderAudioTrack(*track, outputBuffer, startSample);
        else
            renderMidiTrack(*track, outputBuffer, startSample);
    }

    if (playing)
        transportSamplePosition += outputBuffer.getNumSamples();
}

juce::Uuid TrackEngine::addAudioTrack()
{
    const juce::ScopedLock lock(trackLock);
    auto* track = new TrackEntry();
    track->id = juce::Uuid();
    track->type = TrackType::audio;
    track->name = defaultTrackName(track->type, tracks.size());
    track->colour = colourForTrackType(track->type, tracks.size());
    tracks.add(track);
    sendChangeMessage();
    return track->id;
}

juce::Uuid TrackEngine::addMidiTrack()
{
    const juce::ScopedLock lock(trackLock);
    auto* track = new TrackEntry();
    track->id = juce::Uuid();
    track->type = TrackType::midi;
    track->name = defaultTrackName(track->type, tracks.size());
    track->colour = colourForTrackType(track->type, tracks.size());
    track->midiSteps = { true, false, false, true, false, true, false, false,
                         true, false, true, false, false, true, false, false };
    tracks.add(track);
    sendChangeMessage();
    return track->id;
}

std::vector<TrackSnapshot> TrackEngine::getTracks() const
{
    const juce::ScopedLock lock(trackLock);
    std::vector<TrackSnapshot> snapshots;
    snapshots.reserve(static_cast<size_t>(tracks.size()));

    for (auto* track : tracks)
        snapshots.push_back(createSnapshot(*track));

    return snapshots;
}

bool TrackEngine::setTrackVolume(const juce::Uuid& trackId, float volume)
{
    const juce::ScopedLock lock(trackLock);
    if (auto* track = findTrack(trackId))
    {
        track->volume = juce::jlimit(0.0f, 1.5f, volume);
        sendChangeMessage();
        return true;
    }

    return false;
}

bool TrackEngine::setTrackPan(const juce::Uuid& trackId, float pan)
{
    const juce::ScopedLock lock(trackLock);
    if (auto* track = findTrack(trackId))
    {
        track->pan = juce::jlimit(-1.0f, 1.0f, pan);
        sendChangeMessage();
        return true;
    }

    return false;
}

bool TrackEngine::setTrackMuted(const juce::Uuid& trackId, bool muted)
{
    const juce::ScopedLock lock(trackLock);
    if (auto* track = findTrack(trackId))
    {
        track->muted = muted;
        sendChangeMessage();
        return true;
    }

    return false;
}

bool TrackEngine::setMidiRootNote(const juce::Uuid& trackId, int rootNote)
{
    const juce::ScopedLock lock(trackLock);
    if (auto* track = findTrack(trackId))
    {
        track->rootMidiNote = juce::jlimit(36, 84, rootNote);
        sendChangeMessage();
        return true;
    }

    return false;
}

bool TrackEngine::setMidiGain(const juce::Uuid& trackId, float gain)
{
    const juce::ScopedLock lock(trackLock);
    if (auto* track = findTrack(trackId))
    {
        track->midiGain = juce::jlimit(0.0f, 1.0f, gain);
        sendChangeMessage();
        return true;
    }

    return false;
}

bool TrackEngine::setMidiStepActive(const juce::Uuid& trackId, int stepIndex, bool isActive)
{
    const juce::ScopedLock lock(trackLock);
    if (auto* track = findTrack(trackId))
    {
        if (track->type != TrackType::midi || ! juce::isPositiveAndBelow(stepIndex, static_cast<int>(track->midiSteps.size())))
            return false;

        track->midiSteps[static_cast<size_t>(stepIndex)] = isActive;
        sendChangeMessage();
        return true;
    }

    return false;
}

bool TrackEngine::loadAudioFile(const juce::Uuid& trackId, const juce::File& file)
{
    const juce::ScopedLock lock(trackLock);
    if (auto* track = findTrack(trackId))
    {
        if (track->type != TrackType::audio)
            return false;

        if (restoreAudioFile(*track, file.getFullPathName()))
        {
            sendChangeMessage();
            return true;
        }
    }

    return false;
}

void TrackEngine::setPlaying(bool shouldPlay)
{
    const juce::ScopedLock lock(trackLock);
    playing = shouldPlay;
    sendChangeMessage();
}

bool TrackEngine::isPlaying() const
{
    const juce::ScopedLock lock(trackLock);
    return playing;
}

void TrackEngine::resetTransport()
{
    const juce::ScopedLock lock(trackLock);
    transportSamplePosition = 0;

    for (auto* track : tracks)
        track->midiPhase = 0.0f;

    sendChangeMessage();
}

juce::ValueTree TrackEngine::createState() const
{
    const juce::ScopedLock lock(trackLock);
    juce::ValueTree state("TRACKS");
    state.setProperty("playing", playing, nullptr);

    for (auto* track : tracks)
    {
        juce::ValueTree trackState("TRACK");
        trackState.setProperty("id", track->id.toString(), nullptr);
        trackState.setProperty("name", track->name, nullptr);
        trackState.setProperty("type", track->type == TrackType::audio ? "audio" : "midi", nullptr);
        trackState.setProperty("colour", track->colour.toString(), nullptr);
        trackState.setProperty("muted", track->muted, nullptr);
        trackState.setProperty("volume", track->volume, nullptr);
        trackState.setProperty("pan", track->pan, nullptr);
        trackState.setProperty("filePath", track->filePath, nullptr);
        trackState.setProperty("clipLabel", track->clipLabel, nullptr);
        trackState.setProperty("rootMidiNote", track->rootMidiNote, nullptr);
        trackState.setProperty("midiGain", track->midiGain, nullptr);

        juce::StringArray stepValues;
        for (bool step : track->midiSteps)
            stepValues.add(step ? "1" : "0");

        trackState.setProperty("midiSteps", stepValues.joinIntoString(","), nullptr);
        state.appendChild(trackState, nullptr);
    }

    return state;
}

void TrackEngine::loadState(const juce::ValueTree& state)
{
    const juce::ScopedLock lock(trackLock);
    tracks.clear();
    playing = static_cast<bool>(state.getProperty("playing", true));
    transportSamplePosition = 0;

    for (const auto& child : state)
    {
        if (! child.hasType("TRACK"))
            continue;

        auto* track = new TrackEntry();
        track->id = juce::Uuid(child.getProperty("id").toString());
        track->name = child.getProperty("name").toString();
        track->type = child.getProperty("type").toString() == "midi" ? TrackType::midi : TrackType::audio;
        track->colour = juce::Colour::fromString(child.getProperty("colour").toString());
        track->muted = static_cast<bool>(child.getProperty("muted", false));
        track->volume = static_cast<float>(child.getProperty("volume", 0.8f));
        track->pan = static_cast<float>(child.getProperty("pan", 0.0f));
        track->clipLabel = child.getProperty("clipLabel").toString();
        track->rootMidiNote = static_cast<int>(child.getProperty("rootMidiNote", 60));
        track->midiGain = static_cast<float>(child.getProperty("midiGain", 0.2f));

        const auto stepTokens = juce::StringArray::fromTokens(child.getProperty("midiSteps").toString(), ",", {});
        for (int index = 0; index < juce::jmin(stepTokens.size(), static_cast<int>(track->midiSteps.size())); ++index)
            track->midiSteps[static_cast<size_t>(index)] = stepTokens[index] == "1";

        const auto filePath = child.getProperty("filePath").toString();
        if (track->type == TrackType::audio && filePath.isNotEmpty())
            restoreAudioFile(*track, filePath);

        tracks.add(track);
    }

    sendChangeMessage();
}

TrackEngine::TrackEntry* TrackEngine::findTrack(const juce::Uuid& trackId)
{
    for (auto* track : tracks)
    {
        if (track->id == trackId)
            return track;
    }

    return nullptr;
}

const TrackEngine::TrackEntry* TrackEngine::findTrack(const juce::Uuid& trackId) const
{
    for (auto* track : tracks)
    {
        if (track->id == trackId)
            return track;
    }

    return nullptr;
}

juce::Colour TrackEngine::colourForTrackType(TrackType type, int index)
{
    const juce::Colour colours[] = {
        juce::Colour(0xff4ecdc4),
        juce::Colour(0xfff4a261),
        juce::Colour(0xff7bd389),
        juce::Colour(0xff84a59d)
    };

    const auto base = colours[static_cast<size_t>(index % std::size(colours))];
    return type == TrackType::audio ? base : base.interpolatedWith(juce::Colour(0xff8d99ff), 0.35f);
}

TrackSnapshot TrackEngine::createSnapshot(const TrackEntry& entry) const
{
    TrackSnapshot snapshot;
    snapshot.id = entry.id;
    snapshot.name = entry.name;
    snapshot.type = entry.type;
    snapshot.colour = entry.colour;
    snapshot.muted = entry.muted;
    snapshot.volume = entry.volume;
    snapshot.pan = entry.pan;
    snapshot.clipLabel = entry.clipLabel;
    snapshot.filePath = entry.filePath;
    snapshot.hasClip = entry.audioClip.getNumSamples() > 0;
    snapshot.rootMidiNote = entry.rootMidiNote;
    snapshot.midiGain = entry.midiGain;

    for (bool step : entry.midiSteps)
        snapshot.midiSteps.add(step ? 1 : 0);

    return snapshot;
}

bool TrackEngine::restoreAudioFile(TrackEntry& entry, const juce::String& path)
{
    juce::File file(path);

    if (! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));

    if (reader == nullptr)
        return false;

    juce::AudioBuffer<float> clipBuffer(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    reader->read(&clipBuffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);

    entry.audioClip = std::move(clipBuffer);
    entry.audioClipSampleRate = reader->sampleRate;
    entry.filePath = file.getFullPathName();
    entry.clipLabel = file.getFileNameWithoutExtension();
    return true;
}

void TrackEngine::renderAudioTrack(TrackEntry& track, juce::AudioBuffer<float>& outputBuffer, int64 startSample) const
{
    if (! playing || track.audioClip.getNumSamples() == 0)
        return;

    const auto ratio = track.audioClipSampleRate / currentSampleRate;
    const auto leftGain = track.volume * juce::jlimit(0.0f, 1.0f, 1.0f - juce::jmax(0.0f, track.pan));
    const auto rightGain = track.volume * juce::jlimit(0.0f, 1.0f, 1.0f + juce::jmin(0.0f, track.pan));

    for (int sample = 0; sample < outputBuffer.getNumSamples(); ++sample)
    {
        const auto clipPosition = std::fmod((static_cast<double>(startSample + sample) * ratio),
                                            static_cast<double>(track.audioClip.getNumSamples()));
        const auto clipIndex = static_cast<int>(clipPosition);
        const auto nextIndex = (clipIndex + 1) % track.audioClip.getNumSamples();
        const auto fraction = static_cast<float>(clipPosition - static_cast<double>(clipIndex));

        const auto left = juce::jmap(fraction,
                                     track.audioClip.getSample(0, clipIndex),
                                     track.audioClip.getSample(0, nextIndex));
        const auto rightSourceChannel = juce::jmin(1, track.audioClip.getNumChannels() - 1);
        const auto right = juce::jmap(fraction,
                                      track.audioClip.getSample(rightSourceChannel, clipIndex),
                                      track.audioClip.getSample(rightSourceChannel, nextIndex));

        outputBuffer.addSample(0, sample, left * leftGain);
        if (outputBuffer.getNumChannels() > 1)
            outputBuffer.addSample(1, sample, right * rightGain);
    }
}

void TrackEngine::renderMidiTrack(TrackEntry& track, juce::AudioBuffer<float>& outputBuffer, int64 startSample)
{
    if (! playing)
        return;

    constexpr std::array<int, 8> melody = { 0, 2, 4, 7, 9, 7, 4, 2 };
    const auto samplesPerBeat = currentSampleRate * 60.0 / bpm;
    const auto samplesPerStep = samplesPerBeat / 4.0;
    const auto leftGain = track.volume * juce::jlimit(0.0f, 1.0f, 1.0f - juce::jmax(0.0f, track.pan));
    const auto rightGain = track.volume * juce::jlimit(0.0f, 1.0f, 1.0f + juce::jmin(0.0f, track.pan));

    for (int sample = 0; sample < outputBuffer.getNumSamples(); ++sample)
    {
        const auto absoluteSample = startSample + sample;
        const auto step = static_cast<int>((absoluteSample / samplesPerStep)) % static_cast<int>(track.midiSteps.size());
        const auto positionWithinStep = std::fmod(static_cast<double>(absoluteSample), samplesPerStep) / samplesPerStep;

        if (! track.midiSteps[static_cast<size_t>(step)] || positionWithinStep > 0.78)
            continue;

        const auto note = track.rootMidiNote + melody[static_cast<size_t>(step % static_cast<int>(melody.size()))];
        const auto frequency = midiToFrequency(note);
        const auto phaseDelta = juce::MathConstants<float>::twoPi * frequency / static_cast<float>(currentSampleRate);
        const auto envelope = static_cast<float>(1.0 - (positionWithinStep / 0.78));
        const auto value = std::sin(track.midiPhase) * track.midiGain * envelope;

        outputBuffer.addSample(0, sample, value * leftGain);
        if (outputBuffer.getNumChannels() > 1)
            outputBuffer.addSample(1, sample, value * rightGain);

        track.midiPhase += phaseDelta;
        if (track.midiPhase >= juce::MathConstants<float>::twoPi)
            track.midiPhase -= juce::MathConstants<float>::twoPi;
    }
}

