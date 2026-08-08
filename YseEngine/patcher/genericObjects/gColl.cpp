#include "gColl.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className gColl

namespace {

  // A stored message that is a whole number only leaves as an int when the int
  // can hold it: casting a float outside the int range is undefined behaviour,
  // and a patch that stored a huge integer is better served by the float that
  // still carries its value. `.route` decides the same question the same way.
  bool FitsInt(float value) {
    return value >= -2147483648.f && value < 2147483648.f;
  }

  // The bounds of the first token of `text`, or false when there is none.
  bool FirstToken(const char* text, std::size_t length, std::size_t& begin, std::size_t& end) {
    begin = 0;
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
  // place rather than through a std::string, since this runs on whichever
  // thread the message arrived on.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  constexpr char kInletDoc[] =
      "The one inlet, and a command inlet rather than a data inlet — which is what lets .coll "
      "reserve words the rest of the message family deliberately does not. A bang outputs the "
      "entry "
      "at the pointer; a number recalls the entry at that numeric address; a lone symbol recalls "
      "the entry at that symbol address (items after it are ignored, as in Max); and a list whose "
      "first item is a number stores the rest of it at that numeric address. The commands are "
      "'store <address> <message>', 'insert <index> <message>', 'append <message>', 'remove "
      "<address>', 'delete <address>' (which also brings every higher numeric address down by "
      "one), 'clear', 'length', 'goto <address>', 'start', 'end', 'next', 'prev', 'dump', "
      "'read [file]', 'readagain', 'write [file]', 'writeagain' and 'filetype'. An "
      "address is a number or a symbol, decided by the same strict reader .sel and .route use, and "
      "the two never collide: the address 1 and the address one are different entries. Anything "
      "that does not fit — a message longer than 256 characters, an address longer than 64, or a "
      "store into a collection that already holds 256 entries — is refused whole and silently, "
      "since the inlet may be the audio thread.";

  constexpr char kDataDoc[] =
      "The stored message, in the kind it is: a stored '60 100' leaves as a list, a stored '60' as "
      "the int 60, a stored '60.5' as that float, and a stored lone symbol as a one-element list — "
      "the rule .route establishes for a remainder. Max instead prefixes a lone symbol with the "
      "word 'symbol', which is how it restores an atom type this patcher does not have; inventing "
      "that word here would put a token in the message nothing downstream asked for. 'length' "
      "sends "
      "the entry count out here as an int. Nothing is sent when the address holds nothing.";

  constexpr char kAddressDoc[] =
      "The address of the entry that just left the data outlet, as an int for a numeric address "
      "and "
      "as a one-element list for a symbol one. It fires only where Max fires it — on bang, dump, "
      "next and prev, Max's 'whenever a message out the 1st outlet is triggered by bang, dump, "
      "next, prev, or sub' — and not on a plain lookup, which answers with the data alone since "
      "the "
      "asking patch already had the address. It fires before the data outlet, which is Max's "
      "right-to-left order.";

  constexpr char kDoneDoc[] =
      "Bangs when a dump has finished sending every entry. Max's fourth outlet, and the third "
      "here: the file outlet beside it was appended rather than inserted in Max's position, so the "
      "cords of every patch saved while .coll had three outlets still land where they did.";

  constexpr char kFileDoc[] =
      "Bangs when a read has finished loading a file into the collection — Max's third outlet, "
      "appended here as the fourth so no saved patch's cords shift (the promise .coll made in #494 "
      "and the rule the rest of the file-reading family follows). It fires only on success: a file "
      "that does not exist, does not fit, or cannot be opened leaves it silent, and it does not "
      "fire for a write, for which Max has no outlet either. It fires a block or more after the "
      "read message rather than inside it, because the disk work happens on the background pool — "
      "a read arrives on whichever thread dispatched it, which may be the audio callback.";

} // namespace

CONSTRUCT() {
  // One inlet, as in Max. Everything the object does arrives here, which is why
  // the leading item of a message is read as a command first and as an address
  // only when it is none of them.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // ANY on both: what leaves the data outlet is a list, an int or a float
  // depending on what the entry holds, and an address is an int or a symbol.
  ADD_OUT_ANY;
  ADD_OUT_ANY;
  ADD_OUT_BANG;
  // Appended, not inserted: Max puts the file outlet third, but .coll shipped
  // with three outlets in #494 and moving the dump outlet would shift the cords
  // of every patch saved since (issue #683).
  ADD_OUT_BANG;

  // The whole table, taken once here on the control thread. Nothing on a
  // message path ever resizes it or grows a string inside it, which is what
  // makes a store from a rendering graph allocation-free.
  entries.resize(MAX_ENTRIES);
  for (Entry& entry : entries) {
    entry.key.reserve(KEY_CAPACITY + 1);
    entry.value.reserve(VALUE_CAPACITY + 1);
  }
  sendValue.reserve(VALUE_CAPACITY + 1);
  sendAddress.reserve(KEY_CAPACITY + 1);

  // Same treatment for the file buffers (issue #683): a `read` or `write` may
  // arrive on the audio thread, so remembering a name and formatting the whole
  // collection both have to reuse storage that already exists.
  readPath.reserve(fileScheduler::PATH_CAPACITY);
  writePath.reserve(fileScheduler::PATH_CAPACITY);
  fileScratch.reserve(FILE_TEXT_CAPACITY + 1);

  ADD_DESCRIPTION(
      "Stores and recalls a collection of messages held at addresses — Max's coll, 'store and edit "
      "a collection of different messages'. The general-purpose data store of Max patching: "
      "presets, note tables, mapping curves and sequences all live in one, and until this object "
      "nothing in the YSE patcher could hold more than a single value at all, so a patch that "
      "wanted a table had to spell it out as one object per entry. An address is a number or a "
      "symbol, decided by the strict reader .sel and .route share, and the two never collide — the "
      "address 1 and the address one are different entries — while numeric addresses need be "
      "neither contiguous nor in order, exactly as in Max. Entries keep storage order rather than "
      "address order, which is what makes dump, next and prev mean anything: Max sends them 'in "
      "the "
      "order in which they are stored', so a patch that stored a sequence gets that sequence back. "
      "A list whose first item is a number stores the rest of it at that address (Max's list "
      "method), a bare number or symbol recalls, a bang outputs the entry at the pointer, and "
      "store, insert, append, remove, delete, clear, length, goto, start, end, next, prev and dump "
      "are read as commands when they lead a message. Reserving those words is Max's own contract "
      "for coll and not a shortcut: this is the one object in the family whose inlet is a command "
      "inlet rather than a data inlet, which is exactly why the .prepend discipline — never "
      "reserve "
      "a word on an inlet that must carry arbitrary text — does not apply to it, and the data an "
      "entry holds is never parsed for commands. The address leaves outlet 1 only where Max sends "
      "it (bang, dump, next, prev) and always before the data, which is Max's right-to-left order; "
      "outlet 2 bangs when a dump has finished and outlet 3 when a read has. read, readagain, "
      "write and writeagain move the collection through a plain-text file in Max's format, one "
      "'<address>, <message>;' record per line, and a read replaces what is held. None of that "
      "happens on the message path: a read arrives on whichever thread dispatched it, which may be "
      "the audio callback, so the request is a wait-free claim on a patcher-owned slot, the disk "
      "work runs on the background pool, and the contents are parsed in the completion the patcher "
      "delivers at the top of a later block — which is also when outlet 3 bangs. The bare forms of "
      "read and write reuse the last name given, since Max's open a file dialog and a headless "
      "patcher has none, and filetype is consumed and inert for the same reason. Records past the "
      "256-entry bound are dropped and the rest kept, one whose address or message does not fit is "
      "skipped, and a file too large to fit a slot is refused whole with outlet 3 silent. The "
      "format inherits Max's one limitation: a stored message containing a comma or a semicolon "
      "cannot round-trip through a file. The contents survive a DumpJSON / ParseJSON round "
      "trip, which is Max's 'save data with patcher' and the reason pObject grew a state hook: a "
      "creation parameter is what an object was made with, not what it has since been told, so "
      "rewriting one from run-time state would quietly change the arguments a patch author typed. "
      "The pointer is not saved — it is run-time position, as .cycle's thresh and .bucket's freeze "
      "are. The store is bounded at 256 entries of at most 256 characters each, held at addresses "
      "of at most 64, and the whole table is allocated at construction and never resized: that "
      "bound is what makes the object real-time safe, and it is why the store is not published as "
      "a "
      "copy-on-write snapshot the way GraphState is. That model assumes the writer is the control "
      "thread, and a .coll is written by whichever thread its message arrived on — in-patcher "
      "delivery dispatches on the audio thread — so a store reaching it from a rendering graph "
      "would have to allocate a replacement table there. Bounded storage with .value's "
      "non-blocking guard gives what the mandate is after without that: nothing on any path "
      "allocates, locks or blocks, the guard is never held across a send, and a store that loses "
      "the guard drops rather than waiting. Anything that does not fit is refused whole and "
      "silently. Calculate() does nothing. Not ported: the shared name context, the editor window "
      "(open, wclose), and the arithmetic and reordering messages sub, nsub, nth, min, "
      "max, sort, swap, merge, separate, renumber, assoc, deassoc, nstore and subsym.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in", kInletDoc, "at most 256 entries");
  OUTLET_DOC(0, "data", kDataDoc, "");
  OUTLET_DOC(1, "address", kAddressDoc, "");
  OUTLET_DOC(2, "done", kDoneDoc, "");
  OUTLET_DOC(3, "file", kFileDoc, "");
}

// ─── the store ────────────────────────────────────────────────────────────────

bool gColl::ReadAddress(const char* text, std::size_t length, Address& out) {
  // An address that cannot be stored cannot be looked up either, so both ends
  // refuse exactly the same things.
  if (length == 0 || length > KEY_CAPACITY) return false;

  out.text = text;
  out.length = length;

  // Strict, as the rest of the family is: ExprParseFloatList would read `5abc`
  // as 5 and fold `1e999` to 0, and neither answers "is this address a number
  // at all", which is the question — a token that is not one is a symbol
  // address and has to stay distinguishable from every numeric one.
  float number = 0.f;
  if (ReadNumericToken(text, length, number)) {
    out.numeric = true;
    // Max's "a float is converted to an int".
    out.index = ExprToInt(number);
    return true;
  }
  out.numeric = false;
  out.index = 0;
  return true;
}

int gColl::Find(const Address& address) const {
  for (std::size_t i = 0; i < count; i++) {
    const Entry& entry = entries[i];
    if (address.numeric) {
      if (entry.numeric && entry.index == address.index) return (int)i;
      continue;
    }
    if (entry.numeric) continue;
    if (entry.key.size() != address.length) continue;
    // Compared against the character range in place: a substr here would
    // allocate on whichever thread the message arrived on.
    if (entry.key.compare(0, address.length, address.text, address.length) == 0) return (int)i;
  }
  return -1;
}

bool gColl::AssignValue(Entry& entry, const char* value, std::size_t length) {
  // Refused rather than truncated: half a message is a different message, and a
  // patch storing a preset could not tell a clipped one from a stored one.
  if (length > VALUE_CAPACITY) return false;
  entry.value.assign(value, length);
  return true;
}

void gColl::SetNumericKey(Entry& entry, int index) {
  entry.numeric = true;
  entry.index = index;
  // WriteInt rather than std::to_string: no allocation and no locale, and it
  // keeps the key text in step with the index so `delete`'s renumbering cannot
  // leave the two disagreeing.
  char digits[FORMAT_INT_WIDTH];
  entry.key.assign(digits, WriteInt(index, digits));
}

void gColl::CopyEntry(Entry& dst, const Entry& src) {
  dst.key.assign(src.key);
  dst.value.assign(src.value);
  dst.index = src.index;
  dst.numeric = src.numeric;
}

int gColl::HighestIndex() const {
  bool any = false;
  int highest = -1;
  for (std::size_t i = 0; i < count; i++) {
    if (!entries[i].numeric) continue;
    if (!any || entries[i].index > highest) {
      highest = entries[i].index;
      any = true;
    }
  }
  // -1 for "no numeric address" is not ambiguous where it is used: `append`
  // adds one to it, and an empty collection and one whose highest address is
  // -1 both want the next entry at 0.
  return highest;
}

bool gColl::StoreAt(const Address& address, const char* value, std::size_t length) {
  if (length > VALUE_CAPACITY) return false;

  const int at = Find(address);
  if (at >= 0) return AssignValue(entries[(std::size_t)at], value, length);

  // Full: refused rather than grown, since growing the table would allocate on
  // whichever thread the message arrived on.
  if (count >= MAX_ENTRIES) return false;

  Entry& entry = entries[count];
  if (address.numeric) {
    SetNumericKey(entry, address.index);
  } else {
    entry.numeric = false;
    entry.index = 0;
    entry.key.assign(address.text, address.length);
  }
  if (!AssignValue(entry, value, length)) return false;
  count++;
  return true;
}

bool gColl::InsertAt(int index, const char* value, std::size_t length) {
  if (length > VALUE_CAPACITY) return false;
  if (count >= MAX_ENTRIES) return false;

  // Where it lands in storage order: in front of the first entry holding the
  // address it is taking, so `insert` puts the message *at* that address rather
  // than merely near it. Read before the renumbering below moves everything.
  std::size_t position = count;
  for (std::size_t i = 0; i < count; i++) {
    if (entries[i].numeric && entries[i].index >= index) {
      position = i;
      break;
    }
  }

  // Max: "incrementing all equal or greater addresses by 1 if necessary".
  for (std::size_t i = 0; i < count; i++) {
    if (entries[i].numeric && entries[i].index >= index) {
      SetNumericKey(entries[i], entries[i].index + 1);
    }
  }

  for (std::size_t i = count; i > position; i--) {
    CopyEntry(entries[i], entries[i - 1]);
  }

  Entry& entry = entries[position];
  SetNumericKey(entry, index);
  AssignValue(entry, value, length);
  count++;
  if (pointer >= count) pointer = 0;
  return true;
}

void gColl::Erase(std::size_t position, bool renumber) {
  if (position >= count) return;

  const bool wasNumeric = entries[position].numeric;
  const int removed = entries[position].index;

  // Closed rather than left as a hole, so storage order — which is what dump,
  // next and prev walk — stays the order the entries were stored in.
  for (std::size_t i = position; i + 1 < count; i++) {
    CopyEntry(entries[i], entries[i + 1]);
  }
  count--;

  // Max's `delete`, as against its `remove`: "if the specified address is
  // numeric, all higher numbered addresses are decremented by 1".
  if (renumber && wasNumeric) {
    for (std::size_t i = 0; i < count; i++) {
      if (entries[i].numeric && entries[i].index > removed) {
        SetNumericKey(entries[i], entries[i].index - 1);
      }
    }
  }

  if (pointer >= count) pointer = 0;
}

// ─── sending ──────────────────────────────────────────────────────────────────

void gColl::SendTyped(std::size_t pin, const std::string& text, YSE::THREAD thread) {
  // `.route`'s rule for a remainder, minus its bang case: an entry holding
  // nothing is still an entry, and a bang would read downstream as "no data"
  // rather than as "empty data".
  const std::size_t length = text.size();
  float number = 0.f;
  if (length > 0 && ReadNumericToken(text.c_str(), length, number)) {
    // Int or float is decided by the spelling, the test .trigger, .match and
    // .route already share, so a patch that stored `60` does not get `60.` back.
    if (!TokenLooksLikeFloat(text.c_str(), length) && FitsInt(number)) {
      outputs[pin].SendInt((int)number, thread);
    } else {
      outputs[pin].SendFloat(number, thread);
    }
    return;
  }
  outputs[pin].SendList(text, thread);
}

bool gColl::Output(std::size_t position, bool withAddress, YSE::THREAD thread) {
  {
    storeGuard guard(busy);
    if (!guard.Held()) return false;
    if (position >= count) return false;

    // Copied out under the guard and sent after it: holding it across a
    // synchronous fan-out would make a patch that wires an outlet back into
    // this object's inlet lose its own message to the guard it is still
    // holding. Both buffers were reserved at construction, so this allocates
    // nothing.
    const Entry& entry = entries[position];
    sendValue.assign(entry.value);
    sendNumeric = entry.numeric;
    sendIndex = entry.index;
    if (!entry.numeric) sendAddress.assign(entry.key);
  }

  // Address first: Max's right-to-left outlet order, the order .trigger and
  // .bucket already fire in.
  if (withAddress) {
    if (sendNumeric) {
      outputs[1].SendInt(sendIndex, thread);
    } else {
      outputs[1].SendList(sendAddress, thread);
    }
  }
  SendTyped(0, sendValue, thread);
  return true;
}

void gColl::Recall(const Address& address, YSE::THREAD thread) {
  int position = -1;
  {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    position = Find(address);
  }
  if (position < 0) return;

  // No address outlet: Max sends one only for bang, dump, next and prev, and a
  // patch that looked an address up already had it.
  Output((std::size_t)position, false, thread);
}

// ─── commands ─────────────────────────────────────────────────────────────────

bool gColl::HandleCommand(const char* text, std::size_t length, YSE::THREAD thread) {
  std::size_t wordBegin = 0;
  std::size_t wordEnd = 0;
  if (!FirstToken(text, length, wordBegin, wordEnd)) return false;

  const char* word = text + wordBegin;
  const std::size_t wordLength = wordEnd - wordBegin;

  // The argument region: everything after the command word, trimmed.
  std::size_t argBegin = wordEnd;
  std::size_t argEnd = length;
  Trim(text, argBegin, argEnd);

  // The first argument, when the command takes an address.
  Address address;
  std::size_t addressEnd = argBegin;
  bool haveAddress = false;
  if (argBegin < argEnd) {
    addressEnd = argBegin;
    while (addressEnd < argEnd && !IsSelectorSeparator(text[addressEnd]))
      addressEnd++;
    haveAddress = ReadAddress(text + argBegin, addressEnd - argBegin, address);
  }

  // What is left after that address — the message a store carries.
  std::size_t restBegin = addressEnd;
  std::size_t restEnd = argEnd;
  Trim(text, restBegin, restEnd);

  if (TokenIs(word, wordLength, "store", 5)) {
    if (!haveAddress) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    StoreAt(address, text + restBegin, restEnd - restBegin);
    return true;
  }

  if (TokenIs(word, wordLength, "insert", 6)) {
    // Numeric only: Max's insert is "at the address specified by the number",
    // and there is no ordering among symbol addresses to insert into.
    if (!haveAddress || !address.numeric) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    InsertAt(address.index, text + restBegin, restEnd - restBegin);
    return true;
  }

  if (TokenIs(word, wordLength, "append", 6)) {
    // Max: "an index that is one larger than the highest current index".
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    Address next;
    next.numeric = true;
    next.index = HighestIndex() + 1;
    StoreAt(next, text + argBegin, argEnd - argBegin);
    return true;
  }

  if (TokenIs(word, wordLength, "remove", 6) || TokenIs(word, wordLength, "delete", 6)) {
    if (!haveAddress) return true;
    const bool renumber = TokenIs(word, wordLength, "delete", 6);
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    const int position = Find(address);
    if (position >= 0) Erase((std::size_t)position, renumber);
    return true;
  }

  if (TokenIs(word, wordLength, "clear", 5)) {
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    // The strings keep their storage: only `count` says which entries are live,
    // so clearing is O(1) and the capacity a later store needs is still there.
    count = 0;
    pointer = 0;
    return true;
  }

  if (TokenIs(word, wordLength, "length", 6)) {
    std::size_t live = 0;
    {
      storeGuard guard(busy);
      if (!guard.Held()) return true;
      live = count;
    }
    outputs[0].SendInt((int)live, thread);
    return true;
  }

  if (TokenIs(word, wordLength, "goto", 4)) {
    if (!haveAddress) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    const int position = Find(address);
    // Max: "sets the pointer at a specific address, but does not trigger
    // output".
    if (position >= 0) pointer = (std::size_t)position;
    return true;
  }

  if (TokenIs(word, wordLength, "start", 5)) {
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    pointer = 0;
    return true;
  }

  if (TokenIs(word, wordLength, "end", 3)) {
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    pointer = count > 0 ? count - 1 : 0;
    return true;
  }

  if (TokenIs(word, wordLength, "next", 4) || TokenIs(word, wordLength, "prev", 4)) {
    const bool forward = TokenIs(word, wordLength, "next", 4);
    std::size_t at = 0;
    {
      storeGuard guard(busy);
      if (!guard.Held()) return true;
      if (count == 0) return true;
      if (pointer >= count) pointer = 0;
      at = pointer;
      // Max steps the pointer after sending; stepping it here lands it on the
      // same entry and saves taking the guard a second time across a fan-out
      // that may have re-entered this object in the meantime.
      pointer = forward ? (at + 1) % count : (at == 0 ? count - 1 : at - 1);
    }
    Output(at, true, thread);
    return true;
  }

  if (TokenIs(word, wordLength, "dump", 4)) {
    // The guard is taken once per entry rather than once for the whole walk, so
    // a patch whose dump target writes back into this object is not deadlocked
    // out of its own store. Bounds are therefore re-read every step: Output()
    // stops as soon as the position is past the end.
    for (std::size_t i = 0; Output(i, true, thread); i++) {}
    outputs[2].SendBang(thread);
    return true;
  }

  // ─── the file commands (issue #683) ────────────────────────────────────────
  //
  // Every one of these is a claim on a slot and nothing more. Whichever thread
  // is dispatching, no file is opened here.

  if (TokenIs(word, wordLength, "read", 4)) {
    RequestFile(FILE_OP::READ, text + argBegin, argEnd - argBegin);
    return true;
  }

  if (TokenIs(word, wordLength, "readagain", 9)) {
    // Max: "loads the contents of the most recently read file". With no prior
    // read Max falls back to its Open dialog, which a headless patcher has no
    // equivalent of, so it does nothing.
    RequestFile(FILE_OP::READ, nullptr, 0);
    return true;
  }

  if (TokenIs(word, wordLength, "write", 5)) {
    RequestFile(FILE_OP::WRITE, text + argBegin, argEnd - argBegin);
    return true;
  }

  if (TokenIs(word, wordLength, "writeagain", 10)) {
    RequestFile(FILE_OP::WRITE, nullptr, 0);
    return true;
  }

  if (TokenIs(word, wordLength, "filetype", 8)) {
    // Max: "sets the file types which can be read and written into the coll
    // object" — a filter on the file *dialogs*, which a headless patcher does
    // not have. Consumed rather than ignored so a patch brought across from Max
    // does not have the word read as a symbol address instead.
    return true;
  }

  return false;
}

// ─── files ────────────────────────────────────────────────────────────────────

bool gColl::RequestFile(FILE_OP op, const char* name, std::size_t length) {
  fileScheduler* io = FileIO();
  // A standalone .coll has no patcher and so no plumbing. Silent: this may be
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
  // must never touch this object: by the time the job runs, a live edit may
  // have deleted it.
  if (!Serialize()) return false;
  return io->RequestWrite(this, FILE_TAG_WRITE, remembered.c_str(), remembered.size(),
                          fileScratch.c_str(), fileScratch.size());
}

bool gColl::Serialize() {
  storeGuard guard(busy);
  if (!guard.Held()) return false;

  // clear() keeps the capacity reserved at construction, so every append below
  // writes into storage that already exists. FILE_TEXT_CAPACITY is the whole
  // table at its maximum, so the buffer cannot run out.
  fileScratch.clear();
  for (std::size_t i = 0; i < count; i++) {
    const Entry& entry = entries[i];
    fileScratch.append(entry.key);
    fileScratch.append(", ", 2);
    fileScratch.append(entry.value);
    fileScratch.append(";\n", 2);
  }
  return true;
}

bool gColl::LoadFrom(const char* text, std::size_t length) {
  storeGuard guard(busy);
  if (!guard.Held()) return false;

  // Max's read replaces the contents. The strings keep their storage — only
  // `count` says which entries are live — so this costs nothing.
  count = 0;
  pointer = 0;

  std::size_t at = 0;
  while (at < length) {
    // One record, ending at the first `;` or at the end of the file. A trailing
    // fragment with no terminator is still read, so a hand-written file that
    // forgot the last semicolon loads.
    std::size_t end = at;
    while (end < length && text[end] != ';')
      end++;

    std::size_t begin = at;
    std::size_t stop = end;
    Trim(text, begin, stop);
    at = end < length ? end + 1 : length;
    if (stop <= begin) continue;

    // Max's format puts the address first and the message after a comma. Split
    // at the *first* comma only: everything after it is the message, commas and
    // all, which is the only reading under which a message is not silently cut
    // in half by its own punctuation.
    std::size_t comma = begin;
    while (comma < stop && text[comma] != ',')
      comma++;

    std::size_t keyBegin = begin;
    std::size_t keyEnd = comma;
    Trim(text, keyBegin, keyEnd);

    std::size_t valueBegin = comma < stop ? comma + 1 : stop;
    std::size_t valueEnd = stop;
    Trim(text, valueBegin, valueEnd);

    Address address;
    // Read through the same reader the inlet uses, so a numeric address in the
    // file restores as a numeric address and a symbol one as a symbol — the
    // round trip is only exact if both ends classify identically. A record that
    // does not fit is skipped and the rest of the file still loads; one past
    // MAX_ENTRIES is refused by StoreAt for the same reason a `store` into a
    // full collection is.
    if (!ReadAddress(text + keyBegin, keyEnd - keyBegin, address)) continue;
    StoreAt(address, text + valueBegin, valueEnd - valueBegin);
  }
  return true;
}

void gColl::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // Control thread: build the patcher's file plumbing now, so that a `read`
  // arriving later on the audio thread finds it already there (issue #683).
  EnableFileIO();
}

