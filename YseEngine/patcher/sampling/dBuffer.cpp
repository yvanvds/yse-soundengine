#include "dBuffer.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include "../../implementations/logImplementation.h"
#include <cstdio>
#include <cstring>

using namespace YSE::PATCHER;

#define className dBuffer

namespace {

  constexpr std::size_t REPLY_CAPACITY = 64;

  // Whether the message's first word is `word`. In place: no allocation.
  bool FirstWordIs(const std::string& message, const char* word) {
    std::size_t at = 0;
    while (at < message.size() && IsSelectorSeparator(message[at]))
      at++;
    const std::size_t n = std::strlen(word);
    if (message.compare(at, n, word) != 0) return false;
    return at + n == message.size() || IsSelectorSeparator(message[at + n]);
  }

} // namespace

CONSTRUCT_DSP() {
  reply.reserve(REPLY_CAPACITY);

  ADD_IN_0;

  ADD_OUT_BANG;
  ADD_OUT_LIST;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(bufferName);
  ADD_PARAM(rest);

  // A private buffer until the arguments name one, so Buffer() is never null.
  Bind();

  ADD_DESCRIPTION("A named buffer of audio samples that sampling objects play, record and "
                  "read. Loads and saves audio files, resizes, clears, fills and normalizes. "
                  "Buffers are shared by name across every patch and the host program.");
  ADD_CATEGORY(pCategory::SAMPLING);
  INLET_DOC(0, "messages",
            "read, replace, readagain, write (writewave, writeaiff, writeflac), clear, fill, "
            "apply, normalize, setsize, sizeinsamps, crop, crop_samples, duplicate, sr, and "
            "the queries getsamps, getsize, getsr, getchans. Each runs in the background, in "
            "the order sent.",
            "");
  OUTLET_DOC(0, "loaded", "A bang when a read, replace or readagain has finished.", "bang");
  OUTLET_DOC(1, "info",
             "The answer to a query: samps <samples>, size <ms>, sr <Hz> or chans <count>.",
             "list");
  PARAM_DOC("name", "",
            "The name sampling objects and the host use for this buffer. Without one the "
            "buffer is private.",
            "any symbol");
  PARAM_DOC("file", "",
            "Optional: an audio file to load when the object is created, then the size in ms "
            "and the number of channels. A number in the file's place is the size.",
            "file, ms, count");
}

PARM_CLEAR() {
  bufferName.clear();
  rest.clear();
  argFile.clear();
  argChannels = -1;
}

// Control thread: a change of argument rebuilds the object (the callbacks
// make ParamsNeedRebuild() true), so this runs on an object no audio thread
// has seen yet. The names are process-wide, so nothing here needs the patcher.
PARM_PARSE() {
  Bind();
  ApplyArguments();
}

void dBuffer::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler every message goes through, built here so a message on the
  // audio thread finds it.
  EnableFileIO();
  // The file argument waits until now, the first moment the object knows
  // whether its patcher's host resolves paths (issue #1255).
  if (newParent != nullptr) LoadArgumentFile();
}

void dBuffer::Bind() {
  if (bufferName.empty()) {
    if (buffer == nullptr || !buffer->name().empty()) buffer = DSP::sampleBuffer::create();
  } else if (buffer == nullptr || buffer->name() != bufferName) {
    buffer = DSP::sampleBufferRegistry::acquire(bufferName);
  }
  bufferId = buffer->id();
}

void dBuffer::ApplyArguments() {
  std::size_t i = 0;
  float number = 0.f;
  const std::string* file = nullptr;
  if (i < rest.size() && !ReadNumericToken(rest[i], number)) file = &rest[i++];
  float duration = -1.f;
  int channels = -1;
  if (i < rest.size() && ReadNumericToken(rest[i], number)) {
    duration = number;
    i++;
  }
  if (i < rest.size() && ReadNumericToken(rest[i], number)) {
    channels = static_cast<int>(number);
    i++;
  }

  if (duration >= 0.f || channels > 0) {
    buffer->setSizeMs(duration >= 0.f ? duration : buffer->lengthMs(),
                      channels > 0 ? channels : -1);
  }
  // The file's channel count wins unless a channel argument was given.
  argFile = file != nullptr ? *file : std::string();
  argChannels = channels > 0 ? channels : 0;
}

