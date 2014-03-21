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

  CPPFLAGS := $(DEPFLAGS) -D "LINUX=1" -D "DEBUG=1" -D "_DEBUG=1" -D "JUCER_LINUX_MAKE_7346DA2A=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000" -I /usr/include -I /usr/include/freetype2 -I ../../JuceLibraryCode
  CFLAGS += $(CPPFLAGS) $(TARGET_ARCH) -g -ggdb -O0 -std=c++11
  CXXFLAGS += $(CFLAGS)
  LDFLAGS += $(TARGET_ARCH) -L$(BINDIR) -L$(LIBDIR) -L/usr/X11R6/lib/ 
  LDDEPS :=
  RESFLAGS :=  -D "LINUX=1" -D "DEBUG=1" -D "_DEBUG=1" -D "JUCER_LINUX_MAKE_7346DA2A=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000" -I /usr/include -I /usr/include/freetype2 -I ../../JuceLibraryCode
  TARGET := YseDemo
  BLDCMD = $(CXX) -o $(OUTDIR)/$(TARGET) $(OBJECTS) $(LDFLAGS) $(RESOURCES) $(TARGET_ARCH)
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

  CPPFLAGS := $(DEPFLAGS) -D "LINUX=1" -D "NDEBUG=1" -D "JUCER_LINUX_MAKE_7346DA2A=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000" -I /usr/include -I /usr/include/freetype2 -I ../../JuceLibraryCode
  CFLAGS += $(CPPFLAGS) $(TARGET_ARCH) -Os -std=c++11
  CXXFLAGS += $(CFLAGS)
  LDFLAGS += $(TARGET_ARCH) -L$(BINDIR) -L$(LIBDIR) -fvisibility=hidden -L/usr/X11R6/lib/ 
  LDDEPS :=
  RESFLAGS :=  -D "LINUX=1" -D "NDEBUG=1" -D "JUCER_LINUX_MAKE_7346DA2A=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000" -I /usr/include -I /usr/include/freetype2 -I ../../JuceLibraryCode
  TARGET := YseDemo
  BLDCMD = $(CXX) -o $(OUTDIR)/$(TARGET) $(OBJECTS) $(LDFLAGS) $(RESOURCES) $(TARGET_ARCH)
  CLEANCMD = rm -rf $(OUTDIR)/$(TARGET) $(OBJDIR)
endif

OBJECTS := \
  $(OBJDIR)/delay_b71cc077.o \
  $(OBJDIR)/dspObject_4df65954.o \
  $(OBJDIR)/filters_66643b2f.o \
  $(OBJDIR)/math_a511641e.o \
  $(OBJDIR)/math_functions_4d508fa.o \
  $(OBJDIR)/hilbert_b1bc92fc.o \
  $(OBJDIR)/ringModulator_cf23800b.o \
  $(OBJDIR)/sineWave_54962944.o \
  $(OBJDIR)/oscillators_ec482f87.o \
  $(OBJDIR)/ramp_a0f61428.o \
  $(OBJDIR)/sample_99d59d80.o \
  $(OBJDIR)/channelImplementation_b9368d49.o \
  $(OBJDIR)/listenerImplementation_2f5cd79c.o \
  $(OBJDIR)/logImplementation_88b49cca.o \
  $(OBJDIR)/soundImplementation_d4d5c215.o \
  $(OBJDIR)/virtualFinder_7ffb0307.o \
  $(OBJDIR)/channelManager_9ea4553e.o \
  $(OBJDIR)/deviceManager_53deed6d.o \
  $(OBJDIR)/global_1a023237.o \
  $(OBJDIR)/reverbDSP_d6d04405.o \
  $(OBJDIR)/reverbManager_184a5c71.o \
  $(OBJDIR)/settings_296dcef7.o \
  $(OBJDIR)/soundFile_6cacb2a1.o \
  $(OBJDIR)/soundManager_58ad5572.o \
  $(OBJDIR)/time_c641a821.o \
  $(OBJDIR)/underWaterEffect_913530e4.o \
  $(OBJDIR)/vector_7cc3dae9.o \
  $(OBJDIR)/channel_b206b605.o \
  $(OBJDIR)/listener_3c99a73c.o \
  $(OBJDIR)/log_ce3bc246.o \
  $(OBJDIR)/reverb_17a0661a.o \
  $(OBJDIR)/sound_8df0ab51.o \
  $(OBJDIR)/system_6d8877f7.o \
  $(OBJDIR)/ChannelTreeItem_f18519a6.o \
  $(OBJDIR)/yseTimerThread_64774a1c.o \
  $(OBJDIR)/draggedComponent_85ef9d43.o \
  $(OBJDIR)/basic_c172b8af.o \
  $(OBJDIR)/basic3D_365c8820.o \
  $(OBJDIR)/cpuLoad_4aa2482f.o \
  $(OBJDIR)/movingChannels_de5602a7.o \
  $(OBJDIR)/reverbDemo_acaa79be.o \
  $(OBJDIR)/Main_90ebc5c2.o \
  $(OBJDIR)/MainComponent_a6ffb4a5.o \
  $(OBJDIR)/tabs_588e24e7.o \
  $(OBJDIR)/YSEObjects_f8145312.o \

