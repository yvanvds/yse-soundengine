#if YSE_ANDROID

#include "androidDeviceManager.h"
#include "../internalHeaders.h"

// Initial value before the Oboe stream opens. OboeImplementation::openStream
// overwrites this with stream->getSampleRate() (typically 48 kHz on modern
// Android devices) so the rest of the engine sees the negotiated rate.
UInt YSE::SAMPLERATE = 44100;

YSE::DEVICE::managerObject& YSE::DEVICE::Manager() {
  static managerObject d;
  return d;
}

YSE::DEVICE::managerObject::managerObject() {}

YSE::DEVICE::managerObject::~managerObject() {}

Bool YSE::DEVICE::managerObject::init(bool openDevice) {
  YSE::Log().sendMessage("androidDeviceManager: init started");
  if (openDevice && !initDone) {
    implementation.Setup();
    initDone = true;
    open = true;
  }
  YSE::Log().sendMessage("androidDeviceManager: init done");
  deviceManager::init(openDevice);
  return true;
}

void YSE::DEVICE::managerObject::updateDeviceList() {
  devices.clear();

  YSE::device d;
  d.setID(0);
  d.setName("Android Audio");
  d.setTypeName("Oboe");
  d.addOutputChannelName("Left");
  d.addOutputChannelName("Right");
  d.setOutputLatency(100);
  // Reflect the actual rate Oboe negotiated with the device, if the stream is
  // already open; otherwise fall back to the conservative 44.1 kHz default.
  const UInt rate = implementation.getNegotiatedSampleRate() > 0
                        ? (UInt)implementation.getNegotiatedSampleRate()
                        : 44100u;
  d.addAvailableSampleRate(rate);
  devices.push_back(d);
}

void YSE::DEVICE::managerObject::pause() {
  implementation.Suspend();
  // Mirror the PortAudio observable behavior: while paused, the live
  // getActive* getters report 0. We don't tear down the Oboe stream (resume
  // would otherwise have to re-negotiate against the device), we only flip
  // the manager's `open` flag the getters gate on. Issue #74.
  open = false;
}

void YSE::DEVICE::managerObject::resume() {
  implementation.Resume();
  open = true;
}

void YSE::DEVICE::managerObject::addCallback() {
  implementation.Start(YSE::DEVICE::Manager().getMaster().GetBuffers().size());
  // YSE::Log().sendMessage("androidDeviceManager: Callback Added");
}

unsigned int YSE::DEVICE::managerObject::GetCallbacksSinceLastUpdate() {
  return implementation.GetCallbacksSinceLastUpdate();
}

void YSE::DEVICE::managerObject::serviceReconnect() {
  // Runs on the control thread (system::update). Hands the pending reopen to the
  // Oboe implementation, which rebuilds a disconnected stream off the error
  // thread (issue #200).
  implementation.serviceReconnect();
}

void YSE::DEVICE::managerObject::openDevice(const YSE::deviceSetup& object) {
  // android only has one device for now
  return;
}

void YSE::DEVICE::managerObject::close() {
  // YSE::Log().sendMessage("androidDeviceManager: Close started");

  if (!initDone) return;

  // implementation.Stop() is idempotent (guarded on mStream != nullptr) so a
  // close() after pause() — which now clears `open` but leaves the Oboe stream
  // suspended — still tears the stream down on the engine-shutdown path.
  implementation.Stop();
  initDone = false;
  open = false;
  // YSE::Log().sendMessage("androidDeviceManager: Close done");
}

double YSE::DEVICE::managerObject::getActiveSampleRate() const {
  return open ? (double)implementation.getNegotiatedSampleRate() : 0.0;
}

int YSE::DEVICE::managerObject::getActiveBufferSize() const {
  return open ? implementation.getNegotiatedBufferSize() : 0;
}

int YSE::DEVICE::managerObject::getActiveOutputLatency() const {
  if (!open) return 0;
  const int32_t ms = implementation.getNegotiatedOutputLatencyMs();
  const int32_t rate = implementation.getNegotiatedSampleRate();
  const int samplesFromMs = (int)((double)ms * (double)rate / 1000.0);
  if (samplesFromMs > 0) return samplesFromMs;
  // Some Pixel hardware reports ErrorUnimplemented for calculateLatencyMillis;
  // fall back to a single burst worth of frames as a coarse latency estimate
  // so the live API stays non-zero on an open stream. Issue #74.
  return (int)implementation.getNegotiatedBufferSize();
}

#endif
