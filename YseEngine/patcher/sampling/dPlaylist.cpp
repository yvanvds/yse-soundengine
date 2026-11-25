#include "dPlaylist.h"
#include "sfCues.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include "../math/gExprEval.h"
#include "../pListArgs.h"

using namespace YSE::PATCHER;
using YSE::DSP::filePlaylist;
using YSE::DSP::pitchShifter;

namespace {

  // The transport requests, as they travel in `request`.
  constexpr int REQUEST_NONE = 0;
  constexpr int REQUEST_PLAY = 1;
  constexpr int REQUEST_NEXT = 2;
  constexpr int REQUEST_STOP = 3;
  constexpr int REQUEST_PAUSE = 4;
  constexpr int REQUEST_RESUME = 5;

  // What a task does. Its fileResult::tag is TAG_BASE plus this.
  enum class PL_OP : std::uint8_t {
    APPEND,
    REMOVE,
    CLEAR,
    MOVE,
    SELECT,
    UNSELECT,
    SETCLIP,
    RELEASE,
  };
  constexpr int TAG_BASE = 1158000;
  constexpr int OP_COUNT = 8;

  // Room for a notification or a content line: a name and a file of a path's
  // size each, and the numbers.
  constexpr std::size_t LINE_CAPACITY = 2 * fileScheduler::PATH_CAPACITY + 512;

  // The names `setclip` and the content lines use, by DSP::filePlaylist::SETTING.
  constexpr const char* SETTING_NAMES[filePlaylist::SETTINGS] = {
      "loop", "speed", "pitchshift", "pitchshiftcent", "timestretch", "quality"};

  // The playlists tasks find by id. Weak: a playlist lives while its object
  // or a task holds it. Control and background threads only (it locks).
  struct playlistTable {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::weak_ptr<filePlaylist>> lists;
    std::uint64_t next = 1;
  };

  playlistTable& Lists() {
    // Never destroyed, so an object freed during static destruction still
    // finds it.
    static playlistTable* const instance =
        new playlistTable(); // NOLINT(cppcoreguidelines-owning-memory): leaked on purpose
    return *instance;
  }

