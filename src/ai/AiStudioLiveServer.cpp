#include "AiStudioLiveServer.h"

#include "../engine/EditManager.h"
#include "../engine/MixerEngine.h"
#include "../engine/AICommandBridge.h"
#include "MixApplyEngine.h"
#include "MixAction.h"

#include <memory>

namespace te = tracktion::engine;

namespace aizana
{
    namespace
    {
        // Loopback-only private range to probe for a free port.
        constexpr int kPortFirst = 49710;
        constexpr int kPortLast  = 49759;

        juce::String panText (float pan)
        {
            if (std::abs (pan) < 0.02f) return "C";
            const int pct = juce::roundToInt (std::abs (pan) * 100.0f);
            return juce::String (pct) + (pan < 0.0f ? "% L" : "% R");
        }

        LiveResponse ok (const juce::String& text)
        {
            LiveResponse r; r.valid = true; r.ok = true; r.text = text; return r;
        }

        LiveResponse fail (const juce::String& error)
        {
            LiveResponse r; r.valid = true; r.ok = false; r.error = error; return r;
        }

        te::AudioTrack* resolveTrack (te::Edit& edit, const juce::String& name)
        {
            // Plain audio channels first; then Bus/FX channels — buses are regular
            // AudioTracks and pros process them directly ("glue comp on the drum
            // bus"). The Master bus stays unreachable (not in getAudioTracks).
            for (auto* t : te::getAudioTracks (edit))
                if (MixerEngine::isAudioChannel (*t) && t->getName().equalsIgnoreCase (name))
                    return t;

            for (auto* t : te::getAudioTracks (edit))
                if ((MixerEngine::isBusChannel (*t) || MixerEngine::isFXChannel (*t))
                    && t->getName().equalsIgnoreCase (name))
                    return t;

            return nullptr;
        }

        float argNum (const juce::var& args, const char* key, float fallback)
        {
            return args.hasProperty (key) ? (float) (double) args.getProperty (key, fallback) : fallback;
        }

        // One EQ band JSON ({type,freq,gain,q}) → a MixAction. type ∈ high_pass / low_shelf /
        // high_shelf / bell (bell picks carve vs. boost by gain sign). Returns false if freq <= 0.
        bool buildEqAction (const juce::var& band, tracktion::engine::EditItemID id,
                            const juce::String& name, aizana::MixAction& out)
        {
            const auto type = band.getProperty ("type", {}).toString().toLowerCase();
            const float freq = (float) (double) band.getProperty ("freq", 0.0);
            if (freq <= 0.0f)
                return false;

            out = aizana::MixAction{};
            out.targetTrack = id;
            out.targetName  = name;
            out.freqHz      = freq;
            out.gainDb      = (float) (double) band.getProperty ("gain", 0.0);
            out.q           = argNum (band, "q", 0.7f);

            if (type == "high_pass" || type == "highpass" || type == "hpf")
                out.type = aizana::MixActionType::HighPassFilter;
            else if (type == "low_shelf" || type == "lowshelf")
                out.type = aizana::MixActionType::LowShelf;
            else if (type == "high_shelf" || type == "highshelf")
                out.type = aizana::MixActionType::HighShelf;
            else
                out.type = (out.gainDb < 0.0f) ? aizana::MixActionType::CarveBand
                                               : aizana::MixActionType::BoostBand;   // bell
            return true;
        }
    }

    //==============================================================================
    AiStudioLiveServer::AiStudioLiveServer (EditManager& em, MixApplyEngine& ma, AICommandBridge& br)
        : juce::Thread ("aizana-live-bridge"), editManager (em), mixApply (ma), commandBridge (br)
    {
    }

    AiStudioLiveServer::~AiStudioLiveServer()
    {
        stop();
    }

