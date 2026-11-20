#include "dSflist.h"
#include "sfCues.h"
#include <cstring>
#include <type_traits>

using namespace YSE::PATCHER;
using YSE::DSP::fileStreamCues;

namespace {

  enum class LIST_OP : std::uint8_t {
    OPEN,
    CLOSE,
    PRELOAD,
    CLEAR,
  };
  constexpr int TAG = 1155100;
  constexpr std::size_t CLEAR_CAPACITY = 64;

} // namespace

// The record a request becomes. Copied whole into a scheduler slot, so it
// stays trivially copyable.
struct dSflist::listTask {
  LIST_OP op = LIST_OP::OPEN;
  std::uint8_t normalized = 0;
  std::int32_t cue = 0;
  float start = 0.f;
  float end = 0.f;
  float speed = 1.f;
  std::int32_t count = 0;
  std::int32_t cues[CLEAR_CAPACITY] = {};
  char table[fileScheduler::PATH_CAPACITY] = {}; // the table's name
  char name[fileScheduler::PATH_CAPACITY] = {}; // a file, empty for the open one
};

#define className dSflist

CONSTRUCT() {
  embed = 0;
  table = fileStreamCues::acquire(std::string());

  ADD_IN_0;
  INLET_DOC(0, "messages",
            "'open <file>' (the file a preload without one uses), 'fclose [file]' (its cues "
            "removed; none: the open file), 'preload <cue> [file] [start ms] [end ms] "
            "[direction] [speed]', 'preloadn' (the same with start and end from 0 to 1), "
            "'clear [cues]' and 'embed <0/1>'.",
            "");

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(tableName);
  ADD_PARAM(bufferSize);

  ADD_DESCRIPTION("Keeps cues of sound files under a name: the ~sfplay objects created with "
                  "that name play them, each streaming from disk.");
  ADD_CATEGORY(pCategory::SAMPLING);
  PARAM_DOC("name", "", "The name ~sfplay objects play these cues by.", "cue list name");
  PARAM_DOC("buffersize", "0",
            "Read for compatibility, without effect: the engine streams through buffers of a "
            "fixed size.",
            "any");
}

PARM_CLEAR() {
  tableName.clear();
  bufferSize = 0;
}

// Control thread, on an object no other thread has seen yet.
PARM_PARSE() {
  table = fileStreamCues::acquire(tableName);
}

void dSflist::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  EnableFileIO();
}

bool dSflist::Submit(listTask& task, bool resolve) {
  fileScheduler* io = FileIO();
  const std::size_t tableLength = tableName.size();
  if (io == nullptr || tableLength == 0 || tableLength >= sizeof(task.table)) {
    failed.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  std::memcpy(task.table, tableName.c_str(), tableLength + 1);
  const std::size_t length = std::strlen(task.name);
  const bool ok = resolve && length > 0
                      ? io->RequestPathTask(this, TAG, PA_READ, task.name, length,
                                            &dSflist::RunPathTask, &task, sizeof(task))
                      : io->RequestTask(this, TAG, &dSflist::RunTask, &task, sizeof(task));
  if (!ok) failed.fetch_add(1, std::memory_order_relaxed);
  return ok;
}

// Any thread. Read in place: nothing allocates.
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
  std::size_t argBegin = 0;
  std::size_t argEnd = 0;
  NextToken(text, length, end, argBegin, argEnd);
  auto nameInto = [&](listTask& task, std::size_t from, std::size_t to) {
    const std::size_t n = to - from;
    if (n >= sizeof(task.name)) return false;
    std::memcpy(task.name, text + from, n);
    task.name[n] = '\0';
    return true;
  };

  if (Is(text, begin, end, "open")) {
    listTask task;
    task.op = LIST_OP::OPEN;
    if (argBegin < argEnd && nameInto(task, argBegin, argEnd)) Submit(task, true);
  } else if (Is(text, begin, end, "fclose")) {
    listTask task;
    task.op = LIST_OP::CLOSE;
    if (nameInto(task, argBegin, argEnd)) Submit(task, true);
  } else if (Is(text, begin, end, "preload") || Is(text, begin, end, "preloadn")) {
    sfcues::preloadArgs args;
    if (!sfcues::ParsePreload(text, length, end, args)) return;
    listTask task;
    task.op = LIST_OP::PRELOAD;
    task.normalized = Is(text, begin, end, "preloadn") ? 1 : 0;
    task.cue = args.cue;
    task.start = args.start;
    task.end = args.end;
    task.speed = args.speed;
    if (nameInto(task, args.fileBegin, args.fileEnd)) Submit(task, true);
  } else if (Is(text, begin, end, "clear")) {
    listTask task;
    task.op = LIST_OP::CLEAR;
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
  } else if (Is(text, begin, end, "embed")) {
    float number = 0.f;
    if (sfcues::Number(text, argBegin, argEnd, number)) embed = number != 0.f ? 1 : 0;
  }
}

