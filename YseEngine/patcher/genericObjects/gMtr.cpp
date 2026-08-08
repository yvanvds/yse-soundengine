#include "gMtr.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include <cstddef>
#include <cstdint>
#include <string>

using namespace YSE::PATCHER;

#define className gMtr

namespace {

  // A recorded whole number only leaves as an int when the int can hold it:
  // casting a float outside the int range is undefined behaviour, and a patch
  // that sent a huge integer is better served by the float that still carries
  // its value. `.route`, `.coll`, `.textfile` and `.qlist` decide the same
  // question the same way.
  bool FitsInt(float value) {
    return value >= -2147483648.f && value < 2147483648.f;
  }

  // Absolute time is a running sum of deltas, so a long tape can reach the top
  // of the int range. Saturate rather than wrap: a clock that went negative
  // would be worse than one that stopped.
  int SatAdd(int a, int b) {
    const std::int64_t sum = (std::int64_t)a + (std::int64_t)b;
    if (sum > 2147483647LL) return 2147483647;
    if (sum < -2147483648LL) return -2147483647 - 1;
    return (int)sum;
  }

  // The bounds of the token starting at or after `from`, or false when there is
  // none. Walked in place rather than through substr: this runs on whichever
  // thread the message arrived on.
  bool NextToken(const char* text, std::size_t length, std::size_t from, std::size_t& begin,
                 std::size_t& end) {
    begin = from;
    while (begin < length && IsSelectorSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < length && !IsSelectorSeparator(text[end]))
      end++;
    return end > begin;
  }

  // Trim the separators off both ends of [begin, end).
  void Trim(const char* text, std::size_t& begin, std::size_t& end) {
    while (begin < end && IsSelectorSeparator(text[begin]))
      begin++;
    while (end > begin && IsSelectorSeparator(text[end - 1]))
      end--;
  }

  // Whether the `length` characters at `text` are exactly `word`. Compared in
  // place rather than through a std::string, since this runs on whichever thread
  // the message arrived on.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the construction-time clamp log).
  std::string IntText(int value) {
    char digits[YSE::PATCHER::FORMAT_INT_WIDTH];
    const std::size_t written = YSE::PATCHER::WriteInt(value, digits);
    return std::string(digits, written);
  }

  constexpr char kControlInletDoc[] =
      "The transport inlet, for the whole object. 'record' starts a fresh recording on every track "
      "— Max's 'begins recording all messages received in the other inlets' — with the tape wound "
      "back to zero, so the first event's delta is measured from the record message itself. 'play' "
      "rewinds and plays: Max's 'plays back all messages recorded earlier, sending them out the "
      "corresponding outlets in the same rhythm and at the same speed they were recorded'. 'stop' "
      "ends recording or playback, 'rewind' moves the cursor back without playing, 'clear' erases "
      "the tapes, and 'mute' / 'unmute' silence a track without stopping it — Max's 'causes mtr to "
      "stop producing output, while still continuing to play'. 'next' is the manual mode, "
      "outputting one event per track and reporting each on outlet 0. Every one of those may be "
      "followed by one or more 1-based track numbers to address only those tracks; with none, it "
      "addresses every track. Then four settings, which take a value instead of a track list. "
      "'timescale <n>' is a speed percentage — Max: '100 is the original timescale, whereas 200 "
      "would be twice as fast' — so it divides every wait; Max's own footnote comes with it, 'when "
      "a track is played again, the timescale is reset to 100', which is why the scale is sent "
      "after play rather than before. 'first <ms>' makes playback wait that long after a play "
      "before starting, without touching the tape. 'delay <ms>' does touch it, rewriting the first "
      "delta of every track — Max's 'sets the first delta time value of each track to that "
      "number'. "
      "'embed 1' makes the tapes save with the patcher, Max 8's flag and the only thing that makes "
      "this object write anything at all. 'read' and 'write' are accepted and do nothing: file I/O "
      "needs plumbing no patcher object can have yet, since a handler cannot find out which thread "
      "it is on (issues #683 / #691). A bang does nothing — Max 8 answers one with a dictionary, a "
      "type this patcher does not have — and so does any other message.";

  constexpr char kTrackInletDoc[] =
      "This track's tape head. While the track is recording, whatever arrives here is stored with "
      "the gap since the previous event — Max: 'numbers received in that track's inlet are "
      "combined "
      "with a delta time (the amount of time elapsed since the previous event) and stored'. Ints, "
      "floats, lists and bangs are all recorded, and each comes back out the matching outlet as "
      "the "
      "kind it went in. The gap is measured on the patcher's block clock, the same clock playback "
      "waits on, so its resolution is one audio block and it stops when the engine does. The "
      "transport words 'record', 'play', 'stop', 'next', 'rewind', 'clear', 'mute' and 'unmute' "
      "are "
      "commands here rather than data, addressing this one track — Max reserves them on this inlet "
      "too, so a patch brought across behaves the same — and 'play' may carry Max's two optional "
      "arguments, a repeat count and a timescale, so 'play 3 200' plays the track three times at "
      "twice the speed. Everything else is data. At most 256 events per track of at most 128 "
      "characters each; anything past either is dropped silently, since this inlet may be the "
      "audio "
      "thread.";

