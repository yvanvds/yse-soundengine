#include "dCycle.h"
#include "../io/fileScheduler.h"
#include "../pSelector.h"
#include <cstdint>
#include <cstring>
#include <mutex>
#include <type_traits>
#include <unordered_map>

using namespace YSE::PATCHER;

#define className dCycle

// One binding of the table: a reader, whether a name was given, and the
// offset and size a `set` / `setall` brings with it. Made on a control or
// background thread, read by CALC() while it is current, freed off the audio
// thread.
struct dCycle::table {
  DSP::sampleBufferReader reader;
  bool active = false; // a buffer name, not the built-in cosine
  bool setOffset = false;
  bool setSize = false;
  int offset = 0;
  int size = 0;
};

// The handoff between a rebind task and CALC() (issue #1192). Shared by the
// object and the tasks it sends, which find it by id, so whichever lets go
// last frees it, and it frees every table it still holds.
//
//   task  --incoming-->  CALC()  --retired-->  task
//
// `current` is CALC()'s alone once the object is published. `incoming` is
// written by tasks (an unread table is replaced, and freed by the task) and
// taken by CALC() with one exchange. `retired` holds the table CALC() let
// go; CALC() fills it only while it is empty and any task empties it, so the
// audio thread never frees a table and never waits for one.
struct dCycle::mailbox {
  explicit mailbox(std::uint64_t id) : id(id) {}
  ~mailbox();
  mailbox(const mailbox&) = delete;
  mailbox& operator=(const mailbox&) = delete;
  mailbox(mailbox&&) = delete;
  mailbox& operator=(mailbox&&) = delete;

  static std::shared_ptr<mailbox> Make();
  static std::shared_ptr<mailbox> Find(std::uint64_t id);

  const std::uint64_t id;
  table* current = nullptr;
  std::atomic<table*> incoming{nullptr};
  std::atomic<table*> retired{nullptr};
  // A release task is queued or running. Set by CALC() before it asks, so the
  // task's clear always comes after it.
  std::atomic<bool> releaseQueued{false};
};

namespace {
  // The next whitespace-separated word of `message` from `pos`, as
  // [begin, end). Walked in place: no allocation on whichever thread sends.
  void NextWord(const std::string& message, std::size_t pos, std::size_t& begin, std::size_t& end) {
    const std::size_t length = message.size();
    begin = pos;
    while (begin < length && IsSelectorSeparator(message[begin]))
      begin++;
    end = begin;
    while (end < length && !IsSelectorSeparator(message[end]))
      end++;
  }

  bool IsWord(const std::string& message, std::size_t begin, std::size_t end, const char* word,
              std::size_t length) {
    return end - begin == length && message.compare(begin, length, word) == 0;
  }

  enum class REBIND_OP : std::uint8_t {
    BIND, // set / setall / buffer
    RELEASE, // free the table CALC() let go
  };

