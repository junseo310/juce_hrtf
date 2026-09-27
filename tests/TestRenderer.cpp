// Exercises the full render path with a synthetic filter provider, so the DSP
// is under test without needing a 100 MB dataset checked into the repo.

#include "../modules/juce_hrtf/hrtf/BinauralRenderer.h"
#include "../modules/juce_hrtf/hrtf/SphericalHeadProvider.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using namespace juce_hrtf;

static int failures = 0;

static void check (const char* what, bool ok, const char* detail = "")
{
    if (! ok) { ++failures; std::printf ("  FAIL  %-52s %s\n", what, detail); }
    else        std::printf ("  ok    %s\n", what);
}

//==============================================================================
static void renderSilence (BinauralRenderer& r, int numSamples)
{
    std::vector<float> in ((size_t) numSamples, 0.0f);
    std::vector<float> l ((size_t) numSamples, 0.0f), rr ((size_t) numSamples, 0.0f);
    const float* chans[1] = { in.data() };
    r.process (chans, 1, l.data(), rr.data(), numSamples);
}

static int peakIndex (const std::vector<float>& v)
{
    int best = 0;
    for (int i = 1; i < (int) v.size(); ++i)
        if (std::fabs (v[(size_t) i]) > std::fabs (v[(size_t) best]))
            best = i;
    return best;
}

static float rms (const std::vector<float>& v)
{
    double acc = 0.0;
    for (auto s : v) acc += (double) s * s;
    return (float) std::sqrt (acc / (double) v.size());
}

//==============================================================================
int main()
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    BinauralRenderer renderer;
    renderer.prepare (sr, block, 1);
    renderer.setFilterProvider (std::make_shared<SphericalHeadProvider> (sr));

    auto settle = [&] (Vec3 dir)
    {
        renderer.setSourceDirection (0, dir);
        // Let the worker fetch, then let the delay smoothing reach its target.
        std::this_thread::sleep_for (std::chrono::milliseconds (40));
        renderSilence (renderer, (int) (sr * 0.05));
    };

    auto impulseResponse = [&] (std::vector<float>& l, std::vector<float>& r)
    {
        std::vector<float> in ((size_t) block, 0.0f);
        in[0] = 1.0f;
        l.assign ((size_t) block, 0.0f);
        r.assign ((size_t) block, 0.0f);
        const float* chans[1] = { in.data() };
        renderer.process (chans, 1, l.data(), r.data(), block);
    };

    std::printf ("worker handoff\n");
    settle (Vec3::fromSpherical (90.0f, 0.0f));
    check ("worker delivered at least one filter", renderer.getFilterUpdateCount() > 0);

    std::printf ("lateral source, hard left\n");
    {
        std::vector<float> l, r;
        impulseResponse (l, r);

        check ("left ear is louder", rms (l) > rms (r) * 1.2f);
        check ("left ear is earlier", peakIndex (l) < peakIndex (r),
               "ITD sign is inverted -- check the y axis convention");
        check ("ITD is physically plausible (< 1 ms)",
               (peakIndex (r) - peakIndex (l)) < (int) (sr * 0.001));
    }

    std::printf ("lateral source, hard right\n");
    {
        settle (Vec3::fromSpherical (-90.0f, 0.0f));
        std::vector<float> l, r;
        impulseResponse (l, r);

        check ("right ear is louder", rms (r) > rms (l) * 1.2f);
        check ("right ear is earlier", peakIndex (r) < peakIndex (l));
    }

    std::printf ("frontal source\n");
    {
        settle (Vec3::fromSpherical (0.0f, 0.0f));
        std::vector<float> l, r;
        impulseResponse (l, r);

        check ("ears are balanced in front", std::fabs (rms (l) - rms (r)) < 0.01f);
        check ("no ITD in front", peakIndex (l) == peakIndex (r));
    }

    std::printf ("head tracking\n");
    {
        // Source stays put in the world; the listener turns to face it.
        renderer.setSourceDirection (0, Vec3::fromSpherical (90.0f, 0.0f));
        renderer.setListenerOrientation ({ 90.0f, 0.0f, 0.0f });
        std::this_thread::sleep_for (std::chrono::milliseconds (40));
        renderSilence (renderer, (int) (sr * 0.05));

        std::vector<float> l, r;
        impulseResponse (l, r);

        check ("turning to face the source re-centres it", std::fabs (rms (l) - rms (r)) < 0.01f,
               "yaw sign is probably inverted");

        renderer.setListenerOrientation ({ 0.0f, 0.0f, 0.0f });
    }

    std::printf ("continuous motion\n");
    {
        settle (Vec3::fromSpherical (0.0f, 0.0f));

        float phase = 0.0f;
        const auto inc = (float) (2.0 * 3.14159265358979324 * 440.0 / sr);

        float worstJump = 0.0f;
        float prevL = 0.0f;
        bool sawNonFinite = false;

        // Swing the source right around the head over a couple of seconds.
        for (int b = 0; b < 200; ++b)
        {
            const auto az = (float) b * 1.8f;
            renderer.setSourceDirection (0, Vec3::fromSpherical (az, 0.0f));

            std::vector<float> in ((size_t) block), l ((size_t) block, 0.0f), r ((size_t) block, 0.0f);
            for (int i = 0; i < block; ++i) { in[(size_t) i] = std::sin (phase); phase += inc; }

            const float* chans[1] = { in.data() };
            renderer.process (chans, 1, l.data(), r.data(), block);

            for (int i = 0; i < block; ++i)
            {
                const auto s = l[(size_t) i];
                if (! std::isfinite (s)) sawNonFinite = true;
                const auto jump = std::fabs (s - prevL);
                if (b > 2 && jump > worstJump) worstJump = jump;
                prevL = s;
            }
        }

        // A clean 440 Hz sine at unity moves by at most ~0.058 per sample at
        // 48 kHz. Anything much beyond that is a discontinuity, i.e. a click.
        char detail[96];
        std::snprintf (detail, sizeof (detail), "worst sample-to-sample jump was %.4f", worstJump);

        check ("output stays finite", ! sawNonFinite);
        check ("no clicks while the source moves", worstJump < 0.15f, detail);
    }

    std::printf (failures == 0 ? "\nall renderer tests passed\n"
                               : "\n%d renderer test(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
