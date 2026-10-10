#include "gHisto.h"
#include "../pListArgs.h"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gHisto

namespace {

  // A histogram report is exactly two numbers: value, count.
  constexpr int REPORT_ITEMS = 2;

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Bang);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);

  ADD_IN_1;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);

  ADD_OUT_LIST;
  ADD_OUT_BANG;

  ADD_PARAM(size);
  REG_PARM_PARSE;

  ADD_DESCRIPTION("Counts how often each number arrives and sends '<number> <count>' each time. "
                  "The right inlet reports a number's count without counting it.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in",
            "Number: count it and send its count. Bang: send the count of the last counted number "
            "again. 'clear': set every count to 0. 'dump': send every number counted so far.",
            "0 to size-1");
  INLET_DOC(1, "query", "Number: send its count without counting it.", "0 to size-1");
  OUTLET_DOC(0, "count", "'<number> <count>': how often this number has been counted.", "");
  OUTLET_DOC(1, "reject", "Bang when a number is outside 0 to size-1; it is not counted.", "");
  PARAM_DOC("size", "128", "How many numbers can be counted: 0 to size-1. Clamped to 1-4096.",
            "1-4096");
}

PARM_PARSE() {
  // Runs on the control thread after the creation parameters were parsed. The
  // bins are a fixed array, so the size cannot exceed it; below 1 there would be
  // no legal input value at all, so that is the floor.
  int clamped = size.load();
  if (clamped < 1) clamped = 1;
  if (clamped > CAPACITY) clamped = CAPACITY;
  size.store(clamped);
  // A parse may follow a live parameter change, so start from an empty
  // histogram rather than from counts collected under the previous range.
  ClearBins();
}

int gHisto::AddSaturating(int current, int delta) {
  const I64 sum = static_cast<I64>(current) + static_cast<I64>(delta);
  if (sum > static_cast<I64>(MAX_COUNT)) return MAX_COUNT;
  return static_cast<int>(sum);
}

int gHisto::CountOf(int value) const {
  if (!InRange(value)) return 0;
  return bins[static_cast<std::size_t>(value)].load(std::memory_order_relaxed);
}

void gHisto::ClearBins() {
  // The whole array rather than just [0, size): bounded either way, and it
  // leaves no stale count behind for a later size to expose.
  for (auto& bin : bins)
    bin.store(0, std::memory_order_relaxed);
  total.store(0);
}

void gHisto::Report(int value, int count, YSE::THREAD thread) {
  const int items[REPORT_ITEMS] = {value, count};
  outputs[0].SendList(FormatIntList(items, REPORT_ITEMS), thread);
}

void gHisto::Dump(YSE::THREAD thread) {
  const int n = size.load();
  // Bounded by CAPACITY. Empty bins are skipped: a full sweep of a 128-bin
  // histogram is 128 messages of which a handful are usually interesting.
  for (int i = 0; i < n; ++i) {
    const int count = bins[static_cast<std::size_t>(i)].load(std::memory_order_relaxed);
    if (count > 0) Report(i, count, thread);
  }
}

INT_IN(SetInt) {
  if (!InRange(value)) {
    // Max: "numbers outside this range are disregarded". Outlet 1 is how a
    // headless patch notices that its stream is falling outside the histogram.
    outputs[1].SendBang(thread);
    return;
  }

  const auto slot = static_cast<std::size_t>(value);

  if (inlet != 0) {
    // Max's right inlet: the same report, but the number is not counted — and
    // so it does not become what the next bang re-reports either.
    Report(value, bins[slot].load(std::memory_order_relaxed), thread);
    return;
  }

  const int count = AddSaturating(bins[slot].load(std::memory_order_relaxed), 1);
  bins[slot].store(count, std::memory_order_relaxed);
  total.store(AddSaturating(total.load(), 1));
  last.store(value);
  Report(value, count, thread);
}

FLOAT_IN(SetFloat) {
  // Int object: a float counts as the same number, truncated, as in Max.
  SetInt(static_cast<int>(value), inlet, thread);
}

BANG_IN(Bang) {
  (void)inlet; // only inlet 0 registers a bang handler

  // Max: "reports the frequency of the most recently received number", and 0
  // when there has not been one. `last` starts at 0 and every bin starts empty,
  // so that second case needs no special path.
  const int value = last.load();
  Report(value, CountOf(value), thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  if (value == "clear") {
    // Max: "erases the memory of histo, to begin a new histogram". The most
    // recently counted number is not part of that memory — keeping it lets a
    // bang after the clear report an honest zero for it.
    ClearBins();
    return;
  }

  if (value == "dump") {
    Dump(thread);
    return;
  }

  // Anything else is not a message this object knows. Ignored rather than
  // guessed at.
}

GUI_VALUE() {
  // How large the sample is. The last number counted is the other candidate,
  // but it is already visible on the outlet, while the size of the sample the
  // histogram rests on is not.
  return std::to_string(total.load());
}
