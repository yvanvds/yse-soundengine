#include "dSfplay.h"
#include "sfCues.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <vector>

using namespace YSE::PATCHER;
using YSE::DSP::fileStreamCues;
using YSE::DSP::fileStreamPlayer;
using YSE::DSP::pitchShifter;

namespace {

  // The transport requests, as they travel in `request`.
  constexpr int REQUEST_NONE = 0;
  constexpr int REQUEST_PLAY = 1;
  constexpr int REQUEST_LIST = 2;
  constexpr int REQUEST_SEEK = 3;
  constexpr int REQUEST_STOP = 4;
  constexpr int REQUEST_PAUSE = 5;
  constexpr int REQUEST_RESUME = 6;

  // What a task does. Its fileResult::tag is TAG_BASE plus this.
  enum class SF_OP : std::uint8_t {
    OPEN,
    CLOSE,
    PRELOAD,
    CLEAR,
    SET,
    SYNC,
    RELEASE,
  };
  constexpr int TAG_BASE = 1155000;
  constexpr int OP_COUNT = 7;

  // The most cue numbers one `clear` takes.
  constexpr std::size_t CLEAR_CAPACITY = 64;

  // The players tasks find by id. Weak: a player lives while its object or a
  // task holds it. Control and background threads only (it locks).
  struct playerTable {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::weak_ptr<fileStreamPlayer>> players;
    std::uint64_t next = 1;
  };

  playerTable& Players() {
    // Never destroyed, so an object freed during static destruction still
    // finds it.
    static playerTable* const instance =
        new playerTable(); // NOLINT(cppcoreguidelines-owning-memory): leaked on purpose
    return *instance;
  }

  std::shared_ptr<fileStreamPlayer> FindPlayer(std::uint64_t id) {
    playerTable& t = Players();
    const std::lock_guard<std::mutex> lock(t.mutex);
    auto it = t.players.find(id);
    return it == t.players.end() ? nullptr : it->second.lock();
  }

  // A quality by name or number (0-3); -1 for neither.
  int QualityOf(const char* text, std::size_t begin, std::size_t end) {
    using sfcues::Is;
    if (Is(text, begin, end, "basic")) return pitchShifter::BASIC;
    if (Is(text, begin, end, "good")) return pitchShifter::GOOD;
    if (Is(text, begin, end, "better")) return pitchShifter::BETTER;
    if (Is(text, begin, end, "best")) return pitchShifter::BEST;
    float number = 0.f;
    if (sfcues::Number(text, begin, end, number) && number >= 0.f && number <= 3.f)
      return static_cast<int>(number);
    return -1;
  }

} // namespace

// The record a request becomes. Copied whole into a scheduler slot, so it
// stays trivially copyable.
struct dSfplay::sfTask {
  std::uint64_t id = 0;
  SF_OP op = SF_OP::SYNC;
  std::uint8_t normalized = 0;
  std::int32_t cue = 0;
  float start = 0.f;
  float end = 0.f;
  float speed = 1.f;
  std::int32_t count = 0;
  std::int32_t cues[CLEAR_CAPACITY] = {};
  char name[fileScheduler::PATH_CAPACITY] = {}; // a file, or a cue table's name
};

#define className dSfplay

CONSTRUCT_DSP() {
  loop = 0;
  loopOne = 0;
  speed = 1.f;
  modout = 0;
  pitchShift = 1.f;
  pitchShiftCents = 0.f;
  timeStretch = 0;
  quality = pitchShifter::BASIC;
  embed = 0;
  requestStart = 0.f;
  requestEnd = 0.f;

  player = std::make_shared<fileStreamPlayer>(1);
  {
    playerTable& t = Players();
    const std::lock_guard<std::mutex> lock(t.mutex);
    id = t.next++;
    t.players[id] = player;
  }

  // The outlets follow the arguments: built in ShapePorts, here for one
  // channel.
  ShapePorts();

  REG_PARM_PARSE;
  ADD_PARAM(listName);
  ADD_PARAM(channels);
  ADD_PARAM(bufferSize);
  ADD_PARAM(positionOutlet);

  ADD_DESCRIPTION("Plays sound files streamed from disk, without loading them into memory: cues "
                  "of files, played one by one or in a list, with speed, looping, pausing, "
                  "seeking, pitch shift and time stretch; bangs when playback stops.");
  ADD_CATEGORY(pCategory::SAMPLING);
  PARAM_DOC("list", "",
            "The ~sflist whose cues the numbers 2 and up play; none for cues of the object's "
            "own. May be left out: a first argument that is a number is the channel count.",
            "cue list name");
  PARAM_DOC("channels", "1",
            "The number of signal outlets, each playing the file channel of its number. File "
            "channels past the last outlet are not played.",
            "1 to 64");
  PARAM_DOC("buffersize", "0",
            "Read for compatibility, without effect: the engine streams through buffers of a "
            "fixed size.",
            "any");
  PARAM_DOC("position", "0", "1 adds a signal outlet with where playback is, in milliseconds.",
            "0 or 1");
}

