#include "SharedAudioRing.h"

#include <thread>
#include <chrono>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

// ---------------------------------------------------------------------------
// Shared-memory layout. POD only — both processes reinterpret the SAME bytes,
// so no virtuals, no pointers, fixed-width fields, and the atomics must be
// always-lock-free (asserted below) to be safe across the boundary.
// ---------------------------------------------------------------------------
struct SharedAudioRing::Header
{
    std::atomic<uint32_t> magic;        // kMagic once fully initialised by the host
    std::atomic<uint32_t> reqSeq;       // host bumps to request a block
    std::atomic<uint32_t> doneSeq;      // child mirrors reqSeq when the block is processed
    std::atomic<uint32_t> childAlive;   // 1 while child runs; 0 on CLEAN exit (crash leaves 1)
    uint32_t version;
    uint32_t maxChannels;
    uint32_t maxBlockSamples;
    uint32_t numChannels;               // per-request, valid for the current reqSeq
    uint32_t numSamples;                 // per-request
};

static_assert (std::atomic<uint32_t>::is_always_lock_free,
               "Cross-process atomics must be lock-free");

namespace
{
    constexpr uint32_t kMagic        = 0x434C4453; // 'CLDS'
    constexpr uint32_t kVersion      = 1;
    constexpr size_t   kAudioOffset  = 128;        // audio region starts here (header padded out)

    size_t totalSize (uint32_t maxChannels, uint32_t maxBlockSamples) noexcept
    {
        return kAudioOffset
             + (size_t) maxChannels * (size_t) maxBlockSamples * sizeof (float);
    }
}

// ---------------------------------------------------------------------------
// Construction / teardown
// ---------------------------------------------------------------------------
std::unique_ptr<SharedAudioRing> SharedAudioRing::createHost (uint32_t maxChannels,
                                                              uint32_t maxBlockSamples,
                                                              juce::String& outName)
{
    static_assert (sizeof (Header) <= kAudioOffset, "Header must fit before the audio region");

    if (maxChannels == 0 || maxBlockSamples == 0)
        return nullptr;

    // shm_open / sem names on macOS are capped at ~31 chars incl. the leading slash.
    static std::atomic<uint32_t> counter { 0 };
    char nameBuf[32] = {};
    std::snprintf (nameBuf, sizeof (nameBuf), "/cldsr%u_%u",
                   (unsigned) ::getpid(), (unsigned) counter.fetch_add (1));

    // O_EXCL guards against a stale region with the same name; unlink + retry once.
    int fd = ::shm_open (nameBuf, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0 && errno == EEXIST)
    {
        ::shm_unlink (nameBuf);
        fd = ::shm_open (nameBuf, O_CREAT | O_EXCL | O_RDWR, 0600);
    }
    if (fd < 0)
        return nullptr;

    const size_t size = totalSize (maxChannels, maxBlockSamples);

    if (::ftruncate (fd, (off_t) size) != 0)
    {
        ::close (fd);
        ::shm_unlink (nameBuf);
        return nullptr;
    }

    void* base = ::mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED)
    {
        ::close (fd);
        ::shm_unlink (nameBuf);
        return nullptr;
    }

    // Construct the header in place (host owns initialisation). Publish magic LAST with
    // release so a child that sees the magic also sees a fully-initialised header.
    auto* h = ::new (base) Header();
    h->reqSeq.store (0, std::memory_order_relaxed);
    h->doneSeq.store (0, std::memory_order_relaxed);
    h->childAlive.store (0, std::memory_order_relaxed);
    h->version         = kVersion;
    h->maxChannels     = maxChannels;
    h->maxBlockSamples = maxBlockSamples;
    h->numChannels     = 0;
    h->numSamples      = 0;
    h->magic.store (kMagic, std::memory_order_release);

    std::unique_ptr<SharedAudioRing> ring (new SharedAudioRing());
    ring->hdr         = h;
    ring->audio       = reinterpret_cast<float*> (static_cast<char*> (base) + kAudioOffset);
    ring->mapped      = base;
    ring->mappedSize  = size;
    ring->fd          = fd;
    ring->isHost      = true;
    ring->capChannels = maxChannels;
    ring->capSamples  = maxBlockSamples;
    ring->shmName     = juce::String (nameBuf);

    outName = ring->shmName;
    return ring;
}

