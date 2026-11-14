#include "dStretch.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include "../time/timeValue.h"
#include "../../clock/clockManager.h"
#include "../../dsp/sampling/sampleBuffer.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <type_traits>
#include <unordered_map>

using namespace YSE::PATCHER;
using YSE::DSP::timeStretch;

#define className dStretch

// What the object and its jobs share (see the class notes). Found by id,
// held by the object and by a running job, so whichever lets go last frees
// it.
struct dStretch::state {
  explicit state(std::uint64_t id) : id(id) {}

  const std::uint64_t id;
  // Jobs numbered at or below this stop.
  std::atomic<std::uint64_t> cancelThrough{0};
  // The job running now, 0 for none, and its progress.
  std::atomic<std::uint64_t> running{0};
  std::atomic<float> progress{0.f};

  // readagain's copy. The background pool's alone (one worker); the mutex
  // only states it.
  std::mutex mutex;
  std::shared_ptr<DSP::sampleBuffer> original; // under mutex
  std::uint64_t originalOf = 0; // the buffer it is a copy of, by id
  std::uint64_t written = 0; // that buffer's version after the last job
};

namespace {

  enum class OUTCOME : std::uint8_t {
    DONE,
    CANCELLED,
    NO_BUFFER,
  };

  // The process-wide states, by id. Weak: a state lives while its object or
  // a running job holds it. Control and background threads only (it locks).
  struct stateTable {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::weak_ptr<dStretch::state>> states;
    std::uint64_t next = 1;
  };

  stateTable& States() {
    // Never destroyed, so a state freed during static destruction still finds
    // its table.
    static stateTable* const instance =
        new stateTable(); // NOLINT(cppcoreguidelines-owning-memory): leaked on purpose
    return *instance;
  }

  std::shared_ptr<dStretch::state> MakeState() {
    stateTable& table = States();
    const std::lock_guard<std::mutex> lock(table.mutex);
    const std::uint64_t id = table.next++;
    // The deleter takes the state out of the table, wherever the last
    // holder lets go (never the audio thread: see the class notes).
    std::shared_ptr<dStretch::state> made(new dStretch::state(id), [](dStretch::state* s) {
      {
        stateTable& t = States();
        const std::lock_guard<std::mutex> guard(t.mutex);
        auto it = t.states.find(s->id);
        if (it != t.states.end() && it->second.expired()) t.states.erase(it);
      }
      delete s; // NOLINT(cppcoreguidelines-owning-memory): the shared_ptr's deleter
    });
    table.states[id] = made;
    return made;
  }

  std::shared_ptr<dStretch::state> FindState(std::uint64_t id) {
    stateTable& table = States();
    const std::lock_guard<std::mutex> lock(table.mutex);
    auto it = table.states.find(id);
    return it == table.states.end() ? nullptr : it->second.lock();
  }

  // The words ~stretch takes as attributes and commands. Anything else in
  // the settings is a value.
  constexpr std::string_view WORDS[] = {
      "basictuning", "followglobaltempo", "formant",         "formantcorrection",
      "mode",        "originallength",    "originaltempo",   "pitchcorrection",
      "pitchshift",  "pitchshiftcent",    "progress_enable", "quality",
      "readagain",   "slurtime",          "stretch",         "transport",
  };

  bool IsWord(std::string_view token) {
    for (const std::string_view w : WORDS)
      if (token == w) return true;
    return false;
  }

  bool Number(std::string_view token, float& out) {
    return !token.empty() && ReadNumericToken(token.data(), token.size(), out) &&
           std::isfinite(out);
  }

  // An enum by name or number. False when it is neither.
  template <std::size_t N>
  bool Named(std::string_view token, const std::array<std::string_view, N>& names, int& out) {
    for (std::size_t i = 0; i < N; ++i) {
      if (token == names[i]) {
        out = static_cast<int>(i);
        return true;
      }
    }
    float number = 0.f;
    if (!Number(token, number)) return false;
    const long value = std::lround(number);
    if (value < 0 || value >= static_cast<long>(N)) return false;
    out = static_cast<int>(value);
    return true;
  }