  constexpr char kReportOutletDoc[] =
      "What a 'next' just output, as Max's three-item list: the 1-based track number, the delta "
      "time of the event and its absolute time from the start of the tape. Max: 'track number, "
      "delta time, and absolute time of each message being output are sent out the leftmost "
      "outlet as a list'. It fires once per event a 'next' produces, immediately after that event "
      "has left its own outlet, so the pairing is never ambiguous when one 'next' steps several "
      "tracks at once. Automatic playback does not report — Max ties this outlet to 'next', which "
      "is the mode where a patch is driving the tape and needs to know where it has got to.";

  constexpr char kTrackOutletDoc[] =
      "Where this track plays back. Every event leaves in the kind it was recorded, .route's rule: "
      "a bang as a bang, a whole number as an int, a number spelled with a point or an exponent as "
      "a float, and anything else as a list. Silent while the track is muted, though the track "
      "keeps its clock and its cursor — Max: 'still continuing to play' — so unmuting mid-tape "
      "picks up where the tape has got to rather than where it was silenced.";

} // namespace

CONSTRUCT() {
  // Every port and every tape is built by ShapePorts(), so a saved `.mtr 4`
  // comes back with four of each. The clear callback is what makes
  // `SetParams("")` return the object to Max's no-argument shape rather than
  // leaving the previous track count in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // Max: "if there is no argument, there will be only one track". Also the
  // shape ClearParams() restores.
  ShapePorts();

  // The send buffers, taken once here on the control thread. Nothing on a
  // message path ever resizes them, which is what makes a step from a rendering
  // graph allocation-free. One past capacity so an event filled to
  // EVENT_CAPACITY still has room for the terminator c_str() needs.
  sendText.reserve(EVENT_CAPACITY + 1);
  // Three ints and the two spaces between them.
  reportText.reserve(((std::size_t)FORMAT_INT_WIDTH * 3) + 3);

  ADD_DESCRIPTION(
      "Records messages on several independent tracks and plays them back in the rhythm they "
      "arrived — Max's mtr, 'a multi-track recorder for any kind of message'. This is the "
      "automation object: capture a gesture on one track and a second on another, then replay them "
      "in sync. .qlist (issue #500) is the sibling and the contrast is the design. A cue list is "
      "written, so its timing is text an author types and there is one sequence with one cursor; "
      "this is recorded, so its timing is measured off the clock as messages arrive and there are "
      "N sequences at once, each with its own cursor, speed and mute. Both wait on the patcher's "
      "deferred-message scheduler (issue #628), but this one also reads that clock rather than "
      "only "
      "waiting on it, which is what messageScheduler::Now was added for — a gap measured on one "
      "clock and waited out on another does not come back the length it went in. The shape is "
      "Max's: 'the number of tracks determines the number of inlets and outlets in addition to the "
      "leftmost inlet and outlet', so N tracks gives N+1 of each. Inlet 0 takes the transport "
      "words "
      "for the whole object, each optionally followed by the 1-based track numbers it should "
      "address; inlet n is track n's tape head, taking both the data it records and the same "
      "transport words addressed to that one track, which is Max's arrangement and the reason "
      "those "
      "eight words are reserved on a data inlet here where .prepend and .atoi refuse to reserve "
      "any. Outlet n is where track n plays back, and outlet 0 reports what a 'next' just produced "
      "as Max's three-item list of track number, delta time and absolute time. Recording starts a "
      "track's tape at zero and stores each message with the gap since the previous one, the first "
      "gap measured from the record message itself, which is what gives Max's 'delay' something to "
      "overwrite. That gap is read off the scheduler's block counter, so its resolution is one "
      "audio block — not a shortcut but the honest ceiling, since playback waits on the same "
      "clock, "
      "whose deadline floor is also one block, and a delta finer than that could not be played "
      "back. The clock stops when the engine does, so a paused patch holds a recording where it "
      "stands; a standalone object has no patcher and so no clock at all, records everything at "
      "delta 0, and plays straight through. 'play' rewinds and arms the first event's delta, each "
      "delivery sending one event and arming the next; 'stop', 'rewind', 'clear', 'mute' and "
      "'unmute' are Max's, with mute keeping the clock and the cursor and skipping only the send. "
      "'timescale' is a speed percentage — Max: '100 is the original timescale, whereas 200 would "
      "be twice as fast' — and Max's footnote comes with it, that a play resets a track to 100, so "
      "the scale is sent after play rather than before; a 'play' in a track inlet may instead "
      "carry "
      "Max's own repeat count and scale, 'play 3 200'. 'first' delays the start without touching "
      "the tape and 'delay' rewrites the first delta permanently, which is exactly the difference "
      "between a setting and an edit. Storage is the family's model for the family's reason: a "
      "fixed table of 256 events of at most 128 characters per track, allocated whole at "
      "construction behind .value's non-blocking guard whose loser drops rather than waiting, "
      "because a copy-on-write GraphState publish assumes the writer is the control thread while "
      "this object is written by whichever thread its message arrived on. The guard is never held "
      "across a send: a playback step reads its event, advances the cursor and arms the next step "
      "all under it, then releases and only then sends — arming first, so a step needs the guard "
      "once rather than twice, which the scheduler's one-block floor makes safe. Anything past "
      "either bound is dropped whole rather than truncated. Calculate() does nothing. Persistence "
      "follows the family rule of saving exactly where Max has a save flag, and this is the object "
      "where the reference changed its mind: Max 5 said 'the only way to save the contents of mtr "
      "is with the write message; the object's contents cannot be embedded in a patcher file', and "
      "Max 8 added the flag, 'when embed is set to 1, any recorded data is saved with the "
      "patcher'. "
      "So .mtr follows the flag, which is .funbuff's rule and reconciles both — nothing is written "
      "until 'embed 1', and the flag itself is saved so a reloaded object still knows to embed. "
      "Cursors, mutes, timescales and whether a track is playing or recording are run-time "
      "position "
      "and do not survive, the way .coll's pointer does not. Reading and writing files is "
      "deliberately not here, .textfile's and .qlist's answer for their reason: a handler runs on "
      "whichever thread the message arrived on, in-patcher delivery dispatches on the audio "
      "thread, "
      "and THREAD is a dispatch-semantics tag rather than a thread identity, so no object can find "
      "out that it is off the audio callback, where opening a file would block it — shared "
      "plumbing "
      "filed as issue #683, with #691 tracking this object's half. Track count is Max's argument, "
      "clamped to 1-32: 32 is the ceiling Max's own reference gave for most of this object's life, "
      "and Max 8's later 128 is not followed because a playing track holds one slot in the "
      "patcher-wide pending set of 128, so a single object could take the whole table. Not ported: "
      "Max 8's dictionary surface (bang, info, dump, dictionary), the patcher having no dictionary "
      "type; its transport-sync attributes, there being no patcher-to-domain-clock bridge yet "
      "(issue #688); and the editing window, the patcher being headless.");
  ADD_CATEGORY(pCategory::TIME);
  PARAM_DOC("tracks", "1",
            "How many tracks to build, which is also how many inlets and outlets the object gets "
            "beyond the leftmost pair. Max: 'if there is no argument, there will be only one "
            "track', and 'up to 32 tracks are possible'. Clamped to 1-32, and the clamp is logged. "
            "The tapes are allocated at this size once, on the control thread, so the object costs "
            "what the argument asked for and never resizes afterwards.",
            "1-32");
}

