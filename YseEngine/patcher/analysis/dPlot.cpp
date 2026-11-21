#include "dPlot.h"
#include "meterCommon.h"

using namespace YSE::PATCHER;
using namespace YSE::PATCHER::meterCommon;
using YSE::DSP::plotData;

namespace {
  constexpr unsigned int HEADER = 3; // plots, points, serial
  constexpr unsigned int AXES = 9; // the cells before a subplot's values

  // Appends one cell. Host thread.
  void Append(std::string& out, float value) {
    char text[YSE::PATCHER::kExprValueTextMax];
    const int written = YSE::PATCHER::ExprFormatValue(YSE::PATCHER::ExprValue::Float(value), text,
                                                      YSE::PATCHER::kExprValueTextMax);
    if (written > 0)
      out.append(text, static_cast<std::size_t>(written));
    else
      out.append("0.");
  }

  // Cell `index` of a subplot: its axes, then its values. Host thread.
  float SubplotCell(const plotData::subplot& s, unsigned int index) {
    switch (index) {
    case 0:
      return s.domain.start;
    case 1:
      return s.domain.end;
    case 2:
      return s.domain.scale == plotData::LOG ? 1.f : 0.f;
    case 3:
      return s.range.start;
    case 4:
      return s.range.end;
    case 5:
      return s.range.scale == plotData::LOG ? 1.f : 0.f;
    case 6:
      return s.xOrigin;
    case 7:
      return s.yOrigin;
    case 8:
      return s.filter == plotData::ATODB ? 1.f : 0.f;
    default:
      return s.values[index - AXES];
    }
  }

  unsigned int CellCount(const plotData::frame& f) {
    return HEADER + f.plots * (AXES + f.points);
  }

  // Cell `index` of `f`; "" past the end. Host thread.
  std::string FrameCell(const plotData::frame& f, unsigned int index) {
    switch (index) {
    case 0:
      return std::to_string(f.plots);
    case 1:
      return std::to_string(f.points);
    case 2:
      return std::to_string(f.serial);
    default:
      break;
    }
    if (index >= CellCount(f)) return std::string();
    const unsigned int per = AXES + f.points;
    const unsigned int at = index - HEADER;
    return Cell(SubplotCell(f.subplots[at / per], at % per));
  }

  // `linear` or `log` (or 0 / 1) after an axis' two numbers; linear when
  // there is none.
  plotData::SCALE Scale(std::string_view word) {
    if (word == "log" || word == "1") return plotData::LOG;
    return plotData::LINEAR;
  }

  // The number in the token from `from`, NaN when there is none; `next` is
  // where the token ends.
  float NumberAt(const std::string& text, std::size_t from, std::size_t& next) {
    const std::string_view token = Token(text, from, next);
    float number = 0.f;
    if (token.empty() || !ReadNumericToken(token.data(), token.size(), number))
      return std::nanf("");
    return number;
  }
} // namespace

#define className dPlot

CONSTRUCT_DSP() {
  numpoints = static_cast<float>(plotData::DEFAULT_POINTS);
  numplots = 1.f;
  audioframesize = 0.f;
  audioframerate = plotData::DEFAULT_FRAME_RATE;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(numpoints);
  ADD_PARAM(numplots);
  ADD_PARAM(audioframesize);
  ADD_PARAM(audioframerate);

  ShapePorts();

  ADD_DESCRIPTION("The data and axes of a 2-D plot of one or more subplots, each set by lists, "
                  "taken from a signal, or read from a ~buffer, for the host to draw.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  PARAM_DOC("numpoints", "4",
            "The values each subplot shows across its domain ('~plot 64'). Longer lists are "
            "kept up to 4096 values; the first numpoints are shown.",
            "1-4096");
  PARAM_DOC("numplots", "1",
            "The subplots, one inlet each ('~plot 64 2'). Changing it rebuilds the inlets.",
            "1-16");
  PARAM_DOC("audioframesize", "0",
            "The samples a signal frame takes in ('~plot 64 1 1024'); its first numpoints are "
            "shown. 0, or anything up to numpoints, is numpoints.",
            "0 or more samples");
  PARAM_DOC("audioframerate", "15",
            "How many frames a second a signal or a buffer is shown at ('~plot 64 1 0 30').",
            "0.1-1000");
}

