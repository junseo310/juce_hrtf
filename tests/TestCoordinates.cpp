// Deliberately depends on nothing but the standard library, so it compiles and
// runs in CI long before JUCE or libmysofa are in the picture.

#include "../modules/juce_hrtf/hrtf/HrtfTypes.h"

#include <cstdio>
#include <cmath>

using namespace juce_hrtf;

static int failures = 0;

static void expectVec (const char* what, Vec3 got, float x, float y, float z)
{
    const auto ok = std::fabs (got.x - x) < 1.0e-4f
                 && std::fabs (got.y - y) < 1.0e-4f
                 && std::fabs (got.z - z) < 1.0e-4f;

    if (! ok)
    {
        ++failures;
        std::printf ("  FAIL  %-52s got (%+.3f %+.3f %+.3f) want (%+.3f %+.3f %+.3f)\n",
                     what, got.x, got.y, got.z, x, y, z);
    }
    else
    {
        std::printf ("  ok    %s\n", what);
    }
}

int main()
{
    std::printf ("spherical -> cartesian\n");
    expectVec ("front  (az 0,   el 0)",    Vec3::fromSpherical (0.0f,   0.0f),  1, 0, 0);
    expectVec ("left   (az 90,  el 0)",    Vec3::fromSpherical (90.0f,  0.0f),  0, 1, 0);
    expectVec ("right  (az -90, el 0)",    Vec3::fromSpherical (-90.0f, 0.0f),  0,-1, 0);
    expectVec ("behind (az 180, el 0)",    Vec3::fromSpherical (180.0f, 0.0f), -1, 0, 0);
    expectVec ("above  (az 0,   el 90)",   Vec3::fromSpherical (0.0f,  90.0f),  0, 0, 1);

    std::printf ("head rotation\n");
    const Vec3 front { 1.0f, 0.0f, 0.0f };
    const Vec3 above { 0.0f, 0.0f, 1.0f };
    const Vec3 left  { 0.0f, 1.0f, 0.0f };

    expectVec ("no rotation leaves a frontal source alone",
               worldToHead (front, { 0.0f, 0.0f, 0.0f }), 1, 0, 0);

    // Turn to look left; whatever was in front is now off your right ear.
    expectVec ("yaw left 90 puts a frontal source on the right",
               worldToHead (front, { 90.0f, 0.0f, 0.0f }), 0, -1, 0);

    expectVec ("yaw right 90 puts a frontal source on the left",
               worldToHead (front, { -90.0f, 0.0f, 0.0f }), 0, 1, 0);

    // Look up at the ceiling; the thing in front of you is now below your nose.
    expectVec ("pitch up 90 puts a frontal source below",
               worldToHead (front, { 0.0f, 90.0f, 0.0f }), 0, 0, -1);

    expectVec ("pitch up 90 puts an overhead source in front",
               worldToHead (above, { 0.0f, 90.0f, 0.0f }), 1, 0, 0);

    // Tip your head towards your right shoulder; the ceiling swings left.
    expectVec ("roll right 90 puts an overhead source at the left ear",
               worldToHead (above, { 0.0f, 0.0f, 90.0f }), 0, 1, 0);

    expectVec ("roll right 90 puts a left source overhead",
               worldToHead (left, { 0.0f, 0.0f, 90.0f }), 0, 0, -1);

    // Full turn is identity.
    expectVec ("yaw 360 is a no-op",
               worldToHead (front, { 360.0f, 0.0f, 0.0f }), 1, 0, 0);

    std::printf (failures == 0 ? "\nall coordinate tests passed\n"
                               : "\n%d coordinate test(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