void gMtr::ShapePorts() {
  // Rebuilt rather than resized: the track *count* comes from the argument, so
  // the inlets, the outlets and the tapes all have to agree. Safe because every
  // caller runs before the object is wired or published — the constructor, and
  // the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds. A
  // *live* SetParams never reaches here on a published object: registering the
  // callbacks makes ParamsNeedRebuild() true, so #234 replaces the object.
  int count = DEFAULT_TRACKS;
  int requested = DEFAULT_TRACKS;
  bool haveCount = false;

  for (const std::string& token : creationArgs) {
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens; an empty argument is not a number.
    if (token.empty() || haveCount) continue;

    float number = 0.f;
    // Strict on purpose, as the rest of the family is: ExprParseFloatList would
    // read `5abc` as 5 and fold `1e999` to 0, and neither answers "is this
    // creation argument a number at all".
    if (!ReadNumericToken(token, number)) continue;

    requested = ExprToInt(number);
    count = requested;
    if (count > MAX_TRACKS) count = MAX_TRACKS;
    if (count < MIN_TRACKS) count = MIN_TRACKS;
    haveCount = true;
  }

  // The control thread, before the object is wired or published, so this is the
  // one place a clamp can be *said* rather than merely made observable through
  // TrackCount().
  if (haveCount && requested != count) {
    INTERNAL::LogImpl().emit(E_WARNING, "patcher: .mtr track count " + IntText(requested) +
                                            " is outside " + IntText(MIN_TRACKS) + "-" +
                                            IntText(MAX_TRACKS) + "; clamped to " + IntText(count));
  }

  inputs.clear();
  outputs.clear();

  // Inlet 0 and outlet 0: Max's "leftmost inlet and outlet", which are the
  // object's own rather than any track's.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  inputs.back().SetDoc("in", kControlInletDoc, "");

  // ANY: the report is a list, and a track's outlet carries whichever of bang,
  // int, float and list was recorded.
  ADD_OUT_ANY;
  outputs.back().SetDoc("report", kReportOutletDoc, "");

  for (int i = 1; i <= count; i++) {
    inputs.emplace_back(this, false, i);
    REG_BANG_IN(BangIn);
    REG_INT_IN(IntIn);
    REG_FLOAT_IN(FloatIn);
    REG_LIST_IN(ListIn);
    inputs.back().SetDoc(InletLabel(i), kTrackInletDoc, "any");

    ADD_OUT_ANY;
    outputs.back().SetDoc(OutletLabel(i), kTrackOutletDoc, "any");
  }

  // Rebuilt with the ports so the two can never disagree on how many tracks
  // there are. Every event string is reserved here, on the control thread: this
  // is the whole of the object's allocation, and no message path adds to it.
  tracks.clear();
  tracks.resize((std::size_t)count);
  for (Track& track : tracks) {
    track.events.resize(MAX_EVENTS);
    for (Event& event : track.events)
      event.text.reserve(EVENT_CAPACITY + 1);
  }
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous track
  // count.
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

// ─── the clock ────────────────────────────────────────────────────────────────

std::uint64_t gMtr::NowBlock() const {
  const messageScheduler* scheduler = Scheduler();
  // A standalone object has no patcher and so no clock. Everything then records
  // at delta 0 and plays straight through, which is the only honest answer:
  // there is no time for it to be measured against.
  return scheduler == nullptr ? 0 : scheduler->Now();
}

bool gMtr::ArmStep(std::size_t track, int deltaMs, int extraMs) {
  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) return false;

  Track& tr = tracks[track];
  CancelStep(tr);

  // Max's timescale is a speed — "100 is the original timescale, whereas 200
  // would be twice as fast" — so it divides. `timescale` is never zero or
  // negative, which is what makes this safe.
  std::int64_t scaled =
      ((std::int64_t)(deltaMs > 0 ? deltaMs : 0) * (std::int64_t)DEFAULT_TIMESCALE) /
      (std::int64_t)tr.timescale;
  // `first` is not part of the recorded rhythm, so the scale does not stretch
  // it — it delays the start of the tape rather than living inside it.
  if (extraMs > 0) scaled += extraMs;
  if (scaled > 2147483647LL) scaled = 2147483647LL;

  // One clock per track, which is what makes the tracks independent.
  tr.pending = scheduler->ScheduleBang(this, (int)track, (int)scaled);
  return tr.pending != 0;
}

