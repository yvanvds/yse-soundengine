#include "gSeq.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

using namespace YSE::PATCHER;

#define className gSeq

namespace {

  // A running sum of deltas can reach the top of the int range on a long tape.
  // Saturate rather than wrap: a clock that went negative would be worse than
  // one that stopped. `.mtr`'s helper, for `.mtr`'s reason.
  int SatAdd(int a, int b) {
    const std::int64_t sum = (std::int64_t)a + (std::int64_t)b;
    if (sum > 2147483647LL) return 2147483647;
    if (sum < -2147483648LL) return -2147483647 - 1;
    return (int)sum;
  }

  // `hook`'s edit: a delta times a positive multiplier, rounded and saturated.
  // Clamped before the round rather than after, since casting an out-of-range
  // double to int is undefined behaviour rather than a large int.
  int SatScale(int value, float factor) {
    const double scaled = (double)value * (double)factor;
    if (scaled >= 2147483647.0) return 2147483647;
    if (scaled <= 0.0) return 0;
    return (int)std::llround(scaled);
  }

  // `addeventdelay`'s argument, which Max types as a float, as the whole number
  // of milliseconds a delta is stored as. Clamped for the reason above.
  int RoundToInt(float value) {
    const double wide = (double)value;
    if (wide >= 2147483647.0) return 2147483647;
    if (wide <= -2147483648.0) return -2147483647 - 1;
    return (int)std::llround(wide);
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

  // The float argument after a message word — `hook` and `addeventdelay` both
  // take one. Strict, as the rest of the family is: a token that is only partly
  // a number is not a number.
  bool ReadFloatArgAt(const std::string& text, std::size_t offset, float& out) {
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!NextToken(text.c_str(), text.size(), offset, begin, end)) return false;
    return ReadNumericToken(text.c_str() + begin, end - begin, out);
  }

  constexpr char kInletDoc[] =
      "The only inlet, and it carries both the bytes and the transport. While the object is "
      "recording, a number here is one raw MIDI byte — Max: 'numbers received in its inlet are "
      "interpreted as bytes of MIDI messages (usually from midiformat or midiin)' — stored with "
      "the gap since the previous byte, measured on the patcher's block clock. A float is "
      "converted to an int, as Max's is, and a number outside 0-255 is not a MIDI byte and is "
      "refused rather than being folded into range. A list of numbers records each of them in "
      "turn: Max has no list method here, but every message in this patcher arrives as a list, so "
      "refusing one would make the object unreachable from midiformat's own output shape. Then the "
      "transport. 'record' starts a fresh take and 'append' carries on at the end of what is "
      "already there — Max: 'starts recording at the end of the stored sequence, without erasing "
      "the existing sequence'. 'start' and a bang both play from the beginning; 'start <n>' sets "
      "the speed, Max's multiplier where 1024 is the recorded tempo, 512 half of it and 2048 twice "
      "it, and the speed can only be set here, at the moment playback starts. 'start -1' plays on "
      "'tick' messages instead of on the clock — Max: 'seq must receive 48 tick messages per "
      "second' to play at the recorded tempo — which is how a patch drives the sequence from its "
      "own timing source. 'stop' ends recording or playing, and neither 'record' nor 'start' needs "
      "one first. 'clear' erases the tape. 'delay <ms>' sets the onset of the first event and "
      "shifts everything after it, 'addeventdelay <ms>' adds to that onset, and 'hook <f>' "
      "multiplies every event time, which Max allows even mid-playback. 'read', 'write', 'dump' "
      "and 'print' are accepted and do nothing: file I/O needs plumbing no patcher object can have "
      "yet, since a handler cannot find out which thread it is on (issues #683 / #692), and "
      "printing would allocate and lock on a path that may be the audio callback. Anything else "
      "does nothing, which is Max.";

