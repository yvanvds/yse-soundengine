#include "gCapture.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include <climits>

using namespace YSE::PATCHER;

#define className gCapture

namespace {

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

  // Whether the `length` characters at `text` are exactly `word`.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  constexpr char kInletDoc[] =
      "Everything that arrives here is recorded, in arrival order — Max's 'numbers or symbols are "
      "stored in the order in which they are received'. One item is one atom, so a list is "
      "recorded "
      "as its separate elements, Max's 'all numbers and/or symbols in the list are stored in order "
      "from first to last', and each keeps the kind it arrived as. When the record is full the "
      "oldest item is dropped as each new one arrives: Max's 'the earliest stored item is dropped "
      "as each new item is received'. Recording sends nothing. 'dump' sends the whole record out "
      "outlet 0, one item at a time, oldest first; 'clear' erases it; 'count' reports how many "
      "items have arrived since the last count out outlet 1 and zeroes that meter, and 'count 1' "
      "reports it without zeroing it. 'open', 'wclose' and 'write' are consumed and do nothing — "
      "there is no window here and no file I/O on a path that may be the audio thread — and they "
      "are consumed rather than recorded so that the trace matches Max's, where a selector can "
      "never be stored. A bang is not one of Max's messages and does nothing; 'dump' is how a "
      "patch reads the record. A symbol longer than 64 characters is refused whole rather than "
      "truncated.";

  constexpr char kDumpDoc[] =
      "The recorded items, in the order they arrived, one at a time in response to 'dump' — Max's "
      "'outputs the contents of the capture object, one item at a time, out the left outlet'. Each "
      "leaves as the kind it was recorded as: an int as an int, a float as a float, and a symbol "
      "as a one-element list, since the patcher has no symbol atom. Nothing else ever sends here — "
      "recording is silent, and a full record wraps rather than announcing it.";

  constexpr char kCountDoc[] =
      "How many items have arrived since the last 'count' zeroed the meter, in response to a "
      "'count' message — Max's 'the number of items received since last count message was "
      "received'. Not a fill level: it counts arrivals, including the ones the ring has already "
      "dropped, so it can report more than the object is holding. The meter is zeroed by the same "
      "message unless a non-zero flag is given ('count 1' reads it and leaves it standing).";

} // namespace

