/*
  ==============================================================================

    sfzSampler.cpp
    A polyphonic SFZ instrument played by MIDI-style notes (issue #1159).

  ==============================================================================
*/

#include "sfzSampler.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../../synth/samplerVoice.hpp"
#include "../sfzModel.hpp"
#include "../smoother.hpp"

using namespace YSE::DSP;
using YSE::SYNTH::samplerInstrument;
using YSE::SYNTH::samplerVoice;

namespace {

  // The volume smoother's time constant, in seconds.
  constexpr Flt VOLUME_TAU = 0.01f;

  UInt ClampVoices(UInt count) {
    return count < 1 ? 1 : (count > sfzSampler::MAX_VOICES ? sfzSampler::MAX_VOICES : count);
  }

  Int ClampKey(Int key) {
    return key < 0 ? 0 : (key > 127 ? 127 : key);
  }

  // Floor division: the period a scale step lies in.
  Int FloorDiv(Int a, Int b) {
    Int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
  }

} // namespace

// One voice's place in the pool: what the audio side knows about its note.
struct sfzSampler::slot {
  SOUND_STATUS intent = SS_STOPPED;
  Int note = -1;
  std::uint64_t age = 0; // when its note started
  bool held = false; // the key is down
  bool pedalHeld = false; // released while the pedal was down
  bool releaseAfterStart = false; // released before its first block
  bool fresh = false; // started, not rendered yet
  Flt left = 1.f;
  Flt right = 1.f;
};

// An instrument with its voices, built on the control side and handed over
// whole. Made and freed off the audio thread.
struct sfzSampler::pool {
  std::shared_ptr<samplerInstrument> instrument;
  std::vector<std::unique_ptr<samplerVoice>> voices;
  std::vector<slot> slots;
};

// ─── construction ─────────────────────────────────────────────────────────────

sfzSampler::sfzSampler(UInt voices) : voiceCount(ClampVoices(voices)) {
  outLeft = 0.f;
  outRight = 0.f;
  Retune();
}

sfzSampler::~sfzSampler() {
  delete current; // NOLINT(cppcoreguidelines-owning-memory): the handoff's pools
  delete incoming.load(std::memory_order_acquire); // NOLINT(cppcoreguidelines-owning-memory)
  delete retired.load(std::memory_order_acquire); // NOLINT(cppcoreguidelines-owning-memory)
}

// ─── the control side ─────────────────────────────────────────────────────────

bool sfzSampler::Publish(std::shared_ptr<samplerInstrument> playable, const std::string& name,
                         UInt count) {
  auto* next = new pool(); // NOLINT(cppcoreguidelines-owning-memory): handed over
  next->instrument = playable;
  samplerVoice prototype(1);
  prototype.setInstrument(playable);
  next->voices.reserve(count);
  for (UInt i = 0; i < count; ++i)
    next->voices.emplace_back(static_cast<samplerVoice*>(prototype.clone()));
  next->slots.resize(count);

  // The pool the audio side let go has somewhere to be freed: here.
  delete retired.exchange(nullptr, // NOLINT(cppcoreguidelines-owning-memory)
                          std::memory_order_acq_rel);
  // A pool the audio side has not taken yet is replaced: the last one wins.
  delete incoming.exchange(next, // NOLINT(cppcoreguidelines-owning-memory)
                           std::memory_order_acq_rel);

  playingFile = name;
  regionCount.store(static_cast<UInt>(playable->model.regions.size()), std::memory_order_release);
  playing = std::move(playable);
  changes.fetch_add(1, std::memory_order_acq_rel);
  return true;
}

bool sfzSampler::load(const std::string& path) {
  // The file and its samples are read outside the lock: a slow disk keeps
  // no other control call waiting.
  auto playable = std::make_shared<samplerInstrument>();
  playable->model = loadSFZ(path);
  if (!playable->load()) return false;
  const std::lock_guard<std::mutex> lock(edit);
  lastPath = path;
  return Publish(std::move(playable), path, voiceCount.load(std::memory_order_acquire));
}

bool sfzSampler::instrument(std::shared_ptr<samplerInstrument> playable, const std::string& name) {
  if (playable == nullptr || !playable->valid()) return false;
  const std::lock_guard<std::mutex> lock(edit);
  return Publish(std::move(playable), name, voiceCount.load(std::memory_order_acquire));
}

