#pragma once

#include <cmath>
#include <algorithm>

namespace juce_hrtf
{

//==============================================================================
/**  COORDINATE CONVENTION  --  read this before touching anything.

     We use the libmysofa cartesian convention throughout, in metres:

         +x  =  in front of the listener's nose
         +y  =  out of the listener's LEFT ear
         +z  =  out of the top of the listener's head

     Angles, where used, follow SOFA spherical:

         azimuth    counter-clockwise from front, so +90 deg is LEFT
         elevation  positive is up

     Head-tracker hardware will almost certainly disagree with this. Convert at
     the boundary, in one place, and never anywhere else. A left/right swap is
     the single most common bug in binaural code and it is not subtle.
*/
struct Vec3
{
    float x = 1.0f, y = 0.0f, z = 0.0f;   // default: straight ahead

    static Vec3 fromSpherical (float azimuthDegrees, float elevationDegrees, float radius = 1.0f)
    {
        const auto az = azimuthDegrees   * 3.14159265358979324f / 180.0f;
        const auto el = elevationDegrees * 3.14159265358979324f / 180.0f;

        return { radius * std::cos (el) * std::cos (az),
                 radius * std::cos (el) * std::sin (az),
                 radius * std::sin (el) };
    }

    float length() const noexcept { return std::sqrt (x * x + y * y + z * z); }

    Vec3 normalised() const noexcept
    {
        const auto len = length();
        return len > 1.0e-9f ? Vec3 { x / len, y / len, z / len } : Vec3 { 1.0f, 0.0f, 0.0f };
    }
};

//==============================================================================
/**  Listener head orientation, in degrees.

     yaw    turning to look left is POSITIVE  (matches azimuth: left is +)
     pitch  tilting to look up is POSITIVE
     roll   tipping the head towards the right shoulder is POSITIVE
*/
struct Orientation
{
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
};

/**  Takes a direction expressed in world space and re-expresses it relative to
     the listener's head. If the listener turns left, a source that was in front
     of them must end up on their right -- that is what makes head tracking feel
     stable rather than glued to the skull.
*/
inline Vec3 worldToHead (Vec3 worldDirection, Orientation head) noexcept
{
    constexpr auto deg = 3.14159265358979324f / 180.0f;

    // Rotating the listener by +yaw is the same as rotating the world by -yaw.
    const auto cy = std::cos (-head.yaw   * deg), sy = std::sin (-head.yaw   * deg);
    const auto cp = std::cos ( head.pitch * deg), sp = std::sin ( head.pitch * deg);
    const auto cr = std::cos (-head.roll  * deg), sr = std::sin (-head.roll  * deg);

    auto v = worldDirection;

    // yaw: rotation about +z (up)
    const auto x1 =  v.x * cy - v.y * sy;
    const auto y1 =  v.x * sy + v.y * cy;
    v = { x1, y1, v.z };

    // pitch: rotation about +y (left ear axis). Nose-up must lower a frontal
    // source, hence the sign pairing below.
    const auto x2 =  v.x * cp + v.z * sp;
    const auto z2 = -v.x * sp + v.z * cp;
    v = { x2, v.y, z2 };

    // roll: rotation about +x (nose)
    const auto y3 =  v.y * cr - v.z * sr;
    const auto z3 =  v.y * sr + v.z * cr;
    v = { v.x, y3, z3 };

    return v;
}

//==============================================================================
/**  One direction's worth of filter, already split into a tone part and a
     timing part.

     This split is the whole point of the module. Two neighbouring measured
     impulse responses disagree slightly about when the sound arrives, so
     blending them raw comb-filters. Pull the arrival time out as a plain
     delay, blend only the tone, then put the delay back.

     Delays are in SAMPLES at the renderer's sample rate, and may be fractional.
*/
struct FilterPair
{
    static constexpr int maxTaps = 512;

    float left  [maxTaps] {};
    float right [maxTaps] {};

    int   numTaps    = 0;
    float leftDelay  = 0.0f;
    float rightDelay = 0.0f;
};

} // namespace juce_hrtf
