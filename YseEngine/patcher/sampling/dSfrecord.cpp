#include "dSfrecord.h"
#include "sfCues.h"
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>

using namespace YSE::PATCHER;
using YSE::DSP::fileRecorder;

namespace {

  // The record requests, as they travel in `request`.
  constexpr int REQUEST_NONE = 0;
  constexpr int REQUEST_START = 1;
  constexpr int REQUEST_FOR = 2;
  constexpr int REQUEST_LOOP = 3;
  constexpr int REQUEST_STOP = 4;

  // What a task does. Its fileResult::tag is TAG_BASE plus this.
  enum class SF_OP : std::uint8_t {
    OPEN,
    WRITE,
  };
  constexpr int TAG_BASE = 1156000;
  constexpr int OP_COUNT = 2;

  // The recorders tasks find by id. Weak: a recorder lives while its object
  // or a task holds it. Control and background threads only (it locks).
  struct recorderTable {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::weak_ptr<fileRecorder>> recorders;
    std::uint64_t next = 1;
  };

  recorderTable& Recorders() {
    // Never destroyed, so an object freed during static destruction still
    // finds it.
    static recorderTable* const instance =
        new recorderTable(); // NOLINT(cppcoreguidelines-owning-memory): leaked on purpose
    return *instance;
  }

  std::shared_ptr<fileRecorder> FindRecorder(std::uint64_t id) {
    recorderTable& t = Recorders();
    const std::lock_guard<std::mutex> lock(t.mutex);
    auto it = t.recorders.find(id);
    return it == t.recorders.end() ? nullptr : it->second.lock();
  }

  // A file type word of `open`; -1 for none.
  int FileTypeOf(const char* text, std::size_t begin, std::size_t end) {
    using sfcues::Is;
    using T = fileRecorder::FILE_TYPE;
    if (Is(text, begin, end, "aiff") || Is(text, begin, end, "aif") || Is(text, begin, end, "AIFF"))
      return static_cast<int>(T::AIFF);
    if (Is(text, begin, end, "wave") || Is(text, begin, end, "wav") || Is(text, begin, end, "WAVE"))
      return static_cast<int>(T::WAV);
    if (Is(text, begin, end, "flac") || Is(text, begin, end, "FLAC"))
      return static_cast<int>(T::FLAC);
    if (Is(text, begin, end, "ogg") || Is(text, begin, end, "OGG")) return static_cast<int>(T::OGG);
    if (Is(text, begin, end, "au") || Is(text, begin, end, "NeXT")) return static_cast<int>(T::AU);
    if (Is(text, begin, end, "caf")) return static_cast<int>(T::CAF);
    if (Is(text, begin, end, "w64")) return static_cast<int>(T::W64);
    if (Is(text, begin, end, "raw") || Is(text, begin, end, "RAW")) return static_cast<int>(T::RAW);
    return -1;
  }

  // A `samptype` word; -1 for none.
  int SampleTypeOf(const char* text, std::size_t begin, std::size_t end) {
    using sfcues::Is;
    using S = fileRecorder::SAMPLE_TYPE;
    if (Is(text, begin, end, "int8")) return static_cast<int>(S::INT8);
    if (Is(text, begin, end, "int16")) return static_cast<int>(S::INT16);
    if (Is(text, begin, end, "int24")) return static_cast<int>(S::INT24);
    if (Is(text, begin, end, "int32")) return static_cast<int>(S::INT32);
    if (Is(text, begin, end, "float32")) return static_cast<int>(S::FLOAT32);
    if (Is(text, begin, end, "float64")) return static_cast<int>(S::FLOAT64);
    if (Is(text, begin, end, "mulaw")) return static_cast<int>(S::MULAW);
    if (Is(text, begin, end, "alaw")) return static_cast<int>(S::ALAW);
    return -1;
  }

  // The integer sample type of a bit depth; -1 for none.
  int BitDepthType(int bits) {
    using S = fileRecorder::SAMPLE_TYPE;
    switch (bits) {
    case 8:
      return static_cast<int>(S::INT8);
    case 16:
      return static_cast<int>(S::INT16);
    case 24:
      return static_cast<int>(S::INT24);
    case 32:
      return static_cast<int>(S::INT32);
    default:
      return -1;
    }
  }

