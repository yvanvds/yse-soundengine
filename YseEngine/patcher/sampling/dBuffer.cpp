#include "dBuffer.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include "../../implementations/logImplementation.h"
#include <cstdio>
#include <cstring>
#include <type_traits>

using namespace YSE::PATCHER;

#define className dBuffer

namespace {

  enum class TASK_OP : std::uint8_t {
    READ,
    WRITE,
    FILL,
    FILL_SINE,
    WINDOW,
    GAIN,
    OFFSET,
    DELTAS,
    NORMALIZE,
    SIZE_MS,
    SIZE_SAMPLES,
    CROP_MS,
    CROP_SAMPLES,
    DUPLICATE,
    RATE,
    QUERY,
  };

  enum class QUERY : std::uint8_t {
    SAMPS,
    SIZE,
    SR,
    CHANS,
  };

  // A word of a message, in place: no copy, no allocation.
  struct token {
    const char* text = nullptr;
    std::size_t length = 0;
  };

  constexpr int MAX_TOKENS = 8;

  int Tokenize(const std::string& message, token* out, int max) {
    const char* text = message.c_str();
    const std::size_t length = message.size();
    std::size_t at = 0;
    int count = 0;
    while (count < max) {
      while (at < length && IsSelectorSeparator(text[at]))
        at++;
      if (at >= length) break;
      const std::size_t begin = at;
      while (at < length && !IsSelectorSeparator(text[at]))
        at++;
      out[count].text = text + begin;
      out[count].length = at - begin;
      count++;
    }
    return count;
  }

  bool Is(const token& t, const char* word) {
    const std::size_t n = std::strlen(word);
    return t.length == n && std::memcmp(t.text, word, n) == 0;
  }

  bool Number(const token& t, float& out) {
    return t.length > 0 && YSE::PATCHER::ReadNumericToken(t.text, t.length, out);
  }

  // The window names `apply` takes.
  bool WindowNamed(const token& t, YSE::DSP::SAMPLE_WINDOW& out) {
    using YSE::DSP::SAMPLE_WINDOW;
    if (Is(t, "triangle"))
      out = SAMPLE_WINDOW::TRIANGLE;
    else if (Is(t, "hamming"))
      out = SAMPLE_WINDOW::HAMMING;
    else if (Is(t, "hanning") || Is(t, "hann"))
      out = SAMPLE_WINDOW::HANNING;
    else if (Is(t, "blackman"))
      out = SAMPLE_WINDOW::BLACKMAN;
    else if (Is(t, "welch"))
      out = SAMPLE_WINDOW::WELCH;
    else if (Is(t, "kaiser"))
      out = SAMPLE_WINDOW::KAISER;
    else
      return false;
    return true;
  }

  constexpr std::size_t REPLY_CAPACITY = 64;

} // namespace

// The record one message becomes. Copied whole into a scheduler slot, so it
// must stay trivially copyable; the task writes its reply into the same
// record, and the completion reads it back.
struct dBuffer::bufferTask {
  std::uint64_t id = 0;
  TASK_OP op = TASK_OP::QUERY;
  std::uint8_t flag = 0; // replace / cosine / half
  std::uint8_t which = 0; // a QUERY, a SAMPLE_WINDOW or a SAMPLE_FILE
  std::int32_t channels = -1; // -1: not given
  float a = 0.f;
  float b = -1.f;
  std::uint32_t n = 0;
  std::uint32_t m = 0;

  // The reply: the buffer's shape once the task has run.
  std::uint32_t replyFrames = 0;
  std::uint32_t replyChannels = 0;
  float replyRate = 0.f;
  float replyMs = 0.f;

  char text[fileScheduler::PATH_CAPACITY] = {}; // a path, or a buffer name
};

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
  // Synchronous: this is the control thread, and a patch that has just been
  // loaded should have its samples. The file's channel count wins unless a
  // channel argument was given.
  if (file != nullptr && !buffer->read(file->c_str(), 0.f, -1.f, channels > 0 ? channels : 0)) {
    failed.fetch_add(1, std::memory_order_relaxed);
    YSE::INTERNAL::LogImpl().emit(YSE::E_ERROR, "~buffer: cannot read \"" + *file + "\"");
  }
}

// ─── messages ─────────────────────────────────────────────────────────────────

bool dBuffer::Submit(bufferTask& task) {
  fileScheduler* io = FileIO();
  if (io == nullptr) return false;
  task.id = bufferId;
  return io->RequestTask(this, 0, &dBuffer::RunTask, &task, sizeof(task));
}

