// render_artifacts_session.cpp — die Testreihen, die eine ECHTE SESSION brauchen.
//
// Gegenstueck zu render_artifacts_real.cpp: dort laufen reine Analyse-Bausteine
// (Tempo, Pitch) auf echtem Material. Hier wird eine echte te::Edit aufgebaut,
// durch den Renderer geschickt und das ERGEBNIS vermessen — genau die Punkte der
// Live-Checkliste, die bisher nur "am Modell" bewiesen waren:
//
//   P1  Sidechain      — duckt der 809-Bass wirklich auf die Kick?
//   P3  Bounce         — ist der Bounce identisch mit dem Live-Render?
//   P5  Clip-Gain      — folgt der gerenderte Pegel der gezeichneten Kurve?
//   P7b Audio-Bend     — wandert die Transiente an die gezogene Stelle?
//   ET  Merge (Cmd+J)  — ist Merged minus Original Stille?
//
// Jeder Fall misst maschinell UND legt Hoerdateien ab, damit der Rest am Ohr
// entschieden werden kann. Alle Cases tragen das versteckte Tag "[.session]" →
// sie laufen NICHT im normalen Suite-Lauf. Aufruf:
//
//     AIZANA_MATERIAL_DIR="$HOME/repository/Audio Test Material" \
//     AIZANA_ARTIFACT_DIR="$HOME/Desktop/Aizana Testlaeufe/<Datum>" \
//       engine_tests "[session]"
//
// Ohne AIZANA_MATERIAL_DIR ueberspringen sich alle Faelle selbst (SKIP).
//
// MESSPRINZIP (die Lehre vom 2026-08-11): jede Aussage wird gegen eine
// REFERENZ gemessen, die sich nur in der einen fraglichen Eigenschaft
// unterscheidet — zweiter Render ohne Sidechain, zweiter Render ohne Kurve,
// das Original gegen den Merge. Ein Absolutwert auf echtem Material sagt
// nichts, weil das Material selbst schwankt.
#include <catch2/catch_test_macros.hpp>