dSfplay::~dSfplay() {
  playerTable& t = Players();
  const std::lock_guard<std::mutex> lock(t.mutex);
  t.players.erase(id);
}

void dSfplay::ShapePorts() {
  // Rebuilt rather than resized, as ~play: the outlets follow the arguments,
  // and the object is new, so no cord starts on them yet.
  inputs.clear();
  outputs.clear();

  ADD_IN_0;
  REG_INT_IN(SetCue);
  REG_FLOAT_IN(SetCueFloat);
  REG_LIST_IN(SetList);
  INLET_DOC(0, "cues",
            "0 stops, 1 plays the open file, 2 and up play that cue; a list plays its cues one "
            "after the other. Also takes 'open <file>', 'fclose [file]', 'preload <cue> [file] "
            "[start ms] [end ms] [direction] [speed]', 'preloadn' (the same with start and end "
            "from 0 to 1), 'clear [cues]', 'set [list]', 'seek <start ms> [end ms]', 'pause', "
            "'resume', 'loop <0/1>', 'loopone <cue>', 'speed <x>', 'modout <0/1>', 'pitchshift "
            "<ratio>', 'pitchshiftcent <cents>', 'timestretch <0/1>', 'quality "
            "<basic|good|better|best>', 'audiofile <file>' (as open) and 'embed <0/1>'.",
            "0 or higher");
  ADD_IN_1;
  REG_FLOAT_IN(SetSpeed);
  REG_INT_IN(SetSpeedInt);
  INLET_DOC(1, "speed",
            "The playback speed: 1 normal, 2 twice as fast. Below 0 is 0: a stream does not "
            "play backward.",
            "0 or higher");

  for (int i = 0; i < channels; i++) {
    ADD_OUT_BUFFER;
    const std::string n = std::to_string(i + 1);
    outputs.back().SetDoc(channels == 1 ? "out" : "out " + n,
                          "File channel " + n +
                              " as it plays. Silence while stopped or paused, and for a "
                              "channel the file lacks unless modout is on.",
                          "any float");
  }
  if (positionOutlet != 0) {
    ADD_OUT_BUFFER;
    outputs.back().SetDoc("position", "Where the cue that plays is, in ms into its file.", "ms");
  }
  ADD_OUT_BANG;
  outputs.back().SetDoc("done",
                        "A bang after the block in which playback stopped: the cue, or the "
                        "last cue of a list, reached its end, or 0 stopped it. Not when "
                        "another cue takes over, and not on pause.",
                        "bang");
}

void dSfplay::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler every file message goes through, built here so a message
  // on the audio thread finds it.
  EnableFileIO();
}

// Control thread, on an object no audio thread has seen yet (a change of
// argument rebuilds the object).
PARM_PARSE() {
  // Max's list name may be left out: then the numbers move up one place.
  float number = 0.f;
  if (!listName.empty() && ReadNumericToken(listName, number)) {
    positionOutlet = bufferSize;
    bufferSize = channels;
    channels = static_cast<int>(number);
    listName.clear();
  }
  const int most = static_cast<int>(fileStreamPlayer::MAX_CHANNELS);
  channels = channels < 1 ? 1 : (channels > most ? most : channels);
  positionOutlet = positionOutlet != 0 ? 1 : 0;
  player->channels(static_cast<UInt>(channels)); // allocates: control thread
  // Opens the list's cues now, as ~buffer reads its file argument.
  if (!listName.empty()) player->cueList(fileStreamCues::acquire(listName));
  ShapePorts();
}

// ─── requests ─────────────────────────────────────────────────────────────────

