/*
  ==============================================================================

    sound.cpp
    Created: 28 Jan 2014 11:50:15am
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"
#include "../patcher/patcherImplementation.h"
// #include "../synth/synthInterface.hpp"

#include <mutex>
#include <unordered_set>
#include <variant>
#include <vector>

namespace {
  // Process-wide registry of sound names currently claimed as bus producers.
  // Naming happens on the main thread; the mutex is a cheap defensive guard
  // against construction from multiple threads and never sits on the audio
  // path. The "sound." prefix lives in its own namespace, independent of
  // channel names.
  std::mutex& soundNameMutex() {
    static std::mutex m;
    return m;
  }

  std::unordered_set<std::string>& soundNames() {
    static std::unordered_set<std::string> names;
    return names;
  }

  // Returns true when the name was free and is now claimed by the caller.
  bool claimSoundName(const std::string& name) {
    std::lock_guard<std::mutex> lock(soundNameMutex());
    return soundNames().insert(name).second;
  }

  void releaseSoundName(const std::string& name) {
    std::lock_guard<std::mutex> lock(soundNameMutex());
    soundNames().erase(name);
  }
} // namespace

YSE::sound::sound()
  : pimpl(nullptr),
    _pos(0.f),
    _spread(0),
    _volume(0.f),
    _speed(1.f),
    _size(0.f),
    _loop(0.f),
    _relative(false),
    _doppler(true),
    _pan2D(false),
    _occlusion(false),
    _fadeAndStopTime(0),
    _dsp(nullptr) {}

YSE::sound::~sound() {
  unregisterFromBus();
  if (pimpl != nullptr) {
    pimpl->removeInterface();
    Log().sendMessage("removed sound");
    pimpl = nullptr;
  }
}

YSE::sound& YSE::sound::name(const std::string& n) {
  if (n == _name) return *this;
  unregisterFromBus();
  _name = n;
  registerOnBus();
  return *this;
}

void YSE::sound::registerOnBus() {
  if (_name.empty()) return;
  // No bus before System::init() or after System::close(). Naming while the
  // engine is down is a silent no-op (mirrors the patcher gReceive contract).
  if (!INTERNAL::Global().isActive()) return;

  if (!claimSoundName(_name)) {
    INTERNAL::LogImpl().emit(E_FILE_ERROR, "sound name '" + _name +
                                               "' is already in use; bus registration rejected");
    return;
  }
  _busOwner = true;

  using YSE::INTERNAL::Bus;
  using YSE::INTERNAL::BusValue;
  const std::string base = "sound." + _name + ".";

  // The callbacks fire on the main thread (synchronous T_GUI publish or the
  // drainPending() tick). Each guards isValid() so values arriving before
  // create() — or after the sound is gone — are dropped instead of touching a
  // null implementation.
  _busHandles[0] = Bus().subscribe(base + "volume", [this](const BusValue& v) {
    if (auto* f = std::get_if<float>(&v)) {
      if (isValid()) volume(*f);
    } else if (auto* i = std::get_if<int>(&v)) {
      if (isValid()) volume(static_cast<float>(*i));
    }
  });
  _busHandles[1] = Bus().subscribe(base + "speed", [this](const BusValue& v) {
    if (auto* f = std::get_if<float>(&v)) {
      if (isValid()) speed(*f);
    } else if (auto* i = std::get_if<int>(&v)) {
      if (isValid()) speed(static_cast<float>(*i));
    }
  });
  _busHandles[2] = Bus().subscribe(base + "position", [this](const BusValue& v) {
    if (auto* vec = std::get_if<std::vector<float>>(&v)) {
      if (vec->size() == 3 && isValid()) pos(Pos((*vec)[0], (*vec)[1], (*vec)[2]));
    }
  });
}

void YSE::sound::unregisterFromBus() {
  if (!_busOwner) return;
  // Guard the bus access: a sound destructed after System::close() must not
  // touch the torn-down bus. The name registry is independent of the bus, so
  // releasing the name is always safe.
  if (INTERNAL::Global().isActive()) {
    for (auto& handle : _busHandles) {
      if (handle != 0) INTERNAL::Bus().unsubscribe(handle);
    }
  }
  for (auto& handle : _busHandles)
    handle = 0;
  releaseSoundName(_name);
  _busOwner = false;
}

void YSE::sound::create(const char* fileName, channel* ch, bool loop, float volume,
                        bool streaming) {
  assert(pimpl == nullptr);

  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();

  if (pimpl->create(fileName, ch, loop, volume, streaming)) {
    SOUND::Manager().setup(pimpl);
  } else {
    pimpl->setStatus(OBJECT_RELEASE);
    pimpl = nullptr;
  }
}

void YSE::sound::create(YSE::DSP::buffer& buffer, channel* ch, bool loop, float volume) {
  assert(pimpl == nullptr);

  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();

  pimpl->create(buffer, ch, loop, volume);
  SOUND::Manager().setup(pimpl);
}

void YSE::sound::create(MULTICHANNELBUFFER& buffer, channel* ch, bool loop, float volume) {
  assert(pimpl == nullptr);

  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();

  pimpl->create(buffer, ch, loop, volume);
  SOUND::Manager().setup(pimpl);
}

void YSE::sound::create(YSE::DSP::dspSourceObject& dsp, channel* ch, float volume) {
  assert(pimpl == nullptr);

  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();
  pimpl->create(dsp, ch, volume);
  SOUND::Manager().setup(pimpl);
}

void YSE::sound::create(YSE::patcher& patch, channel* ch, float volume) {
  assert(pimpl == nullptr);

  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();
  pimpl->create(patch.pimpl, ch, volume);
  SOUND::Manager().setup(pimpl);
}

/*YSE::sound& YSE::sound::create(SYNTH::interfaceObject & synth, channel *ch, Flt volume) {
  assert(pimpl == nullptr);
  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();
  pimpl->create(synth.pimpl, ch, volume);
  SOUND::Manager().setup(pimpl);
  return *this;
}*/

