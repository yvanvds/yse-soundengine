#include "gQlist.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "../patcherImplementation.h"

using namespace YSE::PATCHER;

#define className gQlist

namespace {

  // A cue that is a whole number only leaves as an int when the int can hold
  // it: casting a float outside the int range is undefined behaviour, and a
  // patch that wrote a huge integer is better served by the float that still
  // carries its value. `.route`, `.coll` and `.textfile` decide the same
  // question the same way.
  bool FitsInt(float value) {
    return value >= -2147483648.f && value < 2147483648.f;
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

  // How many characters of [text, text+length) form the leading run of whole
  // numeric tokens — Max's "lines beginning with a numerical value (or list of
  // numerical values)". 0 when the first token is not a number, `length` (up to
  // trailing separators) when every token is. This is the cut a stored line is
  // split at.
  std::size_t NumericPrefix(const char* text, std::size_t length) {
    std::size_t prefix = 0;
    std::size_t cursor = 0;
    for (;;) {
      std::size_t begin = 0;
      std::size_t end = 0;
      if (!NextToken(text, length, cursor, begin, end)) break;
      float number = 0.f;
      if (!ReadNumericToken(text + begin, end - begin, number)) break;
      prefix = end;
      cursor = end;
    }
    return prefix;
  }

  constexpr char kInletDoc[] =
      "The command inlet. 'bang' plays the whole cue list automatically from the first line — "
      "Max's "
      "'it begins sending messages from the first line, until a line begins with a number, at "
      "which point qlist will use that number as a delay time in milliseconds before continuing to "
      "send the remaining messages' — so a bang always restarts rather than resuming. 'next' steps "
      "by hand: it sends every symbol line it passes remotely and stops after outputting the first "
      "numeric line, which is the number a patch is meant to wait for itself, and 'next 1' skips "
      "the symbol lines instead of sending them. 'fwd <n>' is that repeated n times with nothing "
      "sent remotely — Max's 'fast forward through a given number of lines' — and a bare 'fwd' "
      "moves nothing, Max documenting it as taking a number. 'rewind' puts the cursor back at the "
      "first line, 'stop' ends automatic playback, and 'tempo <f>' scales every wait by dividing: "
      "0.5 plays at half speed and 2 twice as fast, and a tempo of zero or less is refused because "
      "it cannot scale a duration into anything a clock can wait for. 'set' replaces the whole cue "
      "list and with no arguments is the same as 'clear'; 'insert' adds its arguments as a new "
      "entry (Max's own name for an append, reproduced rather than corrected so a patch brought "
      "across builds the same list); 'append' glues its arguments onto the last entry. In all "
      "three, ';' separates cue lines. Max needs a backslash before one, but that is a rule about "
      "the message box that would otherwise eat it — the patcher has no message box, so a "
      "semicolon is written bare. 'read [file]' and 'write [file]' move the cue list through a "
      "text file in Max's own format, one entry per line and semicolon-terminated. Neither opens "
      "anything here: the request is a wait-free claim on a patcher-owned slot, the disk work runs "
      "on the background pool, and a read replaces the list in the completion the patcher delivers "
      "at the top of a later block — which is also when outlet 2 bangs. A read rewinds the cursor "
      "and lets a walk in progress carry on into the list it just loaded; both bare forms reuse "
      "the last name given, since Max's bare forms open a file dialog and a headless patcher has "
      "none, and Max documents no readagain / writeagain for qlist, so neither is invented. "
      "'open' and 'wclose' are consumed and do nothing, there being no editing window here. "
      "Anything else does nothing at all, which is Max — this is a command "
      "inlet, so cue text is written with set, append and insert and never by being sent bare, and "
      "qlist documents no int or float method either. A cue line longer than 256 characters, or "
      "one past the 256th, is refused whole and silently, since the inlet may be the audio thread.";

  constexpr char kDataDoc[] =
      "Numeric cue lines. Max: 'the qlist object outputs a numerical value or list of numerical "
      "values when a line in the cue list begins with a number'. It leaves in the kind it is, "
      ".route's rule — a cue holding '60' as the int 60, one holding '60.5' as that float, and "
      "anything longer as a list. The same line does two jobs and that is the design rather than "
      "an overload: during automatic playback its leading number is also the wait before the walk "
      "goes on, and sending it out is what lets a patch drive the identical list by hand with this "
      "outlet into a delay and the delay's bang back into 'next'. Symbol lines never come out "
      "here; they go to the named receivers.";

  constexpr char kEndDoc[] =
      "Bangs when the walk runs out of cue lines — Max's 'a bang is sent when a cue list has "
      "reached the end, and there are no more lines to send or output'. It fires for a 'next' or "
      "'fwd' that walks off the end just as it does for automatic playback finishing, and it fires "
      "on an empty list, which has reached its end before it starts. Max's third outlet, which "
      "bangs when a file has been read from disk, follows this one (issue #689) — appended rather "
      "than inserted, which is the family's rule and here also Max's own position for it, so no "
      "saved patch's cords shift either way.";

  constexpr char kFileDoc[] =
      "Bangs when a 'read' has finished loading a file into the cue list — Max's third outlet, "
      "which 'bangs when a file has been read successfully from disk'. Appended after the end "
      "outlet rather than inserted, which is the rule the whole file-reading family follows and "
      "which here costs nothing, Max putting it third as well. It fires only on success: a file "
      "that does not exist, does not fit, or cannot be opened leaves it silent, and it does not "
      "fire for a write, for which Max has no outlet either. It fires a block or more after the "
      "read message rather than inside it, because the disk work happens on the background pool — "
      "a read arrives on whichever thread dispatched it, which may be the audio callback.";

} // namespace

CONSTRUCT() {
  // One inlet, as Max has. Everything the object does arrives here, and the
  // leading item is read as a command or not at all: this is a command inlet,
  // the exemption .coll spells out.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);

  // ANY on outlet 0: a numeric cue leaves as a list, an int or a float
  // depending on what it holds. Outlet 1 only ever bangs.
  ADD_OUT_ANY;
  ADD_OUT_BANG;
  // The file outlet, appended after the two #500 shipped rather than inserted
  // (issue #689). Max puts it third as well, so following the family's rule
  // costs nothing here.
  ADD_OUT_BANG;

  // The whole table and every send buffer, taken once here on the control
  // thread. Nothing on a message path ever resizes any of them, which is what
  // makes a store or a step from a rendering graph allocation-free. One past
  // capacity so an entry filled to ENTRY_CAPACITY still has room for the
  // terminator c_str() needs.
  entries.resize(MAX_ENTRIES);
  for (std::string& entry : entries)
    entry.reserve(ENTRY_CAPACITY + 1);
  sendText.reserve(ENTRY_CAPACITY + 1);
  sendName.reserve(ENTRY_CAPACITY + 1);
  sendPayload.reserve(ENTRY_CAPACITY + 1);
  // An append has to hold the old entry and the new text at once before the
  // splitter can decide where the line breaks are.
  joinText.reserve(JOIN_CAPACITY + 1);

  // Same treatment for the file buffers (issue #689): a `read` or `write` may
  // arrive on the audio thread, so remembering a name and formatting the whole
  // cue list both have to reuse storage that already exists.
  readPath.reserve(fileScheduler::PATH_CAPACITY);
  writePath.reserve(fileScheduler::PATH_CAPACITY);
  fileScratch.reserve(FILE_TEXT_CAPACITY + 1);

  ADD_DESCRIPTION(
      "Stores a sequence of messages and plays it back in time — Max's qlist, which 'stores a "
      "collection of timed or untimed cues in the form of messages which can be sent either out "
      "its outlet or remotely to various receive objects'. Every store before it is passive: "
      ".coll answers a lookup, .bag a bang, and .capture, .funbuff, .table and .textfile a dump, "
      "and none of them has any notion of when. This one has a clock, so a scene, a scripted "
      "parameter sweep or a note sequence is one object holding one block of text — and the timing "
      "is data in the list rather than a setting on the object, which is what makes it a cue list "
      "rather than a collection. There are three kinds of line, which is the whole grammar: a line "
      "of only numbers is sent out outlet 0 and, during automatic playback, its leading number is "
      "the wait in milliseconds before the walk continues; a line beginning with a symbol is sent "
      "remotely to the receiver of that name, through the same door .s uses, and does not stop the "
      "walk; and a line of numbers followed by a message is, in Max's words, 'treated as two "
      "separate lines'. That last rule is taken literally — a line is split when it is stored, "
      "never on the way past — so the cursor is one index into one flat table and every traversal "
      "walks the same thing. 'next' is the manual mode, sending symbol lines as it passes them and "
      "stopping after it outputs a numeric one, which is the classic Max idiom of outlet 0 into a "
      "delay and the delay's bang back into next; 'fwd n' repeats that n times sending nothing "
      "remotely; 'bang' is the automatic mode, rewinding first and then waiting out each numeric "
      "line itself; 'stop' ends it, 'rewind' moves the cursor, and 'tempo' divides every wait so "
      "that 2 plays twice as fast. Reaching the end bangs outlet 1 whichever mode got there. The "
      "waiting is done by the patcher's own deferred-message scheduler (issue #628), the mechanism "
      ".bondo defers through: arming is wait-free and allocation-free so a bang from a rendering "
      "graph may arm one, delivery happens inside a real dispatch frame so each resumed step is "
      "one logical event downstream, and the deadline floor of one audio block means a cue list of "
      "zero delays advances one entry per block instead of spinning. Issue #500 suggests driving "
      "playback from a YSE domain clock instead; that is deliberately not done, because Max's cue "
      "list is milliseconds and its tempo is a bare multiplier with no beat or meter in it, so a "
      "beat-driven version would be a different object wearing Max's name — it is filed as #688 "
      "along with the patcher-to-domain-clock bridge it would need. The store is .coll's model and "
      "for .coll's reason — a fixed table allocated whole at construction plus .value's "
      "non-blocking guard, whose loser drops rather than waiting — because a copy-on-write "
      "GraphState publish assumes the writer is the control thread while this object is written by "
      "whichever thread its message arrived on. The guard is never held across a send, so a walk "
      "takes it once per entry and re-reads the bounds each step. At most 256 cue lines of at most "
      "256 characters; anything that does not fit is refused whole rather than truncated, since "
      "half a cue is a different cue. Calculate() does nothing. The contents survive a DumpJSON / "
      "ParseJSON round trip, which is the family's rule of saving exactly where Max has a save "
      "flag and here Max could not be plainer — 'the qlist object saves its cue-list with the "
      "patcher' — so this saves like .coll rather than like .textfile, whose contents live in a "
      "file. The cursor, the tempo and whether it is playing are run-time state and do not "
      "survive, the way .coll's pointer does not. 'read [file]' and 'write [file]' move the cue "
      "list through a text file in Max's format — one entry per line, semicolon-terminated — and "
      "none of it happens on the message path: a handler runs on whichever thread the message "
      "arrived on, in-patcher delivery dispatches on the audio thread, and THREAD is a "
      "dispatch-semantics tag rather than a thread identity, so there is no predicate an object "
      "can ask to find out it is not on the audio callback, where opening a file would block it. "
      "The request is instead a wait-free claim on a patcher-owned slot, the disk work runs on the "
      "background pool honouring the host's IO() layer, and the bytes are parsed in the completion "
      "the patcher delivers at the top of a later block, which is also when outlet 2 bangs — the "
      "shared fileScheduler plumbing of issue #683, whose consumer half here is #689. A read "
      "breaks lines on a semicolon or a newline alike, so a hand-written file with neither the "
      "semicolons nor the CRLF of the writer's own output still loads as the lines it looks like, "
      "and it splits a numbers-then-message line into two entries exactly as the inlet does, so "
      "what a write emitted comes back as the same list. A read replaces what is held and rewinds "
      "the cursor, but does not stop a walk in progress — the walk carries on into the list just "
      "loaded; an over-long line is refused with the rest of the file still loading, lines past "
      "the 256th are dropped, and a file too large or unopenable is refused whole with outlet 2 "
      "silent. Both bare forms reuse the last name given, since Max's open a file dialog and a "
      "headless patcher has none, and Max documents no readagain / writeagain for qlist, so "
      "neither is invented. There is deliberately no filename creation argument either — Max "
      "documents none, and unlike .textfile this object saves its cue list with the patcher, so an "
      "object that also read a file when it joined one would have two answers to what is in the "
      "list arriving in an order nothing controls. Max's third outlet, which bangs when a file has "
      "been read successfully from disk, is appended after the end outlet rather than inserted, "
      "which is the family's rule and here also Max's own position for it. Not ported: the editing "
      "window and everything addressing it, the patcher being headless.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "in", kInletDoc, "at most 256 cue lines of 256 characters");
  OUTLET_DOC(0, "data", kDataDoc, "");
  OUTLET_DOC(1, "end", kEndDoc, "");
  OUTLET_DOC(2, "file", kFileDoc, "");
}

// ─── writing the list ─────────────────────────────────────────────────────────

bool gQlist::PushEntry(const char* text, std::size_t length) {
  // Full, or longer than an entry can hold: refused rather than grown or
  // truncated, since growing the table would allocate on whichever thread the
  // message arrived on and half a cue is a different cue.
  if (count >= MAX_ENTRIES) return false;
  if (length > ENTRY_CAPACITY) return false;

  // assign() into a string reserved to ENTRY_CAPACITY + 1 at construction, so
  // this allocates nothing.
  entries[count].assign(text, length);
  count++;
  return true;
}

void gQlist::StoreLine(const char* text, std::size_t length) {
  std::size_t begin = 0;
  std::size_t end = length;
  Trim(text, begin, end);
  if (end <= begin) return;

  const char* line = text + begin;
  const std::size_t size = end - begin;

  // Max: a line of numbers followed by a message is "treated as two separate
  // lines - the first part (all numerical) is sent out the left outlet, and the
  // second (message) part is remotely sent to a receive object". Done here,
  // once, rather than at every traversal.
  const std::size_t numeric = NumericPrefix(line, size);
  std::size_t restBegin = numeric;
  std::size_t restEnd = size;
  Trim(line, restBegin, restEnd);
  const std::size_t rest = restEnd > restBegin ? restEnd - restBegin : 0;

  // Refused whole, both halves together: a line that only half fits would leave
  // the numbers behind without the message they timed, which is a different
  // cue list rather than a shorter one.
  const std::size_t needed = (numeric > 0 ? 1u : 0u) + (rest > 0 ? 1u : 0u);
  if (count + needed > MAX_ENTRIES) return;
  if (numeric > ENTRY_CAPACITY || rest > ENTRY_CAPACITY) return;

  if (numeric > 0) PushEntry(line, numeric);
  if (rest > 0) PushEntry(line + restBegin, rest);
}

void gQlist::AddLines(const char* text, std::size_t length) {
  // Max's own separator: "each line of the cue-list is a message ending in a
  // semicolon". Written bare here — see the class documentation on the
  // backslash Max needs and this patcher does not.
  std::size_t segment = 0;
  for (std::size_t i = 0; i <= length; i++) {
    if (i == length || text[i] == ';') {
      StoreLine(text + segment, i - segment);
      segment = i + 1;
    }
  }
}

void gQlist::Insert(const char* text, std::size_t length) {
  storeGuard guard(busy);
  if (!guard.Held()) return;
  AddLines(text, length);
}

void gQlist::AppendToLast(const char* text, std::size_t length) {
  storeGuard guard(busy);
  if (!guard.Held()) return;

  // Nothing to append to: Max's append on an empty cue list can only be its
  // insert.
  if (count == 0) {
    AddLines(text, length);
    return;
  }

  // The last entry comes back off the list and the joined text goes through the
  // same splitter as any other line, because numbers followed by a message are
  // two lines however they came to be written that way — an append onto a
  // numeric entry is exactly how that happens.
  const std::string& last = entries[count - 1];
  if (last.size() + 1 + length > JOIN_CAPACITY) return;
  joinText.assign(last);
  joinText.push_back(' ');
  joinText.append(text, length);
  count--;
  AddLines(joinText.c_str(), joinText.size());
}

void gQlist::Clear() {
  // A cleared list has nothing left to walk, so a walk in progress ends with
  // it rather than resuming into an empty table.
  playing.store(false, std::memory_order_relaxed);
  CancelPending();

  storeGuard guard(busy);
  if (!guard.Held()) return;
  // The strings keep their storage: only `count` says which entries are live,
  // so clearing is O(1) and the capacity the next store needs is still there.
  count = 0;
  position = 0;
}

// ─── playing it ───────────────────────────────────────────────────────────────

gQlist::Step gQlist::Advance(bool ignoreSymbols, float& wait, YSE::THREAD thread) {
  {
    storeGuard guard(busy);
    if (!guard.Held()) return Step::DROP;
    if (position >= count) return Step::END;

    // Copied out under the guard and sent after it: holding it across a
    // synchronous fan-out would make a patch that wires an outlet, or a `.r`,
    // back into this object lose its own message to the guard this object is
    // still holding. The buffer was reserved at construction, so this allocates
    // nothing.
    sendText.assign(entries[position]);
    position++;
  }

  // Classified outside the guard, on this object's own buffer. A leading number
  // means the whole line is numbers, because the line was split when it was
  // stored.
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(sendText.c_str(), sendText.size(), 0, begin, end)) return Step::SENT;

  float leading = 0.f;
  if (ReadNumericToken(sendText.c_str() + begin, end - begin, leading)) {
    // Max: "qlist will use that number as a delay time in milliseconds" — the
    // number the line *begins* with, so a numeric list waits out its first
    // value and sends all of them.
    wait = leading;
    SendTyped(0, sendText, thread);
    return Step::OUTPUT;
  }

  // Max's `fwd`, and `next` with a non-zero argument: the symbol lines are
  // stepped over rather than sent.
  if (!ignoreSymbols) SendRemote(sendText, thread);
  return Step::SENT;
}

bool gQlist::Next(bool ignoreSymbols, YSE::THREAD thread) {
  // Bounded by the table: every step either advances the cursor or ends the
  // walk, so MAX_ENTRIES symbol lines is the worst case before the end arrives.
  for (std::size_t steps = 0; steps <= MAX_ENTRIES; steps++) {
    float wait = 0.f;
    switch (Advance(ignoreSymbols, wait, thread)) {
    case Step::OUTPUT:
      // Max: "stop after it encounters and outputs a line beginning with a
      // numerical value". The number it stopped on has already gone out; what
      // to do with it is the patch's business in this mode.
      return true;
    case Step::SENT:
      break;
    case Step::END:
      outputs[1].SendBang(thread);
      return false;
    case Step::DROP:
      return false;
    }
  }
  return false;
}

void gQlist::Resume(YSE::THREAD thread) {
  for (std::size_t steps = 0; steps <= MAX_ENTRIES; steps++) {
    float wait = 0.f;
    switch (Advance(false, wait, thread)) {
    case Step::OUTPUT:
      if (ArmContinue(wait)) return;
      // No scheduler (a standalone object has no dispatch to defer into) or the
      // pending set was full: keep walking with no wait rather than abandoning
      // the sequence half-played. `.bondo`'s fallback, for `.bondo`'s reason.
      break;
    case Step::SENT:
      break;
    case Step::END:
      playing.store(false, std::memory_order_relaxed);
      outputs[1].SendBang(thread);
      return;
    case Step::DROP:
      // The guard went to another thread. A dropped step cannot be retried —
      // there is nothing armed to retry it — so the walk ends here rather than
      // stalling with `playing` still set and no clock behind it.
      playing.store(false, std::memory_order_relaxed);
      return;
    }
  }
  playing.store(false, std::memory_order_relaxed);
}

bool gQlist::ArmContinue(float waitMs) {
  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) return false;