bool sfzSampler::reload() {
  std::string path;
  {
    const std::lock_guard<std::mutex> lock(edit);
    path = lastPath;
  }
  if (path.empty()) return false;
  return load(path);
}

bool sfzSampler::voices(UInt count) {
  count = ClampVoices(count);
  const std::lock_guard<std::mutex> lock(edit);
  if (count == voiceCount.load(std::memory_order_acquire)) return false;
  voiceCount.store(count, std::memory_order_release);
  if (playing != nullptr) Publish(playing, playingFile, count);
  return true;
}

std::string sfzSampler::file() const {
  const std::lock_guard<std::mutex> lock(edit);
  return playingFile;
}

void sfzSampler::collect() {
  const std::lock_guard<std::mutex> lock(edit);
  delete retired.exchange(nullptr, // NOLINT(cppcoreguidelines-owning-memory)
                          std::memory_order_acq_rel);
}

// ─── the audio side ───────────────────────────────────────────────────────────

void sfzSampler::Take() {
  if (incoming.load(std::memory_order_relaxed) == nullptr) return;
  // Only while the retired slot is empty, so the old pool has somewhere to go.
  if (retired.load(std::memory_order_acquire) != nullptr) return;
  pool* next = incoming.exchange(nullptr, std::memory_order_acq_rel);
  if (next == nullptr) return;
  retired.store(current, std::memory_order_release);
  current = next;
  sounding = 0;
}

sfzSampler::slot* sfzSampler::Claim(UInt& index) {
  std::vector<slot>& slots = current->slots;
  const UInt count = static_cast<UInt>(slots.size());
  // A free voice; else the oldest released; else the oldest.
  UInt released = count;
  UInt oldest = 0;
  for (UInt i = 0; i < count; ++i) {
    const slot& s = slots[i];
    if (s.intent == SS_STOPPED) {
      index = i;
      return &slots[i];
    }
    if (!s.held && !s.pedalHeld && (released == count || s.age < slots[released].age)) released = i;
    if (s.age < slots[oldest].age) oldest = i;
  }
  index = released < count ? released : oldest;
  return &slots[index];
}

namespace {
  void Release(YSE::SOUND_STATUS& intent, bool& releaseAfterStart) {
    if (intent == YSE::SS_WANTSTOPLAY)
      releaseAfterStart = true; // it is heard for one block first
    else if (intent != YSE::SS_STOPPED)
      intent = YSE::SS_WANTSTOSTOP;
  }
} // namespace

void sfzSampler::noteOn(Int note, Flt velocity) {
  if (!(velocity > 0.f)) {
    noteOff(note);
    return;
  }
  if (note < 0 || note > 127) return;
  Take();
  if (current == nullptr || current->slots.empty()) return;
  UInt index = 0;
  slot& s = *Claim(index);
  samplerVoice& v = *current->voices[index];
  v.frequency(static_cast<Flt>(note));
  v.velocity(velocity > 1.f ? 1.f : velocity);
  v.tune(keyCents[static_cast<std::size_t>(note)]);
  v.pitchWheel(bend);
  s.intent = SS_WANTSTOPLAY;
  s.note = note;
  s.age = ++clock;
  s.held = true;
  s.pedalHeld = false;
  s.releaseAfterStart = false;
  s.fresh = true;
}

void sfzSampler::noteOff(Int note) {
  if (current == nullptr) return;
  for (slot& s : current->slots) {
    if (!s.held || s.note != note) continue;
    s.held = false;
    if (pedal) {
      s.pedalHeld = true;
      continue;
    }
    Release(s.intent, s.releaseAfterStart);
  }
}

void sfzSampler::sustain(bool down) {
  pedal = down;
  if (down || current == nullptr) return;
  for (slot& s : current->slots) {
    if (!s.pedalHeld) continue;
    s.pedalHeld = false;
    Release(s.intent, s.releaseAfterStart);
  }
}

void sfzSampler::pitchBend(Flt position) {
  if (!std::isfinite(position)) return;
  bend = position < -1.f ? -1.f : (position > 1.f ? 1.f : position);
  if (current == nullptr) return;
  for (auto& v : current->voices)
    v->pitchWheel(bend);
}

void sfzSampler::allNotesOff() {
  pedal = false;
  if (current == nullptr) return;
  for (slot& s : current->slots) {
    s.held = false;
    s.pedalHeld = false;
    Release(s.intent, s.releaseAfterStart);
  }
}

