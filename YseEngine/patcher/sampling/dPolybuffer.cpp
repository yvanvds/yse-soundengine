#include "dPolybuffer.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include "../../implementations/logImplementation.h"
#include <cinttypes>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <type_traits>

using namespace YSE::PATCHER;

#define className dPolybuffer

namespace {

  enum class POLY_OP : std::uint8_t {
    APPEND,
    APPEND_EMPTY,
    READ_FOLDER,
    WRITE_FOLDER,
    CLEAR,
    SEND,
    DUMP,
    BUFFER_LIST,
    COUNT,
    SHORT_NAMES,
    SIZE,
  };

  // Which request a result answers.
  constexpr int TAG_MESSAGE = 0;
  constexpr int TAG_RESTORE = 1;

  bool IsQuery(POLY_OP op) {
    return op == POLY_OP::DUMP || op == POLY_OP::BUFFER_LIST || op == POLY_OP::COUNT ||
           op == POLY_OP::SHORT_NAMES || op == POLY_OP::SIZE;
  }

  // The word starting at or after `from`, in place: [begin, begin + length).
  // Returns false at the end of the text.
  bool NextWord(const std::string& text, std::size_t& from, std::size_t& begin,
                std::size_t& length) {
    std::size_t at = from;
    while (at < text.size() && IsSelectorSeparator(text[at]))
      at++;
    if (at >= text.size()) {
      from = at;
      return false;
    }
    begin = at;
    while (at < text.size() && !IsSelectorSeparator(text[at]))
      at++;
    length = at - begin;
    from = at;
    return true;
  }

  bool Is(const std::string& text, std::size_t begin, std::size_t length, const char* word) {
    const std::size_t n = std::strlen(word);
    return length == n && text.compare(begin, n, word) == 0;
  }

} // namespace

// The record one message becomes, copied whole into a scheduler slot. The
// head carries the request and the task's verdict; the task writes an
// answer's lines into `reply`, each ended by a '\0'.
struct dPolybuffer::polyTask {
  struct header {
    std::uint64_t setId = 0;
    POLY_OP op = POLY_OP::COUNT;
    std::uint32_t index = 0; // send: the buffer, from 1; 0 every buffer
    float ms = 0.f; // appendempty
    std::int32_t channels = 1; // appendempty
    bufferMessage message; // send; its text is the file or folder of the others
    std::uint32_t replyLength = 0;
    std::uint8_t replyTruncated = 0;
  };
  header head;
  char reply[REPLY_CAPACITY] = {};
};

CONSTRUCT_DSP() {
  request = std::make_unique<polyTask>();
  restoreRequest = std::make_unique<polyTask>();
  line.reserve(REPLY_CAPACITY);

  ADD_IN_0;

  ADD_OUT_LIST;
  ADD_OUT_BANG;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(setName);
  ADD_PARAM(embed);
  ADD_PARAM(quiet);

  // A set of its own until the arguments name one, so Set() is never null.
  Bind();

  ADD_DESCRIPTION("A numbered collection of buffers, name.1, name.2 and so on, that sampling "
                  "objects play by name. Loads sound files one at a time or a whole folder, "
                  "writes them all to a folder, and passes buffer messages to one or all.");
  ADD_CATEGORY(pCategory::SAMPLING);
  INLET_DOC(0, "messages",
            "append <file>, appendempty <ms> <channels>, readfolder <folder>, writetofolder "
            "<folder>, clear, send <index> <buffer message> (index 0 is every buffer), and the "
            "queries dump, getbufferlist, getcount, getshortname, getsize. Each runs in the "
            "background, in the order sent.",
            "");
  OUTLET_DOC(0, "info",
             "The answer to a query: 'index name path ms channels rate' per buffer for dump, "
             "'bufferlist <names>' then 'done', '<name> <short name>' per buffer then 'done', "
             "'count <n>', or 'size <bytes>'.",
             "list");
  OUTLET_DOC(1, "done",
             "A bang when an append, appendempty, readfolder, writetofolder, clear or send has "
             "finished.",
             "bang");
  PARAM_DOC("name", "",
            "The collection's name; its buffers are name.1, name.2 and so on. Objects with the "
            "same name share the buffers. Without one the collection gets a name of its own.",
            "any symbol");
  PARAM_DOC("embed", "0",
            "1 saves the buffers with the patch, as the files they were read from (empty ones "
            "by length and channels), and loads them again when the patch is opened.",
            "0 or 1");
  PARAM_DOC("quiet", "0",
            "1 leaves out the warning when a patch opens a collection that is already loaded.",
            "0 or 1");
}

dPolybuffer::~dPolybuffer() = default;

PARM_CLEAR() {
  setName.clear();
  embed = 0;
  quiet = 0;
}

