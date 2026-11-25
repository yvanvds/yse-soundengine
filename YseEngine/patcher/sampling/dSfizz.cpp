#include "dSfizz.h"
#include "sfCues.h"
#include "../../implementations/logImplementation.h"
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>

using namespace YSE::PATCHER;
using YSE::DSP::sfzSampler;

namespace {

  // What a task does. Its fileResult::tag is TAG_BASE plus this.
  enum class SF_OP : std::uint8_t {
    READ,
    READAGAIN,
    VOICES,
    SCALENAME,
    COLLECT,
  };
  constexpr int TAG_BASE = 1159000;
  constexpr int OP_COUNT = 5;

  // The samplers tasks find by id. Weak: a sampler lives while its object
  // or a task holds it. Control and background threads only (it locks).
  struct samplerTable {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::weak_ptr<sfzSampler>> samplers;
    std::uint64_t next = 1;
  };

  samplerTable& Samplers() {
    // Never destroyed, so an object freed during static destruction still
    // finds it.
    static samplerTable* const instance =
        new samplerTable(); // NOLINT(cppcoreguidelines-owning-memory): leaked on purpose
    return *instance;
  }

  std::shared_ptr<sfzSampler> FindSampler(std::uint64_t id) {
    samplerTable& t = Samplers();
    const std::lock_guard<std::mutex> lock(t.mutex);
    auto it = t.samplers.find(id);
    return it == t.samplers.end() ? nullptr : it->second.lock();
  }

  int ClampKey(int key) {
    return key < 0 ? 0 : (key > 127 ? 127 : key);
  }

  // A word that is a number written with a decimal point or an exponent.
  bool IsFloatWord(const char* text, std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end; ++i)
      if (text[i] == '.' || text[i] == 'e' || text[i] == 'E') return true;
    return false;
  }

} // namespace

// The record a request becomes. Copied whole into a scheduler slot, so it
// stays trivially copyable. A scalename task answers in `degrees`.
struct dSfizz::sfTask {
  std::uint64_t id = 0;
  SF_OP op = SF_OP::COLLECT;
  std::int32_t count = 0;
  char name[fileScheduler::PATH_CAPACITY] = {};
  float degrees[sfzSampler::MAX_DEGREES] = {};
};

#define className dSfizz

CONSTRUCT_DSP() {
  voices = static_cast<int>(sfzSampler::DEFAULT_VOICES);
  volume = 0.f;
  base = 440.f;
  ref = 69;
  mid = 60;
  velocity = 0.f;
  degreeCount = 0;

  sampler = std::make_shared<sfzSampler>();
  {
    samplerTable& t = Samplers();
    const std::lock_guard<std::mutex> lock(t.mutex);
    id = t.next++;
    t.samplers[id] = sampler;
  }

  ADD_IN_0;
  REG_INT_IN(SetKey);
  REG_FLOAT_IN(SetKeyFloat);
  REG_LIST_IN(SetList);
  ADD_IN_1;
  REG_INT_IN(SetVelocity);
  REG_FLOAT_IN(SetVelocityFloat);

  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  REG_PARM_PARSE;
  ADD_PARAM(filename);
  ADD_PARAM(voices);
  ADD_PARAM(volume);
  ADD_PARAM(base);
  ADD_PARAM(ref);
  ADD_PARAM(mid);

  ADD_DESCRIPTION("Plays an SFZ sampled instrument from keys and velocities or MIDI messages, "
                  "in stereo: velocity layers, loops, round robin and envelopes as the file "
                  "describes them, with a voice limit, volume and Scala tunings.");
  ADD_CATEGORY(pCategory::SAMPLING);
  INLET_DOC(0, "key",
            "A key number, 0-127, played at the velocity inlet 1 holds: 0 releases the key. A "
            "float is truncated; a list is 'key velocity'. Also takes 'read <file.sfz>', "
            "'readagain', 'midievent <status> <data1> [data2]', 'voices <n>', 'volume <dB>', "
            "'base <Hz>', 'ref <key>', 'mid <key>', 'scale <count> <pitches...>' and "
            "'scalename <file.scl>'.",
            "0-127");
  INLET_DOC(1, "velocity",
            "The velocity of the next key, 0-127: stored, nothing plays. 0 makes the next key a "
            "release. A float is truncated.",
            "0-127");
  OUTLET_DOC(0, "left", "The left channel of the instrument.", "signal");
  OUTLET_DOC(1, "right", "The right channel of the instrument.", "signal");
  PARAM_DOC("filename", "",
            "The .sfz file to load when the object is made. May be left out: a first argument "
            "that is a number is the voice count.",
            "file name");
  PARAM_DOC("voices", "64",
            "How many notes sound at once; a further note takes over the oldest released one, "
            "else the oldest.",
            "1 to 1024");
  PARAM_DOC("volume", "0", "The output volume in dB: 0 unchanged, -70 near silence.", "any number");
  PARAM_DOC("base", "440", "The frequency, in Hz, of the key ref.", "above 0");
  PARAM_DOC("ref", "69", "The key that sounds at base Hz.", "0-127");
  PARAM_DOC("mid", "60", "The key a scale starts on.", "0-127");
}

