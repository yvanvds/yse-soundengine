# Automatically generated makefile, created by the Introjucer
# Don't edit this file! Your changes will be overwritten when you re-save the Introjucer project!

# (this disables dependency generation if multiple architectures are set)
DEPFLAGS := $(if $(word 2, $(TARGET_ARCH)), , -MMD)

ifndef CONFIG
  CONFIG=Debug
endif

ifeq ($(CONFIG),Debug)
  BINDIR := build
  LIBDIR := build
  OBJDIR := build/intermediate/Debug
  OUTDIR := build

  ifeq ($(TARGET_ARCH),)
    TARGET_ARCH := -m64
  endif

  CPPFLAGS := $(DEPFLAGS) -D "LINUX=1" -D "DEBUG=1" -D "_DEBUG=1" -D "JUCE_CHECK_MEMORY_LEAKS=0" -D "JUCER_LINUX_MAKE_6D53C8B4=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000" -I /usr/include -I /usr/include/freetype2 -I ../../JuceLibraryCode -I ../../JuceLibraryCode/modules -I ../yse
  CFLAGS += $(CPPFLAGS) $(TARGET_ARCH) -g -ggdb -O0
  CXXFLAGS += $(CFLAGS) -std=c++11
  LDFLAGS += $(TARGET_ARCH) -L$(BINDIR) -L$(LIBDIR) -L/usr/X11R6/lib/ -lX11 -lasound -ldl -lpthread -lrt 

  TARGET := libyse_static.a
  BLDCMD = ar -rcs $(OUTDIR)/$(TARGET) $(OBJECTS)
  CLEANCMD = rm -rf $(OUTDIR)/$(TARGET) $(OBJDIR)
endif

ifeq ($(CONFIG),Release)
  BINDIR := build
  LIBDIR := build
  OBJDIR := build/intermediate/Release
  OUTDIR := build

  ifeq ($(TARGET_ARCH),)
    TARGET_ARCH := -m64
  endif

  CPPFLAGS := $(DEPFLAGS) -D "LINUX=1" -D "NDEBUG=1" -D "JUCE_CHECK_MEMORY_LEAKS=0" -D "JUCER_LINUX_MAKE_6D53C8B4=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000" -I /usr/include -I /usr/include/freetype2 -I ../../JuceLibraryCode -I ../../JuceLibraryCode/modules -I ../yse
  CFLAGS += $(CPPFLAGS) $(TARGET_ARCH) -O3
  CXXFLAGS += $(CFLAGS) -std=c++11
  LDFLAGS += $(TARGET_ARCH) -L$(BINDIR) -L$(LIBDIR) -fvisibility=hidden -L/usr/X11R6/lib/ -lX11 -lasound -ldl -lpthread -lrt 

  TARGET := libyse_static.a
  BLDCMD = ar -rcs $(OUTDIR)/$(TARGET) $(OBJECTS)
  CLEANCMD = rm -rf $(OUTDIR)/$(TARGET) $(OBJDIR)
endif