  // Max's tempo is a speed, so it divides: "a tempo of 0.5 plays back the cue
  // list at half speed, whereas a tempo of 2. plays it back twice as fast."
  // `tempo` is never zero or negative, which is what makes this safe.
  const float scaled = waitMs / tempo.load(std::memory_order_relaxed);
  int delay = 0;
  if (scaled >= 2147483647.f) {
    delay = 2147483647;
  } else if (scaled > 0.f) {
    delay = (int)scaled;
  }
  // Everything else — zero, negative, or a NaN that no comparison accepts — is
  // 0, which the scheduler floors to one audio block.

  // One clock per object, Max's shape: whatever was pending is replaced.
  CancelPending();
  const messageScheduler::Handle armed = scheduler->ScheduleBang(this, 0, delay);
  pending.store(armed, std::memory_order_relaxed);
  return armed != 0;
}

void gQlist::CancelPending() {
  const messageScheduler::Handle armed = pending.exchange(0, std::memory_order_relaxed);
  if (armed == 0) return;
  messageScheduler* scheduler = Scheduler();
  if (scheduler != nullptr) scheduler->Cancel(armed);
}

void gQlist::DeliverDeferred(const deferredMessage&, YSE::THREAD thread) {
  // The wait has elapsed. The scheduler wraps this in a fresh messageEventScope,
  // so everything the resumed step goes on to cause is one logical event (#628).
  pending.store(0, std::memory_order_relaxed);
  if (!playing.load(std::memory_order_relaxed)) return;

  // The delivered tag is passed straight through, outlet half and remote half
  // alike. It said T_GUI — "let the block's own traversal render it" — and
  // until #690 the remote half had to be forced to T_DSP to keep
  // `patcherImplementation::PassData` off `mtx` on the audio callback. PassData
  // now asks `CallingThread` which thread it is really on, so the tag is free
  // to mean only what it says.
  Resume(thread);
}

