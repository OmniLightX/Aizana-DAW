#include "AiStudioPanel.h"
#include "generator/FaustCompiler.h"

using namespace cloudes;

//==============================================================================
// Background thread: runs the agent CLI and streams its stdout into the panel.

struct AiStudioPanel::AgentRunThread : public juce::Thread
{
    AgentRunThread (AiStudioPanel& ownerPanel, juce::StringArray spawnArgs)
        : juce::Thread ("cloudes-ai-agent")
        , owner (ownerPanel)
        , args  (std::move (spawnArgs))
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
                const auto text = AiAgentBridge::renderStreamJsonLine (line);
                if (text.isNotEmpty())
                    post (text);
            }
        }

        if (acc.isNotEmpty())
            if (const auto t = AiAgentBridge::renderStreamJsonLine (acc); t.isNotEmpty())
                post (t);

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
    juce::ChildProcess proc;
};

//==============================================================================

AiStudioPanel::AiStudioPanel()
{
    sessionId = juce::Uuid().toDashedString();

    header.setText (juce::String::fromUTF8 ("KI-Studio \xe2\x80\x94 beschreibe ein Plugin, die KI baut es"),
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
    input.setTextToShowWhenEmpty (juce::String::fromUTF8 ("z. B. \xe2\x80\x9emach mir ein cremiges Tape-Delay-Plugin\xe2\x80\x9c"),
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

    // Detect the user's CLI up front so we can tell them what's wired.
    cli = AiAgentBridge::detectCli();
    if (cli.isValid())
    {
        status.setText (cli.displayName + juce::String::fromUTF8 (" verbunden \xe2\x80\xa2 generierte Plugins erscheinen in der \xe2\x80\x9e" "Cloudes\xe2\x80\x9c-Rubrik"),
                        juce::dontSendNotification);
        appendLine (juce::String::fromUTF8 ("Verbunden mit ") + cli.displayName
                    + juce::String::fromUTF8 (". Beschreibe ein Plugin \xe2\x80\x94 es wird gebaut und landet in deiner Cloudes-Library.\n"));
    }
    else
    {
        status.setText (juce::String::fromUTF8 ("Keine KI-CLI gefunden \xe2\x80\x94 installiere Claude Code oder Codex"),
                        juce::dontSendNotification);
        appendLine (juce::String::fromUTF8 ("Keine KI-CLI (claude/codex) gefunden. Installiere Claude Code, dann \xc3\xb6" "ffne dieses Panel erneut.\n"));
        sendButton.setEnabled (false);
        input.setEnabled (false);
    }

    setSize (480, 560);
}

AiStudioPanel::~AiStudioPanel()
{
    runThread.reset();   // joins + kills the child process
}

void AiStudioPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    header.setBounds (r.removeFromTop (24));
    r.removeFromTop (4);
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

    const auto mcpBin    = resolveCloudesMcp();
    const auto toolchain = resolveToolchain();
    if (! mcpBin.existsAsFile())
    {
        appendLine (juce::String::fromUTF8 ("\n[!] cloudes-mcp nicht gefunden \xe2\x80\x94 bitte das Target bauen (cloudes-mcp).\n"));
        return;
    }

    // Currently only the Claude Code argument shape is implemented; Codex follows the same seam.
    if (cli.type != AiCli::ClaudeCode)
    {
        appendLine (juce::String::fromUTF8 ("\n[!] Aktuell ist nur Claude Code angebunden (Codex folgt).\n"));
        return;
    }

    input.clear();
    appendLine (juce::String::fromUTF8 ("\n\xe2\x96\xb6 Du: ") + prompt + "\n");

    const auto mcpConfig = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("cloudes_ai_studio_mcp.json");
    AiAgentBridge::writeClaudeMcpConfig (mcpConfig, mcpBin, toolchain);

    const auto argv    = AiAgentBridge::buildClaudeArgs (cli.executable, prompt, sessionId, firstTurn, mcpConfig);
    const auto spawn   = AiAgentBridge::wrapInLoginShell (argv);
    firstTurn = false;

    busy = true;
    sendButton.setEnabled (false);
    status.setText (juce::String::fromUTF8 ("KI arbeitet\xe2\x80\xa6"), juce::dontSendNotification);

    runThread = std::make_unique<AgentRunThread> (*this, spawn);
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

juce::File AiStudioPanel::resolveCloudesMcp()
{
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("CLOUDES_MCP_BINARY", {});
        env.isNotEmpty() && juce::File (env).existsAsFile())
        return juce::File (env);

    const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);

    // Bundled inside the app (release/normal run).
    const auto bundled = app.getChildFile ("Contents/Resources/cloudes-mcp");
    if (bundled.existsAsFile())
        return bundled;

    // Dev fallback: build tree (.../build/Cloudes_artefacts/<cfg>/Cloudes.app → .../build/cloudes-mcp_artefacts/<cfg>/cloudes-mcp)
    const auto cfgDir = app.getParentDirectory();                 // <cfg> (e.g. Release)
    const auto dev = cfgDir.getParentDirectory().getParentDirectory()  // build/
                         .getChildFile ("cloudes-mcp_artefacts")
                         .getChildFile (cfgDir.getFileName())
                         .getChildFile ("cloudes-mcp");
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
    // Otherwise let cloudes-mcp resolve its own sibling toolchain (returns empty → no env set).
    return {};
}