OBJECTS := \
  $(OBJDIR)/playerImplementation_a051a4e3.o \
  $(OBJDIR)/playerInterface_e71c1e72.o \
  $(OBJDIR)/playerManager_a76057e6.o \
  $(OBJDIR)/midifile_329da8d.o \
  $(OBJDIR)/midifileImplementation_ad357f5f.o \
  $(OBJDIR)/midifileManager_9faf10ea.o \
  $(OBJDIR)/dspSound_d858b709.o \
  $(OBJDIR)/dspVoice_7a405ac.o \
  $(OBJDIR)/dspVoiceInternal_68509309.o \
  $(OBJDIR)/samplerSound_ec9b69a2.o \
  $(OBJDIR)/synthImplementation_27771e7d.o \
  $(OBJDIR)/synthInterface_b6438c98.o \
  $(OBJDIR)/synthManager_a9b9178c.o \
  $(OBJDIR)/channelImplementation_b9703dcb.o \
  $(OBJDIR)/channelInterface_242b948a.o \
  $(OBJDIR)/channelManager_d58673fe.o \
  $(OBJDIR)/deviceInterface_e0aed812.o \
  $(OBJDIR)/deviceManager_db94f986.o \
  $(OBJDIR)/deviceSetup_6dd15556.o \
  $(OBJDIR)/fft_c23583c9.o \
  $(OBJDIR)/mayer_786bcde7.o \
  $(OBJDIR)/basicDelay_d0bc6d9f.o \
  $(OBJDIR)/highpassDelay_61a9efb0.o \
  $(OBJDIR)/lowpassDelay_19f56908.o \
  $(OBJDIR)/bandpass_4f5eee78.o \
  $(OBJDIR)/highpass_c7be08e5.o \
  $(OBJDIR)/lowpass_288b8afd.o \
  $(OBJDIR)/sweep_fcf75dc4.o \
  $(OBJDIR)/difference_47acd6f1.o \
  $(OBJDIR)/hilbert_b1bc92fc.o \
  $(OBJDIR)/phaser_b4e5fe35.o \
  $(OBJDIR)/ringModulator_cf23800b.o \
  $(OBJDIR)/sineWave_54962944.o \
  $(OBJDIR)/ADSRenvelope_7f08daa8.o \
  $(OBJDIR)/buffer_2ec8116.o \
  $(OBJDIR)/delay_b71cc077.o \
  $(OBJDIR)/drawableBuffer_79117f54.o \
  $(OBJDIR)/dspObject_4df65954.o \
  $(OBJDIR)/envelope_580ab066.o \
  $(OBJDIR)/fileBuffer_56b6af32.o \
  $(OBJDIR)/filters_66643b2f.o \
  $(OBJDIR)/granulator_2f1ca123.o \
  $(OBJDIR)/interpolate4_b72995e9.o \
  $(OBJDIR)/lfo_7dbe63e9.o \
  $(OBJDIR)/math_a511641e.o \
  $(OBJDIR)/math_functions_4d508fa.o \
  $(OBJDIR)/oscillators_ec482f87.o \
  $(OBJDIR)/ramp_a0f61428.o \
  $(OBJDIR)/rawFilters_56640989.o \
  $(OBJDIR)/sample_functions_bff26fdc.o \
  $(OBJDIR)/wavetable_2898e2e9.o \
  $(OBJDIR)/listenerImplementation_2f5cd79c.o \
  $(OBJDIR)/logImplementation_88b49cca.o \
  $(OBJDIR)/customFileReader_68d33104.o \
  $(OBJDIR)/global_1a023237.o \
  $(OBJDIR)/reverbDSP_d6d04405.o \
  $(OBJDIR)/settings_296dcef7.o \
  $(OBJDIR)/soundFile_6cacb2a1.o \
  $(OBJDIR)/thread_690b11e.o \
  $(OBJDIR)/threadPool_4352e2da.o \
  $(OBJDIR)/time_c641a821.o \
  $(OBJDIR)/underWaterEffect_913530e4.o \
  $(OBJDIR)/virtualFinder_7ffb0307.o \
  $(OBJDIR)/motifImplementation_81ad9b79.o \
  $(OBJDIR)/motifInterface_caf9e21c.o \
  $(OBJDIR)/motifManager_c4efe10.o \
  $(OBJDIR)/scaleImplementation_d8ddccef.o \
  $(OBJDIR)/scaleInterface_bc421ee6.o \
  $(OBJDIR)/scaleManager_2333e55a.o \
  $(OBJDIR)/pNote_be95281a.o \
  $(OBJDIR)/chord_c0824af4.o \
  $(OBJDIR)/note_413a2764.o \
  $(OBJDIR)/reverbImplementation_a6b2bc3.o \
  $(OBJDIR)/reverbInterface_e521cb92.o \
  $(OBJDIR)/reverbManager_5f2ecd06.o \
  $(OBJDIR)/soundImplementation_a72c0263.o \
  $(OBJDIR)/soundInterface_f59d50f2.o \
  $(OBJDIR)/soundManager_918c2a66.o \
  $(OBJDIR)/interpolators_d82dfbb0.o \
  $(OBJDIR)/vector_7cc3dae9.o \
  $(OBJDIR)/io_4bc200ee.o \
  $(OBJDIR)/listener_3c99a73c.o \
  $(OBJDIR)/log_ce3bc246.o \
  $(OBJDIR)/system_6d8877f7.o \
  $(OBJDIR)/juce_audio_basics_2442e4ea.o \
  $(OBJDIR)/juce_audio_devices_a4c8a728.o \
  $(OBJDIR)/juce_audio_formats_d349f0c8.o \
  $(OBJDIR)/juce_core_aff681cc.o \
  $(OBJDIR)/juce_events_79b2840.o \

.PHONY: clean

$(OUTDIR)/$(TARGET): $(OBJECTS) $(RESOURCES)
	@echo Linking yse_static
	-@mkdir -p $(BINDIR)
	-@mkdir -p $(LIBDIR)
	-@mkdir -p $(OUTDIR)
	@$(BLDCMD)

clean:
	@echo Cleaning yse_static
	@$(CLEANCMD)

strip:
	@echo Stripping yse_static
	-@strip --strip-unneeded $(OUTDIR)/$(TARGET)