// ─── sending ──────────────────────────────────────────────────────────────────

void gQlist::SendTyped(std::size_t pin, const std::string& text, YSE::THREAD thread) {
  // `.route`'s rule, shared with `.coll` and `.textfile`: a cue leaves in the
  // kind it is.
  const std::size_t length = text.size();
  float number = 0.f;
  if (length > 0 && ReadNumericToken(text.c_str(), length, number)) {
    // Int or float is decided by the spelling, the test `.trigger`, `.match`,
    // `.route` and `.coll` already share, so a cue holding `60` does not come
    // back as `60.`.
    if (!TokenLooksLikeFloat(text.c_str(), length) && FitsInt(number)) {
      outputs[pin].SendInt((int)number, thread);
    } else {
      outputs[pin].SendFloat(number, thread);
    }
    return;
  }
  outputs[pin].SendList(text, thread);
}

void gQlist::SendRemote(const std::string& text, YSE::THREAD thread) {
  // A standalone object has no patcher to address a receiver in. Max's remote
  // send has nothing to reach either, so this is silence rather than a fallback.
  if (parent == nullptr) return;

  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(text.c_str(), text.size(), 0, begin, end)) return;
  // Into a buffer reserved at construction, and a different one from `text`,
  // which is this object's send buffer.
  sendName.assign(text, begin, end - begin);

  std::size_t restBegin = end;
  std::size_t restEnd = text.size();
  Trim(text.c_str(), restBegin, restEnd);

  // `parent` is a patcherImplementation by construction — the patcher hands
  // itself to every object it creates — which is the cast gSend already makes.
  auto* p = static_cast<patcherImplementation*>(parent);

  if (restEnd <= restBegin) {
    // A bare name is a bang at the far end, the way `; foo` is in Max.
    p->PassBang(sendName, thread);
    return;
  }

  const char* rest = text.c_str() + restBegin;
  const std::size_t restLength = restEnd - restBegin;

  // In the kind the remainder is, so a `.r` downstream sees the int the cue
  // wrote rather than a one-element list. `.route`'s rule again, on the remote
  // side of it.
  float number = 0.f;
  if (ReadNumericToken(rest, restLength, number)) {
    if (!TokenLooksLikeFloat(rest, restLength) && FitsInt(number)) {
      p->PassData((int)number, sendName, thread);
    } else {
      p->PassData(number, sendName, thread);
    }
    return;
  }

  sendPayload.assign(text, restBegin, restLength);
  p->PassData(sendPayload, sendName, thread);
}

