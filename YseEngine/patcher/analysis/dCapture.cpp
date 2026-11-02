#include "dCapture.h"

#include <cmath>
#include <cstring>
#include <string_view>
#include "../math/gExprEval.h"
#include "../pAtomList.h"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;
using YSE::DSP::sampleCapture;

namespace {
  constexpr int FILE_TAG_WRITE = 1;

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

  // A value as list text, into `number` (kExprValueTextMax bytes). The
  // length; a value that is not finite prints as "0.".
  std::size_t Format(float value, char* number) {
    const int written = ExprFormatValue(ExprValue::Float(value), number, kExprValueTextMax);
    return written > 0 ? static_cast<std::size_t>(written) : 0;
  }
} // namespace

#define className dCapture

CONSTRUCT_DSP() {
  snapshot.resize(fn.capacity());
  writePath.reserve(fileScheduler::PATH_CAPACITY);
  fileText.reserve(fileScheduler::BYTES_CAPACITY);
  AtomList::ReserveRender(scratch);

  // in 0: the signal (signal, float or int); a bang dumps; messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_BANG_IN(Dump);
  REG_FLOAT_IN(SetIn);
  REG_INT_IN(SetInInt);

  // out 0: the stored values as lists; out 1: how many, first. Sent after
  // the block (issue #1037).
  ADD_OUT_LIST;
  ADD_OUT_INT;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  ADD_DESCRIPTION("Stores the most recent (or the first) samples of a signal and sends them "
                  "out as lists of numbers on a dump, or writes them to a text file. For "
                  "debugging and test patches.");
  ADD_CATEGORY(pCategory::ANALYSIS);
  INLET_DOC(0, "signal",
            "The signal to store, or a number used while no signal is connected. A bang or "
            "'dump' sends the stored values; 'clear' empties the store; 'write' followed by a "
            "file name saves them as text, one value per line.",
            "any float");
  OUTLET_DOC(0, "values",
             "On a dump, the stored values oldest first, as lists of at most 256 values. A "
             "large store goes out over several blocks.",
             "list");
  OUTLET_DOC(1, "count", "On a dump, how many values follow, before the first list.", "0 and up");
  PARAM_DOC("capture", "4096",
            "An optional 'f' to keep the first samples instead of the most recent ones, the "
            "number of samples to keep (clamped to 1-1048576), then up to 10 positions in "
            "each block, counted from 0, to store instead of every sample ('~capture f 1000 "
            "0 64'). The stored samples are not saved with the patch.",
            "'f', a sample count, then block positions");
}

void dCapture::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // Control thread: the patcher's file table is built here, so a 'write' on
  // the audio thread only claims a slot in it.
  EnableFileIO();
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no audio thread has seen, and the store may be allocated here. Clear runs
// first on every Set, parse only when there are arguments.
PARM_CLEAR() {
  arguments.clear();
  if (fn.capacity() != sampleCapture::DEFAULT_CAPACITY) {
    fn = sampleCapture();
    snapshot.assign(fn.capacity(), 0.f);
  }
  fn.mode(sampleCapture::LAST);
  fn.indices(nullptr, 0);
}

PARM_PARSE() {
  // 'f' anywhere; the first number is the capacity, later ones the block
  // positions. Anything else is ignored.
  bool haveCapacity = false;
  UInt positions[sampleCapture::MAX_INDICES] = {};
  UInt count = 0;
  for (const std::string& token : arguments) {
    if (token == "f") {
      fn.mode(sampleCapture::FIRST);
      continue;
    }
    float number = 0.f;
    if (!ReadNumericToken(token.data(), token.size(), number) || !std::isfinite(number)) {
      continue;
    }
    if (!haveCapacity) {
      haveCapacity = true;
      UInt capacity = 1;
      if (number >= static_cast<float>(sampleCapture::MAXIMUM_CAPACITY)) {
        capacity = sampleCapture::MAXIMUM_CAPACITY;
      } else if (number >= 1.f) {
        capacity = static_cast<UInt>(std::lround(number));
      }
      if (capacity != fn.capacity()) {
        const sampleCapture::MODE mode = fn.mode();
        fn = sampleCapture(capacity);
        fn.mode(mode);
        snapshot.assign(fn.capacity(), 0.f);
      }
    } else if (count < sampleCapture::MAX_INDICES && number >= 0.f) {
      positions[count++] = static_cast<UInt>(std::lround(number));
    }
  }
  fn.indices(positions, count);
}

BANG_IN(Dump) {
  (void)inlet;
  (void)thread;
  dumpRequested = true;
}