  constexpr std::array<std::string_view, 4> QUALITIES = {"basic", "good", "better", "best"};
  // In timeStretch::MODE order.
  constexpr std::array<std::string_view, 8> MODES = {"basic",          "monophonic", "rhythmic",
                                                     "general",        "polyphonic", "pads",
                                                     "extremestretch", "efficient"};

  // Up to `capacity` tokens of `text`, walked in place so nothing allocates
  // on whichever thread delivers the message. The number found.
  std::size_t Tokens(const std::string& text, std::string_view* out, std::size_t capacity) {
    const char* chars = text.c_str();
    const std::size_t size = text.size();
    std::size_t found = 0;
    std::size_t at = 0;
    while (found < capacity) {
      while (at < size && IsSelectorSeparator(chars[at]))
        at++;
      if (at == size) break;
      const std::size_t begin = at;
      while (at < size && !IsSelectorSeparator(chars[at]))
        at++;
      out[found++] = std::string_view(chars + begin, at - begin);
    }
    return found;
  }

  constexpr std::size_t MAX_TOKENS = 8;

  // What a job reports through timeStretch's progress function.
  struct jobProgress {
    dStretch::state* shared = nullptr;
    std::uint64_t job = 0;

    static bool Report(Flt done, void* self) {
      auto* p = static_cast<jobProgress*>(self);
      p->shared->progress.store(done, std::memory_order_relaxed);
      return p->job > p->shared->cancelThrough.load(std::memory_order_acquire);
    }
  };

} // namespace

// The record a bang becomes. Copied whole into a scheduler slot, so it must
// stay trivially copyable; the task writes its outcome into the same record.
struct dStretch::stretchTask {
  std::uint64_t state = 0;
  std::uint64_t job = 0;
  float stretch = 1.f;
  float pitch = 1.f;
  float tuning = 440.f;
  float originalTempo = 120.f;
  float originalLength = 0.f;
  std::int32_t quality = timeStretch::BETTER;
  std::int32_t mode = timeStretch::MODE_BASIC;
  std::uint8_t readAgain = 1;
  std::uint8_t correction = 0;
  std::uint8_t followTempo = 0;
  std::uint8_t lengthGiven = 0;

  // The outcome.
  OUTCOME outcome = OUTCOME::CANCELLED;
  std::uint32_t frames = 0;

  char name[fileScheduler::PATH_CAPACITY] = {};
  char clock[CLOCK_CAPACITY] = {};
};

CONSTRUCT() {
  shared = MakeState();
  stateId = shared->id;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(bufferName);
  ADD_PARAM(settings);

  ADD_IN_0;
  REG_BANG_IN(Bang);

  ADD_OUT_BANG;
  ADD_OUT_FLOAT;

  ADD_DESCRIPTION("Changes the length and the pitch of a named buffer's contents independently, "
                  "in the background: twice as long at the same pitch, or an octave up at the "
                  "same length. Reports its progress and bangs when done.");
  ADD_CATEGORY(pCategory::SAMPLING);
  INLET_DOC(0, "messages",
            "bang or apply processes the buffer; cancel stops it; set <name> picks another "
            "buffer. Also the attribute messages: stretch, pitchshift, pitchshiftcent, quality, "
            "mode, readagain, pitchcorrection, basictuning, followglobaltempo, originaltempo, "
            "originallength, transport and progress_enable.",
            "");
  OUTLET_DOC(0, "done", "A bang when the buffer holds the result.", "bang");
  OUTLET_DOC(1, "progress",
             "How far the job has got, 0 to 1, while it runs, and 1 just before the bang. Only "
             "with progress_enable 1.",
             "0-1");
  PARAM_DOC("name", "", "The buffer to process.", "any symbol");
  PARAM_DOC("settings", "",
            "Any of 'stretch <factor>' (1), 'pitchshift <factor>' (1), 'pitchshiftcent "
            "<cents>' (0), 'quality <basic|good|better|best>' (better), 'mode "
            "<basic|monophonic|rhythmic|general|polyphonic|pads|extremestretch|efficient>' "
            "(basic), 'readagain <0|1>' (1), 'pitchcorrection <0|1>' (0), 'basictuning <Hz>' "
            "(440), 'followglobaltempo <0|1>' (0), 'originaltempo <bpm>' (120), "
            "'originallength <beats>' (0), 'transport <clock>' and 'progress_enable <0|1>' (1).",
            "words and values");
}