void dSflist::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  (void)thread;
  if (result.tag != TAG) return;
  if (!result.ok) failed.fetch_add(1, std::memory_order_relaxed);
}

// ─── the background half ──────────────────────────────────────────────────────

bool dSflist::RunTask(char* args, std::size_t argBytes) {
  return RunPathTask(args, argBytes, nullptr);
}

bool dSflist::RunPathTask(char* args, std::size_t argBytes, const char* path) {
  static_assert(std::is_trivially_copyable<listTask>::value,
                "a listTask is copied into a scheduler slot with memcpy");
  if (argBytes != sizeof(listTask)) return false;
  listTask task;
  std::memcpy(&task, args, sizeof(task));
  task.table[sizeof(task.table) - 1] = '\0';
  task.name[sizeof(task.name) - 1] = '\0';

  // By name: the object may have been deleted since, and its table with it.
  std::shared_ptr<fileStreamCues> found = fileStreamCues::find(task.table);
  if (found == nullptr) return false;
  const std::string file = path != nullptr ? std::string(path) : std::string(task.name);

  switch (task.op) {
  case LIST_OP::OPEN:
    return found->open(file);
  case LIST_OP::CLOSE:
    return found->close(file.empty() ? found->file() : file) > 0 || file.empty();
  case LIST_OP::PRELOAD:
    return found->preload(task.cue, file, task.start, task.end, task.speed, task.normalized != 0);
  case LIST_OP::CLEAR:
    if (task.count == 0) {
      found->clear();
    } else {
      for (std::int32_t i = 0; i < task.count; ++i)
        found->remove(task.cues[i]);
    }
    return true;
  }
  return false;
}

// ─── embed ────────────────────────────────────────────────────────────────────

bool dSflist::DumpState(nlohmann::json::value_type& json) {
  if (embed.load() == 0) return true;
  json["embed"] = 1;
  const std::string file = table->file();
  if (!file.empty()) json["file"] = file;
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

// Control thread, on a fresh object, after PARM_PARSE: the files are checked
// here and now, as ~buffer reads its file argument. A table somebody already
// filled keeps its cues.
void dSflist::RestoreState(const nlohmann::json::value_type& json) {
  if (json.value("embed", 0) == 0) return;
  embed = 1;
  if (table->count() != 0) return;
  const std::string file = json.value("file", std::string());
  if (!file.empty() && !table->open(file)) failed.fetch_add(1, std::memory_order_relaxed);
  const auto stored = json.find("cues");
  if (stored == json.end() || !stored->is_array()) return;
  for (const auto& entry : *stored) {
    if (!entry.is_object()) continue;
    if (!table->preload(entry.value("cue", 0), entry.value("file", std::string()),
                        entry.value("start", 0.f), entry.value("end", 0.f),
                        entry.value("speed", 1.f)))
      failed.fetch_add(1, std::memory_order_relaxed);
  }
}

#undef className
