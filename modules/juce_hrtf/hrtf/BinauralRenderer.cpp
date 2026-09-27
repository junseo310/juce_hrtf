#include "BinauralRenderer.h"

#include <chrono>
#include <cstring>

namespace juce_hrtf
{

namespace
{
    // Generous headroom over a real interaural time difference, which tops out
    // around 0.7 ms (about 34 samples at 48 kHz).
    constexpr int maxDelaySamples = 256;

    inline float readDelayed (const std::vector<float>& line, int writePos, float delay) noexcept
    {
        const auto size = static_cast<int> (line.size());

        if (delay < 0.0f) delay = 0.0f;
        if (delay > static_cast<float> (size - 2)) delay = static_cast<float> (size - 2);

        const auto whole = static_cast<int> (delay);
        const auto frac  = delay - static_cast<float> (whole);

        auto i0 = writePos - whole;   while (i0 < 0) i0 += size;
        auto i1 = i0 - 1;             while (i1 < 0) i1 += size;

        return line[(size_t) i0] * (1.0f - frac) + line[(size_t) i1] * frac;
    }

    inline float convolve (const float* history, int writePos, const float* taps, int numTaps) noexcept
    {
        // history is doubled, so the newest numTaps samples are contiguous.
        const auto* x = history + writePos + FilterPair::maxTaps - (numTaps - 1);

        float acc = 0.0f;
        for (int k = 0; k < numTaps; ++k)
            acc += x[k] * taps[numTaps - 1 - k];

        return acc;
    }
}

//==============================================================================
void BinauralRenderer::Source::prepareState()
{
    history.assign (2 * (size_t) FilterPair::maxTaps, 0.0f);
    delayLineL.assign ((size_t) maxDelaySamples, 0.0f);
    delayLineR.assign ((size_t) maxDelaySamples, 0.0f);

    writePos = 0;
    delayWritePos = 0;
    fadeRemaining = 0;
    hasFilter = false;
    smoothedDelayL = smoothedDelayR = 0.0f;
    targetDelayL = targetDelayR = 0.0f;
}

void BinauralRenderer::Source::applyStagedFilter (int crossfadeLen) noexcept
{
    const auto slot = stagedSlot.exchange (-1, std::memory_order_acquire);

    if (slot < 0)
        return;

    if (! hasFilter)
    {
        // First filter ever: snap rather than fade in from silence.
        current = staging[slot];
        smoothedDelayL = current.leftDelay;
        smoothedDelayR = current.rightDelay;
        hasFilter = true;
        fadeRemaining = 0;
    }
    else
    {
        pending = staging[slot];
        fadeLength = crossfadeLen;
        fadeRemaining = crossfadeLen;
    }

    const auto& latest = fadeRemaining > 0 ? pending : current;
    targetDelayL = latest.leftDelay;
    targetDelayR = latest.rightDelay;
}

void BinauralRenderer::Source::processBlock (const float* in, float* outL, float* outR,
                                             int n, float smoothing) noexcept
{
    if (! hasFilter)
        return;

    for (int i = 0; i < n; ++i)
    {
        const auto x = in[i];

        history[(size_t) writePos] = x;
        history[(size_t) writePos + FilterPair::maxTaps] = x;

        float l = convolve (history.data(), writePos, current.left,  current.numTaps);
        float r = convolve (history.data(), writePos, current.right, current.numTaps);

        if (fadeRemaining > 0)
        {
            // Linear blend is fine here: the two filters are already time
            // aligned by the min-phase split, so they are correlated and do not
            // need an equal-power law.
            const auto t = 1.0f - (static_cast<float> (fadeRemaining) / static_cast<float> (fadeLength));

            const auto pl = convolve (history.data(), writePos, pending.left,  pending.numTaps);
            const auto pr = convolve (history.data(), writePos, pending.right, pending.numTaps);

            l = l * (1.0f - t) + pl * t;
            r = r * (1.0f - t) + pr * t;

            if (--fadeRemaining == 0)
                current = pending;
        }

        if (++writePos >= FilterPair::maxTaps)
            writePos = 0;

        // Arrival time, put back as a plain smoothed fractional delay.
        smoothedDelayL += (targetDelayL - smoothedDelayL) * smoothing;
        smoothedDelayR += (targetDelayR - smoothedDelayR) * smoothing;

        delayLineL[(size_t) delayWritePos] = l;
        delayLineR[(size_t) delayWritePos] = r;

        outL[i] += readDelayed (delayLineL, delayWritePos, smoothedDelayL);
        outR[i] += readDelayed (delayLineR, delayWritePos, smoothedDelayR);

        if (++delayWritePos >= maxDelaySamples)
            delayWritePos = 0;
    }
}

//==============================================================================
BinauralRenderer::BinauralRenderer() = default;

BinauralRenderer::~BinauralRenderer()
{
    workerShouldExit.store (true);

    if (worker.joinable())
        worker.join();
}

void BinauralRenderer::prepare (double newSampleRate, int /*maxBlockSize*/, int numSources)
{
    workerShouldExit.store (true);
    if (worker.joinable())
        worker.join();
    workerShouldExit.store (false);

    sampleRate = newSampleRate;

    // ~5 ms one-pole. Long enough to stay click-free, short enough that the
    // delay tracks a fast pan without lagging behind the tone.
    delaySmoothing = 1.0f - std::exp (-1.0f / (0.005f * static_cast<float> (sampleRate)));

    sources.clear();
    sources.reserve ((size_t) numSources);

    for (int i = 0; i < numSources; ++i)
    {
        auto s = std::make_unique<Source>();
        s->prepareState();
        sources.push_back (std::move (s));
    }

    worker = std::thread ([this] { workerLoop(); });
}

void BinauralRenderer::setFilterProvider (std::shared_ptr<FilterProvider> provider)
{
    {
        std::lock_guard<std::mutex> lock (providerLock);
        filterProvider = std::move (provider);
    }

    for (auto& s : sources)
        s->dirty.store (true, std::memory_order_relaxed);
}

void BinauralRenderer::setSourceDirection (int index, Vec3 dir) noexcept
{
    if (index < 0 || index >= (int) sources.size())
        return;

    auto& s = *sources[(size_t) index];

    // Three separate atomics can in principle tear against each other. The
    // worst case is one worker pass using a direction blended between two
    // adjacent positions, which is itself a valid direction, so it is not worth
    // a lock on the audio path.
    s.dirX.store (dir.x, std::memory_order_relaxed);
    s.dirY.store (dir.y, std::memory_order_relaxed);
    s.dirZ.store (dir.z, std::memory_order_relaxed);
    s.dirty.store (true, std::memory_order_release);
}

void BinauralRenderer::setListenerOrientation (Orientation o) noexcept
{
    yaw  .store (o.yaw,   std::memory_order_relaxed);
    pitch.store (o.pitch, std::memory_order_relaxed);
    roll .store (o.roll,  std::memory_order_relaxed);

    for (auto& s : sources)
        s->dirty.store (true, std::memory_order_release);
}

void BinauralRenderer::setCrossfadeLength (int samples) noexcept
{
    crossfadeLength = samples < 16 ? 16 : samples;
}

void BinauralRenderer::process (const float* const* sourceChannels,
                                int numSourceChannels,
                                float* outLeft,
                                float* outRight,
                                int numSamples) noexcept
{
    const auto n = numSourceChannels < (int) sources.size() ? numSourceChannels
                                                            : (int) sources.size();

    for (int i = 0; i < n; ++i)
    {
        auto& s = *sources[(size_t) i];
        s.applyStagedFilter (crossfadeLength);
        s.processBlock (sourceChannels[i], outLeft, outRight, numSamples, delaySmoothing);
    }
}

void BinauralRenderer::reset() noexcept
{
    for (auto& s : sources)
        s->prepareState();
}

void BinauralRenderer::workerLoop()
{
    while (! workerShouldExit.load (std::memory_order_relaxed))
    {
        std::shared_ptr<FilterProvider> provider;

        {
            std::lock_guard<std::mutex> lock (providerLock);
            provider = filterProvider;
        }

        if (provider != nullptr)
        {
            const Orientation head { yaw.load  (std::memory_order_relaxed),
                                     pitch.load (std::memory_order_relaxed),
                                     roll.load (std::memory_order_relaxed) };

            for (auto& sp : sources)
            {
                auto& s = *sp;

                // Do not stage new work until the audio thread has taken the
                // previous result -- that is what keeps the two-slot handoff
                // free of races without a lock.
                if (s.stagedSlot.load (std::memory_order_acquire) >= 0)
                    continue;

                if (! s.dirty.exchange (false, std::memory_order_acquire))
                    continue;

                const Vec3 world { s.dirX.load (std::memory_order_relaxed),
                                   s.dirY.load (std::memory_order_relaxed),
                                   s.dirZ.load (std::memory_order_relaxed) };

                if (provider->getFilter (worldToHead (world, head), s.staging[s.workerWriteSlot]))
                {
                    s.stagedSlot.store (s.workerWriteSlot, std::memory_order_release);
                    s.workerWriteSlot ^= 1;
                    filterUpdates.fetch_add (1, std::memory_order_relaxed);
                }
            }
        }

        std::this_thread::sleep_for (std::chrono::milliseconds (3));
    }
}

} // namespace juce_hrtf
