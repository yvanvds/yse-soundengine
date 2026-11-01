#include "dSeq.h"

#include <cmath>
#include <cstring>
#include "../math/gExprEval.h"
#include "../pAtomList.h"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

namespace {
  using YSE::DSP::eventSequencer;

  // A power of two, so the wrap is a mask. Each slot carries an event's
  // text inline.
  constexpr UInt QUEUE = 512;
  constexpr UInt QUEUE_MASK = QUEUE - 1;
  constexpr UInt TEXT = eventSequencer::TEXT_CAPACITY;
  // One line out: a sequence, a phase and an event's text.
  constexpr std::size_t LINE = FORMAT_INT_WIDTH + 1 + kExprValueTextMax + 1 + TEXT + 1;
  constexpr int FILE_TAG_READ = 1;
  constexpr int FILE_TAG_WRITE = 2;

  // The commands, as they travel in the queue. Prefixed: YSE's OUT_TYPE
  // has a BANG that member functions would find first.
  enum OP : int {
    OP_EVENT, // recorded when record or overdub is on
    OP_ADD,
    OP_DELETE,
    OP_CLEAR,
    OP_CLEAR_CURRENT,
    OP_CLEAR_ALL,
    OP_SELECT,
    OP_PLAY,
    OP_RECORD,
    OP_OVERDUB,
    OP_DUMP,
    OP_DUMP_ALL,
    OP_STATE,
    OP_WRITE,
  };

  // The next token of `text` from `pos`: [begin, end). Empty at the end.
  // Walked in place: no allocation on whichever thread sends.
  void NextToken(const char* text, std::size_t length, std::size_t pos, std::size_t& begin,
                 std::size_t& end) {
    while (pos < length && IsSelectorSeparator(text[pos]))
      pos++;
    begin = pos;
    while (pos < length && !IsSelectorSeparator(text[pos]))
      pos++;
    end = pos;
  }

  bool Is(const char* text, std::size_t begin, std::size_t end, const char* word) {
    const std::size_t length = std::strlen(word);
    return end - begin == length && std::strncmp(text + begin, word, length) == 0;
  }

  // The next token from `end` as a finite number; false, `end` unmoved,
  // when there is none or it is not one.
  bool NextNumber(const char* text, std::size_t length, std::size_t& end, float& number) {
    std::size_t begin = 0;
    std::size_t stop = 0;
    NextToken(text, length, end, begin, stop);
    if (begin == stop) return false;
    if (!ReadNumericToken(text + begin, stop - begin, number) || !std::isfinite(number)) {
      return false;
    }
    end = stop;
    return true;
  }

  Int ToSequence(float value) {
    return static_cast<Int>(std::lround(value));
  }

  // The rest of `text` from `pos`, its tokens joined by single spaces, into
  // `out` (room for `capacity` bytes and a terminator). The length; 0 when
  // there is nothing, or it does not fit.
  std::size_t Normalize(const char* text, std::size_t length, std::size_t pos, char* out,
                        std::size_t capacity) {
    std::size_t written = 0;
    std::size_t begin = 0;
    std::size_t end = pos;
    for (;;) {
      NextToken(text, length, end, begin, end);
      if (begin == end) break;
      const std::size_t size = end - begin;
      const std::size_t need = written + (written > 0 ? 1 : 0) + size;
      if (need >= capacity) return 0;
      if (written > 0) out[written++] = ' ';
      std::memcpy(out + written, text + begin, size);
      written += size;
    }
    out[written] = '\0';
    return written;
  }

  // The words the inlet takes as commands, each between spaces; anything
  // else is an event.
  constexpr const char* COMMANDS =
      " add clear erase delete dump offset overdub play record read write seq seqnum bang ";

  bool IsCommand(const char* text, std::size_t begin, std::size_t end) {
    const std::size_t size = end - begin;
    for (const char* at = COMMANDS; *at != '\0'; ++at) {
      if (*at == ' ' && std::strncmp(at + 1, text + begin, size) == 0 && at[size + 1] == ' ') {
        return true;
      }
    }
    return false;
  }

