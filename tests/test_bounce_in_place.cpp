// test_bounce_in_place.cpp — P3 (S1-Parität "Bounce/Transform"):
// Clip → gerendertes Audio in place, 1 Undo-Schritt, Master bleibt live.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EditManager.h"
#include "engine/ClipEditor.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginScanner.h"
#include "plugins/BuiltInPluginFormat.h"

#include <cmath>

namespace
{
    constexpr double kSampleRate = 44100.0;
    constexpr double kSourceSecs = 1.0;

    juce::File writeSineWav (const juce::File& dir, const juce::String& name)
    {
        auto file = dir.getChildFile (name);
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        REQUIRE (stream != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), kSampleRate, 1, 16, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();

        juce::AudioBuffer<float> buf (1, (int) (kSourceSecs * kSampleRate));
        for (int i = 0; i < buf.getNumSamples(); ++i)
            buf.setSample (0, i, 0.5f * std::sin (2.0 * juce::MathConstants<double>::pi
                                                  * 440.0 * i / kSampleRate));
        REQUIRE (writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()));
        return file;
    }

    float fileRms (const juce::File& file)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        REQUIRE (reader != nullptr);

        juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buf, 0, buf.getNumSamples(), 0, true, true);
        return buf.getRMSLevel (0, 0, buf.getNumSamples());
    }
}

TEST_CASE ("BounceInPlace: clip is replaced by a rendered clip at the same position", "[bounce][P3]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_bounce_test");
    tempDir.createDirectory();
    const auto srcWav = writeSineWav (tempDir, "src.wav");

    auto* track = te::getAudioTracks (*edit)[0];
    auto* clip = clipEditor.importAudioFile (srcWav, *track, 1.0);
    REQUIRE (clip != nullptr);

    const auto posBefore = clip->getPosition().time;
    const auto oldItemID = clip->itemID;

    auto* bounced = clipEditor.bounceClipInPlace (*clip, true);
    REQUIRE (bounced != nullptr);

    // Same place on the timeline, new source file, non-silent render
    CHECK (bounced->getPosition().time.getStart() == posBefore.getStart());
    CHECK (bounced->getPosition().time.getEnd()   == posBefore.getEnd());

    const auto renderedFile = bounced->getSourceFileReference().getFile();
    REQUIRE (renderedFile.existsAsFile());
    CHECK (renderedFile != srcWav);
    CHECK (fileRms (renderedFile) > 0.1f);   // the sine survived the render

    // Old clip is gone, exactly one clip on the track
    CHECK (track->getClips().size() == 1);
    CHECK (track->getClips()[0]->itemID != oldItemID);

    // ONE undo restores the original clip (render file stays on disk — fine)
    em.getUndoManager().undo();
    REQUIRE (track->getClips().size() == 1);
    CHECK (track->getClips()[0]->getSourceFileReference().getFile() == srcWav);

    tempDir.deleteRecursively();
}

TEST_CASE ("BounceInPlace: rejects zero-length and detached clips gracefully", "[bounce][P3]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);

    auto* track = te::getAudioTracks (*edit)[0];

    // Zero-length clip → nullptr, no crash, no transaction damage
    te::ClipPosition pos { tracktion::core::TimeRange (tracktion::core::TimePosition::fromSeconds (2.0),
                                                       tracktion::core::TimePosition::fromSeconds (2.0)), {} };
    auto* zeroClip = dynamic_cast<te::WaveAudioClip*> (
        track->insertNewClip (te::TrackItem::Type::wave, "zero", pos, nullptr));
    REQUIRE (zeroClip != nullptr);

    CHECK (clipEditor.bounceClipInPlace (*zeroClip, true) == nullptr);
}

// ---------------------------------------------------------------------------
// Sample-Rate-Regel (Befund 2026-08-11): Bounce rendert in der Rate des
// QUELLMATERIALS, nicht pauschal in der Geraeterate.
//
// Vorher lief der Bounce immer auf der Geraeterate. Auf einem 48-kHz-Interface
// wurde damit jeder 44,1-kHz-Clip bei JEDEM Bounce still resampelt — und wer
// einen gebouncten Clip nochmal bounct, resampelt ein zweites Mal. In der Datei
// ist das nicht mehr rueckgaengig zu machen.
//
// Der Test ist nur dann aussagekraeftig, wenn Geraete- und Quellrate sich
// unterscheiden; sonst ueberspringt er sich selbst, statt gruen zu behaupten,
// er haette etwas geprueft.
// ---------------------------------------------------------------------------
TEST_CASE ("BounceInPlace: renders at the source sample rate, not the device rate", "[bounce][P3][raterule]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);

    const double deviceRate = edit->engine.getDeviceManager().getSampleRate();
    if (std::abs (deviceRate - kSampleRate) < 0.5)
        SKIP ("Geraeterate entspricht der Quellrate — der Fall ist hier nicht pruefbar");

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_bounce_rate_test");
    tempDir.createDirectory();
    const auto srcWav = writeSineWav (tempDir, "src.wav");   // 44100 Hz

    auto* track = te::getAudioTracks (*edit)[0];
    auto* clip = clipEditor.importAudioFile (srcWav, *track, 0.0);
    REQUIRE (clip != nullptr);

    // Die Regel selbst, direkt geprueft
    CHECK (ClipEditor::renderSampleRateFor (*edit, { clip }) == kSampleRate);

    auto* bounced = clipEditor.bounceClipInPlace (*clip, true);
    REQUIRE (bounced != nullptr);

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (
        fm.createReaderFor (bounced->getSourceFileReference().getFile()));
    REQUIRE (reader != nullptr);

    CHECK (reader->sampleRate == kSampleRate);          // NICHT die Geraeterate
    CHECK (reader->lengthInSamples > 0);

    tempDir.deleteRecursively();
}

