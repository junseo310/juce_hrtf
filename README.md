# juce_hrtf

SOFA HRTF loading and binaural rendering for JUCE — with the part everyone skips: **sources that move without gargling.**

Loading a SOFA file is easy. Switching between measured impulse responses while a source moves is not, because neighbouring measurements disagree about when the sound arrives, and blending them raw comb-filters. This module pulls the arrival time out of each response, blends only the tone, and puts the timing back as a smoothed fractional delay.

Status: **early.** The DSP core, tests, and offline renderer work. The plugin target is a stub. See milestones below.

---

## Quickstart

```bash
git clone https://github.com/YOURNAME/juce_hrtf
cd juce_hrtf
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

./build/render_offline orbit.wav          # listen on headphones
```

That works on a fresh clone with no submodules, using a built-in spherical head model. For real measured HRTFs:

```bash
git submodule update --init --recursive   # libmysofa
./scripts/fetch-hrtfs.sh                  # SADIE II KU100
cmake -B build && cmake --build build
./build/render_offline orbit.wav hrtfs/SADIE_KU100.sofa
```

## Using it

```cpp
#include <juce_hrtf/juce_hrtf.h>

using namespace juce_hrtf;

BinauralRenderer renderer;
renderer.prepare (48000.0, 512, /*numSources*/ 4);

auto loaded = HrtfDatabase::loadFromFile ("SADIE_KU100.sofa", 48000.0);
if (loaded.ok())
    renderer.setFilterProvider (std::shared_ptr<FilterProvider> (loaded.database.release()));

// Realtime safe, call as often as you like:
renderer.setSourceDirection (0, Vec3::fromSpherical (/*az*/ 45.0f, /*el*/ 10.0f));
renderer.setListenerOrientation ({ yaw, pitch, roll });

// In processBlock:
renderer.process (sourcePointers, numSources, outL, outR, numSamples);
```

## How it's put together

Three layers, so you can take only the piece you want.

| | |
|---|---|
| `FilterProvider` | Abstract: direction in, filter pair out. Not realtime safe. |
| `HrtfDatabase` | A SOFA file, via libmysofa. Resampled and loudness-normalised at load. |
| `SphericalHeadProvider` | Head shadow + Woodworth ITD. No dataset needed. The fallback, and the test fixture. |
| `BinauralRenderer` | FIR pair per source, crossfade, fractional ITD delay, worker-thread filter fetching. |

**Threading.** Fetching a filter walks a lookup structure and interpolates, so it never runs on the audio thread. Directions are published as atomics, a worker prepares filter pairs, and the audio thread collects finished work through a two-slot handoff. Nothing on the audio path allocates, locks, or calls into libmysofa. The cost is a few milliseconds between moving a source and hearing it move — well under the threshold where anyone notices.

**The core does not depend on JUCE.** `modules/juce_hrtf/hrtf/` builds standalone. The module wrapper exists so it drops into a JUCE CMake target, nothing more.

## Coordinate convention

Stated once, here, and enforced by `tests/TestCoordinates.cpp`:

```
+x  in front of the nose
+y  out of the LEFT ear
+z  out of the top of the head

azimuth    counter-clockwise from front, so +90° is LEFT
elevation  positive is up
yaw        turning to look left is positive
```

Your head tracker will disagree. Convert at the boundary, in one place. A left/right swap is the most common bug in this domain and it is not subtle.

## Milestones

- [x] **1.** Builds and tests green on Linux, macOS, Windows
- [x] **2.** Offline render, correct ITD sign, click-free motion under test
- [ ] **3.** Demo plugin with a draggable source view
- [ ] **4.** Direct grid lookup instead of libmysofa's easy API, for control over interpolation order
- [ ] **5.** OSC head tracking, multi-source stress test, tagged binaries

## Licence

MIT. libmysofa is BSD-3-Clause and vendors cleanly. JUCE has its own terms and is only needed for the plugin target — the core and the offline example don't touch it.

## Credits

[libmysofa](https://github.com/hoene/libmysofa) by Christian Hoene does the heavy lifting on file reading and the minimum-phase decomposition. The [SADIE II](https://www.york.ac.uk/sadie-project/) database is the default test dataset.