CONSTRUCT() {
  // The capacity is built by ParseParams, so a saved `.capture 32` comes back
  // holding 32. The clear callback is what makes `SetParams("")` return the
  // object to Max's no-argument shape rather than leaving the previous capacity
  // in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // One inlet, as Max has: every message and every value arrives here.
  ADD_IN_0;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // Outlet 0 carries whatever was recorded, which may be an int, a float or a
  // symbol; outlet 1 carries the count, which is always an int. Max's left and
  // right outlets, in that order.
  ADD_OUT_ANY;
  ADD_OUT_INT;

  // The whole table and the send buffer, taken once here on the control thread.
  // Nothing on a message path ever resizes either, which is what makes
  // recording from a rendering graph allocation-free.
  items.resize(MAX_ITEMS);
  for (std::size_t i = 0; i < MAX_ITEMS; i++) {
    items[i].text.reserve(ITEM_CAPACITY);
  }
  sendText.reserve(ITEM_CAPACITY);

  ADD_DESCRIPTION(
      "Records every value that reaches its inlet in a bounded ring, so a patch can be run and "
      "only then asked what went past — Max's capture, which 'stores items in the order they are "
      "received for viewing, editing, and saving'. The patcher's debugging instrument: .print says "
      "what is passing now, one line at a time, and by the time a graph has gone wrong the "
      "interesting values are already gone. It is not .coll or .bag, which a patch writes by "
      "intent and whose contents are the patch's data; nothing decides what goes in here, so it is "
      "a tape rather than a table. Nor is it .bucket, which also remembers the last N values but "
      "hands them to N outlets as a shift register. One item is one atom: Max's list method stores "
      "'all numbers and/or symbols in the list ... in order from first to last', so a "
      "three-element "
      "list is three items and a dump hands them back one at a time, which is what makes the "
      "object useful as a trace. Each item leaves as the kind it arrived as — .route's rule — and "
      "a "
      "stored symbol leaves as a one-element list, since the patcher has no symbol atom; Max "
      "prefixes it with the word 'symbol', which is Max's way of restoring an atom type this "
      "patcher does not have. The first creation argument is the capacity, Max's 'the first "
      "argument sets a maximum number of items to store; if there is no argument, capture will "
      "store up to 512 items', and the record wraps rather than refusing: 'once the maximum has "
      "been exceeded, the earliest stored item is dropped as each new item is received', which is "
      "the opposite of .coll and .bag and is right for a trace, where the recent past is the "
      "interesting part. 512 is both Max's default and the ceiling here — the table is allocated "
      "whole at construction and a smaller argument only lowers how much of it is used, so no "
      "capacity a patch can ask for ever allocates, and a larger one is clamped the way .bucket "
      "clamps its outlet count. Max's second argument, the a / x / m display format, changes only "
      "how numbers are drawn in the editing window and so is accepted and ignored, which keeps a "
      "'.capture 512 x' brought across from Max building the object it names. 'dump' sends the "
      "whole record out outlet 0 oldest first, 'clear' erases it, and 'count' is the one message "
      "whose name invites a wrong guess: Max's 'the number of items collected since the last count "
      "message', reset to 0 on receipt 'unless flag is set', so it is a since-you-last-asked meter "
      "and not a fill level and can report far more than the object holds. There is deliberately "
      "no "
      "'length' — Max does not give capture one, and a second message answering a different "
      "question on the same inlet would be a trap. 'open', 'wclose' and 'write' are reserved and "
      "consumed silently: Max dispatches on the selector, so a capture in Max cannot store those "
      "symbols either, and an object that stored them would produce a different trace from Max's "
      "for the same patch. The store is .coll's model and for .coll's reason — a fixed table "
      "allocated whole at construction plus .value's non-blocking guard — because a copy-on-write "
      "GraphState publish assumes the writer is the control thread while this object is written by "
      "whichever thread its message arrived on and in-patcher delivery dispatches on the audio "
      "thread. A symbol longer than 64 characters is refused whole rather than truncated, a loser "
      "of the guard drops rather than waiting, and the guard is never held across a send: dump "
      "takes it once per item, .coll's dump exactly, so a patch whose dump target writes back into "
      "this object is not locked out of its own store, and the bounds are re-read every step so a "
      "wrap mid-dump cannot walk off the end. Calculate() does nothing. The capacity survives a "
      "save because it is a creation argument; the contents deliberately do not, since Max gives "
      "coll a 'save data with patcher' flag and capture none — its 'write' message is how a "
      "capture's contents are saved, to a text file, on demand — and a trace is the record of a "
      "run, so a reloaded patch holding the values from the session it was saved in would be "
      "answering a question nobody had asked yet. Not ported: the editing window and everything "
      "addressing it (open, wclose, the double-click, the precision attribute), file writing, and "
      "the listout and size attributes, the patcher having no attribute mechanism.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in", kInletDoc, "any");
  OUTLET_DOC(0, "dump", kDumpDoc, "any");
  OUTLET_DOC(1, "count", kCountDoc, "any int");
  PARAM_DOC("maximum", "512",
            "Max's argument list, in Max's order. The first whole number is how many items the "
            "record holds before the oldest starts falling off — Max's 'the first argument sets a "
            "maximum number of items to store'. With no argument it holds 512, Max's default, "
            "which is also the ceiling: the table is allocated whole at construction, so a larger "
            "argument is clamped rather than honoured and a smaller one only lowers how much of "
            "that table is used. Values below 1 are raised to 1, since a zero-item record would "
            "silently discard everything, and a float is truncated. Max's second argument is the "
            "display format, 'a' / 'x' / 'm', which chooses whether the editing window draws "
            "numbers in ASCII, hexadecimal or a mix; it is accepted and ignored, the YSE patcher "
            "being headless, so that a '.capture 512 x' brought across from Max still builds the "
            "object it names.",
            "1-512, optional display format");
}

// ─── parameters ───────────────────────────────────────────────────────────────

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous
  // capacity.
  creationArgs.clear();
  capacity = DEFAULT_ITEMS;
  first = 0;
  count = 0;
  received = 0;
}