// Control thread: a change of argument rebuilds the object, so this runs on an
// object no audio thread has seen yet.
PARM_PARSE() {
  Bind();
}

void dPolybuffer::Bind() {
  if (setName.empty()) {
    if (set == nullptr) set = DSP::sampleBufferSet::acquire(std::string(), &createdSet);
  } else if (set == nullptr || set->name() != setName) {
    set = DSP::sampleBufferSet::acquire(setName, &createdSet);
  }
  setId = set->id();
}

void dPolybuffer::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler every message goes through, built here so a message on the
  // audio thread finds it.
  EnableFileIO();
}

// ─── messages ─────────────────────────────────────────────────────────────────

bool dPolybuffer::Submit(polyTask& task, int tag, const char* path, PATH_ACCESS access) {
  fileScheduler* io = FileIO();
  if (io == nullptr) return false;
  task.head.setId = setId;
  const bool sent = path != nullptr
                        ? io->RequestPathTask(this, tag, access, path, std::strlen(path),
                                              &dPolybuffer::RunPathTask, &task, sizeof(task))
                        : io->RequestTask(this, tag, &dPolybuffer::RunTask, &task, sizeof(task));
  if (!sent) dropped.fetch_add(1, std::memory_order_relaxed);
  return sent;
}

// Every message becomes one task, built in the object's own record: nothing is
// allocated on any thread.
MESSAGES() {
  (void)value;
  if (busy.exchange(true, std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  struct release {
    std::atomic<bool>& flag;
    ~release() {
      flag.store(false, std::memory_order_release);
    }
  } guard{busy};

  std::size_t at = 0;
  std::size_t begin = 0;
  std::size_t length = 0;
  if (!NextWord(message, at, begin, length)) return;
  const std::size_t wordBegin = begin;
  const std::size_t wordLength = length;
  auto word = [&](const char* w) { return Is(message, wordBegin, wordLength, w); };

  polyTask& task = *request;
  task.head = polyTask::header();
  char* text = task.head.message.text;

  // The next word into the record's text: a file or a folder.
  auto takeName = [&]() {
    if (!NextWord(message, at, begin, length) || length >= sizeof(task.head.message.text))
      return false;
    std::memcpy(text, message.data() + begin, length);
    text[length] = '\0';
    return true;
  };
  // The next word as a number.
  auto takeNumber = [&](float& out) {
    return NextWord(message, at, begin, length) &&
           ReadNumericToken(message.data() + begin, length, out);
  };

  if (word("append")) {
    // Without a name Max opens a dialog, which is the host's.
    if (!takeName()) return;
    task.head.op = POLY_OP::APPEND;
    Submit(task, TAG_MESSAGE, text, PA_READ);
  } else if (word("appendempty")) {
    float ms = 0.f;
    float channels = 1.f;
    if (takeNumber(ms)) takeNumber(channels);
    task.head.op = POLY_OP::APPEND_EMPTY;
    task.head.ms = ms > 0.f ? ms : 0.f;
    task.head.channels = channels >= 1.f ? static_cast<std::int32_t>(channels) : 1;
    Submit(task, TAG_MESSAGE, nullptr, PA_READ);
  } else if (word("readfolder") || word("writetofolder")) {
    if (!takeName()) return;
    const bool write = word("writetofolder");
    task.head.op = write ? POLY_OP::WRITE_FOLDER : POLY_OP::READ_FOLDER;
    Submit(task, TAG_MESSAGE, text, write ? PA_WRITE : PA_READ);
  } else if (word("clear")) {
    task.head.op = POLY_OP::CLEAR;
    Submit(task, TAG_MESSAGE, nullptr, PA_READ);
  } else if (word("send")) {
    float index = 0.f;
    if (!takeNumber(index) || index < 0.f) return;
    // The rest is a ~buffer message; its queries have no outlet here.
    if (!ParseBufferMessage(message.data() + at, message.size() - at, task.head.message) ||
        task.head.message.op == BUFFER_OP::QUERY) {
      return;
    }
    task.head.op = POLY_OP::SEND;
    task.head.index = static_cast<std::uint32_t>(index);
    if (BufferMessageOpensFile(task.head.message)) {
      Submit(task, TAG_MESSAGE, text, BufferMessageAccess(task.head.message));
    } else {
      Submit(task, TAG_MESSAGE, nullptr, PA_READ);
    }
  } else if (word("dump") || word("getbufferlist") || word("getcount") || word("getshortname") ||
             word("getsize")) {
    POLY_OP op = POLY_OP::DUMP;
    if (word("getbufferlist")) op = POLY_OP::BUFFER_LIST;
    if (word("getcount")) op = POLY_OP::COUNT;
    if (word("getshortname")) op = POLY_OP::SHORT_NAMES;
    if (word("getsize")) op = POLY_OP::SIZE;
    task.head.op = op;
    Submit(task, TAG_MESSAGE, nullptr, PA_READ);
  }
  // open, wclose, mouse, drag and anything else: nothing.
}

// ─── embed ────────────────────────────────────────────────────────────────────

// Control thread, under the patcher's lock while it dumps. Every object of the
// set with embed on writes it (as every .coll of a store does); restoring is
// where the duplicates are resolved.
bool dPolybuffer::DumpState(nlohmann::json::value_type& json) {
  if (embed == 0) return true;
  for (const std::shared_ptr<DSP::sampleBuffer>& b : set->buffers()) {
    const DSP::sampleBufferInfo info = b->info();
    nlohmann::json entry;
    if (info.hasFile()) entry["file"] = info.filePath;
    entry["ms"] = info.lengthMs;
    entry["channels"] = info.channels;
    json["buffers"].push_back(entry);
  }
  return true;
}

// Control thread, on a fresh object the audio thread cannot see yet, after
// SetParent.
void dPolybuffer::RestoreState(const nlohmann::json::value_type& json) {
  const auto stored = json.find("buffers");
  if (stored == json.end() || !stored->is_array() || stored->empty()) return;
  // Only the object that created the set fills it: one that joins a set
  // something else already holds adopts what it holds.
  if (!createdSet || set->count() != 0) {
    if (quiet == 0) {
      YSE::INTERNAL::LogImpl().emit(YSE::E_WARNING, "~polybuffer: \"" + set->name() +
                                                        "\" is already loaded; keeping its "
                                                        "buffers");
    }
    return;
  }

  fileScheduler* io = FileIO();
  const bool resolve = io != nullptr && io->HasResolver();
  for (const auto& entry : *stored) {
    if (!entry.is_object()) continue;
    const float ms = entry.value("ms", 0.f);
    const unsigned int channels = entry.value("channels", 1u);
    // A place for every entry first, so a file that fails keeps the numbers of
    // those after it.
    set->appendEmpty(ms, channels);
    const std::string file = entry.value("file", std::string());
    if (file.empty()) continue;
    const std::size_t index = set->count();
    if (resolve) {
      pending.push_back({index, file});
    } else if (!set->buffer(index)->replace(file.c_str())) {
      // Read at once, as ~buffer reads its file argument: the patch just
      // loaded should have its samples.
      failed.fetch_add(1, std::memory_order_relaxed);
      YSE::INTERNAL::LogImpl().emit(YSE::E_ERROR, "~polybuffer: cannot read \"" + file + "\"");
    }
  }
  (void)ContinueRestore();
}

// RestoreState, then DeliverFileResult on the audio thread: one at a time, so
// the scheduler's few slots are never all taken by a long kit. Returns whether
// one was sent.
bool dPolybuffer::ContinueRestore() {
  while (pendingNext < pending.size()) {
    const restoreEntry& next = pending[pendingNext++];
    polyTask& task = *restoreRequest;
    task.head = polyTask::header();
    task.head.op = POLY_OP::SEND;
    task.head.index = static_cast<std::uint32_t>(next.index);
    task.head.message.op = BUFFER_OP::READ;
    task.head.message.flag = 1; // replace: the file's shape
    if (next.path.size() >= sizeof(task.head.message.text)) {
      failed.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    std::memcpy(task.head.message.text, next.path.c_str(), next.path.size() + 1);
    if (Submit(task, TAG_RESTORE, task.head.message.text, PA_READ)) return true;
    failed.fetch_add(1, std::memory_order_relaxed);
  }
  return false;
}

// ─── the background half ──────────────────────────────────────────────────────

bool dPolybuffer::RunTask(char* args, std::size_t argBytes) {
  return RunPathTask(args, argBytes, nullptr);
}

bool dPolybuffer::RunPathTask(char* args, std::size_t argBytes, const char* path) {
  static_assert(std::is_trivially_copyable<polyTask>::value,
                "a polyTask is copied into a scheduler slot with memcpy");
  static_assert(std::is_standard_layout<polyTask>::value, "the reply follows the header");
  if (argBytes != sizeof(polyTask)) return false;
  polyTask::header head;
  std::memcpy(&head, args, sizeof(head));
  char* reply = args + offsetof(polyTask, reply);
  head.replyLength = 0;
  head.replyTruncated = 0;

  // One line of the answer, if it fits.
  auto put = [&](const std::string& text) {
    if (head.replyLength + text.size() + 1 > REPLY_CAPACITY) {
      head.replyTruncated = 1;
      return false;
    }
    std::memcpy(reply + head.replyLength, text.c_str(), text.size() + 1);
    head.replyLength += static_cast<std::uint32_t>(text.size() + 1);
    return true;
  };

  // By id, not through the object: it may have been deleted since.
  std::shared_ptr<DSP::sampleBufferSet> found = DSP::sampleBufferSet::findById(head.setId);
  bool ok = found != nullptr;
  if (ok) {
    DSP::sampleBufferSet& s = *found;
    // The file or folder to open: the host resolver's answer, or the name.
    const char* file = path != nullptr ? path : head.message.text;
    switch (head.op) {
    case POLY_OP::APPEND:
      ok = s.append(file) != nullptr;
      break;
    case POLY_OP::APPEND_EMPTY:
      s.appendEmpty(head.ms, static_cast<UInt>(head.channels));
      break;
    case POLY_OP::READ_FOLDER:
      ok = s.readFolder(file) > 0;
      break;
    case POLY_OP::WRITE_FOLDER:
      ok = s.writeToFolder(file) > 0 || s.count() == 0;
      break;
    case POLY_OP::CLEAR:
      s.clear();
      break;
    case POLY_OP::SEND: {
      const std::vector<std::shared_ptr<DSP::sampleBuffer>> list = s.buffers();
      if (head.index == 0) {
        for (const std::shared_ptr<DSP::sampleBuffer>& b : list) {
          bufferMessage message = head.message;
          ok = ApplyBufferMessage(*b, message, path) && ok;
        }
      } else if (head.index <= list.size()) {
        bufferMessage message = head.message;
        ok = ApplyBufferMessage(*list[head.index - 1], message, path);
      } else {
        ok = false;
      }
      break;
    }
    case POLY_OP::DUMP: {
      const std::vector<std::shared_ptr<DSP::sampleBuffer>> list = s.buffers();
      for (std::size_t i = 0; i < list.size(); ++i) {
        const DSP::sampleBufferInfo info = list[i]->info();
        char numbers[96];
        std::snprintf(numbers, sizeof(numbers), " %g %u %g", static_cast<double>(info.lengthMs),
                      static_cast<unsigned>(info.channels), static_cast<double>(info.sampleRate));
        if (!put(std::to_string(i + 1) + " " + s.bufferName(i + 1) + " " +
                 (info.hasFile() ? info.filePath : std::string("none")) + numbers)) {
          break;
        }
      }
      break;
    }
    case POLY_OP::BUFFER_LIST: {
      std::string names = "bufferlist";
      const std::size_t n = s.count();
      for (std::size_t i = 1; i <= n; ++i) {
        const std::string name = s.bufferName(i);
        if (names.size() + 1 + name.size() + 1 > REPLY_CAPACITY) {
          head.replyTruncated = 1;
          break;
        }
        names += " " + name;
      }
      put(names);
      break;
    }
    case POLY_OP::SHORT_NAMES: {
      const std::size_t n = s.count();
      for (std::size_t i = 1; i <= n; ++i) {
        const std::string shortName = s.shortName(i);
        if (!put(s.bufferName(i) + " " + (shortName.empty() ? std::string("none") : shortName)))
          break;
      }
      break;
    }
    case POLY_OP::COUNT:
      put("count " + std::to_string(s.count()));
      break;
    case POLY_OP::SIZE:
      put("size " + std::to_string(s.memoryBytes()));
      break;
    }
  }

  std::memcpy(args, &head, sizeof(head));
  return ok;
}

// The top of a block, on the audio thread, inside its own dispatch frame.
void dPolybuffer::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  if (result.op != FILE_OP::TASK || result.bytes == nullptr ||
      result.byteCount != sizeof(polyTask)) {
    return;
  }
  polyTask::header head;
  std::memcpy(&head, result.bytes, sizeof(head));

  if (result.tag == TAG_RESTORE) {
    if (!result.ok) failed.fetch_add(1, std::memory_order_relaxed);
    // The last embedded file has loaded (or failed): nothing more to send.
    if (!ContinueRestore()) outputs[1].SendBang(thread);
    return;
  }

  if (!result.ok) {
    failed.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (!IsQuery(head.op)) {
    outputs[1].SendBang(thread);
    return;
  }

  if (head.replyTruncated != 0) truncated.fetch_add(1, std::memory_order_relaxed);
  const char* reply = result.bytes + offsetof(polyTask, reply);
  const std::size_t total = head.replyLength <= REPLY_CAPACITY ? head.replyLength : 0;
  std::size_t at = 0;
  while (at < total) {
    const void* end = std::memchr(reply + at, '\0', total - at);
    const std::size_t length =
        end != nullptr ? static_cast<std::size_t>(static_cast<const char*>(end) - (reply + at))
                       : total - at;
    // Within the capacity reserved at construction: no allocation.
    line.assign(reply + at, length);
    outputs[0].SendList(line, thread);
    at += length + 1;
  }
  if (head.op == POLY_OP::BUFFER_LIST || head.op == POLY_OP::SHORT_NAMES) {
    line.assign("done");
    outputs[0].SendList(line, thread);
  }
}

#undef className
