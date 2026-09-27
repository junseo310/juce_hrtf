/*******************************************************************************
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 juce_hrtf
  vendor:             juce_hrtf
  version:            0.1.0
  name:               SOFA HRTF loading and binaural rendering
  description:        Loads SOFA HRTF datasets and renders moving sources to a binaural stereo pair, with time-aligned filter interpolation.
  website:            https://github.com/YOURNAME/juce_hrtf
  license:            MIT
  minimumCppStandard: 17

  dependencies:       juce_audio_basics

 END_JUCE_MODULE_DECLARATION
*******************************************************************************/

#pragma once

#define JUCE_HRTF_H_INCLUDED

/**  The DSP core deliberately does not depend on JUCE. You can lift the hrtf/
     folder into a non-JUCE project and it will build unchanged; this module
     wrapper only exists so it drops cleanly into a JUCE CMake target.
*/
#include "hrtf/HrtfTypes.h"
#include "hrtf/HrtfDatabase.h"
#include "hrtf/SphericalHeadProvider.h"
#include "hrtf/BinauralRenderer.h"