bool dSfplay::Submit(sfTask& task, bool resolve) {
  fileScheduler* io = FileIO();
  if (io == nullptr) {
    failed.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  task.id = id;
  const int tag = TAG_BASE + static_cast<int>(task.op);
  // A transport request waits for this one: counted before it can complete.
  const bool counted = task.op != SF_OP::RELEASE;
  if (counted) inFlight.fetch_add(1, std::memory_order_acq_rel);
  const std::size_t length = std::strlen(task.name);
  const bool ok = resolve && length > 0
                      ? io->RequestPathTask(this, tag, PA_READ, task.name, length,
                                            &dSfplay::RunPathTask, &task, sizeof(task))
                      : io->RequestTask(this, tag, &dSfplay::RunTask, &task, sizeof(task));
  if (!ok) {
    if (counted) inFlight.fetch_sub(1, std::memory_order_acq_rel);
    failed.fetch_add(1, std::memory_order_relaxed);
  }
  return ok;
}

void dSfplay::Post(int what, int cue, float start, float end) {
  // The numbers first: a block that sees the request sees them.
  requestCue.store(cue, std::memory_order_relaxed);
  requestStart.store(start, std::memory_order_relaxed);
  requestEnd.store(end, std::memory_order_relaxed);
  request.store(what, std::memory_order_release);
}

INT_IN(SetCue) {
  (void)inlet;
  (void)thread;
  if (value < 0) return;
  Post(value == 0 ? REQUEST_STOP : REQUEST_PLAY, value);
}

FLOAT_IN(SetCueFloat) {
  (void)inlet;
  (void)thread;
  if (!std::isfinite(value) || value < 0.f) return;
  const int cue = static_cast<int>(value);
  Post(cue == 0 ? REQUEST_STOP : REQUEST_PLAY, cue);
}

LIST_IN(SetList) {
  (void)inlet;
  (void)thread;
  // In place: the numbers go straight into the request.
  const char* text = value.c_str();
  const std::size_t length = value.size();
  std::size_t pos = 0;
  int count = 0;
  while (count < static_cast<int>(fileStreamPlayer::MAX_LIST)) {
    std::size_t begin = 0;
    std::size_t end = 0;
    sfcues::NextToken(text, length, pos, begin, end);
    float number = 0.f;
    if (!sfcues::Number(text, begin, end, number)) break;
    requestList[count++].store(static_cast<int>(number), std::memory_order_relaxed);
    pos = end;
  }
  if (count == 0) return;
  requestListLength.store(count, std::memory_order_relaxed);
  Post(REQUEST_LIST);
}

FLOAT_IN(SetSpeed) {
  (void)inlet;
  (void)thread;
  if (std::isfinite(value)) speed = value;
}

INT_IN(SetSpeedInt) {
  (void)inlet;
  (void)thread;
  speed = static_cast<float>(value);
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

  // A message that starts with a number is a list of cues.
  float first = 0.f;
  if (sfcues::Number(text, begin, end, first)) {
    SetList(message, 0, YSE::T_GUI);
    return;
  }

  // The next word: a file or list name, or a number.
  std::size_t argBegin = 0;
  std::size_t argEnd = 0;
  NextToken(text, length, end, argBegin, argEnd);
  float number = 0.f;
  const bool hasNumber = sfcues::Number(text, argBegin, argEnd, number);
  auto nameInto = [&](sfTask& task) {
    const std::size_t n = argEnd - argBegin;
    if (n >= sizeof(task.name)) return false;
    std::memcpy(task.name, text + argBegin, n);
    task.name[n] = '\0';
    return true;
  };

  if (Is(text, begin, end, "open") || Is(text, begin, end, "audiofile")) {
    // `open` alone opens a file dialog in Max; the engine has none.
    sfTask task;
    task.op = SF_OP::OPEN;
    if (argBegin < argEnd && nameInto(task)) Submit(task, true);
  } else if (Is(text, begin, end, "fclose")) {
    sfTask task;
    task.op = SF_OP::CLOSE;
    if (nameInto(task)) Submit(task, true);
  } else if (Is(text, begin, end, "preload") || Is(text, begin, end, "preloadn")) {
    sfcues::preloadArgs args;
    if (!sfcues::ParsePreload(text, length, end, args)) return;
    sfTask task;
    task.op = SF_OP::PRELOAD;
    task.normalized = Is(text, begin, end, "preloadn") ? 1 : 0;
    task.cue = args.cue;
    task.start = args.start;
    task.end = args.end;
    task.speed = args.speed;
    const std::size_t n = args.fileEnd - args.fileBegin;
    if (n >= sizeof(task.name)) return;
    std::memcpy(task.name, text + args.fileBegin, n);
    task.name[n] = '\0';
    Submit(task, true);
  } else if (Is(text, begin, end, "clear")) {
    sfTask task;
    task.op = SF_OP::CLEAR;
    std::size_t pos = end;
    while (task.count < static_cast<std::int32_t>(CLEAR_CAPACITY)) {
      std::size_t b = 0;
      std::size_t e = 0;
      NextToken(text, length, pos, b, e);
      float cue = 0.f;
      if (!sfcues::Number(text, b, e, cue)) break;
      task.cues[task.count++] = static_cast<std::int32_t>(cue);
      pos = e;
    }
    Submit(task, false);
  } else if (Is(text, begin, end, "set")) {
    sfTask task;
    task.op = SF_OP::SET;
    if (nameInto(task)) Submit(task, false);
  } else if (Is(text, begin, end, "seek")) {
    if (!hasNumber) return;
    std::size_t b = 0;
    std::size_t e = 0;
    NextToken(text, length, argEnd, b, e);
    float stop = 0.f;
    if (!sfcues::Number(text, b, e, stop)) stop = 0.f;
    Post(REQUEST_SEEK, 1, number, stop);
  } else if (Is(text, begin, end, "pause")) {
    Post(REQUEST_PAUSE);
  } else if (Is(text, begin, end, "resume")) {
    Post(REQUEST_RESUME);
  } else if (Is(text, begin, end, "quality")) {
    const int q = QualityOf(text, argBegin, argEnd);
    if (q >= 0) quality = q;
  } else if (!hasNumber) {
    return; // the attributes below need their number
  } else if (Is(text, begin, end, "loop")) {
    loop = number != 0.f ? 1 : 0;
  } else if (Is(text, begin, end, "loopone")) {
    loopOne = number > 0.f ? static_cast<int>(number) : 0;
  } else if (Is(text, begin, end, "speed")) {
    speed = number;
  } else if (Is(text, begin, end, "modout")) {
    modout = number != 0.f ? 1 : 0;
  } else if (Is(text, begin, end, "pitchshift")) {
    if (number > 0.f) pitchShift = number;
  } else if (Is(text, begin, end, "pitchshiftcent")) {
    pitchShiftCents = number;
  } else if (Is(text, begin, end, "timestretch")) {
    timeStretch = number != 0.f ? 1 : 0;
  } else if (Is(text, begin, end, "embed")) {
    embed = number != 0.f ? 1 : 0;
  }
}

// ─── the block ────────────────────────────────────────────────────────────────

CALC() {
  fileStreamPlayer& p = *player;
  p.loop(loop.load() != 0)
      .loopOne(loopOne.load())
      .speed(speed.load())
      .modout(modout.load() != 0)
      .pitchShift(pitchShift.load())
      .pitchShiftCents(pitchShiftCents.load())
      .timeStretch(timeStretch.load() != 0)
      .quality(static_cast<pitchShifter::QUALITY>(quality.load()));

  // A transport request waits while a file task is in flight, so it finds
  // what the task opens.
  if (inFlight.load(std::memory_order_acquire) <= 0) {
    switch (request.exchange(REQUEST_NONE, std::memory_order_acquire)) {
    case REQUEST_PLAY:
      p.play(requestCue.load(std::memory_order_relaxed));
      break;
    case REQUEST_LIST: {
      Int cues[fileStreamPlayer::MAX_LIST];
      int count = requestListLength.load(std::memory_order_relaxed);
      count = count < 0 ? 0 : std::min(count, static_cast<int>(fileStreamPlayer::MAX_LIST));
      for (int i = 0; i < count; ++i)
        cues[i] = requestList[i].load(std::memory_order_relaxed);
      p.play(cues, static_cast<UInt>(count));
      break;
    }
    case REQUEST_SEEK:
      p.seek(requestStart.load(std::memory_order_relaxed),
             requestEnd.load(std::memory_order_relaxed));
      break;
    case REQUEST_STOP:
      p.stop();
      break;
    case REQUEST_PAUSE:
      p.pause();
      break;
    case REQUEST_RESUME:
      p.resume();
      break;
    default:
      break;
    }
  }

  p();
  for (int c = 0; c < channels; ++c)
    outputs[c].SendBuffer(&p.out(static_cast<UInt>(c)), thread);
  if (positionOutlet != 0) outputs[channels].SendBuffer(&p.position(), thread);
  if (p.done()) outputs.back().ArmBang();

  // Streams the block let go are freed in the background, and a cue table
  // somebody changed is opened again.
  if (p.releasePending() && !releaseQueued.exchange(true, std::memory_order_acq_rel)) {
    sfTask task;
    task.op = SF_OP::RELEASE;
    if (!Submit(task, false)) releaseQueued.store(false, std::memory_order_release);
  }
  if (p.stale() && !syncQueued.exchange(true, std::memory_order_acq_rel)) {
    sfTask task;
    task.op = SF_OP::SYNC;
    if (!Submit(task, false)) syncQueued.store(false, std::memory_order_release);
  }
}

// The top of a block, on the audio thread.
void dSfplay::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  (void)thread;
  const int op = result.tag - TAG_BASE;
  if (op < 0 || op >= OP_COUNT) return;
  const auto what = static_cast<SF_OP>(op);
  if (what == SF_OP::RELEASE) {
    releaseQueued.store(false, std::memory_order_release);
    return;
  }
  if (what == SF_OP::SYNC) syncQueued.store(false, std::memory_order_release);
  inFlight.fetch_sub(1, std::memory_order_acq_rel);
  if (!result.ok) failed.fetch_add(1, std::memory_order_relaxed);
}