  // The process-wide mailboxes, by id. Weak: a mailbox lives while its object
  // or a task holds it. Control and background threads only (it locks).
  struct mailboxTable {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::weak_ptr<dCycle::mailbox>> boxes;
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

std::shared_ptr<dCycle::mailbox> dCycle::mailbox::Make() {
  mailboxTable& t = Mailboxes();
  const std::lock_guard<std::mutex> lock(t.mutex);
  std::shared_ptr<mailbox> box = std::make_shared<mailbox>(t.next++);
  t.boxes[box->id] = box;
  return box;
}

std::shared_ptr<dCycle::mailbox> dCycle::mailbox::Find(std::uint64_t id) {
  mailboxTable& t = Mailboxes();
  const std::lock_guard<std::mutex> lock(t.mutex);
  auto it = t.boxes.find(id);
  return it == t.boxes.end() ? nullptr : it->second.lock();
}

// Off the audio thread: the last holder is the object's destructor or a task.
dCycle::mailbox::~mailbox() {
  delete current;
  delete incoming.load(std::memory_order_acquire);
  delete retired.load(std::memory_order_acquire);
  mailboxTable& t = Mailboxes();
  const std::lock_guard<std::mutex> lock(t.mutex);
  t.boxes.erase(id);
}

// The record a rebind or a release becomes. Copied whole into a scheduler
// slot, so it stays trivially copyable.
struct dCycle::rebindTask {
  std::uint64_t box = 0;
  REBIND_OP op = REBIND_OP::BIND;
  std::uint8_t setOffset = 0;
  std::uint8_t setSize = 0;
  std::int32_t offset = 0;
  std::int32_t size = 0;
  char name[fileScheduler::PATH_CAPACITY] = {}; // empty: the built-in cosine
};

CONSTRUCT_DSP() {
  frequency = 0.f;
  phase = 0.f;
  pendingReset = 0.f;

  // Control thread: the mailbox and the first (cosine) table exist before
  // the object can be published.
  box = mailbox::Make();
  box->current = new table(); // NOLINT(cppcoreguidelines-owning-memory): the mailbox owns it

  // in 0: frequency (signal, float or int), and the messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFrequency);
  REG_INT_IN(SetFrequencyInt);

  // in 1: phase (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetPhase);
  REG_INT_IN(SetPhaseInt);

  // out 0: the waveform
  ADD_OUT_BUFFER;

  REG_PARM_PARSE;
  ADD_PARAM(frequency);
  ADD_PARAM(bufferName);
  ADD_PARAM(offset);
  ADD_PARAM(size);

  ADD_DESCRIPTION("A wavetable oscillator: a cosine by default, or one cycle read from a named "
                  "sample buffer. The phase inlet is added to the running phase, for phase "
                  "modulation.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "freq",
            "Frequency in Hz: a signal, or a number used while no signal is connected. Also "
            "takes 'reset [phase]', 'frequency <hz>', 'phase <0-1>', 'buffer_offset <n>', "
            "'buffer_sizeinsamps <n>', and 'set [name] [offset]', 'setall <name>' and "
            "'buffer <name>' to read another buffer (in the background, from a later block; "
            "not saved with the patch).",
            "any float");
  INLET_DOC(1, "phase",
            "Phase in cycles added to the running phase: a signal for phase modulation, or a "
            "number used while no signal is connected.",
            "0-1");
  OUTLET_DOC(0, "out", "The waveform.", "-1 to 1");
  PARAM_DOC("frequency", "0", "Frequency in Hz used while no signal is connected.", "any float");
  PARAM_DOC("buffer", "",
            "Name of a sample buffer whose first channel is the waveform. None uses the built-in "
            "cosine. A set, setall or buffer message does not change what is saved.",
            "buffer name");
  PARAM_DOC("offset", "0", "The first sample of the buffer the waveform starts at.", "0 or higher");
  PARAM_DOC("size", "0",
            "How many buffer samples make up the waveform: 0 is 512, -1 the rest of the buffer.",
            "-1, 0 or higher");
}

void dCycle::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler the rebinding messages go through, built here so a message
  // on the audio thread finds it.
  EnableFileIO();
}

// Control thread: the buffer name is a string parameter, so a change of
// argument rebuilds the object and this runs on one no audio thread has seen.
PARM_PARSE() {
  const bool active = !bufferName.empty();
  box->current->active = active;
  box->current->reader.bind(active ? DSP::sampleBufferRegistry::acquire(bufferName) : nullptr);
  usesBuffer.store(active, std::memory_order_release);
}

const std::shared_ptr<YSE::DSP::sampleBuffer>& dCycle::Buffer() const {
  return box->current->reader.buffer();
}

bool dCycle::ReleasePending() const {
  return box->retired.load(std::memory_order_acquire) != nullptr;
}

FLOAT_IN(SetFrequency) {
  frequency = value;
}

INT_IN(SetFrequencyInt) {
  frequency = static_cast<float>(value);
}

FLOAT_IN(SetPhase) {
  phase = value;
}

INT_IN(SetPhaseInt) {
  phase = static_cast<float>(value);
}

bool dCycle::Submit(rebindTask& task) {
  fileScheduler* io = FileIO();
  if (io == nullptr) return false;
  task.box = box->id;
  return io->RequestTask(this, 0, &dCycle::RunTask, &task, sizeof(task));
}

MESSAGES() {
  (void)value;
  std::size_t begin = 0;
  std::size_t end = 0;
  NextWord(message, 0, begin, end);

  std::size_t argBegin = 0;
  std::size_t argEnd = 0;
  NextWord(message, end, argBegin, argEnd);
  float number = 0.f;
  const bool hasNumber = ReadNumericToken(message.c_str() + argBegin, argEnd - argBegin, number);

  const bool isSet = IsWord(message, begin, end, "set", 3);
  const bool isSetAll = IsWord(message, begin, end, "setall", 6);
  const bool isBuffer = IsWord(message, begin, end, "buffer", 6);
  if (isSet || isSetAll || isBuffer) {
    // The record lives on the stack and the scheduler copies it into a slot
    // it already owns: no allocation on the sending thread.
    rebindTask task;
    const std::size_t nameLength = argEnd - argBegin;
    if (nameLength >= sizeof(task.name)) return;
    if (nameLength == 0 && !isSet) return; // only `set` may go back to the cosine
    std::memcpy(task.name, message.data() + argBegin, nameLength);
    task.name[nameLength] = '\0';
    if (isSet) {
      std::size_t offBegin = 0;
      std::size_t offEnd = 0;
      NextWord(message, argEnd, offBegin, offEnd);
      float at = 0.f;
      task.setOffset = 1;
      task.offset = ReadNumericToken(message.c_str() + offBegin, offEnd - offBegin, at) && at > 0.f
                        ? static_cast<std::int32_t>(at)
                        : 0;
    } else if (isSetAll) {
      task.setOffset = 1;
      task.offset = 0;
      task.setSize = 1;
      task.size = -1;
    }
    Submit(task);
    return;
  }

  if (IsWord(message, begin, end, "reset", 5)) {
    pendingReset = hasNumber ? number : 0.f;
    resetPending = true;
    return;
  }
  if (!hasNumber) return;
  if (IsWord(message, begin, end, "frequency", 9)) {
    frequency = number;
  } else if (IsWord(message, begin, end, "phase", 5)) {
    phase = number;
  } else if (IsWord(message, begin, end, "buffer_offset", 13)) {
    if (number >= 0.f) offset = static_cast<int>(number);
  } else if (IsWord(message, begin, end, "buffer_sizeinsamps", 18)) {
    size = static_cast<int>(number);
  }
}

// ─── the background half ──────────────────────────────────────────────────────

bool dCycle::RunTask(char* args, std::size_t argBytes) {
  static_assert(std::is_trivially_copyable<rebindTask>::value,
                "a rebindTask is copied into a scheduler slot with memcpy");
  if (argBytes != sizeof(rebindTask)) return false;
  rebindTask task;
  std::memcpy(&task, args, sizeof(task));

  // By id, not through the object: it may have been deleted since.
  std::shared_ptr<mailbox> found = mailbox::Find(task.box);
  if (found == nullptr) return false;

  // Whatever CALC() let go, whichever kind of task this is. The flag first:
  // a table CALC() retires after the exchange below asks again.
  found->releaseQueued.store(false, std::memory_order_release);
  delete found->retired.exchange(nullptr, std::memory_order_acq_rel);
  if (task.op == REBIND_OP::RELEASE) return true;

  std::unique_ptr<table> next = std::make_unique<table>();
  next->active = task.name[0] != '\0';
  if (next->active) next->reader.bind(DSP::sampleBufferRegistry::acquire(task.name));
  next->setOffset = task.setOffset != 0;
  next->offset = task.offset;
  next->setSize = task.setSize != 0;
  next->size = task.size;

  // A table CALC() has not taken yet is replaced: the last rebind wins, and
  // keeps an offset or size the replaced one would have brought. Taken out
  // first and merged while CALC() cannot see `next`; the publishing exchange
  // below makes every field visible to CALC()'s acquire.
  std::unique_ptr<table> unread(found->incoming.exchange(nullptr, std::memory_order_acq_rel));
  if (unread != nullptr) {
    if (!next->setOffset && unread->setOffset) {
      next->setOffset = true;
      next->offset = unread->offset;
    }
    if (!next->setSize && unread->setSize) {
      next->setSize = true;
      next->size = unread->size;
    }
  }
  // Tasks run one at a time, so nothing else fills `incoming` meanwhile; a
  // table found there anyway is superseded like the one above.
  delete found->incoming.exchange(next.release(), std::memory_order_acq_rel);
  return true;
}

// Audio thread, top of CALC(): wait-free. Takes a waiting table only while
// the retired slot is empty, so the old one always has somewhere to go.
void dCycle::TakeRebind() {
  mailbox& b = *box;
  if (b.retired.load(std::memory_order_acquire) == nullptr &&
      b.incoming.load(std::memory_order_relaxed) != nullptr) {
    table* next = b.incoming.exchange(nullptr, std::memory_order_acq_rel);
    if (next != nullptr) {
      b.retired.store(b.current, std::memory_order_release);
      b.current = next;
      if (next->setOffset) offset.store(next->offset);
      if (next->setSize) size.store(next->size);
      usesBuffer.store(next->active, std::memory_order_release);
      // A buffer table is set again every block below; the cosine is not.
      if (!next->active) oscillator.cosine();
    }
  }
  // Ask for the retired table to be freed. A refused request (a full table,
  // or no patcher) is asked again next block; any rebind task frees it too.
  if (b.retired.load(std::memory_order_acquire) != nullptr &&
      !b.releaseQueued.exchange(true, std::memory_order_acq_rel)) {
    rebindTask release;
    release.op = REBIND_OP::RELEASE;
    if (!Submit(release)) b.releaseQueued.store(false, std::memory_order_release);
  }
}

CALC() {
  if (resetPending.exchange(false)) oscillator.reset(pendingReset);
  TakeRebind();

  // Pinned once for the block; the table points into the view, which lives
  // until the end of CALC().
  const table& current = *box->current;
  DSP::sampleView view = current.reader.pin();
  if (current.active) {
    const int first = offset.load();
    oscillator.table(view, first > 0 ? static_cast<UInt>(first) : 0u, size.load());
  }

  // A connected signal wins over the number (issue #1036).
  const bool freqSignal = !inputs[0].IsConstant();
  const bool phaseSignal = !inputs[1].IsConstant();
  DSP::buffer* out = nullptr;
  if (freqSignal && phaseSignal)
    out = &oscillator(inputs[0].Signal(frequency), inputs[1].Signal(phase));
  else if (freqSignal)
    out = &oscillator(inputs[0].Signal(frequency), phase);
  else if (phaseSignal)
    out = &oscillator(frequency, inputs[1].Signal(phase));
  else
    out = &oscillator(frequency, phase);
  outputs[0].SendBuffer(out, thread);
}

#undef className
