#include "dSync.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include "headers/defines.hpp"
#if YSE_ENABLE_MIDI_DEVICE
#include "../../midi/midiInHub.h"
#endif

#include <cmath>
#include <cstring>

using namespace YSE::PATCHER;
using YSE::DSP::clockFollower;

namespace {
  // `<word>` or `<word> <number>`, walked in place so nothing allocates on
  // whichever thread delivers the message. `hasNumber` says whether a number
  // followed.
  bool ReadCommand(const std::string& message, std::size_t& begin, std::size_t& word,
                   bool& hasNumber, float& number) {
    const char* text = message.c_str();
    const std::size_t length = message.size();
    begin = 0;
    while (begin < length && IsSelectorSeparator(text[begin]))
      begin++;
    std::size_t end = begin;
    while (end < length && !IsSelectorSeparator(text[end]))
      end++;
    word = end - begin;

    std::size_t arg = end;
    while (arg < length && IsSelectorSeparator(text[arg]))
      arg++;
    std::size_t argEnd = arg;
    while (argEnd < length && !IsSelectorSeparator(text[argEnd]))
      argEnd++;
    hasNumber = ReadNumericToken(text + arg, argEnd - arg, number) && !std::isnan(number);
    return word > 0;
  }

  bool Is(const std::string& message, std::size_t begin, std::size_t word, const char* name) {
    const std::size_t length = std::strlen(name);
    return word == length && message.compare(begin, length, name) == 0;
  }

  void AppendNumber(std::string& out, float value) {
    char number[kExprValueTextMax];
    const int written = ExprFormatValue(ExprValue::Float(value), number, kExprValueTextMax);
    if (written > 0) out.append(number, static_cast<std::size_t>(written));
  }

#if YSE_ENABLE_MIDI_DEVICE
  // MIDI system real time: 0xF8..0xFF, one byte each.
  constexpr unsigned char kFirstRealTime = 0xF8;
#endif
} // namespace

#define className dSync

CONSTRUCT_DSP() {
  // in 0: click track (signal); bang = tap, int = MIDI byte, float = tempo;
  // bpm, offset, midioffset, ppq, start and stop messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Tap);
  REG_INT_IN(Midi);
  REG_FLOAT_IN(Tempo);

  // out 0: the ramp; out 1: reports; out 2: MIDI clock (sent after the block, #1037)
  ADD_OUT_BUFFER;
  ADD_OUT_ANY;
  ADD_OUT_INT;

  ADD_PARAM(bpm);
  ADD_PARAM(offset);
  ADD_PARAM(midioffset);
  ADD_PARAM(ppq);
  ADD_PARAM(rtport);
  ADD_PARAM(transport);
  // A live re-parse rebuilds the object, so a new rtport is subscribed by the
  // replacement's SetParent.
  REG_PARM_PARSE;

  // "bpm " and a number of at most kExprValueTextMax
  scratch.reserve(kExprValueTextMax + 8);

  ADD_DESCRIPTION("A ramp from 0 to 1 once per beat, at a tempo set by a number, by tapping, by "
                  "MIDI beat clock, by a click track or by a domain clock, with the tempo "
                  "reported and a MIDI beat clock out that follows the ramp.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "in",
            "A click track (a signal with a peak above 0.1 on every beat); a bang is a tap; an "
            "int is a MIDI byte (248 tick, 250 start, 251 continue, 252 stop); a float sets the "
            "tempo. Takes 'bpm <x>', 'offset <0-1>', 'midioffset <0-1>', 'ppq <n>', 'start' and "
            "'stop' (not saved).",
            "signal, bang, 248-252, BPM");
  OUTLET_DOC(0, "ramp", "Rises from 0 to 1 once per beat.", "0-1");
  OUTLET_DOC(1, "report",
             "After a block: 'midi', 'click' and 'tap' when one or more of each arrived, then "
             "'bpm <x>' when the tempo changed.",
             "midi, click, tap, bpm <x>");
  OUTLET_DOC(2, "clock",
             "MIDI beat clock that follows the ramp, one byte at a time: 248 ppq times a beat "
             "while running, 250 on a start, 251 on a continue, 252 on a stop.",
             "248-252");
  PARAM_DOC("bpm", "120", "The tempo at the start, in beats per minute.", "1 to 1000");
  PARAM_DOC("offset", "0", "Shifts the ramp by this much of a beat.", "0-1");
  PARAM_DOC("midioffset", "0", "Shifts the MIDI clock out by this much of a beat.", "0-1");
  PARAM_DOC("ppq", "24", "MIDI clock ticks per beat, in and out.", "1 to 192");
  PARAM_DOC("rtport", "-1",
            "A MIDI input port, counted from 0, whose clock bytes are read directly, as if from "
            ".rtin; -1 reads none.",
            "-1 to 7");
  PARAM_DOC("transport", "",
            "A domain clock to follow, by name (a .transport, a .setclock or the host's): the ramp "
            "is its beat and the tempo its tempo, and taps, MIDI, clicks, tempos, start and stop "
            "are ignored. Empty follows none.",
            "");
}

dSync::~dSync() {
  Unsubscribe();
}

PARM_PARSE() {
  if (ppq.load() < 1) ppq = 1;
  if (ppq.load() > static_cast<Int>(clockFollower::MAX_PPQ))
    ppq = static_cast<Int>(clockFollower::MAX_PPQ);
  if (rtport.load() < -1) rtport = -1;
  // Control thread, before the object is in a graph: the class may be set
  // directly.
  fn.offset(offset).clockOffset(midioffset).ppq(static_cast<UInt>(ppq.load()));
  fn.reset(bpm);
}