// ─── the background half ──────────────────────────────────────────────────────

bool dSfplay::RunTask(char* args, std::size_t argBytes) {
  return RunPathTask(args, argBytes, nullptr);
}

bool dSfplay::RunPathTask(char* args, std::size_t argBytes, const char* path) {
  static_assert(std::is_trivially_copyable<sfTask>::value,
                "an sfTask is copied into a scheduler slot with memcpy");
  if (argBytes != sizeof(sfTask)) return false;
  sfTask task;
  std::memcpy(&task, args, sizeof(task));
  task.name[sizeof(task.name) - 1] = '\0';

  // By id, not through the object: it may have been deleted since.
  std::shared_ptr<fileStreamPlayer> found = FindPlayer(task.id);
  if (found == nullptr) return false;
  // The host resolver's answer, or the name as the patch gave it.
  const std::string file = path != nullptr ? std::string(path) : std::string(task.name);

  switch (task.op) {
  case SF_OP::OPEN:
    return found->open(file);
  case SF_OP::CLOSE:
    return found->close(file);
  case SF_OP::PRELOAD:
    return found->preload(task.cue, file, task.start, task.end, task.speed, task.normalized != 0);
  case SF_OP::CLEAR:
    if (task.count == 0) {
      found->clear();
    } else {
      found->clear(std::vector<Int>(task.cues, task.cues + task.count));
    }
    return true;
  case SF_OP::SET:
    found->cueList(task.name[0] != '\0' ? fileStreamCues::acquire(task.name) : nullptr);
    return true;
  case SF_OP::SYNC:
    return found->sync();
  case SF_OP::RELEASE:
    found->collect();
    return true;
  }
  return false;
}