#include "engine/EditManager.h"
#include "engine/ClipEditor.h"
#include "engine/MixerEngine.h"
#include "engine/ClipGainEnvelope.h"
#include "engine/WarpEngine.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginScanner.h"
#include "plugins/BuiltInPluginFormat.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    using TimePos = tracktion::core::TimePosition;
    using TimeRng = tracktion::core::TimeRange;

    //--------------------------------------------------------------------------
    // Material + Ablage
    //--------------------------------------------------------------------------
    juce::File materialDir()
    {
        auto p = juce::SystemStats::getEnvironmentVariable ("AIZANA_MATERIAL_DIR", {});
        return p.isNotEmpty() ? juce::File (p) : juce::File();
    }

    juce::File outDir (const juce::String& sub)
    {
        auto base = juce::SystemStats::getEnvironmentVariable ("AIZANA_ARTIFACT_DIR", {});
        auto d = base.isNotEmpty()
                   ? juce::File (base)
                   : juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                         .getChildFile ("Aizana Testlaeufe").getChildFile ("session");
        d = d.getChildFile (sub);
        d.createDirectory();
        return d;
    }

    juce::File findStem (const juce::String& fragment)
    {
        auto dir = materialDir();
        if (! dir.isDirectory())
            return {};

        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.wav"))
            if (f.getFileNameWithoutExtension().containsIgnoreCase (fragment))
                return f;
        return {};
    }

    /** Arbeitsverzeichnis pro Fall — eindeutig pro Lauf.
        Der Warp-Render leitet seinen Zieldateinamen aus dem Quell-PFAD ab; ein
        fester Pfad wuerde eine alte (womoeglich stille) Render-Datei eines
        abgebrochenen Laufs wiederverwenden. Siehe test_warp_bend.cpp. */
    juce::File workDir()
    {
        auto d = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("aizana_session_test")
                     .getChildFile (juce::Uuid().toString());
        d.createDirectory();
        return d;
    }

    void note (const juce::File& dir, const juce::String& line)
    {
        dir.getChildFile ("befund.txt").appendText (line + "\n");
        WARN (line.toStdString());
    }

    //--------------------------------------------------------------------------
    // Audio-I/O
    //--------------------------------------------------------------------------
    bool readAudio (const juce::File& file, juce::AudioBuffer<float>& dest, double& sampleRate)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return false;

        sampleRate = reader->sampleRate;
        dest.setSize ((int) juce::jmin ((juce::uint32) 2, reader->numChannels),
                      (int) reader->lengthInSamples);
        reader->read (&dest, 0, dest.getNumSamples(), 0, true, dest.getNumChannels() > 1);
        return true;
    }

    void writeWav (const juce::File& out, const juce::AudioBuffer<float>& buf, double sr)
    {
        out.deleteFile();
        juce::WavAudioFormat fmt;
        if (auto os = out.createOutputStream())
            if (std::unique_ptr<juce::AudioFormatWriter> w (
                    fmt.createWriterFor (os.release(), sr, (unsigned) buf.getNumChannels(), 24, {}, 0));
                w != nullptr)
                w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }

    /** Schneidet einen Ausschnitt aus einem Stem in eine eigene Datei.
        Die Stems sind je 164 s lang — ein Render darauf dauert unnoetig lange,
        und die Messfenster waeren unuebersichtlich. */
    juce::File makeExcerptFile (const juce::File& stem, double startSecs, double lenSecs,
                                const juce::File& dir, const juce::String& name)
    {
        juce::AudioBuffer<float> full;
        double sr = 0.0;
        if (! readAudio (stem, full, sr))
            return {};

        const int start = juce::jlimit (0, juce::jmax (0, full.getNumSamples() - 1),
                                        (int) (startSecs * sr));
        const int len   = juce::jmin ((int) (lenSecs * sr), full.getNumSamples() - start);
        if (len <= 0)
            return {};

        juce::AudioBuffer<float> part (full.getNumChannels(), len);
        for (int ch = 0; ch < full.getNumChannels(); ++ch)
            part.copyFrom (ch, 0, full, ch, start, len);

        auto out = dir.getChildFile (name);
        writeWav (out, part, sr);
        return out;
    }

    /** Erster Stem aus der Wunschliste, der laut genug fuer eine Messung ist.
        Die Stems eines echten Songs sind sehr unterschiedlich ausgesteuert —
        die Gitarre dieses Songs liegt bei max -37,6 dB. Auf so etwas zu messen
        geht rechnerisch, ergibt aber unbrauchbare Hoerproben. */
    juce::File pickAudibleStem (const juce::StringArray& fragments, float minPeak = 0.05f)
    {
        for (const auto& frag : fragments)
        {
            auto f = findStem (frag);
            if (! f.existsAsFile())
                continue;

            juce::AudioBuffer<float> b;
            double sr = 0.0;
            if (readAudio (f, b, sr) && b.getMagnitude (0, b.getNumSamples()) >= minPeak)
                return f;
        }
        return {};
    }

    /** Grobe Energie eines Fensters (jedes 16. Sample reicht zum Vergleichen). */
    double windowEnergy (const juce::AudioBuffer<float>& b, int pos, int win)
    {
        double sum = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const float* d = b.getReadPointer (ch) + pos;
            for (int i = 0; i < win; i += 16)
                sum += (double) d[i] * d[i];
        }
        return sum;
    }

    /** Startsekunde des lautesten zusammenhaengenden Fensters. */
    double loudestOffsetSecs (const juce::AudioBuffer<float>& src, double sr, double lengthSecs)
    {
        const int win  = juce::jmax (1, (int) (lengthSecs * sr));
        const int step = juce::jmax (1, (int) (0.5 * sr));
        if (src.getNumSamples() <= win)
            return 0.0;

        double best = -1.0;
        int    bestPos = 0;
        for (int pos = 0; pos + win <= src.getNumSamples(); pos += step)
            if (const double e = windowEnergy (src, pos, win); e > best)
            {
                best = e;
                bestPos = pos;
            }
        return (double) bestPos / sr;
    }

    /** Fenster, in dem BEIDE Spuren gut zu hoeren sind.
        Fuer den Sidechain-Fall reicht "laut" auf einer Spur nicht: eine Kick
        ohne Bass (oder umgekehrt) macht die Messung wertlos. Gesucht ist
        deshalb das Maximum des SCHWAECHEREN der beiden Pegel. */
    double loudestCommonOffsetSecs (const juce::AudioBuffer<float>& a,
                                    const juce::AudioBuffer<float>& b,
                                    double sr, double lengthSecs)
    {
        const int win  = juce::jmax (1, (int) (lengthSecs * sr));
        const int step = juce::jmax (1, (int) (0.5 * sr));
        const int n    = juce::jmin (a.getNumSamples(), b.getNumSamples());
        if (n <= win)
            return 0.0;

        double best = -1.0;
        int    bestPos = 0;
        for (int pos = 0; pos + win <= n; pos += step)
        {
            const double m = std::min (windowEnergy (a, pos, win) / a.getNumChannels(),
                                       windowEnergy (b, pos, win) / b.getNumChannels());
            if (m > best) { best = m; bestPos = pos; }
        }
        return (double) bestPos / sr;
    }

    /** Lauteste Stelle eines Stems als eigene Datei.
        FESTE Zeitmarken sind untauglich: der erste Anlauf schnitt bei 32 s aus
        der Gitarrenspur — dort ist im Song eine Pause, der Ausschnitt war
        vollstaendig still, und der nachfolgende Render sah aus wie ein
        Render-Fehler. Ein stiller Ausschnitt ist ein Fehler im Messaufbau, kein
        Messergebnis (Lehre vom 2026-08-11, hier prompt wiederholt). */
    juce::File makeLoudExcerptFile (const juce::File& stem, double lenSecs,
                                    const juce::File& dir, const juce::String& name,
                                    double& offsetSecsOut)
    {
        juce::AudioBuffer<float> full;
        double sr = 0.0;
        if (! readAudio (stem, full, sr))
            return {};

        offsetSecsOut = loudestOffsetSecs (full, sr, lenSecs);
        return makeExcerptFile (stem, offsetSecsOut, lenSecs, dir, name);
    }

    //--------------------------------------------------------------------------
    // Messung
    //--------------------------------------------------------------------------
    double rmsRange (const juce::AudioBuffer<float>& b, double sr, double fromSecs, double toSecs)
    {
        const int from = juce::jlimit (0, b.getNumSamples(), (int) (fromSecs * sr));
        const int to   = juce::jlimit (0, b.getNumSamples(), (int) (toSecs   * sr));
        if (to - from <= 0)
            return 0.0;

        double sum = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const float* d = b.getReadPointer (ch);
            for (int i = from; i < to; ++i)
                sum += (double) d[i] * d[i];
        }
        return std::sqrt (sum / ((to - from) * b.getNumChannels()));
    }

    /** Anschlagspositionen einer perkussiven Spur (Sekunden).
        Bewusst simpel: kurzzeitige Energie, Schwelle relativ zum Maximum,
        Mindestabstand. Das reicht, um Kick-Schlaege zu finden — die feine
        Beat-Analyse steckt in TempoDetector und wird hier nicht gebraucht. */
    std::vector<double> onsetTimes (const juce::AudioBuffer<float>& b, double sr,
                                    double minGapSecs = 0.20, double relThreshold = 0.35)
    {
        const int hop = juce::jmax (1, (int) (0.005 * sr));
        std::vector<double> env;
        env.reserve ((size_t) (b.getNumSamples() / hop + 1));

        for (int pos = 0; pos + hop <= b.getNumSamples(); pos += hop)
        {
            double peak = 0.0;
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
            {
                const float* d = b.getReadPointer (ch) + pos;
                for (int i = 0; i < hop; ++i)
                    peak = std::max (peak, (double) std::abs (d[i]));
            }
            env.push_back (peak);
        }
        if (env.empty())
            return {};

        const double maxEnv = *std::max_element (env.begin(), env.end());
        if (maxEnv <= 0.0)
            return {};

        const double thresh  = maxEnv * relThreshold;
        const int    minGap  = juce::jmax (1, (int) (minGapSecs * sr / hop));

        std::vector<double> out;
        int lastHit = -minGap;
        for (int i = 1; i < (int) env.size(); ++i)
        {
            if (env[(size_t) i] > thresh && env[(size_t) i - 1] <= thresh && i - lastHit >= minGap)
            {
                out.push_back ((double) i * hop / sr);
                lastHit = i;
            }
        }
        return out;
    }

    double median (std::vector<double> v)
    {
        if (v.empty()) return 0.0;
        std::sort (v.begin(), v.end());
        return v[v.size() / 2];
    }

    juce::String db (double linear)
    {
        return linear > 1.0e-12 ? juce::String (20.0 * std::log10 (linear), 1) + " dB"
                                : juce::String ("-inf dB");
    }

    //--------------------------------------------------------------------------
    // Render
    //--------------------------------------------------------------------------
    /** Rendert einen Zeitbereich der Edit in eine Datei.
        Parameter spiegeln bewusst den Bounce-Pfad der App (ClipEditor::
        bounceClipInPlace), damit ein Vergleich beider Wege aussagekraeftig ist. */
    juce::File renderRange (te::Edit& edit, const juce::File& outFile,
                            double fromSecs, double toSecs,
                            const juce::BigInteger& tracksToDo,
                            bool useMasterPlugins = true,
                            te::Clip* onlyThisClip = nullptr)
    {
        outFile.deleteFile();

        double sampleRate = edit.engine.getDeviceManager().getSampleRate();
        if (sampleRate < 8000.0)
            sampleRate = 44100.0;                 // headless: kein Geraet offen

        te::Renderer::Parameters params (edit);
        params.destFile           = outFile;
        params.audioFormat        = edit.engine.getAudioFileFormatManager().getWavFormat();
        params.bitDepth           = 24;
        params.sampleRateForAudio = sampleRate;
        params.blockSizeForAudio  = 512;
        params.time               = TimeRng (TimePos::fromSeconds (fromSecs),
                                             TimePos::fromSeconds (toSecs));
        params.realTimeRender     = false;
        params.usePlugins         = true;
        params.useMasterPlugins   = useMasterPlugins;
        params.checkNodesForAudio = false;
        params.tracksToDo         = tracksToDo;
        if (onlyThisClip != nullptr)
            params.allowedClips.add (onlyThisClip);

        const auto rendered = te::Renderer::renderToFile ("Session-Testlauf", params);
        return rendered.existsAsFile() ? rendered : outFile;
    }

    juce::BigInteger trackBits (te::Edit& edit, const juce::Array<te::Track*>& tracks)
    {
        juce::BigInteger bits;
        auto all = te::getAllTracks (edit);
        for (auto* t : tracks)
            if (const int i = all.indexOf (t); i >= 0)
                bits.setBit (i);
        return bits;
    }

    /** Parameter eines Built-in-Plugins ueber die Host-Schnittstelle setzen
        (haelt APVTS + generischen Editor synchron — wie MixApplyEngine). */
    void setHostParam (juce::AudioPluginInstance& inst, const juce::String& id, float realValue)
    {
        for (auto* p : inst.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (rp->paramID == id)
                {
                    rp->setValueNotifyingHost (rp->convertTo0to1 (realValue));
                    return;
                }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// P1 — Sidechain: duckt der Bass wirklich auf die Kick?
//
// Der Nachweis am Modell (Wires vorhanden, Key-Flag gesetzt) existiert bereits
// in test_sidechain_routing.cpp. Offen war die Frage, ob im GERENDERTEN Audio
// eine Pegelabsenkung im Takt der Kick steht.
//
// Messaufbau: zweimal dieselbe Session rendern — einmal mit Kick als Key,
// einmal ohne. Die DIFFERENZ beider Renderings enthaelt exakt das, was der
// Sidechain am Bass geaendert hat (die Kick selbst kuerzt sich weg, weil sie in
// beiden Faellen identisch drin ist). Liegt die Energie dieser Differenz auf den
// Kick-Schlaegen, ist die Kick der Ausloeser — und nicht der Bass selbst.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Session P1: Sidechain duckt den 809-Bass auf die Kick", "[.session][p1]")
{
    if (! materialDir().isDirectory())
        SKIP ("Kein Material — AIZANA_MATERIAL_DIR setzen");

    const auto kickStem = findStem ("Kick");
    const auto bassStem = findStem ("809");
    if (! kickStem.existsAsFile() || ! bassStem.existsAsFile())
        SKIP ("Kick- oder 809-Stem nicht gefunden");

    auto dir  = outDir ("20_sidechain");
    auto work = workDir();

    // Gemeinsames Zeitfenster fuer BEIDE Spuren: sie muessen zueinander passen
    // UND beide muessen dort spielen. Die Stelle wird gesucht, nicht geraten.
    constexpr double kLen = 12.0;
    juce::AudioBuffer<float> kickFull, bassFull;
    double srK = 0.0, srB = 0.0;
    REQUIRE (readAudio (kickStem, kickFull, srK));
    REQUIRE (readAudio (bassStem, bassFull, srB));
    REQUIRE (srK == srB);

    const double kFrom = loudestCommonOffsetSecs (kickFull, bassFull, srK, kLen);
    const auto kickFile = makeExcerptFile (kickStem, kFrom, kLen, work, "kick.wav");
    const auto bassFile = makeExcerptFile (bassStem, kFrom, kLen, work, "bass.wav");
    REQUIRE (kickFile.existsAsFile());
    REQUIRE (bassFile.existsAsFile());

    // Riegel gegen die Stille-Falle: ein leerer Ausschnitt ist ein Fehler im
    // Messaufbau, kein Messergebnis (Lehre vom 2026-08-11).
    juce::AudioBuffer<float> kickBuf, bassBuf;
    double sr = 0.0, sr2 = 0.0;
    REQUIRE (readAudio (kickFile, kickBuf, sr));
    REQUIRE (readAudio (bassFile, bassBuf, sr2));
    REQUIRE (kickBuf.getMagnitude (0, kickBuf.getNumSamples()) > 0.01f);
    REQUIRE (bassBuf.getMagnitude (0, bassBuf.getNumSamples()) > 0.01f);

    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);
    MixerEngine mixer (em);
    PluginScanner scanner;
    PluginManager pm (scanner, em);

    auto* bassTrack = te::getAudioTracks (*edit)[0];
    auto* kickTrack = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit),
                                                 nullptr).get();
    REQUIRE (kickTrack != nullptr);
    kickTrack->setSoloIsolate (false);

    REQUIRE (clipEditor.importAudioFile (bassFile, *bassTrack, 0.0) != nullptr);
    REQUIRE (clipEditor.importAudioFile (kickFile, *kickTrack, 0.0) != nullptr);

    auto* plugin = pm.insertPlugin (*bassTrack,
                                    BuiltInPluginFormat::getDescriptionFor ("Aizana Compressor"),
                                    -1);
    REQUIRE (plugin != nullptr);
    auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin);
    REQUIRE (ext != nullptr);
    ext->initialiseFully();
    auto* inst = ext->getAudioPluginInstance();
    REQUIRE (inst != nullptr);

    // Deutlich hoerbare Einstellung — es geht um den Nachweis, nicht um Geschmack.
    setHostParam (*inst, "threshold", -32.0f);
    setHostParam (*inst, "ratio",      10.0f);
    setHostParam (*inst, "attack",      1.0f);
    setHostParam (*inst, "release",   140.0f);
    setHostParam (*inst, "knee",        2.0f);
    setHostParam (*inst, "makeup",      0.0f);

    const auto bits = trackBits (*edit, { bassTrack, kickTrack });

    // Durchgang 1: MIT Sidechain
    edit->getUndoManager().beginNewTransaction ("Sidechain setzen");
    mixer.setSidechainSource (*ext, *kickTrack);
    REQUIRE (ext->getSidechainSourceID() == kickTrack->itemID);
    REQUIRE (ext->getNumWires() > 0);          // ohne Wires laeuft KEIN Audio (I-Fund)

    const auto fileSC = renderRange (*edit, dir.getChildFile ("a_mit_sidechain.wav"),
                                     0.0, kLen, bits);
    REQUIRE (fileSC.existsAsFile());

    // Durchgang 2: OHNE Sidechain (der Kompressor bleibt drin, nur der Key faellt weg)
    edit->getUndoManager().beginNewTransaction ("Sidechain loesen");
    mixer.clearSidechainSource (*ext);
    const auto fileNo = renderRange (*edit, dir.getChildFile ("b_ohne_sidechain.wav"),
                                     0.0, kLen, bits);
    REQUIRE (fileNo.existsAsFile());

    juce::AudioBuffer<float> withSc, without;
    double srA = 0.0, srWithout = 0.0;
    REQUIRE (readAudio (fileSC, withSc, srA));
    REQUIRE (readAudio (fileNo, without, srWithout));
    REQUIRE (srA == srWithout);

    const int n = juce::jmin (withSc.getNumSamples(), without.getNumSamples());
    REQUIRE (n > (int) (srA * 5.0));
    REQUIRE (withSc.getMagnitude (0, n) > 0.001f);   // ueberhaupt Audio gerendert

    juce::AudioBuffer<float> diff (juce::jmin (withSc.getNumChannels(), without.getNumChannels()), n);
    for (int ch = 0; ch < diff.getNumChannels(); ++ch)
    {
        diff.copyFrom (ch, 0, withSc, ch, 0, n);
        diff.addFrom  (ch, 0, without, ch, 0, n, -1.0f);
    }
    writeWav (dir.getChildFile ("c_differenz_nur_das_ducking.wav"), diff, srA);

    const double diffRms = rmsRange (diff, srA, 0.0, n / srA);
    note (dir, "Material: " + kickStem.getFileName() + " + " + bassStem.getFileName()
               + "  (lauteste gemeinsame " + juce::String (kLen, 0) + " s ab "
               + juce::String (kFrom, 1) + " s)");
    note (dir, "Differenz mit/ohne Sidechain: RMS " + db (diffRms));

    // Der Sidechain muss ueberhaupt etwas veraendern.
    CHECK (diffRms > 1.0e-4);

    // Und die Veraenderung muss auf den Kick-Schlaegen sitzen.
    const auto hits = onsetTimes (kickBuf, sr);
    note (dir, juce::String (hits.size()) + " Kick-Schlaege im Ausschnitt erkannt");
    REQUIRE (hits.size() >= 4);

    std::vector<double> after, before;
    for (double t : hits)
    {
        if (t < 0.25 || t > n / srA - 0.25) continue;
        after .push_back (rmsRange (diff, srA, t,        t + 0.10));
        before.push_back (rmsRange (diff, srA, t - 0.20, t - 0.08));
    }
    REQUIRE (after.size() >= 4);

    const double medAfter  = median (after);
    const double medBefore = median (before);
    note (dir, "Ducking-Energie direkt nach dem Schlag: " + db (medAfter)
               + " | im Zwischenraum: " + db (medBefore)
               + (medBefore > 0.0
                    ? " | Verhaeltnis " + juce::String (medAfter / medBefore, 1) + "x"
                    : juce::String (" | im Zwischenraum passiert NICHTS (exakt 0)")));

    // Kernaussage: die Absenkung folgt der KICK, nicht dem Bass. Beide Haelften
    // der Aussage muessen stimmen — es muss ueberhaupt geduckt werden UND es
    // darf zwischen den Schlaegen nicht passieren.
    CHECK (medAfter > 1.0e-5);
    CHECK (medAfter > medBefore * 2.0);

    work.deleteRecursively();
}

