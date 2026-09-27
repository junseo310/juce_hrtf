#include "HrtfDatabase.h"

#if JUCE_HRTF_HAS_LIBMYSOFA
 #include <mysofa.h>
#endif

#include <vector>

namespace juce_hrtf
{

HrtfDatabase::~HrtfDatabase()
{
   #if JUCE_HRTF_HAS_LIBMYSOFA
    if (easy != nullptr)
        mysofa_close (static_cast<MYSOFA_EASY*> (easy));
   #endif
}

HrtfDatabase::LoadResult HrtfDatabase::loadFromFile ([[maybe_unused]] const std::string& path,
                                                     [[maybe_unused]] double targetSampleRate)
{
   #if ! JUCE_HRTF_HAS_LIBMYSOFA
    return { nullptr, "Built without libmysofa. Run: git submodule update --init --recursive" };
   #else
    if (targetSampleRate <= 0.0)
        return { nullptr, "Invalid target sample rate" };

    int filterLength = 0, err = 0;

    auto* easy = mysofa_open (path.c_str(),
                              static_cast<float> (targetSampleRate),
                              &filterLength,
                              &err);

    if (easy == nullptr)
        return { nullptr, "libmysofa could not open the file (error " + std::to_string (err) + ")" };

    // The renderer uses fixed-size filter storage so the audio thread never
    // allocates. A dataset longer than that is a real, actionable error rather
    // than something to silently truncate.
    if (filterLength > FilterPair::maxTaps)
    {
        mysofa_close (easy);
        return { nullptr, "Impulse responses are " + std::to_string (filterLength)
                          + " taps, which exceeds FilterPair::maxTaps ("
                          + std::to_string (FilterPair::maxTaps) + ")" };
    }

    std::unique_ptr<HrtfDatabase> db { new HrtfDatabase() };
    db->easy = easy;
    db->sampleRate = targetSampleRate;
    db->filterLength = filterLength;

    return { std::move (db), "Loaded " + std::to_string (filterLength) + " taps at "
                             + std::to_string (static_cast<int> (targetSampleRate)) + " Hz" };
   #endif
}

bool HrtfDatabase::getFilter ([[maybe_unused]] Vec3 direction, [[maybe_unused]] FilterPair& result)
{
   #if ! JUCE_HRTF_HAS_LIBMYSOFA
    return false;
   #else
    if (easy == nullptr)
        return false;

    const auto dir = direction.normalised();

    float leftSeconds = 0.0f, rightSeconds = 0.0f;

    mysofa_getfilter_float (static_cast<MYSOFA_EASY*> (easy),
                            dir.x, dir.y, dir.z,
                            result.left, result.right,
                            &leftSeconds, &rightSeconds);

    result.numTaps = filterLength;

    // libmysofa reports arrival times in seconds; the renderer wants samples,
    // and wants them fractional so that motion stays smooth between grid points.
    result.leftDelay  = leftSeconds  * static_cast<float> (sampleRate);
    result.rightDelay = rightSeconds * static_cast<float> (sampleRate);

    return true;
   #endif
}

} // namespace juce_hrtf
