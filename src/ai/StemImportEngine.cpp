#include "StemImportEngine.h"

namespace
{
    using TimePos = tracktion::core::TimePosition;
    using TimeRng = tracktion::core::TimeRange;

    /** Import one audio file onto a track at 0s — transaction-free core
        (mirrors ClipEditor::importAudioFile minus the beginNewTransaction,
        which the AICommandBridge batch already owns). */
    te::WaveAudioClip* importClipNoTxn (const juce::File& audioFile, te::AudioTrack& track)
    {
        juce::AudioFormatManager afm;
        afm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (afm.createReaderFor (audioFile));
        if (reader == nullptr || reader->sampleRate <= 0.0)
            return nullptr;

        const double durationSecs = (double) reader->lengthInSamples / reader->sampleRate;
        te::ClipPosition pos { TimeRng (TimePos::fromSeconds (0.0),
                                        TimePos::fromSeconds (durationSecs)), {} };

        auto* clip = dynamic_cast<te::WaveAudioClip*> (
            track.insertNewClip (te::TrackItem::Type::wave,
                                 audioFile.getFileNameWithoutExtension(), pos, nullptr));
        if (clip != nullptr)
            clip->getSourceFileReference().setToDirectFileReference (audioFile, false); // absolute

        return clip;
    }

    /** Set track volume in dB — transaction-free core (mirrors MixerEngine::setVolume). */
    void setVolumeNoTxn (te::AudioTrack& track, float dB, juce::UndoManager& um)
    {
        if (auto* vp = track.getVolumePlugin())
        {
            const float faderPos = te::decibelsToVolumeFaderPosition (dB);
            vp->volume.setValue (faderPos, &um);
            vp->setSliderPos (faderPos);
        }
    }
}

//==============================================================================
StemImportEngine::StemImportEngine (EditManager& em, MixerEngine& mixer, AICommandBridge& bridge)
    : editManager (em), mixerEngine (mixer), commandBridge (bridge)
{
}

//==============================================================================
void StemImportEngine::applyPlan (const SessionPlan& plan)
{
    if (plan.tracks.isEmpty())
        return;

    commandBridge.enqueue ("KI-Setup: " + juce::String (plan.tracks.size()) + " Stems",
        [this, plan] (juce::UndoManager& um)
        {
            applyPlanNow (plan, um);
        });
}

//==============================================================================
void StemImportEngine::applyPlanNow (const SessionPlan& plan, juce::UndoManager& um)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    // Revalidate the complete batch before mutations: files may have changed
    // while the preview was open. Do not create orphan tracks or buses.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (const auto& planned : plan.tracks)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (planned.file));
        if (! reader || reader->sampleRate <= 0 || reader->lengthInSamples <= 0
            || reader->numChannels == 0)
        {
            if (onError) onError ("Audiodatei konnte nicht gelesen werden: " + planned.file.getFileName());
            return;
        }
    }

    // 1) Stem tracks FIRST (buses must end up at the END of the track list).
    juce::Array<te::AudioTrack*> created;

    for (const auto& planned : plan.tracks)
    {
        auto track = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit),
                                                nullptr);
        if (track == nullptr)
        {
            created.add (nullptr);
            continue;
        }

        track->setName (planned.name);
        track->setColour (planned.colour);
        track->setSoloIsolate (false);

        importClipNoTxn (planned.file, *track);
        setVolumeNoTxn (*track, (float) planned.gainDb, um);

        created.add (track.get());
    }

    // 2) Group buses (appended after the stem tracks → END invariant holds).
    juce::HashMap<juce::String, te::AudioTrack*> buses;
    for (const auto& group : plan.groups)
        if (auto* bus = mixerEngine.createBusChannel (group))
        {
            bus->setColour (SessionPlanner::colourForGroup (group));
            buses.set (group, bus);
        }

    // 3) Routing: track → group bus (bus → master is set by createBusChannel).
    for (int i = 0; i < plan.tracks.size(); ++i)
    {
        auto* track = created[i];
        if (track == nullptr || plan.tracks[i].group.isEmpty())
            continue;

        if (auto* bus = buses[plan.tracks[i].group])
            mixerEngine.routeTrackToBus (*track, *bus);
    }
}
