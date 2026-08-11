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
      "The pitch, and the half of the pair that acts. An int or a float here is paired with the "
      "stored velocity and measured: a non-zero velocity takes the lowest free voice, gives the "
      "note the next serial number and reports it, while a velocity of 0 closes off the voice "
      "holding that pitch and reports how long it lasted — 0 being how the whole MIDI world spells "
      "a release. A float pitch is truncated to an int, as it is in Max. A list is Max's inlet "
      "distribution written on one cord: the first element is the pitch and a second element sets "
      "the velocity first, so '60 100' measures pitch 60 at velocity 100 and '60 0' ends it — "
      "which "
      "is exactly the shape '.midiparse''s note outlet sends. Further elements are ignored. A "
      "single-token numeric list is the number it spells, so a '.m 60' reaches this inlet as a "
      "pitch. The message 'delta' sends the time since the last note-on out the two delta outlets "
      "without touching anything else, which is how a patch asks how long it has been quiet. A "
      "release for a pitch no voice is holding, and a note-on with all 128 voices taken, are both "
      "ignored silently — there is nothing true to report about either. Any pitch at all can be "
      "measured, the table holding pitches rather than being a bitmap over the MIDI note range. "
      "There is no bang method here: Max's bang is the right inlet's, and it resets.";

  constexpr char kVelocityInletDoc[] =
      "Sets the velocity that pitches arriving *after* it will be paired with — Max's middle "
      "inlet. "
      "It outputs nothing by itself: only a pitch completes a note, so only a pitch can start a "
      "measurement or end one. Ints, floats and a list whose leading token is a number all set it; "
      "a float is truncated. The value is stored and reported exactly as given, unclamped and "
      "never "
      "refused for being outside MIDI's 0-127 — this object measures notes, it does not rewrite "
      "them, and 0 is the only value it reads meaning into, that being what a release is spelled "
      "with.";

  constexpr char kResetInletDoc[] =
      "A bang resets — Max's only bang, and his wording: 'resets borax by sending note-offs for "
      "all "
      "notes currently being held, erasing the borax object's memory of all notes received, and "
      "setting its counters and its clock to 0'. The note-offs are full note-off reports, duration "
      "and note-off count included, and go out in ascending voice order before the counters are "
      "zeroed: the durations are the one thing this object measured that nothing else could "
      "reconstruct, so a reset hands them over rather than dropping them. Afterwards the note "
      "serial, the note-off count and the delta count all start again from zero, no voice is "
      "sounding, and the next note-on reports no delta — there being no earlier attack left to "
      "measure from. A reset with nothing sounding sends nothing but still zeroes the counters. "
      "The "
      "inlet takes nothing but a bang; a number here is neither a pitch nor a velocity.";

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
      "Reports note-on and note-off statistics over a stream of pitch/velocity pairs — Max's "
      "'borax', his 'swiss army knife for music analysis'. Nine outlets report what the stream is "
      "doing: the serial number of each note, the voice it took, how many notes are sounding, the "
      "pitch and velocity, how many notes have completed, how long the one that just ended lasted "
      "in milliseconds, how many delta times have been reported and how far apart successive "
      "attacks are. This is the input side of adaptive musical behaviour and the only object in "
      "the "
      "patcher that answers those questions: everything else around a note either makes one "
      "('.makenote', '.noteon'), routes one ('.poly'), filters one ('.stripnote') or cleans up "
      "after one ('.flush', '.sustain', '.midiflush'), so a patch that wants to play denser when "
      "the performer plays faster, or pick a reverb size from how long a phrase's notes are, had "
      "no "
      "way to ask before this. Where '.timer' measures one interval between two bangs, this "
      "measures every note in a polyphonic stream at once, each against its own attack. It reports "
      "rather than holds: the notes it knows about are sounding somewhere else, and it is wired "
      "alongside that rather than in the path. The leftmost outlet is the note's own serial and "
      "not "
      "a running total — the same number that came out at the attack comes out again at the "
      "release, which is what lets something that saw both recognise them as one event and is why "
      "a duration is worth reporting beside it. Voices are the lowest free number, from 1, which "
      "is "
      "'.poly''s numbering so that a '.borax' watching a stream a '.poly' also allocates does not "
      "report different numbers for the same notes; a repeated pitch takes a second voice and the "
      "two release oldest-first, so a balanced source stays balanced. It never steals and has no "
      "overflow outlet — stealing exists so a sounding voice can be reused and this object sounds "
      "nothing — so a note-on with all 128 voices taken is ignored, as is a note-off for a pitch "
      "no "
      "voice is holding: both silently, the thread being routinely the audio callback where a "
      "warning would be an allocation. The outlets fire right to left, Max's order, which here is "
      "not cosmetic: the serial is the value a patch keys on, so it has to reach a hot inlet last "
      "or it would carry the previous note's figures with it. Not every outlet fires on every "
      "message, and that selectivity is Max's and is information — the duration and note-off count "
      "only on a note-off, the two delta outlets only on a note-on that had a predecessor or on "
      "the "
      "'delta' message, which takes a reading on demand and emits nothing before the first "
      "note-on. "
      "The milliseconds are measured, never counted: the clock is std::chrono::steady_clock, which "
      "is '.timer''s reading, the patcher timer thread's scheduling clock and the engine's own "
      "time source, so a duration never disagrees with a '.timer' beside it; it is deliberately "
      "not "
      "the deferred-message scheduler '.makenote' arms releases on, that being the patcher's block "
      "counter and right for scheduling but a block coarse for measuring. A bang in the right "
      "inlet "
      "resets, Max's wording: it sends note-offs for everything held — full reports, durations "
      "included — then zeroes the counters and the clock. There is no teardown release (issue "
      "#758) and deliberately so: '.flush', '.poly' and '.makenote' override it because each is "
      "the "
      "only thing that knows about a sounding note, while this object knows about no note "
      "something "
      "else does not, and a burst of duplicate note-off reports timing how long the patcher took "
      "to "
      "die would be a worse answer than silence. Like '.poly' and '.flush' it opens no device and "
      "needs none, so it works on every platform. The table is 128 entries allocated with the "
      "object, no path allocates, locks or blocks on the audio thread — every one is a bounded "
      "walk "
      "plus one clock read, and no outlet carries text — and Calculate() does nothing.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "pitch", kPitchInletDoc, "int, float, list, 'delta'");
  INLET_DOC(1, "velocity", kVelocityInletDoc, "0-127");
  INLET_DOC(2, "reset", kResetInletDoc, "bang");

  OUTLET_DOC(0, "serial",
             "The number this note was given, counting note-ons from 1 since the last reset. Max's "
             "leftmost outlet, and not a running total: the same number goes out at the attack and "
             "again at the release, which is what lets something that saw both recognise them as "
             "one event and pair a duration with the note it belongs to. Sent last, after every "
             "other outlet, Max's outlets firing right to left — this is the value a patch keys "
             "on, so everything downstream takes it on a hot inlet.",
             "1+");
  OUTLET_DOC(1, "voice",
             "The voice this note took: the lowest number not already in use, from 1. A voice "
             "comes free when the note holding it is released, so a patch with more voices than it "
             "uses always sees the low ones. The same number comes out again at the release. "
             "Numbered from 1 like '.poly''s, so the two objects watching one stream agree.",
             "1-128");
  OUTLET_DOC(
      2, "poly",
      "How many notes are sounding right now, sent on every note-on and every note-off — "
      "the figure this object mostly exists to make a patch able to ask for. It counts up on "
      "an attack and down on a release, so a patch reads 0 as 'the passage has ended' and "
      "reads a rising number as a thickening texture.",
      "0-128");
  OUTLET_DOC(3, "pitch",
             "The pitch of the note being started or ended. The same value on the way in and on "
             "the way out, so a release is identifiable as belonging to its attack. Any int at "
             "all — the table holds pitches rather than being a bitmap over the MIDI range.",
             "int");
  OUTLET_DOC(4, "velocity",
             "The velocity: the stored one when a note starts, and 0 for every release, including "
             "the ones a reset sends. Reported exactly as it was given, unclamped.",
             "int");
  OUTLET_DOC(5, "notesoff",
             "How many notes have completed since the last reset, sent only when one does — Max's "
             "sixth outlet, the count that accompanies each duration. Together with the note "
             "serial it says how much of what was played has finished.",
             "1+");
  OUTLET_DOC(6, "duration",
             "How long the note that just ended lasted, in milliseconds, sent only on a note-off "
             "— and on the note-offs a reset sends. Measured rather than counted: the attack and "
             "the release each read the engine's monotonic clock (std::chrono::steady_clock, the "
             "same source '.timer' reads and the patcher's timer thread schedules on) and the "
             "figure is their difference, so a message the OS delivered late is timed at the "
             "instant it really arrived. A float rather than Max's int, because a grace note and "
             "the next note of a rolled chord can differ by less than a millisecond. Sent before "
             "the note-off count, Max's outlets firing right to left.",
             "0+ ms");
  OUTLET_DOC(7, "deltacount",
             "How many delta times have been reported since the last reset — Max's eighth outlet. "
             "Both a note-on that had a predecessor and the 'delta' message add one, and it is "
             "sent whenever a delta is. It stays silent when the delta outlet does, so a patch "
             "reads a repeat of the previous number as 'no new attack', never as a new one.",
             "1+");
  OUTLET_DOC(8, "delta",
             "The time between successive attacks, in milliseconds — how fast the passage is "
             "being played. Sent on every note-on except the first, there being no earlier attack "
             "to measure from, and on the 'delta' message, which takes the reading on demand (the "
             "time since the last note-on, right now) and is how a patch asks how long it has been "
             "quiet. Same clock, same float, and the same reasons as the duration outlet. Sent "
             "first of all, Max's outlets firing right to left.",
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