void gMtr::CancelStep(Track& tr) {
  if (tr.pending == 0) return;
  messageScheduler* scheduler = Scheduler();
  if (scheduler != nullptr) scheduler->Cancel(tr.pending);
  tr.pending = 0;
}

// ─── recording ────────────────────────────────────────────────────────────────

void gMtr::Record(std::size_t track, bool bang, const char* text, std::size_t length) {
  if (track >= tracks.size()) return;
  // Dropped whole rather than truncated: half a message is a different message,
  // and growing the tape would allocate on whichever thread this is.
  if (!bang && length > EVENT_CAPACITY) return;

  const std::uint64_t now = NowBlock();

  storeGuard guard(busy);
  if (!guard.Held()) return;

  Track& tr = tracks[track];
  if (!tr.recording) return;
  if (tr.count >= MAX_EVENTS) return;

  Event& event = tr.events[tr.count];
  // Max's "the amount of time elapsed since the previous event", measured on
  // the block clock playback will wait on. The guard is defensive: the clock is
  // monotonic, but a track armed before the patcher started would otherwise be
  // able to read a gap backwards.
  event.deltaMs = messageScheduler::MillisForBlocks(now > tr.lastBlock ? now - tr.lastBlock : 0);
  event.bang = bang;
  // assign() into a string reserved to EVENT_CAPACITY + 1 at construction, so
  // this allocates nothing.
  if (bang) {
    event.text.clear();
  } else {
    event.text.assign(text, length);
  }
  tr.lastBlock = now;
  tr.count++;
}

// ─── playing back ─────────────────────────────────────────────────────────────