  constexpr char kByteOutletDoc[] =
      "The sequence, one raw MIDI byte at a time — Max: 'the sequence stored in seq is sent out "
      "the outlet in the form of individual MIDI bytes, usually to be sent to midiparse or "
      "midiout'. Bytes recorded at the same moment leave in the same audio block rather than one "
      "per block: the three bytes of a note-on delivered a block apart would not be that note-on, "
      "so a step keeps going for as long as the next event's gap scales to nothing, and only waits "
      "for a real one.";

  constexpr char kEndOutletDoc[] =
      "Bangs when the sequence finishes — Max: 'indicates that seq has finished playing the "
      "current sequence'. Max's parenthesis is reproduced exactly, because it is surprising and "
      "because it is useful: '(the bang is sent out immediately before the final event of the "
      "sequence is played)'. So this fires first and the last byte follows it, which lets a patch "
      "know that the byte about to arrive is the last one rather than finding out afterwards. A "
      "'start' on an empty sequence bangs nothing: there is no final event for the bang to "
      "precede.";

} // namespace

CONSTRUCT() {
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // Max's left outlet: individual MIDI bytes. Max's middle outlet: the end
  // bang. Max's right outlet carries MIDI-file meta messages and is not built —
  // see the class documentation on why it would be permanently silent, and on
  // it being appended at Max's own position when the file half lands.
  ADD_OUT_INT;
  ADD_OUT_BANG;

  // The whole of this object's allocation, taken here on the control thread.
  // Nothing on a message path ever resizes it, which is what makes recording
  // and playback from a rendering graph allocation-free.
  events.resize(MAX_EVENTS);

  ADD_DESCRIPTION(
      "Records and plays back raw MIDI bytes — Max's seq, 'a sequencer of raw MIDI bytes'. It is "
      "the third object in the store family with a clock and the one whose contents are not "
      "messages: .qlist (issue #500) plays a written score, .mtr (issue #501) plays a recorded "
      "tape of patcher messages, and this plays a recorded tape of the MIDI wire format, one byte "
      "per event, which is Max's own arrangement and the reason Max's See Also puts mtr beside it. "
      "Bytes rather than parsed messages is deliberate: a raw stream is what a MIDI port hands "
      "over, and a sequencer that had to understand running status, system exclusive or a "
      "fourteen-bit bend before it could store them would be unable to record what it does not "
      "understand. Max's answer is midiparse and midiformat on either side, and this patcher's "
      ".noteon, .noteoff, .controlchange and .midiout already speak the same bytes. Timing comes "
      "from the patcher's deferred-message scheduler (issue #628) through the pair .mtr uses: "
      "messageScheduler::Now measures the gap between two arriving bytes and BlocksForMillis waits "
      "one out again, one clock for both halves because a gap measured on one clock and waited out "
      "on another does not come back the length it went in. That clock stops when the engine does, "
      "so a paused patch holds a recording where it stands; a standalone object has no patcher and "
      "so no clock, records everything at delta 0 and plays straight through. One rule departs "
      "from .qlist deliberately: where a cue list treats the scheduler's one-block deadline floor "
      "as a feature, advancing one entry per block through a run of zero delays, this object runs "
      "them out inside a single dispatch, because the three bytes of a note-on delivered one block "
      "apart are not that note-on. The walk is still bounded by the tape, so a patch cannot lock "
      "the audio thread up with it. 'record' starts a fresh take and 'append' carries on at the "
      "end of the existing one; 'start' and a bang play from the beginning; 'start <n>' is Max's "
      "tempo multiplier, 1024 being the recorded speed, and Max's parenthesis that the speed is "
      "settable 'only at the time you start it' is reproduced. 'start -1' is Max's tick-driven "
      "mode: playback then advances on 'tick' messages, 48 of them per second at the recorded "
      "tempo, which is 24 per quarter note at 120 BPM and therefore a MIDI clock. That is also "
      "this object's answer to issue #502's ask for a domain clock — there is no "
      "patcher-to-domain-clock bridge today (issue #688), but tick is Max's own external timing "
      "source and when the bridge lands, driving tick from it is the whole of the work. The end of "
      "the sequence bangs the second outlet, and Max's odd but useful ordering is kept literally: "
      "'the bang is sent out immediately before the final event of the sequence is played'. "
      "'delay' sets the first event's onset and shifts the rest with it, 'addeventdelay' adds to "
      "that onset, and 'hook' multiplies every event time, which Max allows even while the "
      "sequence is playing. Storage is the family's model for the family's reason: a fixed table "
      "of 4096 events allocated whole at construction behind .value's non-blocking guard whose "
      "loser drops rather than waiting, because a copy-on-write GraphState publish assumes the "
      "writer is the control thread while this object is written by whichever thread its message "
      "arrived on. 4096 rather than the 256 the text stores share, because an event here is a "
      "delta and one byte rather than a string: three of them make one note, so 256 would be 85 "
      "notes and not a sequence. The guard is never held across a send, and .mtr's arrangement "
      "inside it is copied — read, advance, arm, release, then send — so a step needs the guard "
      "once rather than twice. Calculate() does nothing. Nothing is saved with the patcher: the "
      "family rule is to save exactly where Max has a save flag, qlist saves its cue list and mtr "
      "has Max 8's embed, and seq has neither because its contents live in a file and read/write "
      "are its persistence, which is text's answer too. The filename creation argument is a "
      "parameter rather than state and does survive, so a patch brought across from Max still "
      "names the file it meant. Reading and writing files is deliberately not here, .textfile's, "
      ".qlist's and .mtr's answer for their reason: a handler runs on whichever thread the message "
      "arrived on, in-patcher delivery dispatches on the audio thread, and THREAD is a "
      "dispatch-semantics tag rather than a thread identity, so no object can find out that it is "
      "off the audio callback, where opening a file would block it — shared plumbing filed as "
      "issue #683, with #692 tracking this object's half. 'print' is inert alongside them for a "
      "neighbouring reason: the patcher's log builds a string and takes a lock, which the same "
      "path must not do. Not ported: the tempo, sequencetempo and overridetempo attributes, all "
      "three of which describe the tempo of a MIDI file and would have nothing to reflect or "
      "override without one; Max's meta outlet, meta events being a file construct that cannot "
      "appear in a recorded byte stream, which when the file half lands is appended at Max's own "
      "position so no saved patch's cords shift; and the editing window, the patcher being "
      "headless.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "in", kInletDoc, "0-255");
  OUTLET_DOC(0, "midi", kByteOutletDoc, "0-255");
  OUTLET_DOC(1, "end", kEndOutletDoc, "");
  PARAM_DOC("filename", "",
            "Max's 'name of a file to be read into seq automatically when the patch is loaded'. "
            "Stored and round-tripped through a save, so a patch brought across from Max still "
            "names the file it meant, but nothing is read from it yet — file I/O cannot be done "
            "from a patcher message handler at all today (issues #683 / #692).",
            "");
}

// ─── parameters ───────────────────────────────────────────────────────────────

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous name.
  creationArgs.clear();
  fileName.clear();
  count = 0;
  position = 0;
  recording = false;
  playing = false;
  speed = NORMAL_SPEED;
}

