/*
==============================================================================

system.cpp
Created: 27 Jan 2014 7:14:31pm
Author:  yvan

==============================================================================
*/

#include "internalHeaders.h"
#include "patcher/time/TimerThread.h"
#include "device/portaudioDeviceManager.h"


#ifdef YSE_WINDOWS
#include <Windows.h>
#else
#include <unistd.h>
#endif


YSE::system & YSE::System() {
  static YSE::system s;
  return s;
}

Bool YSE::system::init() {
  if (INTERNAL::Global().active) {
    INTERNAL::LogImpl().emit(E_DEBUG, "You're trying to initialize more than once!");
    return true;
  }
  // global objects should always be loaded before anything else!
  INTERNAL::Global().init();
	currentlyMissedCallbacks = 0;
	doAutoReconnect = false;
	reconnectDelay = 0;

  if (DEVICE::Manager().init()) {
    INTERNAL::LogImpl().emit(E_DEBUG, "YSE System object initialized");

    // initialize channels
    CHANNEL::Manager().setChannelConf(CT_STEREO);
    CHANNEL::Manager().changeChannelConf();
    CHANNEL::Manager().master().createGlobal();
    CHANNEL::Manager().ambient().create("ambientChannel", CHANNEL::Manager().master());
    CHANNEL::Manager().FX().create("fxChannel", CHANNEL::Manager().master());
    CHANNEL::Manager().music().create("musicChannel", CHANNEL::Manager().master());
    CHANNEL::Manager().gui().create("guiChannel", CHANNEL::Manager().master());
    CHANNEL::Manager().voice().create("voiceChannel", CHANNEL::Manager().master());

    maxSounds(50);
    INTERNAL::Global().active = true;

    DEVICE::Manager().addCallback();

    return true;
  }
  INTERNAL::LogImpl().emit(E_ERROR, "YSE System object failed to initialize");
  return false;
}

void YSE::system::update() {
  INTERNAL::Global().flagForUpdate();
	unsigned int callbacks = DEVICE::Manager().GetCallbacksSinceLastUpdate();
	if (callbacks == 0) {
		currentlyMissedCallbacks++;
		if (doAutoReconnect && currentlyMissedCallbacks > reconnectDelay) {
			pause();
			resume();
		}
	}
	else {
		currentlyMissedCallbacks = 0;
	}
}

void YSE::system::close() {
  YSE::PATCHER::TimerThread().Clear();

  if (INTERNAL::Global().active) {
    INTERNAL::Global().active = false;
    DEVICE::Manager().close();
    INTERNAL::Global().close();
  }
}

void YSE::system::pause() {
	DEVICE::Manager().pause();
}

void YSE::system::resume() {
	DEVICE::Manager().resume();
}

int YSE::system::missedCallbacks() {
	return currentlyMissedCallbacks;
}

YSE::system& YSE::system::autoReconnect(bool on, int delay) {
	doAutoReconnect = on;
	reconnectDelay = delay;
	return *this;
}

YSE::system& YSE::system::occlusionCallback(float(*func)(const YSE::Pos&, const YSE::Pos&)) {
  occlusionPtr = func;
  return *this;
}

YSE::occlusionFunc YSE::system::occlusionCallback() {
  return occlusionPtr;
}

YSE::system::system() : occlusionPtr(nullptr) {
}

YSE::system & YSE::system::underWaterFX(const channel & target) {
  INTERNAL::UnderWaterEffect().channel(target.pimpl);
  return *this;
}

YSE::system & YSE::system::setUnderWaterDepth(float value) {
  INTERNAL::UnderWaterEffect().setDepth(value);
  return *this;
}

YSE::system& YSE::system::maxSounds(Int value) {
  VirtualSoundFinder().setLimit(value);
  return *this;
}

Int YSE::system::maxSounds() {
  return VirtualSoundFinder().getLimit();
}

Flt YSE::system::cpuLoad() {
  return DEVICE::Manager().cpuLoad();
}

void YSE::system::sleep(unsigned int ms) {
#if defined YSE_WINDOWS
  Sleep(ms);
#else
  usleep(static_cast<useconds_t>(ms)* 1000);
#endif
}

YSE::reverb & YSE::system::getGlobalReverb() {
  return REVERB::Manager().getGlobalReverb();
}

const std::vector<YSE::device> & YSE::system::getDevices() {
  return DEVICE::Manager().getDeviceList();
}

void YSE::system::openDevice(const deviceSetup & object, CHANNEL_TYPE conf) {
  DEVICE::Manager().openDevice(object);
  CHANNEL::Manager().setChannelConf(conf, object.getOutputChannels());

}

void YSE::system::closeCurrentDevice() {
  DEVICE::Manager().close();
}

UInt YSE::system::getNumDevices() {
  return DEVICE::Manager().getDeviceList().size();
}

const YSE::device & YSE::system::getDevice(unsigned int nr) {
  return DEVICE::Manager().getDeviceList()[nr];
}

const std::string & YSE::system::getDefaultDevice() {
  return DEVICE::Manager().getDefaultDeviceName();
}

const std::string & YSE::system::getDefaultHost() {
  return DEVICE::Manager().getDefaultTypeName();
}

YSE::system & YSE::system::AudioTest(bool on) {
#ifdef __WINDOWS__
  YSE::INTERNAL::Test().On(on);
#endif
  return *this;
}
