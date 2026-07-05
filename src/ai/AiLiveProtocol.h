#pragma once

#include <juce_core/juce_core.h>

namespace cloudes
{
    /**
        AiLiveProtocol — Phase 4 flagship "Cloudes AI Studio", Slice 2 (live mixing).

        Slice 1 lets the in-app agent author plugins by WRITING FILES (the standalone
        `cloudes-mcp` process compiles .clap into the user's library). Mixing is different:
        the agent must steer the RUNNING session, not the disk. This is the shared, pure,
        golden-testable seam for that:

          running Cloudes app            cloudes-mcp (spawned by the CLI)
          ───────────────────            ───────────────────────────────
          AiStudioLiveServer  ◀── localhost TCP (one JSON line each way) ── live mix tool
          (hosts a loopback socket,       (reads the discovery file, connects,
           applies via AICommandBridge)    sends a request, returns the reply)

        Both ends speak the same one-line JSON request/response and find each other through
        a small discovery file the app writes on startup. This layer is juce_core ONLY (no
        Tracktion, no GUI) so it links into `cloudes-mcp` and is unit-testable; the live
        track resolution + Edit mutation live in AiStudioLiveServer (app side, TE-aware).

        Wire format (newline-delimited, one message per connection):
          request  : {"token":"...","tool":"set_track_volume","args":{"track":"Lead","db":-3}}
          response : {"ok":true,"text":"..."}   |   {"ok":false,"error":"..."}
    */
    struct LiveRequest
    {
        juce::String token;
        juce::String tool;
        juce::var    args;          // tool-specific object ({} when none)
        bool         valid = false; // false → malformed / missing tool
    };

    struct LiveResponse
    {
        bool         ok    = false;
        juce::String text;   // shown to the agent on success
        juce::String error;  // shown to the agent on failure
        bool         valid = false; // false → malformed line
    };

    struct LiveDiscovery
    {
        int          port = 0;
        juce::String token;
        bool         valid = false; // true → port > 0 && token present
    };

    class AiLiveProtocol
    {
    public:
        /** The discovery file the running app writes and `cloudes-mcp` reads:
            ~/Library/Cloudes/aistudio/live.json
            (JUCE userApplicationDataDirectory on macOS is ~/Library, NOT
            ~/Library/Application Support — keep docs/debugging pointed there). */
        static juce::File liveBridgeFile();

        // --- requests -------------------------------------------------------
        static juce::String makeRequest (const juce::String& token,
                                         const juce::String& tool,
                                         const juce::var&     args);
        static LiveRequest  parseRequest (const juce::String& line);

        // --- responses ------------------------------------------------------
        static juce::String  makeResponseOk    (const juce::String& text);
        static juce::String  makeResponseError (const juce::String& error);
        static LiveResponse  parseResponse     (const juce::String& line);

        // --- discovery ------------------------------------------------------
        static juce::File      writeDiscovery (const juce::File& file, int port, const juce::String& token);
        static LiveDiscovery   readDiscovery  (const juce::File& file);
    };
}
