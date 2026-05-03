# ── Engine source files ───────────────────────────────────────────────────────
set(YSE_SRCS
  BufferIO.cpp
  io.cpp
  listener.cpp
  log.cpp
  system.cpp
  channel/channelImplementation.cpp
  channel/channelInterface.cpp
  channel/channelManager.cpp
  device/androidDeviceManager.cpp
  device/deviceInterface.cpp
  device/deviceManager.cpp
  device/deviceSetup.cpp
  device/juceDeviceManager.cpp
  device/OpenSL.cpp
  device/OpenSLImplementation.cpp
  device/portaudioDeviceManager.cpp
  dsp/ADSRenvelope.cpp
  dsp/buffer.cpp
  dsp/delay.cpp
  dsp/drawableBuffer.cpp
  dsp/dspObject.cpp
  dsp/envelope.cpp
  dsp/fileBuffer.cpp
  dsp/filters.cpp
  dsp/fourier/fft.cpp
  dsp/fourier/mayer.cpp
  dsp/interpolate4.cpp
  dsp/lfo.cpp
  dsp/math.cpp
  dsp/math_functions.cpp
  dsp/modules/delay/basicDelay.cpp
  dsp/modules/delay/highpassDelay.cpp
  dsp/modules/delay/lowpassDelay.cpp
  dsp/modules/filters/bandpass.cpp
  dsp/modules/filters/highpass.cpp
  dsp/modules/filters/lowpass.cpp
  dsp/modules/filters/sweep.cpp
  dsp/modules/fm/difference.cpp
  dsp/modules/granulator.cpp
  dsp/modules/hilbert.cpp
  dsp/modules/phaser.cpp
  dsp/modules/ringModulator.cpp
  dsp/modules/sineWave.cpp
  dsp/oscillators.cpp
  dsp/ramp.cpp
  dsp/rawFilters.cpp
  dsp/sample_functions.cpp
  dsp/wavetable.cpp
  implementations/listenerImplementation.cpp
  implementations/logImplementation.cpp
  internal/abstractSoundFile.cpp
  internal/customFileReader.cpp
  internal/global.cpp
  internal/juceSoundFile.cpp
  internal/lsfSoundfile.cpp
  internal/reverbDSP.cpp
  internal/settings.cpp
  internal/thread.cpp
  internal/threadPool.cpp
  internal/time.cpp
  internal/underWaterEffect.cpp
  internal/virtualFinder.cpp
  json/cJSON.cpp
  midi/midifile.cpp
  midi/midifileImplementation.cpp
  midi/midifileManager.cpp
  music/chord.cpp
  music/note.cpp
  music/pNote.cpp
  music/motif/motifImplementation.cpp
  music/motif/motifInterface.cpp
  music/motif/motifManager.cpp
  music/scale/scaleImplementation.cpp
  music/scale/scaleInterface.cpp
  music/scale/scaleManager.cpp
  patcher/filters/dVcf.cpp
  patcher/filters/pBandpass.cpp
  patcher/filters/pHighpass.cpp
  patcher/filters/pLowpass.cpp
  patcher/generatorObjects/dNoise.cpp
  patcher/generatorObjects/dSaw.cpp
  patcher/generatorObjects/pSine.cpp
  patcher/genericObjects/gGate.cpp
  patcher/genericObjects/gReceive.cpp
  patcher/genericObjects/gRoute.cpp
  patcher/genericObjects/gSend.cpp
  patcher/genericObjects/gSwitch.cpp
  patcher/genericObjects/pDac.cpp
  patcher/genericObjects/pLine.cpp
  patcher/guiObjects/gButton.cpp
  patcher/guiObjects/gFloat.cpp
  patcher/guiObjects/gInt.cpp
  patcher/guiObjects/gList.cpp
  patcher/guiObjects/gMessage.cpp
  patcher/guiObjects/gSlider.cpp
  patcher/guiObjects/gText.cpp
  patcher/guiObjects/gToggle.cpp
  patcher/inlet.cpp
  patcher/math/dAdd.cpp
  patcher/math/dClip.cpp
  patcher/math/dDivide.cpp
  patcher/math/dMultiply.cpp
  patcher/math/dSubstract.cpp
  patcher/math/gAdd.cpp
  patcher/math/gCounter.cpp
  patcher/math/gDivide.cpp
  patcher/math/gMultiply.cpp
  patcher/math/gRandom.cpp
  patcher/math/gSubstract.cpp
  patcher/math/pFrequencyToMidi.cpp
  patcher/math/pMidiToFrequency.cpp
  patcher/outlet.cpp
  patcher/parameters.cpp
  patcher/patcher.cpp
  patcher/patcherImplementation.cpp
  patcher/pHandle.cpp
  patcher/pObject.cpp
  patcher/pRegistry.cpp
  patcher/time/gMetro.cpp
  patcher/time/TimerThread.cpp
  player/playerImplementation.cpp
  player/playerInterface.cpp
  player/playerManager.cpp
  reverb/reverbImplementation.cpp
  reverb/reverbInterface.cpp
  reverb/reverbManager.cpp
  sound/soundImplementation.cpp
  sound/soundInterface.cpp
  sound/soundManager.cpp
  synth/dspSound.cpp
  synth/dspVoice.cpp
  synth/dspVoiceInternal.cpp
  synth/samplerSound.cpp
  synth/synthImplementation.cpp
  synth/synthInterface.cpp
  synth/synthManager.cpp
  utils/fileFunctions.cpp
  utils/interpolators.cpp
  utils/vector.cpp
  # AudioTest.cpp was commented out in the original CMakeLists.txt but
  # system.cpp calls YSE::INTERNAL::Test() (inside #ifdef __WINDOWS__), so
  # the DLL fails to link without it.
  internal/AudioTest.cpp
  # patcher/midi objects are registered unconditionally in pRegistry.cpp.
  patcher/midi/mMidiChannelPressure.cpp
  patcher/midi/mMidiControl.cpp
  patcher/midi/mMidiNoteOff.cpp
  patcher/midi/mMidiNoteOn.cpp
  patcher/midi/mMidiOut.cpp
  patcher/midi/mMidiPolyPressure.cpp
  patcher/midi/mMidiProgramChange.cpp
)