void dPlot::ShapePorts() {
  // Rebuilt rather than resized: the subplot count comes from the
  // arguments, so the inlets, the plot and the bindings have to agree.
  const UInt plots = plotData::Plots(numplots.load());
  fn = std::make_unique<plotData>(plots, plotData::MAX_POINTS);
  // The host's first read shows the arguments' points, before any block.
  fn->points(plotData::Points(numpoints.load()))
      .frameSize(plotData::FrameSize(audioframesize.load()))
      .frameRate(audioframerate.load());
  fn->publish();
  (void)fn->acquire();
  bindings.clear();
  inputs.clear();
  for (UInt i = 0; i < plots; ++i) {
    bindings.push_back(std::make_unique<bufferBinding<DSP::sampleBufferReader>>());
    inputs.emplace_back(this, i == 0, static_cast<int>(i));
    REG_SIGNAL_IN;
    REG_FLOAT_IN(SetFloat);
    REG_INT_IN(SetInt);
    REG_LIST_IN(SetList);
    const std::string n = std::to_string(i + 1);
    inputs.back().SetDoc(
        "plot " + n,
        "Subplot " + n +
            ": a number sets its first value, a list its values from the first on; a signal "
            "shows frames of it. 'definedomain <start> <end> [linear|log]', 'definerange "
            "<start> <end> [linear|log]', 'definexorigin <x>', 'defineyorigin <y>', "
            "'definefilter <none|atodb>' set its axes; 'refer <buffer> [<channel>] [<start "
            "ms>] [<end ms>]' shows part of a ~buffer ('refer' alone stops). 'numpoints <n>', "
            "'audioframesize <n>' and 'audioframerate <fps>' set those parameters.",
        "any float");
  }
  const std::size_t entry = static_cast<std::size_t>(plotData::MAX_POINTS) + 3;
  ring.assign(static_cast<std::size_t>(plots) * 2 * entry + 1024, 0.f);
  scratch.assign(plotData::MAX_POINTS, 0.f);
  head.store(0, std::memory_order_relaxed);
  tail.store(0, std::memory_order_relaxed);
}

void dPlot::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler `refer` goes through, built here so a message on the
  // audio thread finds it.
  EnableFileIO();
}

