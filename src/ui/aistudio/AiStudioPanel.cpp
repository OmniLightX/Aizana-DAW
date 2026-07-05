#include "AiStudioPanel.h"
#include "generator/FaustCompiler.h"

using namespace aizana;

//==============================================================================
// Background thread: runs the agent CLI and streams its stdout into the panel.

struct AiStudioPanel::AgentRunThread : public juce::Thread
{
    AgentRunThread (AiStudioPanel& ownerPanel, juce::StringArray spawnArgs, bool parseAsJson)
        : juce::Thread ("aizana-ai-agent")
        , owner (ownerPanel)
        , args  (std::move (spawnArgs))
        , parseJson (parseAsJson)
    {}

    ~AgentRunThread() override
    {
        signalThreadShouldExit();
        proc.kill();
        stopThread (3000);
    }

    void run() override
    {
        if (! proc.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        {
            post ("\n[!] Konnte den Agenten nicht starten.\n");
            finish (-1);
            return;
        }

        juce::String acc;
        char buffer[4096];

        while (! threadShouldExit())
        {
            const auto n = proc.readProcessOutput (buffer, (int) sizeof (buffer));
            if (n <= 0)
                break;                                   // stream closed → process ended

            acc += juce::String::fromUTF8 (buffer, n);

            for (int nl; (nl = acc.indexOfChar ('\n')) >= 0; )
            {
                const auto line = acc.substring (0, nl);
                acc = acc.substring (nl + 1);
                const auto text = parseJson ? AiAgentBridge::renderStreamJsonLine (line)
                                            : (line.trim().isNotEmpty() ? line + "\n" : juce::String());
                if (text.isNotEmpty())
                    post (text);
            }
        }

        if (acc.isNotEmpty())
        {
            const auto t = parseJson ? AiAgentBridge::renderStreamJsonLine (acc) : acc;
            if (t.isNotEmpty())
                post (t);
        }

        proc.waitForProcessToFinish (2000);
        finish (proc.getExitCode());
    }

    void post (const juce::String& text)
    {
        juce::MessageManager::callAsync (
            [safe = juce::Component::SafePointer<AiStudioPanel> (&owner), text]
            {
                if (safe != nullptr)
                    safe->onAgentText (text);
            });
    }

    void finish (int exitCode)
    {
        juce::MessageManager::callAsync (
            [safe = juce::Component::SafePointer<AiStudioPanel> (&owner), exitCode]
            {
                if (safe != nullptr)
                    safe->onAgentDone (exitCode);
            });
    }

    AiStudioPanel&    owner;
    juce::StringArray args;
    bool              parseJson = true;
    juce::ChildProcess proc;
};

//==============================================================================

AiStudioPanel::AiStudioPanel()
{
    sessionId = juce::Uuid().toDashedString();

    header.setText (juce::String::fromUTF8 ("KI-Studio \xe2\x80\x94 Plugins bauen und deinen Mix steuern"),
                    juce::dontSendNotification);
    header.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    header.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (header);

    transcript.setMultiLine (true);
    transcript.setReadOnly (true);
    transcript.setScrollbarsShown (true);
    transcript.setCaretVisible (false);
    transcript.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff141414));
    transcript.setColour (juce::TextEditor::textColourId, juce::Colour (0xffd8d8d8));
    transcript.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff333333));
    transcript.setFont (juce::Font (juce::FontOptions (13.0f)));
    addAndMakeVisible (transcript);

    input.setMultiLine (true, false);
    input.setReturnKeyStartsNewLine (false);   // Return sends
    input.setTextToShowWhenEmpty (juce::String::fromUTF8 ("z. B. \xe2\x80\x9emach mir ein Tape-Delay-Plugin\xe2\x80\x9c oder \xe2\x80\x9emach die Drums lauter und die Stimme klarer\xe2\x80\x9c"),
                                  juce::Colours::grey);
    input.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1e1e1e));
    input.setColour (juce::TextEditor::textColourId, juce::Colours::white);
    input.onReturnKey = [this] { send(); };
    addAndMakeVisible (input);

    sendButton.onClick = [this] { send(); };
    addAndMakeVisible (sendButton);

    status.setFont (juce::Font (juce::FontOptions (11.0f)));
    status.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    addAndMakeVisible (status);

    // Connect row: choose which assistant backs the chat + a re-detect button.
    backendLabel.setColour (juce::Label::textColourId, juce::Colour (0xff999999));
    backendLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (backendLabel);

    backendBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff262626));
    backendBox.setColour (juce::ComboBox::textColourId, juce::Colours::white);
    backendBox.onChange = [this] { onBackendChanged(); };
    addAndMakeVisible (backendBox);

    reconnectButton.onClick = [this] { refreshBackends(); };
    addAndMakeVisible (reconnectButton);

    refreshBackends();

    setSize (480, 560);
}

AiStudioPanel::~AiStudioPanel()
{
    runThread.reset();   // joins + kills the child process
}

void AiStudioPanel::refreshBackends()
{
    clis = AiAgentBridge::detectClis();
    backendBox.clear (juce::dontSendNotification);
    for (int i = 0; i < clis.size(); ++i)
        backendBox.addItem (clis.getReference (i).displayName, i + 1);

    if (clis.isEmpty())
    {
        cli = {};
        backendBox.setTextWhenNoChoicesAvailable (juce::String::fromUTF8 ("keine gefunden"));
        status.setText (juce::String::fromUTF8 ("Keine KI-CLI gefunden \xe2\x80\x94 installiere Claude Code oder Codex, dann \xe2\x80\x9e" "Neu verbinden\xe2\x80\x9c"),
                        juce::dontSendNotification);
        sendButton.setEnabled (false);
        input.setEnabled (false);
        return;
    }

    backendBox.setSelectedId (1, juce::dontSendNotification);   // first = preferred (Claude Code)
    cli = clis.getReference (0);
    sendButton.setEnabled (true);
    input.setEnabled (true);
    sessionId = juce::Uuid().toDashedString();
    firstTurn = true;
    status.setText (cli.displayName + juce::String::fromUTF8 (" verbunden \xe2\x80\xa2 generierte Plugins \xe2\x86\x92 \xe2\x80\x9e" "Aizana\xe2\x80\x9c-Rubrik"),
                    juce::dontSendNotification);
}