void sfzSampler::allSoundOff() {
  if (current == nullptr) return;
  for (slot& s : current->slots) {
    s.intent = SS_STOPPED;
    s.held = false;
    s.pedalHeld = false;
    s.releaseAfterStart = false;
    s.fresh = false;
  }
  sounding = 0;
}

void sfzSampler::midi(Int status, Int data1, Int data2) {
  data1 &= 127;
  data2 &= 127;
  switch (status & 0xF0) {
  case 0x90:
    if (data2 > 0)
      noteOn(data1, static_cast<Flt>(data2) / 127.f);
    else
      noteOff(data1);
    break;
  case 0x80:
    noteOff(data1);
    break;
  case 0xB0:
    if (data1 == 64)
      sustain(data2 >= 64);
    else if (data1 == 120)
      allSoundOff();
    else if (data1 == 121) {
      sustain(false);
      pitchBend(0.f);
    } else if (data1 == 123)
      allNotesOff();
    break;
  case 0xE0:
    pitchBend(static_cast<Flt>(((data2 << 7) | data1) - 8192) / 8192.f);
    break;
  default:
    break;
  }
}

sfzSampler& sfzSampler::volume(Flt decibels) {
  if (!std::isfinite(decibels)) return *this;
  volumeDb = decibels;
  gainTarget = std::pow(10.f, decibels / 20.f);
  return *this;
}

sfzSampler& sfzSampler::base(Flt hz) {
  if (!std::isfinite(hz) || hz <= 0.f) return *this;
  baseHz = hz;
  Retune();
  return *this;
}

sfzSampler& sfzSampler::reference(Int note) {
  referenceKey = ClampKey(note);
  Retune();
  return *this;
}

sfzSampler& sfzSampler::middle(Int note) {
  middleKey = ClampKey(note);
  Retune();
  return *this;
}

sfzSampler& sfzSampler::scale(const Flt* cents, UInt count) {
  if (cents == nullptr) count = 0;
  count = std::min(count, MAX_DEGREES);
  bool usable = count > 0;
  for (UInt i = 0; i < count && usable; ++i)
    usable = std::isfinite(cents[i]);
  if (usable) usable = cents[count - 1] > 0.f;
  degreeCount = usable ? count : 0;
  for (UInt i = 0; i < degreeCount; ++i)
    degrees[i] = cents[i];
  Retune();
  return *this;
}

// Each key's offset in cents from A440 equal temperament, the sampler
// voice's own tuning. 128 keys: called by the setters, never per block.
void sfzSampler::Retune() {
  const Int n = static_cast<Int>(degreeCount);
  const double period = n > 0 ? static_cast<double>(degrees[static_cast<std::size_t>(n - 1)]) : 0.0;
  // Cents of the key `steps` scale steps above the middle key.
  auto at = [&](Int steps) -> double {
    if (n == 0) return 100.0 * steps;
    const Int periods = FloorDiv(steps, n);
    const Int degree = steps - periods * n;
    const double within =
        degree == 0 ? 0.0 : static_cast<double>(degrees[static_cast<std::size_t>(degree - 1)]);
    return static_cast<double>(periods) * period + within;
  };
  const double reference = at(referenceKey - middleKey);
  const double shift = 1200.0 * std::log2(static_cast<double>(baseHz) / 440.0);
  for (Int key = 0; key < static_cast<Int>(KEYS); ++key) {
    const double cents = at(key - middleKey) - reference + shift - 100.0 * (key - 69);
    keyCents[static_cast<std::size_t>(key)] = static_cast<Flt>(cents);
  }
}

bool sfzSampler::scalaPitch(const char* text, std::size_t length, Flt& cents) {
  char word[64] = {};
  if (text == nullptr || length == 0 || length >= sizeof(word)) return false;
  std::memcpy(word, text, length);
  char* end = nullptr;
  if (std::memchr(word, '.', length) != nullptr) {
    const double value = std::strtod(word, &end);
    if (end != word + length || !std::isfinite(value)) return false;
    cents = static_cast<Flt>(value);
    return true;
  }
  const long numerator = std::strtol(word, &end, 10);
  long denominator = 1;
  if (end == word) return false;
  if (*end == '/') {
    char* start = end + 1;
    denominator = std::strtol(start, &end, 10);
    if (end == start) return false;
  }
  if (end != word + length || numerator <= 0 || denominator <= 0) return false;
  cents = static_cast<Flt>(
      1200.0 * std::log2(static_cast<double>(numerator) / static_cast<double>(denominator)));
  return true;
}

