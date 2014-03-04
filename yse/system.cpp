/*
==============================================================================

system.cpp
Created: 27 Jan 2014 7:14:31pm
Author:  yvan

==============================================================================
*/

#include "system.hpp"
#include "internal/global.h"
#include "implementations/logImplementation.h"
#include "implementations/listenerImplementation.h"
#include "internal/deviceManager.h"
#include "internal/channelManager.h"
#include "internal/soundManager.h"
#include "internal/underWaterEffect.h"
#include "internal/time.h"
#include "internal/settings.h"
#include "JuceHeader.h"
#ifdef YSE_WINDOWS
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace YSE{
  class systemImpl {
  public:
    systemImpl() : coInitialized(false) {}
    bool coInitialized; // for windows COM library
  };

  systemImpl & SystemImpl() {
    static systemImpl s;
    return s;
  }
}

YSE::system & YSE::System() {
  static YSE::system s;
  return s;
}

Bool YSE::system::init() {
  if (INTERNAL::Global.active) {
    INTERNAL::Global.getLog().emit(E_DEBUG, "You're trying to initialize more than once!");
    return true;
  }
  // global objects should always be loaded before anything else!
  INTERNAL::Global.init();
  
  // on windows, the COM library has to be initialized. This may or may not
  // also happen in your application. It doesn't matter.
#if defined YSE_WINDOWS
  if (SUCCEEDED(CoInitialize(NULL))) {
    SystemImpl().coInitialized = true;
    INTERNAL::Global.getLog().emit(E_DEBUG, "COM library initialized");
  }
#endif
  if (INTERNAL::Global.getDeviceManager().init()) {
    INTERNAL::Global.getLog().emit(E_DEBUG, "YSE System object initialized");

    // initialize channels
    INTERNAL::Global.getChannelManager().changeChannelConf(CT_STEREO);
    INTERNAL::Global.getChannelManager().mainMix().createGlobal();
    INTERNAL::Global.getChannelManager().ambient().create("ambientChannel", INTERNAL::Global.getChannelManager().mainMix());
    INTERNAL::Global.getChannelManager().FX().create("fxChannel", INTERNAL::Global.getChannelManager().mainMix());
    INTERNAL::Global.getChannelManager().music().create("musicChannel", INTERNAL::Global.getChannelManager().mainMix());
    INTERNAL::Global.getChannelManager().gui().create("guiChannel", INTERNAL::Global.getChannelManager().mainMix());
    INTERNAL::Global.getChannelManager().voice().create("voiceChannel", INTERNAL::Global.getChannelManager().mainMix());

    maxSounds(50);
    INTERNAL::Global.active = true;
    return true;
  }
  INTERNAL::Global.getLog().emit(E_ERROR, "YSE System object failed to initialize");
  return false;
}

void YSE::system::update() {
  INTERNAL::Global.flagForUpdate();
}

void YSE::system::close() {
  if (INTERNAL::Global.active) {
    INTERNAL::Global.active = false;
    INTERNAL::Global.getDeviceManager().close();
    INTERNAL::Global.close();
  }
}

YSE::system& YSE::system::occlusionCallback(Flt(*func)(const YSE::Vec&, const YSE::Vec&)) {
  occlusionPtr = func;
  return *this;
}

YSE::occlusionFunc YSE::system::occlusionCallback() {
  return occlusionPtr;
}

YSE::system::system() : occlusionPtr(NULL) {
}

YSE::system::~system() {
#if defined YSE_WINDOWS
  if (SystemImpl().coInitialized) {
    CoUninitialize();
    SystemImpl().coInitialized = false;
  }
#endif
}

YSE::system & YSE::system::underWaterFX(const channel & target) {
  INTERNAL::UnderWaterEffect().channel(target.pimpl);
  return *this;
}

YSE::system & YSE::system::setUnderWaterDepth(Flt value) {
  INTERNAL::UnderWaterEffect().setDepth(value);
  return *this;
}

YSE::system& YSE::system::maxSounds(Int value) {
  INTERNAL::Global.getSoundManager().setMaxSounds(value);
  return *this;
}

Int YSE::system::maxSounds() {
  return INTERNAL::Global.getSoundManager().getMaxSounds();
}

Flt YSE::system::cpuLoad() {
  return INTERNAL::Global.getDeviceManager().cpuLoad();
}

void YSE::system::sleep(UInt ms) {
#if defined YSE_WINDOWS
  Sleep(ms);
#else
  usleep(static_cast<useconds_t>(ms)* 1000);
#endif
}