dStretch::~dStretch() {
  // Stops a running job and every one still waiting.
  shared->cancelThrough.store(std::numeric_limits<std::uint64_t>::max(), std::memory_order_release);
}

void dStretch::Teardown(YSE::THREAD thread) {
  pObject::Teardown(thread);
  // The object is freed later, on the background pool, behind the jobs it
  // sent: they must not run.
  shared->cancelThrough.store(std::numeric_limits<std::uint64_t>::max(), std::memory_order_release);
}

void dStretch::Defaults() {
  stretchFactor = 1.f;
  pitchFactor = 1.f;
  quality = timeStretch::BETTER;
  mode = timeStretch::MODE_BASIC;
  basicTuning = 440.f;
  originalTempo = 120.f;
  originalLength = 0.f;
  lengthGiven = false;
  readAgain = true;
  correction = false;
  followTempo = false;
  progressOn = true;
  clock[0] = '\0';
}

bool dStretch::SetText(char* out, std::size_t capacity, const char* text, std::size_t length) {
  if (length >= capacity) return false;
  std::memcpy(out, text, length);
  out[length] = '\0';
  return true;
}

timeStretch dStretch::Stretcher() const {
  timeStretch ts;
  ts.stretch(stretchFactor.load())
      .pitch(pitchFactor.load())
      .quality(static_cast<timeStretch::QUALITY>(quality.load()))
      .mode(static_cast<timeStretch::MODE>(mode.load()))
      .pitchCorrection(correction.load())
      .standardPitch(basicTuning.load());
  return ts;
}

bool dStretch::ApplyWord(std::string_view word, const std::string_view* values, std::size_t count) {
  if (word == "transport") {
    if (count == 0) {
      clock[0] = '\0';
      return true;
    }
    return SetText(clock, sizeof(clock), values[0].data(), values[0].size());
  }
  if (count == 0) return false;
  if (word == "quality") {
    int q = 0;
    if (!Named(values[0], QUALITIES, q)) return false;
    quality = q;
    return true;
  }
  if (word == "mode") {
    int m = 0;
    if (!Named(values[0], MODES, m)) return false;
    mode = m;
    return true;
  }
  if (word == "originallength") {
    // Beats: a number, a note value, or `<n> ticks`.
    double beats = 0.0;
    float number = 0.f;
    if (count == 1 && Number(values[0], number)) {
      beats = number;
    } else {
      const char* begin = values[0].data();
      const char* end = values[count - 1].data() + values[count - 1].size();
      if (!ReadBeatTime(begin, static_cast<std::size_t>(end - begin), beats)) return false;
    }
    originalLength = static_cast<float>(beats);
    lengthGiven = beats > 0.0;
    return true;
  }
  float number = 0.f;
  if (!Number(values[0], number)) return false;
  if (word == "stretch") {
    stretchFactor = number;
  } else if (word == "pitchshift") {
    pitchFactor = number;
  } else if (word == "pitchshiftcent") {
    pitchFactor = static_cast<float>(std::exp2(static_cast<double>(number) / 1200.0));
  } else if (word == "readagain") {
    readAgain = number != 0.f;
  } else if (word == "pitchcorrection") {
    correction = number != 0.f;
  } else if (word == "basictuning") {
    basicTuning = number;
  } else if (word == "followglobaltempo") {
    followTempo = number != 0.f;
  } else if (word == "originaltempo") {
    if (!(number > 0.f)) return false;
    originalTempo = number;
    lengthGiven = false;
  } else if (word == "progress_enable") {
    progressOn = number != 0.f;
  } else if (word == "formant" || word == "formantcorrection" || word == "slurtime") {
    // Not done (see the class notes); accepted for patches that set them.
  } else {
    return false;
  }
  return true;
}