    //==============================================================================
    bool AiStudioLiveServer::start()
    {
        if (running)
            return true;

        listener = std::make_unique<juce::StreamingSocket>();

        boundPort = 0;
        for (int p = kPortFirst; p <= kPortLast; ++p)
            if (listener->createListener (p, "127.0.0.1"))
            {
                boundPort = p;
                break;
            }

        if (boundPort == 0)
        {
            listener.reset();
            juce::Logger::writeToLog ("AiStudioLiveServer: no free loopback port in range");
            return false;
        }

        token = juce::Uuid().toDashedString();
        AiLiveProtocol::writeDiscovery (AiLiveProtocol::liveBridgeFile(), boundPort, token);

        running = true;
        startThread();

        // Self-heal: while we run, live.json must always point at us. It can go missing
        // (external delete, or another instance's stop()) → the agent then reports
        // "Aizana isn't running" although it is. Re-assert it cheaply on the message thread.
        startTimer (3000);
        return true;
    }

    void AiStudioLiveServer::stop()
    {
        if (! running && listener == nullptr)
            return;

        stopTimer();

        running = false;
        alive->store (false);         // any queued message-thread job now bails early
        signalThreadShouldExit();     // unblocks the connection wait loops

        if (listener != nullptr)
            listener->close();        // unblocks waitForNextConnection()

        stopThread (4000);            // > the per-iteration waits below → no force-kill
        listener.reset();

        // Only remove live.json if it still describes THIS server. A second Aizana instance
        // (or a live-test client) may have re-bound a fresh port and rewritten it; deleting
        // it then would orphan the still-running instance ("running but no live.json").
        if (AiLiveProtocol::discoveryMatches (AiLiveProtocol::liveBridgeFile(), boundPort, token))
            AiLiveProtocol::liveBridgeFile().deleteFile();
    }

    void AiStudioLiveServer::timerCallback()
    {
        // Runs on the message thread. running/boundPort/token are only written on this
        // thread (start()/stop()), so no synchronisation is needed. Rewrite the discovery
        // file whenever it has gone missing or been clobbered by another instance.
        if (! running || boundPort == 0)
            return;

        if (! AiLiveProtocol::discoveryMatches (AiLiveProtocol::liveBridgeFile(), boundPort, token))
            AiLiveProtocol::writeDiscovery (AiLiveProtocol::liveBridgeFile(), boundPort, token);
    }

    //==============================================================================
    void AiStudioLiveServer::run()
    {
        while (! threadShouldExit())
        {
            if (listener == nullptr)
                break;

            std::unique_ptr<juce::StreamingSocket> conn (listener->waitForNextConnection());
            if (conn == nullptr)
                continue;   // closed on shutdown → loop re-checks threadShouldExit()

            handleConnection (*conn);
        }
    }

    void AiStudioLiveServer::handleConnection (juce::StreamingSocket& sock)
    {
        // Read one newline-terminated request line (cap size; tools are tiny).
        std::string buf;
        bool gotLine = false;
        while (buf.size() < (1u << 16) && ! threadShouldExit())
        {
            if (sock.waitUntilReady (true, 1000) != 1)   // short → loop stays responsive to stop()
                break;

            char tmp[512];
            const int n = sock.read (tmp, (int) sizeof tmp, false);
            if (n <= 0)
                break;

            buf.append (tmp, (size_t) n);
            const auto nl = buf.find ('\n');
            if (nl != std::string::npos) { buf.resize (nl); gotLine = true; break; }
        }

        if (! gotLine)
            return;

        const auto reqLine = juce::String::fromUTF8 (buf.c_str(), (int) buf.size());
        const auto req     = AiLiveProtocol::parseRequest (reqLine);

        LiveResponse resp;
        if (! req.valid)
            resp = fail ("malformed request");
        else if (req.token != token)
            resp = fail ("unauthorised (token mismatch)");
        else if (juce::MessageManager::existsAndIsCurrentThread())
        {
            resp = dispatch (req);
        }
        else
        {
            // Marshal Edit access to the message thread. Capture into a shared holder so a
            // timeout/shutdown can't leave the async job writing to dead stack; the `alive`
            // guard lets a job that outlives the server bail before touching `this`.
            struct Pending { LiveResponse out; juce::WaitableEvent done; };
            auto pending = std::make_shared<Pending>();
            auto guard   = alive;

            juce::MessageManager::callAsync ([this, req, pending, guard]
            {
                pending->out = guard->load() ? dispatch (req) : fail ("Aizana is shutting down");
                pending->done.signal();
            });

            // Poll so stop() (which sets threadShouldExit on the message thread) can never
            // deadlock against our wait — otherwise message thread ↔ this thread block forever.
            resp = fail ("timed out applying the move");
            while (! threadShouldExit())
                if (pending->done.wait (100))
                {
                    resp = pending->out;
                    break;
                }
        }

        const auto line = (resp.ok ? AiLiveProtocol::makeResponseOk (resp.text)
                                   : AiLiveProtocol::makeResponseError (resp.error)) + "\n";
        const auto utf8 = line.toRawUTF8();
        sock.write (utf8, (int) std::strlen (utf8));
    }

