#include "AiLiveProtocol.h"

#include <sys/stat.h>

namespace aizana
{
    namespace
    {
        juce::var emptyObject()
        {
            return juce::var (new juce::DynamicObject());
        }

        juce::String toLine (const juce::var& v)
        {
            // allOnOneLine = true → exactly one physical line (embedded newlines stay \n-escaped),
            // which is what the newline-delimited socket transport requires.
            return juce::JSON::toString (v, true);
        }
    }

    juce::File AiLiveProtocol::liveBridgeFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Aizana")
                   .getChildFile ("aistudio")
                   .getChildFile ("live.json");
    }

    //==============================================================================
    juce::String AiLiveProtocol::makeRequest (const juce::String& token,
                                              const juce::String& tool,
                                              const juce::var&     args)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("token", token);
        o->setProperty ("tool",  tool);
        o->setProperty ("args",  args.isObject() ? args : emptyObject());
        return toLine (juce::var (o));
    }

    LiveRequest AiLiveProtocol::parseRequest (const juce::String& line)
    {
        LiveRequest r;
        const auto v = juce::JSON::parse (line.trim());
        if (! v.isObject())
            return r;

        r.token = v.getProperty ("token", {}).toString();
        r.tool  = v.getProperty ("tool",  {}).toString();
        r.args  = v.getProperty ("args", emptyObject());
        r.valid = r.tool.isNotEmpty();
        return r;
    }

    //==============================================================================
    juce::String AiLiveProtocol::makeResponseOk (const juce::String& text)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("ok",   true);
        o->setProperty ("text", text);
        return toLine (juce::var (o));
    }

    juce::String AiLiveProtocol::makeResponseError (const juce::String& error)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("ok",    false);
        o->setProperty ("error", error);
        return toLine (juce::var (o));
    }

    LiveResponse AiLiveProtocol::parseResponse (const juce::String& line)
    {
        LiveResponse r;
        const auto v = juce::JSON::parse (line.trim());
        if (! v.isObject())
            return r;

        r.valid = true;
        r.ok    = (bool) v.getProperty ("ok", false);
        r.text  = v.getProperty ("text",  {}).toString();
        r.error = v.getProperty ("error", {}).toString();
        return r;
    }

    //==============================================================================
    juce::File AiLiveProtocol::writeDiscovery (const juce::File& file, int port, const juce::String& token)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("port",  port);
        o->setProperty ("token", token);

        file.getParentDirectory().createDirectory();
        file.replaceWithText (toLine (juce::var (o)));

        // The token grants control of the running mix session. Restrict the discovery file to
        // the owner (0600) so another local user can't read it and drive the live bridge. The
        // app and the aizana-mcp child run as the same user, so owner-only read is sufficient.
        ::chmod (file.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR);
        return file;
    }

    LiveDiscovery AiLiveProtocol::readDiscovery (const juce::File& file)
    {
        LiveDiscovery d;
        if (! file.existsAsFile())
            return d;

        const auto v = juce::JSON::parse (file.loadFileAsString());
        if (! v.isObject())
            return d;

        d.port  = (int) v.getProperty ("port", 0);
        d.token = v.getProperty ("token", {}).toString();
        d.valid = d.port > 0 && d.token.isNotEmpty();
        return d;
    }

    bool AiLiveProtocol::discoveryMatches (const juce::File& file, int port, const juce::String& token)
    {
        const auto d = readDiscovery (file);
        return d.valid && d.port == port && d.token == token;
    }
}