PARM_CLEAR() {
  bufferName.clear();
  settings.clear();
  Defaults();
}

// Control thread, on an object no audio thread has seen yet (a change of
// argument rebuilds the object).
PARM_PARSE() {
  Defaults();
  if (!SetText(name, sizeof(name), bufferName.c_str(), bufferName.size())) name[0] = '\0';
  const std::vector<std::string_view> tokens(settings.begin(), settings.end());
  std::size_t at = 0;
  while (at < tokens.size()) {
    if (!IsWord(tokens[at])) {
      at++;
      continue;
    }
    std::size_t end = at + 1;
    while (end < tokens.size() && !IsWord(tokens[end]))
      end++;
    ApplyWord(tokens[at], tokens.data() + at + 1, end - at - 1);
    at = end;
  }
}

void dStretch::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler every job goes through, built here so a bang on the audio
  // thread finds it.
  EnableFileIO();
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(Bang) {
  (void)inlet;
  (void)thread;
  Request();
}

MESSAGES() {
  (void)value;
  std::array<std::string_view, MAX_TOKENS> tokens;
  const std::size_t count = Tokens(message, tokens.data(), tokens.size());
  if (count == 0) return;
  const std::string_view word = tokens[0];
  if (word == "bang" || word == "apply") {
    Request();
  } else if (word == "cancel") {
    Cancel();
  } else if (word == "set") {
    if (count == 1) {
      name[0] = '\0';
    } else {
      SetText(name, sizeof(name), tokens[1].data(), tokens[1].size());
    }
  } else {
    ApplyWord(word, tokens.data() + 1, count - 1);
  }
}

