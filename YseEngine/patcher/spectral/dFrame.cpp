#include "dFrame.h"

#include <cmath>
#include <string_view>
#include "../math/gExprEval.h"
#include "../pAtomList.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include "../../headers/constants.hpp"
#include "spectralPass.h"

using namespace YSE::PATCHER;
using YSE::DSP::frameSnapshot;
using YSE::DSP::frameSource;

namespace {
  // The token starting at or after `from` in `text`, walked in place so
  // nothing allocates on whichever thread delivers the message. `next` is
  // set to where the token ends. Empty when there is none.
  std::string_view Token(const char* text, std::size_t size, std::size_t from, std::size_t& next) {
    std::size_t begin = from;
    while (begin < size && IsSelectorSeparator(text[begin]))
      begin++;
    std::size_t end = begin;
    while (end < size && !IsSelectorSeparator(text[end]))
      end++;
    next = end;
    return std::string_view(text + begin, end - begin);
  }

  // The number a token reads as; false when it is none.
  bool Number(std::string_view token, float& number) {
    return !token.empty() && ReadNumericToken(token.data(), token.size(), number);
  }

  // A parameter held between `lowest` and `highest`.
  UInt Between(int value, UInt lowest, UInt highest) {
    if (value < static_cast<int>(lowest)) return lowest;
    const auto v = static_cast<UInt>(value);
    return v > highest ? highest : v;
  }

  // A bin number from a message: rounded, at least 0, at most the largest
  // capacity.
  UInt Bin(float value) {
    if (!(value > 0.f)) return 0;
    if (value >= static_cast<float>(frameSource::MAXIMUM_CAPACITY))
      return frameSource::MAXIMUM_CAPACITY;
    return static_cast<UInt>(std::lround(value));
  }

  // A value as list text, into `number` (kExprValueTextMax bytes). The
  // length.
  std::size_t Format(float value, char* number) {
    const int written = ExprFormatValue(ExprValue::Float(value), number, kExprValueTextMax);
    return written > 0 ? static_cast<std::size_t>(written) : 0;
  }

  constexpr const char* BINS_DOC_TAIL =
      " Half the FFT size of the ~pfft it runs in, or the FFT size with the full spectrum. "
      "Changing it rebuilds the object.";
} // namespace

// ─── ~frame ───────────────────────────────────────────────────────────────────

#define className dFrame

CONSTRUCT_DSP() {
  scratch.assign(staged.capacity(), 0.f);

  // in 0: lists, numbers, fill, mirror
  ADD_IN_0;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  REG_LIST_IN(SetList);

  // in 1: the bin the next lists start at
  ADD_IN_1;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0: the frame
  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(bins);

  ADD_DESCRIPTION("Puts out a list as a frame: value k as bin k of every spectral frame inside "
                  "a ~pfft, or as sample k of every block elsewhere. A spectrum drawn from data, "
                  "or frozen from a ~framesnap.");
  ADD_CATEGORY(pCategory::SPECTRAL);
  INLET_DOC(0, "values",
            "A list sets the values from the offset on; the bins after the list put out the "
            "fill value. A number is a list of one. 'fill <value>' sets the fill value (0); "
            "'mirror 1' makes the bins above Nyquist of a full-spectrum ~pfft mirror the ones "
            "below instead of putting out 0. Not saved.",
            "list, number, fill, mirror");
  INLET_DOC(1, "offset",
            "The bin the next lists start at; the bins before keep their values. ~framesnap's "
            "right outlet sends it before each list.",
            "0 or more");
  OUTLET_DOC(0, "out",
             "The frame: bin k (inside a ~pfft) or sample k (elsewhere) is value k, or the "
             "fill value.",
             "");
  PARAM_DOC("bins", "4096",
            std::string("The most values kept; bins past it put out the fill value.") +
                BINS_DOC_TAIL,
            "1-65536");
}

void dFrame::Allocate(UInt capacity) {
  staged = frameSource(capacity);
  for (frameSource& slot : slots)
    slot = frameSource(capacity);
  scratch.assign(capacity, 0.f);
}

// Control thread: the clear and parse callbacks make a live change rebuild
// the object, so the storage is allocated on one no audio thread has seen.
PARM_CLEAR() {
  bins = static_cast<int>(frameSource::DEFAULT_CAPACITY);
  if (staged.capacity() != frameSource::DEFAULT_CAPACITY) Allocate(frameSource::DEFAULT_CAPACITY);
}

PARM_PARSE() {
  const UInt capacity = Between(bins, 1, frameSource::MAXIMUM_CAPACITY);
  if (staged.capacity() != capacity) Allocate(capacity);
  bins = static_cast<int>(capacity);
}

// The message thread's copy into the back slot, which then swaps with the
// middle one, marked newer. The audio thread never touches the back slot.
void dFrame::Publish() {
  slots[back].copy(staged);
  back = middle.exchange(back | DIRTY, std::memory_order_acq_rel) & ~DIRTY;
}