// ─── commands ─────────────────────────────────────────────────────────────────

bool gQlist::HandleCommand(const char* word, std::size_t length, const std::string& message,
                           std::size_t argOffset, YSE::THREAD thread) {
  if (TokenIs(word, length, "next", 4)) {
    // Max: "if the word next is followed by a non-zero argument, it will ignore
    // lines beginning with symbols and only output the next line beginning with
    // a numerical value."
    int ignoreSymbols = 0;
    ReadIntArg(message, argOffset, ignoreSymbols);
    Next(ignoreSymbols != 0, thread);
    return true;
  }

  if (TokenIs(word, length, "fwd", 3)) {
    // Max documents `fwd` as "followed by a number"; a bare one has no distance
    // to travel, so it moves nothing rather than guessing at 1.
    int lines = 0;
    if (!ReadIntArg(message, argOffset, lines)) return true;
    // "Fast forward through a given number of lines, without remotely sending
    // messages to named receive objects ... lines beginning with symbols will
    // be ignored" — which is `next`'s ignore-symbols mode, repeated. Stopping
    // at the end keeps it from banging outlet 1 once per remaining repeat.
    for (int i = 0; i < lines; i++) {
      if (!Next(true, thread)) break;
    }
    return true;
  }

  if (TokenIs(word, length, "rewind", 6)) {
    // "Move to the beginning of the file." A cursor move and nothing else —
    // `stop` is what ends a walk, and a rewind during one deliberately lets it
    // carry on from the top.
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    position = 0;
    return true;
  }

  if (TokenIs(word, length, "stop", 4)) {
    // "Stop a qlist which is in the middle of playback as a result of a bang
    // message." The cursor stays put, so a following `next` carries on from
    // where the clock left off.
    playing.store(false, std::memory_order_relaxed);
    CancelPending();
    return true;
  }

  if (TokenIs(word, length, "clear", 5)) {
    Clear();
    return true;
  }

  if (TokenIs(word, length, "set", 3)) {
    // "Set the contents of a qlist object. It completely clears any previous
    // cue-list contents. Sending a set message with no arguments is the same as
    // sending a clear message."
    Clear();
    std::size_t begin = argOffset;
    std::size_t end = message.size();
    Trim(message.c_str(), begin, end);
    if (end > begin) Insert(message.c_str() + begin, end - begin);
    return true;
  }

  if (TokenIs(word, length, "insert", 6)) {
    // "Append those arguments to the qlist object's cue-list as a new entry in
    // the list" — an append despite the name, which is Max's and is reproduced
    // rather than corrected.
    std::size_t begin = argOffset;
    std::size_t end = message.size();
    Trim(message.c_str(), begin, end);
    if (end > begin) Insert(message.c_str() + begin, end - begin);
    return true;
  }

  if (TokenIs(word, length, "append", 6)) {
    // "Append those arguments to the last entry in the qlist object's
    // cue-list."
    std::size_t begin = argOffset;
    std::size_t end = message.size();
    Trim(message.c_str(), begin, end);
    if (end > begin) AppendToLast(message.c_str() + begin, end - begin);
    return true;
  }

  if (TokenIs(word, length, "tempo", 5)) {
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!NextToken(message.c_str(), message.size(), argOffset, begin, end)) return true;
    float value = 0.f;
    if (!ReadNumericToken(message.c_str() + begin, end - begin, value)) return true;
    // A tempo of zero or less cannot scale a duration into anything a clock can
    // wait for, so it is refused and the previous one kept.
    if (value <= 0.f) return true;
    tempo.store(value, std::memory_order_relaxed);
    return true;
  }

  // The file half (issue #689). Both are a claim on a patcher-owned slot and
  // nothing more: whichever thread is dispatching, no file is opened here. The
  // whole remainder is the path, so a name with spaces in it still works, and a
  // bare form reuses the last name given — Max's opens a file dialog, which a
  // headless patcher has no equivalent of.
  const bool isRead = TokenIs(word, length, "read", 4);
  if (isRead || TokenIs(word, length, "write", 5)) {
    const FILE_OP op = isRead ? FILE_OP::READ : FILE_OP::WRITE;
    std::size_t begin = argOffset;
    std::size_t end = message.size();
    Trim(message.c_str(), begin, end);
    RequestFile(op, message.c_str() + begin, end - begin);
    return true;
  }

  // Consumed and inert: there is no editing window to open, close or
  // double-click.
  if (TokenIs(word, length, "open", 4)) return true;
  if (TokenIs(word, length, "wclose", 6)) return true;

  return false;
}