PARM_PARSE() {
  capacity = DEFAULT_ITEMS;

  for (const std::string& token : creationArgs) {
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens; an empty argument is not a number.
    if (token.empty()) continue;

    float number = 0.f;
    // Strict on purpose, as the rest of the family is: ExprParseFloatList would
    // read `5abc` as 5 and fold `1e999` to 0, and neither answers "is this
    // creation argument a number at all". Max's second argument is a display
    // format letter, so the very case a loose reader would misread is one the
    // object is actually handed.
    if (!ReadNumericToken(token, number)) break;

    const int asked = ExprToInt(number);
    // Clamped rather than honoured at the top: honouring a larger request would
    // mean a table a message path might later have to grow. Raised to 1 at the
    // bottom, since a zero-item record would silently discard everything.
    if (asked < (int)MIN_ITEMS) {
      capacity = MIN_ITEMS;
    } else if ((std::size_t)asked > MAX_ITEMS) {
      capacity = MAX_ITEMS;
    } else {
      capacity = (std::size_t)asked;
    }
    break;
  }

  // Max's second argument is the display format and there is no window here to
  // apply it to, so it is read for nothing at all — see the parameter docs.

  // A re-parse also drops what was recorded: the object that comes back is the
  // one the arguments describe, and a trace left over from the previous
  // capacity would be a record of a run under different rules.
  first = 0;
  count = 0;
  received = 0;
}

// ─── the record ───────────────────────────────────────────────────────────────

void gCapture::Record(bool isSymbol, bool isFloat, float number, const char* text,
                      std::size_t length) {
  storeGuard guard(busy);
  if (!guard.Held()) return;

  // The slot past the newest is always free when the record is not full, and is
  // the oldest one when it is — so the same expression both appends and evicts.
  Item& slot = items[SlotOf(count)];
  slot.isSymbol = isSymbol;
  slot.isFloat = isFloat;
  slot.number = number;
  if (isSymbol) {
    // assign() into a string reserved to ITEM_CAPACITY at construction, so this
    // allocates nothing. The caller has already refused anything longer.
    slot.text.assign(text, length);
  } else {
    slot.text.clear();
  }

  if (count < capacity) {
    count++;
  } else {
    // Max: "the earliest stored item is dropped as each new item is received".
    first = (first + 1) % MAX_ITEMS;
  }

  // Arrivals, not contents: the meter counts the items the ring has already
  // dropped too. Saturated rather than wrapped — signed overflow is undefined,
  // and a meter that went negative would read as a count nothing sent.
  if (received < INT_MAX) received++;
}

void gCapture::RecordToken(const char* token, std::size_t length) {
  float number = 0.f;
  if (ReadNumericToken(token, length, number)) {
    // Int or float is decided by the spelling, the test .trigger, .match,
    // .route and .coll already share, so a patch that sent `60` does not get
    // `60.` back out of the trace.
    Record(false, TokenLooksLikeFloat(token, length), number, nullptr, 0);
    return;
  }

  // Refused whole rather than truncated: half a symbol is a different symbol,
  // and a trace that quietly rewrote what it saw would be worse than one with a
  // hole in it.
  if (length == 0 || length > ITEM_CAPACITY) return;
  Record(true, false, 0.f, token, length);
}

// ─── sending ──────────────────────────────────────────────────────────────────

bool gCapture::Output(std::size_t position, YSE::THREAD thread) {
  bool isSymbol = false;
  bool isFloat = false;
  float number = 0.f;
  {
    storeGuard guard(busy);
    if (!guard.Held()) return false;
    if (position >= count) return false;

    // Copied out before the send: holding the guard across a synchronous fan-out
    // would lock a patch that records into this object from downstream out of
    // its own store, and handing the outlet the stored string would let that
    // patch mutate the very message still being fanned out. The buffer was
    // reserved at construction, so this allocates nothing.
    const Item& item = items[SlotOf(position)];
    isSymbol = item.isSymbol;
    isFloat = item.isFloat;
    number = item.number;
    if (isSymbol) sendText.assign(item.text);
  }

  if (isSymbol) {
    // A one-element list: the patcher has no symbol atom, and Max's `symbol`
    // prefix would put a token in the trace that nothing put into it.
    outputs[0].SendList(sendText, thread);
  } else if (isFloat) {
    outputs[0].SendFloat(number, thread);
  } else {
    outputs[0].SendInt((int)number, thread);
  }
  return true;
}