void gColl::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  // Max has no outlet for a finished write, so a write reports only by having
  // happened. A failed read reports by the outlet staying silent.
  if (result.op != FILE_OP::READ || result.tag != FILE_TAG_READ) return;
  if (!result.ok || result.bytes == nullptr) return;
  if (!LoadFrom(result.bytes, result.byteCount)) return;

  // Max: "sent out when coll has finished reading in a file of data". After the
  // contents are in place, so a patch that reacts to the bang by asking for an
  // entry finds it.
  outputs[3].SendBang(thread);
}

// ─── inlet ────────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  // Max: "triggers output of data at current pointer position".
  std::size_t at = 0;
  {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    if (count == 0) return;
    if (pointer >= count) pointer = 0;
    at = pointer;
  }
  Output(at, true, thread);
}

INT_IN(IntIn) {
  (void)inlet;
  Address address;
  address.numeric = true;
  address.index = value;
  Recall(address, thread);
}

FLOAT_IN(FloatIn) {
  (void)inlet;
  Address address;
  address.numeric = true;
  // Max's float method addresses the same entries the int method does.
  address.index = ExprToInt(value);
  Recall(address, thread);
}

LIST_IN(ListIn) {
  (void)inlet;
  const char* text = value.c_str();
  const std::size_t length = value.size();

  // A command first — this is the one object in the family whose inlet is a
  // command inlet, and Max reserves the same words on it.
  if (HandleCommand(text, length, thread)) return;

  std::size_t begin = 0;
  std::size_t end = 0;
  if (!FirstToken(text, length, begin, end)) return;

  Address address;
  if (!ReadAddress(text + begin, end - begin, address)) return;

  std::size_t restBegin = end;
  std::size_t restEnd = length;
  Trim(text, restBegin, restEnd);

  // Max's list method: "the first value is used as the address at which to
  // store the remaining items in the list". Only for a numeric first item — a
  // leading symbol is Max's `anything`, which retrieves rather than stores, and
  // `store` is how a symbol address is written.
  if (address.numeric && restBegin < restEnd) {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    StoreAt(address, text + restBegin, restEnd - restBegin);
    return;
  }

  Recall(address, thread);
}

