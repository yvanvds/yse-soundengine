#include "dOscbank.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include "../io/fileScheduler.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include "../../dsp/sampling/sampleBuffer.hpp"

using namespace YSE::PATCHER;

#define className oscillatorBankObject

// A table a copybuf task made (issue #1196). Made and freed off the audio
// thread; CALC() swaps its samples with the bank's storage, so once taken it
// holds the storage the bank let go.
struct oscillatorBankObject::stagedTable {
  std::vector<Flt> samples;
};

// The handoff between a copybuf task and CALC(), as ~cycle's (#1192). Shared
// by the object and the tasks it sends, which find it by id, so whichever
// lets go last frees it, and it frees every table it still holds.
//
//   task  --incoming-->  CALC()  --retired-->  task
//
// `incoming` is written by tasks (an unread table is replaced, and freed by
// the task) and taken by CALC() with one exchange. `retired` holds the table
// CALC() let go; CALC() fills it only while it is empty and any task empties
// it, so the audio thread never frees a table and never waits for one.
struct oscillatorBankObject::mailbox {
  explicit mailbox(std::uint64_t id) : id(id) {}
  ~mailbox();
  mailbox(const mailbox&) = delete;
  mailbox& operator=(const mailbox&) = delete;
  mailbox(mailbox&&) = delete;
  mailbox& operator=(mailbox&&) = delete;

  static std::shared_ptr<mailbox> Make();
  static std::shared_ptr<mailbox> Find(std::uint64_t id);

  const std::uint64_t id;
  std::atomic<stagedTable*> incoming{nullptr};
  std::atomic<stagedTable*> retired{nullptr};
  // A release task is queued or running. Set by CALC() before it asks, so the
  // task's clear always comes after it.
  std::atomic<bool> releaseQueued{false};
};

namespace {
  constexpr int MAX_OSCILLATORS = static_cast<int>(oscillatorBankControl::MAX_OSCILLATORS);

  UInt AtLeast(int value, int least) {
    return static_cast<UInt>(value < least ? least : value);
  }

  // The next token of `message` from `pos`: [begin, end). Empty at the end.
  // Walked in place: no allocation on whichever thread sends.
  void NextWord(const std::string& message, std::size_t pos, std::size_t& begin, std::size_t& end) {
    while (pos < message.size() && IsSelectorSeparator(message[pos]))
      pos++;
    begin = pos;
    while (pos < message.size() && !IsSelectorSeparator(message[pos]))
      pos++;
    end = pos;
  }

  enum class COPY_OP : std::uint8_t {
    COPY, // copybuf
    RELEASE, // free the table CALC() let go
  };

  // The process-wide mailboxes, by id. Weak: a mailbox lives while its object
  // or a task holds it. Control and background threads only (it locks).
  struct mailboxTable {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::weak_ptr<oscillatorBankObject::mailbox>> boxes;
    std::uint64_t next = 1;
  };

  mailboxTable& Mailboxes() {
    // Never destroyed, so a mailbox freed during static destruction still
    // finds its table.
    static mailboxTable* const instance =
        new mailboxTable(); // NOLINT(cppcoreguidelines-owning-memory): leaked on purpose
    return *instance;
  }
} // namespace

std::shared_ptr<oscillatorBankObject::mailbox> oscillatorBankObject::mailbox::Make() {
  mailboxTable& t = Mailboxes();
  const std::lock_guard<std::mutex> lock(t.mutex);
  std::shared_ptr<mailbox> made = std::make_shared<mailbox>(t.next++);
  t.boxes[made->id] = made;
  return made;
}

std::shared_ptr<oscillatorBankObject::mailbox>
oscillatorBankObject::mailbox::Find(std::uint64_t id) {
  mailboxTable& t = Mailboxes();
  const std::lock_guard<std::mutex> lock(t.mutex);
  auto it = t.boxes.find(id);
  return it == t.boxes.end() ? nullptr : it->second.lock();
}

