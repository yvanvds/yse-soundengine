// `.borax` (issue #543). See mBorax.h for the design; this file is the grammar,
// the voice table and the two clocks-worth of arithmetic over it.
// No platform guard, deliberately — see the header.
#include "mBorax.h"

#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

#include <chrono>
#include <cstddef>
#include <limits>

using namespace YSE::PATCHER;

#define className mBorax

namespace {

  // The bounds of one whitespace-separated token, found in place: a substr here
  // would allocate on whichever thread the message arrived on, and that is
  // routinely the audio callback. `.poly`, `.flush`, `.sustain`, `.stripnote`
  // and `.makenote` read their lists the same way.
  bool NextToken(const std::string& text, std::size_t from, std::size_t& begin, std::size_t& end) {
    begin = from;
    while (begin < text.size() && IsSelectorSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < text.size() && !IsSelectorSeparator(text[end]))
      end++;
    return end > begin;
  }

  constexpr char kPitchInletDoc[] =
      "A pitch is measured as a note-on, or as a note-off when the stored velocity is 0. A list "
      "\"pitch velocity\" sets the velocity first. \"delta\" reports the time since the last "
      "note-on.";

  constexpr char kVelocityInletDoc[] =
      "Velocity for the pitches that follow; sends nothing. 0 means a note-off. Not clamped.";

  constexpr char kResetInletDoc[] =
      "A bang reports a note-off for every sounding note, then sets every counter back to 0.";

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // Max's right inlet takes a bang and nothing else, so nothing else is
  // registered: a number here would be a reset spelled by accident.
  ADD_IN_2;
  REG_BANG_IN(Reset);

  // Max's nine, in Max's order. Separate ports rather than one packed list
  // because a patch wires *one* of these somewhere — the polyphony count into a
  // gate, the duration into a reverb size — and packing would make every reader
  // unpack again. The two millisecond outlets are floats rather than the ints
  // Max sends: a grace note and the next note in a rolled chord differ by less
  // than a millisecond, and truncating would throw that away for nothing.
  ADD_OUT_INT; // 0 note serial
  ADD_OUT_INT; // 1 voice
  ADD_OUT_INT; // 2 polyphony
  ADD_OUT_INT; // 3 pitch
  ADD_OUT_INT; // 4 velocity
  ADD_OUT_INT; // 5 note-off count
  ADD_OUT_FLOAT; // 6 duration, ms
  ADD_OUT_INT; // 7 delta count
  ADD_OUT_FLOAT; // 8 delta, ms

  // No creation arguments: Max's borax has none, and there is nothing to
  // configure — the table is the MIDI note range and the clock is the engine's.

