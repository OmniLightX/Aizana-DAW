#include "CountIn.h"

using namespace tracktion;
using namespace tracktion::literals;

//==============================================================================
countin::Plan countin::makePlan (te::TempoSequence& ts,
                                 TimePosition recordStart,
                                 int bars)
{
    Plan plan;

    if (bars <= 0)
        return plan;

    // Auf die Taktlinie ABRUNDEN, dann `bars` ganze Takte zurueck. Der Vorlauf
    // beginnt damit immer auf einer Eins und endet immer exakt am
    // Aufnahmestart; bei krummem Start ist er laenger, nie kuerzer.
    const auto bb    = ts.toBarsAndBeats (recordStart);
    const auto start = ts.toTime (tempo::BarsAndBeats { bb.bars - bars });

    if (start >= recordStart)
        return plan;

    plan.enabled          = true;
    plan.countInStartSecs = start.inSeconds();
    plan.recordStartSecs  = recordStart.inSeconds();
    return plan;
}

//==============================================================================
countin::ClickSetup countin::decideClick (bool userClickOn, bool countInClick,
                                          bool clickWhileRecording, TimeRange runUp)
{
    ClickSetup setup;

    // Der Klick des Anwenders laeuft durch — der Bereich waere hier ohnehin
    // wirkungslos (ClickGenerator wertet ihn nur bei ausgeschaltetem Schalter
    // aus), also setzen wir ihn gar nicht erst.
    if (userClickOn && clickWhileRecording)
    {
        setup.enabled = true;
        return setup;
    }

    // Ab hier gilt: ab dem Aufnahmestart ist Ruhe. Der Schalter muss dafuer AUS
    // sein, denn nur dann wirkt der Bereich. Im Vorlauf klingt es, wenn ein
    // Vorzaehler bestellt ist ODER der Anwender sein Metronom ohnehin anhat
    // (dann ist der Vorlauf ja Wiedergabe).
    setup.enabled = false;

    if (countInClick || userClickOn)
        setup.range = runUp;

    return setup;
}

//==============================================================================
namespace
{
    const juce::Identifier clickWhileRecordingID ("aizanaClickWhileRecording");
}

bool countin::getClickWhileRecording (te::Edit& edit)
{
    // Vorgabe AN: so verhaelt sich Logic („Click while recording" ist gesetzt,
    // „Only during count-in" nicht), und der Beatbau braucht es so.
    return (bool) edit.state.getProperty (clickWhileRecordingID, true);
}

void countin::setClickWhileRecording (te::Edit& edit, bool shouldClick)
{
    // Ohne UndoManager — wie TE es mit `clickTrackEnabled` selbst haelt: eine
    // Abhoer-Einstellung gehoert nicht in die Rueckgaengig-Kette des Anwenders.
    edit.state.setProperty (clickWhileRecordingID, shouldClick, nullptr);
}

//==============================================================================
namespace
{
    /** Zwei Bereiche gelten als gleich, wenn beide Kanten auf eine Mikrosekunde
        genau uebereinstimmen — Zeitpositionen laufen ueber double. */
    bool sameRange (TimeRange a, TimeRange b) noexcept
    {
        return std::abs (a.getStart().inSeconds() - b.getStart().inSeconds()) < 1.0e-6
            && std::abs (a.getEnd().inSeconds()   - b.getEnd().inSeconds())   < 1.0e-6;
    }

    /** Dasselbe Kriterium, das `TransportControl::performRecord()` anlegt, bevor
        es die Aufnahme ueberhaupt versucht (`areAnyInputsRecording()` — dort
        privat, deshalb hier nachgebaut). */
    bool anyInputArmed (te::Edit& edit)
    {
        for (auto* in : edit.getAllInputDevices())
            if (in != nullptr && in->isRecordingActive())
                return true;

        return false;
    }
}

