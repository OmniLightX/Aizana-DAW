#pragma once
#include <functional>
#include <mutex>

namespace aizana
{
    // Used only by the message/IPC threads, never by the audio callback.
    // Copy before invocation so clearing a connection cannot destroy a callback
    // while its captures are being used on the IPC thread.
    class ConnectionLostCallback
    {
    public:
        void set (std::function<void()> next)
        {
            const std::lock_guard<std::mutex> lock (mutex);
            callback = std::move (next);
        }
        void notify()
        {
            std::function<void()> copy;
            {
                const std::lock_guard<std::mutex> lock (mutex);
                copy = callback;
            }
            if (copy)
                copy();
        }
    private:
        std::mutex mutex;
        std::function<void()> callback;
    };
}