gMtr::Step gMtr::TakeStep(std::size_t track, bool& muted, bool& armed) {
  muted = false;
  armed = false;

  storeGuard guard(busy);
  if (!guard.Held()) return Step::DROP;

  Track& tr = tracks[track];
  // Whatever armed this step has fired; the handle it left behind is stale.
  tr.pending = 0;
  // Stopped or cleared between the arm and the delivery.
  if (!tr.playing) return Step::END;
  if (tr.position >= tr.count) {
    tr.playing = false;
    return Step::END;
  }

  const Event& event = tr.events[tr.position];
  sendBang = event.bang;
  // Into the buffer reserved at construction, and copied rather than referenced:
  // the send happens with the guard released, so handing an outlet the stored
  // string would let a patch that records into this object from downstream
  // mutate the very message still being fanned out.
  sendText.assign(event.text);
  // The absolute clock keeps running through automatic playback even though
  // nothing reports it, so a `next` after a `stop` reports where the tape
  // actually got to rather than where the last `next` left it.
  tr.absMs = SatAdd(tr.absMs, event.deltaMs);
  muted = tr.muted;
  tr.position++;

  // Armed here, under the guard and before the send, so a step needs the guard
  // once rather than twice — and one it lost half way through would leave the
  // track marked playing with no clock behind it. The scheduler's one-block
  // deadline floor is what makes arming this early safe: the step just armed
  // cannot be delivered inside the dispatch that armed it.
  if (tr.position < tr.count) {
    armed = ArmStep(track, tr.events[tr.position].deltaMs, 0);
  } else if (tr.iterations > 1) {
    // Max's repeat count, from `play 3` in a track inlet. The gap before the
    // first event of the next pass is that event's own delta, which is what
    // makes a recorded loop loop at the length it was recorded.
    tr.iterations--;
    tr.position = 0;
    tr.absMs = 0;
    armed = ArmStep(track, tr.events[0].deltaMs, 0);
  } else {
    tr.playing = false;
  }
  return Step::OUTPUT;
}

void gMtr::Resume(std::size_t track, YSE::THREAD thread) {
  if (track >= tracks.size()) return;

  // Bounded by the tape: every step either advances the cursor or ends the
  // walk. The loop only runs more than once when there is no clock to arm on —
  // a standalone object, or a full pending set — where playing straight through
  // is better than abandoning the tape half played. `.qlist`'s fallback, for
  // `.qlist`'s reason.
  for (std::size_t steps = 0; steps <= MAX_EVENTS; steps++) {
    bool muted = false;
    bool armed = false;
    switch (TakeStep(track, muted, armed)) {
    case Step::OUTPUT:
      if (!muted) SendEvent(track, sendBang, thread);
      if (armed) return;
      break;
    case Step::END:
    case Step::DROP:
      return;
    }
  }
}

void gMtr::NextStep(std::size_t track, YSE::THREAD thread) {
  bool muted = false;
  bool bang = false;
  int delta = 0;
  int absolute = 0;

  {
    storeGuard guard(busy);
    if (!guard.Held()) return;

    Track& tr = tracks[track];
    // Max's `next` walks the tape by hand rather than by the clock, so it does
    // not arm anything and a tape that has run out simply has no next message.
    if (tr.position >= tr.count) return;

    const Event& event = tr.events[tr.position];
    bang = event.bang;
    sendText.assign(event.text);
    delta = event.deltaMs;
    tr.absMs = SatAdd(tr.absMs, event.deltaMs);
    absolute = tr.absMs;
    muted = tr.muted;
    tr.position++;
  }

  // The event first, then the report — right to left, and it also means a
  // `next` that steps several tracks at once pairs each report with the event
  // it describes rather than emitting a block of them at the end.
  if (!muted) SendEvent(track, bang, thread);
  SendReport(track, delta, absolute, thread);
}

// ─── sending ──────────────────────────────────────────────────────────────────

void gMtr::SendEvent(std::size_t track, bool bang, YSE::THREAD thread) {
  const std::size_t pin = track + 1;
  if (pin >= outputs.size()) return;

  if (bang) {
    outputs[pin].SendBang(thread);
    return;
  }

  // `.route`'s rule, shared with `.coll`, `.textfile` and `.qlist`: an event
  // leaves in the kind it went in.
  const std::size_t length = sendText.size();
  float number = 0.f;
  if (length > 0 && ReadNumericToken(sendText.c_str(), length, number)) {
    // Int or float is decided by the spelling, the test `.trigger`, `.match`,
    // `.route` and `.coll` already share, so a recorded `60` does not come back
    // as `60.`.
    if (!TokenLooksLikeFloat(sendText.c_str(), length) && FitsInt(number)) {
      outputs[pin].SendInt((int)number, thread);
    } else {
      outputs[pin].SendFloat(number, thread);
    }
    return;
  }
  outputs[pin].SendList(sendText, thread);
}

void gMtr::SendReport(std::size_t track, int deltaMs, int absMs, YSE::THREAD thread) {
  if (outputs.empty()) return;

  // Built in place in a buffer reserved at construction, through the patcher's
  // one int formatter: no allocation, no locale, on whichever thread this is.
  reportText.clear();
  char digits[FORMAT_INT_WIDTH];
  std::size_t written = WriteInt((int)track + 1, digits);
  reportText.append(digits, written);
  reportText.push_back(' ');
  written = WriteInt(deltaMs, digits);
  reportText.append(digits, written);
  reportText.push_back(' ');
  written = WriteInt(absMs, digits);
  reportText.append(digits, written);

  outputs[0].SendList(reportText, thread);
}