CountInRecorder::Outcome CountInRecorder::start (te::Edit& edit, int bars,
                                                 bool countInClick, bool clickWhileRecording)
{
    auto& transport = edit.getTransport();

    if (bars <= 0 || phase != Phase::idle || transport.isRecording())
        return Outcome::noRunUp;

    // Ohne scharfgeschalteten Eingang lehnt TE die Aufnahme ohnehin ab (mit
    // eigener Meldung). Dann NICHT vorher losspielen — sonst laeuft der Kopf
    // vor und niemand nimmt auf; genau dieses Bild hat der Anwender bereits
    // einmal gesehen.
    if (! anyInputArmed (edit))
        return Outcome::noRunUp;

    // ---- Schleifen-Aufnahme (Takes) ------------------------------------
    // Der Kopf kann bei eingeschalteter Schleife nicht VOR dem Bereich stehen
    // (PlayHead faltet modulo zurueck, solange rollInToLoop nicht gesetzt ist —
    // und das setzt nur TEs eigener Aufnahmeweg). Also uebergeben wir hier an
    // TEs nativen Vorzaehler: er rollt sauber in die Schleife hinein und
    // laesst die Take-Mechanik unberuehrt. TE kennt 1 oder 2 Takte.
    if (transport.looping)
    {
        // Den Klickbereich setzt hier TE selbst (performRecord() rechnet ihn aus
        // den Vorzaehler-Beats). Uns bleibt der SCHALTER: soll die Aufnahme
        // still sein, muss er aus — sonst ist TEs Bereich wirkungslos.
        borrowClick (edit, countin::decideClick (edit.clickTrackEnabled.get(),
                                                 countInClick, clickWhileRecording, {}));

        const auto saved = edit.getCountInMode();
        edit.setCountInMode (bars >= 2 ? te::Edit::CountIn::twoBar
                                       : te::Edit::CountIn::oneBar);

        transport.record (false);       // laeuft synchron durch performRecord()

        edit.setCountInMode (saved);

        if (! transport.isRecording())
        {
            giveBack (edit);
            return Outcome::failed;
        }

        // Geliehen bleibt geliehen, bis die Aufnahme endet — poll() raeumt auf.
        phase         = Phase::recording;
        sawRecording  = true;
        return Outcome::nativeLoopRollIn;
    }

    // ---- Normalfall: einrollen und einpunchen ---------------------------
    const bool punchAlreadyOn = edit.recordingPunchInOut.get();
    const auto plannedRecStart = punchAlreadyOn ? transport.getLoopRange().getStart()
                                                : transport.getPosition();

    const auto plan = countin::makePlan (edit.tempoSequence, plannedRecStart, bars);

    if (! plan.enabled)
        return Outcome::noRunUp;

    savedLoopRange = transport.getLoopRange();
    savedPunch     = punchAlreadyOn;
    borrowedPunch  = false;
    borrowedClick  = false;
    recordStart    = plannedRecStart;

    if (! punchAlreadyOn)
    {
        // Der Punch-Bereich IST der Aufnahmebereich: ab dem Startpunkt bis ans
        // Ende. Damit ist der Einstieg sample-genau (TE nimmt
        // getLoopRange().getStart() als punchInTime), egal wann wir gleich
        // scharfschalten. Geliehen — wird in giveBack() zurueckgegeben.
        appliedLoopRange = { recordStart, te::Edit::getMaximumEditEnd() };
        transport.setLoopRange (appliedLoopRange);

        // ZURUECKLESEN, was TE tatsaechlich gespeichert hat: setLoopRange()
        // KLEMMT auf `edit.getLength() + Edit::getMaximumLength() * 0.75`. Merkt
        // man sich den ungeklemmten Wunschwert, schlaegt der Vergleich in
        // giveBack() fehl — wir hielten unseren eigenen Bereich fuer den des
        // Anwenders und gaben ihn nie zurueck. Im Messlauf 2026-08-18 gefangen:
        // nach der Aufnahme stand LOOPBIS auf 129600 statt auf dem alten Wert.
        appliedLoopRange = transport.getLoopRange();
        edit.recordingPunchInOut.setValue (true, nullptr);
        borrowedPunch = true;
    }

    // Positionieren. TE klemmt in performPositionChange() auf
    //   -getNumCountInBeats() Beats - 0.5 s
    // — ohne Vorzaehler-Modus also auf -0.5 s. Wer bei Takt 1 aufnimmt, bekaeme
    // damit gar keinen Vorlauf. Deshalb heben wir die Klemme fuer genau diesen
    // einen Aufruf an und nehmen danach den ACHTEN Wert
    // (`transport.getPosition()`, den TE zurueckschreibt) als Vorlauf-Anfang —
    // so steht die Klemmlogik an genau einer Stelle, naemlich in TE.
    {
        const auto savedMode = edit.getCountInMode();
        edit.setCountInMode (te::Edit::CountIn::twoBar);
        transport.setPosition (TimePosition::fromSeconds (plan.countInStartSecs));
        edit.setCountInMode (savedMode);
    }

    runUpStart = transport.getPosition();

    if (runUpStart >= recordStart)
    {
        // Vor dem Songanfang war kein Platz mehr — ohne Zaehlen aufnehmen,
        // statt still nichts zu tun.
        giveBack (edit);
        return Outcome::noRunUp;
    }

    transport.play (false);

    // ERST NACH play(): performPlay() loescht den Klickbereich an seinem Ende.
    // Mit leerem Klick-Schalter klingt der Klick dann NUR in diesem Bereich
    // (ClickGenerator::isMutedAtTime) — Logics „Only during count-in". Steht
    // der Klick des Anwenders ohnehin an, laeuft er einfach weiter.
    //
    // Das Ende liegt eine HALBE Zaehlzeit VOR dem Aufnahmestart. Sonst klingt
    // der Schlag AUF dem Aufnahmestart noch mit — aus „1-2-3-4, Aufnahme" wuerde
    // „1-2-3-4-5". TE macht es in seinem eigenen Vorzaehler genauso und
    // begruendet es zusaetzlich damit, keinen Block zu hoeren, der direkt auf
    // einem Schlag beginnt.
    {
        auto& ts = edit.tempoSequence;
        const auto clickEnd = ts.toTime (ts.toBeats (recordStart) - BeatDuration::fromBeats (0.5));
        const TimeRange runUp { runUpStart, std::max (runUpStart, clickEnd) };

        borrowClick (edit, countin::decideClick (edit.clickTrackEnabled.get(),
                                                 countInClick, clickWhileRecording, runUp));
    }

    phase        = Phase::rollingIn;
    sawRecording = false;
    return Outcome::rollingIn;
}

