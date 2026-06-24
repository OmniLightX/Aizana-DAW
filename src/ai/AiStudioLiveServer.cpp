#include "AiStudioLiveServer.h"

#include "../engine/EditManager.h"
#include "../engine/MixerEngine.h"
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
    }

    //==============================================================================
    AiStudioLiveServer::AiStudioLiveServer (EditManager& em, MixApplyEngine& ma)
        : juce::Thread ("cloudes-live-bridge"), editManager (em), mixApply (ma)
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
        signalThreadShouldExit();

        if (listener != nullptr)
            listener->close();        // unblocks waitForNextConnection()

        stopThread (3000);
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
            if (sock.waitUntilReady (true, 4000) != 1)
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
        else
        {
            // Marshal Edit access to the message thread. Capture by value into a shared
            // holder so a timeout can't leave the async job writing to dead stack.
            struct Pending { LiveResponse out; juce::WaitableEvent done; };
            auto pending = std::make_shared<Pending>();

            if (juce::MessageManager::existsAndIsCurrentThread())
            {
                resp = dispatch (req);
            }
            else
            {
                juce::MessageManager::callAsync ([this, req, pending]
                {
                    pending->out = dispatch (req);
                    pending->done.signal();
                });

                resp = pending->done.wait (8000) ? pending->out
                                                 : fail ("timed out applying the move");
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

                body << "  - \"" << t->getName() << "\"  [vol "
                     << juce::String (gainDb, 1) << " dB, pan " << panText (pan) << "]\n";
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
                mixApply.applyPlan (plan);
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
            mixApply.applyPlan (plan);
            return ok ("Set \"" + track->getName() + "\" pan to " + panText (pan) + ".");
        }

        return fail ("unknown tool: " + req.tool);
    }
}