void gMtr::DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) {
  // The wait has elapsed. The scheduler wraps this in a fresh messageEventScope,
  // so everything the resumed step goes on to cause is one logical event (#628).
  // The tag is the track, which is what lets one object hold one clock per
  // track without them being able to be confused for one another.
  if (msg.tag < 0 || (std::size_t)msg.tag >= tracks.size()) return;

  // The delivered tag is passed straight through. It is T_GUI — "let the
  // block's own traversal render it" — which is the right reading for an outlet
  // send. The *other* reading of that tag, the one `patcherImplementation::
  // PassData` takes, is the trap `.qlist` documents and #690 tracks; this
  // object never sends remotely, so it never meets it.
  Resume((std::size_t)msg.tag, thread);
}

// ─── commands ─────────────────────────────────────────────────────────────────

gMtr::Cmd gMtr::ReadCommand(const char* word, std::size_t length) {
  if (TokenIs(word, length, "record", 6)) return Cmd::RECORD;
  if (TokenIs(word, length, "play", 4)) return Cmd::PLAY;
  if (TokenIs(word, length, "stop", 4)) return Cmd::STOP;
  if (TokenIs(word, length, "next", 4)) return Cmd::NEXT;
  if (TokenIs(word, length, "rewind", 6)) return Cmd::REWIND;
  if (TokenIs(word, length, "clear", 5)) return Cmd::CLEAR;
  if (TokenIs(word, length, "mute", 4)) return Cmd::MUTE;
  if (TokenIs(word, length, "unmute", 6)) return Cmd::UNMUTE;
  return Cmd::NONE;
}

void gMtr::Apply(Cmd cmd, std::size_t track, int iterations, int timescale, YSE::THREAD thread) {
  if (track >= tracks.size()) return;

  if (cmd == Cmd::NEXT) {
    // The one command that sends, so it manages the guard itself.
    NextStep(track, thread);
    return;
  }

  bool armed = false;
  bool started = false;

  {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    Track& tr = tracks[track];

    switch (cmd) {
    case Cmd::RECORD:
      // Max: "begins recording all messages received in the other inlets." A
      // fresh take: the tape is wound back to zero rather than appended to, so
      // the first event's delta is measured from this message and `delay` has
      // something to overwrite.
      CancelStep(tr);
      tr.playing = false;
      tr.recording = true;
      tr.count = 0;
      tr.position = 0;
      tr.absMs = 0;
      tr.lastBlock = NowBlock();
      break;

    case Cmd::PLAY:
      // Max: "plays back all messages recorded earlier, sending them out the
      // corresponding outlets in the same rhythm and at the same speed they
      // were recorded" — the whole tape, so it rewinds first. `rewind` and
      // `next` are the manual cursor.
      CancelStep(tr);
      tr.recording = false;
      if (tr.count == 0) {
        tr.playing = false;
        break;
      }
      tr.position = 0;
      tr.absMs = 0;
      tr.playing = true;
      tr.iterations = iterations > 0 ? iterations : 1;
      // Max: "when a track is played again, the timescale is reset to 100" —
      // which is why a timescale is sent *after* a play, and why `play 3 200`
      // in a track inlet is the way to set one up front.
      tr.timescale = timescale > 0 ? timescale : DEFAULT_TIMESCALE;
      armed = ArmStep(track, tr.events[0].deltaMs, firstMs);
      started = true;
      break;

    case Cmd::STOP:
      // Max: "stops mtr when it is recording or playing." The cursor stays put,
      // so a following `next` carries on from where the clock left off.
      CancelStep(tr);
      tr.playing = false;
      tr.recording = false;
      break;

    case Cmd::REWIND:
      // Max: "resets mtr to the beginning of its recorded sequence." A cursor
      // move and nothing else.
      tr.position = 0;
      tr.absMs = 0;
      break;

    case Cmd::CLEAR:
      // Max: "erases the contents of mtr." A cleared track has nothing left to
      // play, so a walk in progress ends with it.
      CancelStep(tr);
      tr.playing = false;
      tr.recording = false;
      tr.count = 0;
      tr.position = 0;
      tr.absMs = 0;
      break;

    case Cmd::MUTE:
      // Max: "causes mtr to stop producing output, while still continuing to
      // 'play'" — so the clock and the cursor keep running and only the send is
      // skipped.
      tr.muted = true;
      break;

    case Cmd::UNMUTE:
      tr.muted = false;
      break;

    case Cmd::NEXT:
    case Cmd::NONE:
      break;
    }
  }

  // No clock to wait on — a standalone object, or a full pending set. Playing
  // straight through is better than not playing at all, and the walk is bounded
  // by the tape.
  if (started && !armed) Resume(track, thread);
}