void CountInRecorder::poll (te::Edit& edit)
{
    if (phase == Phase::idle)
        return;

    auto& transport = edit.getTransport();

    if (phase == Phase::rollingIn)
    {
        if (! transport.isPlaying())
        {
            giveBack (edit);            // abgebrochen (Stopp gedrueckt)
            return;
        }

        // Scharfschalten, SOBALD der Transport wirklich rollt. TEs
        // performRecord() dereferenziert im „laeuft schon"-Zweig den SyncPoint
        // des Wiedergabekontexts — den gibt es erst, wenn der Audio-Thread
        // einen Block gerechnet hat. Genau darauf warten wir hier; der
        // Aufnahmestart selbst bleibt davon unberuehrt, weil der Punch-Bereich
        // ihn festlegt.
        auto* ctx = transport.getCurrentPlaybackContext();

        if (ctx == nullptr || ! ctx->getSyncPoint().has_value())
        {
            if (transport.getPosition() >= recordStart)
            {
                // Es ist gerollt, aber nie ein Kontext entstanden — nicht
                // stillschweigend weiterlaufen lassen.
                transport.stop (false, false);
                giveBack (edit);
            }
            return;
        }

        transport.record (false);

        if (transport.isRecording())
        {
            phase        = Phase::recording;
            sawRecording = true;
        }
        else
        {
            transport.stop (false, false);
            giveBack (edit);
        }

        return;
    }

    // Phase::recording — geliehenen Zustand erst zurueckgeben, wenn die
    // Aufnahme abgeschlossen ist: EditPlaybackContext::stopRecording() liest
    // `transport.getLoopRange()` fuer den Punch-Ausstieg noch waehrend des
    // Stoppens.
    if (transport.isRecording())
    {
        sawRecording = true;
        return;
    }

    // `sawRecording` verhindert, dass wir gleich beim ersten Tick aufraeumen,
    // falls die Aufnahme erst einen Moment spaeter wirklich anlaeuft.
    if (sawRecording || ! transport.isPlaying())
        giveBack (edit);
}

void CountInRecorder::holdClickForRecording (te::Edit& edit, bool clickWhileRecording)
{
    if (phase != Phase::idle || ! edit.getTransport().isRecording())
        return;

    // Kein Vorlauf, also auch kein Vorlaufbereich: entweder der Klick laeuft
    // weiter, oder er ist ab jetzt still.
    const auto setup = countin::decideClick (edit.clickTrackEnabled.get(),
                                             false, clickWhileRecording, {});

    if (setup.enabled == edit.clickTrackEnabled.get() && edit.getClickTrackRange().isEmpty())
        return;                         // nichts zu leihen

    borrowedPunch = false;
    borrowClick (edit, setup);
    phase         = Phase::recording;
    sawRecording  = true;
}

void CountInRecorder::borrowClick (te::Edit& edit, countin::ClickSetup setup)
{
    if (! borrowedClick)
    {
        savedClick    = edit.clickTrackEnabled.get();
        borrowedClick = true;
    }

    edit.clickTrackEnabled.setValue (setup.enabled, nullptr);
    edit.setClickTrackRange (setup.range);
}

void CountInRecorder::detach() noexcept
{
    phase         = Phase::idle;
    borrowedPunch = false;
    borrowedClick = false;
}

void CountInRecorder::giveBack (te::Edit& edit)
{
    auto& transport = edit.getTransport();

    edit.setClickTrackRange ({});

    if (borrowedClick)
        edit.clickTrackEnabled.setValue (savedClick, nullptr);

    if (borrowedPunch)
    {
        // Nur zuruecknehmen, was noch UNSER Wert ist — hat der Anwender
        // waehrenddessen selbst am Bereich gezogen, gehoert er ihm.
        if (sameRange (transport.getLoopRange(), appliedLoopRange))
            transport.setLoopRange (savedLoopRange);

        edit.recordingPunchInOut.setValue (savedPunch, nullptr);
    }

    borrowedPunch = false;
    borrowedClick = false;
    phase         = Phase::idle;
    sawRecording  = false;
}