.PHONY: clean

$(OUTDIR)/$(TARGET): $(OBJECTS) $(LDDEPS) $(RESOURCES)
	@echo Linking YseDemo
	-@mkdir -p $(BINDIR)
	-@mkdir -p $(LIBDIR)
	-@mkdir -p $(OUTDIR)
	@$(BLDCMD)

clean:
	@echo Cleaning YseDemo
	@$(CLEANCMD)

strip:
	@echo Stripping YseDemo
	-@strip --strip-unneeded $(OUTDIR)/$(TARGET)

$(OBJDIR)/delay_b71cc077.o: ../../../yse/dsp/delay.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling delay.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/dspObject_4df65954.o: ../../../yse/dsp/dspObject.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling dspObject.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/filters_66643b2f.o: ../../../yse/dsp/filters.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling filters.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/math_a511641e.o: ../../../yse/dsp/math.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling math.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/math_functions_4d508fa.o: ../../../yse/dsp/math_functions.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling math_functions.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/hilbert_b1bc92fc.o: ../../../yse/dsp/modules/hilbert.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling hilbert.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/ringModulator_cf23800b.o: ../../../yse/dsp/modules/ringModulator.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling ringModulator.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/sineWave_54962944.o: ../../../yse/dsp/modules/sineWave.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling sineWave.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/oscillators_ec482f87.o: ../../../yse/dsp/oscillators.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling oscillators.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/ramp_a0f61428.o: ../../../yse/dsp/ramp.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling ramp.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/sample_99d59d80.o: ../../../yse/dsp/sample.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling sample.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/channelImplementation_b9368d49.o: ../../../yse/implementations/channelImplementation.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling channelImplementation.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/listenerImplementation_2f5cd79c.o: ../../../yse/implementations/listenerImplementation.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling listenerImplementation.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/logImplementation_88b49cca.o: ../../../yse/implementations/logImplementation.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling logImplementation.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/soundImplementation_d4d5c215.o: ../../../yse/implementations/soundImplementation.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling soundImplementation.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/virtualFinder_7ffb0307.o: ../../../yse/internal/virtualFinder.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling virtualFinder.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/channelManager_9ea4553e.o: ../../../yse/internal/channelManager.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling channelManager.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/deviceManager_53deed6d.o: ../../../yse/internal/deviceManager.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling deviceManager.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/global_1a023237.o: ../../../yse/internal/global.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling global.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/reverbDSP_d6d04405.o: ../../../yse/internal/reverbDSP.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling reverbDSP.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/reverbManager_184a5c71.o: ../../../yse/internal/reverbManager.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling reverbManager.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/settings_296dcef7.o: ../../../yse/internal/settings.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling settings.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/soundFile_6cacb2a1.o: ../../../yse/internal/soundFile.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling soundFile.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/soundManager_58ad5572.o: ../../../yse/internal/soundManager.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling soundManager.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/time_c641a821.o: ../../../yse/internal/time.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling time.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/underWaterEffect_913530e4.o: ../../../yse/internal/underWaterEffect.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling underWaterEffect.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/vector_7cc3dae9.o: ../../../yse/utils/vector.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling vector.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/channel_b206b605.o: ../../../yse/channel.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling channel.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/listener_3c99a73c.o: ../../../yse/listener.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling listener.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/log_ce3bc246.o: ../../../yse/log.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling log.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/reverb_17a0661a.o: ../../../yse/reverb.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling reverb.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/sound_8df0ab51.o: ../../../yse/sound.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling sound.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/system_6d8877f7.o: ../../../yse/system.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling system.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/ChannelTreeItem_f18519a6.o: ../../Source/parts/ChannelTreeItem.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling ChannelTreeItem.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/yseTimerThread_64774a1c.o: ../../Source/parts/yseTimerThread.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling yseTimerThread.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/draggedComponent_85ef9d43.o: ../../Source/parts/draggedComponent.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling draggedComponent.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/basic_c172b8af.o: ../../Source/basic.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling basic.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/basic3D_365c8820.o: ../../Source/basic3D.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling basic3D.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/cpuLoad_4aa2482f.o: ../../Source/cpuLoad.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling cpuLoad.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/movingChannels_de5602a7.o: ../../Source/movingChannels.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling movingChannels.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/reverbDemo_acaa79be.o: ../../Source/reverbDemo.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling reverbDemo.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/Main_90ebc5c2.o: ../../Source/Main.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling Main.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/MainComponent_a6ffb4a5.o: ../../Source/MainComponent.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling MainComponent.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/tabs_588e24e7.o: ../../Source/tabs.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling tabs.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

$(OBJDIR)/YSEObjects_f8145312.o: ../../Source/YSEObjects.cpp
	-@mkdir -p $(OBJDIR)
	@echo "Compiling YSEObjects.cpp"
	@$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

-include $(OBJECTS:%.o=%.d)
