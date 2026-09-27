#pragma once

#include "HrtfDatabase.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace juce_hrtf
{

//==============================================================================
/**  Renders a handful of mono sources to a binaural stereo pair.

     THREADING, because this is the part that matters:

     Fetching a filter for a new direction is not realtime safe -- it walks a
     lookup structure and interpolates. So it does not happen on the audio
     thread. Directions are published as plain atomics, a worker thread notices
     them and prepares filter pairs, and the audio thread picks up finished work
     via a two-slot handoff. Nothing on the audio path allocates, locks, or
     calls into libmysofa.

     The cost is a few milliseconds of latency between moving a source and
     hearing it move, which is far below the threshold where anyone notices.
*/
class BinauralRenderer
{
public:
    BinauralRenderer();
    ~BinauralRenderer();

    /**  Call from the message thread before playback. Allocates. */
    void prepare (double sampleRate, int maxBlockSize, int numSources);

    /**  Swaps in a new filter source. Safe to call while audio is running;
         the change takes effect over the next few milliseconds.
    */
    void setFilterProvider (std::shared_ptr<FilterProvider> provider);

    /**  Direction is in WORLD space -- see the note in HrtfTypes.h. Realtime safe. */
    void setSourceDirection (int sourceIndex, Vec3 worldDirection) noexcept;

    /**  Realtime safe. Marks every source for re-fetch. */
    void setListenerOrientation (Orientation) noexcept;

    /**  How long to blend between two filters, in samples. Longer is smoother
         but smears fast motion. 256 at 48 kHz is a reasonable default.
    */
    void setCrossfadeLength (int samples) noexcept;

    /**  Adds the rendered result into outLeft/outRight. Does NOT clear them
         first, so several renderers can share an output bus. Realtime safe.
    */
    void process (const float* const* sourceChannels,
                  int numSourceChannels,
                  float* outLeft,
                  float* outRight,
                  int numSamples) noexcept;

    void reset() noexcept;

    /**  Number of filter pairs the worker has handed over. Useful in tests and
         for a "is anything actually happening" readout in a demo UI.
    */
    int getFilterUpdateCount() const noexcept { return filterUpdates.load (std::memory_order_relaxed); }

private:
    //==========================================================================
    struct Source
    {
        void prepareState();
        void applyStagedFilter (int crossfadeLength) noexcept;
        void processBlock (const float* in, float* outL, float* outR, int n,
                           float delaySmoothing) noexcept;

        // --- audio thread only ---
        std::vector<float> history;        // doubled ring buffer, no modulo in the inner loop
        int   writePos = 0;
        FilterPair current, pending;
        int   fadeRemaining = 0;
        int   fadeLength = 0;
        bool  hasFilter = false;

        std::vector<float> delayLineL, delayLineR;
        int   delayWritePos = 0;
        float smoothedDelayL = 0.0f, smoothedDelayR = 0.0f;
        float targetDelayL   = 0.0f, targetDelayR   = 0.0f;

        // --- shared ---
        std::atomic<float> dirX { 1.0f }, dirY { 0.0f }, dirZ { 0.0f };
        std::atomic<bool>  dirty { true };

        FilterPair staging[2];
        std::atomic<int> stagedSlot { -1 };   // -1 means "audio thread has taken it"
        int workerWriteSlot = 0;              // worker thread only
    };

    void workerLoop();

    std::vector<std::unique_ptr<Source>> sources;
    double sampleRate = 0.0;
    int crossfadeLength = 256;
    float delaySmoothing = 0.0f;

    std::shared_ptr<FilterProvider> filterProvider;   // guarded by providerLock
    std::mutex providerLock;                          // worker + message thread only

    std::atomic<float> yaw { 0.0f }, pitch { 0.0f }, roll { 0.0f };
    std::atomic<int>  filterUpdates { 0 };
    std::atomic<bool> workerShouldExit { false };
    std::thread worker;
};

} // namespace juce_hrtf