// ─────────────────────────────────────────────────────────────────────────────
// P3 — Bounce in Place gegen Live-Render.
//
// Der Bounce ersetzt einen Clip durch seine gerenderte Fassung. Wenn dabei auch
// nur die Insert-Kette anders behandelt wird als beim normalen Abspielen, faellt
// das im Alltag erst auf, wenn der Mix schon steht. Deshalb: dieselbe Strecke
// einmal ueber den Renderer, einmal ueber den Bounce-Weg — und die Differenz
// muss im Bereich der Rundung liegen.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Session P3: Bounce in Place ist deckungsgleich mit dem Live-Render", "[.session][p3]")
{
    if (! materialDir().isDirectory())
        SKIP ("Kein Material — AIZANA_MATERIAL_DIR setzen");

    const auto stem = pickAudibleStem ({ "Chorus Main 1", "chords", "Plucks", "Guitar" });
    if (! stem.existsAsFile())
        SKIP ("Kein ausreichend lauter Stem gefunden");

    auto dir  = outDir ("21_bounce");
    auto work = workDir();

    double srcFrom = 0.0;
    const auto src = makeLoudExcerptFile (stem, 10.0, work, "guitar.wav", srcFrom);
    REQUIRE (src.existsAsFile());
    {   // nie gegen Stille vergleichen — sonst ist jede Differenz "perfekt"
        juce::AudioBuffer<float> probe; double psr = 0.0;
        REQUIRE (readAudio (src, probe, psr));
        REQUIRE (probe.getMagnitude (0, probe.getNumSamples()) > 0.01f);
    }

    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);
    PluginScanner scanner;
    PluginManager pm (scanner, em);

    auto* track = te::getAudioTracks (*edit)[0];
    auto* clip = clipEditor.importAudioFile (src, *track, 0.0);
    REQUIRE (clip != nullptr);

    // Insert-FX auf der Spur: der Bounce MUSS sie einbacken (withTrackFX = true).
    auto* plugin = pm.insertPlugin (*track,
                                    BuiltInPluginFormat::getDescriptionFor ("Aizana Saturator"),
                                    -1);
    REQUIRE (plugin != nullptr);
    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin))
    {
        ext->initialiseFully();
        if (auto* inst = ext->getAudioPluginInstance())
            setHostParam (*inst, "drive", 6.0f);   // hoerbar, aber nicht am Anschlag
    }

    const auto range = clip->getPosition().time;

    // Referenz ueber den normalen Renderer — gleiche Parameter wie der Bounce-Weg
    // (nur diese Spur, nur dieser Clip, Master bleibt aussen vor).
    const auto refFile = renderRange (*edit, dir.getChildFile ("a_live_render.wav"),
                                      range.getStart().inSeconds(), range.getEnd().inSeconds(),
                                      trackBits (*edit, { track }), false, clip);
    REQUIRE (refFile.existsAsFile());

    auto* bounced = clipEditor.bounceClipInPlace (*clip, true);
    REQUIRE (bounced != nullptr);
    const auto bounceFile = bounced->getSourceFileReference().getFile();
    REQUIRE (bounceFile.existsAsFile());

    juce::AudioBuffer<float> a, b;
    double srA = 0.0, srB = 0.0;
    REQUIRE (readAudio (refFile, a, srA));
    REQUIRE (readAudio (bounceFile, b, srB));
    REQUIRE (srA == srB);

    const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
    REQUIRE (n > (int) (srA * 5.0));
    REQUIRE (a.getMagnitude (0, n) > 0.01f);          // nicht gegen Stille vergleichen

    juce::AudioBuffer<float> d (juce::jmin (a.getNumChannels(), b.getNumChannels()), n);
    for (int ch = 0; ch < d.getNumChannels(); ++ch)
    {
        d.copyFrom (ch, 0, a, ch, 0, n);
        d.addFrom  (ch, 0, b, ch, 0, n, -1.0f);
    }

    const double sig  = rmsRange (a, srA, 0.0, n / srA);
    const double err  = rmsRange (d, srA, 0.0, n / srA);
    const double snr  = err > 0.0 ? 20.0 * std::log10 (sig / err) : 999.0;
    const double peak = d.getMagnitude (0, n);

    juce::AudioBuffer<float> copy (b);
    writeWav (dir.getChildFile ("b_bounce.wav"), copy, srB);
    writeWav (dir.getChildFile ("c_differenz.wav"), d, srA);

    note (dir, "Laengen: Live-Render " + juce::String (a.getNumSamples())
               + " Samples, Bounce " + juce::String (b.getNumSamples()));
    note (dir, "Signal " + db (sig) + " | Differenz " + db (err)
               + " | Abstand " + juce::String (snr, 1) + " dB | groesste Einzelabweichung "
               + juce::String (peak, 6));

    // Beide Wege muessen dasselbe Ergebnis liefern; 60 dB Abstand ist deutlich
    // unter jeder Hoerschwelle und laesst Raum fuer 24-Bit-Rundung.
    CHECK (snr > 60.0);
    CHECK (std::abs (a.getNumSamples() - b.getNumSamples()) <= 1);

    work.deleteRecursively();
}