// ─── embed ────────────────────────────────────────────────────────────────────

// Control thread, under DumpJSON.
bool dSfplay::DumpState(nlohmann::json::value_type& json) {
  if (embed.load() == 0) return true;
  json["embed"] = 1;
  const std::string file = player->file();
  if (!file.empty()) json["file"] = file;
  // The cues of an ~sflist are its own to save.
  std::shared_ptr<fileStreamCues> table = player->cueList();
  if (!table->name().empty()) return true;
  for (const fileStreamCues::cue& c : table->cues()) {
    nlohmann::json entry;
    entry["cue"] = c.number;
    entry["file"] = c.file;
    entry["start"] = c.startMs;
    entry["end"] = c.endMs;
    entry["speed"] = c.speed;
    json["cues"].push_back(entry);
  }
  return true;
}

// Control thread, on a fresh object the audio thread cannot see yet, after
// SetParent and PARM_PARSE. The files are opened here and now, as ~buffer
// reads its file argument.
void dSfplay::RestoreState(const nlohmann::json::value_type& json) {
  if (json.value("embed", 0) == 0) return;
  embed = 1;
  const std::string file = json.value("file", std::string());
  if (!file.empty() && !player->open(file)) failed.fetch_add(1, std::memory_order_relaxed);
  const auto stored = json.find("cues");
  if (stored == json.end() || !stored->is_array()) return;
  std::shared_ptr<fileStreamCues> table = player->cueList();
  if (!table->name().empty()) return;
  for (const auto& entry : *stored) {
    if (!entry.is_object()) continue;
    if (!table->preload(entry.value("cue", 0), entry.value("file", std::string()),
                        entry.value("start", 0.f), entry.value("end", 0.f),
                        entry.value("speed", 1.f)))
      failed.fetch_add(1, std::memory_order_relaxed);
  }
  if (!player->sync()) failed.fetch_add(1, std::memory_order_relaxed);
}

#undef className
