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

YSE::system& YSE::System() {
  static YSE::system s;
  return s;
}

Bool YSE::system::init() {
  return initShared(true);
}

Bool YSE::system::initOffline() {
  return initShared(false);
}

void YSE::system::renderOffline(int blocks) {
  DEVICE::Manager().renderOffline(blocks);
}

Bool YSE::system::initShared(bool openDevice) {
  if (INTERNAL::Global().active) {
    INTERNAL::LogImpl().emit(E_DEBUG, "You're trying to initialize more than once!");
    return true;
  }
  // global objects should always be loaded before anything else!
  INTERNAL::Global().init();
  currentlyMissedCallbacks = 0;
  doAutoReconnect = false;
  reconnectDelay = 0;

  if (DEVICE::Manager().init(openDevice)) {
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

    if (openDevice) {
      DEVICE::Manager().addCallback();
    }

    // addCallback() is the last point at which the backend can negotiate
    // SAMPLERATE (PortAudio on desktop, Oboe on Android). After this, lock
    // SAMPLERATE for the rest of the session — DSP lookup tables and other
    // derived caches assume a stable rate per session.
    INTERNAL::Global().sampleRateLocked = true;

#ifdef YSE_WINDOWS
    timeBeginPeriod(1);
#endif

    // Boot the embedded interpreter (issue #124) after the audio device is
    // open. No-op unless built with YSE_ENABLE_PYTHON.
    INTERNAL::Global().startScripting();

    return true;
  }
  INTERNAL::LogImpl().emit(E_ERROR, "YSE System object failed to initialize");
  return false;
}

void YSE::system::update() {
  INTERNAL::Global().flagForUpdate();
  // Drain any publishes the audio thread queued since the last update tick
  // and dispatch them synchronously to their subscribers. Cheap when empty:
  // a single SPSC peek + early exit.
  INTERNAL::Global().namedBus().drainPending();
  // Wake the script thread once per tick so future scheduled work can advance
  // (issue #124 establishes the wake; #126/#127 add the scheduling). No-op
  // unless built with YSE_ENABLE_PYTHON.
  INTERNAL::Global().wakeScripting();
  // Deliver any completed script results to the C API error callback (issue
  // #125). Drains on the main thread, so the callback fires here — never on
  // the script thread. No-op unless built with YSE_ENABLE_PYTHON.
  INTERNAL::Global().drainScriptResults();
  unsigned int callbacks = DEVICE::Manager().GetCallbacksSinceLastUpdate();
  if (callbacks == 0) {
    currentlyMissedCallbacks++;
    if (doAutoReconnect && currentlyMissedCallbacks > reconnectDelay) {
      pause();
      resume();
    }
  } else {
    currentlyMissedCallbacks = 0;
  }
}

void YSE::system::close() {
  YSE::PATCHER::TimerThread().Clear();

  if (INTERNAL::Global().active) {
    // Finalize the embedded interpreter (issue #124) before the audio device
    // closes, mirroring the start ordering in initShared(). No-op unless built
    // with YSE_ENABLE_PYTHON.
    INTERNAL::Global().stopScripting();
    // Release the SAMPLERATE lock first so the next init() pass can rewrite
    // SAMPLERATE if the host opens a device with a different negotiated rate.
    INTERNAL::Global().sampleRateLocked = false;
    INTERNAL::Global().active = false;
    DEVICE::Manager().close();
    INTERNAL::Global().close();
    // Tear down the channel manager last: Global().close() has joined both
    // thread pools and the device is already closed, so the persistent
    // master/named channels can be cleared synchronously. This drops their
    // implementation handles so the next System::init() can re-create them
    // instead of asserting (issue #132).
    CHANNEL::Manager().destroy();
  }

#ifdef YSE_WINDOWS
  timeEndPeriod(1);
#endif
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

YSE::system& YSE::system::occlusionCallback(float (*func)(const YSE::Pos&, const YSE::Pos&)) {
  occlusionPtr = func;
  return *this;
}

YSE::occlusionFunc YSE::system::occlusionCallback() {
  return occlusionPtr;
}

YSE::system::system() : occlusionPtr(nullptr) {}

YSE::system& YSE::system::underWaterFX(const channel& target) {
  INTERNAL::UnderWaterEffect().channel(target.pimpl);
  return *this;
}

YSE::system& YSE::system::setUnderWaterDepth(float value) {
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

double YSE::system::getSampleRate() {
  // The session lock is set at the end of initShared(); before that, SAMPLERATE
  // still holds its 44100 default and reporting it would mislead hosts that
  // start sample-count-driven work pre-init.
  return INTERNAL::Global().isSampleRateLocked() ? (double)SAMPLERATE : 0.0;
}

double YSE::system::getActiveSampleRate() {
  return DEVICE::Manager().getActiveSampleRate();
}

int YSE::system::getActiveBufferSize() {
  return DEVICE::Manager().getActiveBufferSize();
}

int YSE::system::getActiveOutputLatency() {
  return DEVICE::Manager().getActiveOutputLatency();
}

void YSE::system::sleep(unsigned int ms) {
#if defined YSE_WINDOWS
  Sleep(ms);
#else
  usleep(static_cast<useconds_t>(ms) * 1000);
#endif
}

YSE::reverb& YSE::system::getGlobalReverb() {
  return REVERB::Manager().getGlobalReverb();
}

const std::vector<YSE::device>& YSE::system::getDevices() {
  return DEVICE::Manager().getDeviceList();
}

void YSE::system::openDevice(const deviceSetup& object, CHANNEL_TYPE conf) {
  DEVICE::Manager().openDevice(object);
  CHANNEL::Manager().setChannelConf(conf, object.getOutputChannels());
}

void YSE::system::closeCurrentDevice() {
  DEVICE::Manager().close();
}

UInt YSE::system::getNumDevices() {
  return static_cast<UInt>(DEVICE::Manager().getDeviceList().size());
}

const YSE::device& YSE::system::getDevice(unsigned int nr) {
  return DEVICE::Manager().getDeviceList()[nr];
}

const std::string& YSE::system::getDefaultDevice() {
  return DEVICE::Manager().getDefaultDeviceName();
}

const std::string& YSE::system::getDefaultHost() {
  return DEVICE::Manager().getDefaultTypeName();
}

#if YSE_ENABLE_MIDI_DEVICE
unsigned int YSE::system::getNumMidiInDevices() {
  return MIDI::DeviceManager().getNumMidiInDevices();
}

unsigned int YSE::system::getNumMidiOutDevices() {
  return MIDI::DeviceManager().getNumMidiOutDevices();
}

const std::string YSE::system::getMidiInDeviceName(unsigned int ID) {
  return MIDI::DeviceManager().getMidiInDeviceName(ID);
}

const std::string YSE::system::getMidiOutDeviceName(unsigned int ID) {
  std::string result = MIDI::DeviceManager().getMidiOutDeviceName(ID);
  return result;
}
#endif

YSE::system& YSE::system::AudioTest(bool on) {
#ifdef __WINDOWS__
  YSE::INTERNAL::Test().On(on);
#endif
  return *this;
}