// Off the audio thread: the last holder is the object's destructor or a task.
oscillatorBankObject::mailbox::~mailbox() {
  delete incoming.load(std::memory_order_acquire);
  delete retired.load(std::memory_order_acquire);
  mailboxTable& t = Mailboxes();
  const std::lock_guard<std::mutex> lock(t.mutex);
  t.boxes.erase(id);
}

// The record a copybuf or a release becomes. Copied whole into a scheduler
// slot, so it stays trivially copyable.
struct oscillatorBankObject::copyTask {
  std::uint64_t box = 0;
  COPY_OP op = COPY_OP::COPY;
  std::uint32_t points = 0; // how many samples the table is made of
  std::uint32_t offset = 0;
  char name[fileScheduler::PATH_CAPACITY] = {};
};

oscillatorBankObject::oscillatorBankObject(bool interpolating)
  : pObject(true),
    interpolating(interpolating),
    bank(DSP::oscillatorBank::DEFAULT_OSCILLATORS, interpolating),
    control(!interpolating),
    // Control thread: the mailbox exists before the object can be published.
    box(mailbox::Make()) {
  // in 0: frequency; in 1: magnitude; in 2: phase; in 3: the oscillator
  // they set. Each a signal, a float or an int. Messages on any inlet.
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  ADD_IN_2;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  ADD_IN_3;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_OUT_BUFFER;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(oscillators);
  ADD_PARAM(freqsmooth);
  ADD_PARAM(magsmooth);
  if (!interpolating) ADD_PARAM(tablesize);

  ADD_DESCRIPTION(interpolating
                      ? "A bank of sine oscillators with smoothed frequency and magnitude "
                        "changes, summed into one signal, reading its table between points for a "
                        "cleaner sine: additive synthesis, and resynthesis from analysis data."
                      : "A bank of sine oscillators with smoothed frequency and magnitude "
                        "changes, summed into one signal, reading its table without "
                        "interpolation (cheaper; set the table size with tabpoints): additive "
                        "synthesis, and resynthesis from analysis data.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "freq",
            "Frequency in Hz of the oscillator the index names. A signal sets it sample by "
            "sample; a number sets it once while no inlet carries a signal. Also takes 'set f m "
            "f m ...', 'clear', 'silence', 'size <n>', 'freqsmooth <n>', 'magsmooth <n>' and "
            "'framesync <0/1>'" +
                std::string(interpolating ? "" : ", 'tabpoints <n>'") +
                ", and 'copybuf <buffer> [offset]' to make the table from a buffer's first "
                "channel (in the background, from a later block; not saved with the patch).",
            "any float");
  INLET_DOC(1, "magnitude",
            "Magnitude (amplitude) of the oscillator the index names: a signal, or a number.",
            "0-1");
  INLET_DOC(2, "phase",
            "With framesync 1 only: the phase the oscillator the index names jumps to when its "
            "frame takes effect. A signal, or a number.",
            "0-1");
  INLET_DOC(3, "index",
            "The oscillator the other inlets set, from 0. A signal (one oscillator per sample, "
            "as an analysis frame arrives), or a number naming the oscillator later numbers set.",
            "0 to oscillators - 1");
  OUTLET_DOC(0, "out", "The sum of every oscillator: magnitude times sine.", "any float");
  PARAM_DOC("oscillators", "64",
            "How many oscillators the bank allocates; 'size' runs fewer. Changing it rebuilds "
            "the object.",
            "1-4096");
  PARAM_DOC("freqsmooth", "1", "Samples over which a new frequency is ramped to; 0 or 1 jumps.",
            "0 or more");
  PARAM_DOC("magsmooth", "0", "Samples over which a new magnitude is ramped to; 0 or 1 jumps.",
            "0 or more");
  if (!interpolating)
    PARAM_DOC("tablesize", "4096",
              "Points in the sine table, rounded to a power of two: more is cleaner.", "2-65536");
}