// ─── files ────────────────────────────────────────────────────────────────────

bool gQlist::RequestFile(FILE_OP op, const char* name, std::size_t length) {
  fileScheduler* io = FileIO();
  // A standalone .qlist has no patcher and so no plumbing. Silent: this may be
  // the audio thread, where a log line would allocate.
  if (io == nullptr) return false;

  std::string& remembered = op == FILE_OP::READ ? readPath : writePath;
  if (name != nullptr && length > 0) {
    if (length >= fileScheduler::PATH_CAPACITY) return false;
    // assign() into a string reserved at construction reuses its storage.
    remembered.assign(name, length);
  }
  // Nothing named yet, and no dialog to ask with.
  if (remembered.empty()) return false;

  if (op == FILE_OP::READ) {
    return io->RequestRead(this, FILE_TAG_READ, remembered.c_str(), remembered.size());
  }

  // The bytes are built here rather than on the pool thread, because the pool
  // must never touch this object: by the time the job runs, a live edit may have
  // deleted it.
  if (!Serialize()) return false;
  return io->RequestWrite(this, FILE_TAG_WRITE, remembered.c_str(), remembered.size(),
                          fileScratch.c_str(), fileScratch.size());
}

bool gQlist::Serialize() {
  storeGuard guard(busy);
  if (!guard.Held()) return false;

  // clear() keeps the capacity reserved at construction, so every append below
  // writes into storage that already exists. FILE_TEXT_CAPACITY is every entry
  // at its maximum plus its terminator, so the buffer cannot run out.
  fileScratch.clear();
  for (std::size_t i = 0; i < count; i++) {
    fileScratch.append(entries[i]);
    // Max's format: "each line of the cue-list is a message ending in a
    // semicolon". No entry can hold one of its own — an entry is what splitting
    // on `;` produced — so the round trip is exact.
    fileScratch.append(";\n", 2);
  }
  return true;
}

