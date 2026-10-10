#include "headers/defines.hpp"
// See the matching guard in mMidiInfo.h.
#if YSE_ENABLE_MIDI_DEVICE
#include "mMidiInfo.h"
#include "../pObjectList.hpp"
#include "midiPortScanner.h"

#include <memory>
#include <thread>

using namespace YSE::PATCHER;
#define className mMidiInfo

namespace {

  // How many characters of a snapshot slot are the name. `strnlen` is POSIX
  // rather than standard C++, and the slots are written by the scanner alone,
  // so the two-line scan is both portable and enough.
  std::size_t NameLength(const char* name, std::size_t cap) {
    std::size_t length = 0;
    while (length < cap && name[length] != '\0')
      length++;
    return length;
  }

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Report);
  REG_LIST_IN(SetDirection);

  ADD_OUT_LIST; // 0 — name
  ADD_OUT_INT; // 1 — index
  ADD_OUT_INT; // 2 — count

  ADD_PARAM(directionArg);
  REG_PARM_PARSE;

  scratch.reserve(NAME_CAPACITY);

  // Taken for the object's whole life, here rather than in SetParent: a slot
  // costs nothing until something is asked of it, and claiming it once means
  // `refresh` never has to allocate one on a path that may be the audio
  // callback. A full table leaves `scan` at 0, which `refresh` accepts and
  // quietly does nothing about.
  scan = MidiPortScanner().Claim();

  ADD_DESCRIPTION("Lists the machine's MIDI ports. A bang sends the number of ports, then an index "
                  "and a name for each, so a patch can find a device by name.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "bang",
            "A bang sends the listing. \"input\" and \"output\" switch which ports are listed; "
            "\"refresh\" scans the machine again and sends the listing when the scan is done.",
            "");
  OUTLET_DOC(0, "name",
             "The port's name, spaces included, cut at 63 characters. Sent after its index.", "");
  OUTLET_DOC(1, "index",
             "The port's index: the port argument .midiout, .notein and the other MIDI objects "
             "take. Counted from 0.",
             "0-31");
  OUTLET_DOC(2, "count", "How many ports there are. Sent first; 0 when there are none.", "0-32");
  PARAM_DOC("direction", "input",
            "Which ports to list: \"input\" or \"output\". Anything else means \"input\".",
            "input | output");
}

mMidiInfo::~mMidiInfo() {
  // Hands the slot back without joining: the #227 epoch reclaimer frees retired
  // patcher objects on the background pool itself, so a destructor that joined
  // its own job could spin on the very worker running it. `Release` waits out a
  // scan mid-write instead, which is bounded by one enumeration — see
  // midiPortScanner's class notes.
  MidiPortScanner().Release(scan);
}

PARM_PARSE() {
  direction = (directionArg == "output") ? DIR_OUTPUT : DIR_INPUT;
}

void mMidiInfo::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // A standalone object — a unit-test rig, or one not yet added to a patcher —
  // deliberately asks the platform nothing: enumeration is not free, and an
  // object outside a graph has nobody to report to.
  if (newParent == nullptr) return;

  // On the heap rather than the stack: the table is a few kilobytes and this
  // runs on the control thread, where an allocation costs nothing. Scanned
  // synchronously rather than through the background pool: blocking is free
  // here, and deferring the *first* snapshot would leave a freshly loaded patch
  // reporting an empty machine until its next block.
  auto fresh = std::make_unique<midiPortSnapshot>();
  midiPortScanner::ScanNow(*fresh);
  Publish(*fresh);
}

void mMidiInfo::Publish(const midiPortSnapshot& from) {
  // Waits rather than giving up. This is the control thread — it may block —
  // and the only thing it can be waiting for is a report walk of at most 32
  // outlet sends, which no patch can hold open.
  while (!Enter()) {
    std::this_thread::yield();
  }
  ports = from;
  Leave();
}

bool mMidiInfo::Enter() {
  bool expected = false;
  return busy.compare_exchange_strong(expected, true, std::memory_order_acquire,
                                      std::memory_order_relaxed);
}

void mMidiInfo::Leave() {
  busy.store(false, std::memory_order_release);
}

int mMidiInfo::PortCount() const {
  return ports.count[Direction()];
}

std::string mMidiInfo::PortName(int index) const {
  const int dir = Direction();
  if (index < 0 || index >= ports.count[dir]) return std::string();
  const char* name = ports.names[dir][index];
  return std::string(name, NameLength(name, NAME_CAPACITY - 1));
}

void mMidiInfo::Emit(YSE::THREAD thread) {
  const int dir = Direction();
  const int count = ports.count[dir];

  outputs[2].SendInt(count, thread);
  for (int i = 0; i < count; i++) {
    outputs[1].SendInt(i, thread);
    const char* name = ports.names[dir][i];
    scratch.assign(name, NameLength(name, NAME_CAPACITY - 1));
    outputs[0].SendList(scratch, thread);
  }
}

BANG_IN(Report) {
  if (!Enter()) {
    // Another thread is mid-report, or this bang came back around from our own
    // name outlet. Counted rather than logged: this may be the audio callback.
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  Emit(thread);
  Leave();
}

CALC() {
  // Audio thread, from the top of the block. Its only job is to collect a
  // rescan that finished on the background pool (issue #757) — a result with no
  // cord to arrive on — and re-send the listing.
  if (scan == 0) return;

  if (!Enter()) {
    // A report is in flight on another thread. Not counted as a drop: nothing
    // was refused, the snapshot is still waiting in the scanner's slot and the
    // next block collects it.
    return;
  }

  // Wait-free: a state load, and — when a scan has landed — one assignment of a
  // fixed-size struct into storage that already exists. No allocation, no lock.
  if (MidiPortScanner().Consume(scan, ports)) {
    refreshed.fetch_add(1, std::memory_order_relaxed);
    Emit(thread);
  }

  Leave();
}

void mMidiInfo::Command(const std::string& text) {
  if (text == "input") {
    direction = DIR_INPUT;
  } else if (text == "output") {
    direction = DIR_OUTPUT;
  } else if (text == "refresh") {
    // One CAS and one lock-free push. Whatever thread dispatched this — and it
    // is routinely the audio callback — nothing here allocates, locks or
    // blocks; the enumeration itself happens on the background pool.
    MidiPortScanner().Request(scan);
  }
}

LIST_IN(SetDirection) {
  Command(value);
}

MESSAGES() {
  Command(message);
}

#endif