Bool YSE::sound::isValid() {
  return pimpl != nullptr;
}

void YSE::sound::pos(const Pos& v) {
  if (_pos != v) {
    _pos = v;
    SOUND::messageObject m;
    m.ID = SOUND::POSITION;
    m.vecValue[0] = v.x;
    m.vecValue[1] = v.y;
    m.vecValue[2] = v.z;
    pimpl->sendMessage(m);
  }
}

YSE::Pos YSE::sound::pos() {
  return _pos;
}

void YSE::sound::spread(Flt value) {
  Clamp(value, 0.f, 1.f);
  if (_spread != value) {
    _spread = value;
    SOUND::messageObject m;
    m.ID = SOUND::SPREAD;
    m.floatValue = value;
    pimpl->sendMessage(m);
  }
}

Flt YSE::sound::spread() {
  return _spread;
}

void YSE::sound::volume(Flt value, UInt time) {
  Clamp(value, 0.f, 1.f);
  if (_volume != value) {
    _volume = value;
    SOUND::messageObject m;
    m.ID = SOUND::VOLUME_VALUE;
    m.floatValue = value;
    pimpl->sendMessage(m);

    if (time > 0) {
      SOUND::messageObject m2;
      m2.ID = SOUND::VOLUME_TIME;
      m2.uintValue = time;
      pimpl->sendMessage(m2);
    }
  }
}

Flt YSE::sound::volume() {
  return _volume;
}

void YSE::sound::speed(Flt value) {
  if (_speed != value) {
    _speed = value;
    SOUND::messageObject m;
    m.ID = SOUND::SPEED;
    m.floatValue = value;
    pimpl->sendMessage(m);
  }
}

Flt YSE::sound::speed() {
  return _speed;
}

void YSE::sound::size(Flt value) {
  if (_size != value) {
    _size = value;
    SOUND::messageObject m;
    m.ID = SOUND::SIZE;
    m.floatValue = value;
    pimpl->sendMessage(m);
  }
}

Flt YSE::sound::size() {
  return _size;
}

void YSE::sound::looping(Bool value) {
  if (_loop != value) {
    _loop = value;
    SOUND::messageObject m;
    m.ID = SOUND::LOOP;
    m.boolValue = value;
    pimpl->sendMessage(m);
  }
}

Bool YSE::sound::looping() {
  return _loop;
}

