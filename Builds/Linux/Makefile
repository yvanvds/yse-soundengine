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
    TARGET_ARCH := -march=native
  endif

  CPPFLAGS := $(DEPFLAGS) -D "LINUX=1" -D "DEBUG=1" -D "_DEBUG=1" -D "JUCER_LINUX_MAKE_7346DA2A=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000" -I /usr/include -I /usr/include/freetype2 -I ../../JuceLibraryCode -I ../../JuceLibraryCode/modules
  CFLAGS += $(CPPFLAGS) $(TARGET_ARCH) -g -ggdb -O0
  CXXFLAGS += $(CFLAGS)
  LDFLAGS += $(TARGET_ARCH) -L$(BINDIR) -L$(LIBDIR) -L/usr/X11R6/lib/ -lX11 -lasound -ldl -lpthread -lrt 
  LDDEPS :=
  RESFLAGS :=  -D "LINUX=1" -D "DEBUG=1" -D "_DEBUG=1" -D "JUCER_LINUX_MAKE_7346DA2A=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000" -I /usr/include -I /usr/include/freetype2 -I ../../JuceLibraryCode -I ../../JuceLibraryCode/modules
  TARGET := libyse-soundengine.a
  BLDCMD = ar -rcs $(OUTDIR)/$(TARGET) $(OBJECTS)
  CLEANCMD = rm -rf $(OUTDIR)/$(TARGET) $(OBJDIR)
endif

ifeq ($(CONFIG),Release)
  BINDIR := build
  LIBDIR := build
  OBJDIR := build/intermediate/Release
  OUTDIR := build

  ifeq ($(TARGET_ARCH),)
    TARGET_ARCH := -march=native
  endif

  CPPFLAGS := $(DEPFLAGS) -D "LINUX=1" -D "NDEBUG=1" -D "JUCER_LINUX_MAKE_7346DA2A=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000" -I /usr/include -I /usr/include/freetype2 -I ../../JuceLibraryCode -I ../../JuceLibraryCode/modules
  CFLAGS += $(CPPFLAGS) $(TARGET_ARCH) -Os
  CXXFLAGS += $(CFLAGS)
  LDFLAGS += $(TARGET_ARCH) -L$(BINDIR) -L$(LIBDIR) -fvisibility=hidden -L/usr/X11R6/lib/ -lX11 -lasound -ldl -lpthread -lrt 
  LDDEPS :=
  RESFLAGS :=  -D "LINUX=1" -D "NDEBUG=1" -D "JUCER_LINUX_MAKE_7346DA2A=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000" -I /usr/include -I /usr/include/freetype2 -I ../../JuceLibraryCode -I ../../JuceLibraryCode/modules
  TARGET := libyse-soundengine.a
  BLDCMD = ar -rcs $(OUTDIR)/$(TARGET) $(OBJECTS)
  CLEANCMD = rm -rf $(OUTDIR)/$(TARGET) $(OBJDIR)
endif

OBJECTS := \
  $(OBJDIR)/filesysImpl_31eab580.o \
  $(OBJDIR)/filesystem_ef21eae6.o \
  $(OBJDIR)/opensl_c67a515e.o \
  $(OBJDIR)/pa_1973f12c.o \
  $(OBJDIR)/reverbbackend_b3568d71.o \
  $(OBJDIR)/soundfile_3552a07a.o \
  $(OBJDIR)/soundLoader_884e2571.o \
  $(OBJDIR)/ysetime_19878347.o \
  $(OBJDIR)/delay_e3d4d95f.o \
  $(OBJDIR)/dsp_c302d45d.o \
  $(OBJDIR)/filters_4579ba17.o \
  $(OBJDIR)/math_95fe8e36.o \
  $(OBJDIR)/math_functions_ec467512.o \
  $(OBJDIR)/hilbert_b878a9e4.o \
  $(OBJDIR)/ringModulator_929cc8f3.o \
  $(OBJDIR)/sineWave_255cef5c.o \
  $(OBJDIR)/oscillators_3533fa6f.o \
  $(OBJDIR)/ramp_91e33e40.o \
  $(OBJDIR)/sample_420a198.o \
  $(OBJDIR)/instrument_f8dbcaca.o \
  $(OBJDIR)/sampler_cdaaedcf.o \
  $(OBJDIR)/sineSynth_a8dc0276.o \
  $(OBJDIR)/channelimpl_5b3d82f1.o \
  $(OBJDIR)/baseInstrumentImpl_753961d7.o \
  $(OBJDIR)/samplerImpl_1c66f4f3.o \
  $(OBJDIR)/sineSynthImpl_417b751a.o \
  $(OBJDIR)/internalObjects_b69d0665.o \
  $(OBJDIR)/listenerimpl_18381490.o \
  $(OBJDIR)/globalTrack_9354c78c.o \
  $(OBJDIR)/trackImpl_391eac6f.o \
  $(OBJDIR)/playlistimpl_44f9d3ae.o \
  $(OBJDIR)/reverbimpl_a6ce516e.o \
  $(OBJDIR)/settings_e6927fdf.o \
  $(OBJDIR)/soundimpl_62b6353d.o \
  $(OBJDIR)/chord_9f97c9dc.o \
  $(OBJDIR)/note_ab852b7c.o \
  $(OBJDIR)/track_89219f4b.o \
  $(OBJDIR)/error_72853d14.o \
  $(OBJDIR)/guard_53c0f331.o \
  $(OBJDIR)/memory_f59705bf.o \
  $(OBJDIR)/misc_db2d8e6a.o \
  $(OBJDIR)/vector_805e3901.o \
  $(OBJDIR)/channel_883fe8ed.o \
  $(OBJDIR)/device_5ba788b6.o \
  $(OBJDIR)/listener_2d86d154.o \
  $(OBJDIR)/playlist_a9d0cf72.o \
  $(OBJDIR)/reverb_fd813632.o \
  $(OBJDIR)/sound_cf297839.o \
  $(OBJDIR)/speakers_c1f6dcf4.o \
  $(OBJDIR)/system_5369480f.o \
  $(OBJDIR)/juce_audio_basics_2442e4ea.o \
  $(OBJDIR)/juce_audio_devices_a4c8a728.o \
  $(OBJDIR)/juce_audio_formats_d349f0c8.o \
  $(OBJDIR)/juce_core_aff681cc.o \
  $(OBJDIR)/juce_events_79b2840.o \