FLOAT_IN(SetIn) {
  (void)inlet;
  (void)thread;
  in = value;
}

INT_IN(SetInInt) {
  (void)inlet;
  (void)thread;
  in = static_cast<float>(value);
}

MESSAGES() {
  (void)value;
  // 'dump', 'clear' or 'write <file>'; anything else is ignored.
  const char* text = message.c_str();
  const std::size_t size = message.size();
  std::size_t next = 0;
  const std::string_view word = Token(text, size, 0, next);
  if (word == "dump") {
    dumpRequested = true;
  } else if (word == "clear") {
    clearRequested = true;
  } else if (word == "write") {
    RequestWrite(text, size, next);
  }
}

// The file name is the rest of the message, its tokens joined by single
// spaces; there is no dialog to ask. Any thread: copied into the slot, no
// allocation.
void dCapture::RequestWrite(const char* text, std::size_t length, std::size_t pos) {
  if (FileIO() == nullptr || writing.test_and_set(std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (writeRequested.load(std::memory_order_acquire)) {
    // The audio thread has not taken the last one yet.
    writing.clear(std::memory_order_release);
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  std::size_t written = 0;
  std::size_t next = pos;
  for (;;) {
    const std::string_view token = Token(text, length, next, next);
    if (token.empty()) break;
    const std::size_t need = written + (written > 0 ? 1 : 0) + token.size();
    if (need >= fileScheduler::PATH_CAPACITY) {
      written = 0;
      break;
    }
    if (written > 0) pathSlot[written++] = ' ';
    std::memcpy(pathSlot + written, token.data(), token.size());
    written += token.size();
  }
  if (written == 0) {
    // No file name, or one too long.
    writing.clear(std::memory_order_release);
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  pathLength = written;
  writeRequested.store(true, std::memory_order_release);
  writing.clear(std::memory_order_release);
}

// The store as text, one value per line, into the reserved fileText. False
// when it does not fit.
bool dCapture::Save() {
  fileText.clear();
  char number[kExprValueTextMax];
  for (UInt i = 0; i < fn.size(); ++i) {
    const std::size_t size = Format(fn.sample(i), number);
    if (fileText.size() + size + 1 > fileScheduler::BYTES_CAPACITY) return false;
    fileText.append(number, size);
    fileText.push_back('\n');
  }
  return true;
}

CALC() {
  // Requests from the senders, at the start of the block: a clear applies
  // before this block is stored.
  if (clearRequested.exchange(false)) fn.clear();
  if (writeRequested.load(std::memory_order_acquire)) {
    // Within the room reserved in the constructor.
    writePath.assign(pathSlot, pathLength);
    writeRequested.store(false, std::memory_order_release);
    writePending = true;
  }

  // Unconnected, the inlet reads its number (#1036).
  fn(inputs[0].Signal(in.load()));

  // A dump takes the store as it is at the end of this block; one asked for
  // during a dump waits for it to end.
  if (!dumping && dumpRequested.exchange(false)) {
    dumpEnd = fn.copy(snapshot.data(), static_cast<UInt>(snapshot.size()));
    dumpAt = 0;
    dumping = true;
    countPending = true;
  }
  // Never Send* a control message from CALC(): the patcher sends these after
  // the traversal, through FlushArmedMessages.
  if (dumping || writePending) MarkOutletArmed();
}

// After the block's traversal, on the audio thread: right outlet first, then
// this block's share of the dump, then a write.
void dCapture::FlushArmedMessages(YSE::THREAD thread) {
  if (countPending) {
    countPending = false;
    outputs[1].SendInt(static_cast<int>(dumpEnd), thread);
  }

  if (dumping) {
    const UInt stop =
        dumpEnd - dumpAt > DUMP_VALUES_PER_BLOCK ? dumpAt + DUMP_VALUES_PER_BLOCK : dumpEnd;
    char number[kExprValueTextMax];
    std::size_t atoms = 0;
    scratch.clear();
    for (; dumpAt < stop; ++dumpAt) {
      const std::size_t size = Format(snapshot[dumpAt], number);
      // A list stays within what an AtomList holds.
      if (atoms > 0 &&
          (atoms == AtomList::MAX_ATOMS || scratch.size() + 1 + size > AtomList::TEXT_CAPACITY)) {
        outputs[0].SendList(scratch, thread);
        scratch.clear();
        atoms = 0;
      }
      if (atoms > 0) scratch.push_back(' ');
      scratch.append(number, size);
      atoms++;
    }
    if (atoms > 0) outputs[0].SendList(scratch, thread);
    if (dumpAt == dumpEnd) dumping = false;
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
}

#undef className