UInt sfzSampler::scalaDegrees(const std::string& text, Flt* cents, UInt capacity) {
  if (cents == nullptr) return 0;
  std::size_t pos = 0;
  int line = 0; // 0 the description, 1 the count, then the pitches
  long expected = 0;
  UInt read = 0;
  while (pos <= text.size()) {
    std::size_t next = text.find('\n', pos);
    if (next == std::string::npos) next = text.size();
    std::size_t begin = pos;
    std::size_t end = next;
    pos = next + 1;
    if (begin < end && text[begin] == '!') continue;
    if (line == 0) {
      line = 1; // the description, which may be empty
      continue;
    }
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0)
      begin++;
    std::size_t wordEnd = begin;
    while (wordEnd < end && std::isspace(static_cast<unsigned char>(text[wordEnd])) == 0)
      wordEnd++;
    if (begin == wordEnd) continue;
    if (line == 1) {
      char* stop = nullptr;
      const std::string count = text.substr(begin, wordEnd - begin);
      expected = std::strtol(count.c_str(), &stop, 10);
      if (stop == count.c_str() || *stop != '\0' || expected <= 0) return 0;
      line = 2;
      continue;
    }
    Flt value = 0.f;
    if (!scalaPitch(text.c_str() + begin, wordEnd - begin, value)) return 0;
    if (read < capacity) cents[read] = value;
    read++;
    if (static_cast<long>(read) == expected) break;
  }
  if (static_cast<long>(read) != expected) return 0;
  return std::min(read, capacity);
}

Flt sfzSampler::pitch(Int note) const {
  note = ClampKey(note);
  const double cents = 100.0 * (note - 69) + keyCents[static_cast<std::size_t>(note)];
  return static_cast<Flt>(440.0 * std::pow(2.0, cents / 1200.0));
}

YSE::DSP::buffer& sfzSampler::operator()(UInt length) {
  if (length > STANDARD_BUFFERSIZE) length = STANDARD_BUFFERSIZE;
  Take();
  outLeft.resize(length);
  outRight.resize(length);
  outLeft = 0.f;
  outRight = 0.f;

  if (gainRate != SAMPLERATE) {
    gainRate = SAMPLERATE;
    gainCoef = onePoleCoef(VOLUME_TAU, static_cast<Flt>(SAMPLERATE));
  }
  if (!gainStarted) {
    gain = gainTarget; // the volume set before the first block holds at once
    gainStarted = true;
  }

  sounding = 0;
  Flt* l = outLeft.getPtr();
  Flt* r = outRight.getPtr();
  if (current != nullptr) {
    const std::vector<sfzRegion>& regions = current->instrument->model.regions;
    const std::size_t count = current->slots.size();
    for (std::size_t i = 0; i < count; ++i) {
      slot& s = current->slots[i];
      if (s.intent == SS_STOPPED) continue;
      samplerVoice& v = *current->voices[i];
      v.samples[0].resize(length);
      v.process(s.intent);
      if (s.fresh) {
        s.fresh = false;
        // The pan of the first region the note plays.
        const int region = v.layerRegion(0);
        Flt pan = 0.f;
        if (region >= 0 && static_cast<std::size_t>(region) < regions.size())
          pan = regions[static_cast<std::size_t>(region)].pan / 100.f;
        pan = pan < -1.f ? -1.f : (pan > 1.f ? 1.f : pan);
        s.left = pan > 0.f ? 1.f - pan : 1.f;
        s.right = pan < 0.f ? 1.f + pan : 1.f;
        if (s.releaseAfterStart) {
          s.releaseAfterStart = false;
          if (s.intent != SS_STOPPED) s.intent = SS_WANTSTOSTOP;
        }
      }
      const Flt* in = v.samples[0].getPtr();
      for (UInt k = 0; k < length; ++k) {
        l[k] += in[k] * s.left;
        r[k] += in[k] * s.right;
      }
      if (s.intent == SS_STOPPED) {
        s.held = false;
        s.pedalHeld = false;
      } else {
        sounding++;
      }
    }
  }

  for (UInt k = 0; k < length; ++k) {
    gain = onePoleSmooth(gain, gainTarget, gainCoef);
    l[k] *= gain;
    r[k] *= gain;
  }
  return outLeft;
}
