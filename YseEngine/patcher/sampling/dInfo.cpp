#include "dInfo.h"
#include "../pSelector.h"
#include "../../dsp/sampling/sampleBuffer.hpp"
#include <cstdio>
#include <cstring>
#include <type_traits>

using namespace YSE::PATCHER;

#define className dInfo

namespace {

  constexpr std::size_t TEXT_CAPACITY = fileScheduler::PATH_CAPACITY;

  // `source` into `out` with its terminator; false, and `out` empty, when it
  // does not fit.
  bool CopyText(char (&out)[TEXT_CAPACITY], const std::string& source) {
    if (source.size() >= TEXT_CAPACITY) {
      out[0] = '\0';
      return false;
    }
    std::memcpy(out, source.c_str(), source.size() + 1);
    return true;
  }

} // namespace

// The record a bang becomes. Copied whole into a scheduler slot, so it must
// stay trivially copyable; the task writes the answer into the same record,
// and the completion reads it back.
struct dInfo::infoTask {
  char name[TEXT_CAPACITY] = {}; // the buffer to report on

  // The answer.
  float rate = 0.f;
  float lengthMs = 0.f;
  std::uint32_t channels = 0;
  std::uint8_t hasInstrument = 0;
  std::uint8_t hasSustain = 0;
  std::uint8_t hasRelease = 0;
  float sustainStart = 0.f;
  float sustainEnd = 0.f;
  float releaseStart = 0.f;
  float releaseEnd = 0.f;
  std::int32_t instrument[7] = {}; // note, detune, keys low/high, velocities low/high, gain
  char fileName[TEXT_CAPACITY] = {}; // empty: not sent
  char filePath[TEXT_CAPACITY] = {}; // empty: not sent
};

CONSTRUCT_DSP() {
  reply.reserve(TEXT_CAPACITY + 16);

  ADD_IN_0;
  REG_BANG_IN(Bang);

  ADD_OUT_FLOAT; // 0 sample rate
  ADD_OUT_LIST; // 1 instrument
  ADD_OUT_FLOAT; // 2 sustain loop start
  ADD_OUT_FLOAT; // 3 sustain loop end
  ADD_OUT_FLOAT; // 4 release loop start
  ADD_OUT_FLOAT; // 5 release loop end
  ADD_OUT_FLOAT; // 6 length
  ADD_OUT_LIST; // 7 file name
  ADD_OUT_INT; // 8 channels
  ADD_OUT_LIST; // 9 file path

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(bufferName);

  ADD_DESCRIPTION("Reports what a named sample buffer holds: its sample rate, length and "
                  "channels, and the name, path, loop points and instrument data of the file "
                  "last loaded into it.");
  ADD_CATEGORY(pCategory::SAMPLING);
  INLET_DOC(0, "messages",
            "bang reports on the buffer; set <name> picks another buffer (set alone, none).", "");
  OUTLET_DOC(0, "sample rate", "The rate the buffer's samples are at, in Hz.", "Hz");
  OUTLET_DOC(1, "instrument",
             "The file's instrument data: base note, detune in cents, lowest and highest "
             "note, lowest and highest velocity, gain. Not sent when the file has none.",
             "list");
  OUTLET_DOC(2, "sustain start",
             "Where the file's sustain loop starts, in ms. Not sent without the loop.", "ms");
  OUTLET_DOC(3, "sustain end",
             "Where the file's sustain loop ends, in ms. Not sent without the loop.", "ms");
  OUTLET_DOC(4, "release start",
             "Where the file's release loop starts, in ms. Not sent without the loop.", "ms");
  OUTLET_DOC(5, "release end",
             "Where the file's release loop ends, in ms. Not sent without the loop.", "ms");
  OUTLET_DOC(6, "length", "The buffer's length, in ms.", "ms");
  OUTLET_DOC(7, "file name",
             "The name of the file last loaded into the buffer. Not sent when none was.", "");
  OUTLET_DOC(8, "channels",
             "How many channels that file has, or the buffer has when no file was loaded.",
             "count");
  OUTLET_DOC(9, "file path",
             "The path of the file last loaded into the buffer, as it was given to read. Not "
             "sent when none was.",
             "");
  PARAM_DOC("name", "", "The buffer to report on.", "any symbol");
}

PARM_CLEAR() {
  bufferName.clear();
}

// Control thread, on an object no audio thread has seen yet (a change of
// argument rebuilds the object).
PARM_PARSE() {
  if (!SetName(bufferName.c_str(), bufferName.size())) name[0] = '\0';
}