bool gQlist::LoadFrom(const char* text, std::size_t length) {
  storeGuard guard(busy);
  if (!guard.Held()) return false;

  // Max's read replaces the contents, and the cursor goes with them: it pointed
  // into a list that no longer exists. The strings keep their storage — only
  // `count` says which entries are live — so this costs nothing.
  count = 0;
  position = 0;

  // Broken on a semicolon *or* a newline, where `AddLines` takes only the
  // semicolon: a written cue line can carry punctuation the file's line breaks
  // must not steal, but a file has no such ambiguity — see the class
  // documentation. Each segment then goes through the same StoreLine the inlet
  // uses, so a numbers-then-message line in a file splits into two entries the
  // way it does when it is typed, and a `\r` before the break is trimmed with
  // the rest of the whitespace.
  std::size_t segment = 0;
  for (std::size_t i = 0; i <= length; i++) {
    if (i == length || text[i] == ';' || text[i] == '\n') {
      StoreLine(text + segment, i - segment);
      segment = i + 1;
    }
  }
  return true;
}

void gQlist::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // Control thread, and the one place the patcher's file table can be built: a
  // `read` arriving later on the audio thread has to find it already there
  // (issue #683). Nothing is read here — Max gives `qlist` no filename argument,
  // and this object restores its cue list from the patch instead.
  EnableFileIO();
}