PARM_PARSE() {
  fileName.clear();

  for (const std::string& token : creationArgs) {
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens; an empty token is not a filename.
    if (token.empty()) continue;
    fileName = token;
    break;
  }

  // A re-parse also drops the tape: the object that comes back is the one the
  // arguments describe, and a recording left over from before would belong to a
  // file the object is no longer named after. `.textfile`'s rule.
  count = 0;
  position = 0;
  recording = false;
  playing = false;
  speed = NORMAL_SPEED;
}

// ─── the clock ────────────────────────────────────────────────────────────────

std::uint64_t gSeq::NowBlock() const {
  const messageScheduler* scheduler = Scheduler();
  // A standalone object has no patcher and so no clock. Everything then records
  // at delta 0 and plays straight through, which is the only honest answer:
  // there is no time for it to be measured against.
  return scheduler == nullptr ? 0 : scheduler->Now();
}

int gSeq::ScaledMillis(int deltaMs) const {
  if (deltaMs <= 0) return 0;
  // Max's multiplier is a speed — "start 2048 plays it back at twice the
  // original speed" — so it divides. `speed` is never zero here: a `start`
  // refuses a non-positive multiplier, and tick mode never reaches this.
  std::int64_t scaled = ((std::int64_t)deltaMs * (std::int64_t)NORMAL_SPEED) / (std::int64_t)speed;
  if (scaled > 2147483647LL) scaled = 2147483647LL;
  return (int)scaled;
}

