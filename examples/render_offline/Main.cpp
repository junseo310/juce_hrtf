// Renders a source orbiting the listener, straight to a WAV file.
//
//   render_offline out.wav [hrtf.sofa]
//
// With no .sofa argument it uses the built-in spherical head model, so this
// builds and runs the moment you clone the repo. WAV writing is done by hand
// rather than through JUCE, to keep the example dependency-free.

#include "../../modules/juce_hrtf/hrtf/BinauralRenderer.h"
#include "../../modules/juce_hrtf/hrtf/SphericalHeadProvider.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace juce_hrtf;

namespace
{
    void writeU32 (std::FILE* f, unsigned int v) { std::fwrite (&v, 4, 1, f); }
    void writeU16 (std::FILE* f, unsigned short v) { std::fwrite (&v, 2, 1, f); }

    bool writeWav16 (const std::string& path,
                     const std::vector<float>& left,
                     const std::vector<float>& right,
                     int sampleRate)
    {
        auto* f = std::fopen (path.c_str(), "wb");
        if (f == nullptr) return false;

        const auto frames = (unsigned int) left.size();
        const auto dataBytes = frames * 2u * 2u;

        std::fwrite ("RIFF", 1, 4, f); writeU32 (f, 36 + dataBytes);
        std::fwrite ("WAVE", 1, 4, f);
        std::fwrite ("fmt ", 1, 4, f); writeU32 (f, 16);
        writeU16 (f, 1); writeU16 (f, 2);
        writeU32 (f, (unsigned int) sampleRate);
        writeU32 (f, (unsigned int) sampleRate * 4u);
        writeU16 (f, 4); writeU16 (f, 16);
        std::fwrite ("data", 1, 4, f); writeU32 (f, dataBytes);

        for (size_t i = 0; i < left.size(); ++i)
        {
            auto clamp = [] (float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); };
            const short l = (short) (clamp (left[i])  * 32767.0f);
            const short r = (short) (clamp (right[i]) * 32767.0f);
            std::fwrite (&l, 2, 1, f);
            std::fwrite (&r, 2, 1, f);
        }

        std::fclose (f);
        return true;
    }
}

int main (int argc, char** argv)
{
    const std::string outPath = argc > 1 ? argv[1] : "orbit.wav";
    const std::string sofaPath = argc > 2 ? argv[2] : "";

    constexpr double sr = 48000.0;
    constexpr int block = 256;
    constexpr double durationSeconds = 8.0;

    BinauralRenderer renderer;
    renderer.prepare (sr, block, 1);

    std::shared_ptr<FilterProvider> provider;

    if (! sofaPath.empty())
    {
        auto result = HrtfDatabase::loadFromFile (sofaPath, sr);
        std::printf ("%s\n", result.message.c_str());

        if (! result.ok())
        {
            std::printf ("Falling back to the spherical head model.\n");
        }
        else
        {
            provider = std::shared_ptr<FilterProvider> (result.database.release());
        }
    }

    if (provider == nullptr)
    {
        std::printf ("Using the built-in spherical head model (no pinna cues, "
                     "so expect a flat ring rather than real elevation).\n");
        provider = std::make_shared<SphericalHeadProvider> (sr);
    }

    renderer.setFilterProvider (provider);

    const auto totalSamples = (int) (sr * durationSeconds);
    std::vector<float> outL, outR;
    outL.reserve ((size_t) totalSamples);
    outR.reserve ((size_t) totalSamples);

    // A short repeating click train reads much more clearly than a sustained
    // tone -- transients are what actually carry localisation cues.
    int   sampleCounter = 0;
    float noiseState = 0.0f;
    unsigned int rngState = 22222u;

    for (int pos = 0; pos < totalSamples; pos += block)
    {
        const auto n = (pos + block <= totalSamples) ? block : (totalSamples - pos);
        const auto seconds = (double) pos / sr;

        // One full lap, at ear height, over the length of the file.
        const auto azimuth = (float) (360.0 * seconds / durationSeconds);
        renderer.setSourceDirection (0, Vec3::fromSpherical (azimuth, 0.0f));

        std::vector<float> in ((size_t) n, 0.0f);

        for (int i = 0; i < n; ++i)
        {
            const auto inBurst = (sampleCounter % (int) (sr * 0.25)) < (int) (sr * 0.02);

            rngState = rngState * 1664525u + 1013904223u;
            const auto white = ((float) (rngState >> 8) / 8388608.0f) - 1.0f;
            noiseState = 0.7f * noiseState + 0.3f * white;   // gently darkened

            in[(size_t) i] = inBurst ? noiseState * 0.4f : 0.0f;
            ++sampleCounter;
        }

        std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
        const float* chans[1] = { in.data() };
        renderer.process (chans, 1, l.data(), r.data(), n);

        outL.insert (outL.end(), l.begin(), l.end());
        outR.insert (outR.end(), r.begin(), r.end());

        // The worker thread runs on wall-clock time, so an offline render that
        // outpaces it would only ever see one filter. Pace it to real time.
        std::this_thread::sleep_for (std::chrono::microseconds ((long) (1000000.0 * n / sr)));
    }

    if (! writeWav16 (outPath, outL, outR, (int) sr))
    {
        std::printf ("Could not write %s\n", outPath.c_str());
        return 1;
    }

    std::printf ("Wrote %s (%.1f s, %d filter updates)\n",
                 outPath.c_str(), durationSeconds, renderer.getFilterUpdateCount());
    return 0;
}