void gMtr::RunOnTracks(Cmd cmd, const std::string& message, std::size_t argOffset,
                       YSE::THREAD thread) {
  // Max: "play, followed by one or more track numbers" — and with none, every
  // track. Read into a stack array so nothing allocates.
  int numbers[MAX_TRACKS];
  const int found = ReadIntList(message, argOffset, numbers, MAX_TRACKS);

  if (found == 0) {
    for (std::size_t track = 0; track < tracks.size(); track++)
      Apply(cmd, track, 1, DEFAULT_TIMESCALE, thread);
    return;
  }

  for (int i = 0; i < found; i++) {
    // Max's track numbers are 1-based, matching the inlet they belong to. One
    // outside the range names no track and is skipped rather than clamped: a
    // clamp would silently address a track the patch did not mean.
    if (numbers[i] < 1 || numbers[i] > (int)tracks.size()) continue;
    Apply(cmd, (std::size_t)(numbers[i] - 1), 1, DEFAULT_TIMESCALE, thread);
  }
}

bool gMtr::HandleSetting(const char* word, std::size_t length, const std::string& message,
                         std::size_t argOffset) {
  if (TokenIs(word, length, "timescale", 9)) {
    int value = 0;
    if (!ReadIntArg(message, argOffset, value)) return true;
    // A timescale of zero or less cannot scale a duration into anything a clock
    // can wait for, so it is refused and the previous one kept.
    if (value <= 0) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    // Max: "sets the timescale for all tracks."
    for (Track& tr : tracks)
      tr.timescale = value;
    return true;
  }

  if (TokenIs(word, length, "first", 5)) {
    // Max: "causes mtr to wait that amount of time after a play message is
    // received before playing back." A setting rather than an edit — the tape
    // is untouched.
    int value = 0;
    if (!ReadIntArg(message, argOffset, value)) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    firstMs = value > 0 ? value : 0;
    return true;
  }

  if (TokenIs(word, length, "delay", 5)) {
    // Max: "sets the first delta time value of each track to that number, so
    // that all tracks begin playing back that amount of time after the play
    // message is received." An edit rather than a setting — this is written
    // into the tape and is what a save would save.
    int value = 0;
    if (!ReadIntArg(message, argOffset, value)) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    for (Track& tr : tracks) {
      if (tr.count > 0) tr.events[0].deltaMs = value > 0 ? value : 0;
    }
    return true;
  }

  if (TokenIs(word, length, "embed", 5)) {
    // Max 8: "when embed is set to 1, any recorded data is saved with the
    // patcher." The whole of this object's persistence, and off until a patch
    // turns it on — `.funbuff`'s rule, and the family's.
    int value = 0;
    if (!ReadIntArg(message, argOffset, value)) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    embed = value != 0;
    return true;
  }

  // Consumed and inert. File I/O cannot be done from a message handler at all
  // today — issue #683 builds the plumbing, #691 is this object's half. See the
  // class documentation.
  if (TokenIs(word, length, "read", 4)) return true;
  if (TokenIs(word, length, "write", 5)) return true;

  return false;
}

// ─── inlets ───────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)thread;
  // The control inlet answers a bang with a dictionary in Max 8, and this
  // patcher has no dictionary type, so it does nothing here. A bang in a track
  // inlet is data like any other message.
  if (inlet <= 0) return;
  Record((std::size_t)(inlet - 1), true, nullptr, 0);
}

INT_IN(IntIn) {
  (void)thread;
  if (inlet <= 0) return;
  char digits[FORMAT_INT_WIDTH];
  const std::size_t written = WriteInt(value, digits);
  Record((std::size_t)(inlet - 1), false, digits, written);
}

FLOAT_IN(FloatIn) {
  (void)thread;
  if (inlet <= 0) return;
  // Through the patcher's one float formatter, so a recorded float comes back
  // spelled the way every other float in the patcher is.
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Record((std::size_t)(inlet - 1), false, text, (std::size_t)written);
}

LIST_IN(ListIn) {
  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(text, length, 0, begin, end)) return;

  const Cmd cmd = ReadCommand(text + begin, end - begin);

  if (inlet <= 0) {
    // The control inlet is a command inlet: a transport word optionally
    // followed by track numbers, a setting followed by its value, and anything
    // else does nothing at all, which is Max.
    if (cmd != Cmd::NONE) {
      RunOnTracks(cmd, value, end, thread);
      return;
    }
    HandleSetting(text + begin, end - begin, value, end);
    return;
  }

  const std::size_t track = (std::size_t)(inlet - 1);

  if (cmd != Cmd::NONE) {
    // Max reserves the transport words on a track's own inlet too, so this is
    // reproduced rather than invented — see the class documentation on why the
    // `.prepend` / `.atoi` rule about not reserving words on a data inlet
    // cannot be followed here.
    int iterations = 1;
    int timescale = DEFAULT_TIMESCALE;
    if (cmd == Cmd::PLAY) {
      // Max's `play 3 200`: three times through, at twice the speed.
      std::size_t cursor = end;
      int argument = 0;
      if (ReadIntArgAt(value, cursor, argument) && argument > 0) iterations = argument;
      if (ReadIntArgAt(value, cursor, argument) && argument > 0) timescale = argument;
    }
    Apply(cmd, track, iterations, timescale, thread);
    return;
  }

  // Data. Trimmed, because a recorded message is stored as text and the
  // separators around it are not part of it.
  std::size_t first = 0;
  std::size_t last = length;
  Trim(text, first, last);
  if (last <= first) return;
  Record(track, false, text + first, last - first);
}