    //==============================================================================
    LiveResponse AiStudioLiveServer::dispatch (const LiveRequest& req)
    {
        auto* edit = editManager.getEdit();
        if (edit == nullptr)
            return fail ("no session is open in Aizana");

        if (req.tool == "list_tracks")
        {
            juce::String body;
            int n = 0;
            for (auto* t : te::getAudioTracks (*edit))
            {
                const bool isBus = MixerEngine::isBusChannel (*t);
                const bool isFx  = MixerEngine::isFXChannel  (*t);
                if (! MixerEngine::isAudioChannel (*t) && ! isBus && ! isFx)
                    continue;   // VCA etc.

                float gainDb = 0.0f, pan = 0.0f;
                if (auto* vp = t->getVolumePlugin())
                {
                    gainDb = vp->getVolumeDb();
                    pan    = vp->pan.get();
                }

                juce::String flags;
                if (isBus)              flags << ", BUS";
                if (isFx)               flags << ", FX bus";
                if (t->isMuted (false)) flags << ", muted";
                if (t->isSolo  (false)) flags << ", soloed";

                body << "  - \"" << t->getName() << "\"  [vol "
                     << juce::String (gainDb, 1) << " dB, pan " << panText (pan) << flags << "]\n";
                ++n;
            }

            if (n == 0)
                return ok ("The live session has no audio tracks yet.");

            return ok (juce::String (n) + " channel(s) in the live session:\n" + body
                       + "\nUse the exact name with the other tools. Entries marked BUS are bus "
                         "channels — you can EQ/compress them directly or route tracks to them "
                         "via create_bus_channel.");
        }

        if (req.tool == "set_track_volume" || req.tool == "set_track_pan")
        {
            const auto name = req.args.getProperty ("track", {}).toString();
            if (name.isEmpty())
                return fail ("'track' (string) is required");

            auto* track = resolveTrack (*edit, name);
            if (track == nullptr)
                return fail ("no audio track named \"" + name + "\" — call list_tracks for exact names");

            MixPlan plan;

            if (req.tool == "set_track_volume")
            {
                if (! req.args.hasProperty ("db"))
                    return fail ("'db' (number, dB) is required");

                const float db = juce::jlimit (-60.0f, 12.0f,
                                               (float) (double) req.args.getProperty ("db", 0.0));
                MixAction a;
                a.type        = MixActionType::SetTrackGain;
                a.targetTrack = track->itemID;
                a.targetName  = track->getName();
                a.gainDb      = db;
                plan.summary  = "KI-Studio: " + track->getName() + " Pegel " + juce::String (db, 1) + " dB";
                plan.add (a);
                mixApply.applyPlan (plan); commandBridge.applyPendingCommands();   // apply now → reply + later list_tracks are accurate
                return ok ("Set \"" + track->getName() + "\" volume to " + juce::String (db, 1) + " dB.");
            }

            // set_track_pan
            if (! req.args.hasProperty ("pan"))
                return fail ("'pan' (number, -1.0 = L .. +1.0 = R) is required");

            const float pan = juce::jlimit (-1.0f, 1.0f,
                                            (float) (double) req.args.getProperty ("pan", 0.0));
            MixAction a;
            a.type        = MixActionType::SetTrackPan;
            a.targetTrack = track->itemID;
            a.targetName  = track->getName();
            a.pan         = pan;
            plan.summary  = "KI-Studio: " + track->getName() + " Pan";
            plan.add (a);
            mixApply.applyPlan (plan); commandBridge.applyPendingCommands();   // apply now → reply + later list_tracks are accurate
            return ok ("Set \"" + track->getName() + "\" pan to " + panText (pan) + ".");
        }

        // mute / solo — not MixActions; enqueue directly. Writing the ValueTree property
        // through the UndoManager makes it one undo step (TE's setMute/setSolo aren't undoable).
        if (req.tool == "mute_track" || req.tool == "solo_track")
        {
            const auto name = req.args.getProperty ("track", {}).toString();
            if (name.isEmpty())
                return fail ("'track' (string) is required");

            auto* track = resolveTrack (*edit, name);
            if (track == nullptr)
                return fail ("no audio track named \"" + name + "\" — call list_tracks for exact names");

            const bool isMute = (req.tool == "mute_track");
            const bool value  = (bool) req.args.getProperty (isMute ? "muted" : "soloed", true);
            const auto id     = track->itemID;
            const auto tname  = track->getName();

            commandBridge.enqueue ("KI-Studio: " + tname + (isMute ? " Mute" : " Solo"),
                [emp = &editManager, id, isMute, value] (juce::UndoManager& um)
            {
                auto* e = emp->getEdit();
                if (e == nullptr)
                    return;

                for (auto* t : te::getAudioTracks (*e))
                    if (t->itemID == id)
                    {
                        if (isMute)
                        {
                            t->state.setProperty (te::IDs::mute, value, &um);
                        }
                        else
                        {
                            t->state.setProperty (te::IDs::soloIsolate, false, &um);  // see I-03
                            t->state.setProperty (te::IDs::solo, value, &um);
                        }
                        return;
                    }
            });
            commandBridge.applyPendingCommands();   // apply now → reply + later list_tracks are accurate

            const juce::String verb = isMute ? (value ? "Muted" : "Un-muted")
                                             : (value ? "Soloed" : "Un-soloed");
            return ok (verb + " \"" + tname + "\".");
        }

        // EQ — one Aizana EQ per call (bands fold into a single plugin instance).
        if (req.tool == "set_track_eq")
        {
            const auto name = req.args.getProperty ("track", {}).toString();
            if (name.isEmpty())
                return fail ("'track' (string) is required");

            auto* track = resolveTrack (*edit, name);
            if (track == nullptr)
                return fail ("no audio track named \"" + name + "\" — call list_tracks for exact names");

            auto* arr = req.args.getProperty ("bands", {}).getArray();
            if (arr == nullptr || arr->isEmpty())
                return fail ("'bands' (non-empty array of {type, freq, gain, q}) is required");

            MixPlan plan;
            for (const auto& b : *arr)
            {
                MixAction a;
                if (buildEqAction (b, track->itemID, track->getName(), a))
                    plan.add (a);
            }
            if (plan.isEmpty())
                return fail ("no valid EQ bands — each band needs a positive 'freq'");

            plan.summary = "KI-Studio: " + track->getName() + " EQ";
            mixApply.applyPlan (plan); commandBridge.applyPendingCommands();   // apply now → reply + later list_tracks are accurate
            return ok ("Added a " + juce::String (plan.size()) + "-band EQ to \"" + track->getName() + "\".");
        }

        // Compressor / de-esser — built-in FX inserts via the MixAction vocabulary.
        if (req.tool == "add_compressor" || req.tool == "add_deesser")
        {
            const auto name = req.args.getProperty ("track", {}).toString();
            if (name.isEmpty())
                return fail ("'track' (string) is required");

            auto* track = resolveTrack (*edit, name);
            if (track == nullptr)
                return fail ("no audio track named \"" + name + "\" — call list_tracks for exact names");

            MixAction a;
            a.targetTrack = track->itemID;
            a.targetName  = track->getName();

            juce::String what;
            if (req.tool == "add_compressor")
            {
                a.type         = MixActionType::Compress;
                a.thresholdDb  = juce::jlimit (-60.0f, 0.0f,  argNum (req.args, "threshold_db", -18.0f));
                a.ratio        = juce::jlimit (1.0f,   20.0f, argNum (req.args, "ratio",        2.0f));
                a.makeupGainDb = juce::jlimit (0.0f,   24.0f, argNum (req.args, "makeup_db",     0.0f));
                what = "compressor (" + juce::String (a.ratio, 1) + ":1 @ "
                         + juce::String (a.thresholdDb, 1) + " dB)";
            }
            else
            {
                a.type        = MixActionType::DeEss;
                a.freqHz      = juce::jlimit (2000.0f, 12000.0f, argNum (req.args, "freq_hz",      5500.0f));
                a.thresholdDb = juce::jlimit (-60.0f,  0.0f,     argNum (req.args, "threshold_db", -20.0f));
                a.ratio       = juce::jlimit (1.0f,    20.0f,    argNum (req.args, "ratio",         4.0f));
                what = "de-esser (" + juce::String (juce::roundToInt (a.freqHz)) + " Hz)";
            }

            MixPlan plan;
            plan.summary = "KI-Studio: " + track->getName() + " " + (req.tool == "add_compressor" ? "Comp" : "De-Ess");
            plan.add (a);
            mixApply.applyPlan (plan); commandBridge.applyPendingCommands();   // apply now → reply + later list_tracks are accurate
            return ok ("Added a " + what + " to \"" + track->getName() + "\".");
        }

        // Reverb — a send to a shared "Reverb" FX bus (pro pattern), not a per-track insert.
        if (req.tool == "add_reverb")
        {
            const auto name = req.args.getProperty ("track", {}).toString();
            if (name.isEmpty())
                return fail ("'track' (string) is required");

            auto* track = resolveTrack (*edit, name);
            if (track == nullptr)
                return fail ("no audio track named \"" + name + "\" — call list_tracks for exact names");

            // Sending the shared Reverb return to itself would be a feedback loop.
            if (MixerEngine::isFXChannel (*track) && track->getName().equalsIgnoreCase ("Reverb"))
                return fail ("\"Reverb\" IS the shared reverb return — sending it to itself would "
                             "feed back. Send other tracks/buses to it instead.");

            const float amount = juce::jlimit (0.0f, 1.0f, argNum (req.args, "amount", 0.3f));
            mixApply.addReverbSend (track->itemID, amount);
            commandBridge.applyPendingCommands();   // apply now → reply is accurate
            return ok ("Sent \"" + track->getName() + "\" to the shared Reverb bus ("
                       + juce::String (juce::roundToInt (amount * 100.0f)) + "%).");
        }

        // Bus channel — create (or reuse) a named bus and optionally route tracks to it.
        // Live-test finding 2026-07-02: the agent had no way to build bus routing.
        if (req.tool == "create_bus_channel")
        {
            const auto busName = req.args.getProperty ("name", {}).toString().trim();
            if (busName.isEmpty())
                return fail ("'name' (string) is required — the bus channel's display name");

            std::vector<te::EditItemID> ids;
            juce::StringArray routed, unknown;

            if (auto* arr = req.args.getProperty ("tracks", {}).getArray())
            {
                for (const auto& v : *arr)
                {
                    const auto tname = v.toString();
                    if (auto* track = resolveTrack (*edit, tname))
                    {
                        ids.push_back (track->itemID);
                        routed.add (track->getName());
                    }
                    else
                        unknown.add (tname);
                }

                if (! unknown.isEmpty())
                    return fail ("no audio track named " + unknown.joinIntoString (", ")
                                 + " — call list_tracks for exact names (no bus was created)");
            }

            mixApply.createBusAndRoute (busName, ids);
            commandBridge.applyPendingCommands();   // apply now → reply + later list_tracks are accurate

            juce::String msg = "Created (or reused) bus channel \"" + busName + "\".";
            if (! routed.isEmpty())
                msg += " Routed: " + routed.joinIntoString (", ") + ".";
            return ok (msg + " You can now process the bus itself (set_track_eq / add_compressor "
                       "with the bus name) or route more tracks by calling create_bus_channel "
                       "again with the same name.");
        }

        return fail ("unknown tool: " + req.tool);
    }
}