// ─────────────────────────────────────────────────────────────────────────────
// P5 — Clip-Gain-Envelope: folgt der gerenderte Pegel der gezeichneten Kurve?
//
// Auf echtem Material schwankt der Pegel von sich aus — ein Absolutwert pro
// Fenster taugt nicht. Gemessen wird deshalb das VERHAELTNIS zweier Renderings
// derselben Stelle (mit und ohne Kurve). Dieses Verhaeltnis ist der tatsaechlich
// angewandte Gain und laesst sich direkt mit dem gezeichneten Sollwert
// vergleichen.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Session P5: Clip-Gain-Kurve wirkt im Render wie gezeichnet", "[.session][p5]")
{
    if (! materialDir().isDirectory())
        SKIP ("Kein Material — AIZANA_MATERIAL_DIR setzen");

    const auto stem = pickAudibleStem ({ "Chorus Main 1", "chords", "Plucks", "Guitar" });
    if (! stem.existsAsFile())
        SKIP ("Kein ausreichend lauter Stem gefunden");

    auto dir  = outDir ("22_clipgain");
    auto work = workDir();

    constexpr double kLen = 9.0;
    double srcFrom = 0.0;
    const auto src = makeLoudExcerptFile (stem, kLen, work, "guitar.wav", srcFrom);
    REQUIRE (src.existsAsFile());
    {
        juce::AudioBuffer<float> probe; double psr = 0.0;
        REQUIRE (readAudio (src, probe, psr));
        REQUIRE (probe.getMagnitude (0, probe.getNumSamples()) > 0.01f);
    }

    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);

    auto* track = te::getAudioTracks (*edit)[0];
    auto* clip = clipEditor.importAudioFile (src, *track, 0.0);
    REQUIRE (clip != nullptr);

    const auto bits = trackBits (*edit, { track });

    // Referenz ohne Kurve
    const auto refFile = renderRange (*edit, dir.getChildFile ("a_ohne_kurve.wav"),
                                      0.0, kLen, bits, false, clip);
    REQUIRE (refFile.existsAsFile());

    // Kurve: 0 dB — Plateau -18 dB — zurueck auf 0 dB, mit kurzen Rampen.
    edit->getUndoManager().beginNewTransaction ("Gain-Kurve");
    ClipGainEnvelope::addPoint (*clip, 0.0, 0.0f);
    ClipGainEnvelope::addPoint (*clip, 2.5, 0.0f);
    ClipGainEnvelope::addPoint (*clip, 3.0, -18.0f);
    ClipGainEnvelope::addPoint (*clip, 6.0, -18.0f);
    ClipGainEnvelope::addPoint (*clip, 6.5, 0.0f);
    ClipGainEnvelope::addPoint (*clip, kLen, 0.0f);
    REQUIRE (ClipGainEnvelope::numPoints (*clip) == 6);

    const auto envFile = renderRange (*edit, dir.getChildFile ("b_mit_kurve.wav"),
                                      0.0, kLen, bits, false, clip);
    REQUIRE (envFile.existsAsFile());

    juce::AudioBuffer<float> plain, curved;
    double srA = 0.0, srB = 0.0;
    REQUIRE (readAudio (refFile, plain, srA));
    REQUIRE (readAudio (envFile, curved, srB));
    REQUIRE (srA == srB);
    REQUIRE (plain.getMagnitude (0, plain.getNumSamples()) > 0.01f);

    struct Window { double from, to, sollDb; const char* name; };
    const Window windows[] = {
        { 0.5, 2.3, 0.0,   "vor der Absenkung " },
        { 3.4, 5.8, -18.0, "im Plateau        " },
        { 7.0, 8.5, 0.0,   "nach der Rueckkehr" },
    };

    note (dir, "Quelle: " + stem.getFileName() + " (lauteste " + juce::String (kLen, 0)
               + " s ab " + juce::String (srcFrom, 1) + " s)");
    for (const auto& w : windows)
    {
        const double a = rmsRange (plain,  srA, w.from, w.to);
        const double b = rmsRange (curved, srB, w.from, w.to);
        REQUIRE (a > 1.0e-5);                       // Fenster darf nicht still sein
        const double gotDb = 20.0 * std::log10 (b / a);

        note (dir, juce::String (w.name) + " Soll " + juce::String (w.sollDb, 1)
                   + " dB | gemessen " + juce::String (gotDb, 1) + " dB");

        // 1,5 dB Toleranz: die Rampen reichen minimal in die Fenster hinein und
        // TE glaettet die Automation blockweise.
        CHECK (std::abs (gotDb - w.sollDb) < 1.5);
    }

    work.deleteRecursively();
}