void dInfo::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler every bang goes through, built here so a bang on the audio
  // thread finds it.
  EnableFileIO();
}

bool dInfo::SetName(const char* text, std::size_t length) {
  if (length >= sizeof(name)) return false;
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

// `bang` and `set [name]`, read in place: no copy, no allocation.
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
  } else if (is(begin, n, "set")) {
    std::size_t argBegin = 0;
    const std::size_t argLength = word(argBegin);
    SetName(text + argBegin, argLength);
  }
}

void dInfo::Request() {
  if (name[0] == '\0') return;
  fileScheduler* io = FileIO();
  if (io == nullptr) return;
  infoTask task;
  std::memcpy(task.name, name, sizeof(task.name));
  io->RequestTask(this, 0, &dInfo::RunTask, &task, sizeof(task));
}

// ─── the background half ──────────────────────────────────────────────────────

bool dInfo::RunTask(char* args, std::size_t argBytes) {
  static_assert(std::is_trivially_copyable<infoTask>::value,
                "an infoTask is copied into a scheduler slot with memcpy");
  if (argBytes != sizeof(infoTask)) return false;
  infoTask task;
  std::memcpy(&task, args, sizeof(task));

  std::shared_ptr<DSP::sampleBuffer> buffer = DSP::sampleBufferRegistry::find(task.name);
  if (buffer == nullptr) return false;
  const DSP::sampleBufferInfo info = buffer->info();

  task.rate = info.sampleRate;
  task.lengthMs = info.lengthMs;
  task.channels = info.hasFile() ? info.fileChannels : info.channels;

  const DSP::sampleInstrument& in = info.instrument;
  task.hasInstrument = in.defined ? 1 : 0;
  task.instrument[0] = in.baseNote;
  task.instrument[1] = in.detune;
  task.instrument[2] = in.keyLow;
  task.instrument[3] = in.keyHigh;
  task.instrument[4] = in.velocityLow;
  task.instrument[5] = in.velocityHigh;
  task.instrument[6] = in.gain;
  task.hasSustain = in.sustainLoop.defined ? 1 : 0;
  task.sustainStart = info.framesToMs(in.sustainLoop.start);
  task.sustainEnd = info.framesToMs(in.sustainLoop.end);
  task.hasRelease = in.releaseLoop.defined ? 1 : 0;
  task.releaseStart = info.framesToMs(in.releaseLoop.start);
  task.releaseEnd = info.framesToMs(in.releaseLoop.end);

  if (info.hasFile()) {
    CopyText(task.fileName, info.fileName());
    CopyText(task.filePath, info.filePath);
  }

  std::memcpy(args, &task, sizeof(task));
  return true;
}

// The top of a block, on the audio thread, inside its own dispatch frame.
// Right to left, so the sample rate arrives last.
void dInfo::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  if (result.op != FILE_OP::TASK || result.bytes == nullptr ||
      result.byteCount != sizeof(infoTask)) {
    return;
  }
  if (!result.ok) {
    failed.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  infoTask task;
  std::memcpy(&task, result.bytes, sizeof(task));

  // Within the capacity reserved at construction: no allocation.
  if (task.filePath[0] != '\0') {
    reply.assign(task.filePath);
    outputs[9].SendList(reply, thread);
  }
  outputs[8].SendInt(static_cast<int>(task.channels), thread);
  if (task.fileName[0] != '\0') {
    reply.assign(task.fileName);
    outputs[7].SendList(reply, thread);
  }
  outputs[6].SendFloat(task.lengthMs, thread);
  if (task.hasRelease != 0) {
    outputs[5].SendFloat(task.releaseEnd, thread);
    outputs[4].SendFloat(task.releaseStart, thread);
  }
  if (task.hasSustain != 0) {
    outputs[3].SendFloat(task.sustainEnd, thread);
    outputs[2].SendFloat(task.sustainStart, thread);
  }
  if (task.hasInstrument != 0) {
    char text[96];
    const int length = std::snprintf(text, sizeof(text), "%d %d %d %d %d %d %d", task.instrument[0],
                                     task.instrument[1], task.instrument[2], task.instrument[3],
                                     task.instrument[4], task.instrument[5], task.instrument[6]);
    if (length > 0) {
      reply.assign(text, static_cast<std::size_t>(length));
      outputs[1].SendList(reply, thread);
    }
  }
  outputs[0].SendFloat(task.rate, thread);
}

#undef className