  std::shared_ptr<filePlaylist> FindList(std::uint64_t id) {
    playlistTable& t = Lists();
    const std::lock_guard<std::mutex> lock(t.mutex);
    auto it = t.lists.find(id);
    return it == t.lists.end() ? nullptr : it->second.lock();
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

  // The SETTING a word names; SETTINGS for none.
  int SettingOf(const char* text, std::size_t begin, std::size_t end) {
    for (int k = 0; k < filePlaylist::SETTINGS; ++k)
      if (sfcues::Is(text, begin, end, SETTING_NAMES[k])) return k;
    return filePlaylist::SETTINGS;
  }

  void AppendNumber(std::string& out, float value) {
    char number[kExprValueTextMax];
    const int written = ExprFormatValue(ExprValue::Float(value), number, kExprValueTextMax);
    if (written > 0) out.append(number, static_cast<std::size_t>(written));
  }

  void AppendInt(std::string& out, int value) {
    char digits[FORMAT_INT_WIDTH];
    out.append(digits, WriteInt(value, digits));
  }

  // The last part of a name, after its final '/' or '\'.
  const char* LastPart(const char* name) {
    const char* last = name;
    for (const char* c = name; *c != '\0'; ++c)
      if (*c == '/' || *c == '\\') last = c + 1;
    return last;
  }

} // namespace

// The record a request becomes. Copied whole into a scheduler slot, so it
// stays trivially copyable.
struct dPlaylist::plTask {
  std::uint64_t id = 0;
  PL_OP op = PL_OP::RELEASE;
  std::uint8_t normalized = 0;
  std::uint8_t setting = 0;
  std::int32_t clip = 0;
  std::int32_t place = 0;
  float start = 0.f;
  float end = 0.f;
  float value = 0.f;
  char name[fileScheduler::PATH_CAPACITY] = {}; // a file, as the patch gave it
};

#define className dPlaylist

CONSTRUCT_DSP() {
  loop = 0;
  speed = 1.f;
  timeStretch = 0;
  pitchShift = 1.f;
  pitchShiftCents = 0.f;
  quality = pitchShifter::BASIC;
  loopReport = 0;
  reportProgress = 0;
  line.reserve(LINE_CAPACITY);

  list = std::make_shared<filePlaylist>(2);
  {
    playlistTable& t = Lists();
    const std::lock_guard<std::mutex> lock(t.mutex);
    id = t.next++;
    t.lists[id] = list;
  }

  // The outlets follow the channel count: built in ShapePorts.
  ShapePorts();

  REG_PARM_PARSE;
  ADD_PARAM(channelCount);
  ADD_PARAM(loop);
  ADD_PARAM(speed);
  ADD_PARAM(timeStretch);
  ADD_PARAM(pitchShift);
  ADD_PARAM(pitchShiftCents);
  ADD_PARAM(quality);
  ADD_PARAM(loopReport);
  ADD_PARAM(reportProgress);

  ADD_DESCRIPTION("A list of sound files, each a clip with a selection of its own, played from "
                  "disk one clip at a time, with speed, looping, pausing, pitch shift and time "
                  "stretch; reports clips starting, ending and looping. The host draws the list.");
  ADD_CATEGORY(pCategory::SAMPLING);
  PARAM_DOC("channelcount", "2",
            "The number of signal outlets, each playing the file channel of its number. File "
            "channels past the last outlet are not played.",
            "1 to 64");
  PARAM_DOC("loop", "0", "1 loops every clip that has no loop setting of its own.", "0 or 1");
  PARAM_DOC("speed", "1",
            "The playback speed of every clip that has none of its own: 1 normal, 2 twice as "
            "fast, below 0 backward, 0 holds.",
            "any float");
  PARAM_DOC("timestretch", "0",
            "1 keeps the pitch when the speed changes, so the speed only changes the tempo.",
            "0 or 1");
  PARAM_DOC("pitchshift", "1", "Shifts the pitch by this ratio, keeping the duration.", "above 0");
  PARAM_DOC("pitchshiftcent", "0", "Shifts the pitch by this many cents, on top of pitchshift.",
            "any float");
  PARAM_DOC("quality", "0",
            "How the pitch is shifted: 0 basic, 1 good, 2 better, 3 best (see ~pitchshift).",
            "0 to 3");
  PARAM_DOC("loopreport", "0", "1 sends loopnotify each time a clip loops.", "0 or 1");
  PARAM_DOC("reportprogress", "0",
            "1 sends progress, how far through its selection the clip is, every 50 ms of play.",
            "0 or 1");
}

dPlaylist::~dPlaylist() {
  playlistTable& t = Lists();
  const std::lock_guard<std::mutex> lock(t.mutex);
  t.lists.erase(id);
}

void dPlaylist::ShapePorts() {
  // Rebuilt rather than resized: the outlets follow the arguments, and the
  // object is new, so no cord starts on them yet.
  inputs.clear();
  outputs.clear();

  ADD_IN_0;
  REG_INT_IN(SetClip);
  REG_FLOAT_IN(SetClipFloat);
  REG_LIST_IN(SetList);
  INLET_DOC(0, "clips",
            "0 stops, 1 and up play that clip from the start of its selection. Also takes "
            "'append <file> [place]', 'remove <clip>', 'clear', 'move <clip> <place>', "
            "'selection [clip] <start> <end>' (0 to 1; without a clip every clip; a clip alone "
            "clears its selection), 'selectionms' (the same in ms), 'setclip <clip> <loop|speed|"
            "pitchshift|pitchshiftcent|timestretch|quality> [value]' (no value: follow the "
            "playlist again), 'next', 'pause', 'resume', 'getcontent', and 'loop', 'speed', "
            "'timestretch', 'pitchshift', 'pitchshiftcent', 'quality', 'loopreport' and "
            "'reportprogress' with a value.",
            "0 or higher");

  for (int i = 0; i < channelCount; i++) {
    ADD_OUT_BUFFER;
    const std::string n = std::to_string(i + 1);
    outputs.back().SetDoc(channelCount == 1 ? "out" : "out " + n,
                          "File channel " + n +
                              " of the clip that plays. Silence while stopped or paused, and for "
                              "a channel the file lacks.",
                          "any float");
  }
  ADD_OUT_BUFFER;
  outputs.back().SetDoc("position",
                        "The playhead: the number of the clip that plays plus how far into its "
                        "file it is, so 2.5 is halfway through clip 2. 0 while nothing plays.",
                        "0 or higher");
  ADD_OUT_LIST;
  outputs.back().SetDoc("notifications",
                        "'start <clip> <name>' when a clip starts, 'done <clip> <name>' when it "
                        "reaches its end or 0 stops it (not when another clip takes over, not on "
                        "pause), 'loopnotify <clip> <name>' when it loops (with loopreport) and "
                        "'progress <clip> <0-1>' while it plays (with reportprogress).",
                        "list");
  ADD_OUT_LIST;
  outputs.back().SetDoc("content",
                        "getcontent's answer: 'clips <count>', then for each clip 'clip <n> "
                        "<name> <file> <length ms> <start ms> <end ms>' and its own settings as "
                        "name and value pairs.",
                        "list");
}

void dPlaylist::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler every list message goes through, built here so a message
  // on the audio thread finds it.
  EnableFileIO();
}