void gQlist::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  // Max has no outlet for a finished write, so a write reports only by having
  // happened. A failed read reports by the outlet staying silent.
  if (result.op != FILE_OP::READ || result.tag != FILE_TAG_READ) return;
  if (!result.ok || result.bytes == nullptr) return;
  if (!LoadFrom(result.bytes, result.byteCount)) return;

  // Max's third outlet: "bangs when a file has been read successfully from
  // disk". After the list is in place, so a patch that reacts to it with a
  // `bang` or a `next` finds it. `thread` is the tag the scheduler delivered —
  // forwarded unchanged, because Pass* picks its mechanism from the render-frame
  // marker rather than from the tag (issue #690).
  outputs[2].SendBang(thread);
}

// ─── inlet ────────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  // Max: "sending a bang to qlist triggers automatic-playback of the entire cue
  // list. It begins sending messages from the first line" — so a bang always
  // restarts, and a stopped list resumed with one starts over rather than
  // continuing.
  {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    position = 0;
  }
  CancelPending();
  playing.store(true, std::memory_order_relaxed);
  Resume(thread);
}

LIST_IN(ListIn) {
  (void)inlet;
  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(text, length, 0, begin, end)) return;

  // A command inlet: a message that is none of the words does nothing at all,
  // which is Max. Cue text is written with `set`, `append` and `insert`, never
  // by being sent bare — see the class documentation on why reserving words
  // here is legitimate where `.prepend` and `.atoi` may not.
  HandleCommand(text + begin, end - begin, value, end, thread);
}

