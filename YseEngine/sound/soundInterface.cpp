/*
  ==============================================================================

    sound.cpp
    Created: 28 Jan 2014 11:50:15am
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"
#include "../patcher/patcherImplementation.h"
#include "../synth/synthInterface.hpp"

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

  // Registry of sounds with occlusion enabled (issue #209). The user occlusion
  // callback used to run inside SOUND::implementationObject::update(), which
  // executes on the audio callback thread — a real-time violation, since the
  // callback typically raycasts through game-world geometry (locks, allocation,
  // blocking I/O). Instead we track the occlusion-enabled interface objects here
  // and run their callbacks on the control thread in SOUND::updateOcclusion(),
  // delivering the result to the implementation over the existing message queue.
  //
  // Registration and iteration both happen on the application thread (occlusion
  // setter / destructor / System().update()); the mutex is a cheap defensive
  // guard against construction from multiple threads and never sits on the
  // audio path.
  std::mutex& occlusionMutex() {
    static std::mutex m;
    return m;
  }

  std::unordered_set<YSE::sound*>& occlusionSounds() {
    static std::unordered_set<YSE::sound*> s;
    return s;
  }

  void registerOcclusion(YSE::sound* s) {
    std::lock_guard<std::mutex> lock(occlusionMutex());
    occlusionSounds().insert(s);
  }

  void unregisterOcclusion(YSE::sound* s) {
    std::lock_guard<std::mutex> lock(occlusionMutex());
    occlusionSounds().erase(s);
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
  // Drop out of the control-thread occlusion registry before the interface goes
  // away, so updateOcclusion() can never touch a destroyed sound (issue #209).
  unregisterOcclusion(this);
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
  // create() refuses when the patcher is already owned by another sound
  // (issue #287); on refusal the impl is released and the sound stays invalid.
  if (pimpl->create(patch.pimpl, ch, volume)) {
    SOUND::Manager().setup(pimpl);
  } else {
    pimpl->setStatus(OBJECT_RELEASE);
    pimpl = nullptr;
  }
}

void YSE::sound::create(YSE::synth& synth, channel* ch, float volume) {
  assert(pimpl == nullptr);

  // Ensure the synth has a live implementation, then attach its aggregate
  // output source through the existing dspSourceObject render path. All
  // synth-specific work (drain / allocate / mix) happens inside that source's
  // process(), which the sound already knows how to call — no new audio-thread
  // path is opened (docs/design/synth_core.md §9).
  if (!synth.isValid()) synth.create();

  pimpl = SOUND::Manager().addImplementation(this);
  if (ch == nullptr) ch = &CHANNEL::Manager().master();
  // Attach as a pre-spatialized source (issue #169): the synth's aggregate now
  // produces a device-width, per-voice-panned bed which the sound plays straight
  // through without re-panning (docs/design/per_note_positioning.md §7).
  pimpl->create(synth.pimpl->getOutputSource(), ch, volume, /*preSpatialized=*/true);
  SOUND::Manager().setup(pimpl);
}

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
    // Enrol / withdraw from the control-thread occlusion driver (issue #209).
    // The driver only runs the user callback for sounds that are registered.
    if (value) {
      registerOcclusion(this);
    } else {
      unregisterOcclusion(this);
    }
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

void YSE::SOUND::updateOcclusion() {
  // Runs on the application (control) thread, driven once per System().update().
  // This is the whole point of issue #209: the user occlusion callback must not
  // execute on the audio callback thread, where a raycast's locks / allocations
  // / blocking I/O would cause priority-inversion stalls and dropouts.
  occlusionFunc cb = System().occlusionCallback();
  if (cb == nullptr) return;

  const Flt df = INTERNAL::Settings().distanceFactor;
  // Scale the listener position exactly as listenerImplementation::update()
  // does (newPos = pos * distanceFactor), but compute it here from the atomic
  // listener position so we never read the audio thread's non-atomic newPos.
  const Pos listenerScaled = Listener().pos() * df;

  // Snapshot the registry under the lock, then invoke callbacks with the lock
  // released. A user callback that (pathologically) creates or destroys a sound
  // would otherwise re-enter the registry mutex and deadlock, or mutate the set
  // mid-iteration.
  std::vector<YSE::sound*> snapshot;
  {
    std::lock_guard<std::mutex> lock(occlusionMutex());
    snapshot.assign(occlusionSounds().begin(), occlusionSounds().end());
  }

  for (YSE::sound* s : snapshot) {
    if (s->pimpl == nullptr) continue; // create() not (yet) succeeded
    Flt occ = cb(s->_pos * df, listenerScaled);
    Clamp(occ, 0.f, 1.f);
    // Only enqueue when the value changes — matches the compare-then-send
    // pattern of every other setter and keeps the SPSC queue near-empty when
    // occlusion is static.
    if (s->_occlusionValue != occ) {
      s->_occlusionValue = occ;
      SOUND::messageObject m;
      m.ID = SOUND::OCCLUSION_VALUE;
      m.floatValue = occ;
      s->pimpl->sendMessage(m);
    }
  }
}