bool gSeq::ArmStep(int waitMs) {
  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) return false;

  CancelStep();
  // One clock per object, Max's shape — `.bondo`'s rule, arrived at for the
  // same reason. The tag is unused: this object has only one kind of pending
  // step.
  pending = scheduler->ScheduleBang(this, 0, waitMs);
  return pending != 0;
}

void gSeq::CancelStep() {
  if (pending == 0) return;
  messageScheduler* scheduler = Scheduler();
  if (scheduler != nullptr) scheduler->Cancel(pending);
  pending = 0;
}

// ─── recording ────────────────────────────────────────────────────────────────

void gSeq::Record(int value) {
  // Max stores raw MIDI bytes, and a number outside a byte is not one. Refused
  // rather than masked or clamped: the family's rule everywhere else is that
  // something that does not fit is refused whole, and a byte folded into range
  // would be a different MIDI message rather than a rejected one.
  if (value < 0 || value > 255) return;

  const std::uint64_t now = NowBlock();

  storeGuard guard(busy);
  if (!guard.Held()) return;

  if (!recording) return;
  // Dropped rather than growing the tape, which would allocate on whichever
  // thread this is.
  if (count >= MAX_EVENTS) return;

  // The gap since the previous byte, measured on the block clock playback will
  // wait on. The comparison is defensive: the clock is monotonic, but an object
  // that began recording before the patcher started would otherwise be able to
  // read a gap backwards.
  events[count].deltaMs = messageScheduler::MillisForBlocks(now > lastBlock ? now - lastBlock : 0);
  events[count].byte = (unsigned char)value;
  lastBlock = now;
  count++;
}

// ─── playing back ─────────────────────────────────────────────────────────────

gSeq::Step gSeq::TakeStep(bool& last, bool& armed) {
  last = false;
  armed = false;

  storeGuard guard(busy);
  if (!guard.Held()) return Step::DROP;

  // Whatever armed this step has fired; the handle it left behind is stale.
  pending = 0;
  // Stopped, cleared or switched to recording between the arm and the delivery.
  if (!playing) return Step::END;
  if (position >= count) {
    playing = false;
    return Step::END;
  }

  // Copied out rather than referenced: the send happens with the guard
  // released, so a patch that records into this object from downstream must not
  // be able to move the byte still being fanned out.
  sendByte = events[position].byte;
  position++;
  last = position >= count;

  if (last) {
    playing = false;
  } else {
    // Armed here, under the guard and before the send, so a step needs the
    // guard once rather than twice — `.mtr`'s arrangement, for `.mtr`'s reason.
    // A gap that scales to nothing arms nothing at all and the walk carries on
    // in this same dispatch, which is what keeps the bytes of one MIDI message
    // together; the scheduler's one-block floor would otherwise spread them
    // over three blocks.
    const int wait = ScaledMillis(events[position].deltaMs);
    if (wait > 0) armed = ArmStep(wait);
  }
  return Step::OUTPUT;
}