dSfizz::~dSfizz() {
  samplerTable& t = Samplers();
  const std::lock_guard<std::mutex> lock(t.mutex);
  t.samplers.erase(id);
}

// Control thread, on an object no audio thread has seen yet (a change of
// argument rebuilds the object).
PARM_PARSE() {
  // The file may be left out: then the numbers move up one place. Read
  // again from the argument string, so a number missing at the end keeps its
  // default instead of the one before it.
  float number = 0.f;
  if (!filename.empty() && ReadNumericToken(filename, number)) {
    filename.clear();
    voices = static_cast<int>(sfzSampler::DEFAULT_VOICES);
    volume = 0.f;
    base = 440.f;
    ref = 69;
    mid = 60;
    const std::string& args = GetParams();
    const char* text = args.c_str();
    std::size_t pos = 0;
    for (int field = 0; field < 5; ++field) {
      std::size_t begin = 0;
      std::size_t end = 0;
      sfcues::NextToken(text, args.size(), pos, begin, end);
      if (!sfcues::Number(text, begin, end, number)) break;
      pos = end;
      switch (field) {
      case 0:
        voices = static_cast<int>(number);
        break;
      case 1:
        volume = number;
        break;
      case 2:
        base = number;
        break;
      case 3:
        ref = static_cast<int>(number);
        break;
      default:
        mid = static_cast<int>(number);
        break;
      }
    }
  }
  // A number that slid into the wrong place, or a missing one: defaults.
  if (!(base.load() > 0.f) || !std::isfinite(base.load())) base = 440.f;
  if (!std::isfinite(volume.load())) volume = 0.f;
  ref = ClampKey(ref.load());
  mid = ClampKey(mid.load());
  int count = voices.load();
  const int most = static_cast<int>(sfzSampler::MAX_VOICES);
  count = count < 1 ? 1 : (count > most ? most : count);
  voices = count;
  sampler->voices(static_cast<UInt>(count)); // no instrument yet: nothing allocates
  pendingFile = filename;
}

void dSfizz::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler every file message goes through, built here so a message
  // on the audio thread finds it.
  EnableFileIO();
  // The file argument waits until now, the first moment the object knows
  // whether its patcher's host resolves paths (issue #1255).
  if (newParent != nullptr) LoadArgumentFile();
}

