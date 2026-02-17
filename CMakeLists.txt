##############################################################################
# CMakeLists.txt — Build configuration for Kawaii K50000SV
##############################################################################
#
# This file tells CMake how to build our VST3 plugin. CMake is a build system
# generator — it reads this file and produces platform-specific build files
# (Makefiles, Xcode projects, etc.).
#
# BUILD INSTRUCTIONS:
#   mkdir build && cd build
#   cmake .. -DCMAKE_BUILD_TYPE=Release
#   cmake --build . --config Release
#
# The result will be:
#   build/VST3/KawaiiK50000SV.vst3  (a macOS bundle ready to use)
#
# INSTALLATION:
#   cp -r build/VST3/KawaiiK50000SV.vst3 ~/Library/Audio/Plug-Ins/VST3/
#
##############################################################################

# Minimum CMake version. 3.25 is required by VSTGUI.
cmake_minimum_required(VERSION 3.25)

# Project metadata. VERSION is used in the bundle's Info.plist.
# LANGUAGES CXX OBJCXX — Objective-C++ is required by VSTGUI on macOS.
project(KawaiiK50000SV VERSION 1.0.0 LANGUAGES CXX OBJCXX)

# We use C++17 features (std::clamp, std::optional, structured bindings, etc.)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

##############################################################################
# VST3 SDK
##############################################################################
# The VST3 SDK is Steinberg's open-source framework for building VST3 plugins.
# It must be cloned as a Git submodule into the vst3sdk/ directory.

