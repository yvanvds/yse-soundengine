// `~dspstate`, `~dsptime`, `~dspstress` (issue #1172): see pDspStatus.h. The
// patcher keeps the figures (patcherImplementation::DspOn, DspTimeMs,
// RenderLoad); these objects only report them.
#include "pDspStatus.h"

#include "../patcherImplementation.h"

using namespace YSE::PATCHER;

// ─── ~dspstate ────────────────────────────────────────────────────────────────

#define className pDspstate

CONSTRUCT() {
  // Not active: a bang reports through its handler, and Calculate is the
  // block poll alone.
  inputs.emplace_back(this, false, 0);
  REG_BANG_IN(Bang);

  ADD_OUT_INT; // 0 DSP on/off
  ADD_OUT_FLOAT; // 1 sample rate
  ADD_OUT_INT; // 2 signal block size
  ADD_OUT_INT; // 3 I/O block size

  ADD_DESCRIPTION("Reports whether the patcher's DSP is on, the sample rate and the block size: on "
                  "a bang, and by itself whenever the DSP is turned on or off.");
  ADD_CATEGORY(pCategory::IO);
  INLET_DOC(0, "bang", "Reports the DSP state, sample rate and block size.", "");
  OUTLET_DOC(0, "DSP on/off",
             "1 when the patcher's DSP is on, 0 when it is off. Sent last, after the other "
             "outlets.",
             "0 or 1");
  OUTLET_DOC(1, "sample rate", "The sample rate.", "Hz");
  OUTLET_DOC(2, "signal block size", "The number of samples the patcher renders per block.",
             "samples");
  OUTLET_DOC(3, "I/O block size",
             "The number of samples the patcher takes in and hands out per block.", "samples");
}

bool pDspstate::DspOn() const {
  if (parent == nullptr) return true;
  return static_cast<const patcherImplementation*>(parent)->DspOn();
}

void pDspstate::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  wasOn.store(DspOn(), std::memory_order_relaxed);
}

void pDspstate::Report(YSE::THREAD thread) {
  const int block = static_cast<int>(YSE::STANDARD_BUFFERSIZE);
  outputs[3].SendInt(block, thread);
  outputs[2].SendInt(block, thread);
  outputs[1].SendFloat(static_cast<float>(YSE::SAMPLERATE), thread);
  outputs[0].SendInt(DspOn() ? 1 : 0, thread);
}

BANG_IN(Bang) {
  (void)inlet;
  Report(thread);
}

// The block poll, at the top of every block with the other drains: a switch
// that changed since the last block is reported, once.
CALC() {
  const bool on = DspOn();
  if (on == wasOn.load(std::memory_order_relaxed)) return;
  wasOn.store(on, std::memory_order_relaxed);
  Report(thread);
}

#undef className

// ─── ~dsptime ─────────────────────────────────────────────────────────────────

#define className pDsptime

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Bang);

  ADD_OUT_FLOAT;

  ADD_DESCRIPTION("Reports the milliseconds of audio the patcher has processed since its DSP was "
                  "last turned on. Counts processed samples, not the wall clock.");
  ADD_CATEGORY(pCategory::IO);
  INLET_DOC(0, "bang", "Reports the time processed.", "");
  OUTLET_DOC(0, "time",
             "Milliseconds of audio processed since the patcher's DSP was last turned on.", "ms");
}

BANG_IN(Bang) {
  (void)inlet;
  const double ms =
      parent == nullptr ? 0.0 : static_cast<const patcherImplementation*>(parent)->DspTimeMs();
  outputs[0].SendFloat(static_cast<float>(ms), thread);
}

#undef className

// ─── ~dspstress ───────────────────────────────────────────────────────────────

#define className pDspstress

CONSTRUCT() {
  // Not active: Calculate is the block poll alone.
  inputs.emplace_back(this, false, 0);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);

  ADD_OUT_FLOAT;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(intervalArg);

  ADD_DESCRIPTION("Reports how much of the available time the patcher takes to process its "
                  "audio, in percent, at a regular interval and only when it changes.");
  ADD_CATEGORY(pCategory::IO);
  INLET_DOC(0, "interval", "Sets the reporting interval in ms; 0 stops reporting.", "ms");
  OUTLET_DOC(0, "load",
             "The mean processing time over the interval, as a percentage of the audio's "
             "duration. Above 100 the patch cannot keep up.",
             "percent");
  PARAM_DOC("interval", "250", "The reporting interval in ms; 0 stops reporting.", "ms");
}

PARM_CLEAR() {
  intervalArg = DEFAULT_INTERVAL_MS;
}

// Control thread, on an object no audio thread has seen yet (a change of
// argument rebuilds the object).
PARM_PARSE() {
  SetInterval(intervalArg);
}

void pDspstress::SetInterval(float ms) {
  // NaN and negatives are off.
  interval.store(ms > 0.f ? ms : 0.f, std::memory_order_relaxed);
}

INT_IN(SetInt) {
  (void)inlet;
  (void)thread;
  SetInterval(static_cast<float>(value));
}

FLOAT_IN(SetFloat) {
  (void)inlet;
  (void)thread;
  SetInterval(value);
}

void pDspstress::Feed(float load, double blockMs, YSE::THREAD thread) {
  const float ms = interval.load(std::memory_order_relaxed);
  if (!(ms > 0.f)) {
    // Off: the next window starts when an interval is set again.
    elapsedMs = 0.0;
    loadSum = 0.0;
    blocks = 0;
    return;
  }
  elapsedMs += blockMs;
  loadSum += static_cast<double>(load);
  blocks++;
  if (elapsedMs < static_cast<double>(ms)) return;

  const float percent = static_cast<float>(100.0 * loadSum / static_cast<double>(blocks));
  elapsedMs = 0.0;
  loadSum = 0.0;
  blocks = 0;
  if (sentAny && percent == lastSent) return;
  sentAny = true;
  lastSent = percent;
  outputs[0].SendFloat(percent, thread);
}

// The block poll: the load of the block before this one (this block's own is
// measured once it ends).
CALC() {
  if (parent == nullptr) return;
  const double blockMs =
      static_cast<double>(YSE::STANDARD_BUFFERSIZE) * 1000.0 / static_cast<double>(YSE::SAMPLERATE);
  Feed(static_cast<const patcherImplementation*>(parent)->RenderLoad(), blockMs, thread);
}

#undef className