FLOAT_IN(SetFloat) {
  (void)thread;
  if (inlet == 1) {
    offset = Bin(value);
    return;
  }
  staged.set(&value, 1, offset);
  Publish();
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

LIST_IN(SetList) {
  (void)thread;
  const char* text = value.c_str();
  const std::size_t size = value.size();
  std::size_t next = 0;
  if (inlet == 1) {
    // The first number.
    float number = 0.f;
    if (Number(Token(text, size, 0, next), number)) offset = Bin(number);
    return;
  }
  // Every token a value, one that is not a number 0, as many as are kept.
  UInt count = 0;
  while (count < scratch.size()) {
    const std::string_view token = Token(text, size, next, next);
    if (token.empty()) break;
    float number = 0.f;
    scratch[count++] = Number(token, number) ? number : 0.f;
  }
  staged.set(scratch.data(), count, offset);
  Publish();
}

MESSAGES() {
  (void)value;
  // 'fill <value>' or 'mirror <0|1>'; anything else is ignored.
  const char* text = message.c_str();
  const std::size_t size = message.size();
  std::size_t next = 0;
  const std::string_view word = Token(text, size, 0, next);
  float number = 0.f;
  if (!Number(Token(text, size, next, next), number)) return;
  if (word == "fill") {
    staged.fill(number);
  } else if (word == "mirror") {
    staged.mirror(number != 0.f);
  } else {
    return;
  }
  Publish();
}

CALC() {
  // The newest values, taken at the start of a frame only: the first pass
  // inside a ~pfft, any block outside one.
  const spectralPass* pass = CurrentSpectralPass();
  if ((pass == nullptr || pass->pass == 0) &&
      (middle.load(std::memory_order_acquire) & DIRTY) != 0) {
    front = middle.exchange(front, std::memory_order_acq_rel) & ~DIRTY;
  }
  frameSource& fn = slots[front];
  YSE::DSP::buffer& out = pass != nullptr ? fn(pass->Frame(nullptr, nullptr)) : fn();
  outputs[0].SendBuffer(&out, thread);
}

#undef className

// ─── ~framesnap ───────────────────────────────────────────────────────────────

#define className dFramesnap

CONSTRUCT_DSP() {
  report.assign(fn.capacity(), 0.f);
  AtomList::ReserveRender(scratch);

  // in 0: the frames; bang, on/off and the word messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Report);
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // in 1: the interval in ms
  ADD_IN_1;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0: the frame as lists; out 1: the bin each list starts at, first.
  // Sent after the block (issue #1037).
  ADD_OUT_LIST;
  ADD_OUT_INT;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(interval);
  ADD_PARAM(bins);

  ADD_DESCRIPTION("Reports a frame as lists of numbers: a ~pfft's spectral frame bin by bin, or "
                  "a block elsewhere, on a bang or every interval while reporting is on.");
  ADD_CATEGORY(pCategory::SPECTRAL);
  INLET_DOC(0, "in",
            "The frames, a signal: inside a ~pfft its spectral frames, elsewhere every block. "
            "A bang reports the last whole frame; a non-zero number starts interval reporting "
            "and 0 stops it. Also takes 'start' (a bang when there is no interval), 'stop', "
            "'active <0|1>', 'interval <ms>' and 'sampleinterval <samples>'.",
            "signal, bang, number, messages");
  INLET_DOC(1, "interval", "Reporting interval in milliseconds. 0 stops interval reporting.",
            "0 or more ms");
  OUTLET_DOC(0, "values",
             "A report: the frame's values from bin 0 on, as lists of at most 256 values. A "
             "frame of more than 4096 bins goes out over several blocks.",
             "list");
  OUTLET_DOC(1, "offset",
             "Before each list, the bin it starts at: 0 for the first. Into the right inlet of "
             "a ~frame, it puts a large frame back together.",
             "0 or more");
  PARAM_DOC("interval", "0",
            "Reporting interval in milliseconds, counted in the time the frames stand for (the "
            "hop inside a ~pfft). 0 reports only on a bang. Reports due within one block are "
            "one.",
            "0 or more ms");
  PARAM_DOC("bins", "4096",
            std::string("The largest frame kept, in bins; a report holds no more.") + BINS_DOC_TAIL,
            "1-65536");
}

PARM_CLEAR() {
  interval = 0.f;
  bins = static_cast<int>(frameSnapshot::DEFAULT_CAPACITY);
  if (fn.capacity() != frameSnapshot::DEFAULT_CAPACITY) {
    fn = frameSnapshot();
    report.assign(fn.capacity(), 0.f);
  }
}

PARM_PARSE() {
  if (!(interval.load() > 0.f)) interval = 0.f;
  const UInt capacity = Between(bins, 1, frameSnapshot::MAXIMUM_CAPACITY);
  if (fn.capacity() != capacity) {
    fn = frameSnapshot(capacity);
    report.assign(fn.capacity(), 0.f);
  }
  bins = static_cast<int>(capacity);
}

// A bang is taken at the next frame, on the audio thread: the frame is the
// audio thread's.
BANG_IN(Report) {
  (void)inlet;
  (void)thread;
  reportRequested = true;
}

FLOAT_IN(SetFloat) {
  (void)thread;
  if (inlet == 1) {
    interval = value > 0.f ? value : 0.f;
    sampleInterval = 0;
  } else {
    active = value != 0.f;
  }
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

// `start`, `stop`, `active <n>`, `interval <ms>` and `sampleinterval <n>`.
MESSAGES() {
  (void)value;
  const char* text = message.c_str();
  const std::size_t size = message.size();
  std::size_t next = 0;
  const std::string_view word = Token(text, size, 0, next);
  if (word == "start") {
    active = true;
    // With no interval, a start is a bang.
    if (sampleInterval.load() <= 0 && !(interval.load() > 0.f)) reportRequested = true;
    return;
  }
  if (word == "stop") {
    active = false;
    return;
  }
  float number = 0.f;
  if (!Number(Token(text, size, next, next), number)) return;
  if (word == "active") {
    active = number != 0.f;
  } else if (word == "interval") {
    interval = number > 0.f ? number : 0.f;
    sampleInterval = 0;
  } else if (word == "sampleinterval") {
    sampleInterval = number < 1.f ? 0 : static_cast<int>(number);
  }
}

void dFramesnap::Tick(double samples) {
  // The period follows the live sample rate, so it is worked out each time.
  bool due = false;
  const int count = sampleInterval.load();
  const double period = count > 0 ? static_cast<double>(count)
                                  : static_cast<double>(interval.load()) * YSE::SAMPLERATE / 1000.0;
  if (!active.load() || period <= 0.0) {
    // Report on the first frame after reporting (re)starts.
    samplesLeft = 0.0;
  } else {
    // samplesLeft counts from the start of this frame to the next report;
    // several due within it are one.
    if (samplesLeft < samples) {
      due = true;
      samplesLeft += std::ceil((samples - samplesLeft) / period) * period;
    }
    samplesLeft -= samples;
  }

  // One report at a time: an interval report due meanwhile is skipped, a
  // bang waits.
  if (reporting) return;
  if (reportRequested.exchange(false)) due = true;
  if (!due) return;
  reportSize = fn.copy(report.data(), static_cast<UInt>(report.size()));
  reportAt = 0;
  reporting = reportSize > 0;
  // Never Send* a control message from CALC(): the patcher sends this after
  // the traversal, through FlushArmedMessages.
  if (reporting) MarkOutletArmed();
}

CALC() {
  (void)thread;
  YSE::DSP::buffer& in = inputs[0].Signal(0.f);
  const spectralPass* pass = CurrentSpectralPass();
  if (pass != nullptr) {
    // A pass: a run of the spectral frame. The clock moves a hop per frame.
    YSE::DSP::stftFrame run = pass->Frame(in.getPtr(), nullptr);
    if (run.count > in.getLength()) run.count = in.getLength();
    const U64 before = fn.frames();
    fn(run, in.getPtr());
    if (fn.frames() != before) Tick(static_cast<double>(pass->hop));
  } else {
    fn(in);
    Tick(static_cast<double>(in.getLength()));
  }
}

// After the block's traversal, on the audio thread: this block's share of
// the report, each list after the bin it starts at.
void dFramesnap::FlushArmedMessages(YSE::THREAD thread) {
  if (!reporting) return;
  const UInt stop =
      reportSize - reportAt > VALUES_PER_BLOCK ? reportAt + VALUES_PER_BLOCK : reportSize;
  char number[kExprValueTextMax];
  std::size_t atoms = 0;
  UInt first = reportAt;
  scratch.clear();
  for (; reportAt < stop; ++reportAt) {
    const std::size_t length = Format(report[reportAt], number);
    // A list stays within what an AtomList holds.
    if (atoms > 0 &&
        (atoms == AtomList::MAX_ATOMS || scratch.size() + 1 + length > AtomList::TEXT_CAPACITY)) {
      outputs[1].SendInt(static_cast<int>(first), thread);
      outputs[0].SendList(scratch, thread);
      scratch.clear();
      atoms = 0;
      first = reportAt;
    }
    if (atoms > 0) scratch.push_back(' ');
    scratch.append(number, length);
    atoms++;
  }
  if (atoms > 0) {
    outputs[1].SendInt(static_cast<int>(first), thread);
    outputs[0].SendList(scratch, thread);
  }
  if (reportAt == reportSize) {
    reporting = false;
  } else {
    // The rest next block.
    MarkOutletArmed();
  }
}

#undef className
