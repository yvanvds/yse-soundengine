#include "dSfinfo.h"
#include "../pSelector.h"
#include "../../internal/customFileReader.h"
#include "../../io.hpp"
#include <cstring>
#include <sndfile.hh>
#include <type_traits>

using namespace YSE::PATCHER;

#define className dSfinfo

namespace {

  constexpr std::size_t TEXT_CAPACITY = fileScheduler::PATH_CAPACITY;
  constexpr std::size_t TYPE_CAPACITY = 16;

  // Max's sample-type symbol and the sample size in bits for a libsndfile
  // subtype. An encoding without a fixed sample format (Vorbis, Opus, MPEG,
  // ADPCM, ...) is "other", 0 bits.
  const char* SampleType(int format, std::uint32_t& bits) {
    switch (format & SF_FORMAT_SUBMASK) {
    case SF_FORMAT_PCM_S8:
      bits = 8;
      return "int8";
    case SF_FORMAT_PCM_U8:
      bits = 8;
      return "uint8";
    case SF_FORMAT_PCM_16:
      bits = 16;
      return "int16";
    case SF_FORMAT_PCM_24:
      bits = 24;
      return "int24";
    case SF_FORMAT_PCM_32:
      bits = 32;
      return "int32";
    case SF_FORMAT_FLOAT:
      bits = 32;
      return "float32";
    case SF_FORMAT_DOUBLE:
      bits = 64;
      return "float64";
    case SF_FORMAT_ULAW:
      bits = 8;
      return "mulaw";
    case SF_FORMAT_ALAW:
      bits = 8;
      return "alaw";
    default:
      bits = 0;
      return "other";
    }
  }

  // An open sound file, through the host's virtual file system when it is
  // active, as DSP::sampleBuffer::read opens one.
  struct openFile {
    SndfileHandle handle;
    void* ioHandle = nullptr;

    explicit openFile(const char* path) {
      if (YSE::IO().getActive()) {
        long long size = 0;
        if (!YSE::INTERNAL::customFileReader::Open(path, &size, &ioHandle)) return;
        handle = SndfileHandle(YSE::INTERNAL::customFileReader::GetVIO(), ioHandle);
      } else {
        handle = SndfileHandle(path);
      }
    }
    ~openFile() {
      // The handle reads through ioHandle, so it closes first.
      handle = SndfileHandle();
      if (ioHandle != nullptr) YSE::INTERNAL::customFileReader::Close(ioHandle);
    }
    openFile(const openFile&) = delete;
    openFile& operator=(const openFile&) = delete;
    openFile(openFile&&) = delete;
    openFile& operator=(openFile&&) = delete;
  };

} // namespace

// The record a request becomes. Copied whole into a scheduler slot, so it
// must stay trivially copyable; the task writes the answer into the same
// record, and the completion reads it back.
struct dSfinfo::sfinfoTask {
  char name[TEXT_CAPACITY] = {}; // the file, as the patch named it

  // The answer.
  std::uint32_t channels = 0;
  std::uint32_t bits = 0;
  float rate = 0.f;
  float lengthMs = 0.f;
  char type[TYPE_CAPACITY] = {};
};

CONSTRUCT_DSP() {
  reply.reserve(TEXT_CAPACITY);

  ADD_IN_0;
  REG_BANG_IN(Bang);

  ADD_OUT_INT; // 0 channels
  ADD_OUT_INT; // 1 sample size
  ADD_OUT_FLOAT; // 2 sample rate
  ADD_OUT_FLOAT; // 3 length
  ADD_OUT_LIST; // 4 sample type
  ADD_OUT_LIST; // 5 file name

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(fileArgument);

  ADD_DESCRIPTION("Reports what a sound file holds: its channels, sample size, sample rate, "
                  "length, sample type and name. Reads only the file's header.");
  ADD_CATEGORY(pCategory::SAMPLING);
  INLET_DOC(0, "messages",
            "open <file> names a sound file and reports on it; bang reports on the last file "
            "named.",
            "");
  OUTLET_DOC(0, "channels", "How many channels the file has.", "count");
  OUTLET_DOC(1, "sample size", "The size of one sample in bits, or 0 for a compressed encoding.",
             "bits");
  OUTLET_DOC(2, "sample rate", "The file's sample rate, in Hz.", "Hz");
  OUTLET_DOC(3, "length", "The file's length, in ms.", "ms");
  OUTLET_DOC(4, "sample type",
             "int8, uint8, int16, int24, int32, float32, float64, mulaw, alaw, or other.", "");
  OUTLET_DOC(5, "file name", "The file's name, without its folders.", "");
  PARAM_DOC("file", "", "The sound file a bang reports on.", "file");
}