void dSync::Unsubscribe() {
  const unsigned int held = subscription.exchange(0, std::memory_order_acq_rel);
  if (held == 0) return;
#if YSE_ENABLE_MIDI_DEVICE
  YSE::MIDI::InHub().Unsubscribe(held);
#endif
}

void dSync::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // Control thread: the transport clock is bound here and only here (a live
  // re-parse rebuilds the object), so CALC() only ever reads it.
  clock.Bind(Clocks(), transport.c_str(), transport.size());
  // As the .rtin family does: re-take the subscription whenever the parent
  // changes, and hold none outside a patcher.
  Unsubscribe();
#if YSE_ENABLE_MIDI_DEVICE
  if (newParent == nullptr || rtport.load() < 0) return;
  subscription.store(YSE::MIDI::InHub().Subscribe(static_cast<unsigned int>(rtport.load()), Type()),
                     std::memory_order_release);
#endif
}

void dSync::Push(eventKind kind, float value) {
  if (!events.try_push(event{kind, value})) dropped.fetch_add(1, std::memory_order_relaxed);
}

BANG_IN(Tap) {
  (void)inlet;
  (void)thread;
  Push(eventKind::TAP, 0.f);
}

INT_IN(Midi) {
  (void)inlet;
  (void)thread;
  Push(eventKind::MIDI, static_cast<float>(value));
}

FLOAT_IN(Tempo) {
  (void)inlet;
  (void)thread;
  Push(eventKind::TEMPO, value);
}

MESSAGES() {
  (void)value;
  std::size_t begin = 0;
  std::size_t word = 0;
  bool hasNumber = false;
  float number = 0.f;
  if (!ReadCommand(message, begin, word, hasNumber, number)) return;
  if (Is(message, begin, word, "start")) {
    Push(eventKind::START, 0.f);
  } else if (Is(message, begin, word, "stop")) {
    Push(eventKind::STOP, 0.f);
  } else if (!hasNumber) {
    return;
  } else if (Is(message, begin, word, "bpm")) {
    Push(eventKind::TEMPO, number);
  } else if (Is(message, begin, word, "offset")) {
    offset = number;
  } else if (Is(message, begin, word, "midioffset")) {
    midioffset = number;
  } else if (Is(message, begin, word, "ppq")) {
    if (!(number >= 1.f)) number = 1.f;
    if (number > static_cast<float>(clockFollower::MAX_PPQ))
      number = static_cast<float>(clockFollower::MAX_PPQ);
    ppq = static_cast<Int>(number);
  }
}

CALC() {
  fn.offset(offset).clockOffset(midioffset).ppq(static_cast<UInt>(ppq.load()));

  // Events in arrival order, then what the rtport wire brought.
  event e{eventKind::TAP, 0.f};
  while (events.try_pop(e)) {
    switch (e.kind) {
    case eventKind::TAP:
      fn.tap();
      break;
    case eventKind::TEMPO:
      fn.tempo(e.value);
      break;
    case eventKind::MIDI:
      fn.midi(static_cast<Int>(e.value));
      break;
    case eventKind::START:
      fn.start();
      break;
    case eventKind::STOP:
      fn.stop();
      break;
    }
  }
#if YSE_ENABLE_MIDI_DEVICE
  const unsigned int held = subscription.load(std::memory_order_acquire);
  if (held != 0) {
    std::size_t budget = YSE::MIDI::inHub::kDrainPerBlock;
    YSE::MIDI::inEvent wire;
    while (budget > 0 && YSE::MIDI::InHub().TryPop(held, wire)) {
      budget--;
      // A real-time message is exactly one byte; see .rtin.
      if (wire.len == 1 && wire.bytes[0] >= kFirstRealTime) fn.midi(wire.bytes[0]);
    }
  }
#endif

  // With a transport the clock is the beat (issue #1278), and the class
  // ignores the events above. Unconnected, the inlet has no click track
  // (#1036).
  DSP::buffer& out = clock.Bound() ? fn(clock.Read(Clocks(), STANDARD_BUFFERSIZE))
                                   : (inputs[0].IsConstant() ? fn() : fn(inputs[0].Signal(0.f)));
  outputs[0].SendBuffer(&out, thread);

  // Never Send* from CALC(): the patcher calls FlushArmedMessages after the
  // traversal.
  if (fn.clockBytes() > 0 || fn.ticks() > 0 || fn.clicks() > 0 || fn.taps() > 0 ||
      fn.tempoChanged()) {
    flushPending = true;
    MarkOutletArmed();
  }
}

// After the block's traversal, on the audio thread: right outlet first. The
// class's report holds until the next CALC().
void dSync::FlushArmedMessages(YSE::THREAD thread) {
  if (!flushPending) return;
  flushPending = false;
  for (UInt i = 0; i < fn.clockBytes(); ++i)
    outputs[2].SendInt(fn.clockByte(i), thread);
  if (fn.ticks() > 0) outputs[1].SendMessage(midiWord, thread);
  if (fn.clicks() > 0) outputs[1].SendMessage(clickWord, thread);
  if (fn.taps() > 0) outputs[1].SendMessage(tapWord, thread);
  if (fn.tempoChanged()) {
    scratch.assign("bpm ");
    AppendNumber(scratch, fn.tempo());
    outputs[1].SendMessage(scratch, thread);
  }
}

#undef className
