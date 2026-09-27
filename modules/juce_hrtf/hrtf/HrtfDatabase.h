#pragma once

#include "HrtfTypes.h"

#include <memory>
#include <string>

namespace juce_hrtf
{

//==============================================================================
/**  Anything that can turn a direction into a filter pair.

     Kept abstract on purpose: the renderer never knows whether the filters came
     from a measured SOFA file, a synthetic model, or a test fixture. That is
     what lets the whole DSP path be tested without shipping a 100 MB dataset.

     Implementations are NOT required to be realtime safe. The renderer calls
     them from a worker thread and hands results to the audio thread.
*/
class FilterProvider
{
public:
    virtual ~FilterProvider() = default;

    /**  Fills `result` for the given direction, expressed in HEAD space.
         Returns false if no filter could be produced.
    */
    virtual bool getFilter (Vec3 directionInHeadSpace, FilterPair& result) = 0;

    /**  Sample rate the returned filters and delays are expressed at. */
    virtual double getSampleRate() const = 0;
};

//==============================================================================
/**  A SOFA file, opened and resampled to the host's rate.

     Wraps libmysofa's "easy" interface, which already does the parts that are
     tedious and easy to get wrong: neighbour lookup on the measurement grid,
     resampling to the target rate, loudness normalisation, and -- the important
     one -- minimum-phase decomposition, which is what separates each impulse
     response into a tone part and a broadband arrival delay.

     Load on a background thread. Opening a full dataset takes hundreds of
     milliseconds and allocates freely.
*/
class HrtfDatabase : public FilterProvider
{
public:
    ~HrtfDatabase() override;

    struct LoadResult
    {
        std::unique_ptr<HrtfDatabase> database;   // null on failure
        std::string message;                      // human readable, always set
        bool ok() const { return database != nullptr; }
    };

    /**  Opens a .sofa file and resamples it to `targetSampleRate`. */
    static LoadResult loadFromFile (const std::string& path, double targetSampleRate);

    bool   getFilter (Vec3 directionInHeadSpace, FilterPair& result) override;
    double getSampleRate() const override { return sampleRate; }

    int getFilterLength() const noexcept { return filterLength; }

private:
    HrtfDatabase() = default;
    HrtfDatabase (const HrtfDatabase&) = delete;

    void* easy = nullptr;          // MYSOFA_EASY*, kept opaque to avoid leaking
    double sampleRate = 0.0;       // the libmysofa header into every TU
    int filterLength = 0;
};

} // namespace juce_hrtf