// Every message becomes one task. No allocation here: the record lives on
// the stack and the scheduler copies it into a slot it already owns.
MESSAGES() {
  (void)value;
  token words[MAX_TOKENS];
  const int count = Tokenize(message, words, MAX_TOKENS);
  if (count == 0) return;
  const token& word = words[0];

  bufferTask task;
  float x = 0.f;
  float y = 0.f;

  // A path or a name into task.text; false when there is none or it is too
  // long to carry.
  auto takeText = [&](int index) {
    if (index >= count || words[index].length >= sizeof(task.text)) return false;
    std::memcpy(task.text, words[index].text, words[index].length);
    task.text[words[index].length] = '\0';
    return true;
  };

  if (Is(word, "read") || Is(word, "replace")) {
    if (!takeText(1)) return;
    task.op = TASK_OP::READ;
    task.flag = Is(word, "replace") ? 1 : 0;
    task.a = count > 2 && Number(words[2], x) ? x : 0.f;
    task.b = count > 3 && Number(words[3], x) ? x : -1.f;
    task.channels = count > 4 && Number(words[4], x) ? static_cast<std::int32_t>(x) : -1;
    // Remembered for readagain, which repeats it as sent.
    std::memcpy(lastPath, task.text, sizeof(lastPath));
    lastStart = task.a;
    lastDuration = task.b;
    lastChannels = task.channels;
    lastReplace = task.flag != 0;
  } else if (Is(word, "readagain")) {
    if (lastPath[0] == '\0') return;
    task.op = TASK_OP::READ;
    std::memcpy(task.text, lastPath, sizeof(task.text));
    task.flag = lastReplace ? 1 : 0;
    task.a = lastStart;
    task.b = lastDuration;
    task.channels = lastChannels;
  } else if (Is(word, "write") || Is(word, "writewave") || Is(word, "writeaiff") ||
             Is(word, "writeflac")) {
    if (!takeText(1)) return;
    task.op = TASK_OP::WRITE;
    DSP::SAMPLE_FILE format = DSP::SAMPLE_FILE::AUTO;
    if (Is(word, "writewave")) format = DSP::SAMPLE_FILE::WAV;
    if (Is(word, "writeaiff")) format = DSP::SAMPLE_FILE::AIFF;
    if (Is(word, "writeflac")) format = DSP::SAMPLE_FILE::FLAC;
    task.which = static_cast<std::uint8_t>(format);
  } else if (Is(word, "clear")) {
    task.op = TASK_OP::FILL;
    task.a = 0.f;
  } else if (Is(word, "fill")) {
    if (count < 2) return;
    if (Number(words[1], x)) {
      task.op = TASK_OP::FILL;
      task.a = x;
    } else if (Is(words[1], "sin") || Is(words[1], "cos")) {
      task.op = TASK_OP::FILL_SINE;
      task.flag = Is(words[1], "cos") ? 1 : 0;
      task.a = count > 2 && Number(words[2], x) ? x : 1.f;
      task.b = count > 3 && Number(words[3], y) && y != 0.f ? y : 1.f;
    } else {
      return;
    }
  } else if (Is(word, "apply")) {
    if (count < 2) return;
    DSP::SAMPLE_WINDOW window = DSP::SAMPLE_WINDOW::HANNING;
    if (Is(words[1], "gain") || Is(words[1], "offset")) {
      if (count < 3 || !Number(words[2], x)) return;
      task.op = Is(words[1], "gain") ? TASK_OP::GAIN : TASK_OP::OFFSET;
      task.a = x;
    } else if (Is(words[1], "getdeltas")) {
      task.op = TASK_OP::DELTAS;
    } else if (WindowNamed(words[1], window)) {
      task.op = TASK_OP::WINDOW;
      task.which = static_cast<std::uint8_t>(window);
      task.a = 6.f;
      int next = 2;
      if (window == DSP::SAMPLE_WINDOW::KAISER && next < count && Number(words[next], x)) {
        task.a = x;
        next++;
      }
      task.flag = next < count && Is(words[next], "half") ? 1 : 0;
    } else {
      return;
    }
  } else if (Is(word, "normalize")) {
    task.op = TASK_OP::NORMALIZE;
    task.a = count > 1 && Number(words[1], x) ? x : 1.f;
  } else if (Is(word, "setsize") || Is(word, "sizeinsamps")) {
    if (count < 2 || !Number(words[1], x) || x < 0.f) return;
    const bool samples = Is(word, "sizeinsamps");
    task.op = samples ? TASK_OP::SIZE_SAMPLES : TASK_OP::SIZE_MS;
    task.a = x;
    task.n = samples ? static_cast<std::uint32_t>(x) : 0;
    task.channels =
        count > 2 && Number(words[2], y) && y >= 1.f ? static_cast<std::int32_t>(y) : -1;
  } else if (Is(word, "crop") || Is(word, "crop_samples")) {
    if (count < 3 || !Number(words[1], x) || !Number(words[2], y) || x < 0.f || y < 0.f) return;
    const bool samples = Is(word, "crop_samples");
    task.op = samples ? TASK_OP::CROP_SAMPLES : TASK_OP::CROP_MS;
    task.a = x;
    task.b = y;
    task.n = static_cast<std::uint32_t>(x);
    task.m = static_cast<std::uint32_t>(y);
  } else if (Is(word, "duplicate")) {
    if (!takeText(1)) return;
    task.op = TASK_OP::DUPLICATE;
  } else if (Is(word, "sr")) {
    if (count < 2 || !Number(words[1], x) || x < 0.f) return;
    task.op = TASK_OP::RATE;
    task.a = x;
  } else if (Is(word, "getsamps") || Is(word, "getsize") || Is(word, "getsr") ||
             Is(word, "getchans")) {
    task.op = TASK_OP::QUERY;
    QUERY query = QUERY::SAMPS;
    if (Is(word, "getsize")) query = QUERY::SIZE;
    if (Is(word, "getsr")) query = QUERY::SR;
    if (Is(word, "getchans")) query = QUERY::CHANS;
    task.which = static_cast<std::uint8_t>(query);
  } else {
    return;
  }

  Submit(task);
}

