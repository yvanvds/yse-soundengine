
# internal/AudioTest.cpp


add_definitions(-DYSE_LINUX)


set(SRCS
        BufferIO.cpp
        io.cpp
        listener.cpp
        log.cpp
        system.cpp
        channel/channelImplementation.cpp
        channel/channelInterface.cpp
        channel/channelManager.cpp
        channel/channelMessage.h
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
)


add_library(YSE_dlib SHARED ${SRCS})
add_library(YSE_slib STATIC ${SRCS})


find_package (Threads)


target_link_libraries (YSE_slib ${CMAKE_THREAD_LIBS_INIT} -lpthread -lsndfile -lportaudio )
target_link_libraries (YSE_dlib ${CMAKE_THREAD_LIBS_INIT} -lpthread -lsndfile -lportaudio )