void dBuffer::LoadArgumentFile() {
  if (argFile.empty()) return;
  const std::string file = std::move(argFile);
  argFile.clear();

  // While the host resolves paths (issue #1255), the name is the host's to
  // place, and its answer may come later: read it the way a `read` message
  // does. The file outlet bangs when it has loaded.
  fileScheduler* io = FileIO();
  if (io != nullptr && io->HasResolver()) {
    bufferMessage task;
    if (file.size() >= sizeof(task.text)) {
      failed.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    std::memcpy(task.text, file.c_str(), file.size() + 1);
    task.op = BUFFER_OP::READ;
    task.channels = argChannels;
    if (!Submit(task)) failed.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  // Synchronous: this is the control thread, and a patch that has just been
  // loaded should have its samples.
  if (!buffer->read(file.c_str(), 0.f, -1.f, argChannels)) {
    failed.fetch_add(1, std::memory_order_relaxed);
    YSE::INTERNAL::LogImpl().emit(YSE::E_ERROR, "~buffer: cannot read \"" + file + "\"");
  }
}

// ─── messages ─────────────────────────────────────────────────────────────────

bool dBuffer::Submit(bufferMessage& task) {
  fileScheduler* io = FileIO();
  if (io == nullptr) return false;
  task.id = bufferId;
  // A read or a write names a file, which the host's path resolver gets to
  // place (issue #1255). Every other task carries a buffer name or nothing.
  if (BufferMessageOpensFile(task)) {
    return io->RequestPathTask(this, 0, BufferMessageAccess(task), task.text,
                               std::strlen(task.text), &dBuffer::RunPathTask, &task, sizeof(task));
  }
  return io->RequestTask(this, 0, &dBuffer::RunTask, &task, sizeof(task));
}

// Every message becomes one task. No allocation here: the record lives on
// the stack and the scheduler copies it into a slot it already owns.
MESSAGES() {
  (void)value;
  bufferMessage task;
  if (FirstWordIs(message, "readagain")) {
    if (lastPath[0] == '\0') return;
    task.op = BUFFER_OP::READ;
    std::memcpy(task.text, lastPath, sizeof(task.text));
    task.flag = lastReplace ? 1 : 0;
    task.a = lastStart;
    task.b = lastDuration;
    task.channels = lastChannels;
  } else {
    if (!ParseBufferMessage(message.c_str(), message.size(), task)) return;
    if (task.op == BUFFER_OP::READ) {
      // Remembered for readagain, which repeats it as sent.
      std::memcpy(lastPath, task.text, sizeof(lastPath));
      lastStart = task.a;
      lastDuration = task.b;
      lastChannels = task.channels;
      lastReplace = task.flag != 0;
    }
  }

  Submit(task);
}

// ─── the background half ──────────────────────────────────────────────────────

bool dBuffer::RunTask(char* args, std::size_t argBytes) {
  return RunPathTask(args, argBytes, nullptr);
}

bool dBuffer::RunPathTask(char* args, std::size_t argBytes, const char* path) {
  if (argBytes != sizeof(bufferMessage)) return false;
  bufferMessage task;
  std::memcpy(&task, args, sizeof(task));

  // By id, not through the object: it may have been deleted since.
  std::shared_ptr<DSP::sampleBuffer> buf = DSP::sampleBufferRegistry::findById(task.id);
  if (buf == nullptr) return false;

  const bool ok = ApplyBufferMessage(*buf, task, path);
  std::memcpy(args, &task, sizeof(task));
  return ok;
}

// The top of a block, on the audio thread, inside its own dispatch frame.
void dBuffer::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  if (result.op != FILE_OP::TASK || result.bytes == nullptr ||
      result.byteCount != sizeof(bufferMessage)) {
    return;
  }
  if (!result.ok) {
    failed.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  bufferMessage task;
  std::memcpy(&task, result.bytes, sizeof(task));

  if (task.op == BUFFER_OP::READ) {
    outputs[0].SendBang(thread);
    return;
  }
  if (task.op != BUFFER_OP::QUERY) return;

  char text[REPLY_CAPACITY];
  int length = 0;
  switch (static_cast<BUFFER_QUERY>(task.which)) {
  case BUFFER_QUERY::SAMPS:
    length = std::snprintf(text, sizeof(text), "samps %u", static_cast<unsigned>(task.replyFrames));
    break;
  case BUFFER_QUERY::SIZE:
    length = std::snprintf(text, sizeof(text), "size %g", static_cast<double>(task.replyMs));
    break;
  case BUFFER_QUERY::SR:
    length = std::snprintf(text, sizeof(text), "sr %g", static_cast<double>(task.replyRate));
    break;
  case BUFFER_QUERY::CHANS:
    length =
        std::snprintf(text, sizeof(text), "chans %u", static_cast<unsigned>(task.replyChannels));
    break;
  }
  if (length <= 0) return;
  // Within the capacity reserved at construction: no allocation.
  reply.assign(text, static_cast<std::size_t>(length));
  outputs[1].SendList(reply, thread);
}

#undef className