// Control thread: the callbacks make ParamsNeedRebuild() true, so a live
// change rebuilds the object and the allocation never happens on a bank the
// audio thread is rendering. Clear runs first on every Set, parse only when
// there are arguments.
PARM_CLEAR() {
  oscillators = static_cast<int>(DSP::oscillatorBank::DEFAULT_OSCILLATORS);
  freqsmooth = 1;
  magsmooth = 0;
  tablesize = static_cast<int>(DSP::oscillatorBank::DEFAULT_TABLE_SIZE);
  bank = DSP::oscillatorBank(DSP::oscillatorBank::DEFAULT_OSCILLATORS, interpolating);
  control.Build(DSP::oscillatorBank::DEFAULT_OSCILLATORS);
}

PARM_PARSE() {
  if (oscillators < 1) oscillators = 1;
  if (oscillators > MAX_OSCILLATORS) oscillators = MAX_OSCILLATORS;
  const UInt capacity = AtLeast(oscillators, 1);
  bank = DSP::oscillatorBank(capacity, interpolating,
                             interpolating ? DSP::oscillatorBank::DEFAULT_TABLE_SIZE
                                           : AtLeast(tablesize, 0));
  bank.frequencySmoothing(AtLeast(freqsmooth, 0));
  bank.magnitudeSmoothing(AtLeast(magsmooth, 0));
  control.Build(capacity);
}

FLOAT_IN(SetFloat) {
  (void)thread;
  switch (inlet) {
  case 0:
    control.Frequency(value);
    break;
  case 1:
    control.Magnitude(value);
    break;
  case 2:
    control.Phase(value);
    break;
  default:
    control.Index(value);
    break;
  }
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

void oscillatorBankObject::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler copybuf goes through, built here so a message on the audio
  // thread finds it.
  EnableFileIO();
}

bool oscillatorBankObject::ReleasePending() const {
  return box->retired.load(std::memory_order_acquire) != nullptr;
}

bool oscillatorBankObject::Submit(copyTask& task) {
  fileScheduler* io = FileIO();
  if (io == nullptr) return false;
  task.box = box->id;
  return io->RequestTask(this, 0, &oscillatorBankObject::RunTask, &task, sizeof(task));
}

bool oscillatorBankObject::CopyBuffer(const std::string& message) {
  std::size_t begin = 0;
  std::size_t end = 0;
  NextWord(message, 0, begin, end);
  if (end - begin != 7 || message.compare(begin, 7, "copybuf") != 0) return false;

  // The record lives on the stack and the scheduler copies it into a slot it
  // already owns: no allocation on the sending thread.
  copyTask task;
  std::size_t nameBegin = 0;
  std::size_t nameEnd = 0;
  NextWord(message, end, nameBegin, nameEnd);
  const std::size_t nameLength = nameEnd - nameBegin;
  if (nameLength == 0 || nameLength >= sizeof(task.name)) return true;
  std::memcpy(task.name, message.data() + nameBegin, nameLength);
  task.name[nameLength] = '\0';

  std::size_t offBegin = 0;
  std::size_t offEnd = 0;
  NextWord(message, nameEnd, offBegin, offEnd);
  float at = 0.f;
  if (ReadNumericToken(message.c_str() + offBegin, offEnd - offBegin, at) && std::isfinite(at) &&
      at > 0.f)
    task.offset = at >= 4294967040.f ? 0xFFFFFF00u : static_cast<std::uint32_t>(at);
  // The most a table can need: ~ioscbank's is fixed; ~oscbank's tabpoints
  // may change before the table lands, and any size reads from the start.
  task.points =
      interpolating ? DSP::oscillatorBank::DEFAULT_TABLE_SIZE : DSP::oscillatorBank::MAX_TABLE_SIZE;
  Submit(task);
  return true;
}

MESSAGES() {
  (void)value;
  if (CopyBuffer(message)) return;
  control.Message(message);
}