  int Clamp(int value, int lowest, int highest) {
    return value < lowest ? lowest : (value > highest ? highest : value);
  }

} // namespace

// The record a request becomes. Copied whole into a scheduler slot, so it
// stays trivially copyable.
struct dSfrecord::sfTask {
  std::uint64_t id = 0;
  SF_OP op = SF_OP::WRITE;
  std::uint8_t fileType = 0;
  std::uint8_t sampleType = 0;
  std::uint8_t byteSwap = 0;
  std::int32_t channels = 0;
  char name[fileScheduler::PATH_CAPACITY] = {};
};

#define className dSfrecord

CONSTRUCT_DSP() {
  beginRamp = 0.f;
  endRamp = 0.f;
  bitDepth = 16;
  dither = 0;
  quantization = 0;
  nchans = 0;
  sortLoop = 0;
  sampleType = static_cast<int>(fileRecorder::SAMPLE_TYPE::INT16);
  byteSwap = 0;
  requestMs = 0.f;

  recorder = std::make_shared<fileRecorder>(1);
  {
    recorderTable& t = Recorders();
    const std::lock_guard<std::mutex> lock(t.mutex);
    id = t.next++;
    t.recorders[id] = recorder;
  }

  // The inlets follow the channel count: built in ShapePorts, here for one.
  ShapePorts();

  ADD_OUT_BUFFER;
  ADD_OUT_INT;

  REG_PARM_PARSE;
  ADD_PARAM(channels);
  ADD_PARAM(bufferSize);
  ADD_PARAM(beginRamp);
  ADD_PARAM(endRamp);
  ADD_PARAM(bitDepth);
  ADD_PARAM(dither);
  ADD_PARAM(quantization);
  ADD_PARAM(nchans);
  ADD_PARAM(sortLoop);

  ADD_DESCRIPTION("Records signals to a sound file on disk as they play: until stopped, for a "
                  "time, or the last stretch in a loop, in AIFF, WAV, FLAC and other types, with "
                  "fades, dither and the time recorded.");
  ADD_CATEGORY(pCategory::SAMPLING);
  OUTLET_DOC(0, "time",
             "How long the recording has run, in milliseconds: 0 at its first sample. After a "
             "stop it holds the recording's length.",
             "0 or higher");
  OUTLET_DOC(1, "dropped",
             "The samples per channel of the recording that were lost because the disk fell "
             "behind, sent when the count changes. 0 when nothing was lost.",
             "0 or higher");
  PARAM_DOC("channels", "1",
            "The number of signal inlets, each recorded into the file channel of its number.",
            "1 to 64");
  PARAM_DOC("buffersize", "0",
            "How many samples per channel wait for the disk before any are lost; 0 for 65536. "
            "Below 4096 is 4096.",
            "0 or higher");
  PARAM_DOC("beginramp", "0", "A fade-in over the start of each recording, in milliseconds.",
            "0 or higher");
  PARAM_DOC("endramp", "0",
            "A fade-out over the end of each recording, in milliseconds. A stop records on for "
            "that long while it fades.",
            "0 or higher");
  PARAM_DOC("bitdepth", "16",
            "The bits of each integer sample of the next file opened. samptype can choose "
            "floats and the 8-bit codes instead.",
            "8, 16, 24 or 32");
  PARAM_DOC("dither", "0",
            "What is added before integer samples are rounded: 0 nothing, 1 triangular dither, "
            "2 triangular dither with noise shaping.",
            "0 to 2");
  PARAM_DOC("quantization", "0", "How integer samples are rounded: 0 to the nearest step, 1 down.",
            "0 or 1");
  PARAM_DOC("nchans", "0",
            "The channels of the next file opened; 0 for one per inlet. Inlets past them are "
            "not recorded; channels past the inlets are silent.",
            "0 to 64");
  PARAM_DOC("sortloop", "0",
            "1 puts a loop recording in order when it stops, so its file starts with the oldest "
            "sample instead of where the circle was.",
            "0 or 1");
}