void gSeq::Resume(YSE::THREAD thread) {
  // Bounded by the tape: every step either advances the cursor or ends the
  // walk. It runs more than once for a run of zero-delta events — the bytes of
  // one MIDI message — and when there is no clock to arm on at all, a
  // standalone object or a full pending set, where playing straight through is
  // better than abandoning the sequence half played.
  for (std::size_t steps = 0; steps <= MAX_EVENTS; steps++) {
    bool last = false;
    bool armed = false;
    switch (TakeStep(last, armed)) {
    case Step::OUTPUT:
      SendStep(last, thread);
      if (armed) return;
      break;
    case Step::END:
    case Step::DROP:
      return;
    }
  }
}

gSeq::Step gSeq::TakeTickStep(bool& last) {
  last = false;

  storeGuard guard(busy);
  if (!guard.Held()) return Step::DROP;

  if (!playing || speed != TICK_SPEED) return Step::END;
  if (position >= count) {
    playing = false;
    return Step::END;
  }

  // Max: "in order to play the sequence at its original recorded tempo, seq
  // must receive 48 tick messages per second". Derived from the count rather
  // than accumulated per tick, so 48 ticks is exactly one second however many
  // have gone by.
  const std::int64_t elapsedMs = (ticks * 1000) / TICKS_PER_SECOND;
  // Not due yet. END rather than a state of its own: both answers stop the
  // walk, and the next tick asks again.
  if (dueMs > elapsedMs) return Step::END;

  sendByte = events[position].byte;
  position++;
  last = position >= count;

  if (last) {
    playing = false;
  } else {
    dueMs += events[position].deltaMs;
  }
  return Step::OUTPUT;
}

void gSeq::Tick(YSE::THREAD thread) {
  {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    // A tick outside Max's `start -1` mode means nothing — the millisecond
    // clock is already running the sequence.
    if (!playing || speed != TICK_SPEED) return;
    ticks++;
  }

  // Every event the tick just made due, which is more than one whenever the
  // sequence is denser than the tick grid. Bounded by the tape.
  for (std::size_t steps = 0; steps <= MAX_EVENTS; steps++) {
    bool last = false;
    switch (TakeTickStep(last)) {
    case Step::OUTPUT:
      SendStep(last, thread);
      break;
    case Step::END:
    case Step::DROP:
      return;
    }
  }
}

// ─── sending ──────────────────────────────────────────────────────────────────

void gSeq::SendStep(bool last, YSE::THREAD thread) {
  // Max: "the bang is sent out immediately before the final event of the
  // sequence is played." Odd, documented, and useful — a patch learns that the
  // byte about to arrive is the last one rather than finding out afterwards.
  if (last && outputs.size() > 1) outputs[1].SendBang(thread);
  if (!outputs.empty()) outputs[0].SendInt((int)sendByte, thread);
}

void gSeq::DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) {
  (void)msg;
  // The wait has elapsed. The scheduler wraps this in a fresh messageEventScope,
  // so everything the resumed step goes on to cause is one logical event (#628).
  //
  // The delivered tag is passed straight through. It is T_GUI — "let the
  // block's own traversal render it" — which is the right reading for an outlet
  // send. The *other* reading of that tag, the one `patcherImplementation::
  // PassData` takes, is the trap `.qlist` documents and #690 tracks; this object
  // never sends remotely, so it never meets it.
  Resume(thread);
}

// ─── commands ─────────────────────────────────────────────────────────────────

void gSeq::StartRecording(bool keepContents) {
  storeGuard guard(busy);
  if (!guard.Held()) return;

  // Max: "a stop message need not be received when switching directly from
  // playing to recording, or vice-versa."
  CancelStep();
  playing = false;
  recording = true;

  // Max's `record` starts a fresh take; `append` "starts recording at the end
  // of the stored sequence, without erasing the existing sequence", which is
  // the whole difference between the two.
  if (!keepContents) count = 0;
  position = 0;

  // The first gap of a take is measured from this message, which is what gives
  // Max's `delay` something to overwrite. An `append` measures the gap to the
  // byte it stored last from here too: the time the object spent stopped is not
  // part of the recording.
  lastBlock = NowBlock();
}