// ─── the background half ──────────────────────────────────────────────────────

bool oscillatorBankObject::RunTask(char* args, std::size_t argBytes) {
  static_assert(std::is_trivially_copyable<copyTask>::value,
                "a copyTask is copied into a scheduler slot with memcpy");
  if (argBytes != sizeof(copyTask)) return false;
  copyTask task;
  std::memcpy(&task, args, sizeof(task));

  // By id, not through the object: it may have been deleted since.
  std::shared_ptr<mailbox> found = mailbox::Find(task.box);
  if (found == nullptr) return false;

  // Whatever CALC() let go, whichever kind of task this is. The flag first:
  // a table CALC() retires after the exchange below asks again.
  found->releaseQueued.store(false, std::memory_order_release);
  delete found->retired.exchange(nullptr, std::memory_order_acq_rel);
  if (task.op == COPY_OP::RELEASE) return true;

  // A name no buffer has changes nothing.
  task.name[sizeof(task.name) - 1] = '\0';
  const std::shared_ptr<DSP::sampleBuffer> source = DSP::sampleBufferRegistry::find(task.name);
  if (source == nullptr) return false;

  std::unique_ptr<stagedTable> next = std::make_unique<stagedTable>();
  next->samples.assign(task.points, 0.f);
  const UInt copied = source->getSamples(0, task.offset, next->samples.data(), task.points);
  for (UInt i = 0; i < copied; ++i)
    if (!std::isfinite(next->samples[i])) next->samples[i] = 0.f;

  // A table CALC() has not taken yet is replaced: the last copybuf wins.
  // Tasks run one at a time, so nothing else fills `incoming` meanwhile.
  delete found->incoming.exchange(next.release(), std::memory_order_acq_rel);
  return true;
}

// Audio thread, after the queued messages: wait-free. Takes a waiting table
// only while the retired slot is empty, so the old storage always has
// somewhere to go.
void oscillatorBankObject::TakeTable() {
  mailbox& b = *box;
  if (b.retired.load(std::memory_order_acquire) == nullptr &&
      b.incoming.load(std::memory_order_relaxed) != nullptr) {
    stagedTable* next = b.incoming.exchange(nullptr, std::memory_order_acq_rel);
    if (next != nullptr) {
      // Swapped, not copied: `next` leaves holding the bank's old storage.
      bank.swapTable(next->samples);
      b.retired.store(next, std::memory_order_release);
    }
  }
  // Ask for the retired table to be freed. A refused request (a full table,
  // or no patcher) is asked again next block; any copybuf task frees it too.
  if (b.retired.load(std::memory_order_acquire) != nullptr &&
      !b.releaseQueued.exchange(true, std::memory_order_acq_rel)) {
    copyTask release;
    release.op = COPY_OP::RELEASE;
    if (!Submit(release)) b.releaseQueued.store(false, std::memory_order_release);
  }
}

CALC() {
  const bool signals = !inputs[0].IsConstant() || !inputs[1].IsConstant() ||
                       !inputs[2].IsConstant() || !inputs[3].IsConstant();
  control.Apply(bank, signals);
  // After the messages, so a tabpoints sent before the copybuf does not undo it.
  TakeTable();
  DSP::buffer* out = nullptr;
  if (signals) {
    // Unconnected inlets read as their numbers (#1036); the phase only when
    // a phase signal is connected, so frames do not reset running phases.
    DSP::buffer& frequency = inputs[0].Signal(control.FrequencyNumber());
    DSP::buffer& magnitude = inputs[1].Signal(control.MagnitudeNumber());
    DSP::buffer& index = inputs[3].Signal(control.IndexNumber());
    out = inputs[2].IsConstant() ? &bank(frequency, magnitude, index)
                                 : &bank(frequency, magnitude, inputs[2].Signal(0.f), index);
  } else {
    out = &bank();
  }
  outputs[0].SendBuffer(out, thread);
}

#undef className