  void AppendNumber(std::string& out, float value) {
    char number[kExprValueTextMax];
    const int written = ExprFormatValue(ExprValue::Float(value), number, kExprValueTextMax);
    if (written > 0) out.append(number, static_cast<std::size_t>(written));
  }

  void AppendInt(std::string& out, int value) {
    char digits[FORMAT_INT_WIDTH];
    out.append(digits, WriteInt(value, digits));
  }
} // namespace

#define className dSeq

CONSTRUCT_DSP() {
  queue.resize(QUEUE);
  writePath.reserve(TEXT);
  fileText.reserve(fileScheduler::BYTES_CAPACITY);
  scratch.reserve(LINE);

  // in 0: the driving phase (signal); numbers, lists and messages are
  // events or commands, the word messages arriving as lists
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Bang);
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  REG_LIST_IN(SetList);

  // out 0: the events; out 1: dump's lists; out 2: the state. Sent after the
  // block (issue #1037).
  ADD_OUT_ANY;
  ADD_OUT_LIST;
  ADD_OUT_LIST;

  ADD_DESCRIPTION("Event sequencer driven by a 0..1 ramp: messages stored at phase positions, "
                  "added or recorded live, sent when the ramp crosses them.");
  ADD_CATEGORY(pCategory::SEQUENCE);
  INLET_DOC(0, "phase",
            "The driving phase, a signal running 0 to 1 such as a ~phasor (unconnected reads "
            "0). A number, a list or a message that is not a command is an event, recorded at "
            "the current phase while 'record' or 'overdub' is on. Commands: 'add', 'clear', "
            "'erase', 'delete', 'dump', 'offset', 'overdub', 'play', 'record', 'read', "
            "'write', 'seq' and 'seqnum'. A bang reports the state.",
            "0 to 1");
  OUTLET_DOC(0, "events", "Each event as it is crossed, the message it was stored as.", "any");
  OUTLET_DOC(1, "dump", "On 'dump', one 'sequence phase message' list per event.", "list");
  OUTLET_DOC(2, "state",
             "On a bang: 'sequence play record overdub events-in-sequence events-in-all'.", "list");
}

void dSeq::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // Control thread: the patcher's file table is built here, so a 'read' or
  // 'write' on the audio thread only claims a slot in it.
  EnableFileIO();
}

BANG_IN(Bang) {
  (void)inlet;
  (void)thread;
  Post(OP_STATE, 0, 0.f, 0.f, nullptr, 0);
}