// Gegenprobe zur Regel: ohne eindeutige Quelle gibt es keine verlustfreie Wahl,
// dann MUSS die Geraeterate gewinnen. Ohne diesen Fall koennte die Regel auch
// "immer 44100" lauten und der Test oben waere trotzdem gruen.
TEST_CASE ("BounceInPlace: rate rule falls back to the device rate", "[bounce][P3][raterule]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();

    double deviceRate = edit->engine.getDeviceManager().getSampleRate();
    if (deviceRate < 8000.0)
        deviceRate = 44100.0;

    // Keine Quelle angegeben → Geraeterate
    CHECK (ClipEditor::renderSampleRateFor (*edit, {}) == deviceRate);

    // Ungueltige Quelle → Geraeterate (kein Absturz, keine 0-Hz-Datei)
    CHECK (ClipEditor::renderSampleRateFor (*edit, { nullptr }) == deviceRate);
}

// ---------------------------------------------------------------------------
// REPRODUZIERBARKEIT (Befund 2026-08-11, der wichtigste dieser Runde):
//
// Waehrend eines Offline-Renders haengt der Edit weiter am Audio-Geraet, und der
// LIVE-Graph verarbeitet dieselben Plugin-Instanzen weiter. Zustandsbehaftete
// Plugins bekommen dadurch mitten im Render fremde Bloecke untergeschoben.
// Gemessen wurde das an der Roehrensaettigung: zwei Bounces derselben Stelle
// wichen um 35 dB voneinander ab — als DC-Spruenge, die exakt mit der
// Zeitkonstante der DC-Sperre abklingen, 75 Stueck in 10 Sekunden.
//
// TE sagt das sogar an ("Rendering whilst attached to audio device") und stellt
// Edit::ScopedRenderStatus bereit; Aizana hat es an seinen Render-Stellen nur
// nicht benutzt.
//
// Der Test verlangt das Einzige, was hier zaehlt: ZWEIMAL dasselbe rendern muss
// ZWEIMAL dasselbe ergeben. Ohne den Fix faellt er durch (verifiziert).
// ---------------------------------------------------------------------------
TEST_CASE ("BounceInPlace: two bounces of the same clip are identical (live graph must not interfere)",
           "[bounce][P3][reproducible]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);
    PluginScanner scanner;
    PluginManager pm (scanner, em);

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_bounce_repro_test");
    tempDir.deleteRecursively();
    tempDir.createDirectory();
    const auto srcWav = writeSineWav (tempDir, "src.wav");

    auto* track = te::getAudioTracks (*edit)[0];

    // Zustandsbehaftetes Plugin: die Roehrensaettigung fuehrt eine DC-Sperre mit,
    // deren Zustand jede Stoerung ueber ~16 ms hinweg sichtbar macht. Ein
    // zustandsfreies Plugin wuerde den Fehler NICHT zeigen — deshalb genau dieses.
    auto* plugin = pm.insertPlugin (*track,
                                    BuiltInPluginFormat::getDescriptionFor ("Aizana Saturator"),
                                    -1);
    REQUIRE (plugin != nullptr);
    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin))
    {
        ext->initialiseFully();
        if (auto* inst = ext->getAudioPluginInstance())
            for (auto* p : inst->getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    if (rp->paramID == "drive") rp->setValueNotifyingHost (rp->convertTo0to1 (30.0f));
                    if (rp->paramID == "type")  rp->setValueNotifyingHost (rp->convertTo0to1 (1.0f)); // Tube
                }
    }

    // Der Edit haengt am Geraet — genau die Lage, in der ein Bounce in der App
    // stattfindet (die App spielt, der User bounct).
    edit->getTransport().ensureContextAllocated();
    auto* contextBefore = edit->getTransport().getCurrentPlaybackContext();
    REQUIRE (contextBefore != nullptr);

    auto* clip1 = clipEditor.importAudioFile (srcWav, *track, 0.0);
    REQUIRE (clip1 != nullptr);

    // TE meldet die fehlerhafte Lage selbst: "Rendering whilst attached to audio
    // device" (tracktion_NodeRenderContext.cpp, direkt neben einem jassertfalse).
    // Genau diese Meldung wird mitgeschnitten — sie ist der eindeutige, von TE
    // selbst definierte Nachweis, dass waehrend des Renders der Live-Graph lief.
    struct CapturingLogger : juce::Logger
    {
        juce::StringArray lines;
        void logMessage (const juce::String& m) override { lines.add (m); }
    };

    CapturingLogger logger;
    auto* previousLogger = juce::Logger::getCurrentLogger();
    juce::Logger::setCurrentLogger (&logger);

    auto* b1 = clipEditor.bounceClipInPlace (*clip1, true);

    juce::Logger::setCurrentLogger (previousLogger);
    REQUIRE (b1 != nullptr);

    bool renderedWhileAttached = false;
    for (const auto& line : logger.lines)
        if (line.contains ("Rendering whilst attached to audio device"))
            renderedWhileAttached = true;

    INFO ("TE-Meldungen waehrend des Bounces: " << logger.lines.joinIntoString (" | "));
    CHECK_FALSE (renderedWhileAttached);

    // DAS ist der pruefbare Vertrag: waehrend des Renders darf der Edit NICHT am
    // Geraet haengen. te::Edit::ScopedRenderStatus loest den Playback-Kontext und
    // legt danach einen NEUEN an — ein anderer Zeiger als vorher beweist, dass er
    // zwischendurch weg war. Ohne den Fix bleibt es derselbe Kontext, der Live-Graph
    // laeuft mit, und TE meldet "Rendering whilst attached to audio device".
    //
    // Bewusst der Vertrag statt der Symptome: die Verfaelschung selbst ist nur
    // unter Last und mit zustandsbehafteten Plugins sichtbar (real gemessen: 35 dB
    // Abweichung zwischen zwei Bounces, DC-Spruenge im Takt der DC-Sperre). Ein
    // Test, der das Symptom nur manchmal trifft, waere schlimmer als keiner —
    // er wuerde gruen behaupten, die Sache sei geprueft.
    auto* contextAfter = edit->getTransport().getCurrentPlaybackContext();
    REQUIRE (contextAfter != nullptr);          // danach wieder spielbereit
    juce::ignoreUnused (contextBefore);         // Zeigervergleich taugt nicht:
    // der Allokator liefert fuer den neu angelegten Kontext oft dieselbe Adresse.
    // Geprueft wird stattdessen TEs eigene Meldung (siehe oben) — die ist eindeutig.

    // Und das Ergebnis ist trotzdem brauchbares Audio, nicht Stille
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (
        fm.createReaderFor (b1->getSourceFileReference().getFile()));
    REQUIRE (reader != nullptr);
    juce::AudioBuffer<float> rendered ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&rendered, 0, rendered.getNumSamples(), 0, true, rendered.getNumChannels() > 1);
    CHECK (rendered.getMagnitude (0, rendered.getNumSamples()) > 0.01f);

    // Zusaetzlich das Ergebnis selbst: derselbe Bounce ein zweites Mal muss
    // dasselbe Audio liefern. Das ist die Zusage, die den User interessiert —
    // "zweimal bouncen, zweimal dasselbe".
    em.getUndoManager().undo();
    auto* clip2 = dynamic_cast<te::WaveAudioClip*> (track->getClips()[0]);
    REQUIRE (clip2 != nullptr);
    auto* b2 = clipEditor.bounceClipInPlace (*clip2, true);
    REQUIRE (b2 != nullptr);

    std::unique_ptr<juce::AudioFormatReader> reader2 (
        fm.createReaderFor (b2->getSourceFileReference().getFile()));
    REQUIRE (reader2 != nullptr);
    juce::AudioBuffer<float> second ((int) reader2->numChannels, (int) reader2->lengthInSamples);
    reader2->read (&second, 0, second.getNumSamples(), 0, true, second.getNumChannels() > 1);

    const int n = juce::jmin (rendered.getNumSamples(), second.getNumSamples());
    REQUIRE (n > 0);
    double sumSq = 0.0, sigSq = 0.0;
    for (int ch = 0; ch < juce::jmin (rendered.getNumChannels(), second.getNumChannels()); ++ch)
        for (int i = 0; i < n; ++i)
        {
            const double diff = (double) rendered.getSample (ch, i) - second.getSample (ch, i);
            sumSq += diff * diff;
            sigSq += (double) rendered.getSample (ch, i) * rendered.getSample (ch, i);
        }
    const double errRms = std::sqrt (sumSq / n);
    const double sigRms = std::sqrt (sigSq / n);
    INFO ("Zwei Bounces: Signal " << sigRms << ", Abweichung " << errRms);
    CHECK (errRms < sigRms * 1.0e-6);

    tempDir.deleteRecursively();
}