PARM_CLEAR() {
  fileArgument.clear();
}

// Control thread, on an object no audio thread has seen yet (a change of
// argument rebuilds the object).
PARM_PARSE() {
  if (!SetName(fileArgument.c_str(), fileArgument.size())) name[0] = '\0';
}

void dSfinfo::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler every request goes through, built here so a bang on the
  // audio thread finds it.
  EnableFileIO();
}

bool dSfinfo::SetName(const char* text, std::size_t length) {
  if (length == 0 || length >= sizeof(name)) return false;
  std::memcpy(name, text, length);
  name[length] = '\0';
  return true;
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(Bang) {
  (void)inlet;
  (void)thread;
  Request();
}

// `bang` and `open <file>`, read in place: no copy, no allocation.
MESSAGES() {
  (void)value;
  const char* text = message.c_str();
  const std::size_t length = message.size();
  std::size_t at = 0;
  auto word = [&](std::size_t& begin) {
    while (at < length && IsSelectorSeparator(text[at]))
      at++;
    begin = at;
    while (at < length && !IsSelectorSeparator(text[at]))
      at++;
    return at - begin;
  };
  auto is = [&](std::size_t begin, std::size_t n, const char* expected) {
    return n == std::strlen(expected) && std::memcmp(text + begin, expected, n) == 0;
  };

  std::size_t begin = 0;
  const std::size_t n = word(begin);
  if (is(begin, n, "bang")) {
    Request();
  } else if (is(begin, n, "open")) {
    std::size_t argBegin = 0;
    const std::size_t argLength = word(argBegin);
    // `open` alone opens a file dialog in Max; the engine has none.
    if (SetName(text + argBegin, argLength)) Request();
  }
}

void dSfinfo::Request() {
  if (name[0] == '\0') return;
  fileScheduler* io = FileIO();
  if (io == nullptr) return;
  sfinfoTask task;
  std::memcpy(task.name, name, sizeof(task.name));
  // The name goes through the host's path resolver when one is installed
  // (issue #1255); the task then opens the path it answers.
  io->RequestPathTask(this, 0, PA_READ, task.name, std::strlen(task.name), &dSfinfo::RunTask, &task,
                      sizeof(task));
}

// ─── the background half ──────────────────────────────────────────────────────

bool dSfinfo::RunTask(char* args, std::size_t argBytes, const char* path) {
  static_assert(std::is_trivially_copyable<sfinfoTask>::value,
                "an sfinfoTask is copied into a scheduler slot with memcpy");
  if (argBytes != sizeof(sfinfoTask)) return false;
  sfinfoTask task;
  std::memcpy(&task, args, sizeof(task));

  // The host resolver's answer, or the name as the patch gave it.
  openFile file(path != nullptr ? path : task.name);
  if (!file.handle) return false;
  const int channels = file.handle.channels();
  const int rate = file.handle.samplerate();
  const sf_count_t frames = file.handle.frames();
  if (channels <= 0 || rate <= 0 || frames < 0) return false;

  task.channels = static_cast<std::uint32_t>(channels);
  task.rate = static_cast<float>(rate);
  task.lengthMs =
      static_cast<float>(static_cast<double>(frames) * 1000.0 / static_cast<double>(rate));
  const char* type = SampleType(file.handle.format(), task.bits);
  std::strncpy(task.type, type, sizeof(task.type) - 1);

  std::memcpy(args, &task, sizeof(task));
  return true;
}

// The top of a block, on the audio thread, inside its own dispatch frame.
// Right to left, so the channel count arrives last.
void dSfinfo::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  if (result.op != FILE_OP::TASK || result.bytes == nullptr ||
      result.byteCount != sizeof(sfinfoTask)) {
    return;
  }
  if (!result.ok) {
    failed.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  sfinfoTask task;
  std::memcpy(&task, result.bytes, sizeof(task));

  // The last part of the name, after its final '/' or '\'.
  const char* fileName = task.name;
  for (const char* c = task.name; *c != '\0'; ++c) {
    if (*c == '/' || *c == '\\') fileName = c + 1;
  }

  // Within the capacity reserved at construction: no allocation.
  reply.assign(fileName);
  outputs[5].SendList(reply, thread);
  reply.assign(task.type);
  outputs[4].SendList(reply, thread);
  outputs[3].SendFloat(task.lengthMs, thread);
  outputs[2].SendFloat(task.rate, thread);
  outputs[1].SendInt(static_cast<int>(task.bits), thread);
  outputs[0].SendInt(static_cast<int>(task.channels), thread);
}

#undef className
