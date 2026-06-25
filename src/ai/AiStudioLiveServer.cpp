#include "AiStudioLiveServer.h"

#include "../engine/EditManager.h"
#include "../engine/MixerEngine.h"
#include "../engine/AICommandBridge.h"
#include "MixApplyEngine.h"
#include "MixAction.h"

#include <memory>

namespace te = tracktion::engine;

namespace cloudes
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
            for (auto* t : te::getAudioTracks (edit))
                if (MixerEngine::isAudioChannel (*t) && t->getName().equalsIgnoreCase (name))
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
                            const juce::String& name, cloudes::MixAction& out)
        {
            const auto type = band.getProperty ("type", {}).toString().toLowerCase();
            const float freq = (float) (double) band.getProperty ("freq", 0.0);
            if (freq <= 0.0f)
                return false;

            out = cloudes::MixAction{};
            out.targetTrack = id;
            out.targetName  = name;
            out.freqHz      = freq;
            out.gainDb      = (float) (double) band.getProperty ("gain", 0.0);
            out.q           = argNum (band, "q", 0.7f);

            if (type == "high_pass" || type == "highpass" || type == "hpf")
                out.type = cloudes::MixActionType::HighPassFilter;
            else if (type == "low_shelf" || type == "lowshelf")
                out.type = cloudes::MixActionType::LowShelf;
            else if (type == "high_shelf" || type == "highshelf")
                out.type = cloudes::MixActionType::HighShelf;
            else
                out.type = (out.gainDb < 0.0f) ? cloudes::MixActionType::CarveBand
                                               : cloudes::MixActionType::BoostBand;   // bell
            return true;
        }
    }

    //==============================================================================
    AiStudioLiveServer::AiStudioLiveServer (EditManager& em, MixApplyEngine& ma, AICommandBridge& br)
        : juce::Thread ("cloudes-live-bridge"), editManager (em), mixApply (ma), commandBridge (br)
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
        return true;
    }

    void AiStudioLiveServer::stop()
    {
        if (! running && listener == nullptr)
            return;

        running = false;
        alive->store (false);         // any queued message-thread job now bails early
        signalThreadShouldExit();     // unblocks the connection wait loops

        if (listener != nullptr)
            listener->close();        // unblocks waitForNextConnection()

        stopThread (4000);            // > the per-iteration waits below → no force-kill
        listener.reset();

        AiLiveProtocol::liveBridgeFile().deleteFile();
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
                pending->out = guard->load() ? dispatch (req) : fail ("Cloudes is shutting down");
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
            return fail ("no session is open in Cloudes");

        if (req.tool == "list_tracks")
        {
            juce::String body;
            int n = 0;
            for (auto* t : te::getAudioTracks (*edit))
            {
                if (! MixerEngine::isAudioChannel (*t))
                    continue;

                float gainDb = 0.0f, pan = 0.0f;
                if (auto* vp = t->getVolumePlugin())
                {
                    gainDb = vp->getVolumeDb();
                    pan    = vp->pan.get();
                }

                juce::String flags;
                if (t->isMuted (false)) flags << ", muted";
                if (t->isSolo  (false)) flags << ", soloed";

                body << "  - \"" << t->getName() << "\"  [vol "
                     << juce::String (gainDb, 1) << " dB, pan " << panText (pan) << flags << "]\n";
                ++n;
            }

            if (n == 0)
                return ok ("The live session has no audio tracks yet.");

            return ok (juce::String (n) + " audio track(s) in the live session:\n" + body
                       + "\nUse set_track_volume / set_track_pan with the exact track name.");
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

        // EQ — one Cloudes EQ per call (bands fold into a single plugin instance).
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

        return fail ("unknown tool: " + req.tool);
    }
}