// ─── persistence ──────────────────────────────────────────────────────────────

void gMtr::DumpState(nlohmann::json::value_type& json) {
  // Max 8's embed is what decides whether there is anything to write at all.
  // With it off nothing is written and the serialised object is byte for byte
  // what it was — Max 5's "the object's contents cannot be embedded in a
  // patcher file", which is the same object before the flag existed.
  if (!embed) return;

  // Control thread (patcherImplementation::DumpJSON holds mtx), but the guard is
  // still taken, because a message may be arriving from a rendering graph while
  // the patch is being saved.
  storeGuard guard(busy);
  if (!guard.Held()) return;

  // Written even with empty tapes, so a reloaded object still knows to embed
  // itself the next time the patch is saved.
  json["embed"] = true;

  for (const Track& tr : tracks) {
    nlohmann::json::value_type events = nlohmann::json::array();
    for (std::size_t i = 0; i < tr.count; i++) {
      nlohmann::json::value_type event;
      event["d"] = tr.events[i].deltaMs;
      if (tr.events[i].bang) {
        event["b"] = true;
      } else {
        event["v"] = tr.events[i].text;
      }
      events.push_back(event);
    }
    json["tracks"].push_back(events);
  }
}

void gMtr::RestoreState(const nlohmann::json::value_type& json) {
  // Called from ParseJSON on the control thread, on a freshly built object the
  // audio thread cannot see yet.
  storeGuard guard(busy);
  if (!guard.Held()) return;

  embed = json.value("embed", false);

  const auto stored = json.find("tracks");
  if (stored == json.end() || !stored->is_array()) return;

  std::size_t index = 0;
  for (const auto& list : *stored) {
    // The track *count* is a creation parameter, restored before this runs, so
    // a saved patch whose argument has since been edited down simply loses the
    // tapes that no longer have a track to live on.
    if (index >= tracks.size()) break;

    Track& tr = tracks[index];
    tr.count = 0;
    tr.position = 0;
    tr.absMs = 0;

    if (list.is_array()) {
      for (const auto& event : list) {
        if (tr.count >= MAX_EVENTS) break;
        if (!event.is_object()) continue;

        Event& dst = tr.events[tr.count];
        dst.deltaMs = event.value("d", 0);
        dst.bang = event.value("b", false);
        if (dst.bang) {
          dst.text.clear();
        } else {
          const std::string text = event.value("v", std::string());
          if (text.size() > EVENT_CAPACITY) continue;
          dst.text.assign(text);
        }
        tr.count++;
      }
    }
    index++;
  }
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::size_t gMtr::Count(int track) const {
  storeGuard guard(busy);
  if (!guard.Held() || track < 0 || track >= (int)tracks.size()) return 0;
  return tracks[(std::size_t)track].count;
}

std::string gMtr::EventAt(int track, std::size_t index) const {
  storeGuard guard(busy);
  if (!guard.Held() || track < 0 || track >= (int)tracks.size()) return std::string();

  const Track& tr = tracks[(std::size_t)track];
  if (index >= tr.count) return std::string();

  const Event& event = tr.events[index];
  char digits[FORMAT_INT_WIDTH];
  const std::size_t written = WriteInt(event.deltaMs, digits);
  std::string text(digits, written);
  text.push_back(' ');
  text.append(event.bang ? "bang" : event.text);
  return text;
}

std::size_t gMtr::Position(int track) const {
  storeGuard guard(busy);
  if (!guard.Held() || track < 0 || track >= (int)tracks.size()) return 0;
  return tracks[(std::size_t)track].position;
}

bool gMtr::IsRecording(int track) const {
  storeGuard guard(busy);
  if (!guard.Held() || track < 0 || track >= (int)tracks.size()) return false;
  return tracks[(std::size_t)track].recording;
}

bool gMtr::IsPlaying(int track) const {
  storeGuard guard(busy);
  if (!guard.Held() || track < 0 || track >= (int)tracks.size()) return false;
  return tracks[(std::size_t)track].playing;
}

bool gMtr::IsMuted(int track) const {
  storeGuard guard(busy);
  if (!guard.Held() || track < 0 || track >= (int)tracks.size()) return false;
  return tracks[(std::size_t)track].muted;
}

int gMtr::Timescale(int track) const {
  storeGuard guard(busy);
  if (!guard.Held() || track < 0 || track >= (int)tracks.size()) return DEFAULT_TIMESCALE;
  return tracks[(std::size_t)track].timescale;
}

int gMtr::First() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return firstMs;
}

bool gMtr::Embeds() const {
  storeGuard guard(busy);
  if (!guard.Held()) return false;
  return embed;
}

#undef className