// ─── commands ─────────────────────────────────────────────────────────────────

bool gCapture::HandleCommand(const char* word, std::size_t length, const std::string& message,
                             std::size_t argOffset, YSE::THREAD thread) {
  if (TokenIs(word, length, "clear", 5)) {
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    // Max: "erases the contents of a capture object". Only the live-item count
    // says which slots are live, so this is O(1) and the storage the next item
    // needs is still there. The arrival meter is left standing: Max documents
    // `clear` as erasing the contents and says nothing about the counter, and
    // the two answer different questions.
    first = 0;
    count = 0;
    return true;
  }

  if (TokenIs(word, length, "dump", 4)) {
    // The guard is taken once per item rather than once for the whole walk, so a
    // patch whose dump target records back into this object is not locked out of
    // its own store. Bounds are therefore re-read every step: Output() stops as
    // soon as the position is past the end, which is also what makes a wrap that
    // happened mid-dump safe.
    for (std::size_t i = 0; Output(i, thread); i++) {}
    return true;
  }

  if (TokenIs(word, length, "count", 5)) {
    // Max: "upon receipt of the count message, the object's internal count will
    // be reset to 0 unless flag is set."
    int flag = 0;
    const bool keep = ReadIntArg(message, argOffset, flag) && flag != 0;

    int meter = 0;
    {
      storeGuard guard(busy);
      if (!guard.Held()) return true;
      meter = received;
      if (!keep) received = 0;
    }
    // Read and zeroed under the guard, sent after releasing it: a value recorded
    // by a re-entrant send is then counted rather than swallowed by the reset.
    outputs[1].SendInt(meter, thread);
    return true;
  }

  // The three with no headless meaning. Consumed rather than recorded because
  // Max dispatches on the selector, so a `capture` in Max cannot store these
  // symbols either — and a trace that differed from Max's for the same patch is
  // the one thing a debugging instrument must not produce. There is no window to
  // open or close, and `write` is file I/O on a path that may be the audio
  // thread.
  if (TokenIs(word, length, "open", 4)) return true;
  if (TokenIs(word, length, "wclose", 6)) return true;
  if (TokenIs(word, length, "write", 5)) return true;

  return false;
}

// ─── inlet ────────────────────────────────────────────────────────────────────

INT_IN(IntIn) {
  (void)inlet;
  Record(false, false, (float)value, nullptr, 0);
}

FLOAT_IN(FloatIn) {
  (void)inlet;
  // Recorded as a float and handed back as one: Max's capture stores what it was
  // given, and the object that converted would be lying about what went past.
  Record(false, true, value, nullptr, 0);
}

LIST_IN(ListIn) {
  (void)inlet;
  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(text, length, 0, begin, end)) return;

  // A command first — see the class documentation for why a data inlet gets to
  // reserve words here at all.
  if (HandleCommand(text + begin, end - begin, value, end, thread)) return;

  // Max's list and anything methods: "all numbers and/or symbols in the list are
  // stored in order from first to last". One item per atom, which is what makes
  // a dump a readable trace rather than a pile of re-framed lists.
  std::size_t cursor = begin;
  std::size_t tokenBegin = 0;
  std::size_t tokenEnd = 0;
  while (NextToken(text, length, cursor, tokenBegin, tokenEnd)) {
    RecordToken(text + tokenBegin, tokenEnd - tokenBegin);
    cursor = tokenEnd;
  }
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::size_t gCapture::Count() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return count;
}

int gCapture::Received() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return received;
}

bool gCapture::IsSymbolAt(std::size_t position) const {
  storeGuard guard(busy);
  if (!guard.Held()) return false;
  if (position >= count) return false;
  return items[SlotOf(position)].isSymbol;
}

float gCapture::NumberAt(std::size_t position) const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0.f;
  if (position >= count) return 0.f;
  const Item& item = items[SlotOf(position)];
  return item.isSymbol ? 0.f : item.number;
}

std::string gCapture::SymbolAt(std::size_t position) const {
  storeGuard guard(busy);
  if (!guard.Held()) return "";
  if (position >= count) return "";
  const Item& item = items[SlotOf(position)];
  return item.isSymbol ? item.text : std::string();
}

#undef className