// ─── persistence ──────────────────────────────────────────────────────────────

void gQlist::DumpState(nlohmann::json::value_type& json) {
  // Max: "the qlist object saves its cue-list with the patcher" — the plainest
  // save flag in the family, so the contents ride a DumpJSON the way `.coll`'s
  // do. Control thread (patcherImplementation::DumpJSON holds mtx), but the
  // guard is still taken, because a message may be arriving from a rendering
  // graph while the patch is being saved.
  storeGuard guard(busy);
  if (!guard.Held()) return;
  if (count == 0) return;

  for (std::size_t i = 0; i < count; i++)
    json["cues"].push_back(entries[i]);
}

void gQlist::RestoreState(const nlohmann::json::value_type& json) {
  const auto stored = json.find("cues");
  if (stored == json.end() || !stored->is_array()) return;

  storeGuard guard(busy);
  if (!guard.Held()) return;

  count = 0;
  position = 0;
  for (const auto& cue : *stored) {
    if (!cue.is_string()) continue;
    // Written back entry by entry rather than through the splitter: what was
    // saved was already split, and re-splitting it would be a second pass over
    // lines that have no boundary left to find.
    const std::string text = cue.get<std::string>();
    PushEntry(text.c_str(), text.size());
  }
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::size_t gQlist::Count() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return count;
}

std::string gQlist::EntryAt(std::size_t index) const {
  storeGuard guard(busy);
  if (!guard.Held()) return std::string();
  if (index >= count) return std::string();
  return entries[index];
}

std::size_t gQlist::Position() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return position;
}

#undef className