// ─── the background half ──────────────────────────────────────────────────────

bool dBuffer::RunTask(char* args, std::size_t argBytes) {
  static_assert(std::is_trivially_copyable<bufferTask>::value,
                "a bufferTask is copied into a scheduler slot with memcpy");
  if (argBytes != sizeof(bufferTask)) return false;
  bufferTask task;
  std::memcpy(&task, args, sizeof(task));

  // By id, not through the object: it may have been deleted since.
  std::shared_ptr<DSP::sampleBuffer> buf = DSP::sampleBufferRegistry::findById(task.id);
  if (buf == nullptr) return false;

  bool ok = true;
  switch (task.op) {
  case TASK_OP::READ:
    ok = task.flag != 0 ? buf->replace(task.text)
                        : buf->read(task.text, task.a, task.b, task.channels);
    break;
  case TASK_OP::WRITE:
    ok = buf->write(task.text, static_cast<DSP::SAMPLE_FILE>(task.which));
    break;
  case TASK_OP::FILL:
    buf->fill(task.a);
    break;
  case TASK_OP::FILL_SINE:
    buf->fillSine(task.a, task.b, task.flag != 0);
    break;
  case TASK_OP::WINDOW:
    buf->applyWindow(static_cast<DSP::SAMPLE_WINDOW>(task.which), task.flag != 0, task.a);
    break;
  case TASK_OP::GAIN:
    buf->applyGain(task.a);
    break;
  case TASK_OP::OFFSET:
    buf->applyOffset(task.a);
    break;
  case TASK_OP::DELTAS:
    buf->applyDeltas();
    break;
  case TASK_OP::NORMALIZE:
    buf->normalize(task.a);
    break;
  case TASK_OP::SIZE_MS:
    buf->setSizeMs(task.a, task.channels);
    break;
  case TASK_OP::SIZE_SAMPLES:
    buf->setSize(task.n, task.channels);
    break;
  case TASK_OP::CROP_MS:
    ok = buf->cropMs(task.a, task.b);
    break;
  case TASK_OP::CROP_SAMPLES:
    ok = buf->crop(task.n, task.m);
    break;
  case TASK_OP::DUPLICATE: {
    std::shared_ptr<DSP::sampleBuffer> source = DSP::sampleBufferRegistry::find(task.text);
    ok = source != nullptr;
    if (ok) buf->copyFrom(*source);
    break;
  }
  case TASK_OP::RATE:
    buf->setSampleRate(task.a);
    break;
  case TASK_OP::QUERY:
    break;
  }

  task.replyFrames = buf->frames();
  task.replyChannels = buf->channels();
  task.replyRate = buf->sampleRate();
  task.replyMs = buf->lengthMs();
  std::memcpy(args, &task, sizeof(task));
  return ok;
}

// The top of a block, on the audio thread, inside its own dispatch frame.
void dBuffer::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  if (result.op != FILE_OP::TASK || result.bytes == nullptr ||
      result.byteCount != sizeof(bufferTask)) {
    return;
  }
  if (!result.ok) {
    failed.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  bufferTask task;
  std::memcpy(&task, result.bytes, sizeof(task));

  if (task.op == TASK_OP::READ) {
    outputs[0].SendBang(thread);
    return;
  }
  if (task.op != TASK_OP::QUERY) return;

  char text[REPLY_CAPACITY];
  int length = 0;
  switch (static_cast<QUERY>(task.which)) {
  case QUERY::SAMPS:
    length = std::snprintf(text, sizeof(text), "samps %u", static_cast<unsigned>(task.replyFrames));
    break;
  case QUERY::SIZE:
    length = std::snprintf(text, sizeof(text), "size %g", static_cast<double>(task.replyMs));
    break;
  case QUERY::SR:
    length = std::snprintf(text, sizeof(text), "sr %g", static_cast<double>(task.replyRate));
    break;
  case QUERY::CHANS:
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