FLOAT_IN(SetFloat) {
  (void)inlet;
  (void)thread;
  if (!std::isfinite(value)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  char number[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Float(value), number, kExprValueTextMax);
  Event(number, written > 0 ? static_cast<std::size_t>(written) : 0);
}

INT_IN(SetInt) {
  (void)inlet;
  (void)thread;
  char digits[FORMAT_INT_WIDTH];
  Event(digits, WriteInt(value, digits));
}

LIST_IN(SetList) {
  (void)inlet;
  const char* text = value.c_str();
  const std::size_t length = value.size();
  std::size_t begin = 0;
  std::size_t end = 0;
  NextToken(text, length, 0, begin, end);
  if (begin == end) return;
  float number = 0.f;
  if (ReadNumericToken(text + begin, end - begin, number)) {
    Event(text, length);
  } else if (IsCommand(text, begin, end)) {
    Command(text, length, end, begin, thread);
  } else if (Is(text, begin, end, "list") || Is(text, begin, end, "symbol") ||
             Is(text, begin, end, "int") || Is(text, begin, end, "float")) {
    // A typed message's selector says what it is; the event is the rest.
    Event(text + end, length - end);
  } else {
    Event(text, length);
  }
}

// An event: recorded at the current phase if recording is on when the next
// block applies it.
void dSeq::Event(const char* text, std::size_t length) {
  Post(OP_EVENT, 0, 0.f, 0.f, text, length);
}

// Queues one command, its text (when there is one) normalized into the
// slot. Refused, counted, when another sender has the queue, the queue is
// full, or the text is empty or too long.
void dSeq::Post(int op, Int sequence, Flt a, Flt b, const char* text, std::size_t length) {
  if (writing.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const UInt start = head.load(std::memory_order_relaxed);
  if (start - tail.load(std::memory_order_acquire) >= QUEUE) {
    writing.clear(std::memory_order_release);
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  command& c = queue[start & QUEUE_MASK];
  c.length = 0;
  c.text[0] = '\0';
  if (text != nullptr) {
    c.length = static_cast<UInt>(Normalize(text, length, 0, c.text, TEXT));
    if (c.length == 0) {
      writing.clear(std::memory_order_release);
      dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
  }
  c.op = op;
  c.sequence = sequence;
  c.a = a;
  c.b = b;
  head.store(start + 1, std::memory_order_release);
  writing.clear(std::memory_order_release);
}

// The commands. `word` is where the command word starts, `pos` where it
// ends. A missing or malformed number refuses the message.
void dSeq::Command(const char* text, std::size_t length, std::size_t pos, std::size_t word,
                   THREAD thread) {
  (void)thread;
  const Int shift = offset.load(std::memory_order_relaxed);
  std::size_t end = pos;
  float number = 0.f;

  if (Is(text, word, pos, "bang")) {
    Post(OP_STATE, 0, 0.f, 0.f, nullptr, 0);
    return;
  }
  if (Is(text, word, pos, "add")) {
    float sequence = 0.f;
    float phase = 0.f;
    if (!NextNumber(text, length, end, sequence) || !NextNumber(text, length, end, phase)) {
      dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    // Post refuses a missing message.
    Post(OP_ADD, ToSequence(sequence) + shift, phase, 0.f, text + end, length - end);
    return;
  }
  if (Is(text, word, pos, "clear") || Is(text, word, pos, "erase")) {
    std::size_t begin = 0;
    std::size_t stop = 0;
    NextToken(text, length, end, begin, stop);
    if (begin == stop) {
      Post(OP_CLEAR_CURRENT, 0, 0.f, 0.f, nullptr, 0);
    } else if (Is(text, begin, stop, "all")) {
      Post(OP_CLEAR_ALL, 0, 0.f, 0.f, nullptr, 0);
    } else if (NextNumber(text, length, end, number)) {
      Post(OP_CLEAR, ToSequence(number) + shift, 0.f, 0.f, nullptr, 0);
    } else {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }
    return;
  }
  if (Is(text, word, pos, "delete")) {
    float sequence = 0.f;
    float from = 0.f;
    if (!NextNumber(text, length, end, sequence) || !NextNumber(text, length, end, from)) {
      dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    float to = from;
    (void)NextNumber(text, length, end, to);
    std::size_t begin = 0;
    std::size_t stop = 0;
    NextToken(text, length, end, begin, stop);
    if (begin == stop) {
      // Any message in the range.
      Post(OP_DELETE, ToSequence(sequence) + shift, from, to, nullptr, 0);
    } else {
      Post(OP_DELETE, ToSequence(sequence) + shift, from, to, text + end, length - end);
    }
    return;
  }
  if (Is(text, word, pos, "dump")) {
    std::size_t begin = 0;
    std::size_t stop = 0;
    NextToken(text, length, end, begin, stop);
    if (begin == stop) {
      Post(OP_DUMP_ALL, 0, 0.f, 0.f, nullptr, 0);
    } else if (NextNumber(text, length, end, number)) {
      Post(OP_DUMP, ToSequence(number) + shift, 0.f, 0.f, nullptr, 0);
    } else {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }
    return;
  }
  if (Is(text, word, pos, "read") || Is(text, word, pos, "write")) {
    // The file name is the rest of the message; there is no dialog to ask.
    char path[TEXT];
    const std::size_t size = Normalize(text, length, end, path, TEXT);
    fileScheduler* io = FileIO();
    if (size == 0 || io == nullptr) {
      dropped.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    if (Is(text, word, pos, "write")) {
      // The sequences live on the audio thread, which builds the text.
      Post(OP_WRITE, 0, 0.f, 0.f, path, size);
    } else if (!io->RequestRead(this, FILE_TAG_READ, path, size)) {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }
    return;
  }

  // The rest take one number; play, record and overdub default to 1.
  std::size_t begin = 0;
  std::size_t stop = 0;
  NextToken(text, length, end, begin, stop);
  const bool none = begin == stop;
  if (!none && !NextNumber(text, length, end, number)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const Flt on = none || number != 0.f ? 1.f : 0.f;
  if (Is(text, word, pos, "play")) {
    Post(OP_PLAY, 0, on, 0.f, nullptr, 0);
  } else if (Is(text, word, pos, "record")) {
    Post(OP_RECORD, 0, on, 0.f, nullptr, 0);
  } else if (Is(text, word, pos, "overdub")) {
    Post(OP_OVERDUB, 0, on, 0.f, nullptr, 0);
  } else if (none) {
    // offset, seq and seqnum need their number.
    dropped.fetch_add(1, std::memory_order_relaxed);
  } else if (Is(text, word, pos, "offset")) {
    offset.store(ToSequence(number), std::memory_order_relaxed);
  } else {
    Post(OP_SELECT, ToSequence(number) + shift, 0.f, 0.f, nullptr, 0);
  }
}

// One line per event, 'sequence phase message', into the reserved text.
// False when it does not fit a file.
bool dSeq::Save() {
  fileText.clear();
  for (UInt i = 0; i < fn.size(); ++i) {
    if (fileText.size() + LINE > fileText.capacity()) return false;
    AppendInt(fileText, static_cast<int>(fn.eventSequence(i)));
    fileText.push_back(' ');
    AppendNumber(fileText, fn.eventPhase(i));
    fileText.push_back(' ');
    fileText.append(fn.eventText(i), fn.eventLength(i));
    fileText.push_back('\n');
  }
  return true;
}

// A saved file replaces every sequence; a malformed line, or more events
// than the store holds, refuses the whole file. Read twice in place, once
// to check and once to store.
void dSeq::Load(const char* bytes, std::size_t count) {
  for (int pass = 0; pass < 2; ++pass) {
    UInt events = 0;
    std::size_t line = 0;
    while (line < count) {
      std::size_t stop = line;
      while (stop < count && bytes[stop] != '\n')
        stop++;
      std::size_t begin = 0;
      std::size_t end = 0;
      NextToken(bytes, stop, line, begin, end);
      if (begin != end) {
        float sequence = 0.f;
        float phase = 0.f;
        std::size_t at = line;
        char message[TEXT];
        if (!NextNumber(bytes, stop, at, sequence) || !NextNumber(bytes, stop, at, phase)) {
          return;
        }
        const std::size_t size = Normalize(bytes, stop, at, message, TEXT);
        if (size == 0 || ++events > fn.capacity()) return;
        if (pass == 1) (void)fn.add(ToSequence(sequence), phase, message, static_cast<UInt>(size));
      }
      line = stop + 1;
    }
    if (pass == 0) fn.clearAll();
  }
}

void dSeq::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  (void)thread;
  // A write reports by having happened, a failed read by nothing changing.
  if (result.op != FILE_OP::READ || result.tag != FILE_TAG_READ) return;
  if (!result.ok || result.bytes == nullptr) return;
  Load(result.bytes, result.byteCount);
}

CALC() {
  (void)thread;
  // Every command that arrived since the last block, in order.
  const UInt last = head.load(std::memory_order_acquire);
  UInt next = tail.load(std::memory_order_relaxed);
  while (next != last) {
    const command& c = queue[next & QUEUE_MASK];
    switch (c.op) {
    case OP_EVENT:
      (void)fn.recordEvent(c.text, c.length);
      break;
    case OP_ADD:
      (void)fn.add(c.sequence, c.a, c.text, c.length);
      break;
    case OP_DELETE:
      (void)fn.remove(c.sequence, c.a, c.b, c.length > 0 ? c.text : nullptr, c.length);
      break;
    case OP_CLEAR:
      (void)fn.clear(c.sequence);
      break;
    case OP_CLEAR_CURRENT:
      (void)fn.clear(fn.selected());
      break;
    case OP_CLEAR_ALL:
      fn.clearAll();
      break;
    case OP_SELECT:
      fn.select(c.sequence);
      break;
    case OP_PLAY:
      fn.play(c.a != 0.f);
      break;
    case OP_RECORD:
      fn.record(c.a != 0.f);
      break;
    case OP_OVERDUB:
      fn.overdub(c.a != 0.f);
      break;
    case OP_DUMP:
    case OP_DUMP_ALL:
      dumpPending = true;
      dumpAll = c.op == OP_DUMP_ALL;
      dumpSequence = c.sequence;
      break;
    case OP_STATE:
      statePending = true;
      break;
    case OP_WRITE:
      // Within the room reserved in the constructor.
      writePath.assign(c.text, c.length);
      writePending = true;
      break;
    default:
      break;
    }
    next++;
  }
  tail.store(next, std::memory_order_release);

  fn(inputs[0].Signal(0.f));
  if (fn.missed() > 0) missed.fetch_add(fn.missed(), std::memory_order_relaxed);
  // Never Send* a control message from CALC(): the patcher sends these after
  // the traversal, through FlushArmedMessages.
  if (fn.fired() > 0 || dumpPending || statePending || writePending) MarkOutletArmed();
}

// After the block's traversal, on the audio thread: right outlet first, then
// a write, then the events in the order they fired.
void dSeq::FlushArmedMessages(YSE::THREAD thread) {
  if (statePending) {
    statePending = false;
    const Int sequence = fn.selected();
    scratch.clear();
    AppendInt(scratch, static_cast<int>(sequence));
    scratch.append(fn.playing() ? " 1" : " 0");
    scratch.append(fn.recording() ? " 1" : " 0");
    scratch.append(fn.overdubbing() ? " 1 " : " 0 ");
    AppendInt(scratch, static_cast<int>(fn.count(sequence)));
    scratch.push_back(' ');
    AppendInt(scratch, static_cast<int>(fn.size()));
    outputs[2].SendList(scratch, thread);
  }

  if (dumpPending) {
    dumpPending = false;
    for (UInt i = 0; i < fn.size(); ++i) {
      if (!dumpAll && fn.eventSequence(i) != dumpSequence) continue;
      scratch.clear();
      AppendInt(scratch, static_cast<int>(fn.eventSequence(i)));
      scratch.push_back(' ');
      AppendNumber(scratch, fn.eventPhase(i));
      scratch.push_back(' ');
      scratch.append(fn.eventText(i), fn.eventLength(i));
      outputs[1].SendList(scratch, thread);
    }
  }

  if (writePending) {
    writePending = false;
    fileScheduler* io = FileIO();
    if (io == nullptr || !Save() ||
        !io->RequestWrite(this, FILE_TAG_WRITE, writePath.c_str(), writePath.size(),
                          fileText.c_str(), fileText.size())) {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }
  }

  for (UInt i = 0; i < fn.fired(); ++i) {
    const DSP::eventSequencer::firing& event = fn.firedEvent(i);
    if (std::memchr(event.text, ' ', event.length) == nullptr) {
      // One atom leaves as the int, float, bang or symbol it spells.
      if (event.length == 4 && std::strncmp(event.text, "bang", 4) == 0) {
        outputs[0].SendBang(thread);
      } else {
        SendAtom(outputs[0], event.text, event.length, scratch, thread);
      }
    } else {
      scratch.assign(event.text, event.length);
      outputs[0].SendList(scratch, thread);
    }
  }
}

#undef className