void AiStudioPanel::onBackendChanged()
{
    const int idx = backendBox.getSelectedId() - 1;
    if (idx < 0 || idx >= clis.size())
        return;
    cli = clis.getReference (idx);
    sessionId = juce::Uuid().toDashedString();
    firstTurn = true;
    status.setText (cli.displayName + juce::String::fromUTF8 (" verbunden"), juce::dontSendNotification);
}

void AiStudioPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    header.setBounds (r.removeFromTop (24));
    r.removeFromTop (6);

    auto connectRow = r.removeFromTop (26);
    backendLabel.setBounds (connectRow.removeFromLeft (28));
    reconnectButton.setBounds (connectRow.removeFromRight (110));
    connectRow.removeFromRight (6);
    backendBox.setBounds (connectRow.removeFromLeft (180));
    r.removeFromTop (6);

    status.setBounds (r.removeFromBottom (18));

    auto inputRow = r.removeFromBottom (64);
    r.removeFromBottom (8);
    sendButton.setBounds (inputRow.removeFromRight (90).reduced (0, 14));
    inputRow.removeFromRight (8);
    input.setBounds (inputRow);

    transcript.setBounds (r);
}

void AiStudioPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));
}

void AiStudioPanel::appendLine (const juce::String& text)
{
    transcript.moveCaretToEnd();
    transcript.insertTextAtCaret (text);
    transcript.moveCaretToEnd();
}

void AiStudioPanel::send()
{
    if (busy || ! cli.isValid())
        return;

    const auto prompt = input.getText().trim();
    if (prompt.isEmpty())
        return;

    const auto mcpBin    = resolveAizanaMcp();
    const auto toolchain = resolveToolchain();
    if (! mcpBin.existsAsFile())
    {
        appendLine (juce::String::fromUTF8 ("\n[!] aizana-mcp nicht gefunden \xe2\x80\x94 bitte das Target bauen (aizana-mcp).\n"));
        return;
    }

    input.clear();
    appendLine (juce::String::fromUTF8 ("\n\xe2\x96\xb6 Du: ") + prompt + "\n");

    juce::StringArray argv;
    bool parseJson = true;

    if (cli.type == AiCli::ClaudeCode)
    {
        const auto mcpConfig = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("aizana_ai_studio_mcp.json");
        AiAgentBridge::writeClaudeMcpConfig (mcpConfig, mcpBin, toolchain);
        argv      = AiAgentBridge::buildClaudeArgs (cli.executable, prompt, sessionId, firstTurn, mcpConfig);
        parseJson = true;   // claude --output-format stream-json
    }
    else // Codex
    {
        argv      = AiAgentBridge::buildCodexArgs (cli.executable, prompt, mcpBin, toolchain);
        parseJson = false;  // codex exec prints plain text
    }
    firstTurn = false;

    const auto spawn = AiAgentBridge::wrapInLoginShell (argv);

    busy = true;
    sendButton.setEnabled (false);
    status.setText (cli.displayName + juce::String::fromUTF8 (" arbeitet\xe2\x80\xa6"), juce::dontSendNotification);

    runThread = std::make_unique<AgentRunThread> (*this, spawn, parseJson);
    runThread->startThread();
}

void AiStudioPanel::onAgentText (const juce::String& text)
{
    appendLine (text);
}

void AiStudioPanel::onAgentDone (int exitCode)
{
    busy = false;
    sendButton.setEnabled (true);
    if (exitCode != 0)
        appendLine (juce::String::fromUTF8 ("\n[Agent beendet mit Code ") + juce::String (exitCode) + "]\n");
    appendLine ("\n");
    status.setText (cli.displayName + juce::String::fromUTF8 (" bereit"), juce::dontSendNotification);
}

//==============================================================================

juce::File AiStudioPanel::resolveAizanaMcp()
{
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("AIZANA_MCP_BINARY", {});
        env.isNotEmpty() && juce::File (env).existsAsFile())
        return juce::File (env);

    const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);

    // Bundled inside the app (release/normal run).
    const auto bundled = app.getChildFile ("Contents/Resources/aizana-mcp");
    if (bundled.existsAsFile())
        return bundled;

    // Dev fallback: build tree (.../build/Aizana_artefacts/<cfg>/Aizana.app → .../build/aizana-mcp_artefacts/<cfg>/aizana-mcp)
    const auto cfgDir = app.getParentDirectory();                 // <cfg> (e.g. Release)
    const auto dev = cfgDir.getParentDirectory().getParentDirectory()  // build/
                         .getChildFile ("aizana-mcp_artefacts")
                         .getChildFile (cfgDir.getFileName())
                         .getChildFile ("aizana-mcp");
    if (dev.existsAsFile())
        return dev;

    return {};
}

juce::File AiStudioPanel::resolveToolchain()
{
    const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    const auto bundled = app.getChildFile ("Contents/Resources/GeneratorToolchain");
    if (bundled.isDirectory())
        return bundled;
    // Otherwise let aizana-mcp resolve its own sibling toolchain (returns empty → no env set).
    return {};
}