  ADD_DESCRIPTION(
      "Reports statistics about a stream of notes: serial number, voice, how many notes "
      "are sounding, durations and the time between attacks.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "pitch", kPitchInletDoc, "int, float, list, 'delta'");
  INLET_DOC(1, "velocity", kVelocityInletDoc, "0-127");
  INLET_DOC(2, "reset", kResetInletDoc, "bang");

  OUTLET_DOC(0, "serial",
             "The note's number, counting note-ons from 1. The same at its note-on and its "
             "note-off. Sent last.",
             "1+");
  OUTLET_DOC(1, "voice", "The voice the note took: the lowest free number, from 1.", "1-128");
  OUTLET_DOC(2, "poly", "How many notes are sounding, on every note-on and note-off.", "0-128");
  OUTLET_DOC(3, "pitch", "The pitch of the note that started or ended.", "int");
  OUTLET_DOC(4, "velocity", "The stored velocity at a note-on, 0 at a note-off.", "int");
  OUTLET_DOC(5, "notesoff", "How many notes have ended, sent at each note-off.", "1+");
  OUTLET_DOC(6, "duration", "How long the note that just ended lasted, in milliseconds.", "0+ ms");
  OUTLET_DOC(7, "deltacount", "How many delta times have been reported, sent with each one.", "1+");
  OUTLET_DOC(8, "delta",
             "Milliseconds since the previous note-on, sent at each note-on after the first and "
             "on \"delta\".",
             "0+ ms");
}

// ─── the time base ──────────────────────────────────────────────────────────

std::int64_t mBorax::NowNs() {
  // steady_clock, and not by preference: it is `.timer`'s reading and issue
  // #543's ask ("the same clock source as `.timer`"), which is also
  // `timerThread::Clock`, `INTERNAL::time`'s source and `MIDI::nowNs`'s. A wall
  // clock would be wrong twice over — not monotonic, so an NTP correction
  // mid-note would stretch, shrink or reverse a duration.
  //
  // RT-safe: a QueryPerformanceCounter on Windows and a vDSO clock_gettime on
  // Linux and Android. INTERNAL::time::update reads it from the audio callback
  // on every block already.
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

int mBorax::AsOutlet(std::uint64_t count) {
  // 2^31 notes is not a patch anybody will play, but a cast that wrapped would
  // be undefined behaviour rather than merely a wrong number.
  constexpr std::uint64_t top = (std::uint64_t)std::numeric_limits<int>::max();
  return count > top ? std::numeric_limits<int>::max() : (int)count;
}

int mBorax::PitchOfVoice(int voice) const {
  if (voice < 1 || voice > MAX_VOICES) return -1;
  const Voice& v = table[voice - 1];
  return v.active ? v.pitch : -1;
}

// ─── the outlets ────────────────────────────────────────────────────────────

void mBorax::EmitNote(int slot, int noteVelocity, YSE::THREAD thread) {
  // Right to left, and load-bearing: the serial is the value a patch keys on,
  // so everything downstream takes it on a hot inlet and it has to arrive last
  // or it would carry the previous note's figures with it. Outlets 6 and 5 are
  // the caller's — they belong to a note-off only.
  const Voice& voice = table[slot];
  outputs[4].SendInt(noteVelocity, thread);
  outputs[3].SendInt(voice.pitch, thread);
  outputs[2].SendInt(poly, thread);
  // Voices are numbered from 1, `.poly`'s numbering and the way a patch sees
  // them.
  outputs[1].SendInt(slot + 1, thread);
  outputs[0].SendInt(AsOutlet(voice.serial), thread);
}

void mBorax::EmitDelta(std::int64_t now, YSE::THREAD thread) {
  // Nothing at all before the first attack: there is no interval between one
  // event and no event, which is `.timer`'s rule and for `.timer`'s reason —
  // a 0 here would be a real reading as far as anything downstream could tell.
  if (!haveOnset) return;

  const std::int64_t ns = now - lastOnsetNs;
  // Negative cannot happen on a monotonic clock, but the subtraction is not
  // worth trusting blindly: a reader that did would report an enormous interval.
  lastDeltaMs = ns <= 0 ? 0.0 : (double)ns / 1000000.0;
  deltaCount++;

  // Right to left, so the count lands after the figure it counts.
  outputs[8].SendFloat((float)lastDeltaMs, thread);
  outputs[7].SendInt(AsOutlet(deltaCount), thread);
}

// ─── the two events ─────────────────────────────────────────────────────────

void mBorax::NoteOn(int pitch, int noteVelocity, YSE::THREAD thread) {
  // The lowest free voice — Max's "equal to the lowest available number", which
  // is `.poly`'s free-voice rule too.
  int found = -1;
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!table[i].active) {
      found = i;
      break;
    }
  }
  // 128 already sounding. Ignored entirely rather than displacing a note being
  // measured: stealing is an allocation policy, and this object sounds nothing.
  if (found < 0) return;

  const std::int64_t now = NowNs();

  noteCount++;
  Voice& voice = table[found];
  voice.pitch = pitch;
  voice.onsetNs = now;
  voice.serial = noteCount;
  voice.age = nextAge++;
  voice.active = true;
  poly++;

  // Right to left, so the delta pair goes first — and against the *same*
  // reading the onset above was taken from, so a note's own attack instant and
  // the interval leading up to it are one instant rather than two.
  EmitDelta(now, thread);
  EmitNote(found, noteVelocity, thread);

  // Only after the delta has been measured against the previous attack.
  lastOnsetNs = now;
  haveOnset = true;
}

void mBorax::NoteOff(int pitch, YSE::THREAD thread) {
  // Oldest first, so a pitch played twice is closed off in the order it was
  // played and a balanced source stays balanced — `.poly`'s pairing.
  int found = -1;
  std::uint64_t bestAge = std::numeric_limits<std::uint64_t>::max();
  for (int i = 0; i < MAX_VOICES; i++) {
    if (table[i].active && table[i].pitch == pitch && table[i].age < bestAge) {
      bestAge = table[i].age;
      found = i;
    }
  }
  // No voice is holding it: it was never played, or it was ignored by a full
  // table, or a reset already closed it. There is no attack to measure from, and
  // a duration from nothing would be worse than none.
  if (found < 0) return;

  const std::int64_t ns = NowNs() - table[found].onsetNs;
  lastDurationMs = ns <= 0 ? 0.0 : (double)ns / 1000000.0;

  // Freed before anything is sent, so an outlet wired somewhere that reads this
  // object back sees a table already emptied of what is on its way out —
  // `.flush`'s and `.poly`'s rule, for their reason.
  table[found].active = false;
  poly--;
  noteOffCount++;

  // Right to left. The duration pair belongs to a note-off alone, so it is here
  // rather than in EmitNote.
  outputs[6].SendFloat((float)lastDurationMs, thread);
  outputs[5].SendInt(AsOutlet(noteOffCount), thread);
  EmitNote(found, 0, thread);
}