std::unique_ptr<SharedAudioRing> SharedAudioRing::attachChild (const juce::String& name)
{
    const int fd = ::shm_open (name.toRawUTF8(), O_RDWR, 0600);
    if (fd < 0)
        return nullptr;

    // Map the header first to read the real size, then re-map the full region.
    void* probe = ::mmap (nullptr, kAudioOffset, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (probe == MAP_FAILED)
    {
        ::close (fd);
        return nullptr;
    }

    auto* h = reinterpret_cast<Header*> (probe);
    if (h->magic.load (std::memory_order_acquire) != kMagic || h->version != kVersion)
    {
        ::munmap (probe, kAudioOffset);
        ::close (fd);
        return nullptr;
    }

    const uint32_t mc = h->maxChannels;
    const uint32_t mb = h->maxBlockSamples;
    const size_t   size = totalSize (mc, mb);
    ::munmap (probe, kAudioOffset);

    void* base = ::mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED)
    {
        ::close (fd);
        return nullptr;
    }

    std::unique_ptr<SharedAudioRing> ring (new SharedAudioRing());
    ring->hdr         = reinterpret_cast<Header*> (base);
    ring->audio       = reinterpret_cast<float*> (static_cast<char*> (base) + kAudioOffset);
    ring->mapped      = base;
    ring->mappedSize  = size;
    ring->fd          = fd;
    ring->isHost      = false;
    ring->capChannels = mc;
    ring->capSamples  = mb;
    ring->shmName     = name;
    return ring;
}

SharedAudioRing::~SharedAudioRing()
{
    if (mapped != nullptr)
        ::munmap (mapped, mappedSize);
    if (fd >= 0)
        ::close (fd);
    if (isHost && shmName.isNotEmpty())
        ::shm_unlink (shmName.toRawUTF8());   // only the host owns the kernel object
}

// ---------------------------------------------------------------------------
// Host round trip (audio thread)
// ---------------------------------------------------------------------------
SharedAudioRing::Status SharedAudioRing::roundTrip (uint32_t numChannels,
                                                    uint32_t numSamples,
                                                    uint32_t timeoutMicros) noexcept
{
    if (hdr == nullptr || numChannels > capChannels || numSamples > capSamples)
        return Status::timedOut;

    // Per-request sizes, then publish a new request. The store-release on reqSeq orders the
    // audio writes (already in audioData()) and these sizes before the child's acquire-load.
    hdr->numChannels = numChannels;
    hdr->numSamples  = numSamples;

    const uint32_t seq = hdr->reqSeq.load (std::memory_order_relaxed) + 1;
    hdr->reqSeq.store (seq, std::memory_order_release);

    const int64_t ticksPerSec = juce::Time::getHighResolutionTicksPerSecond();
    const int64_t deadline = juce::Time::getHighResolutionTicks()
                           + (int64_t) ((double) timeoutMicros * 1.0e-6 * (double) ticksPerSec);

    for (int spins = 0;; ++spins)
    {
        if (hdr->doneSeq.load (std::memory_order_acquire) == seq)
            return Status::ok;   // child wrote output into audioData() before its release

        if (hdr->childAlive.load (std::memory_order_acquire) == 0)
            return Status::childGone;

        if (juce::Time::getHighResolutionTicks() > deadline)
            return Status::timedOut;   // hung or crashed — host must not block past this

        // Busy-spin briefly (lowest latency for the common fast reply), then back off to a
        // short sleep so we don't burn a core if the child is slow. Bounded by `deadline`.
        if (spins < 256)
            std::this_thread::yield();
        else
            std::this_thread::sleep_for (std::chrono::microseconds (20));
    }
}

// ---------------------------------------------------------------------------
// Child pump loop
// ---------------------------------------------------------------------------
void SharedAudioRing::runChildLoop (const std::function<void (float*, uint32_t, uint32_t)>& process,
                                    const std::function<bool()>& shouldExit)
{
    if (hdr == nullptr)
        return;

    hdr->childAlive.store (1, std::memory_order_release);

    // Sync to the CURRENT request, not doneSeq: if the host published a request before this
    // child attached (and got childGone), that block's audio may be mid-overwrite by the host's
    // next roundTrip. Skip it and wait for the next request — the host re-syncs on the seq it
    // waits for, so nothing is lost.
    uint32_t lastSeq = hdr->reqSeq.load (std::memory_order_acquire);
    int idleSpins = 0;

    while (! shouldExit())
    {
        const uint32_t seq = hdr->reqSeq.load (std::memory_order_acquire);

        if (seq != lastSeq)
        {
            const uint32_t ch = hdr->numChannels;
            const uint32_t n  = hdr->numSamples;

            if (ch <= capChannels && n <= capSamples && process)
                process (audio, ch, n);   // in place over the shared audio region

            lastSeq = seq;
            hdr->doneSeq.store (seq, std::memory_order_release);
            idleSpins = 0;
        }
        else
        {
            // Adaptive backoff: tight spin while busy, longer sleep when the transport is idle
            // so a stopped DAW doesn't peg a core. Caps at 1 ms latency-to-wake when idle.
            if (idleSpins < 512)        { std::this_thread::yield(); ++idleSpins; }
            else if (idleSpins < 4096)  { std::this_thread::sleep_for (std::chrono::microseconds (50));  ++idleSpins; }
            else                          std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }

    markChildExiting();
}

void SharedAudioRing::markChildExiting() noexcept
{
    if (hdr != nullptr)
        hdr->childAlive.store (0, std::memory_order_release);
}