// ─── persistence ──────────────────────────────────────────────────────────────

void gColl::DumpState(nlohmann::json::value_type& json) {
  // Control thread — patcherImplementation::DumpJSON holds mtx — but the guard
  // is still taken, because a message may be arriving from a rendering graph
  // while the patch is being saved.
  storeGuard guard(busy);
  if (!guard.Held()) return;
  if (count == 0) return;

  for (std::size_t i = 0; i < count; i++) {
    nlohmann::json entry;
    entry["key"] = entries[i].key;
    entry["value"] = entries[i].value;
    json["entries"].push_back(entry);
  }
}

void gColl::RestoreState(const nlohmann::json::value_type& json) {
  const auto stored = json.find("entries");
  if (stored == json.end() || !stored->is_array()) return;

  storeGuard guard(busy);
  if (!guard.Held()) return;

  count = 0;
  pointer = 0;
  for (const auto& entry : *stored) {
    const std::string key = entry.value("key", std::string());
    const std::string message = entry.value("value", std::string());
    Address address;
    // Written back through the same reader the inlet uses, so a numeric key
    // restores as a numeric address and a symbol one as a symbol — the round
    // trip is only exact if both ends classify identically.
    if (!ReadAddress(key.c_str(), key.size(), address)) continue;
    StoreAt(address, message.c_str(), message.size());
  }
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::string gColl::KeyAt(std::size_t position) const {
  storeGuard guard(busy);
  if (!guard.Held()) return std::string();
  if (position >= count) return std::string();
  return entries[position].key;
}

std::string gColl::ValueAt(std::size_t position) const {
  storeGuard guard(busy);
  if (!guard.Held()) return std::string();
  if (position >= count) return std::string();
  return entries[position].value;
}

std::string gColl::Lookup(const std::string& key) const {
  Address address;
  if (!ReadAddress(key.c_str(), key.size(), address)) return std::string();

  storeGuard guard(busy);
  if (!guard.Held()) return std::string();
  const int position = Find(address);
  if (position < 0) return std::string();
  return entries[(std::size_t)position].value;
}

#undef className