dSfrecord::~dSfrecord() {
  recorderTable& t = Recorders();
  const std::lock_guard<std::mutex> lock(t.mutex);
  t.recorders.erase(id);
}

void dSfrecord::ShapePorts() {
  // Rebuilt rather than resized, as ~record: the inlets follow the
  // arguments, and the object is new, so no cord ends on them yet.
  inputs.clear();
  for (int i = 0; i < channels; i++) {
    if (i == 0) {
      ADD_IN_0;
      REG_SIGNAL_IN;
      REG_FLOAT_IN(SetRecord);
      REG_INT_IN(SetRecordInt);
      INLET_DOC(
          0, channels == 1 ? "signal" : "signal 1",
          "The signal to record into channel 1; unconnected it records silence. A number "
          "other than 0 starts recording into the open file, 0 stops it and closes the file. "
          "Also takes 'open <file> [type]', 'start [file] [channels]', 'stop', 'record <ms>', "
          "'loop <ms>', 'samptype <int8|int16|int24|int32|float32|float64|mulaw|alaw> [swap]', "
          "'bitdepth <bits>', 'beginramp <ms>', 'endramp <ms>', 'dither <0-2>', "
          "'quantization <0/1>', 'nchans <n>' and 'sortloop <0/1>'.",
          "any float");
    } else {
      inputs.emplace_back(this, false, i);
      REG_SIGNAL_IN;
      const std::string n = std::to_string(i + 1);
      inputs.back().SetDoc("signal " + n,
                           "The signal to record into channel " + n +
                               "; unconnected it records silence.",
                           "any float");
    }
  }
}

void dSfrecord::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler every file message goes through, built here so a message
  // on the audio thread finds it.
  EnableFileIO();
}

// Control thread, on an object no audio thread has seen yet (a change of
// argument rebuilds it).
PARM_PARSE() {
  channels = Clamp(channels, 1, static_cast<int>(fileRecorder::MAX_CHANNELS));
  bufferSize = bufferSize < 0 ? 0 : bufferSize;
  const int type = BitDepthType(bitDepth.load());
  if (type >= 0) sampleType = type;
  dither = Clamp(dither.load(), 0, 2);
  quantization = Clamp(quantization.load(), 0, 1);
  nchans = Clamp(nchans.load(), 0, static_cast<int>(fileRecorder::MAX_CHANNELS));
  sortLoop = sortLoop.load() != 0 ? 1 : 0;
  // Allocates: control thread.
  recorder->resize(static_cast<UInt>(channels), bufferSize > 0 ? static_cast<UInt>(bufferSize)
                                                               : fileRecorder::DEFAULT_RING_FRAMES);
  Apply();
  ShapePorts();
}

// The settings the recorder takes while it writes. Atomics: any thread.
void dSfrecord::Apply() {
  recorder->dither(static_cast<fileRecorder::DITHER>(dither.load()))
      .quantization(static_cast<fileRecorder::QUANTIZE>(quantization.load()))
      .sortLoop(sortLoop.load() != 0);
}

// ─── requests ─────────────────────────────────────────────────────────────────