# MIDI device I/O (Windows only, requires RtMidi).
# On Linux these files are entirely guarded by #if YSE_WINDOWS so they
# compile to empty TUs and are left out of the source list for clarity.
if(WIN32 AND RtMidi_FOUND)
  list(APPEND YSE_SRCS
    midi/midiDeviceManager.cpp
    midi/device.cpp
    midi/midiNote.cpp
  )
endif()

# ── Source-file-scoped warning suppressions ───────────────────────────────────
# cJSON is a vendored C file; suppress all its warnings rather than patching it.
set_source_files_properties(json/cJSON.cpp
  PROPERTIES COMPILE_FLAGS "-w"
)

# Patcher objects implement a common inlet/thread/message/value interface where
# many overrides intentionally leave some parameters unused.  Suppress
# -Wunused-parameter and the two vendored json.hpp pedantic warnings for all
# patcher TUs rather than touching every .cpp file.
set(PATCHER_SUPPRESS "-Wno-unused-parameter -Wno-deprecated-literal-operator -Wno-tautological-overlap-compare")
set_source_files_properties(
  patcher/patcher.cpp
  patcher/patcherImplementation.cpp
  patcher/pObject.cpp
  patcher/pHandle.cpp
  patcher/pRegistry.cpp
  patcher/inlet.cpp
  patcher/outlet.cpp
  patcher/parameters.cpp
  patcher/filters/dVcf.cpp
  patcher/filters/pBandpass.cpp
  patcher/filters/pHighpass.cpp
  patcher/filters/pLowpass.cpp
  patcher/generatorObjects/dNoise.cpp
  patcher/generatorObjects/dSaw.cpp
  patcher/generatorObjects/pSine.cpp
  patcher/genericObjects/gGate.cpp
  patcher/genericObjects/gReceive.cpp
  patcher/genericObjects/gRoute.cpp
  patcher/genericObjects/gSend.cpp
  patcher/genericObjects/gSwitch.cpp
  patcher/genericObjects/pDac.cpp
  patcher/genericObjects/pLine.cpp
  patcher/guiObjects/gButton.cpp
  patcher/guiObjects/gFloat.cpp
  patcher/guiObjects/gInt.cpp
  patcher/guiObjects/gList.cpp
  patcher/guiObjects/gMessage.cpp
  patcher/guiObjects/gSlider.cpp
  patcher/guiObjects/gText.cpp
  patcher/guiObjects/gToggle.cpp
  patcher/math/dAdd.cpp
  patcher/math/dClip.cpp
  patcher/math/dDivide.cpp
  patcher/math/dMultiply.cpp
  patcher/math/dSubstract.cpp
  patcher/math/gAdd.cpp
  patcher/math/gCounter.cpp
  patcher/math/gDivide.cpp
  patcher/math/gMultiply.cpp
  patcher/math/gRandom.cpp
  patcher/math/gSubstract.cpp
  patcher/math/pFrequencyToMidi.cpp
  patcher/math/pMidiToFrequency.cpp
  patcher/midi/mMidiChannelPressure.cpp
  patcher/midi/mMidiControl.cpp
  patcher/midi/mMidiNoteOff.cpp
  patcher/midi/mMidiNoteOn.cpp
  patcher/midi/mMidiOut.cpp
  patcher/midi/mMidiPolyPressure.cpp
  patcher/midi/mMidiProgramChange.cpp
  patcher/time/gMetro.cpp
  PROPERTIES COMPILE_FLAGS "${PATCHER_SUPPRESS}"
)

