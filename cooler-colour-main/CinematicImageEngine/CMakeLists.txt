cmake_minimum_required(VERSION 3.10)
project(CinematicImageEngine)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Include directories
include_directories(
    ../OpenFX-1.4/include
    ../Support/include
    ../Support/Plugins/include
    ../Support/Library
)

# Support Library Sources
set(SUPPORT_SOURCES
    ../Support/Library/ofxsCore.cpp
    ../Support/Library/ofxsImageEffect.cpp
    ../Support/Library/ofxsInteract.cpp
    ../Support/Library/ofxsLog.cpp
    ../Support/Library/ofxsMultiThread.cpp
    ../Support/Library/ofxsParams.cpp
    ../Support/Library/ofxsProperty.cpp
    ../Support/Library/ofxsPropertyValidation.cpp
)

# Plugin Sources
set(PLUGIN_SOURCES
    CinematicImageEngine.cpp
    CinematicImageEngine.h
    ColorIngestTweaks.h
    FilmResponse.h
    TonalEngine.h
    ColorEnergyEngine.h
    HighlightProtection.h
    SplitToning.h
    FilmGrain.h
    DreamyMist.h
    DreamyBlur.h
    CinematicGlow.h
    Halation.h
    Vignette.h
    Sharpening.h
    AnamorphicStreak.h
    ChromaticAberration.h
    Dither.h
    Utils.h
)

# Build type defaults to Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Aggressive optimization for Release builds
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    add_compile_options(-fvisibility=hidden)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-ffast-math -funroll-loops)
        # Link-Time Optimization for cross-TU inlining
        add_compile_options(-flto)
        add_link_options(-flto)
    endif()
elseif(UNIX)
    add_compile_options(-fvisibility=hidden)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-ffast-math -march=native -funroll-loops)
        add_compile_options(-flto)
        add_link_options(-flto)
    endif()
endif()

# Define the shared library (plugin)
add_library(CinematicImageEngine SHARED ${PLUGIN_SOURCES} ${SUPPORT_SOURCES})

# Platform specific output
if(APPLE)
    set_target_properties(CinematicImageEngine PROPERTIES SUFFIX ".ofx")

    # Bundle structure
    set(BUNDLE_NAME "CinematicImageEngine.ofx.bundle")
    set(BUNDLE_CONTENT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${BUNDLE_NAME}/Contents")
    set(BUNDLE_MACOS_DIR "${BUNDLE_CONTENT_DIR}/MacOS")

    # Generate Info.plist for proper OFX host detection
    file(WRITE "${BUNDLE_CONTENT_DIR}/Info.plist"
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>English</string>
    <key>CFBundleExecutable</key>
    <string>CinematicImageEngine.ofx</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundlePackageType</key>
    <string>BNDL</string>
    <key>CFBundleVersion</key>
    <string>1.4</string>
    <key>CFBundleIdentifier</key>
    <string>com.ColormetricLabs.CinematicImageEngine</string>
</dict>
</plist>
")

    add_custom_command(TARGET CinematicImageEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${BUNDLE_MACOS_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:CinematicImageEngine> ${BUNDLE_MACOS_DIR}/CinematicImageEngine.ofx
    )

elseif(UNIX)
    set_target_properties(CinematicImageEngine PROPERTIES SUFFIX ".ofx")

    # Linux structure
    set(BUNDLE_NAME "CinematicImageEngine.ofx.bundle")
    set(BUNDLE_CONTENT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${BUNDLE_NAME}/Contents")
    set(BUNDLE_LINUX_DIR "${BUNDLE_CONTENT_DIR}/Linux-x86-64")

    add_custom_command(TARGET CinematicImageEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${BUNDLE_LINUX_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:CinematicImageEngine> ${BUNDLE_LINUX_DIR}/CinematicImageEngine.ofx
    )
endif()