void gSeq::StartPlaying(int requested, YSE::THREAD thread) {
  bool armed = false;
  bool started = false;

  {
    storeGuard guard(busy);
    if (!guard.Held()) return;

    CancelStep();
    recording = false;
    playing = false;

    // Max's `bang`: "plays the sequence stored in seq", from the beginning.
    // Nothing recorded is nothing to play, and there is no final event for the
    // end bang to precede either.
    if (count == 0) return;

    position = 0;
    playing = true;
    // Max: the speed is settable "only at the time you start it", so this is
    // the one place it is written.
    speed = requested;

    if (speed == TICK_SPEED) {
      // Max: "starts the sequencer, but rather than follow Max's millisecond
      // clock, seq waits for a tick message to advance its clock." So nothing
      // happens here at all — not even a zero-delta first event, which falls
      // due on the first tick.
      ticks = 0;
      dueMs = events[0].deltaMs;
      return;
    }

    const int wait = ScaledMillis(events[0].deltaMs);
    if (wait > 0) armed = ArmStep(wait);
    started = true;
  }

  // Either the first event is due immediately, or there is no clock to wait on
  // — a standalone object, or a full pending set. Playing on is better than not
  // playing at all, and the walk is bounded by the tape.
  if (started && !armed) Resume(thread);
}

void gSeq::StopAll() {
  storeGuard guard(busy);
  if (!guard.Held()) return;

  // Max: "stops the sequencer if it is recording or playing." The cursor stays
  // where it is; only a `start` rewinds.
  CancelStep();
  playing = false;
  recording = false;
}

void gSeq::Clear() {
  storeGuard guard(busy);
  if (!guard.Held()) return;

  // Max: "clears the sequence currently stored in the seq object." A cleared
  // sequence has nothing left to play, so a walk in progress ends with it.
  CancelStep();
  playing = false;
  recording = false;
  count = 0;
  position = 0;
}

bool gSeq::HandleCommand(const char* word, std::size_t length, const std::string& message,
                         std::size_t argOffset, YSE::THREAD thread) {
  if (TokenIs(word, length, "record", 6)) {
    StartRecording(false);
    return true;
  }

  if (TokenIs(word, length, "append", 6)) {
    StartRecording(true);
    return true;
  }

  if (TokenIs(word, length, "start", 5)) {
    // Max: "the word start by itself has the same effect as bang", and with a
    // number, "start 1024 indicates normal tempo".
    int requested = NORMAL_SPEED;
    int argument = 0;
    if (ReadIntArg(message, argOffset, argument)) {
      if (argument == TICK_SPEED) {
        requested = TICK_SPEED;
      } else if (argument > 0) {
        requested = argument;
      }
      // Anything else — 0, or a negative that is not -1 — cannot scale a
      // duration into something a clock can wait for, so it is refused and the
      // recorded tempo used instead.
    }
    StartPlaying(requested, thread);
    return true;
  }

  if (TokenIs(word, length, "stop", 4)) {
    StopAll();
    return true;
  }

  if (TokenIs(word, length, "tick", 4)) {
    Tick(thread);
    return true;
  }

  if (TokenIs(word, length, "clear", 5)) {
    Clear();
    return true;
  }

  if (TokenIs(word, length, "delay", 5)) {
    // Max: "sets the onset time, in milliseconds, of the first event in the
    // recorded sequence. All events in the sequence are shifted so that the
    // first event occurs at the specified onset time." With deltas stored
    // rather than absolute times, that shift *is* writing the first delta.
    int value = 0;
    if (!ReadIntArg(message, argOffset, value)) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    if (count > 0) events[0].deltaMs = value > 0 ? value : 0;
    return true;
  }

  if (TokenIs(word, length, "addeventdelay", 13)) {
    // Max: "adds to the delay onset time, in milliseconds, of the first event
    // in the recorded sequence" — the same edit as `delay`, made relative.
    float value = 0.f;
    if (!ReadFloatArgAt(message, argOffset, value)) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    if (count > 0) {
      const int added = SatAdd(events[0].deltaMs, RoundToInt(value));
      events[0].deltaMs = added > 0 ? added : 0;
    }
    return true;
  }

  if (TokenIs(word, length, "hook", 4)) {
    // Max: "multiplies all the event times in the stored sequence by that
    // number. For example, if the number is 2.0, all event times will be
    // doubled, and the sequence will play back twice as slowly.
    // Multiplications can even be performed while the sequence is playing" —
    // so this is an edit rather than a setting, and a hook mid-playback reaches
    // every event from the next step onward. The step already armed keeps the
    // wait it was armed with, there being no way to shorten a deadline that has
    // already been handed to the scheduler.
    float value = 0.f;
    if (!ReadFloatArgAt(message, argOffset, value)) return true;
    // A multiplier of zero or less cannot scale a duration into anything a
    // clock can wait for, so it is refused and the tape left alone.
    if (!(value > 0.f)) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    for (std::size_t i = 0; i < count; i++)
      events[i].deltaMs = SatScale(events[i].deltaMs, value);
    return true;
  }

  // Consumed and inert. File I/O cannot be done from a message handler at all
  // today — issue #683 builds the plumbing, #692 is this object's half — and
  // `print` would build a log string and take the log's lock on the same path.
  // See the class documentation.
  if (TokenIs(word, length, "read", 4)) return true;
  if (TokenIs(word, length, "write", 5)) return true;
  if (TokenIs(word, length, "dump", 4)) return true;
  if (TokenIs(word, length, "print", 5)) return true;

  return false;
}