bool dSfrecord::Submit(sfTask& task, bool resolve) {
  fileScheduler* io = FileIO();
  if (io == nullptr) {
    failed.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  task.id = id;
  const int tag = TAG_BASE + static_cast<int>(task.op);
  // A start waits for an open: counted before it can complete.
  const bool counted = task.op == SF_OP::OPEN;
  if (counted) inFlight.fetch_add(1, std::memory_order_acq_rel);
  const std::size_t length = std::strlen(task.name);
  const bool ok = resolve && length > 0
                      ? io->RequestPathTask(this, tag, PA_WRITE, task.name, length,
                                            &dSfrecord::RunPathTask, &task, sizeof(task))
                      : io->RequestTask(this, tag, &dSfrecord::RunTask, &task, sizeof(task));
  if (!ok) {
    if (counted) inFlight.fetch_sub(1, std::memory_order_acq_rel);
    if (counted) failed.fetch_add(1, std::memory_order_relaxed);
  }
  return ok;
}

void dSfrecord::Post(int what, float ms) {
  requestMs.store(ms, std::memory_order_relaxed);
  request.store(what, std::memory_order_release);
}

FLOAT_IN(SetRecord) {
  (void)inlet;
  (void)thread;
  if (std::isnan(value)) return;
  Post(value != 0.f ? REQUEST_START : REQUEST_STOP);
}

INT_IN(SetRecordInt) {
  (void)inlet;
  (void)thread;
  Post(value != 0 ? REQUEST_START : REQUEST_STOP);
}

bool dSfrecord::OpenFrom(const char* text, std::size_t length, std::size_t pos, bool withChannels) {
  using sfcues::NextToken;
  std::size_t b1 = 0;
  std::size_t e1 = 0;
  NextToken(text, length, pos, b1, e1);
  if (b1 == e1) return false;
  std::size_t b2 = 0;
  std::size_t e2 = 0;
  NextToken(text, length, e1, b2, e2);

  sfTask task;
  task.op = SF_OP::OPEN;
  task.fileType = static_cast<std::uint8_t>(fileRecorder::FILE_TYPE::AUTO);
  task.channels = nchans.load();
  std::size_t nameBegin = b1;
  std::size_t nameEnd = e1;
  if (withChannels) {
    // start <file> [channels]
    float count = 0.f;
    if (sfcues::Number(text, b2, e2, count) && count >= 1.f)
      task.channels =
          Clamp(static_cast<int>(count), 1, static_cast<int>(fileRecorder::MAX_CHANNELS));
  } else if (b2 < e2) {
    // open <type> <file>, or open <file> <type>
    const int first = FileTypeOf(text, b1, e1);
    const int second = FileTypeOf(text, b2, e2);
    if (first >= 0) {
      task.fileType = static_cast<std::uint8_t>(first);
      nameBegin = b2;
      nameEnd = e2;
    } else if (second >= 0) {
      task.fileType = static_cast<std::uint8_t>(second);
    }
  }
  const std::size_t n = nameEnd - nameBegin;
  if (n >= sizeof(task.name)) return false;
  std::memcpy(task.name, text + nameBegin, n);
  task.name[n] = '\0';
  task.sampleType = static_cast<std::uint8_t>(sampleType.load());
  task.byteSwap = byteSwap.load() != 0 ? 1 : 0;
  return Submit(task, true);
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

  if (Is(text, begin, end, "open")) {
    // `open` alone opens a save dialog in Max; the engine has none.
    OpenFrom(text, length, end, false);
    return;
  }
  if (Is(text, begin, end, "start")) {
    OpenFrom(text, length, end, true);
    Post(REQUEST_START);
    return;
  }
  if (Is(text, begin, end, "stop")) {
    Post(REQUEST_STOP);
    return;
  }

  std::size_t argBegin = 0;
  std::size_t argEnd = 0;
  NextToken(text, length, end, argBegin, argEnd);
  if (Is(text, begin, end, "samptype")) {
    const int type = SampleTypeOf(text, argBegin, argEnd);
    if (type < 0) return;
    sampleType = type;
    std::size_t b = 0;
    std::size_t e = 0;
    NextToken(text, length, argEnd, b, e);
    float swap = 0.f;
    byteSwap = sfcues::Number(text, b, e, swap) && swap != 0.f ? 1 : 0;
    return;
  }
  float number = 0.f;
  if (!sfcues::Number(text, argBegin, argEnd, number)) return;
  if (Is(text, begin, end, "record")) {
    if (number > 0.f) Post(REQUEST_FOR, number);
  } else if (Is(text, begin, end, "loop")) {
    if (number > 0.f) Post(REQUEST_LOOP, number);
  } else if (Is(text, begin, end, "bitdepth") || Is(text, begin, end, "format")) {
    const int type = BitDepthType(static_cast<int>(number));
    if (type < 0) return;
    bitDepth = static_cast<int>(number);
    sampleType = type;
  } else if (Is(text, begin, end, "beginramp")) {
    beginRamp = number > 0.f ? number : 0.f;
  } else if (Is(text, begin, end, "endramp")) {
    endRamp = number > 0.f ? number : 0.f;
  } else if (Is(text, begin, end, "dither")) {
    dither = Clamp(static_cast<int>(number), 0, 2);
    Apply();
  } else if (Is(text, begin, end, "quantization")) {
    quantization = Clamp(static_cast<int>(number), 0, 1);
    Apply();
  } else if (Is(text, begin, end, "nchans")) {
    nchans = Clamp(static_cast<int>(number), 0, static_cast<int>(fileRecorder::MAX_CHANNELS));
  } else if (Is(text, begin, end, "sortloop")) {
    sortLoop = number != 0.f ? 1 : 0;
    Apply();
  }
}

// ─── the block ────────────────────────────────────────────────────────────────

CALC() {
  fileRecorder& r = *recorder;
  r.beginRamp(beginRamp.load()).endRamp(endRamp.load());

  // A start waits while an open is in flight, so it records into the file
  // the open makes.
  if (inFlight.load(std::memory_order_acquire) <= 0) {
    switch (request.exchange(REQUEST_NONE, std::memory_order_acquire)) {
    case REQUEST_START:
      r.record(true);
      break;
    case REQUEST_FOR:
      r.recordFor(requestMs.load(std::memory_order_relaxed));
      break;
    case REQUEST_LOOP:
      r.loop(requestMs.load(std::memory_order_relaxed));
      break;
    case REQUEST_STOP:
      r.record(false);
      break;
    default:
      break;
    }
  }

  // Every signal inlet, connected or not: an unconnected one records 0.
  const auto count = static_cast<UInt>(channels);
  for (UInt i = 0; i < count; ++i)
    blocks[i] = &inputs[i].Signal(0.f);
  DSP::buffer& time = r(blocks.data(), count);
  outputs[0].SendBuffer(&time, thread);

  const std::uint64_t dropped = r.dropped();
  if (dropped != lastDropped) {
    lastDropped = dropped;
    outputs[1].ArmInt(dropped > 0x7FFFFFFFu ? 0x7FFFFFFF : static_cast<int>(dropped));
  }

  // What the block recorded goes to the disk in the background, one task
  // at a time.
  if (r.pending() && !drainQueued.exchange(true, std::memory_order_acq_rel)) {
    sfTask task;
    task.op = SF_OP::WRITE;
    if (!Submit(task, false)) drainQueued.store(false, std::memory_order_release);
  }
}

// The top of a block, on the audio thread.
void dSfrecord::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  (void)thread;
  const int op = result.tag - TAG_BASE;
  if (op < 0 || op >= OP_COUNT) return;
  if (static_cast<SF_OP>(op) == SF_OP::WRITE) {
    drainQueued.store(false, std::memory_order_release);
    return;
  }
  inFlight.fetch_sub(1, std::memory_order_acq_rel);
  if (!result.ok) failed.fetch_add(1, std::memory_order_relaxed);
}

