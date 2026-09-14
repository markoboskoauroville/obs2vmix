# obs2vmix: the FX rack hosts VST 2.x plugins with obs-vst's host class, built
# here as a static library (its sources live under plugins/obs-vst), plus the
# master chain, the rack UI and the MIDI input layer.

set(_vst_dir "${CMAKE_SOURCE_DIR}/plugins/obs-vst")

add_library(obs2vmix-vst-host STATIC)
add_library(OBS::obs2vmix-vst-host ALIAS obs2vmix-vst-host)

target_sources(
  obs2vmix-vst-host
  PRIVATE
    "${_vst_dir}/VSTPlugin.cpp"
    "${_vst_dir}/EditorWidget.cpp"
    "${_vst_dir}/headers/VSTPlugin.h"
    "${_vst_dir}/headers/EditorWidget.h"
    "${_vst_dir}/headers/vst-plugin-callbacks.hpp"
    "${_vst_dir}/vst_header/aeffectx.h"
    $<$<PLATFORM_ID:Darwin>:${_vst_dir}/mac/EditorWidget-osx.mm>
    $<$<PLATFORM_ID:Darwin>:${_vst_dir}/mac/VSTPlugin-osx.mm>
    $<$<PLATFORM_ID:Linux,FreeBSD,OpenBSD>:${_vst_dir}/linux/EditorWidget-linux.cpp>
    $<$<PLATFORM_ID:Linux,FreeBSD,OpenBSD>:${_vst_dir}/linux/VSTPlugin-linux.cpp>
    $<$<PLATFORM_ID:Windows>:${_vst_dir}/win/EditorWidget-win.cpp>
    $<$<PLATFORM_ID:Windows>:${_vst_dir}/win/VSTPlugin-win.cpp>
)

target_include_directories(obs2vmix-vst-host PUBLIC "${_vst_dir}/headers" "${_vst_dir}/vst_header")

target_link_libraries(
  obs2vmix-vst-host
  PUBLIC OBS::libobs Qt::Widgets
  PRIVATE
    "$<$<PLATFORM_ID:Darwin>:$<LINK_LIBRARY:FRAMEWORK,Cocoa.framework>>"
    "$<$<PLATFORM_ID:Darwin>:$<LINK_LIBRARY:FRAMEWORK,Foundation.framework>>"
)

set_target_properties(obs2vmix-vst-host PROPERTIES FOLDER frontend AUTOMOC ON POSITION_INDEPENDENT_CODE ON)

target_link_libraries(obs-studio PRIVATE OBS::obs2vmix-vst-host)

target_sources(
  obs-studio
  PRIVATE
    components/FxRack.cpp
    components/FxRack.hpp
    utility/MasterChain.cpp
    utility/MasterChain.hpp
    utility/MidiIn.cpp
    utility/MidiIn.hpp
    utility/Obs2vmixUpdate.cpp
    utility/Obs2vmixUpdate.hpp
)

if(OS_MACOS)
  target_sources(obs-studio PRIVATE utility/MidiIn_macOS.mm)
  target_link_libraries(
    obs-studio
    PRIVATE "$<LINK_LIBRARY:FRAMEWORK,CoreMIDI.framework>" "$<LINK_LIBRARY:FRAMEWORK,CoreFoundation.framework>"
  )
elseif(OS_WINDOWS)
  target_sources(obs-studio PRIVATE utility/MidiIn_Windows.cpp)
  target_link_libraries(obs-studio PRIVATE winmm)
else()
  target_sources(obs-studio PRIVATE utility/MidiIn_Linux.cpp)
  find_package(ALSA QUIET)
  if(ALSA_FOUND)
    target_link_libraries(obs-studio PRIVATE ALSA::ALSA)
    target_compile_definitions(obs-studio PRIVATE OBS2VMIX_HAVE_ALSA)
  else()
    message(STATUS "obs2vmix: ALSA not found, the FX rack builds without MIDI input")
  endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Network)
target_link_libraries(obs-studio PRIVATE Qt::Network)