set(VST3_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vst3sdk")

if(NOT EXISTS ${VST3_SDK_ROOT})
    message(FATAL_ERROR
        "VST3 SDK not found at ${VST3_SDK_ROOT}. Please clone it:\n"
        "  cd ${CMAKE_CURRENT_SOURCE_DIR}\n"
        "  git clone --recursive https://github.com/steinbergmedia/vst3sdk.git"
    )
endif()

# Add the SDK root to the include path so we can #include its headers
# with paths like "pluginterfaces/base/funknown.h".
include_directories(${VST3_SDK_ROOT})

##############################################################################
# VSTGUI — Steinberg's UI framework (for custom editor)
##############################################################################
# Disable standalone/tools/tests — we only need the core vstgui library.
set(VSTGUI_STANDALONE OFF CACHE BOOL "" FORCE)
set(VSTGUI_STANDALONE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VSTGUI_TOOLS OFF CACHE BOOL "" FORCE)
set(VSTGUI_DISABLE_UNITTESTS 1)

add_subdirectory(${VST3_SDK_ROOT}/vstgui4/vstgui ${CMAKE_BINARY_DIR}/vstgui)

include_directories(${VST3_SDK_ROOT}/vstgui4)

##############################################################################
# VST3 SDK Source Files
##############################################################################
# The VST3 SDK is a source-level dependency — we compile the parts we need
# directly into our plugin binary. This avoids DLL/dylib dependencies.
#
# There are three layers:
#   1. BASE — Core C++ utilities (strings, streams, threading)
#   2. PLUGINTERFACES — Abstract interfaces (IComponent, IAudioProcessor, etc.)
#   3. PUBLIC.SDK — Concrete base classes we inherit from (AudioEffect, EditController)

# Layer 1: Base utilities
set(VST3_BASE_SOURCES
    ${VST3_SDK_ROOT}/base/source/baseiids.cpp       # Interface ID definitions
    ${VST3_SDK_ROOT}/base/source/fbuffer.cpp         # Buffer utility
    ${VST3_SDK_ROOT}/base/source/fdebug.cpp          # Debug utilities
    ${VST3_SDK_ROOT}/base/source/fobject.cpp         # Base object with ref counting
    ${VST3_SDK_ROOT}/base/source/fstreamer.cpp       # Stream reading/writing
    ${VST3_SDK_ROOT}/base/source/fstring.cpp         # String utilities
    ${VST3_SDK_ROOT}/base/source/updatehandler.cpp   # Change notification system
    ${VST3_SDK_ROOT}/base/thread/source/fcondition.cpp  # Condition variables
    ${VST3_SDK_ROOT}/base/thread/source/flock.cpp       # Mutexes/locks
)

# Layer 2: Plugin interfaces (abstract types)
set(VST3_PLUGINTERFACES_SOURCES
    ${VST3_SDK_ROOT}/pluginterfaces/base/conststringtable.cpp  # String table for IDs
    ${VST3_SDK_ROOT}/pluginterfaces/base/coreiids.cpp          # Core interface IDs
    ${VST3_SDK_ROOT}/pluginterfaces/base/funknown.cpp          # IUnknown (COM base)
    ${VST3_SDK_ROOT}/pluginterfaces/base/ustring.cpp           # Unicode string
)

# Layer 3: Public SDK (concrete classes we inherit from)
set(VST3_SDK_SOURCES
    ${VST3_SDK_ROOT}/public.sdk/source/main/pluginfactory.cpp      # Plugin factory
    ${VST3_SDK_ROOT}/public.sdk/source/main/macmain.cpp            # macOS entry point
    ${VST3_SDK_ROOT}/public.sdk/source/common/commoniids.cpp       # Common IIDs
    ${VST3_SDK_ROOT}/public.sdk/source/common/pluginview.cpp       # Base IPlugView
    ${VST3_SDK_ROOT}/public.sdk/source/vst/vstcomponentbase.cpp    # ComponentBase
    ${VST3_SDK_ROOT}/public.sdk/source/vst/vstcomponent.cpp        # Component
    ${VST3_SDK_ROOT}/public.sdk/source/vst/vstaudioeffect.cpp      # AudioEffect (our base)
    ${VST3_SDK_ROOT}/public.sdk/source/vst/vstbus.cpp              # Audio/Event bus
    ${VST3_SDK_ROOT}/public.sdk/source/vst/vsteditcontroller.cpp   # EditController (our base)
    ${VST3_SDK_ROOT}/public.sdk/source/vst/vstparameters.cpp       # Parameter management
    ${VST3_SDK_ROOT}/public.sdk/source/vst/vstinitiids.cpp         # VST interface IDs
    ${VST3_SDK_ROOT}/public.sdk/source/vst/vstguieditor.cpp        # VSTGUIEditor base class
    ${VST3_SDK_ROOT}/public.sdk/source/main/moduleinit.cpp         # Module init (required by vstguieditor)
)

##############################################################################
# Our Plugin Source Files
##############################################################################

set(PLUGIN_SOURCES
    source/entry/KawaiiEntry.cpp                # Plugin factory / entry point
    source/processor/KawaiiProcessor.cpp        # Audio processor
    source/controller/KawaiiController.cpp      # Parameter controller
    source/editor/KawaiiEditor.cpp              # Custom VSTGUI editor
    source/gpu/MetalSineBank.mm                 # Metal GPU compute for additive synthesis
)

##############################################################################
# Build Target
##############################################################################
# MODULE creates a dynamically loaded library (a .bundle on macOS).
# This is what the DAW loads when it scans for plugins.

set(ALL_SOURCES
    ${VST3_BASE_SOURCES}
    ${VST3_PLUGINTERFACES_SOURCES}
    ${VST3_SDK_SOURCES}
    ${PLUGIN_SOURCES}
)

add_library(KawaiiK50000SV MODULE ${ALL_SOURCES})

# Add our source directories to the include path so files can include
# each other with relative paths like "../entry/KawaiiCids.h".
target_include_directories(KawaiiK50000SV PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
)

##############################################################################
# macOS Bundle Properties
##############################################################################
# On macOS, a VST3 plugin is a "bundle" — a directory with a specific structure
# that the OS treats as a single file. These properties control the bundle's
# metadata (shown in Finder's "Get Info" and used by DAWs to identify the plugin).

set_target_properties(KawaiiK50000SV PROPERTIES
    BUNDLE TRUE                                                    # Make it a bundle
    BUNDLE_EXTENSION "vst3"                                        # File extension
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.xenonbug.kawaiiK50000SV"   # Reverse-DNS identifier
    MACOSX_BUNDLE_BUNDLE_NAME "Kawaii K50000SV"                   # Display name
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"                          # Build version
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"                    # Marketing version
    XCODE_ATTRIBUTE_WRAPPER_EXTENSION "vst3"                      # For Xcode builds
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/VST3"           # Output location
)

# Fix the Info.plist so macOS treats the .vst3 as a proper bundle (single file
# in Finder) instead of showing it as a folder. CFBundlePackageType must be
# "BNDL" for plugin bundles, not "APPL" (which is for applications).
add_custom_command(TARGET KawaiiK50000SV POST_BUILD
    COMMAND /usr/libexec/PlistBuddy -c "Set :CFBundlePackageType BNDL"
        "$<TARGET_BUNDLE_DIR:KawaiiK50000SV>/Contents/Info.plist"
)

##############################################################################
# Platform-Specific Settings (macOS)
##############################################################################

if(APPLE)
    # Tell the VST3 SDK we're on macOS (it uses this for platform-specific code).
    target_compile_definitions(KawaiiK50000SV PRIVATE
        SMTG_OS_MACOS=1
        RELEASE=1
    )

    # Link against macOS system frameworks.
    # CoreFoundation: Core C-based utilities (strings, URLs, etc.)
    # Foundation: Objective-C base framework
    # Cocoa: UI framework (needed by VST3 SDK even without a custom GUI)
    # AudioToolbox: Our audio file loading (ExtAudioFile API)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    find_library(FOUNDATION_LIBRARY Foundation)
    find_library(COCOA_LIBRARY Cocoa)
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
    find_library(QUARTZCORE_LIBRARY QuartzCore)
    find_library(ACCELERATE_LIBRARY Accelerate)
    find_library(METAL_LIBRARY Metal)

    target_link_libraries(KawaiiK50000SV PRIVATE
        ${COREFOUNDATION_LIBRARY}
        ${FOUNDATION_LIBRARY}
        ${COCOA_LIBRARY}
        ${AUDIOTOOLBOX_LIBRARY}
        ${QUARTZCORE_LIBRARY}
        ${ACCELERATE_LIBRARY}
        ${METAL_LIBRARY}
        vstgui
    )
endif()

##############################################################################
# Post-Build: Create Bundle Structure
##############################################################################
# VST3 bundles need a Resources directory inside Contents/.
# This command creates it after the build finishes.

add_custom_command(TARGET KawaiiK50000SV POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_BUNDLE_DIR:KawaiiK50000SV>/Contents/Resources"
    COMMAND /usr/bin/SetFile -a B "$<TARGET_BUNDLE_DIR:KawaiiK50000SV>"
)

##############################################################################
# Installation
##############################################################################
# "cmake --install ." copies the built plugin to the system VST3 folder.
# DAWs scan this folder to find available plugins.

install(TARGETS KawaiiK50000SV
    DESTINATION "/Library/Audio/Plug-Ins/VST3"
)
