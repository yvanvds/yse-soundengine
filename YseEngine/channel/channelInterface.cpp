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
  // Drop this return from the control-thread wiring graph so cycle checks and
  // generation indexing for the surviving returns stay correct. Guarded on the
  // active engine (like unregisterFromBus): after System::close() the graph is
  // already torn down and the peer impls are gone, so recomputing/posting would
  // be unsafe. The impl-side teardown (unlink from the returns list, sever the
  // back-reference registry) runs separately on the audio thread via the
  // OBJECT_RELEASE path once removeInterface() nulls the head. (issue #165)
  if (_isReturn && pimpl != nullptr && INTERNAL::Global().isActive()) {
    CHANNEL::Manager().unregisterReturnGraph(pimpl);
  }
  if (pimpl != nullptr) {
    pimpl->removeInterface();
    pimpl = nullptr;
  }
}

YSE::channel& YSE::channel::create(const char* name, channel& parent, int sendSlots) {
  assert(pimpl == nullptr); // make sure we don't get called twice
  this->logName = name;

  _sendSlots = sendSlots < 0 ? 0 : sendSlots;
  _sends.assign((std::size_t)_sendSlots, SendMirror{});

  pimpl = CHANNEL::Manager().addImplementation(this);
  pimpl->parent = parent.pimpl;
  // Control-thread writes applied before the impl is handed to the slow/audio
  // threads (mirrors `parent`); setup() reads sendSlotCount on the slow pool.
  pimpl->sendSlotCount = _sendSlots;
  CHANNEL::Manager().setup(pimpl);
  return *this;
}

YSE::channel& YSE::channel::makeReturn(const char* name, int sendSlots) {
  assert(pimpl == nullptr); // make sure we don't get called twice
  this->logName = name;

  _isReturn = true;
  _sendSlots = sendSlots < 0 ? 0 : sendSlots;
  _sends.assign((std::size_t)_sendSlots, SendMirror{});

  pimpl = CHANNEL::Manager().addImplementation(this);
  // A return folds into master and is excluded from the source tree. Setting
  // parent to master keeps the impl's release/destructor guards well-defined
  // (they short-circuit on connectedToParent, which stays false for returns).
  pimpl->parent = CHANNEL::Manager().master().pimpl;
  pimpl->isReturn = true;
  pimpl->sendSlotCount = _sendSlots;
  CHANNEL::Manager().setup(pimpl);
  // Register as a node in the control-thread wiring graph so return→return
  // cycle checks and generation indexing can see it.
  CHANNEL::Manager().registerReturnGraph(pimpl);
  return *this;
}

bool YSE::channel::isReturn() const {
  return _isReturn;
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

YSE::channel& YSE::channel::setDSP(YSE::DSP::dspObject* value) {
  if (_dsp != value) {
    _dsp = value;
    CHANNEL::messageObject m;
    m.ID = CHANNEL::ATTACH_DSP;
    m.ptrValue = value;
    pimpl->sendMessage(m);
  }
  return (*this);
}

YSE::DSP::dspObject* YSE::channel::getDSP() {
  return _dsp;
}

YSE::channel& YSE::channel::send(int slot, channel& returnBus, float level, bool preFader) {
  if (pimpl == nullptr) return *this;
  if (slot < 0 || slot >= _sendSlots) {
    INTERNAL::LogImpl().emit(E_ERROR, "channel send: slot index out of range; ignored");
    return *this;
  }
  if (!returnBus._isReturn || returnBus.pimpl == nullptr) {
    INTERNAL::LogImpl().emit(E_ERROR, "channel send: target is not a return bus; ignored");
    return *this;
  }
  if (returnBus.pimpl == pimpl) {
    INTERNAL::LogImpl().emit(E_ERROR, "channel send: a channel cannot send to itself; ignored");
    return *this;
  }

  // A return→return edge must keep the routing acyclic. Validate BEFORE the old
  // edge for this slot is removed, so a rejected wiring leaves the graph
  // untouched (the temporary presence of both the old and new edge is legal —
  // a return may send to several returns).
  bool newGraphEdge = false;
  if (_isReturn) {
    if (!CHANNEL::Manager().tryAddReturnEdge(pimpl, returnBus.pimpl)) {
      INTERNAL::LogImpl().emit(
          E_ERROR, "channel send: rejected — would create a cycle in the return graph");
      return *this;
    }
    newGraphEdge = true;
  }

  // Accepted. Drop the slot's previous return→return edge, if any.
  SendMirror& m = _sends[slot];
  if (m.graphEdge && m.targetImpl != nullptr) {
    CHANNEL::Manager().removeReturnEdge(pimpl, m.targetImpl);
  }
  m.target = &returnBus;
  m.targetImpl = returnBus.pimpl;
  m.level = level;
  m.preFader = preFader;
  m.graphEdge = newGraphEdge;

  // Post the wiring to the audio thread: ADD_SEND links the slot + back-reference
  // (ramp reset to silence), then SEND_LEVEL sets the target level so it fades in.
  {
    CHANNEL::messageObject msg;
    msg.ID = CHANNEL::ADD_SEND;
    msg.send.target = returnBus.pimpl;
    msg.send.slot = slot;
    msg.send.preFader = preFader;
    pimpl->sendMessage(msg);
  }
  {
    CHANNEL::messageObject msg;
    msg.ID = CHANNEL::SEND_LEVEL;
    msg.sendLevel.slot = slot;
    msg.sendLevel.level = level;
    pimpl->sendMessage(msg);
  }
  return *this;
}

YSE::channel& YSE::channel::setSendLevel(int slot, float level) {
  if (pimpl == nullptr) return *this;
  if (slot < 0 || slot >= _sendSlots) return *this;
  _sends[slot].level = level;
  CHANNEL::messageObject msg;
  msg.ID = CHANNEL::SEND_LEVEL;
  msg.sendLevel.slot = slot;
  msg.sendLevel.level = level;
  pimpl->sendMessage(msg);
  return *this;
}

YSE::channel& YSE::channel::clearSend(int slot) {
  if (pimpl == nullptr) return *this;
  if (slot < 0 || slot >= _sendSlots) return *this;
  SendMirror& m = _sends[slot];
  if (m.graphEdge && m.targetImpl != nullptr) {
    CHANNEL::Manager().removeReturnEdge(pimpl, m.targetImpl);
  }
  m = SendMirror{};
  CHANNEL::messageObject msg;
  msg.ID = CHANNEL::REMOVE_SEND;
  msg.uintValue = (UInt)slot;
  pimpl->sendMessage(msg);
  return *this;
}

float YSE::channel::getSendLevel(int slot) const {
  if (slot < 0 || slot >= _sendSlots) return 0.f;
  return _sends[slot].level;
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