// Control thread: the callbacks make a change of argument rebuild the
// object, so these run on one no audio thread has seen.
PARM_CLEAR() {
  numpoints = static_cast<float>(plotData::DEFAULT_POINTS);
  numplots = 1.f;
  audioframesize = 0.f;
  audioframerate = plotData::DEFAULT_FRAME_RATE;
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

void dPlot::Post(OP op, unsigned int plot, const Flt* args, unsigned int n) {
  if (sending.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const std::size_t size = ring.size();
  const std::size_t at = head.load(std::memory_order_relaxed);
  const std::size_t used = at - tail.load(std::memory_order_acquire);
  if (size - used < 3 + static_cast<std::size_t>(n)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
  } else {
    ring[at % size] = static_cast<Flt>(op);
    ring[(at + 1) % size] = static_cast<Flt>(plot);
    ring[(at + 2) % size] = static_cast<Flt>(n);
    for (unsigned int i = 0; i < n; ++i)
      ring[(at + 3 + i) % size] = args[i];
    head.store(at + 3 + n, std::memory_order_release);
  }
  sending.clear(std::memory_order_release);
}

void dPlot::PostNumbers(unsigned int plot, const std::string& text, std::size_t cursor) {
  // Written straight into the ring as the tokens are read, so a list of any
  // length up to the capacity needs no buffer of its own.
  if (sending.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const std::size_t size = ring.size();
  const std::size_t at = head.load(std::memory_order_relaxed);
  const std::size_t room = size - (at - tail.load(std::memory_order_acquire));
  std::size_t n = 0;
  bool full = room < 3;
  std::size_t next = cursor;
  while (!full) {
    const std::string_view token = Token(text, next, next);
    if (token.empty()) break;
    float number = 0.f;
    if (!ReadNumericToken(token.data(), token.size(), number)) continue;
    if (n >= plotData::MAX_POINTS) break; // cut at the capacity
    if (3 + n >= room) {
      full = true;
      break;
    }
    ring[(at + 3 + n) % size] = number;
    ++n;
  }
  if (full) {
    dropped.fetch_add(1, std::memory_order_relaxed);
  } else if (n > 0) {
    ring[at % size] = static_cast<Flt>(OP_SET);
    ring[(at + 1) % size] = static_cast<Flt>(plot);
    ring[(at + 2) % size] = static_cast<Flt>(n);
    head.store(at + 3 + n, std::memory_order_release);
  }
  sending.clear(std::memory_order_release);
}

void dPlot::Drain() {
  const std::size_t size = ring.size();
  std::size_t at = tail.load(std::memory_order_relaxed);
  const std::size_t end = head.load(std::memory_order_acquire);
  while (at < end) {
    const auto op = static_cast<OP>(static_cast<int>(ring[at % size]));
    const auto plot = static_cast<UInt>(ring[(at + 1) % size]);
    const auto n = static_cast<UInt>(ring[(at + 2) % size]);
    for (UInt i = 0; i < n && i < scratch.size(); ++i)
      scratch[i] = ring[(at + 3 + i) % size];
    const Flt* a = scratch.data();
    switch (op) {
    case OP_SET:
      fn->set(plot, a, n);
      break;
    case OP_DOMAIN:
      fn->domain(plot, a[0], a[1], a[2] != 0.f ? plotData::LOG : plotData::LINEAR);
      break;
    case OP_RANGE:
      fn->range(plot, a[0], a[1], a[2] != 0.f ? plotData::LOG : plotData::LINEAR);
      break;
    case OP_XORIGIN:
      fn->xOrigin(plot, a[0]);
      break;
    case OP_YORIGIN:
      fn->yOrigin(plot, a[0]);
      break;
    case OP_FILTER:
      fn->filter(plot, a[0] != 0.f ? plotData::ATODB : plotData::NONE);
      break;
    case OP_REGION:
      fn->region(plot, static_cast<UInt>(a[0]), a[1], a[2]);
      break;
    }
    at += 3 + n;
  }
  tail.store(at, std::memory_order_release);
}

FLOAT_IN(SetFloat) {
  (void)thread;
  if (std::isnan(value)) return;
  Post(OP_SET, static_cast<unsigned int>(inlet), &value, 1);
}

INT_IN(SetInt) {
  (void)thread;
  const auto number = static_cast<Flt>(value);
  Post(OP_SET, static_cast<unsigned int>(inlet), &number, 1);
}

// Numbers, and every word message: the object has no command channel, so
// a word-first message reaches here with the inlet it came in on. Read in
// place, so nothing allocates on whichever thread delivers it.
LIST_IN(SetList) {
  (void)thread;
  const auto plot = static_cast<unsigned int>(inlet);
  std::size_t next = 0;
  const std::string_view word = Token(value, 0, next);
  if (word.empty()) return;
  float number = 0.f;
  if (ReadNumericToken(word.data(), word.size(), number)) {
    PostNumbers(plot, value, 0);
    return;
  }

  if (word == "definedomain" || word == "definerange") {
    Flt args[3];
    args[0] = NumberAt(value, next, next);
    args[1] = NumberAt(value, next, next);
    args[2] = Scale(Token(value, next, next)) == plotData::LOG ? 1.f : 0.f;
    Post(word == "definedomain" ? OP_DOMAIN : OP_RANGE, plot, args, 3);
  } else if (word == "definexorigin" || word == "defineyorigin") {
    const Flt at = NumberAt(value, next, next);
    if (!std::isnan(at)) Post(word == "definexorigin" ? OP_XORIGIN : OP_YORIGIN, plot, &at, 1);
  } else if (word == "definefilter") {
    const std::string_view which = Token(value, next, next);
    if (which == "none" || which == "atodb") {
      const Flt atodb = which == "atodb" ? 1.f : 0.f;
      Post(OP_FILTER, plot, &atodb, 1);
    }
  } else if (word == "refer") {
    // The buffer is bound in the background; the region travels with the
    // other settings. The channel counts from 1.
    const std::string_view name = Token(value, next, next);
    if (plot < bindings.size()) bindings[plot]->Request(*this, name.data(), name.size());
    Flt args[3];
    const Flt channel = NumberAt(value, next, next);
    args[0] = std::isnan(channel) || channel < 1.f ? 0.f : std::floor(channel) - 1.f;
    args[1] = NumberAt(value, next, next);
    args[2] = NumberAt(value, next, next);
    Post(OP_REGION, plot, args, 3);
  } else if (word == "numpoints") {
    if (Number(value, next, number)) numpoints = number;
  } else if (word == "audioframesize") {
    if (Number(value, next, number)) audioframesize = number;
  } else if (word == "audioframerate") {
    if (Number(value, next, number)) audioframerate = number;
  }
  // Anything else (the drawing messages) is the host's.
}

CALC() {
  (void)thread;
  DSP::plotData& plot = *fn;
  plot.points(plotData::Points(numpoints.load()))
      .frameSize(plotData::FrameSize(audioframesize.load()))
      .frameRate(audioframerate.load());
  Drain();
  for (UInt p = 0; p < plot.plots(); ++p) {
    bufferBinding<DSP::sampleBufferReader>& binding = *bindings[p];
    binding.Take(*this); // a `refer` waiting since the last block
    if (!inputs[p].IsConstant()) {
      plot(p, inputs[p].Signal(0.f)); // a cord wins
    } else if (binding.Current().buffer() != nullptr) {
      const DSP::sampleView view = binding.Current().pin();
      plot(p, view, YSE::STANDARD_BUFFERSIZE);
    }
  }
  plot.publish();
}

// Host thread: the newest frame, every cell.
GUI_VALUE() {
  std::lock_guard<std::mutex> lock(reading);
  const plotData::frame f = fn->acquire();
  std::string out;
  out.reserve(16 + static_cast<std::size_t>(f.plots) * (AXES + f.points) * 12);
  out += FrameCell(f, 0);
  out += ' ';
  out += FrameCell(f, 1);
  out += ' ';
  out += FrameCell(f, 2);
  for (UInt p = 0; p < f.plots; ++p) {
    for (UInt i = 0; i < AXES + f.points; ++i) {
      out += ' ';
      Append(out, SubplotCell(f.subplots[p], i));
    }
  }
  return out;
}

GUI_VALUE_COUNT() {
  std::lock_guard<std::mutex> lock(reading);
  return CellCount(fn->held());
}

// Cell 0 takes the newest frame; the others read the one it took.
GUI_VALUE_AT() {
  std::lock_guard<std::mutex> lock(reading);
  const plotData::frame f = index == 0 ? fn->acquire() : fn->held();
  return FrameCell(f, index);
}

#undef className