$(OBJDIR)/playerImplementation_a051a4e3.o: ../../../yse/player/playerImplementation.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling playerImplementation.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/playerInterface_e71c1e72.o: ../../../yse/player/playerInterface.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling playerInterface.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/playerManager_a76057e6.o: ../../../yse/player/playerManager.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling playerManager.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/midifile_329da8d.o: ../../../yse/midi/midifile.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling midifile.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/midifileImplementation_ad357f5f.o: ../../../yse/midi/midifileImplementation.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling midifileImplementation.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/midifileManager_9faf10ea.o: ../../../yse/midi/midifileManager.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling midifileManager.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/dspSound_d858b709.o: ../../../yse/synth/dspSound.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling dspSound.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/dspVoice_7a405ac.o: ../../../yse/synth/dspVoice.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling dspVoice.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/dspVoiceInternal_68509309.o: ../../../yse/synth/dspVoiceInternal.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling dspVoiceInternal.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/samplerSound_ec9b69a2.o: ../../../yse/synth/samplerSound.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling samplerSound.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/synthImplementation_27771e7d.o: ../../../yse/synth/synthImplementation.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling synthImplementation.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/synthInterface_b6438c98.o: ../../../yse/synth/synthInterface.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling synthInterface.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/synthManager_a9b9178c.o: ../../../yse/synth/synthManager.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling synthManager.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/channelImplementation_b9703dcb.o: ../../../yse/channel/channelImplementation.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling channelImplementation.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/channelInterface_242b948a.o: ../../../yse/channel/channelInterface.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling channelInterface.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/channelManager_d58673fe.o: ../../../yse/channel/channelManager.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling channelManager.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/deviceInterface_e0aed812.o: ../../../yse/device/deviceInterface.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling deviceInterface.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/deviceManager_db94f986.o: ../../../yse/device/deviceManager.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling deviceManager.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/deviceSetup_6dd15556.o: ../../../yse/device/deviceSetup.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling deviceSetup.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/fft_c23583c9.o: ../../../yse/dsp/fourier/fft.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling fft.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/mayer_786bcde7.o: ../../../yse/dsp/fourier/mayer.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling mayer.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/basicDelay_d0bc6d9f.o: ../../../yse/dsp/modules/delay/basicDelay.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling basicDelay.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/highpassDelay_61a9efb0.o: ../../../yse/dsp/modules/delay/highpassDelay.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling highpassDelay.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/lowpassDelay_19f56908.o: ../../../yse/dsp/modules/delay/lowpassDelay.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling lowpassDelay.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/bandpass_4f5eee78.o: ../../../yse/dsp/modules/filters/bandpass.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling bandpass.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/highpass_c7be08e5.o: ../../../yse/dsp/modules/filters/highpass.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling highpass.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/lowpass_288b8afd.o: ../../../yse/dsp/modules/filters/lowpass.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling lowpass.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/sweep_fcf75dc4.o: ../../../yse/dsp/modules/filters/sweep.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling sweep.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/difference_47acd6f1.o: ../../../yse/dsp/modules/fm/difference.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling difference.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/hilbert_b1bc92fc.o: ../../../yse/dsp/modules/hilbert.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling hilbert.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/phaser_b4e5fe35.o: ../../../yse/dsp/modules/phaser.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling phaser.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/ringModulator_cf23800b.o: ../../../yse/dsp/modules/ringModulator.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling ringModulator.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/sineWave_54962944.o: ../../../yse/dsp/modules/sineWave.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling sineWave.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/ADSRenvelope_7f08daa8.o: ../../../yse/dsp/ADSRenvelope.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling ADSRenvelope.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/buffer_2ec8116.o: ../../../yse/dsp/buffer.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling buffer.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/delay_b71cc077.o: ../../../yse/dsp/delay.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling delay.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/drawableBuffer_79117f54.o: ../../../yse/dsp/drawableBuffer.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling drawableBuffer.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/dspObject_4df65954.o: ../../../yse/dsp/dspObject.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling dspObject.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/envelope_580ab066.o: ../../../yse/dsp/envelope.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling envelope.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/fileBuffer_56b6af32.o: ../../../yse/dsp/fileBuffer.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling fileBuffer.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/filters_66643b2f.o: ../../../yse/dsp/filters.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling filters.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/granulator_2f1ca123.o: ../../../yse/dsp/modules/granulator.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling granulator.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/interpolate4_b72995e9.o: ../../../yse/dsp/interpolate4.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling interpolate4.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/lfo_7dbe63e9.o: ../../../yse/dsp/lfo.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling lfo.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/math_a511641e.o: ../../../yse/dsp/math.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling math.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/math_functions_4d508fa.o: ../../../yse/dsp/math_functions.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling math_functions.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/oscillators_ec482f87.o: ../../../yse/dsp/oscillators.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling oscillators.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/ramp_a0f61428.o: ../../../yse/dsp/ramp.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling ramp.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/rawFilters_56640989.o: ../../../yse/dsp/rawFilters.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling rawFilters.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/sample_functions_bff26fdc.o: ../../../yse/dsp/sample_functions.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling sample_functions.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/wavetable_2898e2e9.o: ../../../yse/dsp/wavetable.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling wavetable.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/listenerImplementation_2f5cd79c.o: ../../../yse/implementations/listenerImplementation.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling listenerImplementation.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/logImplementation_88b49cca.o: ../../../yse/implementations/logImplementation.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling logImplementation.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/customFileReader_68d33104.o: ../../../yse/internal/customFileReader.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling customFileReader.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/global_1a023237.o: ../../../yse/internal/global.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling global.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/reverbDSP_d6d04405.o: ../../../yse/internal/reverbDSP.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling reverbDSP.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/settings_296dcef7.o: ../../../yse/internal/settings.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling settings.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/soundFile_6cacb2a1.o: ../../../yse/internal/soundFile.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling soundFile.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/thread_690b11e.o: ../../../yse/internal/thread.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling thread.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/threadPool_4352e2da.o: ../../../yse/internal/threadPool.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling threadPool.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/time_c641a821.o: ../../../yse/internal/time.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling time.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/underWaterEffect_913530e4.o: ../../../yse/internal/underWaterEffect.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling underWaterEffect.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/virtualFinder_7ffb0307.o: ../../../yse/internal/virtualFinder.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling virtualFinder.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/motifImplementation_81ad9b79.o: ../../../yse/music/motif/motifImplementation.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling motifImplementation.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/motifInterface_caf9e21c.o: ../../../yse/music/motif/motifInterface.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling motifInterface.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/motifManager_c4efe10.o: ../../../yse/music/motif/motifManager.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling motifManager.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/scaleImplementation_d8ddccef.o: ../../../yse/music/scale/scaleImplementation.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling scaleImplementation.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/scaleInterface_bc421ee6.o: ../../../yse/music/scale/scaleInterface.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling scaleInterface.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/scaleManager_2333e55a.o: ../../../yse/music/scale/scaleManager.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling scaleManager.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/pNote_be95281a.o: ../../../yse/music/pNote.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling pNote.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/chord_c0824af4.o: ../../../yse/music/chord.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling chord.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/note_413a2764.o: ../../../yse/music/note.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling note.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/reverbImplementation_a6b2bc3.o: ../../../yse/reverb/reverbImplementation.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling reverbImplementation.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/reverbInterface_e521cb92.o: ../../../yse/reverb/reverbInterface.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling reverbInterface.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/reverbManager_5f2ecd06.o: ../../../yse/reverb/reverbManager.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling reverbManager.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/soundImplementation_a72c0263.o: ../../../yse/sound/soundImplementation.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling soundImplementation.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/soundInterface_f59d50f2.o: ../../../yse/sound/soundInterface.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling soundInterface.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/soundManager_918c2a66.o: ../../../yse/sound/soundManager.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling soundManager.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/interpolators_d82dfbb0.o: ../../../yse/utils/interpolators.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling interpolators.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/vector_7cc3dae9.o: ../../../yse/utils/vector.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling vector.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/io_4bc200ee.o: ../../../yse/io.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling io.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/listener_3c99a73c.o: ../../../yse/listener.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling listener.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/log_ce3bc246.o: ../../../yse/log.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling log.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/system_6d8877f7.o: ../../../yse/system.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling system.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/juce_audio_basics_2442e4ea.o: ../../JuceLibraryCode/modules/juce_audio_basics/juce_audio_basics.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling juce_audio_basics.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/juce_audio_devices_a4c8a728.o: ../../JuceLibraryCode/modules/juce_audio_devices/juce_audio_devices.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling juce_audio_devices.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/juce_audio_formats_d349f0c8.o: ../../JuceLibraryCode/modules/juce_audio_formats/juce_audio_formats.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling juce_audio_formats.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/juce_core_aff681cc.o: ../../JuceLibraryCode/modules/juce_core/juce_core.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling juce_core.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/juce_events_79b2840.o: ../../JuceLibraryCode/modules/juce_events/juce_events.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling juce_events.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

-include $(OBJECTS:%.o=%.d)
