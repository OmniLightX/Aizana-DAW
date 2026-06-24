#pragma once

#include <juce_core/juce_core.h>
#include "AiLiveProtocol.h"

class EditManager;
class MixApplyEngine;
class AICommandBridge;

namespace cloudes
{
    /**
        AiStudioLiveServer — Phase 4 flagship "Cloudes AI Studio", Slice 2 (live mixing).

        The app side of the live bridge. While the app runs it hosts a loopback TCP socket
        and writes a discovery file (port + random token) so the agent's `cloudes-mcp` child
        can reach the RUNNING session. Each connection carries one AiLiveProtocol request;
        the server validates the token, dispatches the tool, and writes one response line.

        Threading:
          - The accept/read loop runs on a dedicated background thread (NOT the audio thread).
          - Every tool that touches the Edit is marshalled to the MESSAGE thread (callAsync +
            WaitableEvent) — TE structure access and mutation belong there.
          - Mutating tools go through MixApplyEngine → AICommandBridge, so each agent move is
            one undo step (same path as the KI-Mix button). The server never mutates directly.

        Read tools  : list_tracks
        Write tools : set_track_volume {track, db}, set_track_pan {track, pan},
                      mute_track {track, muted}, solo_track {track, soloed},
                      set_track_eq {track, bands[]}, add_compressor {track, ...},
                      add_deesser {track, ...}.
        Volume/pan/EQ/compress/de-ess go through MixApplyEngine (MixAction vocabulary);
        mute/solo are not MixActions and go through the bridge directly (still one undo step).
    */
    class AiStudioLiveServer : private juce::Thread
    {
    public:
        AiStudioLiveServer (EditManager& editManager, MixApplyEngine& mixApply, AICommandBridge& bridge);
        ~AiStudioLiveServer() override;

        /** Bind a loopback port, mint a token, write the discovery file, start listening.
            Safe to call twice (no-op if already running). Returns false if no port was free. */
        bool start();

        /** Stop listening, close the socket, delete the discovery file. */
        void stop();

        bool isRunning() const noexcept { return running; }
        int  getPort()   const noexcept { return boundPort; }

    private:
        void run() override;                                   // accept loop (bg thread)
        void handleConnection (juce::StreamingSocket& sock);   // one request/response
        LiveResponse dispatch (const LiveRequest& req);        // message thread

        EditManager&     editManager;
        MixApplyEngine&  mixApply;
        AICommandBridge& commandBridge;

        std::unique_ptr<juce::StreamingSocket> listener;
        juce::String token;
        int          boundPort = 0;
        bool         running   = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiStudioLiveServer)
    };
}