# soundImplementation.cpp and soundInterface.cpp pull in json.hpp through patcher headers.
set_source_files_properties(
  sound/soundImplementation.cpp
  sound/soundInterface.cpp
  PROPERTIES COMPILE_FLAGS "-Wno-deprecated-literal-operator -Wno-tautological-overlap-compare"
)

# ── Shared library target ─────────────────────────────────────────────────────
add_library(yse SHARED ${YSE_SRCS})

set_target_properties(yse PROPERTIES
  OUTPUT_NAME yse
  # All symbols exported for now. The API macro in headers/defines.hpp handles
  # __declspec(dllexport/dllimport) under MSVC. With MinGW/Clang on Windows,
  # YSE_MSVC is not set so API expands to nothing, and all DLL symbols are
  # exported by default (MinGW behaviour). Visibility hardening (fvisibility=hidden
  # + per-symbol export attributes) is a follow-up task.
  CXX_VISIBILITY_PRESET default
  VISIBILITY_INLINES_HIDDEN OFF
)

target_compile_features(yse PUBLIC cxx_std_17)

# Expose YseEngine/ as the public include root so consumers can use
#   #include "yse.hpp"
# The same path also lets midiDeviceManager.h's include of
#   "../dependencies/rtmidi/include/RtMidi.h"
# resolve via the compiler's include-directory search as:
#   <YseEngine_dir>/../dependencies/rtmidi/include/RtMidi.h
# which is ${CMAKE_SOURCE_DIR}/dependencies/rtmidi/include/RtMidi.h.
target_include_directories(yse
  PUBLIC
    ${CMAKE_SOURCE_DIR}/YseEngine
  PRIVATE
    # pa_asio.h and other Windows-specific PortAudio extension headers are not
    # shipped by the MSYS2 portaudio package; the vendored copies fill the gap.
    # We still link against the system library — only the headers are borrowed.
    ${CMAKE_SOURCE_DIR}/dependencies/portaudio/include
    # midiDeviceManager.h uses "../dependencies/rtmidi/include/RtMidi.h" as a
    # relative path; device.cpp uses a bare "RtMidi.h". Both resolve once this
    # directory is in the include search path.
    ${CMAKE_SOURCE_DIR}/dependencies/rtmidi/include
)

target_compile_definitions(yse
  PRIVATE
    PORTAUDIO_BACKEND
    LIBSOUNDFILE_BACKEND
)

target_link_libraries(yse
  PRIVATE
    PkgConfig::PortAudio
    PkgConfig::SndFile
    Threads::Threads
)

# On Windows, winmm provides the underlying Win32 MIDI API used by RtMidi.
if(WIN32)
  target_link_libraries(yse PRIVATE winmm)
  if(RtMidi_FOUND)
    target_link_libraries(yse PRIVATE PkgConfig::RtMidi)
  endif()
  # The API export macro in headers/defines.hpp only covers MSVC
  # (__declspec(dllexport)) and macOS (visibility("default")). With
  # MinGW/Clang on Windows, API expands to nothing and Windows PE DLLs do
  # not export symbols implicitly. --export-all-symbols forces full export
  # until visibility hardening is addressed (see KNOWN_ISSUES.md).
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(yse PRIVATE -Wl,--export-all-symbols)
  endif()
elseif(RtMidi_FOUND)
  target_link_libraries(yse PRIVATE PkgConfig::RtMidi)
endif()