.PHONY: clean

$(OUTDIR)/$(TARGET): $(OBJECTS) $(LDDEPS) $(RESOURCES)
	@echo Linking yse-soundengine
	-@mkdir -p $(BINDIR)
	-@mkdir -p $(LIBDIR)
	-@mkdir -p $(OUTDIR)
	@$(BLDCMD)

clean:
	@echo Cleaning yse-soundengine
	@$(CLEANCMD)

strip:
	@echo Stripping yse-soundengine
	-@strip --strip-unneeded $(OUTDIR)/$(TARGET)

$(OBJDIR)/filesysImpl_31eab580.o: ../../src/backend/filesysImpl.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling filesysImpl.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/filesystem_ef21eae6.o: ../../src/backend/filesystem.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling filesystem.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/opensl_c67a515e.o: ../../src/backend/opensl.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling opensl.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/pa_1973f12c.o: ../../src/backend/pa.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling pa.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/reverbbackend_b3568d71.o: ../../src/backend/reverbbackend.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling reverbbackend.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/soundfile_3552a07a.o: ../../src/backend/soundfile.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling soundfile.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/soundLoader_884e2571.o: ../../src/backend/soundLoader.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling soundLoader.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/ysetime_19878347.o: ../../src/backend/ysetime.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling ysetime.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/delay_e3d4d95f.o: ../../src/dsp/delay.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling delay.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/dsp_c302d45d.o: ../../src/dsp/dsp.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling dsp.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/filters_4579ba17.o: ../../src/dsp/filters.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling filters.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/math_95fe8e36.o: ../../src/dsp/math.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling math.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/math_functions_ec467512.o: ../../src/dsp/math_functions.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling math_functions.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/hilbert_b878a9e4.o: ../../src/dsp/modules/hilbert.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling hilbert.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/ringModulator_929cc8f3.o: ../../src/dsp/modules/ringModulator.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling ringModulator.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/sineWave_255cef5c.o: ../../src/dsp/modules/sineWave.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling sineWave.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/oscillators_3533fa6f.o: ../../src/dsp/oscillators.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling oscillators.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/ramp_91e33e40.o: ../../src/dsp/ramp.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling ramp.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/sample_420a198.o: ../../src/dsp/sample.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling sample.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/instrument_f8dbcaca.o: ../../src/instruments/instrument.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling instrument.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/sampler_cdaaedcf.o: ../../src/instruments/sampler.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling sampler.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/sineSynth_a8dc0276.o: ../../src/instruments/sineSynth.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling sineSynth.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/channelimpl_5b3d82f1.o: ../../src/internal/channelimpl.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling channelimpl.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/baseInstrumentImpl_753961d7.o: ../../src/internal/instruments/baseInstrumentImpl.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling baseInstrumentImpl.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/samplerImpl_1c66f4f3.o: ../../src/internal/instruments/samplerImpl.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling samplerImpl.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/sineSynthImpl_417b751a.o: ../../src/internal/instruments/sineSynthImpl.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling sineSynthImpl.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/internalObjects_b69d0665.o: ../../src/internal/internalObjects.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling internalObjects.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/listenerimpl_18381490.o: ../../src/internal/listenerimpl.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling listenerimpl.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/globalTrack_9354c78c.o: ../../src/internal/music/globalTrack.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling globalTrack.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/trackImpl_391eac6f.o: ../../src/internal/music/trackImpl.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling trackImpl.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/playlistimpl_44f9d3ae.o: ../../src/internal/playlistimpl.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling playlistimpl.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/reverbimpl_a6ce516e.o: ../../src/internal/reverbimpl.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling reverbimpl.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/settings_e6927fdf.o: ../../src/internal/settings.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling settings.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/soundimpl_62b6353d.o: ../../src/internal/soundimpl.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling soundimpl.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/chord_9f97c9dc.o: ../../src/music/chord.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling chord.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/note_ab852b7c.o: ../../src/music/note.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling note.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/track_89219f4b.o: ../../src/music/track.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling track.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/error_72853d14.o: ../../src/utils/error.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling error.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/guard_53c0f331.o: ../../src/utils/guard.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling guard.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/memory_f59705bf.o: ../../src/utils/memory.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling memory.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/misc_db2d8e6a.o: ../../src/utils/misc.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling misc.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/vector_805e3901.o: ../../src/utils/vector.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling vector.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/channel_883fe8ed.o: ../../src/channel.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling channel.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/device_5ba788b6.o: ../../src/device.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling device.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/listener_2d86d154.o: ../../src/listener.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling listener.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/playlist_a9d0cf72.o: ../../src/playlist.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling playlist.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/reverb_fd813632.o: ../../src/reverb.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling reverb.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/sound_cf297839.o: ../../src/sound.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling sound.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/speakers_c1f6dcf4.o: ../../src/speakers.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling speakers.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/system_5369480f.o: ../../src/system.cpp
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
