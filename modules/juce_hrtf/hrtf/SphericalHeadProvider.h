#pragma once

#include "HrtfDatabase.h"

#include <cmath>

namespace juce_hrtf
{

//==============================================================================
/**  A spherical head model: broadband head shadow plus a Woodworth ITD.

     This is not an HRTF. There are no pinna cues, so it will not give you
     elevation or reliable front/back discrimination -- everything sits on a
     ring around your ears. What it does give you is correct left/right and
     correct timing, for free, with no dataset.

     Two uses. It is what the renderer falls back to before the user has loaded
     a file, so a plugin is never silent on first launch. And it is the fixture
     the DSP tests run against, so the render path is covered without a 100 MB
     download in CI.
*/
class SphericalHeadProvider : public FilterProvider
{
public:
    explicit SphericalHeadProvider (double sampleRateToUse) : sampleRate (sampleRateToUse) {}

    bool getFilter (Vec3 directionInHeadSpace, FilterPair& out) override
    {
        const auto d  = directionInHeadSpace.normalised();
        const auto az = std::atan2 (d.y, d.x);          // positive is to the left

        constexpr float headRadius   = 0.0875f;         // metres, roughly average
        constexpr float speedOfSound = 343.0f;

        const auto absAz = std::fabs (az);
        const auto itdSeconds = (headRadius / speedOfSound) * (absAz + std::sin (absAz));
        const auto itdSamples = static_cast<float> (itdSeconds * sampleRate);

        const auto lateral = std::sin (az);             // -1 hard right, +1 hard left

        out.numTaps = 8;
        for (int i = 0; i < out.numTaps; ++i) { out.left[i] = 0.0f; out.right[i] = 0.0f; }

        out.left [0] = 0.75f + 0.25f * lateral;
        out.right[0] = 0.75f - 0.25f * lateral;

        // Only the far ear is delayed, so a frontal source has no ITD at all
        // and the two branches meet continuously at az == 0.
        out.leftDelay  = lateral >= 0.0f ? 0.0f : itdSamples;
        out.rightDelay = lateral >= 0.0f ? itdSamples : 0.0f;

        return true;
    }

    double getSampleRate() const override { return sampleRate; }

private:
    double sampleRate;
};

} // namespace juce_hrtf