// Control thread, on an object no audio thread has seen yet (a change of
// argument rebuilds it).
PARM_PARSE() {
  const int most = static_cast<int>(filePlaylist::MAX_CHANNELS);
  channelCount = channelCount < 1 ? 1 : (channelCount > most ? most : channelCount);
  list->channels(static_cast<UInt>(channelCount)); // allocates: control thread
  ShapePorts();
}

// ─── requests ─────────────────────────────────────────────────────────────────

bool dPlaylist::Submit(plTask& task, bool resolve) {
  fileScheduler* io = FileIO();
  if (io == nullptr) {
    failed.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  task.id = id;
  const int tag = TAG_BASE + static_cast<int>(task.op);
  // A transport request waits for this one: counted before it can complete.
  const bool counted = task.op != PL_OP::RELEASE;
  if (counted) inFlight.fetch_add(1, std::memory_order_acq_rel);
  const std::size_t length = std::strlen(task.name);
  const bool ok = resolve && length > 0
                      ? io->RequestPathTask(this, tag, PA_READ, task.name, length,
                                            &dPlaylist::RunPathTask, &task, sizeof(task))
                      : io->RequestTask(this, tag, &dPlaylist::RunTask, &task, sizeof(task));
  if (!ok) {
    if (counted) inFlight.fetch_sub(1, std::memory_order_acq_rel);
    failed.fetch_add(1, std::memory_order_relaxed);
  }
  return ok;
}

void dPlaylist::Post(int what, int clip) {
  // The clip first: a block that sees the request sees it.
  requestClip.store(clip, std::memory_order_relaxed);
  request.store(what, std::memory_order_release);
}

INT_IN(SetClip) {
  (void)inlet;
  (void)thread;
  if (value < 0) return;
  Post(value == 0 ? REQUEST_STOP : REQUEST_PLAY, value);
}

FLOAT_IN(SetClipFloat) {
  (void)inlet;
  (void)thread;
  if (!std::isfinite(value) || value < 0.f) return;
  const int clip = static_cast<int>(value);
  Post(clip == 0 ? REQUEST_STOP : REQUEST_PLAY, clip);
}

LIST_IN(SetList) {
  // A list of numbers: its first is the clip.
  std::size_t begin = 0;
  std::size_t end = 0;
  sfcues::NextToken(value.c_str(), value.size(), 0, begin, end);
  float number = 0.f;
  if (sfcues::Number(value.c_str(), begin, end, number)) SetClipFloat(number, inlet, thread);
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

  float first = 0.f;
  if (sfcues::Number(text, begin, end, first)) {
    SetClipFloat(first, 0, YSE::T_GUI);
    return;
  }

  // Up to three numbers after the word, and the word after it.
  std::size_t argBegin = 0;
  std::size_t argEnd = 0;
  NextToken(text, length, end, argBegin, argEnd);
  float numbers[3] = {0.f, 0.f, 0.f};
  int count = 0;
  {
    std::size_t pos = end;
    while (count < 3) {
      std::size_t b = 0;
      std::size_t e = 0;
      NextToken(text, length, pos, b, e);
      if (!sfcues::Number(text, b, e, numbers[count])) break;
      count++;
      pos = e;
    }
  }

  if (Is(text, begin, end, "append")) {
    // `append` alone opens a file dialog in Max: the host's to do.
    const std::size_t n = argEnd - argBegin;
    float number = 0.f;
    if (n == 0 || sfcues::Number(text, argBegin, argEnd, number)) return;
    plTask task;
    task.op = PL_OP::APPEND;
    if (n >= sizeof(task.name)) {
      failed.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    std::memcpy(task.name, text + argBegin, n);
    task.name[n] = '\0';
    std::size_t b = 0;
    std::size_t e = 0;
    NextToken(text, length, argEnd, b, e);
    float place = 0.f;
    if (sfcues::Number(text, b, e, place) && place >= 1.f) task.place = static_cast<int>(place);
    Submit(task, true);
  } else if (Is(text, begin, end, "remove")) {
    if (count < 1) return;
    plTask task;
    task.op = PL_OP::REMOVE;
    task.clip = static_cast<int>(numbers[0]);
    Submit(task, false);
  } else if (Is(text, begin, end, "clear")) {
    plTask task;
    task.op = PL_OP::CLEAR;
    Submit(task, false);
  } else if (Is(text, begin, end, "move")) {
    if (count < 2) return;
    plTask task;
    task.op = PL_OP::MOVE;
    task.clip = static_cast<int>(numbers[0]);
    task.place = static_cast<int>(numbers[1]);
    Submit(task, false);
  } else if (Is(text, begin, end, "selection") || Is(text, begin, end, "selectionms")) {
    if (count < 1) return;
    plTask task;
    task.normalized = Is(text, begin, end, "selection") ? 1 : 0;
    if (count == 1) {
      task.op = PL_OP::UNSELECT;
      task.clip = static_cast<int>(numbers[0]);
    } else {
      task.op = PL_OP::SELECT;
      // Two numbers: every clip.
      task.clip = count == 3 ? static_cast<int>(numbers[0]) : 0;
      task.start = count == 3 ? numbers[1] : numbers[0];
      task.end = count == 3 ? numbers[2] : numbers[1];
    }
    Submit(task, false);
  } else if (Is(text, begin, end, "setclip")) {
    if (count < 1) return;
    std::size_t clipBegin = 0;
    std::size_t clipEnd = 0;
    NextToken(text, length, end, clipBegin, clipEnd);
    std::size_t wordBegin = 0;
    std::size_t wordEnd = 0;
    NextToken(text, length, clipEnd, wordBegin, wordEnd);
    const int which = SettingOf(text, wordBegin, wordEnd);
    if (which >= filePlaylist::SETTINGS) return;
    std::size_t valueBegin = 0;
    std::size_t valueEnd = 0;
    NextToken(text, length, wordEnd, valueBegin, valueEnd);
    plTask task;
    task.op = PL_OP::SETCLIP;
    task.clip = static_cast<int>(numbers[0]);
    task.setting = static_cast<std::uint8_t>(which);
    task.value = std::numeric_limits<float>::quiet_NaN(); // no value: follow the playlist
    if (valueBegin < valueEnd) {
      float v = 0.f;
      if (which == filePlaylist::QUALITY) {
        const int q = QualityOf(text, valueBegin, valueEnd);
        if (q < 0) return;
        v = static_cast<float>(q);
      } else if (!sfcues::Number(text, valueBegin, valueEnd, v)) {
        return;
      }
      task.value = v;
    }
    Submit(task, false);
  } else if (Is(text, begin, end, "next")) {
    Post(REQUEST_NEXT);
  } else if (Is(text, begin, end, "pause")) {
    Post(REQUEST_PAUSE);
  } else if (Is(text, begin, end, "resume")) {
    Post(REQUEST_RESUME);
  } else if (Is(text, begin, end, "getcontent")) {
    contentWanted.store(true, std::memory_order_release);
  } else if (Is(text, begin, end, "quality")) {
    const int q = QualityOf(text, argBegin, argEnd);
    if (q >= 0) quality = q;
  } else if (count < 1) {
    return; // the attributes below need their number
  } else if (Is(text, begin, end, "loop")) {
    loop = numbers[0] != 0.f ? 1 : 0;
  } else if (Is(text, begin, end, "speed")) {
    speed = numbers[0];
  } else if (Is(text, begin, end, "timestretch")) {
    timeStretch = numbers[0] != 0.f ? 1 : 0;
  } else if (Is(text, begin, end, "pitchshift")) {
    if (numbers[0] > 0.f) pitchShift = numbers[0];
  } else if (Is(text, begin, end, "pitchshiftcent")) {
    pitchShiftCents = numbers[0];
  } else if (Is(text, begin, end, "loopreport")) {
    loopReport = numbers[0] != 0.f ? 1 : 0;
  } else if (Is(text, begin, end, "reportprogress")) {
    reportProgress = numbers[0] != 0.f ? 1 : 0;
  }
}

// ─── the block ────────────────────────────────────────────────────────────────

CALC() {
  filePlaylist& p = *list;
  p.loop(loop.load() != 0)
      .speed(speed.load())
      .timeStretch(timeStretch.load() != 0)
      .pitchShift(pitchShift.load())
      .pitchShiftCents(pitchShiftCents.load())
      .quality(static_cast<pitchShifter::QUALITY>(quality.load()))
      .reportProgress(reportProgress.load() != 0);

  // A transport request, and getcontent, wait while a task is in flight, so
  // they find what the task did.
  if (inFlight.load(std::memory_order_acquire) <= 0) {
    switch (request.exchange(REQUEST_NONE, std::memory_order_acquire)) {
    case REQUEST_PLAY:
      p.play(requestClip.load(std::memory_order_relaxed));
      break;
    case REQUEST_NEXT:
      p.next();
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
    if (contentWanted.exchange(false, std::memory_order_acq_rel)) contentPending = true;
  }

  p();
  for (int c = 0; c < channelCount; ++c)
    outputs[c].SendBuffer(&p.out(static_cast<UInt>(c)), thread);
  outputs[channelCount].SendBuffer(&p.position(), thread);

  // Never Send* a control message from CALC(): FlushArmedMessages sends
  // these after the traversal.
  startedClip = p.started();
  doneClip = p.done();
  loopedClip = loopReport.load() != 0 ? p.looped() : 0;
  progressClip = p.progressDue() ? p.playing() : 0;
  progressValue = p.progress();
  if (startedClip != 0 || doneClip != 0 || loopedClip != 0 || progressClip != 0 || contentPending)
    MarkOutletArmed();

  // What the block let go is freed in the background.
  if (p.releasePending() && !releaseQueued.exchange(true, std::memory_order_acq_rel)) {
    plTask task;
    task.op = PL_OP::RELEASE;
    if (!Submit(task, false)) releaseQueued.store(false, std::memory_order_release);
  }
}

// After the block's traversal, on the audio thread: the content outlet
// first (right to left), then the notifications in the order they happened.
void dPlaylist::FlushArmedMessages(YSE::THREAD thread) {
  filePlaylist& p = *list;
  outlet& notify = outputs[channelCount + 1];
  outlet& content = outputs[channelCount + 2];

  // Within the capacity reserved at construction: no allocation. A line
  // that would not fit is not sent.
  auto fits = [&](const filePlaylist::clip& c) {
    return c.name.size() + c.file.size() + 256 <= line.capacity();
  };
  auto event = [&](const char* word, Int number) {
    const filePlaylist::clip* c = p.clipAt(number);
    if (c == nullptr || !fits(*c)) return;
    line.assign(word);
    line.push_back(' ');
    AppendInt(line, static_cast<int>(number));
    line.push_back(' ');
    line.append(c->name);
    notify.SendList(line, thread);
  };

  if (contentPending) {
    contentPending = false;
    line.assign("clips ");
    AppendInt(line, static_cast<int>(p.clipCount()));
    content.SendList(line, thread);
    for (UInt n = 1; n <= p.clipCount(); ++n) {
      const filePlaylist::clip* c = p.clipAt(static_cast<Int>(n));
      if (c == nullptr || !fits(*c)) continue;
      line.assign("clip ");
      AppendInt(line, static_cast<int>(n));
      line.push_back(' ');
      line.append(c->name);
      line.push_back(' ');
      line.append(c->file);
      for (float v : {c->lengthMs, c->startMs, c->endMs}) {
        line.push_back(' ');
        AppendNumber(line, v);
      }
      for (int k = 0; k < filePlaylist::SETTINGS; ++k) {
        if (std::isnan(c->settings[k])) continue;
        line.push_back(' ');
        line.append(SETTING_NAMES[k]);
        line.push_back(' ');
        AppendNumber(line, c->settings[k]);
      }
      content.SendList(line, thread);
    }
  }

  if (startedClip != 0) event("start", startedClip);
  if (loopedClip != 0) event("loopnotify", loopedClip);
  if (progressClip != 0) {
    line.assign("progress ");
    AppendInt(line, static_cast<int>(progressClip));
    line.push_back(' ');
    AppendNumber(line, progressValue);
    notify.SendList(line, thread);
  }
  if (doneClip != 0) event("done", doneClip);
}

// The top of a block, on the audio thread.
void dPlaylist::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  (void)thread;
  const int op = result.tag - TAG_BASE;
  if (op < 0 || op >= OP_COUNT) return;
  if (static_cast<PL_OP>(op) == PL_OP::RELEASE) {
    releaseQueued.store(false, std::memory_order_release);
    return;
  }
  inFlight.fetch_sub(1, std::memory_order_acq_rel);
  if (!result.ok) failed.fetch_add(1, std::memory_order_relaxed);
}

// ─── the background half ──────────────────────────────────────────────────────

bool dPlaylist::RunTask(char* args, std::size_t argBytes) {
  return RunPathTask(args, argBytes, nullptr);
}

bool dPlaylist::RunPathTask(char* args, std::size_t argBytes, const char* path) {
  static_assert(std::is_trivially_copyable<plTask>::value,
                "a plTask is copied into a scheduler slot with memcpy");
  if (argBytes != sizeof(plTask)) return false;
  plTask task;
  std::memcpy(&task, args, sizeof(task));
  task.name[sizeof(task.name) - 1] = '\0';

  // By id, not through the object: it may have been deleted since.
  std::shared_ptr<filePlaylist> found = FindList(task.id);
  if (found == nullptr) return false;

  switch (task.op) {
  case PL_OP::APPEND: {
    // The host resolver's answer, or the name as the patch gave it; the clip
    // is called by the last part of the name the patch gave.
    const std::string file = path != nullptr ? std::string(path) : std::string(task.name);
    return found->append(file, task.place, LastPart(task.name)) != 0;
  }
  case PL_OP::REMOVE:
    return found->remove(task.clip);
  case PL_OP::CLEAR:
    found->clear();
    return true;
  case PL_OP::MOVE:
    return found->move(task.clip, task.place);
  case PL_OP::SELECT:
    return task.normalized != 0 ? found->selection(task.clip, task.start, task.end)
                                : found->selectionMs(task.clip, task.start, task.end);
  case PL_OP::UNSELECT:
    return found->clearSelection(task.clip);
  case PL_OP::SETCLIP:
    return found->setting(task.clip, static_cast<filePlaylist::SETTING>(task.setting), task.value);
  case PL_OP::RELEASE:
    found->collect();
    return true;
  }
  return false;
}

// ─── the GUI value ────────────────────────────────────────────────────────────

// Host thread: one read of the list, under its own lock only.
std::vector<std::string> dPlaylist::Cells() const {
  std::vector<std::string> cells;
  const std::uint64_t serial = list->version();
  const std::vector<filePlaylist::clip> clips = list->clips();
  auto number = [](float v) {
    std::string s;
    AppendNumber(s, v);
    return s;
  };
  cells.reserve(HEAD_CELLS + CLIP_CELLS * clips.size());
  cells.push_back(std::to_string(serial));
  cells.push_back(std::to_string(clips.size()));
  cells.push_back(std::to_string(list->playing()));
  cells.push_back(list->paused() ? "1" : "0");
  cells.push_back(number(list->where()));
  for (const filePlaylist::clip& c : clips) {
    cells.push_back(c.name);
    cells.push_back(number(c.lengthMs));
    cells.push_back(number(c.startMs));
    cells.push_back(number(c.endMs));
  }
  return cells;
}

std::string dPlaylist::GetGuiValue() {
  std::string out;
  for (const std::string& cell : Cells()) {
    if (!out.empty()) out.push_back(' ');
    out.append(cell);
  }
  return out;
}

unsigned int dPlaylist::GetGuiValueCount() const {
  return HEAD_CELLS + CLIP_CELLS * list->count();
}

std::string dPlaylist::GetGuiValueAt(unsigned int index) {
  const std::vector<std::string> cells = Cells();
  return index < cells.size() ? cells[index] : std::string();
}

// ─── saving ───────────────────────────────────────────────────────────────────

// Control thread, under DumpJSON. Nothing for an empty list, so a new object
// dumps as it always did.
bool dPlaylist::DumpState(nlohmann::json::value_type& json) {
  for (const filePlaylist::clip& c : list->clips()) {
    nlohmann::json entry;
    entry["file"] = c.file;
    entry["name"] = c.name;
    if (c.selected) {
      entry["start"] = c.startMs;
      entry["end"] = c.endMs;
    }
    for (int k = 0; k < filePlaylist::SETTINGS; ++k)
      if (!std::isnan(c.settings[k])) entry[SETTING_NAMES[k]] = c.settings[k];
    json["clips"].push_back(entry);
  }
  return true;
}

// Control thread, on a fresh object the audio thread cannot see yet, after
// SetParent and PARM_PARSE. The files are opened here and now.
void dPlaylist::RestoreState(const nlohmann::json::value_type& json) {
  const auto stored = json.find("clips");
  if (stored == json.end() || !stored->is_array()) return;
  for (const auto& entry : *stored) {
    if (!entry.is_object()) continue;
    const Int n =
        list->append(entry.value("file", std::string()), 0, entry.value("name", std::string()));
    if (n == 0) {
      failed.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    if (entry.find("start") != entry.end() &&
        !list->selectionMs(n, entry.value("start", 0.f), entry.value("end", 0.f)))
      failed.fetch_add(1, std::memory_order_relaxed);
    for (int k = 0; k < filePlaylist::SETTINGS; ++k) {
      const auto v = entry.find(SETTING_NAMES[k]);
      if (v != entry.end() && v->is_number())
        list->setting(n, static_cast<filePlaylist::SETTING>(k), v->get<float>());
    }
  }
}

#undef className