// ─── inlets ───────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  // Max: "bang — starts playing the sequence stored in seq", at the recorded
  // tempo.
  StartPlaying(NORMAL_SPEED, thread);
}

INT_IN(IntIn) {
  (void)inlet;
  (void)thread;
  Record(value);
}

FLOAT_IN(FloatIn) {
  (void)inlet;
  (void)thread;
  // Max: "float — converted to int."
  Record((int)value);
}

LIST_IN(ListIn) {
  (void)inlet;
  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(text, length, 0, begin, end)) return;

  if (HandleCommand(text + begin, end - begin, value, end, thread)) return;

  // Not a command: a list of numbers is a run of MIDI bytes, recorded in order.
  // Max documents no list method — its own chain feeds seq one int at a time —
  // but every message in this patcher arrives as a list, so a numeric list is
  // the shape midiformat's output actually has here and refusing it would leave
  // the object unreachable. The bytes of one list all record inside one
  // dispatch and so share one block, which gives the second and third of a
  // note-on a delta of zero: exactly what playback runs out together.
  std::size_t cursor = 0;
  int number = 0;
  while (ReadIntArgAt(value, cursor, number))
    Record(number);
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::size_t gSeq::Count() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return count;
}

std::string gSeq::EventAt(std::size_t index) const {
  storeGuard guard(busy);
  if (!guard.Held() || index >= count) return std::string();

  char digits[FORMAT_INT_WIDTH];
  std::size_t written = WriteInt(events[index].deltaMs, digits);
  std::string text(digits, written);
  text.push_back(' ');
  written = WriteInt((int)events[index].byte, digits);
  text.append(digits, written);
  return text;
}

std::size_t gSeq::Position() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return position;
}

bool gSeq::IsRecording() const {
  storeGuard guard(busy);
  if (!guard.Held()) return false;
  return recording;
}

bool gSeq::IsPlaying() const {
  storeGuard guard(busy);
  if (!guard.Held()) return false;
  return playing;
}

int gSeq::Speed() const {
  storeGuard guard(busy);
  if (!guard.Held()) return NORMAL_SPEED;
  return speed;
}

bool gSeq::IsTickDriven() const {
  storeGuard guard(busy);
  if (!guard.Held()) return false;
  return speed == TICK_SPEED;
}

std::string gSeq::Filename() const {
  storeGuard guard(busy);
  if (!guard.Held()) return std::string();
  return fileName;
}

#undef className
