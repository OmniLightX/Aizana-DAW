#pragma once
#include <juce_core/juce_core.h>
#include <spawn.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <vector>
#include <crt_externs.h>

namespace aizana::detail
{
// Background-thread only. Own process group bounds compiler descendants too.
inline int runCompilerProcess (const juce::StringArray& args, juce::String& log,
                               int timeoutMs = 120000)
{
    if (args.isEmpty()) return -1;
    if (timeoutMs <= 0) { log << "\nCompiler timed out before launch."; return -2; }
    int pipefd[2];
    if (pipe (pipefd) != 0) return -1;
    // Configure nonblocking reads before spawning: a failed fcntl must never
    // silently turn the deadline loop into a blocking read.
    if (fcntl (pipefd[0], F_SETFD, FD_CLOEXEC) < 0
        || fcntl (pipefd[1], F_SETFD, FD_CLOEXEC) < 0
        || fcntl (pipefd[0], F_SETFL, O_NONBLOCK) < 0)
    {
        close (pipefd[0]); close (pipefd[1]);
        log << "\nCould not configure compiler output pipe.";
        return -1;
    }
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    const bool actionsReady = posix_spawn_file_actions_init (&actions) == 0;
    const bool attributesReady = posix_spawnattr_init (&attributes) == 0;
    if (! actionsReady || ! attributesReady
        || posix_spawn_file_actions_adddup2 (&actions, pipefd[1], STDOUT_FILENO) != 0
        || posix_spawn_file_actions_adddup2 (&actions, pipefd[1], STDERR_FILENO) != 0
        || posix_spawn_file_actions_addclose (&actions, pipefd[0]) != 0
        || posix_spawn_file_actions_addclose (&actions, pipefd[1]) != 0
        || posix_spawnattr_setflags (&attributes, POSIX_SPAWN_SETPGROUP) != 0
        || posix_spawnattr_setpgroup (&attributes, 0) != 0)
    {
        if (actionsReady) posix_spawn_file_actions_destroy (&actions);
        if (attributesReady) posix_spawnattr_destroy (&attributes);
        close (pipefd[0]); close (pipefd[1]);
        log << "\nCould not configure compiler process group.";
        return -1;
    }
    std::vector<char*> argv;
    for (const auto& arg : args) argv.push_back (const_cast<char*> (arg.toRawUTF8()));
    argv.push_back (nullptr);
    pid_t pid = -1;
    const int error = posix_spawnp (&pid, argv[0], &actions, &attributes,
                                   argv.data(), *_NSGetEnviron());
    posix_spawn_file_actions_destroy (&actions);
    posix_spawnattr_destroy (&attributes);
    close (pipefd[1]);
    if (error != 0)
    {
        close (pipefd[0]);
        log << "\nCould not start compiler (error " << error << ").";
        return -1;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds (timeoutMs);
    juce::MemoryOutputStream output;
    constexpr size_t maxOutput = 1024 * 1024;
    bool truncated = false, exited = false, eof = false, timedOut = false;
    int status = 0;
    while (! exited || ! eof)
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            timedOut = true;
            break;
        }
        // Bound each drain pass so continuous output cannot starve the deadline.
        for (int block = 0; block < 16; ++block)
        {
            char bytes[4096];
            const auto n = read (pipefd[0], bytes, sizeof bytes);
            if (n == 0) { eof = true; break; }
            if (n < 0) { if (errno == EINTR) continue; break; }
            const auto remaining = maxOutput - output.getDataSize();
            const auto keep = std::min (remaining, static_cast<size_t> (n));
            output.write (bytes, keep);
            truncated |= keep != static_cast<size_t> (n);
        }
        if (! exited)
        {
            const auto result = waitpid (pid, &status, WNOHANG);
            if (result == pid) exited = true;
            else if (result < 0 && errno != EINTR) { status = -1; exited = true; }
        }
        if (! exited || ! eof)
        {
            pollfd descriptor { pipefd[0], POLLIN, 0 };
            if (eof) poll (nullptr, 0, 10);
            else poll (&descriptor, 1, 10);
        }
    }
    // No compiler-owned child may outlive this invocation, including a child
    // that inherited stdout after the compiler itself exited.
    kill (-pid, SIGKILL);
    if (! exited)
        while (waitpid (pid, &status, 0) < 0 && errno == EINTR) {}
    close (pipefd[0]);
    log << juce::String::fromUTF8 (static_cast<const char*> (output.getData()),
                                 static_cast<int> (output.getDataSize()));
    if (truncated) log << "\n[Compiler output truncated after 1 MiB]";
    if (timedOut) { log << "\nCompiler timed out; process group terminated."; return -2; }
    return status >= 0 && WIFEXITED (status) ? WEXITSTATUS (status) : -1;
}

inline juce::String cppStringLiteral (const juce::String& text)
{
    juce::String result = "\"";
    // Fixed-width octal escapes preserve UTF-8 and cannot consume following digits.
    for (const unsigned char* p = reinterpret_cast<const unsigned char*> (text.toRawUTF8()); *p; ++p)
    {
        if (*p >= 32 && *p < 127 && *p != '"' && *p != '\\')
            result += juce::String::charToString (*p);
        else
        {
            result += '\\';
            result += juce::String::charToString ('0' + ((*p >> 6) & 7));
            result += juce::String::charToString ('0' + ((*p >> 3) & 7));
            result += juce::String::charToString ('0' + (*p & 7));
        }
    }
    return result + "\"";
}
}