// ─────────────────────────────────────────────────────────────────────────────
// P7b — Audio-Bend: wandert die Transiente wirklich dorthin, wo sie hingezogen
// wurde? Synthetisch ist das bewiesen ([warp]); auf echtem Schlagzeug ist die
// Frage, ob der Marker auch dann sitzt, wenn die Transiente nicht allein im
// Nichts steht (Nachhall, Uebersprechen, benachbarte Schlaege).
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Session P7b: Audio-Bend verschiebt eine echte Transiente", "[.session][p7b]")
{
    if (! materialDir().isDirectory())
        SKIP ("Kein Material — AIZANA_MATERIAL_DIR setzen");

    const auto stem = findStem ("Kick");
    if (! stem.existsAsFile())
        SKIP ("Kick-Stem nicht gefunden");

    auto dir  = outDir ("23_bend");
    auto work = workDir();

    double srcFrom = 0.0;
    const auto src = makeLoudExcerptFile (stem, 6.0, work, "kick.wav", srcFrom);
    REQUIRE (src.existsAsFile());

    juce::AudioBuffer<float> srcBuf;
    double sr = 0.0;
    REQUIRE (readAudio (src, srcBuf, sr));
    REQUIRE (srcBuf.getMagnitude (0, srcBuf.getNumSamples()) > 0.01f);
    const auto hitsBefore = onsetTimes (srcBuf, sr);
    REQUIRE (hitsBefore.size() >= 4);

    // Eine Transiente in der Mitte des Ausschnitts waehlen: sie hat Nachbarn auf
    // beiden Seiten, also muss TE wirklich zwischen den Markern stretchen.
    const double target = hitsBefore[2];
    const double moveTo = target - 0.080;

    EditManager em;
    em.createNewEdit();
    ClipEditor clipEditor (em);
    auto* track = te::getAudioTracks (*em.getEdit())[0];
    auto* clip = clipEditor.importAudioFile (src, *track, 0.0);
    REQUIRE (clip != nullptr);

    auto& um = em.getUndoManager();
    um.beginNewTransaction ("Audio-Bend an");
    WarpEngine::setWarpEnabled (*clip, true);
    REQUIRE (WarpEngine::isWarped (*clip));

    um.beginNewTransaction ("Bend-Marker");
    const int idx = WarpEngine::addMarkerAtSourceTime (*clip, target);
    REQUIRE (idx > 0);
    um.beginNewTransaction ("Bend ziehen");
    const double applied = WarpEngine::moveMarker (*clip, idx, moveTo);

    // Auf den Hintergrund-Render warten (Proxy-Pfad, siehe WarpEngine-Doku).
    // Die Wartebedingung ist bewusst schwaecher als die Behauptung: es wird nur
    // auf "Datei da und nicht still" gewartet, die Position prueft erst die
    // Assertion. Ein haengender Render laeuft in den Timeout und faellt durch.
    juce::File renderedFile;
    for (int i = 0; i < 800; ++i)
    {
        if (! WarpEngine::isWarpRenderPending (*clip))
        {
            const auto af = clip->getAudioFile();
            const auto f  = af.getFile();
            if (f.existsAsFile() && f != src)
            {
                juce::AudioBuffer<float> probe;
                double psr = 0.0;
                if (readAudio (f, probe, psr) && probe.getMagnitude (0, probe.getNumSamples()) > 0.01f)
                {
                    renderedFile = f;
                    break;
                }
            }
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }

    REQUIRE (renderedFile.existsAsFile());

    juce::AudioBuffer<float> outBuf;
    double osr = 0.0;
    REQUIRE (readAudio (renderedFile, outBuf, osr));
    const auto hitsAfter = onsetTimes (outBuf, osr);
    REQUIRE (hitsAfter.size() >= 3);

    // Naechstgelegene Transiente zur Zielposition suchen
    double best = 1.0e9;
    for (double t : hitsAfter)
        best = std::min (best, std::abs (t - applied));

    juce::AudioBuffer<float> srcCopy (srcBuf);
    writeWav (dir.getChildFile ("a_original.wav"), srcCopy, sr);
    writeWav (dir.getChildFile ("b_gebendet.wav"), outBuf, osr);

    note (dir, "Quelle: " + stem.getFileName() + " (lauteste 6 s ab "
               + juce::String (srcFrom, 1) + " s), "
               + juce::String (hitsBefore.size()) + " Schlaege erkannt");
    note (dir, "Transiente bei " + juce::String (target, 3) + " s auf "
               + juce::String (applied, 3) + " s gezogen");
    note (dir, "Naechste Transiente im Rendering liegt " + juce::String (best * 1000.0, 1)
               + " ms daneben");

    // 25 ms Toleranz: SoundTouch schmiert die Flanke leicht, die Onset-Erkennung
    // arbeitet in 5-ms-Schritten.
    CHECK (best < 0.025);

    // Der erste Schlag vor dem Marker darf NICHT mitgewandert sein.
    double firstShift = 1.0e9;
    for (double t : hitsAfter)
        firstShift = std::min (firstShift, std::abs (t - hitsBefore[0]));
    note (dir, "Erster Schlag (vor dem Marker) verschoben um "
               + juce::String (firstShift * 1000.0, 1) + " ms");
    CHECK (firstShift < 0.030);

    work.deleteRecursively();
}

// ─────────────────────────────────────────────────────────────────────────────
// ET — "Events verbinden" (Cmd+J): der Merge rendert die Vereinigungsstrecke
// neu. Der schaerfste denkbare Test ist die Nullsumme: teilt man einen Clip und
// fuegt die Haelften sofort wieder zusammen, muss das Ergebnis das Original
// sein. Jede Abweichung — Versatz, Pegelfehler, verschluckte Fades — zeigt sich
// als Rest, der nicht Stille ist.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Session ET: Merged minus Original ist Stille", "[.session][et]")
{
    if (! materialDir().isDirectory())
        SKIP ("Kein Material — AIZANA_MATERIAL_DIR setzen");

    const auto stem = pickAudibleStem ({ "Chorus Main 1", "chords", "Plucks", "Guitar" });
    if (! stem.existsAsFile())
        SKIP ("Kein ausreichend lauter Stem gefunden");

    auto dir  = outDir ("24_merge");
    auto work = workDir();

    constexpr double kLen = 8.0, kSplit = 3.5;
    double srcFrom = 0.0;
    const auto src = makeLoudExcerptFile (stem, kLen, work, "guitar.wav", srcFrom);
    REQUIRE (src.existsAsFile());
    {
        juce::AudioBuffer<float> probe; double psr = 0.0;
        REQUIRE (readAudio (src, probe, psr));
        REQUIRE (probe.getMagnitude (0, probe.getNumSamples()) > 0.01f);
    }

    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);

    auto* track = te::getAudioTracks (*edit)[0];
    auto* clip = clipEditor.importAudioFile (src, *track, 0.0);
    REQUIRE (clip != nullptr);

    // Referenz: dieselbe Strecke UNGETEILT durch denselben Renderer.
    // Bewusst nicht gegen die Quelldatei vergleichen — der Merge rendert mit der
    // SESSION-Rate (hier 48 kHz Geraeterate), das Material liegt in 44,1 kHz vor.
    // Ein direkter Sample-Vergleich waere damit gar nicht durchfuehrbar; gegen
    // einen Render derselben Strecke ist er es, und die Aussage wird sogar
    // schaerfer: verglichen wird "geteilt und wieder verbunden" gegen "nie
    // angefasst", bei sonst identischem Weg.
    const auto refFile = renderRange (*edit, dir.getChildFile ("a_ungeteilt.wav"),
                                      0.0, kLen, trackBits (*edit, { track }), false, clip);
    REQUIRE (refFile.existsAsFile());

    auto* right = clipEditor.cutClip (*clip, kSplit);
    REQUIRE (right != nullptr);
    REQUIRE (track->getClips().size() == 2);

    juce::Array<te::Clip*> both;
    both.add (clip);
    both.add (right);

    auto* merged = clipEditor.mergeClips (both);
    REQUIRE (merged != nullptr);
    CHECK (track->getClips().size() == 1);

    auto* mergedWave = dynamic_cast<te::WaveAudioClip*> (merged);
    REQUIRE (mergedWave != nullptr);
    const auto mergedFile = mergedWave->getSourceFileReference().getFile();
    REQUIRE (mergedFile.existsAsFile());

    juce::AudioBuffer<float> ref, out;
    double srA = 0.0, srB = 0.0;
    REQUIRE (readAudio (refFile, ref, srA));
    REQUIRE (readAudio (mergedFile, out, srB));
    REQUIRE (srA == srB);
    REQUIRE (ref.getMagnitude (0, ref.getNumSamples()) > 0.01f);

    const int n = juce::jmin (ref.getNumSamples(), out.getNumSamples());
    juce::AudioBuffer<float> d (juce::jmin (ref.getNumChannels(), out.getNumChannels()), n);
    for (int ch = 0; ch < d.getNumChannels(); ++ch)
    {
        d.copyFrom (ch, 0, ref, ch, 0, n);
        d.addFrom  (ch, 0, out, ch, 0, n, -1.0f);
    }

    const double sig = rmsRange (ref, srA, 0.0, n / srA);
    const double err = rmsRange (d,   srA, 0.0, n / srA);
    const double snr = err > 0.0 ? 20.0 * std::log10 (sig / err) : 999.0;

    // Der Rest um die Schnittstelle interessiert besonders: dort sitzen die
    // Fades, die der Split anlegt.
    const double errAtSeam = rmsRange (d, srA, kSplit - 0.05, kSplit + 0.05);

    writeWav (dir.getChildFile ("b_merged.wav"), out, srB);
    writeWav (dir.getChildFile ("c_differenz.wav"), d, srA);

    note (dir, "Quelle: " + stem.getFileName() + " (lauteste " + juce::String (kLen, 0)
               + " s ab " + juce::String (srcFrom, 1) + " s), Schnitt bei "
               + juce::String (kSplit, 1) + " s");
    note (dir, "Session-Rate " + juce::String (srA, 0) + " Hz (Material 44100 Hz)");
    note (dir, "Laengen: ungeteilt " + juce::String (ref.getNumSamples())
               + " Samples, verbunden " + juce::String (out.getNumSamples()));
    note (dir, "Signal " + db (sig) + " | Rest " + db (err) + " | Abstand "
               + juce::String (snr, 1) + " dB | Rest an der Schnittstelle " + db (errAtSeam));

    CHECK (snr > 60.0);
    CHECK (std::abs (ref.getNumSamples() - out.getNumSamples()) <= 64);

    // Eine Ruecknahme stellt beide Originale wieder her (I-02: EIN Undo-Schritt).
    em.getUndoManager().undo();
    CHECK (track->getClips().size() == 2);

    work.deleteRecursively();
}