void dStretch::Request() {
  fileScheduler* io = FileIO();
  if (name[0] == '\0' || io == nullptr) {
    refused.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  stretchTask task;
  task.state = stateId;
  task.job = requested.fetch_add(1, std::memory_order_acq_rel) + 1;
  task.stretch = stretchFactor.load();
  task.pitch = pitchFactor.load();
  task.tuning = basicTuning.load();
  task.originalTempo = originalTempo.load();
  task.originalLength = originalLength.load();
  task.quality = quality.load();
  task.mode = mode.load();
  task.readAgain = readAgain.load() ? 1 : 0;
  task.correction = correction.load() ? 1 : 0;
  task.followTempo = followTempo.load() ? 1 : 0;
  task.lengthGiven = lengthGiven.load() ? 1 : 0;
  std::memcpy(task.name, name, sizeof(task.name));
  std::memcpy(task.clock, clock, sizeof(task.clock));
  if (!io->RequestTask(this, 0, &dStretch::RunTask, &task, sizeof(task)))
    refused.fetch_add(1, std::memory_order_relaxed);
}

void dStretch::Cancel() {
  shared->cancelThrough.store(requested.load(std::memory_order_acquire), std::memory_order_release);
}

// ─── the background half ──────────────────────────────────────────────────────

bool dStretch::RunTask(char* args, std::size_t argBytes) {
  static_assert(std::is_trivially_copyable<stretchTask>::value,
                "a stretchTask is copied into a scheduler slot with memcpy");
  if (argBytes != sizeof(stretchTask)) return false;
  stretchTask task;
  std::memcpy(&task, args, sizeof(task));
  const auto finish = [&](OUTCOME outcome, bool ok) {
    task.outcome = outcome;
    std::memcpy(args, &task, sizeof(task));
    return ok;
  };

  // No state: the object is gone, and its jobs with it.
  const std::shared_ptr<state> shared = FindState(task.state);
  if (shared == nullptr) return finish(OUTCOME::CANCELLED, false);
  if (task.job <= shared->cancelThrough.load(std::memory_order_acquire))
    return finish(OUTCOME::CANCELLED, false);

  const std::shared_ptr<DSP::sampleBuffer> buffer = DSP::sampleBufferRegistry::find(task.name);
  if (buffer == nullptr) return finish(OUTCOME::NO_BUFFER, false);

  timeStretch ts;
  ts.stretch(task.stretch)
      .pitch(task.pitch)
      .quality(static_cast<timeStretch::QUALITY>(task.quality))
      .mode(static_cast<timeStretch::MODE>(task.mode))
      .pitchCorrection(task.correction != 0)
      .standardPitch(task.tuning);

  const std::lock_guard<std::mutex> lock(shared->mutex);
  // readagain: the copy taken before, unless the buffer changed since the
  // last job wrote it; else a new copy.
  const DSP::sampleBuffer* source = buffer.get();
  if (task.readAgain != 0) {
    const bool kept = shared->original != nullptr && shared->originalOf == buffer->id() &&
                      shared->written == buffer->version();
    if (!kept) {
      shared->original = DSP::sampleBuffer::create();
      shared->original->copyFrom(*buffer);
      shared->originalOf = buffer->id();
    }
    source = shared->original.get();
  } else {
    shared->original.reset();
  }

  if (task.followTempo != 0 && task.clock[0] != '\0') {
    const Flt tempo = CLOCK::Manager().currentTempo(task.clock);
    Flt original = task.originalTempo;
    // originallength in beats over the source's length gives the tempo.
    const Flt seconds = source->lengthMs() / 1000.f;
    if (task.lengthGiven != 0 && seconds > 0.f) original = task.originalLength * 60.f / seconds;
    if (tempo > 0.f && original > 0.f) ts.stretch(original / tempo);
  }

  shared->progress.store(0.f, std::memory_order_relaxed);
  shared->running.store(task.job, std::memory_order_release);
  jobProgress progress{shared.get(), task.job};
  const bool ok = ts.process(*source, *buffer, &jobProgress::Report, &progress);
  shared->running.store(0, std::memory_order_release);
  if (!ok) {
    // A source that is empty is not a cancel, but nothing changed either.
    return finish(source->frames() == 0 ? OUTCOME::NO_BUFFER : OUTCOME::CANCELLED, false);
  }
  shared->written = buffer->version();
  task.frames = buffer->frames();
  return finish(OUTCOME::DONE, true);
}

// ─── the audio-thread half ────────────────────────────────────────────────────

// The block poll, at the top of every block: the running job's progress, at
// most once per hundredth. Two atomic loads when nothing runs.
CALC() {
  const std::uint64_t job = shared->running.load(std::memory_order_acquire);
  if (job == 0 || !progressOn.load()) return;
  const float done = shared->progress.load(std::memory_order_relaxed);
  const auto step = static_cast<int>(done * 100.f);
  if (job == reportedJob && step == reportedStep) return;
  reportedJob = job;
  reportedStep = step;
  outputs[1].SendFloat(done, thread);
}

// The top of a block, on the audio thread, inside its own dispatch frame.
// Right to left: the last progress, then the bang.
void dStretch::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  if (result.op != FILE_OP::TASK || result.bytes == nullptr ||
      result.byteCount != sizeof(stretchTask)) {
    return;
  }
  stretchTask task;
  std::memcpy(&task, result.bytes, sizeof(task));
  if (!result.ok || task.outcome != OUTCOME::DONE) {
    if (task.outcome == OUTCOME::NO_BUFFER) {
      failed.fetch_add(1, std::memory_order_relaxed);
    } else {
      cancelled.fetch_add(1, std::memory_order_relaxed);
    }
    return;
  }
  reportedJob = task.job;
  reportedStep = 100;
  if (progressOn.load()) outputs[1].SendFloat(1.f, thread);
  outputs[0].SendBang(thread);
}

#undef className
