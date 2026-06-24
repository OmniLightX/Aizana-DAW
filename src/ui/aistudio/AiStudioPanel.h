#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ai/AiAgentBridge.h"

/**
    AiStudioPanel — Phase 4 flagship "Cloudes AI Studio": an in-app chat where the user
    talks to their own Claude Code / Codex CLI and has it act on Cloudes. The CLI is spawned
    as the agent (login-shell wrapped so it inherits the user's PATH/auth) with the bundled
    `cloudes-mcp` server registered, so the assistant can author real plugins that land in
    the user's "Cloudes" plugin library and show up live.

    Slice 1: plugin generation via chat. Live mixing tools are a follow-up (same seam).
    The subprocess runs on a background thread; output streams back via callAsync + SafePointer.
*/
class AiStudioPanel : public juce::Component
{
public:
    AiStudioPanel();
    ~AiStudioPanel() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    struct AgentRunThread;

    void send();
    void refreshBackends();                 // detect CLIs → fill the chooser + status
    void onBackendChanged();
    void onAgentText (const juce::String& text);
    void onAgentDone (int exitCode);
    void appendLine (const juce::String& text);

    // Resolve the bundled cloudes-mcp binary + its toolchain (env override → app bundle → dev build).
    static juce::File resolveCloudesMcp();
    static juce::File resolveToolchain();

    juce::TextEditor transcript;
    juce::TextEditor input;
    juce::TextButton sendButton { "Senden" };
    juce::Label      header;
    juce::Label      status;

    // Connect row: pick which assistant backs the chat + re-detect.
    juce::Label      backendLabel { {}, "KI:" };
    juce::ComboBox   backendBox;
    juce::TextButton reconnectButton { juce::String::fromUTF8 ("Neu verbinden") };
    juce::Array<cloudes::AiCliInfo> clis;

    cloudes::AiCliInfo cli;
    juce::String       sessionId;     // dashed UUID, stable per panel
    bool               firstTurn = true;
    bool               busy      = false;

    std::unique_ptr<AgentRunThread> runThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiStudioPanel)
};
