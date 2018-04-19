# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)



add_library( # Sets the name of the library.
             libYse

             # Sets the library as a shared library.
             SHARED

             # Provides a relative path to your source file(s).
             ../../YseEngine/BufferIO.cpp
             ../../YseEngine/io.cpp
             ../../YseEngine/listener.cpp
             ../../YseEngine/log.cpp
             ../../YseEngine/system.cpp

             ../../YseEngine/channel/channelImplementation.cpp
             ../../YseEngine/channel/channelInterface.cpp
             ../../YseEngine/channel/channelManager.cpp

             ../../YseEngine/device/androidDeviceManager.cpp
             ../../YseEngine/device/deviceInterface.cpp
             ../../YseEngine/device/deviceManager.cpp
             ../../YseEngine/device/deviceSetup.cpp
             ../../YseEngine/device/OpenSL.cpp
             ../../YseEngine/device/OpenSLImplementation.cpp

             ../../YseEngine/dsp/ADSRenvelope.cpp
             ../../YseEngine/dsp/buffer.cpp
             ../../YseEngine/dsp/delay.cpp
             ../../YseEngine/dsp/drawableBuffer.cpp
             ../../YseEngine/dsp/dspObject.cpp
             ../../YseEngine/dsp/envelope.cpp
             ../../YseEngine/dsp/fileBuffer.cpp
             ../../YseEngine/dsp/filters.cpp
             ../../YseEngine/dsp/interpolate4.cpp
             ../../YseEngine/dsp/lfo.cpp
             ../../YseEngine/dsp/math.cpp
             ../../YseEngine/dsp/math_functions.cpp
             ../../YseEngine/dsp/oscillators.cpp
             ../../YseEngine/dsp/ramp.cpp
             ../../YseEngine/dsp/rawFilters.cpp
             ../../YseEngine/dsp/sample_functions.cpp
             ../../YseEngine/dsp/wavetable.cpp

             ../../YseEngine/dsp/fourier/fft.cpp
             ../../YseEngine/dsp/fourier/mayer.cpp

             ../../YseEngine/dsp/modules/granulator.cpp
             ../../YseEngine/dsp/modules/hilbert.cpp
             ../../YseEngine/dsp/modules/phaser.cpp
             ../../YseEngine/dsp/modules/ringModulator.cpp
             ../../YseEngine/dsp/modules/sineWave.cpp

             ../../YseEngine/dsp/modules/delay/basicDelay.cpp
             ../../YseEngine/dsp/modules/delay/highpassDelay.cpp
             ../../YseEngine/dsp/modules/delay/lowpassDelay.cpp

             ../../YseEngine/dsp/modules/filters/bandpass.cpp
             ../../YseEngine/dsp/modules/filters/highpass.cpp
             ../../YseEngine/dsp/modules/filters/lowpass.cpp
             ../../YseEngine/dsp/modules/filters/sweep.cpp

             ../../YseEngine/dsp/modules/fm/difference.cpp

             ../../YseEngine/implementations/listenerImplementation.cpp
             ../../YseEngine/implementations/logImplementation.cpp

             ../../YseEngine/internal/abstractSoundFile.cpp
             ../../YseEngine/internal/AudioTest.cpp
             ../../YseEngine/internal/customFileReader.cpp
             ../../YseEngine/internal/global.cpp
             ../../YseEngine/internal/lsfSoundfile.cpp
             ../../YseEngine/internal/reverbDSP.cpp
             ../../YseEngine/internal/settings.cpp
             ../../YseEngine/internal/thread.cpp
             ../../YseEngine/internal/threadPool.cpp
             ../../YseEngine/internal/time.cpp
             ../../YseEngine/internal/underWaterEffect.cpp
             ../../YseEngine/internal/virtualFinder.cpp

             ../../YseEngine/midi/midifile.cpp
             ../../YseEngine/midi/midifileImplementation.cpp
             ../../YseEngine/midi/midifileManager.cpp

             ../../YseEngine/music/chord.cpp
             ../../YseEngine/music/note.cpp
             ../../YseEngine/music/pNote.cpp
             ../../YseEngine/music/motif/motifImplementation.cpp
             ../../YseEngine/music/motif/motifInterface.cpp
             ../../YseEngine/music/motif/motifManager.cpp
             ../../YseEngine/music/scale/scaleImplementation.cpp
             ../../YseEngine/music/scale/scaleInterface.cpp
             ../../YseEngine/music/scale/scaleManager.cpp

             ../../YseEngine/patcher/inlet.cpp
             ../../YseEngine/patcher/outlet.cpp
             ../../YseEngine/patcher/parameters.cpp
             ../../YseEngine/patcher/patcher.cpp
             ../../YseEngine/patcher/patcherImplementation.cpp
             ../../YseEngine/patcher/pHandle.cpp
             ../../YseEngine/patcher/pObject.cpp
             ../../YseEngine/patcher/pRegistry.cpp

             ../../YseEngine/patcher/filters/dVcf.cpp
             ../../YseEngine/patcher/filters/pBandpass.cpp
             ../../YseEngine/patcher/filters/pHighpass.cpp
             ../../YseEngine/patcher/filters/pLowpass.cpp

             ../../YseEngine/patcher/generatorObjects/dNoise.cpp
             ../../YseEngine/patcher/generatorObjects/dSaw.cpp
             ../../YseEngine/patcher/generatorObjects/pSine.cpp

             ../../YseEngine/patcher/genericObjects/gGate.cpp
             ../../YseEngine/patcher/genericObjects/gReceive.cpp
             ../../YseEngine/patcher/genericObjects/gRoute.cpp
             ../../YseEngine/patcher/genericObjects/gSend.cpp
             ../../YseEngine/patcher/genericObjects/gSwitch.cpp
             ../../YseEngine/patcher/genericObjects/pDac.cpp
             ../../YseEngine/patcher/genericObjects/pLine.cpp

             ../../YseEngine/patcher/guiObjects/gButton.cpp
             ../../YseEngine/patcher/guiObjects/gFloat.cpp
             ../../YseEngine/patcher/guiObjects/gInt.cpp
             ../../YseEngine/patcher/guiObjects/gMessage.cpp
             ../../YseEngine/patcher/guiObjects/gSlider.cpp
             ../../YseEngine/patcher/guiObjects/gText.cpp
             ../../YseEngine/patcher/guiObjects/gToggle.cpp

             ../../YseEngine/patcher/math/dAdd.cpp
             ../../YseEngine/patcher/math/dClip.cpp
             ../../YseEngine/patcher/math/dDivide.cpp
             ../../YseEngine/patcher/math/dMultiply.cpp
             ../../YseEngine/patcher/math/dSubstract.cpp
             ../../YseEngine/patcher/math/gAdd.cpp
             ../../YseEngine/patcher/math/gCounter.cpp
             ../../YseEngine/patcher/math/gDivide.cpp
             ../../YseEngine/patcher/math/gMultiply.cpp
             ../../YseEngine/patcher/math/gRandom.cpp
             ../../YseEngine/patcher/math/gSubstract.cpp
             ../../YseEngine/patcher/math/pFrequencyToMidi.cpp
             ../../YseEngine/patcher/math/pMidiToFrequency.cpp

             ../../YseEngine/patcher/time/gMetro.cpp
             ../../YseEngine/patcher/time/TimerThread.cpp

             ../../YseEngine/player/playerImplementation.cpp
             ../../YseEngine/player/playerInterface.cpp
             ../../YseEngine/player/playerManager.cpp

             ../../YseEngine/reverb/reverbImplementation.cpp
             ../../YseEngine/reverb/reverbManager.cpp
             ../../YseEngine/reverb/reverbInterface.cpp

             ../../YseEngine/sound/soundImplementation.cpp
             ../../YseEngine/sound/soundInterface.cpp
             ../../YseEngine/sound/soundManager.cpp

             ../../YseEngine/synth/dspSound.cpp
             ../../YseEngine/synth/dspVoice.cpp
             ../../YseEngine/synth/dspVoiceInternal.cpp
             ../../YseEngine/synth/samplerSound.cpp
             ../../YseEngine/synth/synthImplementation.cpp
             ../../YseEngine/synth/synthInterface.cpp
             ../../YseEngine/synth/synthManager.cpp

             ../../YseEngine/utils/fileFunctions.cpp
             ../../YseEngine/utils/interpolators.cpp
             ../../YseEngine/utils/vector.cpp

             )

set(sndfile_DIR ${CMAKE_SOURCE_DIR}/../../dependencies/libsndfile/lib)
add_library(snd-lib SHARED IMPORTED)
set_target_properties(snd-lib PROPERTIES IMPORTED_LOCATION ${sndfile_DIR}/libsndfile.so)

# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.

find_library( # Sets the name of the path variable.
              log-lib

              # Specifies the name of the NDK library that
              # you want CMake to locate.
              log )

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
                       libYse

                       # Links the target library to the log library
                       # included in the NDK.
                       ${log-lib} snd-lib OpenSLES )