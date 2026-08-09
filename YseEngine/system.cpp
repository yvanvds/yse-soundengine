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

#include <chrono>

#ifdef YSE_WINDOWS
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace {
  // Wall clock for the autoReconnect watchdog (issue #681). Only ever called
  // from system::update() and system::autoReconnect(), both control-thread
  // entry points — never from an audio callback.
  unsigned long long nowMs() {
    return (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  // Minimum time a freshly started stream gets to produce its first callback
  // before the watchdog is allowed to call it stalled (issue #681). Starting a
  // stream returns before the device runs: measured at 15-70 ms on Windows 11,
  // load-dependent. This ceiling is an order of magnitude above that, and short
  // enough that a device which opens but never delivers is still reconnected
  // promptly. It is a floor under the user's `delay`, not an addition to it.
  constexpr unsigned long long kFirstCallbackGraceMs = 500;
} // namespace

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
  // Seed SAMPLERATE with the application's requested rate before anything can
  // derive state from it (issue #646): objects constructed between here and
  // device negotiation (the #637 init window) then already see the requested
  // rate. The backend stays authoritative — if a device opens below, it
  // rewrites SAMPLERATE with the negotiated rate; when no device opens
  // (initOffline, headless CI), the requested rate IS the session rate.
  {
    const UInt requested = DEVICE::Manager().getRequestedSampleRate();
    if (requested != 0 && !INTERNAL::Global().isSampleRateLocked()) {
      SAMPLERATE = requested;
    }
  }

  // global objects should always be loaded before anything else!
  INTERNAL::Global().init();
  currentlyMissedCallbacks = 0;
  doAutoReconnect = false;
  reconnectDelay = 0;
  watchdogStreamStarts = DEVICE::Manager().getStreamStartCount();
  watchdogSinceMs = nowMs();
  watchdogAwaitingFirstCallback = false;

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
    // Remember which kind of session this is, not merely that one is up (issue
    // #719). resume() is the only consumer, and it is what stops an offline
    // session from acquiring a device it was never asked to have — see the
    // comment there and on global::isDeviceSession().
    INTERNAL::Global().sessionHasDevice = openDevice;

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
  // Run user occlusion callbacks on this (control) thread and hand the results
  // to the audio thread via the sound message queue. Keeps user raycast code
  // off the audio callback path (issue #209). No-op when no callback is
  // installed or no sound has occlusion enabled.
  SOUND::updateOcclusion();
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
  // Rebuild a disconnected audio device on this control thread rather than the
  // backend's error thread (e.g. Oboe's onErrorAfterClose). No-op on backends
  // that reconnect synchronously (issue #200).
  DEVICE::Manager().serviceReconnect();

  // Two distinct things are tracked here, and conflating them was issue #681:
  //
  //  * currentlyMissedCallbacks — consecutive update() ticks during which the
  //    device delivered no audio callback. That is the raw liveness reading
  //    hosts poll through missedCallbacks(), and it deliberately still counts
  //    the start-up window of a stream that has been started but is not yet
  //    delivering: "started" is not "running", and a host asking whether audio
  //    is flowing must not be told yes before it is.
  //
  //  * the autoReconnect trigger — which must fire only for a device that is
  //    genuinely stalled. A stream that was just started is in the same
  //    zero-callback state as a disconnected one for the 15-70 ms it takes the
  //    backend to deliver its first callback, and tearing it down there (the
  //    remedy is pause() + resume()) puts the reopened stream straight back
  //    into that window: a host polling update() faster than the device starts
  //    never let a healthy device play a sample.
  const unsigned int streamStarts = DEVICE::Manager().getStreamStartCount();
  const bool streamJustStarted = streamStarts != watchdogStreamStarts;
  if (streamJustStarted) {
    // init(), resume(), openDevice(), or a backend-side rebuild opened a stream
    // since the last tick. It is coming up, not stalling.
    watchdogStreamStarts = streamStarts;
    watchdogAwaitingFirstCallback = true;
  }

  const unsigned int callbacks = DEVICE::Manager().GetCallbacksSinceLastUpdate();
  if (callbacks != 0) {
    currentlyMissedCallbacks = 0;
    watchdogAwaitingFirstCallback = false;
    return;
  }

  // A silence starts at the first empty tick, and at every (re)open — both are
  // points from which "no callbacks for `delay` ms" starts being true.
  if (currentlyMissedCallbacks == 0 || streamJustStarted) watchdogSinceMs = nowMs();
  currentlyMissedCallbacks++;

  if (!doAutoReconnect) return;

  // reconnectDelay is milliseconds (see autoReconnect). A stream that has not
  // delivered its first callback yet gets at least the start-up grace, so a
  // small — or zero, which is the default — delay cannot cut the device off
  // while it is coming up.
  unsigned long long waitMs = (unsigned long long)reconnectDelay;
  if (watchdogAwaitingFirstCallback && waitMs < kFirstCallbackGraceMs) {
    waitMs = kFirstCallbackGraceMs;
  }
  if (nowMs() - watchdogSinceMs < waitMs) return;

  pause();
  resume();
  // Restart the interval from here whatever resume() achieved. A stream that
  // came up bumps the stream-start count and re-enters the grace above on the
  // next tick; one that did not (no device to open — the disconnected case
  // autoReconnect exists for) is retried `delay` ms from now rather than on
  // every single update() tick, which is what "delay between reconnection
  // attempts" has always promised.
  watchdogSinceMs = nowMs();
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
    // The session's device mode dies with the session (issue #719): the next
    // one declares its own, and until then resume() must refuse — with no
    // session up there is nothing to open a device for.
    INTERNAL::Global().sessionHasDevice = false;
    DEVICE::Manager().close();
    INTERNAL::Global().close();
    // Drain the sound manager BEFORE the channel manager: a sound impl that
    // never reached OBJECT_DELETE before close() still holds a `parent` pointer
    // into a channel impl. Freeing the channels first (CHANNEL::destroy) would
    // leave that pointer dangling, to be dereferenced either at the next init()
    // or during static teardown — a use-after-free (issue #298). Clearing the
    // sound impls here, while the channels they reference are still alive,
    // removes the lingering references entirely.
    SOUND::Manager().destroy();
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
  // A session that was never given a device does not acquire one here (issue
  // #719). resume() is a request to restart the device this session already
  // had, not a request for one — an initOffline() session has none, and a
  // closed engine has none either.
  //
  // Without this the call went straight to addCallback() ->
  // Pa_GetDefaultOutputDevice() -> Pa_OpenStream / Pa_StartStream. In a fresh
  // process that fails closed (initOffline() skips Pa_Initialize, so there is
  // no default device to find) and the omission was invisible. But
  // Pa_Initialize runs once per process and managerObject::terminate() — the
  // only call that undoes it — is private and destructor-only, so in any
  // process that has ever called init(), this opened a real stream on an
  // offline session: a live PortAudio callback thread driving the manager
  // update() functions that renderOffline()'s caller is already driving from
  // its own thread, which deviceManager.h documents as single-threaded.
  //
  // Deliberately the *only* guard. The autoReconnect watchdog in update()
  // reaches a device through this same call, so gating here covers it too,
  // rather than repeating the check at each call site and leaving the next one
  // free to forget it (the reasoning that settled issue #716). The watchdog's
  // pause()/resume() pair on an offline session is then two no-ops: pause()
  // closes a stream that does not exist, which it already did.
  if (!INTERNAL::Global().isDeviceSession()) {
    INTERNAL::LogImpl().emit(E_DEBUG,
                             "resume() ignored: this session has no audio device to resume.");
    return;
  }
  DEVICE::Manager().resume();
}

int YSE::system::missedCallbacks() {
  return currentlyMissedCallbacks;
}

YSE::system& YSE::system::requestSampleRate(unsigned int rate) {
  DEVICE::Manager().requestSampleRate(rate);
  return *this;
}

unsigned int YSE::system::requestSampleRate() {
  return DEVICE::Manager().getRequestedSampleRate();
}

YSE::system& YSE::system::autoReconnect(bool on, int delay) {
  doAutoReconnect = on;
  // Milliseconds, as documented — it used to be compared against a count of
  // empty update() ticks, which made the wait depend on how fast the host
  // happens to poll (issue #681). A negative wait is meaningless; clamp it to
  // "as soon as the device is confirmed stalled".
  reconnectDelay = delay < 0 ? 0 : delay;
  // Measure the first wait from here rather than from whatever the previous
  // configuration left behind, so enabling the watchdog cannot fire it
  // immediately on a stale timestamp.
  watchdogSinceMs = nowMs();
  return *this;
}

YSE::system& YSE::system::occlusionCallback(float (*func)(const YSE::Pos&, const YSE::Pos&)) {
  occlusionPtr.store(func, std::memory_order_release);
  return *this;
}

YSE::occlusionFunc YSE::system::occlusionCallback() {
  return occlusionPtr.load(std::memory_order_acquire);
}

YSE::system::system() : occlusionPtr(nullptr) {
  watchdogSinceMs = nowMs();
}

YSE::system& YSE::system::underWaterFX(const channel& target) {
  // The stock underwater effect is an ordinary insert module since issue
  // #327; the driver attaches it through the normal channel::setDSP path,
  // which needs a live implementation to message.
  if (target.pimpl == nullptr) {
    INTERNAL::LogImpl().emit(E_ERROR, "underWaterFX: channel is not created; ignored");
    return *this;
  }
  INTERNAL::UnderWaterEffect().attach(target);
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

bool YSE::system::createClock(const std::string& name, float initialTempo) {
  return CLOCK::Manager().createClock(name, initialTempo);
}

void YSE::system::destroyClock(const std::string& name) {
  CLOCK::Manager().destroyClock(name);
}

bool YSE::system::clockExists(const std::string& name) {
  return CLOCK::Manager().clockExists(name);
}

YSE::system& YSE::system::setTempo(const std::string& name, float bpm, float rampSeconds) {
  CLOCK::Manager().setTempo(name, bpm, rampSeconds);
  return *this;
}

double YSE::system::beatPosition(const std::string& name) {
  return CLOCK::Manager().beatPosition(name);
}

float YSE::system::currentTempo(const std::string& name) {
  return CLOCK::Manager().currentTempo(name);
}

double YSE::system::getSampleRate() {
  // The session lock is set at the end of initShared(); before that, SAMPLERATE
  // still holds its 48000 default and reporting it would mislead hosts that
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
  // The mixer layout must follow the device that is actually open (issue
  // #665). The backend reports whether a stream is running afterwards: a setup
  // with no output device or an ID no host API resolves (both refused since
  // #661), a Pa_OpenStream / Pa_StartStream error, and the offline engine all
  // report false. Applying the requested layout for any of those configures
  // the mixer for a device that is not playing — doOnCallback() resizes the
  // master to getNumberOfOutputs() on the next callback, so a refused switch
  // from a stereo device to a 5.1 one leaves the engine rendering six channels
  // into the two-channel stream that is still live. Leave the layout the
  // running device negotiated.
  if (!DEVICE::Manager().openDevice(object)) return;

  // The session now has a device, whatever it was started with (issue #719).
  //
  // This is a decision, not a side effect. Unlike resume(), which asks for the
  // session's own device back, openDevice() names a device and asks for it — an
  // unambiguous request that an initOffline() session is entitled to make (a
  // headless tool that later decides to play out loud, say). Refusing it would
  // leave such a host with no way at all to reach a device short of close() +
  // init(), and would be a strange thing to refuse *after* the stream is
  // already running. So the session is promoted here instead, which is what
  // makes the pause()/resume() pair work on it afterwards. The flip side —
  // renderOffline() is no longer safe to drive on this session — is the
  // caller's, and is stated on renderOffline() in system.hpp.
  INTERNAL::Global().sessionHasDevice = true;

  // A backend with a single fixed device (Oboe) reports success without
  // reading the setup at all, so the zero-output guard from #661 still has to
  // stand on its own: a zero-output layout silences the engine on the next
  // callback, by the same doOnCallback() resize.
  const int outputs = object.getOutputChannels();
  if (outputs <= 0) return;
  CHANNEL::Manager().setChannelConf(conf, outputs);
}

YSE::system& YSE::system::setChannelConfiguration(CHANNEL_TYPE conf, Int outputs) {
  // The layout half of openDevice(), without the device (issue #668). Since
  // #665 the mixer follows the device that actually opened, so a session with
  // no device to follow — initOffline(), headless CI, renderOffline()
  // benchmarks — had no way at all to leave the stereo layout initShared()
  // installs. This is that way.
  //
  // Same zero-output guard as openDevice(): deviceManager::doOnCallback()
  // resizes the master to getNumberOfOutputs() on the next callback, so a
  // zero-output layout means everything rendered after it goes nowhere.
  if (outputs <= 0) {
    INTERNAL::LogImpl().emit(E_WARNING,
                             "Cannot set a channel configuration with no output channels.");
    return *this;
  }
  CHANNEL::Manager().setChannelConf(conf, outputs);
  return *this;
}

void YSE::system::closeCurrentDevice() {
  DEVICE::Manager().close();
}

UInt YSE::system::getNumDevices() {
  return static_cast<UInt>(DEVICE::Manager().getDeviceList().size());
}

const YSE::device& YSE::system::getDevice(unsigned int nr) {
  // Bound-checked on purpose (issue #581, same fix shape as #565): operator[]
  // never throws, so an out-of-range index used to read past the end of the
  // list and the try/catch in yse_system_get_device() was dead code. With an
  // empty device list — headless CI, or any offline session — index 0 is
  // already out of range, which is the first thing a naive binding tries.
  return DEVICE::Manager().getDeviceList().at(nr);
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
  // No platform guard here on purpose (issue #570). The diagnostic tone is an
  // ordinary dspSourceObject — 11 sines through a low-pass, driven by the same
  // sound path as any user DSP source — so nothing in it is Windows-specific.
  // The old `#ifdef __WINDOWS__` was a JUCE-era remnant (same shape as the
  // lsfSoundfile guard fixed in #46) and turned this documented public call,
  // and the C API's yse_system_audio_test(), into a silent no-op on Linux,
  // macOS, BSD and Android — the platforms where output routing is hardest to
  // diagnose in the first place.
  YSE::INTERNAL::Test().On(on);
  return *this;
}
