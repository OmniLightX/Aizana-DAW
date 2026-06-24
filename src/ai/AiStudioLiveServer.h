#pragma once

#include <juce_core/juce_core.h>
#include "AiLiveProtocol.h"

class EditManager;
class MixApplyEngine;

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
        Write tools : set_track_volume {track, db}, set_track_pan {track, pan}
        (More moves — mute/solo/EQ/compress — slot in via the same dispatch in a follow-up.)
    */
    class AiStudioLiveServer : private juce::Thread
    {
    public:
        AiStudioLiveServer (EditManager& editManager, MixApplyEngine& mixApply);
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

        EditManager&    editManager;
        MixApplyEngine& mixApply;

        std::unique_ptr<juce::StreamingSocket> listener;
        juce::String token;
        int          boundPort = 0;
        bool         running   = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiStudioLiveServer)
    };
}