void YSE::sound::play() {
  SOUND::messageObject m;
  m.ID = SOUND::INTENT;
  m.intentValue = SI_PLAY;
  pimpl->sendMessage(m);
}

void YSE::sound::pause() {
  SOUND::messageObject m;
  m.ID = SOUND::INTENT;
  m.intentValue = SI_PAUSE;
  pimpl->sendMessage(m);
}

void YSE::sound::stop() {
  SOUND::messageObject m;
  m.ID = SOUND::INTENT;
  m.intentValue = SI_STOP;
  pimpl->sendMessage(m);
}

void YSE::sound::toggle() {
  SOUND::messageObject m;
  m.ID = SOUND::INTENT;
  m.intentValue = SI_TOGGLE;
  pimpl->sendMessage(m);
}

void YSE::sound::restart() {
  SOUND::messageObject m;
  m.ID = SOUND::INTENT;
  m.intentValue = SI_RESTART;
  pimpl->sendMessage(m);
}

Bool YSE::sound::isPlaying() {
  return (pimpl->_head_status == SS_PLAYING || pimpl->_head_status == SS_PLAYING_FULL_VOLUME);
}

Bool YSE::sound::isPaused() {
  return pimpl->_head_status == SS_PAUSED;
}

Bool YSE::sound::isStopped() {
  return pimpl->_head_status == SS_STOPPED;
}

void YSE::sound::occlusion(Bool value) {
  if (_occlusion != value) {
    _occlusion = value;
    SOUND::messageObject m;
    m.ID = SOUND::OCCLUSION;
    m.boolValue = value;
    pimpl->sendMessage(m);
  }
}

Bool YSE::sound::occlusion() {
  return _occlusion;
}

Bool YSE::sound::isStreaming() {
  return pimpl->_head_streaming;
}

void YSE::sound::setDSP(YSE::DSP::dspObject* value) {
  if (_dsp != value) {
    _dsp = value;
    SOUND::messageObject m;
    m.ID = SOUND::DSP;
    m.ptrValue = value;
    pimpl->sendMessage(m);
  }
}

YSE::DSP::dspObject* YSE::sound::getDSP() {
  return _dsp;
}

void YSE::sound::time(Flt value) {
  // don't compare with local time var in this case because
  // that value is set by the implementation
  SOUND::messageObject m;
  m.ID = SOUND::TIME;
  m.floatValue = value;
  pimpl->sendMessage(m);
}

Flt YSE::sound::time() {
  return pimpl->_head_time;
}

UInt YSE::sound::length() {
  return pimpl->_head_length;
}

void YSE::sound::relative(Bool value) {
  if (_relative != value) {
    _relative = value;
    SOUND::messageObject m;
    m.ID = SOUND::RELATIVE;
    m.boolValue = value;
    pimpl->sendMessage(m);
  }
}

Bool YSE::sound::relative() {
  return _relative;
}

void YSE::sound::doppler(Bool value) {
  if (_doppler != value) {
    _doppler = value;
    SOUND::messageObject m;
    m.ID = SOUND::DOPPLER;
    m.boolValue = value;
    pimpl->sendMessage(m);
  }
}

Bool YSE::sound::doppler() {
  return _doppler;
}

void YSE::sound::pan2D(Bool value) {
  if (_pan2D != value) {
    _relative = value;
    _doppler = !value;
    _pan2D = value;
    SOUND::messageObject m;
    m.ID = SOUND::PAN2D;
    m.boolValue = value;
    pimpl->sendMessage(m);
  }
}

Bool YSE::sound::pan2D() {
  return _pan2D;
}

Bool YSE::sound::isReady() {
  if (pimpl && pimpl->getStatus() == OBJECT_READY) return true;
  return false;
}

void YSE::sound::fadeAndStop(UInt time) {
  SOUND::messageObject m;
  m.ID = SOUND::FADE_AND_STOP;
  m.uintValue = time;
  pimpl->sendMessage(m);
}

void YSE::sound::moveTo(channel& target) {
  if (_parent != &target) {
    _parent = &target;
    SOUND::messageObject m;
    m.ID = SOUND::MOVE;
    m.ptrValue = _parent;
    pimpl->sendMessage(m);
  }
}