void mBorax::Play(int pitch, YSE::THREAD thread) {
  if (!Enter()) return;

  // A velocity of 0 is a release — how the whole MIDI world spells one, and how
  // every note source in the patcher reports one. Anything else is an attack.
  const int noteVelocity = Velocity();
  if (noteVelocity == 0) {
    NoteOff(pitch, thread);
  } else {
    NoteOn(pitch, noteVelocity, thread);
  }

  Leave();
}

void mBorax::ResetAll(YSE::THREAD thread) {
  if (!Enter()) return;

  // Every voice is freed first and only then reported, in one bounded pass over
  // a stack array: a send runs the whole subgraph behind the outlets, and a walk
  // that emitted as it went could meet a voice its own output had started.
  const std::int64_t now = NowNs();
  int taken[MAX_VOICES];
  int count = 0;

  for (int i = 0; i < MAX_VOICES; i++) {
    if (!table[i].active) continue;
    table[i].active = false;
    taken[count] = i;
    count++;
  }

  // Ascending voice order — the order the table walks in, deterministic rather
  // than the accident of arrival. Full note-off reports: the durations are the
  // one thing this object measured that nothing else could reconstruct, so a
  // reset hands them over rather than dropping them. The polyphony count is
  // decremented here rather than in the pass above so it *drains* across the
  // reports, exactly as it would had the note-offs arrived one at a time; a
  // patch watching that outlet would otherwise be told 0 while two more
  // releases were still on their way.
  for (int n = 0; n < count; n++) {
    const Voice& voice = table[taken[n]];
    const std::int64_t ns = now - voice.onsetNs;
    lastDurationMs = ns <= 0 ? 0.0 : (double)ns / 1000000.0;
    noteOffCount++;
    poly--;
    outputs[6].SendFloat((float)lastDurationMs, thread);
    outputs[5].SendInt(AsOutlet(noteOffCount), thread);
    EmitNote(taken[n], 0, thread);
  }

  // Max: "erasing the borax object's memory of all notes received, and setting
  // its counters and its clock to 0". After the reports, so the note-offs above
  // carry the numbers the notes really had.
  noteCount = 0;
  noteOffCount = 0;
  deltaCount = 0;
  poly = 0;
  nextAge = 1;
  lastOnsetNs = 0;
  haveOnset = false;
  lastDurationMs = 0.0;
  lastDeltaMs = 0.0;

  Leave();
}

bool mBorax::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    // Another thread is mid-message, or a patch has wired an outlet back into
    // an inlet. Counted rather than spun on: this is a path the audio callback
    // takes, and neither the table nor the counters are re-entrant.
    dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

void mBorax::Leave() {
  busy.store(false, std::memory_order_release);
}

// ─── the grammar ────────────────────────────────────────────────────────────

BANG_IN(Reset) {
  (void)inlet;
  ResetAll(thread);
}

INT_IN(IntIn) {
  if (inlet == 0) {
    Play(value, thread);
    return;
  }
  // Max's middle inlet: the velocity for pitches arriving after it. Stored as
  // given — see the header on why nothing is clamped here.
  velocity = value;
}

FLOAT_IN(FloatIn) {
  // Max converts a float to an int in both inlets. ExprToInt rather than a cast:
  // a value outside the int range is undefined behaviour to cast.
  if (inlet == 0) {
    Play(ExprToInt(value), thread);
    return;
  }
  velocity = ExprToInt(value);
}

LIST_IN(ListIn) {
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(value, 0, begin, end)) return;
  const std::size_t length = end - begin;

  if (inlet != 0) {
    float number = 0.f;
    if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
    velocity = ExprToInt(number);
    return;
  }

  // Max's only left-inlet command, and a word rather than a pitch, so it cannot
  // be played. There is no `clear` and no `stop` here: Max's borax has neither,
  // and the right inlet's bang is the reset.
  if (length == 5 && value.compare(begin, length, "delta", 5) == 0) {
    if (!Enter()) return;
    EmitDelta(NowNs(), thread);
    Leave();
    return;
  }

  float pitch = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, length, pitch)) return;

  // Max's list distributes across the inlets right to left, so a second element
  // is the velocity and it is stored *before* the pitch is read against it —
  // exactly as if it had arrived at the middle inlet first. That is what makes
  // one cord from `.midiparse`'s note outlet work, and it is the only way the
  // very first note can be measured correctly. Further elements are ignored.
  std::size_t secondBegin = 0;
  std::size_t secondEnd = 0;
  if (NextToken(value, end, secondBegin, secondEnd)) {
    float second = 0.f;
    if (ReadNumericToken(value.c_str() + secondBegin, secondEnd - secondBegin, second)) {
      velocity = ExprToInt(second);
    }
  }

  Play(ExprToInt(pitch), thread);
}

#undef className
