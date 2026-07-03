/*
  ==============================================================================

  channel.cpp
  Created: 30 Jan 2014 4:20:50pm
  Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"

#include <cmath>
#include <mutex>
#include <unordered_set>
#include <variant>

namespace {
  // Linear to dBFS with a -120 dB floor for silence.
  inline float linearToDb(float linear) {
    constexpr float kDbFloor = -120.f;
    if (linear <= 0.f) return kDbFloor;
    const float db = 20.f * std::log10(linear);
    return db < kDbFloor ? kDbFloor : db;
  }

  // Process-wide registry of channel names currently claimed as bus producers.
  // Independent of the "sound." namespace; naming runs on the main thread and
  // the mutex never touches the audio path.
  std::mutex& channelNameMutex() {
    static std::mutex m;
    return m;
  }

  std::unordered_set<std::string>& channelNames() {
    static std::unordered_set<std::string> names;
    return names;
  }

  bool claimChannelName(const std::string& name) {
    std::lock_guard<std::mutex> lock(channelNameMutex());
    return channelNames().insert(name).second;
  }

  void releaseChannelName(const std::string& name) {
    std::lock_guard<std::mutex> lock(channelNameMutex());
    channelNames().erase(name);
  }
} // namespace

YSE::channel::channel() : volume(1.f), allowVirtual(true), pimpl(nullptr) {}

YSE::channel::~channel() {
  unregisterFromBus();
  if (pimpl != nullptr) {
    pimpl->removeInterface();
    pimpl = nullptr;
  }
}

YSE::channel& YSE::channel::create(const char* name, channel& parent) {
  assert(pimpl == nullptr); // make sure we don't get called twice
  this->logName = name;

  pimpl = CHANNEL::Manager().addImplementation(this);
  pimpl->parent = parent.pimpl;
  CHANNEL::Manager().setup(pimpl);
  return *this;
}

void YSE::channel::createGlobal() {
  assert(pimpl == nullptr); // make sure we don't get called twice
  this->logName = "Master channel";

  // the global channel will be created instantly because no audio
  // thread can be running before this is ready anyway
  pimpl = CHANNEL::Manager().addImplementation(this);
  CHANNEL::Manager().setMaster(pimpl);
}

YSE::channel& YSE::channel::setVolume(Flt value) {
  Clamp(value, 0.f, 1.f);
  CHANNEL::messageObject m;
  m.ID = CHANNEL::VOLUME;
  m.floatValue = value;
  pimpl->sendMessage(m);
  volume = value; // only used for getVolume
  return (*this);
}

Flt YSE::channel::getVolume() {
  return volume;
}

YSE::channel& YSE::channel::name(const std::string& n) {
  if (n == busName) return *this;
  unregisterFromBus();
  busName = n;
  registerOnBus();
  return *this;
}

void YSE::channel::registerOnBus() {
  if (busName.empty()) return;
  // No bus before System::init() or after System::close().
  if (!INTERNAL::Global().isActive()) return;

  if (!claimChannelName(busName)) {
    INTERNAL::LogImpl().emit(E_FILE_ERROR, "channel name '" + busName +
                                               "' is already in use; bus registration rejected");
    return;
  }
  busOwner = true;

  using YSE::INTERNAL::BusValue;
  // Callback fires on the main thread (synchronous T_GUI publish or the
  // drainPending() tick). isValid() guards against a value arriving before
  // create() or after the channel is gone.
  busVolumeHandle =
      INTERNAL::Bus().subscribe("channel." + busName + ".volume", [this](const BusValue& v) {
        if (auto* f = std::get_if<float>(&v)) {
          if (isValid()) setVolume(*f);
        } else if (auto* i = std::get_if<int>(&v)) {
          if (isValid()) setVolume(static_cast<float>(*i));
        }
      });
}

void YSE::channel::unregisterFromBus() {
  if (!busOwner) return;
  // A channel destructed after System::close() must not touch the torn-down
  // bus; the name registry is independent and always safe to update.
  if (INTERNAL::Global().isActive() && busVolumeHandle != 0)
    INTERNAL::Bus().unsubscribe(busVolumeHandle);
  busVolumeHandle = 0;
  releaseChannelName(busName);
  busOwner = false;
}

YSE::channel& YSE::channel::moveTo(channel& parent) {
  CHANNEL::messageObject m;
  m.ID = CHANNEL::MOVE;
  m.ptrValue = &parent;
  pimpl->sendMessage(m);
  return (*this);
}

YSE::channel& YSE::channel::setVirtual(Bool value) {
  CHANNEL::messageObject m;
  m.ID = CHANNEL::VIRTUAL;
  m.boolValue = true;
  pimpl->sendMessage(m);
  allowVirtual = value;
  return (*this);
}

bool YSE::channel::getVirtual() {
  return allowVirtual;
}

bool YSE::channel::isValid() {
  return pimpl != nullptr;
}

YSE::channel& YSE::channel::attachReverb() {
  CHANNEL::messageObject m;
  m.ID = CHANNEL::ATTACH_REVERB;
  m.boolValue = true;
  pimpl->sendMessage(m);
  return (*this);
}

int YSE::channel::getNumOutputs() {
  if (pimpl == nullptr) return 0;
  return pimpl->getNumOutputs();
}

float YSE::channel::getPeakLinearPre() {
  if (pimpl == nullptr) return 0.f;
  return pimpl->getPeakLinearPreCombined();
}

float YSE::channel::getPeakLinearPost() {
  if (pimpl == nullptr) return 0.f;
  return pimpl->getPeakLinearPostCombined();
}

float YSE::channel::getPeakDbPre() {
  return linearToDb(getPeakLinearPre());
}

float YSE::channel::getPeakDbPost() {
  return linearToDb(getPeakLinearPost());
}

float YSE::channel::getPeakLinearPre(int outputIdx) {
  if (pimpl == nullptr) return 0.f;
  return pimpl->getPeakLinearPre(outputIdx);
}

float YSE::channel::getPeakLinearPost(int outputIdx) {
  if (pimpl == nullptr) return 0.f;
  return pimpl->getPeakLinearPost(outputIdx);
}

float YSE::channel::getPeakDbPre(int outputIdx) {
  return linearToDb(getPeakLinearPre(outputIdx));
}

float YSE::channel::getPeakDbPost(int outputIdx) {
  return linearToDb(getPeakLinearPost(outputIdx));
}

YSE::channel& YSE::ChannelMaster() {
  return CHANNEL::Manager().master();
}

YSE::channel& YSE::ChannelFX() {
  return CHANNEL::Manager().FX();
}

YSE::channel& YSE::ChannelMusic() {
  return CHANNEL::Manager().music();
}

YSE::channel& YSE::ChannelAmbient() {
  return CHANNEL::Manager().ambient();
}

YSE::channel& YSE::ChannelVoice() {
  return CHANNEL::Manager().voice();
}

YSE::channel& YSE::ChannelGui() {
  return CHANNEL::Manager().gui();
}