// ─── the background half ──────────────────────────────────────────────────────

bool dSfrecord::RunTask(char* args, std::size_t argBytes) {
  return RunPathTask(args, argBytes, nullptr);
}

bool dSfrecord::RunPathTask(char* args, std::size_t argBytes, const char* path) {
  static_assert(std::is_trivially_copyable<sfTask>::value,
                "an sfTask is copied into a scheduler slot with memcpy");
  if (argBytes != sizeof(sfTask)) return false;
  sfTask task;
  std::memcpy(&task, args, sizeof(task));
  task.name[sizeof(task.name) - 1] = '\0';

  // By id, not through the object: it may have been deleted since.
  std::shared_ptr<fileRecorder> found = FindRecorder(task.id);
  if (found == nullptr) return false;

  if (task.op == SF_OP::WRITE) {
    found->write();
    return true;
  }
  // The host resolver's answer, or the name as the patch gave it.
  const std::string file = path != nullptr ? std::string(path) : std::string(task.name);
  found->sampleType(static_cast<fileRecorder::SAMPLE_TYPE>(task.sampleType))
      .byteSwap(task.byteSwap != 0)
      .fileChannels(static_cast<UInt>(task.channels));
  return found->open(file, static_cast<fileRecorder::FILE_TYPE>(task.fileType));
}

#undef className
