cmake_minimum_required(VERSION 3.22)
project(juce_hrtf VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(JUCE_HRTF_BUILD_TESTS   "Build the test binaries"      ON)
option(JUCE_HRTF_BUILD_EXAMPLE "Build the offline renderer"   ON)
option(JUCE_HRTF_BUILD_PLUGIN  "Build the demo audio plugin"  OFF)

#==============================================================================
# libmysofa. Optional on purpose: everything except actually reading a .sofa
# file works without it, so a fresh clone builds and its tests pass before you
# have fetched a single submodule.
#==============================================================================
set(JUCE_HRTF_HAS_LIBMYSOFA 0)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/libmysofa/CMakeLists.txt")
    set(BUILD_TESTS       OFF CACHE BOOL "" FORCE)   # libmysofa's own tests
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/libmysofa EXCLUDE_FROM_ALL)
    set(JUCE_HRTF_HAS_LIBMYSOFA 1)
    message(STATUS "juce_hrtf: libmysofa found, SOFA loading enabled")
else()
    message(STATUS "juce_hrtf: libmysofa absent, SOFA loading disabled. "
                   "Run: git submodule update --init --recursive")
endif()

#==============================================================================
# The DSP core. No JUCE, no platform assumptions.
#==============================================================================
add_library(juce_hrtf_core STATIC
    modules/juce_hrtf/hrtf/HrtfDatabase.cpp
    modules/juce_hrtf/hrtf/BinauralRenderer.cpp)

target_include_directories(juce_hrtf_core PUBLIC modules/juce_hrtf)
target_compile_definitions(juce_hrtf_core PUBLIC JUCE_HRTF_HAS_LIBMYSOFA=${JUCE_HRTF_HAS_LIBMYSOFA})

find_package(Threads REQUIRED)
target_link_libraries(juce_hrtf_core PUBLIC Threads::Threads)

if(JUCE_HRTF_HAS_LIBMYSOFA)
    target_link_libraries(juce_hrtf_core PUBLIC mysofa-static)
endif()

if(MSVC)
    target_compile_options(juce_hrtf_core PRIVATE /W4)
else()
    target_compile_options(juce_hrtf_core PRIVATE -Wall -Wextra)
endif()

#==============================================================================
if(JUCE_HRTF_BUILD_TESTS)
    enable_testing()

    add_executable(test_coordinates tests/TestCoordinates.cpp)
    target_link_libraries(test_coordinates PRIVATE juce_hrtf_core)
    add_test(NAME coordinates COMMAND test_coordinates)

    add_executable(test_renderer tests/TestRenderer.cpp)
    target_link_libraries(test_renderer PRIVATE juce_hrtf_core)
    add_test(NAME renderer COMMAND test_renderer)
endif()

if(JUCE_HRTF_BUILD_EXAMPLE)
    add_executable(render_offline examples/render_offline/Main.cpp)
    target_link_libraries(render_offline PRIVATE juce_hrtf_core)
endif()

#==============================================================================
# Plugin target, off by default so the common path never needs JUCE checked out.
#==============================================================================
if(JUCE_HRTF_BUILD_PLUGIN)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
        message(FATAL_ERROR "JUCE_HRTF_BUILD_PLUGIN is ON but external/JUCE is missing. "
                            "Run: git submodule update --init --recursive")
    endif()

    add_subdirectory(external/JUCE EXCLUDE_FROM_ALL)
    juce_add_module(${CMAKE_CURRENT_SOURCE_DIR}/modules/juce_hrtf)

    # TODO(milestone 3): juce_add_plugin() with a draggable source view.
    message(STATUS "juce_hrtf: plugin target is a stub, see milestone 3 in the README")
endif()