void dSfizz::LoadArgumentFile() {
  if (pendingFile.empty()) return;
  const std::string file = std::move(pendingFile);
  pendingFile.clear();

  // While the host resolves paths, the name is the host's to place: read it
  // the way a `read` message does.
  fileScheduler* io = FileIO();
  if (io != nullptr && io->HasResolver()) {
    if (!FileTask(static_cast<int>(SF_OP::READ), file.c_str(), file.size(), 0))
      failed.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  // Synchronous: this is the control thread, and a patch that has just been
  // loaded should have its instrument.
  if (!sampler->load(file)) {
    failed.fetch_add(1, std::memory_order_relaxed);
    YSE::INTERNAL::LogImpl().emit(YSE::E_ERROR, "~sfizz: cannot read \"" + file + "\"");
  }
}

// ─── requests ─────────────────────────────────────────────────────────────────

bool dSfizz::Submit(sfTask& task, bool resolve) {
  fileScheduler* io = FileIO();
  if (io == nullptr) {
    failed.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  task.id = id;
  const int tag = TAG_BASE + static_cast<int>(task.op);
  // Notes wait for this one: counted before it can complete.
  const bool counted = task.op != SF_OP::COLLECT;
  if (counted) inFlight.fetch_add(1, std::memory_order_acq_rel);
  const std::size_t length = std::strlen(task.name);
  const bool ok = resolve && length > 0
                      ? io->RequestPathTask(this, tag, PA_READ, task.name, length,
                                            &dSfizz::RunPathTask, &task, sizeof(task))
                      : io->RequestTask(this, tag, &dSfizz::RunTask, &task, sizeof(task));
  if (!ok && counted) {
    inFlight.fetch_sub(1, std::memory_order_acq_rel);
    failed.fetch_add(1, std::memory_order_relaxed);
  }
  return ok;
}

bool dSfizz::FileTask(int op, const char* text, std::size_t length, std::size_t pos) {
  std::size_t begin = 0;
  std::size_t end = 0;
  sfcues::NextToken(text, length, pos, begin, end);
  // Without a name Max opens a dialog; the engine has none.
  if (begin == end) return false;
  sfTask task;
  task.op = static_cast<SF_OP>(op);
  const std::size_t n = end - begin;
  if (n >= sizeof(task.name)) {
    failed.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  std::memcpy(task.name, text + begin, n);
  task.name[n] = '\0';
  return Submit(task, true);
}

void dSfizz::Push(const noteEvent& event) {
  if (!events.try_push(event)) dropped.fetch_add(1, std::memory_order_relaxed);
}

void dSfizz::Key(int key, float velocity127) {
  if (key < 0 || key > 127) return;
  noteEvent e;
  e.data1 = key;
  e.velocity = velocity127 > 0.f ? velocity127 / 127.f : 0.f;
  Push(e);
}

INT_IN(SetKey) {
  (void)inlet;
  (void)thread;
  Key(value, velocity.load());
}

FLOAT_IN(SetKeyFloat) {
  (void)inlet;
  (void)thread;
  if (!std::isfinite(value)) return;
  Key(static_cast<int>(value), velocity.load());
}

// `key velocity`: the velocity is kept, then the key plays.
LIST_IN(SetList) {
  (void)inlet;
  (void)thread;
  const char* text = value.c_str();
  const std::size_t length = value.size();
  std::size_t b1 = 0;
  std::size_t e1 = 0;
  sfcues::NextToken(text, length, 0, b1, e1);
  float key = 0.f;
  if (!sfcues::Number(text, b1, e1, key)) return;
  std::size_t b2 = 0;
  std::size_t e2 = 0;
  sfcues::NextToken(text, length, e1, b2, e2);
  float vel = 0.f;
  if (sfcues::Number(text, b2, e2, vel)) velocity = static_cast<float>(static_cast<int>(vel));
  Key(static_cast<int>(key), velocity.load());
}

INT_IN(SetVelocity) {
  (void)inlet;
  (void)thread;
  velocity = static_cast<float>(value < 0 ? 0 : (value > 127 ? 127 : value));
}

FLOAT_IN(SetVelocityFloat) {
  (void)inlet;
  (void)thread;
  if (!std::isfinite(value)) return;
  const int v = static_cast<int>(value);
  velocity = static_cast<float>(v < 0 ? 0 : (v > 127 ? 127 : v));
}

// `midievent <status> <data1> [data2]`. A note's velocity written as a
// float is 0 to 1 (a high-resolution velocity).
void dSfizz::Midievent(const char* text, std::size_t length, std::size_t pos) {
  float bytes[3] = {0.f, 0.f, 0.f};
  bool isFloat = false;
  int read = 0;
  for (float& byte : bytes) {
    std::size_t begin = 0;
    std::size_t end = 0;
    sfcues::NextToken(text, length, pos, begin, end);
    if (!sfcues::Number(text, begin, end, byte)) break;
    if (read == 2) isFloat = IsFloatWord(text, begin, end);
    pos = end;
    read++;
  }
  if (read < 2) return;
  const int status = static_cast<int>(bytes[0]);
  noteEvent e;
  const int kind = status & 0xF0;
  if (isFloat && (kind == 0x90 || kind == 0x80)) {
    // A key at a high-resolution velocity; a note off is velocity 0.
    e.data1 = static_cast<int>(bytes[1]) & 127;
    e.velocity = kind == 0x90 && bytes[2] > 0.f ? (bytes[2] > 1.f ? 1.f : bytes[2]) : 0.f;
    Push(e);
    return;
  }
  if (status < 0x80 || status > 0xEF) return; // channel messages only
  e.status = status;
  e.data1 = static_cast<int>(bytes[1]);
  e.data2 = static_cast<int>(bytes[2]);
  Push(e);
}

// `scale <count> <pitch>...`: cents with a decimal point, ratios without.
void dSfizz::Scale(const char* text, std::size_t length, std::size_t pos) {
  std::size_t begin = 0;
  std::size_t end = 0;
  sfcues::NextToken(text, length, pos, begin, end);
  float number = 0.f;
  int count = 0;
  if (sfcues::Number(text, begin, end, number)) {
    count = static_cast<int>(number);
    pos = end;
  }
  const int most = static_cast<int>(sfzSampler::MAX_DEGREES);
  count = count < 0 ? 0 : (count > most ? most : count);
  int read = 0;
  while (read < count) {
    sfcues::NextToken(text, length, pos, begin, end);
    Flt cents = 0.f;
    if (!sfzSampler::scalaPitch(text + begin, end - begin, cents)) break;
    degrees[static_cast<std::size_t>(read)].store(cents, std::memory_order_relaxed);
    pos = end;
    read++;
  }
  // A short list is no scale: equal temperament.
  degreeCount.store(read == count ? count : 0, std::memory_order_relaxed);
  scaleVersion.fetch_add(1, std::memory_order_release);
}

// The word messages, on any thread. Read in place: nothing allocates.
MESSAGES() {
  (void)value;
  using sfcues::Is;
  using sfcues::NextToken;
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t begin = 0;
  std::size_t end = 0;
  NextToken(text, length, 0, begin, end);
  if (begin == end) return;

  if (Is(text, begin, end, "read")) {
    FileTask(static_cast<int>(SF_OP::READ), text, length, end);
    return;
  }
  if (Is(text, begin, end, "scalename")) {
    FileTask(static_cast<int>(SF_OP::SCALENAME), text, length, end);
    return;
  }
  if (Is(text, begin, end, "readagain")) {
    sfTask task;
    task.op = SF_OP::READAGAIN;
    Submit(task, false);
    return;
  }
  if (Is(text, begin, end, "midievent")) {
    Midievent(text, length, end);
    return;
  }
  if (Is(text, begin, end, "scale")) {
    Scale(text, length, end);
    return;
  }

  std::size_t argBegin = 0;
  std::size_t argEnd = 0;
  NextToken(text, length, end, argBegin, argEnd);
  float number = 0.f;
  if (!sfcues::Number(text, argBegin, argEnd, number)) return;
  if (Is(text, begin, end, "voices")) {
    const int most = static_cast<int>(sfzSampler::MAX_VOICES);
    const int count =
        number < 1.f ? 1 : (number > static_cast<float>(most) ? most : static_cast<int>(number));
    voices = count;
    sfTask task;
    task.op = SF_OP::VOICES;
    task.count = count;
    Submit(task, false);
  } else if (Is(text, begin, end, "volume")) {
    volume = number;
  } else if (Is(text, begin, end, "base")) {
    if (number > 0.f) base = number;
  } else if (Is(text, begin, end, "ref")) {
    ref = ClampKey(static_cast<int>(number));
  } else if (Is(text, begin, end, "mid")) {
    mid = ClampKey(static_cast<int>(number));
  }
}

// ─── the block ────────────────────────────────────────────────────────────────

CALC() {
  sfzSampler& s = *sampler;
  if (s.volume() != volume.load()) s.volume(volume.load());
  if (s.base() != base.load()) s.base(base.load());
  if (s.reference() != ref.load()) s.reference(ref.load());
  if (s.middle() != mid.load()) s.middle(mid.load());
  const std::uint32_t version = scaleVersion.load(std::memory_order_acquire);
  if (version != appliedScale) {
    appliedScale = version;
    std::array<Flt, sfzSampler::MAX_DEGREES> local{};
    const int count = degreeCount.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i)
      local[static_cast<std::size_t>(i)] =
          degrees[static_cast<std::size_t>(i)].load(std::memory_order_relaxed);
    s.scale(local.data(), static_cast<UInt>(count));
  }

  // Notes wait while a file task is in flight, so they play the instrument
  // it loads.
  if (inFlight.load(std::memory_order_acquire) <= 0) {
    noteEvent e;
    while (events.try_pop(e)) {
      if (e.status == 0)
        s.noteOn(e.data1, e.velocity); // velocity 0 releases
      else
        s.midi(e.status, e.data1, e.data2);
    }
  }

  s();
  outputs[1].SendBuffer(&s.right(), thread);
  outputs[0].SendBuffer(&s.left(), thread);

  // The pool the block let go is freed in the background.
  if (s.collectPending() && !collectQueued.exchange(true, std::memory_order_acq_rel)) {
    sfTask task;
    task.op = SF_OP::COLLECT;
    if (!Submit(task, false)) collectQueued.store(false, std::memory_order_release);
  }
}

// The top of a block, on the audio thread.
void dSfizz::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  (void)thread;
  const int op = result.tag - TAG_BASE;
  if (op < 0 || op >= OP_COUNT) return;
  if (static_cast<SF_OP>(op) == SF_OP::COLLECT) {
    collectQueued.store(false, std::memory_order_release);
    return;
  }
  if (!result.ok) failed.fetch_add(1, std::memory_order_relaxed);
  if (static_cast<SF_OP>(op) == SF_OP::SCALENAME && result.ok &&
      result.byteCount == sizeof(sfTask)) {
    // The degrees the task read, in its reply. Copied field by field: the
    // bytes are the scheduler's and may not be aligned for an sfTask.
    std::int32_t count = 0;
    std::memcpy(&count, result.bytes + offsetof(sfTask, count), sizeof(count));
    const int most = static_cast<int>(sfzSampler::MAX_DEGREES);
    count = count < 0 ? 0 : (count > most ? most : count);
    for (int i = 0; i < count; ++i) {
      float cents = 0.f;
      std::memcpy(&cents,
                  result.bytes + offsetof(sfTask, degrees) +
                      static_cast<std::size_t>(i) * sizeof(float),
                  sizeof(float));
      degrees[static_cast<std::size_t>(i)].store(cents, std::memory_order_relaxed);
    }
    degreeCount.store(count, std::memory_order_relaxed);
    scaleVersion.fetch_add(1, std::memory_order_release);
  }
  inFlight.fetch_sub(1, std::memory_order_acq_rel);
}

// ─── saving ───────────────────────────────────────────────────────────────────

bool dSfizz::DumpState(nlohmann::json::value_type& json) {
  const std::string file = sampler->file();
  if (!file.empty() && file != filename) json["file"] = file;
  return true;
}

// Control thread, on a fresh object the audio thread cannot see yet, after
// SetParent and PARM_PARSE.
void dSfizz::RestoreState(const nlohmann::json::value_type& json) {
  const std::string file = json.value("file", std::string());
  if (file.empty()) return;
  if (!sampler->load(file)) failed.fetch_add(1, std::memory_order_relaxed);
}

// ─── the background half ──────────────────────────────────────────────────────

bool dSfizz::RunTask(char* args, std::size_t argBytes) {
  return RunPathTask(args, argBytes, nullptr);
}

bool dSfizz::RunPathTask(char* args, std::size_t argBytes, const char* path) {
  static_assert(std::is_trivially_copyable<sfTask>::value,
                "an sfTask is copied into a scheduler slot with memcpy");
  if (argBytes != sizeof(sfTask)) return false;
  sfTask task;
  std::memcpy(&task, args, sizeof(task));
  task.name[sizeof(task.name) - 1] = '\0';

  // By id, not through the object: it may have been deleted since.
  std::shared_ptr<sfzSampler> found = FindSampler(task.id);
  if (found == nullptr) return false;

  // The host resolver's answer, or the name as the patch gave it.
  const std::string file = path != nullptr ? std::string(path) : std::string(task.name);
  switch (task.op) {
  case SF_OP::READ:
    return found->load(file);
  case SF_OP::READAGAIN:
    return found->reload();
  case SF_OP::VOICES:
    found->voices(static_cast<UInt>(task.count));
    return true;
  case SF_OP::COLLECT:
    found->collect();
    return true;
  case SF_OP::SCALENAME: {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const UInt count = sfzSampler::scalaDegrees(text, task.degrees, sfzSampler::MAX_DEGREES);
    if (count == 0) return false;
    task.count = static_cast<std::int32_t>(count);
    std::memcpy(args, &task, sizeof(task)); // the reply
    return true;
  }
  }
  return false;
}

#undef className
