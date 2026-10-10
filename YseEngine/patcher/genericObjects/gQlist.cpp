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
      "A cue list: lines of numbers and messages played back in time. A line of numbers is "
      "sent out and its first number is the wait before the next line; a line starting with a "
      "word is sent to the receivers of that name.");
  ADD_CATEGORY(pCategory::SEQUENCE);
  INLET_DOC(0, "in",
            "bang plays the list from the top; 'next' steps by hand. Also 'stop', 'rewind', "
            "'fwd', 'tempo', 'clock', 'set', 'insert', 'append', 'clear', 'read' and 'write'.",
            "at most 256 cue lines of 256 characters");
  OUTLET_DOC(0, "data", "Lines of numbers, as an int, a float or a list.", "");
  OUTLET_DOC(1, "end", "A bang when playback or 'next' reaches the end of the list.", "");
  OUTLET_DOC(2, "file", "A bang when a 'read' has loaded a file. Silent when the read fails.", "");
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

bool gQlist::ArmContinue(float wait) {
  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) return false;

  // Max's tempo is a speed, so it divides: "a tempo of 0.5 plays back the cue
  // list at half speed, whereas a tempo of 2. plays it back twice as fast."
  // `tempo` is never zero or negative, which is what makes this safe. It scales
  // a beat wait exactly as it scales a millisecond one — the unit changes,
  // the multiplier does not (issue #688).
  const float scaled = wait / tempo.load(std::memory_order_relaxed);

  // One clock per object, Max's shape: whatever was pending is replaced.
  CancelPending();

  const clockBridge::Handle onClock = binding.load(std::memory_order_relaxed);
  messageScheduler::Handle armed = 0;
  if (onClock != 0) {
    // Beats on the bound domain clock. Anything that is not a positive number —
    // zero, negative, NaN — is 0 beats, which the scheduler treats the way it
    // treats a delay of 0: due at the next drain, never inside this dispatch.
    armed = scheduler->ScheduleBangOnClock(this, 0, onClock, scaled > 0.f ? (double)scaled : 0.0);
  } else {
    int delay = 0;
    if (scaled >= 2147483647.f) {
      delay = 2147483647;
    } else if (scaled > 0.f) {
      delay = (int)scaled;
    }
    // Everything else — zero, negative, or a NaN that no comparison accepts —
    // is 0, which the scheduler floors to one audio block.
    armed = scheduler->ScheduleBang(this, 0, delay);
  }

  pending.store(armed, std::memory_order_relaxed);
  return armed != 0;
}

void gQlist::SetClock(const char* name, std::size_t length) {
  // A bare `clock` goes back to Max's milliseconds. The cursor and whatever is
  // pending are left alone: this changes the unit of the *next* wait, the way
  // `tempo` changes its scale, and a step already armed leaves when it was
  // armed to leave.
  if (name == nullptr || length == 0) {
    binding.store(0, std::memory_order_relaxed);
    return;
  }

  // A standalone object has no patcher and so no bridge, exactly as it has no
  // scheduler to defer into. Silent, since this may be the audio thread.
  clockBridge* clocks = Clocks();
  if (clocks == nullptr) return;

  // Wait-free: a bounded walk over the patcher's binding table and a memcpy of
  // the name into a slot that already exists. The name is *not* looked up here
  // — that takes the clock manager's mutex and happens on the background pool.
  const clockBridge::Handle bound = clocks->Bind(name, length);
  // A refusal (the table is full, or the name is longer than a slot holds)
  // leaves the object on whatever clock it was on rather than silently falling
  // back to milliseconds, which would change what every stored number means.
  if (bound == 0) return;
  binding.store(bound, std::memory_order_relaxed);
}

const char* gQlist::ClockName() const {
  const clockBridge::Handle bound = binding.load(std::memory_order_relaxed);
  if (bound == 0) return "";
  const clockBridge* clocks = Clocks();
  if (clocks == nullptr) return "";
  return clocks->NameOf(bound);
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

  if (TokenIs(word, length, "clock", 5)) {
    // The domain-clock opt-in (issue #688). `clock <name>` puts every following
    // wait on that clock in beats; a bare `clock` puts it back on Max's
    // milliseconds. Max's own vocabulary — `setclock` documents its name as
    // something "passed as the argument to a 'clock' message to numerous
    // objects that use timing in Max" — though Max's `qlist` reference lists no
    // `clock` method of its own, so this is an addition and not a port. The
    // whole remainder is the name, so a clock named with spaces still works.
    std::size_t begin = argOffset;
    std::size_t end = message.size();
    Trim(message.c_str(), begin, end);
    SetClock(message.c_str() + begin, end > begin ? end - begin : 0);
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

bool gQlist::DumpState(nlohmann::json::value_type& json) {
  // Max: "the qlist object saves its cue-list with the patcher" — the plainest
  // save flag in the family, so the contents ride a DumpJSON the way `.coll`'s
  // do. Control thread (patcherImplementation::DumpJSON holds mtx), but the
  // guard is still taken, because a message may be arriving from a rendering
  // graph while the patch is being saved. Through saveGuard, which waits that
  // message out rather than skipping the contents (issue #940).
  const saveGuard guard(busy);
  if (!guard.Held()) return false;
  if (count == 0) return true;

  for (std::size_t i = 0; i < count; i++)
    json["cues"].push_back(entries[i]);
  return true;
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
