# ── Shared infrastructure ─────────────────────────────────────────────────────
# All demos share basePage, MenuAction, and the stdafx PCH stub.
# Building them once as a static library avoids recompiling per demo.
add_library(yse_demo_common STATIC
  basePage.cpp
  MenuAction.cpp
  # stdafx.cpp is a UTF-16 LE file (MSVC PCH trigger) that Clang cannot
  # parse. It only contains #include "stdafx.h", which every demo .cpp
  # already includes directly, so nothing is lost by omitting it.
)

target_compile_features(yse_demo_common PRIVATE cxx_std_17)

# Expose the source directory so that generated demo mains can find
# demo headers (e.g. "Demo00_PlaySound.h") and stdafx.h.
target_include_directories(yse_demo_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# PUBLIC: consumers of yse_demo_common also get yse's PUBLIC includes
# (i.e. the YseEngine/ root) and link against libyse.
target_link_libraries(yse_demo_common PUBLIC yse)

# ── Demo builder function ─────────────────────────────────────────────────────
# Creates a standalone demo executable from a single Demo*.cpp source.
# A minimal main() is generated from cmake/demo_main.cpp.in; it calls
# YSE::System().init(), instantiates the demo class, runs it, then closes.
function(add_yse_demo target header_stem class_name)
  set(DEMO_HEADER "${header_stem}.h")
  set(DEMO_CLASS  "${class_name}")
  set(main_out    "${CMAKE_CURRENT_BINARY_DIR}/main_${target}.cpp")
  configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/demo_main.cpp.in"
    "${main_out}"
    @ONLY
  )

  add_executable(${target}
    "${main_out}"
    "${header_stem}.cpp"
  )

  target_compile_features(${target} PRIVATE cxx_std_17)
  target_link_libraries(${target} PRIVATE yse_demo_common)
endfunction()

# ── Demo executables ──────────────────────────────────────────────────────────
add_yse_demo(Demo00 Demo00_PlaySound       DemoPlaySound)
add_yse_demo(Demo01 Demo01_SoundProperties DemoSoundProperties)
add_yse_demo(Demo02 Demo02_3D              Demo3D)
add_yse_demo(Demo03 Demo03_Virtual         DemoVirtual)
add_yse_demo(Demo04 Demo04_Channels        DemoChannels)
add_yse_demo(Demo05 Demo05_Reverb          DemoReverb)
add_yse_demo(Demo06 Demo06_Devices         DemoDevices)
add_yse_demo(Demo07 Demo07_DspSource       DemoDspSource)
add_yse_demo(Demo08 Demo08_Occlusion       DemoOcclusion)
add_yse_demo(Demo09 Demo09_Streaming       DemoStreaming)
add_yse_demo(Demo10 Demo10_FilePosition    DemoFilePosition)
add_yse_demo(Demo11 Demo11_VirtualIO       DemoVirtualIO)
add_yse_demo(Demo12 Demo12_AudioTest       DemoAudioTest)
add_yse_demo(Demo13 Demo13_Patcher         DemoPatcher)
add_yse_demo(Demo14 Demo14_LoadPatcher     DemoLoadPatcher)
add_yse_demo(Demo15 Demo15_RestartAudio    DemoRestartAudio)
add_yse_demo(Test01 Test01_Pitch           Test01_Pitch)

# Demo16 and Demo17 use YSE::midiOut which requires the RtMidi backend.
# RtMidi is now an unconditional dependency on every desktop platform, so
# these are always built.
add_yse_demo(Demo16 Demo16_Midi        DemoMidi)
add_yse_demo(Demo17 Demo17_MidiPatcher DemoMidiPatcher)

# ── Combined Demo executable (all pages via menu) ─────────────────────────────
add_executable(Demo
  demo_main.cpp
  MenuTop.cpp
  MenuBasics.cpp
  MenuDsp.cpp
  MenuOther.cpp
  Demo00_PlaySound.cpp
  Demo01_SoundProperties.cpp
  Demo02_3D.cpp
  Demo03_Virtual.cpp
  Demo04_Channels.cpp
  Demo05_Reverb.cpp
  Demo06_Devices.cpp
  Demo07_DspSource.cpp
  Demo08_Occlusion.cpp
  Demo09_Streaming.cpp
  Demo10_FilePosition.cpp
  Demo11_VirtualIO.cpp
  Demo12_AudioTest.cpp
  Demo13_Patcher.cpp
  Demo14_LoadPatcher.cpp
  Demo15_RestartAudio.cpp
  Demo16_Midi.cpp
  Demo17_MidiPatcher.cpp
  Test01_Pitch.cpp
)
target_compile_features(Demo PRIVATE cxx_std_17)
target_link_libraries(Demo PRIVATE yse_demo_common)
