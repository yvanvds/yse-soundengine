#include "gColl.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "../patcherImplementation.h"

#include <cstring>

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

  // The bounds of the `position`-th token of `text`, counted from **1** — the
  // numbering Max's coll uses for message elements ("nth 75 2 will output the
  // second item in the list stored at address 75"). False when there is no such
  // token. In place, so reading an element costs nothing (issue #684).
  bool ElementAt(const char* text, std::size_t length, int position, std::size_t& begin,
                 std::size_t& end) {
    if (position < 1) return false;
    std::size_t at = 0;
    int seen = 0;
    while (at < length) {
      while (at < length && IsSelectorSeparator(text[at]))
        at++;
      if (at >= length) break;
      std::size_t stop = at;
      while (stop < length && !IsSelectorSeparator(text[stop]))
        stop++;
      seen++;
      if (seen == position) {
        begin = at;
        end = stop;
        return true;
      }
      at = stop;
    }
    return false;
  }

  // The same, over the [from, to) argument region of a command message, with
  // the offsets reported in the message's own coordinates.
  bool ArgAt(const char* text, std::size_t from, std::size_t to, int position, std::size_t& begin,
             std::size_t& end) {
    if (to <= from) return false;
    std::size_t b = 0;
    std::size_t e = 0;
    if (!ElementAt(text + from, to - from, position, b, e)) return false;
    begin = from + b;
    end = from + e;
    return true;
  }

  // Everything from argument `position` to the end of the region — the data
  // half of a `sub` or a `merge`, which is the rest of the message rather than
  // one token.
  bool ArgTail(const char* text, std::size_t from, std::size_t to, int position, std::size_t& begin,
               std::size_t& end) {
    std::size_t b = 0;
    std::size_t e = 0;
    if (!ArgAt(text, from, to, position, b, e)) return false;
    begin = b;
    end = to;
    Trim(text, begin, end);
    return end > begin;
  }

  // Argument `position` read as an int, through the strict reader the rest of
  // the family uses. False when the argument is missing or is not a number at
  // all, which is what lets every optional argument keep its default.
  bool ReadIntArgument(const char* text, std::size_t from, std::size_t to, int position, int& out) {
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!ArgAt(text, from, to, position, begin, end)) return false;
    float number = 0.f;
    if (!ReadNumericToken(text + begin, end - begin, number)) return false;
    out = ExprToInt(number);
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
      "'sub <address> <position> <data>' and 'nsub ...' (replace the element at a 1-based position "
      "inside a stored message; sub also sends the result out afterwards), 'nth <address> "
      "<position>', 'min [element]' and 'max [element]' (lowest / highest value at an element "
      "position across every entry, default 1), 'sort [-1|1] [entry]' (ascending or descending, by "
      "the address when entry is -1 and by the nth element otherwise), 'swap <address> <address>', "
      "'merge <address> <data>', 'separate <index>', 'renumber [start]', 'renumber2 [from]' "
      "(move every numeric address at or above 'from', 0 by default, up by one), "
      "'assoc <symbol> <number>' and 'deassoc <symbol> <number>' (give a numeric entry a second, "
      "symbol address that reaches it, or take it away again; the number has to exist already, and "
      "an entry the symbol already reached is removed), 'nstore <number> <symbol> <message>' "
      "(store and associate in one message — either order of the pair), 'subsym <new> <old>' "
      "(rename a symbol address or an alias, refused when the new name is already in use), "
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
      "the entry count out here as an int, and 'nth', 'min' and 'max' send the single element they "
      "found in the kind it was stored as. Nothing is sent when the address holds nothing.";

  constexpr char kAddressDoc[] =
      "The address of the entry that just left the data outlet, as an int for a numeric address "
      "and "
      "as a one-element list for a symbol one. An entry with both — a numeric address and the "
      "symbol alias an 'assoc' gave it — reports the number: the alias is a way in, not something "
      "the outlet announces, which is what cyclone's single output routine does and what its help "
      "patch annotates as 'address is still an int, not the alias'. Max's own text says a symbol "
      "address sends 0 here, which contradicts the output table one screen away in the same "
      "reference; the symbol is sent, as cyclone sends it, and 0 is reserved for an entry with "
      "neither address — which this object cannot produce. It fires only where Max fires it — on "
      "bang, dump, "
      "next, prev and sub, Max's 'whenever a message out the 1st outlet is triggered by bang, "
      "dump, next, prev, or sub' — and not on a plain lookup, which answers with the data alone "
      "since the asking patch already had the address. It fires before the data outlet, which is "
      "Max's right-to-left order.";

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

// ─── the store ────────────────────────────────────────────────────────────────

collStore::collStore() {
  // The whole table, taken here on the control thread. Nothing on a message
  // path ever resizes it or grows a string inside it, which is what makes a
  // store from a rendering graph allocation-free.
  entries.resize(MAX_ENTRIES);
  for (collEntry& entry : entries) {
    entry.key.reserve(KEY_CAPACITY + 1);
    entry.value.reserve(VALUE_CAPACITY + 1);
    // The second address (issue #695) is reserved here for the same reason the
    // first is: an `assoc` may arrive on the audio thread, and a lookup that
    // has to compare against an alias must not be the thing that grows it.
    entry.alias.reserve(KEY_CAPACITY + 1);
  }
}

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

  // Max's two creation arguments, in Max's order (issue #684).
  ADD_PARAM(collName);
  ADD_PARAM(noSearch);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // A private store to start with, so `store` is never null and no message
  // handler needs a null check. Rebind() trades it for a shared one as soon as
  // there is both a name and a patcher to prefix it with.
  Rebind();

  sendValue.reserve(VALUE_CAPACITY + 1);
  sendAddress.reserve(KEY_CAPACITY + 1);

  // `sub` splices here before the result goes back over the entry, and a splice
  // that would overflow the entry is refused before a character is copied — so
  // the entry's own bound is also this buffer's.
  editScratch.reserve(VALUE_CAPACITY + 1);

  // `sort`'s working set, and the scratch entry `sort` and `swap` share. All of
  // it is sized here because both messages may arrive on the audio thread.
  sortKeys.resize(MAX_ENTRIES);
  sortOrder.resize(MAX_ENTRIES);
  sortVisited.resize(MAX_ENTRIES);
  scratchEntry.key.reserve(KEY_CAPACITY + 1);
  scratchEntry.value.reserve(VALUE_CAPACITY + 1);
  scratchEntry.alias.reserve(KEY_CAPACITY + 1);

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
      "store, insert, append, remove, delete, clear, length, goto, start, end, next, prev, dump, "
      "sub, nsub, nth, min, max, sort, swap, merge, separate, renumber, renumber2, assoc, deassoc, "
      "nstore and subsym are read as "
      "commands when they lead a message. Reserving those words is Max's own contract "
      "for coll and not a shortcut: this is the one object in the family whose inlet is a command "
      "inlet rather than a data inlet, which is exactly why the .prepend discipline — never "
      "reserve "
      "a word on an inlet that must carry arbitrary text — does not apply to it, and the data an "
      "entry holds is never parsed for commands. The address leaves outlet 1 only where Max sends "
      "it (bang, dump, next, prev, sub) and always before the data, which is Max's right-to-left "
      "order; outlet 2 bangs when a dump has finished and outlet 3 when a read has. The first "
      "creation argument is Max's shared name: all .coll objects of one name share their contents, "
      "through a store held weakly in the patcher's shared-name registry under "
      "'<patcherName>.<name>' — the address form .s, .r and .value already use. An unnamed .coll "
      "keeps a store of its own rather than pooling on the empty name, the traversal pointer stays "
      "per-object so two .coll objects on one name walk the collection independently, and Max's "
      "refer is not ported: it resolves a name from a message, and a name resolves under a mutex "
      "on the control thread, never on the audio callback a message handler may be running on. "
      "Max's second argument, no-search, is accepted and inert — there is no automatic hunt for a "
      "file named after the collection to suppress. sub and nsub replace the element at a 1-based "
      "position inside a stored message, nth reads one out, min and max scan an element position "
      "across every entry, sort reorders storage (stably, ascending on -1 and descending on 1, by "
      "the address when the second argument is -1 and by the nth element otherwise), swap "
      "exchanges two entries' addresses without moving their data, merge appends to what an "
      "address already holds, separate opens a numeric gap, renumber renumbers the numeric "
      "entries consecutively from the address it is given (0 by default), and renumber2 moves "
      "every numeric address at or above the one it is given (also 0 by default) up by one. "
      "assoc, deassoc, nstore and subsym are Max's symbol/number address aliasing: a numeric entry "
      "can be given a second, symbol address, and from then on any reference to that symbol is a "
      "reference to the number — a store, a remove, an nth or a bare recall all reach the entry by "
      "either name. One symbol reaches one entry, which is what keeps a lookup's answer "
      "independent of storage order, so associating a symbol that already reached another entry "
      "removes that entry, as Max's own reference says it does. The alias rides with its entry "
      "through insert, delete, renumber, renumber2 and separate, and with the address through "
      "swap; the address outlet reports the number for an entry that has both; and a plain store "
      "at an aliased address keeps the alias, which is where this departs from cyclone. "
      "read, readagain, "
      "write and writeagain move the collection through a plain-text file in Max's format, one "
      "'<address>, <message>;' record per line — an aliased entry writing both of its addresses, "
      "number first and symbol second, the order Max 5's format paragraph gives and the one "
      "cyclone writes, while a read accepts either order — and a read replaces what is held. None "
      "of that "
      "happens on the message path: a read arrives on whichever thread dispatched it, which may be "
      "the audio callback, so the request is a wait-free claim on a patcher-owned slot, the disk "
      "work runs on the background pool, and the contents are parsed in the completion the patcher "
      "delivers at the top of a later block — which is also when outlet 3 bangs. The bare forms of "
      "read and write reuse the last name given, since Max's open a file dialog and a headless "
      "patcher has none, and filetype is consumed and inert for the same reason. Records past the "
      "256-entry bound are dropped and the rest kept, one whose address or message does not fit is "
      "skipped, and a file larger than a slot is refused whole with outlet 3 silent. The "
      "format inherits Max's one limitation: a stored message containing a comma or a semicolon "
      "cannot round-trip through a file. The contents survive a DumpJSON / ParseJSON round "
      "trip, which is Max's 'save data with patcher' and the reason pObject grew a state hook: a "
      "creation parameter is what an object was made with, not what it has since been told, so "
      "rewriting one from run-time state would quietly change the arguments a patch author typed. "
      "Every .coll on a shared name writes those contents and only the one that created the store "
      "reads them back, so a sibling does not reload identical entries over the top and a patcher "
      "loaded into an engine where the name is already live joins the running collection instead "
      "of resetting it. The pointer is not saved — it is run-time position, as .cycle's thresh and "
      ".bucket's freeze are. The store is bounded at 256 entries of at most 256 characters each, "
      "held at addresses of at most 64, and the whole table is allocated when the store is built "
      "and never resized: that bound is what makes the object real-time safe, and it is why the "
      "store is not published as a "
      "copy-on-write snapshot the way GraphState is. That model assumes the writer is the control "
      "thread, and a .coll is written by whichever thread its message arrived on — in-patcher "
      "delivery dispatches on the audio thread — so a store reaching it from a rendering graph "
      "would have to allocate a replacement table there. Bounded storage with .value's "
      "non-blocking guard gives what the mandate is after without that: nothing on any path "
      "allocates, locks or blocks, the guard is never held across a send, and a store that loses "
      "the guard drops rather than waiting. Even sort keeps that promise — the sort key of every "
      "entry is read once into a fixed array, the indices are insertion-sorted against it, and the "
      "permutation is applied by cycle-following through a single scratch entry, so no input costs "
      "more than one entry copy per entry. Anything that does not fit is refused whole and "
      "silently. Calculate() does nothing. Not ported: the editor window (open, wclose), refer, "
      "and the embed / flags save switch.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in", kInletDoc, "at most 256 entries");
  OUTLET_DOC(0, "data", kDataDoc, "");
  OUTLET_DOC(1, "address", kAddressDoc, "");
  OUTLET_DOC(2, "done", kDoneDoc, "");
  OUTLET_DOC(3, "file", kFileDoc, "");
  PARAM_DOC("name", "",
            "Max's shared context: all .coll objects of this name share their contents, through a "
            "store addressed as \"<patcherName>.<name>\" — the same address form .s, .r and .value "
            "use, so two patchers given one name share their collections as they already share "
            "their sends. Empty gives this object a store of its own rather than pooling it with "
            "every other unnamed .coll in the patcher. The name is resolved once, on the control "
            "thread, which is why Max's runtime 'refer' is not ported.",
            "any identifier");
  PARAM_DOC("no-search", "0",
            "Max's second argument, which stops coll hunting for a file named after the "
            "collection when the patch loads. Accepted so a patch brought across from Max builds, "
            "and read for nothing: there is no such hunt here, and adding one would mean reading a "
            "file at construction from a search path this engine does not have.",
            "any int");
}

// ─── naming (issue #684) ──────────────────────────────────────────────────────

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is the only thing that makes `SetParams("")` a real
// reset — dropping the shared name and going back to a private store — rather
// than a no-op that leaves the object on its old name.
PARM_CLEAR() {
  collName.clear();
  noSearch = 0;
  Rebind();
}

PARM_PARSE() {
  Rebind();
}

void gColl::Rebind() {
  // No name, or no patcher to prefix it with, means no address — and no address
  // means a private store. See the class documentation for why an unnamed .coll
  // does not pool on "<patcherName>.".
  std::string address;
  if (!collName.empty() && parent != nullptr) {
    auto* p = static_cast<patcherImplementation*>(parent);
    address = p->Name() + "." + collName;
  }

  // Unchanged binding: keep the store, and with it everything in it. A live
  // SetParams that leaves the name alone must not empty the collection, and
  // neither must the second Rebind() a Set() makes (clear, then parse).
  if (store != nullptr && address == boundAddress) return;

  bool created = false;
  if (address.empty()) {
    store = std::make_shared<collStore>();
    created = true;
  } else {
    store = AcquireNamedStore<collStore>(address, created);
  }
  boundAddress = address;
  // Only the object that brought the store into existence restores saved
  // contents into it; one joining an established name adopts what is there.
  createdStore = created;
}

void gColl::RefreshBinding() {
  Rebind();
}

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
  for (std::size_t i = 0; i < store->count; i++) {
    const Entry& entry = store->entries[i];
    if (address.numeric) {
      if (entry.numeric && entry.index == address.index) return (int)i;
      continue;
    }
    // A symbol query answers from whichever field holds this entry's symbol:
    // its address when it has a symbol one, its alias when it is numeric —
    // Max's "after association, any reference to that symbol will be
    // interpreted as a reference to the number address" (issue #695). An entry
    // with no alias has an empty one, which no address can match because
    // ReadAddress refuses an empty token.
    const std::string& symbol = entry.numeric ? entry.alias : entry.key;
    if (symbol.size() != address.length) continue;
    // Compared against the character range in place: a substr here would
    // allocate on whichever thread the message arrived on.
    if (symbol.compare(0, address.length, address.text, address.length) == 0) return (int)i;
  }
  return -1;
}

int gColl::FindSymbol(const char* text, std::size_t length, int except) const {
  for (std::size_t i = 0; i < store->count; i++) {
    if (except >= 0 && (std::size_t)except == i) continue;
    const Entry& entry = store->entries[i];
    const std::string& symbol = entry.numeric ? entry.alias : entry.key;
    if (symbol.size() != length) continue;
    if (symbol.compare(0, length, text, length) == 0) return (int)i;
  }
  return -1;
}

void gColl::SetAlias(Entry& entry, const char* text, std::size_t length) {
  // assign() into the buffer the store reserved, so a second address costs no
  // allocation on whichever thread the message arrived on.
  entry.alias.assign(text, length);
}

bool gColl::Associate(int index, const char* text, std::size_t length) {
  Address symbol;
  if (!ReadAddress(text, length, symbol)) return false;
  // A numeric token cannot be an alias. #494's guarantee is that the address 1
  // and the address `one` never collide, and an alias spelled `1` would be
  // reached by a numeric lookup of 1 — which already means another entry.
  if (symbol.numeric) return false;

  Address at;
  at.numeric = true;
  at.index = index;
  int position = Find(at);
  // Max: "provided that the number address already exists". Nothing is created
  // and nothing is said.
  if (position < 0) return false;

  // One symbol reaches one entry, and Max says which one gives way: "if the
  // symbol was already being used as an address, or was already associated with
  // a number address, the message that was stored at that address is removed".
  // Removed rather than renumbered — this is `remove`, not `delete`.
  //
  // The entry being associated is excluded from that search, which is what
  // makes associating the same symbol twice a no-op instead of an entry
  // deleting itself as its own collision. cyclone buys the same guarantee with
  // an explicit `ep1->e_symkey != s` because its lookup has no way to skip a
  // candidate.
  const int clash = FindSymbol(text, length, position);
  if (clash >= 0) {
    Erase((std::size_t)clash, false);
    // The table closed the gap, so the target may have moved down one.
    position = Find(at);
    if (position < 0) return false;
  }

  // Max: "Each number address can have only one symbol associated with it" —
  // so this replaces whatever the entry had rather than adding to it, and the
  // symbol it replaces stops meaning anything.
  SetAlias(store->entries[(std::size_t)position], text, length);
  return true;
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
  // The alias travels with the entry, so the shifting that insert, delete and
  // sort do cannot orphan a symbol from the data it names (issue #695).
  dst.alias.assign(src.alias);
  dst.index = src.index;
  dst.numeric = src.numeric;
}

int gColl::HighestIndex() const {
  bool any = false;
  int highest = -1;
  for (std::size_t i = 0; i < store->count; i++) {
    if (!store->entries[i].numeric) continue;
    if (!any || store->entries[i].index > highest) {
      highest = store->entries[i].index;
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
  if (at >= 0) return AssignValue(store->entries[(std::size_t)at], value, length);

  // Full: refused rather than grown, since growing the table would allocate on
  // whichever thread the message arrived on.
  if (store->count >= MAX_ENTRIES) return false;

  Entry& entry = store->entries[store->count];
  // The table is a pool: this slot was live before a `clear` or a `remove`, so
  // a stale alias would make the new entry answer to a symbol nobody gave it
  // (issue #695).
  entry.alias.clear();
  if (address.numeric) {
    SetNumericKey(entry, address.index);
  } else {
    entry.numeric = false;
    entry.index = 0;
    entry.key.assign(address.text, address.length);
  }
  if (!AssignValue(entry, value, length)) return false;
  store->count++;
  return true;
}

bool gColl::InsertAt(int index, const char* value, std::size_t length) {
  if (length > VALUE_CAPACITY) return false;
  if (store->count >= MAX_ENTRIES) return false;

  // Where it lands in storage order: in front of the first entry holding the
  // address it is taking, so `insert` puts the message *at* that address rather
  // than merely near it. Read before the renumbering below moves everything.
  std::size_t position = store->count;
  for (std::size_t i = 0; i < store->count; i++) {
    if (store->entries[i].numeric && store->entries[i].index >= index) {
      position = i;
      break;
    }
  }

  // Max: "incrementing all equal or greater addresses by 1 if necessary".
  for (std::size_t i = 0; i < store->count; i++) {
    if (store->entries[i].numeric && store->entries[i].index >= index) {
      SetNumericKey(store->entries[i], store->entries[i].index + 1);
    }
  }

  for (std::size_t i = store->count; i > position; i--) {
    CopyEntry(store->entries[i], store->entries[i - 1]);
  }

  Entry& entry = store->entries[position];
  // As in StoreAt: the slot came from the pool and may still carry the alias of
  // whatever lived here before (issue #695).
  entry.alias.clear();
  SetNumericKey(entry, index);
  AssignValue(entry, value, length);
  store->count++;
  return true;
}

void gColl::Erase(std::size_t position, bool renumber) {
  if (position >= store->count) return;

  const bool wasNumeric = store->entries[position].numeric;
  const int removed = store->entries[position].index;

  // Closed rather than left as a hole, so storage order — which is what dump,
  // next and prev walk — stays the order the entries were stored in.
  for (std::size_t i = position; i + 1 < store->count; i++) {
    CopyEntry(store->entries[i], store->entries[i + 1]);
  }
  store->count--;

  // Max's `delete`, as against its `remove`: "if the specified address is
  // numeric, all higher numbered addresses are decremented by 1".
  if (renumber && wasNumeric) {
    for (std::size_t i = 0; i < store->count; i++) {
      if (store->entries[i].numeric && store->entries[i].index > removed) {
        SetNumericKey(store->entries[i], store->entries[i].index - 1);
      }
    }
  }

  // Every other .coll on this store keeps its own pointer, and every read of
  // one clamps against the live count — which is the only thing a shrinking
  // table can do for a cursor it cannot reach.
  if (pointer >= store->count) pointer = 0;
}

// ─── the edit messages (issue #684) ───────────────────────────────────────────

bool gColl::Substitute(std::size_t at, int position, const char* data, std::size_t length) {
  if (length == 0) return false;
  Entry& entry = store->entries[at];

  std::size_t begin = 0;
  std::size_t end = 0;
  if (!ElementAt(entry.value.c_str(), entry.value.size(), position, begin, end)) return false;

  // Measured before anything is copied, so an over-long splice leaves the entry
  // exactly as it was rather than half rewritten.
  const std::size_t tail = entry.value.size() - end;
  if (begin + length + tail > VALUE_CAPACITY) return false;

  editScratch.clear();
  editScratch.append(entry.value, 0, begin);
  editScratch.append(data, length);
  editScratch.append(entry.value, end, std::string::npos);
  entry.value.assign(editScratch);
  return true;
}

bool gColl::MergeInto(Entry& entry, const char* data, std::size_t length) {
  if (length == 0) return false;
  const std::size_t separator = entry.value.empty() ? 0 : 1;
  if (entry.value.size() + separator + length > VALUE_CAPACITY) return false;
  // Both appends stay inside the capacity the entry was built with, so neither
  // reallocates.
  if (separator != 0) entry.value.append(1, ' ');
  entry.value.append(data, length);
  return true;
}

bool gColl::CaptureElement(const std::string& value, int position) {
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!ElementAt(value.c_str(), value.size(), position, begin, end)) return false;
  // Into the send buffer reserved at construction, so the guard can be released
  // before the element goes out.
  sendValue.assign(value, begin, end - begin);
  return true;
}

bool gColl::CaptureExtreme(int position, bool wantMax) {
  bool found = false;
  float best = 0.f;
  std::size_t bestEntry = 0;
  std::size_t bestBegin = 0;
  std::size_t bestEnd = 0;

  for (std::size_t i = 0; i < store->count; i++) {
    const std::string& value = store->entries[i].value;
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!ElementAt(value.c_str(), value.size(), position, begin, end)) continue;
    float number = 0.f;
    // An entry whose element at this position is not a number has no value to
    // be lowest or highest, so it is skipped rather than counted as zero.
    if (!ReadNumericToken(value.c_str() + begin, end - begin, number)) continue;
    if (!found || (wantMax ? number > best : number < best)) {
      found = true;
      best = number;
      bestEntry = i;
      bestBegin = begin;
      bestEnd = end;
    }
  }

  if (!found) return false;
  // The token's own text rather than the parsed float, so a stored 60 comes
  // back as the int 60 and a stored 60.5 as that float.
  sendValue.assign(store->entries[bestEntry].value, bestBegin, bestEnd - bestBegin);
  return true;
}

bool gColl::SortKeyLess(const SortKey& a, const SortKey& b) {
  // An entry with nothing at the position sorts first. Max documents no order
  // for it, and leaving it out of the ordering entirely is not an option: every
  // entry has to land somewhere.
  if (a.present != b.present) return !a.present;
  if (!a.present) return false;
  // Numbers before symbols — undocumented in Max, and the reading that keeps a
  // numeric run contiguous instead of interleaving it with words.
  if (a.numeric != b.numeric) return a.numeric;
  if (a.numeric) return a.number < b.number;

  const std::size_t shared = a.length < b.length ? a.length : b.length;
  const int order = shared == 0 ? 0 : std::memcmp(a.text, b.text, shared);
  if (order != 0) return order < 0;
  return a.length < b.length;
}

void gColl::Sort(bool ascending, int element) {
  const std::size_t live = store->count;
  if (live < 2) return;

  // One pass over the table to read every key, so the comparison loop below
  // never parses a number twice. The pointers are into entries that do not move
  // until the permutation is applied.
  for (std::size_t i = 0; i < live; i++) {
    const Entry& entry = store->entries[i];
    SortKey& key = sortKeys[i];
    key = SortKey();

    if (element < 0) {
      // Max: "If the second argument is -1, the index (either number or symbol)
      // associated with the data is used."
      key.present = true;
      key.numeric = entry.numeric;
      if (entry.numeric) {
        key.number = (float)entry.index;
      } else {
        key.text = entry.key.c_str();
        key.length = entry.key.size();
      }
    } else {
      // Max: "If the second argument is not present or is 0, the first item in
      // the data is used. If the second argument is 1 or greater, that data
      // element is used" — read literally, 0 and 1 name the same element.
      const int at = element < 1 ? 1 : element;
      std::size_t begin = 0;
      std::size_t end = 0;
      if (ElementAt(entry.value.c_str(), entry.value.size(), at, begin, end)) {
        key.present = true;
        key.text = entry.value.c_str() + begin;
        key.length = end - begin;
        key.numeric = ReadNumericToken(key.text, key.length, key.number);
      }
    }
    sortOrder[i] = i;
  }

  // Insertion sort over the indices — stable, so entries that compare equal
  // keep the storage order they had, and free of moves entirely on input that
  // is already in order.
  for (std::size_t i = 1; i < live; i++) {
    const std::size_t take = sortOrder[i];
    std::size_t at = i;
    while (at > 0) {
      const SortKey& previous = sortKeys[sortOrder[at - 1]];
      const bool before =
          ascending ? SortKeyLess(sortKeys[take], previous) : SortKeyLess(previous, sortKeys[take]);
      if (!before) break;
      sortOrder[at] = sortOrder[at - 1];
      at--;
    }
    sortOrder[at] = take;
  }

  // Apply the permutation by following its cycles through one scratch entry, so
  // the whole reorder costs at most one entry copy per entry however scrambled
  // the input was.
  for (std::size_t i = 0; i < live; i++)
    sortVisited[i] = 0;

  for (std::size_t i = 0; i < live; i++) {
    if (sortVisited[i] != 0) continue;
    if (sortOrder[i] == i) {
      sortVisited[i] = 1;
      continue;
    }
    CopyEntry(scratchEntry, store->entries[i]);
    std::size_t destination = i;
    for (;;) {
      sortVisited[destination] = 1;
      const std::size_t source = sortOrder[destination];
      if (source == i) break;
      CopyEntry(store->entries[destination], store->entries[source]);
      destination = source;
    }
    CopyEntry(store->entries[destination], scratchEntry);
  }
}

void gColl::SwapAddresses(std::size_t a, std::size_t b) {
  Entry& first = store->entries[a];
  Entry& second = store->entries[b];

  // Max: "The data is unchanged, but the indexes that they use are swapped."
  // Through the scratch entry reserved at construction, so exchanging two keys
  // does not build a third string on the audio thread.
  //
  // The alias goes with them. It is an *address*, not data — the whole point of
  // #695 is that a patch can reach the entry by it — so leaving it behind would
  // make `swap` move half of each address and give the symbol to the other
  // entry's data, which is the one thing the message promises not to do.
  scratchEntry.key.assign(first.key);
  scratchEntry.alias.assign(first.alias);
  const int index = first.index;
  const bool numeric = first.numeric;

  first.key.assign(second.key);
  first.alias.assign(second.alias);
  first.index = second.index;
  first.numeric = second.numeric;

  second.key.assign(scratchEntry.key);
  second.alias.assign(scratchEntry.alias);
  second.index = index;
  second.numeric = numeric;
}

void gColl::Increment(int first) {
  // Max's renumber2 ("increment indices by one") and Max's separate are the
  // same operation: every numeric address at or above `first` moves up by one,
  // which leaves `first` itself open. The two messages differ only in that
  // renumber2's argument defaults to 0 and separate's is required. See the
  // class documentation for the sources (#694, #709).
  for (std::size_t i = 0; i < store->count; i++) {
    Entry& entry = store->entries[i];
    if (entry.numeric && entry.index >= first) SetNumericKey(entry, entry.index + 1);
  }
}

void gColl::Renumber(int first) {
  int next = first;
  for (std::size_t i = 0; i < store->count; i++) {
    Entry& entry = store->entries[i];
    // Symbol addresses are left alone: they have no place in a numeric
    // sequence, and renumbering one would destroy the only handle a patch has
    // on that entry.
    if (!entry.numeric) continue;
    SetNumericKey(entry, next);
    next++;
  }
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
    storeGuard guard(store->busy);
    if (!guard.Held()) return false;
    if (position >= store->count) return false;

    // Copied out under the guard and sent after it: holding it across a
    // synchronous fan-out would make a patch that wires an outlet back into
    // this object's inlet lose its own message to the guard it is still
    // holding. Both buffers were reserved at construction, so this allocates
    // nothing.
    const Entry& entry = store->entries[position];
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
    storeGuard guard(store->busy);
    if (!guard.Held()) return;
    position = Find(address);
  }
  if (position < 0) return;

  // No address outlet: Max sends one only for bang, dump, next, prev and sub,
  // and a patch that looked an address up already had it.
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
    storeGuard guard(store->busy);
    if (!guard.Held()) return true;
    StoreAt(address, text + restBegin, restEnd - restBegin);
    return true;
  }

  if (TokenIs(word, wordLength, "insert", 6)) {
    // Numeric only: Max's insert is "at the address specified by the number",
    // and there is no ordering among symbol addresses to insert into.
    if (!haveAddress || !address.numeric) return true;
    storeGuard guard(store->busy);
    if (!guard.Held()) return true;
    InsertAt(address.index, text + restBegin, restEnd - restBegin);
    return true;
  }

  if (TokenIs(word, wordLength, "append", 6)) {
    // Max: "an index that is one larger than the highest current index".
    storeGuard guard(store->busy);
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
    storeGuard guard(store->busy);
    if (!guard.Held()) return true;
    const int position = Find(address);
    if (position >= 0) Erase((std::size_t)position, renumber);
    return true;
  }

  if (TokenIs(word, wordLength, "clear", 5)) {
    storeGuard guard(store->busy);
    if (!guard.Held()) return true;
    // The strings keep their storage: only `count` says which entries are live,
    // so clearing is O(1) and the capacity a later store needs is still there.
    store->count = 0;
    pointer = 0;
    return true;
  }

  if (TokenIs(word, wordLength, "length", 6)) {
    std::size_t live = 0;
    {
      storeGuard guard(store->busy);
      if (!guard.Held()) return true;
      live = store->count;
    }
    outputs[0].SendInt((int)live, thread);
    return true;
  }

  if (TokenIs(word, wordLength, "goto", 4)) {
    if (!haveAddress) return true;
    storeGuard guard(store->busy);
    if (!guard.Held()) return true;
    const int position = Find(address);
    // Max: "sets the pointer at a specific address, but does not trigger
    // output".
    if (position >= 0) pointer = (std::size_t)position;
    return true;
  }

  if (TokenIs(word, wordLength, "start", 5)) {
    storeGuard guard(store->busy);
    if (!guard.Held()) return true;
    pointer = 0;
    return true;
  }

  if (TokenIs(word, wordLength, "end", 3)) {
    storeGuard guard(store->busy);
    if (!guard.Held()) return true;
    pointer = store->count > 0 ? store->count - 1 : 0;
    return true;
  }

  if (TokenIs(word, wordLength, "next", 4) || TokenIs(word, wordLength, "prev", 4)) {
    const bool forward = TokenIs(word, wordLength, "next", 4);
    std::size_t at = 0;
    {
      storeGuard guard(store->busy);
      if (!guard.Held()) return true;
      if (store->count == 0) return true;
      if (pointer >= store->count) pointer = 0;
      at = pointer;
      // Max steps the pointer after sending; stepping it here lands it on the
      // same entry and saves taking the guard a second time across a fan-out
      // that may have re-entered this object in the meantime.
      pointer = forward ? (at + 1) % store->count : (at == 0 ? store->count - 1 : at - 1);
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

  // The arithmetic and reordering half (issue #684). Split out because these
  // all tokenise their arguments differently from the address-first commands
  // above.
  if (HandleEditCommand(word, wordLength, text, argBegin, argEnd, thread)) return true;

  // The aliasing half (issue #695). None of these sends anything, which is why
  // it does not take a thread.
  if (HandleAliasCommand(word, wordLength, text, argBegin, argEnd)) return true;

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

bool gColl::HandleEditCommand(const char* word, std::size_t wordLength, const char* text,
                              std::size_t argBegin, std::size_t argEnd, YSE::THREAD thread) {
  std::size_t begin = 0;
  std::size_t end = 0;

  const bool isSub = TokenIs(word, wordLength, "sub", 3);
  if (isSub || TokenIs(word, wordLength, "nsub", 4)) {
    // Max: "nsub 2 4 7 replaces the fourth element of address 2 with the value
    // 7. Number values and symbols can both be substituted in this manner."
    Address address;
    if (!ArgAt(text, argBegin, argEnd, 1, begin, end)) return true;
    if (!ReadAddress(text + begin, end - begin, address)) return true;
    int position = 0;
    if (!ReadIntArgument(text, argBegin, argEnd, 2, position)) return true;
    std::size_t dataBegin = 0;
    std::size_t dataEnd = 0;
    if (!ArgTail(text, argBegin, argEnd, 3, dataBegin, dataEnd)) return true;

    bool replaced = false;
    std::size_t at = 0;
    {
      storeGuard guard(store->busy);
      if (!guard.Held()) return true;
      const int found = Find(address);
      if (found < 0) return true;
      at = (std::size_t)found;
      replaced = Substitute(at, position, text + dataBegin, dataEnd - dataBegin);
    }

    // Max: sub is "the same as nsub, except that the message stored at the
    // specified address is sent out after the item has been substituted" — and
    // sub is the fifth trigger Max lists for the address outlet.
    if (isSub && replaced) Output(at, true, thread);
    return true;
  }

  if (TokenIs(word, wordLength, "nth", 3)) {
    // Max: "nth 75 2 will output the second item in the list stored at address
    // 75."
    Address address;
    if (!ArgAt(text, argBegin, argEnd, 1, begin, end)) return true;
    if (!ReadAddress(text + begin, end - begin, address)) return true;
    int position = 0;
    if (!ReadIntArgument(text, argBegin, argEnd, 2, position)) return true;

    {
      storeGuard guard(store->busy);
      if (!guard.Held()) return true;
      const int found = Find(address);
      if (found < 0) return true;
      if (!CaptureElement(store->entries[(std::size_t)found].value, position)) return true;
    }
    // No address outlet: Max lists nth nowhere among its triggers.
    SendTyped(0, sendValue, thread);
    return true;
  }

  const bool wantMax = TokenIs(word, wordLength, "max", 3);
  if (wantMax || TokenIs(word, wordLength, "min", 3)) {
    // Max: "Gets the lowest value in any entry. An optional integer argument
    // (defaults to '1') specifies an element position to use."
    int element = 1;
    (void)ReadIntArgument(text, argBegin, argEnd, 1, element);

    {
      storeGuard guard(store->busy);
      if (!guard.Held()) return true;
      if (!CaptureExtreme(element, wantMax)) return true;
    }
    SendTyped(0, sendValue, thread);
    return true;
  }

  if (TokenIs(word, wordLength, "sort", 4)) {
    // Max: "If the first argument is -1, the items are sorted in ascending
    // order. If the first argument is 1, the items are sorted in descending
    // order." Anything else, a bare sort included, is ascending.
    int order = -1;
    (void)ReadIntArgument(text, argBegin, argEnd, 1, order);
    int element = 0;
    (void)ReadIntArgument(text, argBegin, argEnd, 2, element);

    storeGuard guard(store->busy);
    if (!guard.Held()) return true;
    Sort(order != 1, element);
    return true;
  }

  if (TokenIs(word, wordLength, "swap", 4)) {
    Address first;
    Address second;
    if (!ArgAt(text, argBegin, argEnd, 1, begin, end)) return true;
    if (!ReadAddress(text + begin, end - begin, first)) return true;
    if (!ArgAt(text, argBegin, argEnd, 2, begin, end)) return true;
    if (!ReadAddress(text + begin, end - begin, second)) return true;

    storeGuard guard(store->busy);
    if (!guard.Held()) return true;
    const int a = Find(first);
    const int b = Find(second);
    // Both have to exist: there is no address for a missing entry to give the
    // one that is there, so half a swap would silently lose an address.
    if (a < 0 || b < 0 || a == b) return true;
    SwapAddresses((std::size_t)a, (std::size_t)b);
    return true;
  }

  if (TokenIs(word, wordLength, "merge", 5)) {
    Address address;
    if (!ArgAt(text, argBegin, argEnd, 1, begin, end)) return true;
    if (!ReadAddress(text + begin, end - begin, address)) return true;
    std::size_t dataBegin = 0;
    std::size_t dataEnd = 0;
    if (!ArgTail(text, argBegin, argEnd, 2, dataBegin, dataEnd)) return true;

    storeGuard guard(store->busy);
    if (!guard.Held()) return true;
    const int found = Find(address);
    if (found < 0) {
      // Max: "If the address does not yet exist, it is created."
      StoreAt(address, text + dataBegin, dataEnd - dataBegin);
      return true;
    }
    MergeInto(store->entries[(std::size_t)found], text + dataBegin, dataEnd - dataBegin);
    return true;
  }

  if (TokenIs(word, wordLength, "separate", 8)) {
    // At or above the address given, so the slot that opens is the one named.
    // The reference prose says "greater than", but its own worked example does
    // not; see the class documentation for the sources (#709). Unlike
    // renumber2 the argument is required — there is no address to separate at
    // without one.
    int index = 0;
    if (!ReadIntArgument(text, argBegin, argEnd, 1, index)) return true;
    storeGuard guard(store->busy);
    if (!guard.Held()) return true;
    Increment(index);
    return true;
  }

  if (TokenIs(word, wordLength, "renumber2", 9)) {
    // Max's whole description is "increment indices by one", and that is meant
    // literally: the addresses keep their gaps and each one at or above the
    // argument moves up by one. It is not a 1-based spelling of `renumber`.
    // The argument defaults to 0, so a bare renumber2 moves the whole
    // collection up. See the class documentation for the sources (#694).
    int first = 0;
    ReadIntArgument(text, argBegin, argEnd, 1, first);

    storeGuard guard(store->busy);
    if (!guard.Held()) return true;
    Increment(first);
    return true;
  }

  if (TokenIs(word, wordLength, "renumber", 8)) {
    // The reference states no default starting address; it is 0. See the class
    // documentation (#694).
    int first = 0;
    ReadIntArgument(text, argBegin, argEnd, 1, first);

    storeGuard guard(store->busy);
    if (!guard.Held()) return true;
    Renumber(first);
    return true;
  }

  return false;
}

// ─── the symbol aliases (issue #695) ──────────────────────────────────────────

bool gColl::HandleAliasCommand(const char* word, std::size_t wordLength, const char* text,
                               std::size_t argBegin, std::size_t argEnd) {
  std::size_t begin = 0;
  std::size_t end = 0;

  const bool isDeassoc = TokenIs(word, wordLength, "deassoc", 7);
  if (isDeassoc || TokenIs(word, wordLength, "assoc", 5)) {
    // Max: "the word assoc, followed by a symbol and a number" — the symbol
    // comes first, and cyclone binds both methods as (A_SYMBOL, A_FLOAT).
    if (!ArgAt(text, argBegin, argEnd, 1, begin, end)) return true;
    const char* symbol = text + begin;
    const std::size_t symbolLength = end - begin;
    int index = 0;
    if (!ReadIntArgument(text, argBegin, argEnd, 2, index)) return true;

    storeGuard guard(store->busy);
    if (!guard.Held()) return true;

    if (!isDeassoc) {
      Associate(index, symbol, symbolLength);
      return true;
    }

    // deassoc names both halves, and both have to match: "removes the
    // association between the symbol and the number address". cyclone reads
    // only the number — its handler opens with `s = NULL;` — but that discards
    // an argument its own method signature declares, and taking a patch's
    // association away over a symbol it did not name is the more surprising of
    // the two readings.
    Address at;
    at.numeric = true;
    at.index = index;
    const int position = Find(at);
    if (position < 0) return true;
    Entry& entry = store->entries[(std::size_t)position];
    if (entry.alias.size() != symbolLength) return true;
    if (entry.alias.compare(0, symbolLength, symbol, symbolLength) != 0) return true;
    // The entry stays, with its number and its data; only the second address
    // goes. "The symbol will no longer have any meaning to coll."
    entry.alias.clear();
    return true;
  }

  if (TokenIs(word, wordLength, "nstore", 6)) {
    // Max: "followed by a number and a symbol (or a symbol and a number),
    // followed by any other message" — both orders, which cyclone accepts too
    // and its help file spells out. Max 8's argument table lists only the
    // number-first form; Max 5's prose is the wider of the two and costs
    // nothing to honour.
    if (!ArgAt(text, argBegin, argEnd, 1, begin, end)) return true;
    Address first;
    if (!ReadAddress(text + begin, end - begin, first)) return true;
    const char* firstText = text + begin;
    const std::size_t firstLength = end - begin;

    if (!ArgAt(text, argBegin, argEnd, 2, begin, end)) return true;
    Address second;
    if (!ReadAddress(text + begin, end - begin, second)) return true;
    const char* secondText = text + begin;
    const std::size_t secondLength = end - begin;

    // Exactly one of the two has to be the number and the other the symbol.
    const char* symbol = nullptr;
    std::size_t symbolLength = 0;
    int index = 0;
    if (first.numeric && !second.numeric) {
      index = first.index;
      symbol = secondText;
      symbolLength = secondLength;
    } else if (!first.numeric && second.numeric) {
      index = second.index;
      symbol = firstText;
      symbolLength = firstLength;
    } else {
      return true;
    }

    std::size_t dataBegin = argEnd;
    std::size_t dataEnd = argEnd;
    // The message may be empty, as it may be for `store`: an address with
    // nothing at it is still an address, and refusing one here would invent a
    // rule this object does not have anywhere else.
    (void)ArgTail(text, argBegin, argEnd, 3, dataBegin, dataEnd);

    storeGuard guard(store->busy);
    if (!guard.Held()) return true;

    // Max defines the message by an equivalence — "the same effect as storing
    // the message at an int address, then using the assoc message to associate
    // a symbol with that number" — so that is literally what it is, in that
    // order and for both spellings of the arguments. (cyclone's two orders are
    // not equivalent: each removes a different colliding entry, which is an
    // artefact of its two code paths rather than anything the reference says.)
    Address address;
    address.numeric = true;
    address.index = index;
    if (!StoreAt(address, text + dataBegin, dataEnd - dataBegin)) return true;
    Associate(index, symbol, symbolLength);
    return true;
  }

  if (TokenIs(word, wordLength, "subsym", 6)) {
    // Max: "the first argument to subsym is the new symbol to use, and the
    // second argument is the symbol associator to replace", with the worked
    // example `subsym jack jill` turning `jill, 40 50 60;` into
    // `jack, 40 50 60;` — a plain symbol *address*, so this renames those as
    // well as aliases. cyclone gets that for free by holding both in one field.
    if (!ArgAt(text, argBegin, argEnd, 1, begin, end)) return true;
    Address fresh;
    if (!ReadAddress(text + begin, end - begin, fresh)) return true;
    const char* freshText = text + begin;
    const std::size_t freshLength = end - begin;
    if (fresh.numeric) return true;

    if (!ArgAt(text, argBegin, argEnd, 2, begin, end)) return true;
    Address previous;
    if (!ReadAddress(text + begin, end - begin, previous)) return true;
    if (previous.numeric) return true;

    storeGuard guard(store->busy);
    if (!guard.Held()) return true;

    const int position = Find(previous);
    if (position < 0) return true;
    // Refused rather than allowed to duplicate. cyclone does not check this and
    // will leave two entries answering to one symbol, which makes the second
    // unreachable by name; here one symbol reaches one entry, because that is
    // what makes a lookup's answer independent of storage order. Removing the
    // entry in the way `assoc` does is not the answer either: Max documents
    // that removal for `assoc` alone, and a rename that silently takes another
    // entry's data with it is worse than a rename that does not happen.
    if (FindSymbol(freshText, freshLength, position) >= 0) return true;

    Entry& entry = store->entries[(std::size_t)position];
    if (entry.numeric) {
      // The numeric address is untouched — cyclone's changesymkey touches only
      // the symbol, so subsym is a safe rename for an aliased record.
      SetAlias(entry, freshText, freshLength);
    } else {
      entry.key.assign(freshText, freshLength);
    }
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
  storeGuard guard(store->busy);
  if (!guard.Held()) return false;

  // clear() keeps the capacity reserved at construction, so every append below
  // writes into storage that already exists. FILE_TEXT_CAPACITY is the whole
  // table at its maximum, so the buffer cannot run out.
  fileScratch.clear();
  for (std::size_t i = 0; i < store->count; i++) {
    const Entry& entry = store->entries[i];
    fileScratch.append(entry.key);
    // The second address goes after the first, which is the order Max 5's
    // format paragraph gives: "the address (an int or a symbol), any symbols
    // associated with that address (if the address is an int), a comma ...".
    // See the file section of the class documentation (issue #695).
    if (!entry.alias.empty()) {
      fileScratch.append(1, ' ');
      fileScratch.append(entry.alias);
    }
    fileScratch.append(", ", 2);
    fileScratch.append(entry.value);
    fileScratch.append(";\n", 2);
  }
  return true;
}

bool gColl::LoadFrom(const char* text, std::size_t length) {
  storeGuard guard(store->busy);
  if (!guard.Held()) return false;

  // Max's read replaces the contents. The strings keep their storage — only
  // `count` says which entries are live — so this costs nothing.
  store->count = 0;
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

    // The address field is one token or two: a number, a symbol, or a number
    // and the symbol associated with it (issue #695). Each token is read
    // through the same reader the inlet uses, so a numeric address in the file
    // restores as a numeric address and a symbol one as a symbol — the round
    // trip is only exact if both ends classify identically. Order does not
    // matter and the last token of each kind wins, which is what cyclone's
    // reader does; a record that does not fit is skipped and the rest of the
    // file still loads, and one past MAX_ENTRIES is refused by StoreAt for the
    // same reason a `store` into a full collection is.
    Address number;
    Address symbol;
    bool haveNumber = false;
    bool haveSymbol = false;
    std::size_t token = keyBegin;
    while (token < keyEnd) {
      while (token < keyEnd && IsSelectorSeparator(text[token]))
        token++;
      std::size_t stop = token;
      while (stop < keyEnd && !IsSelectorSeparator(text[stop]))
        stop++;
      if (stop == token) break;
      Address one;
      if (ReadAddress(text + token, stop - token, one)) {
        if (one.numeric) {
          number = one;
          haveNumber = true;
        } else {
          symbol = one;
          haveSymbol = true;
        }
      }
      token = stop;
    }
    if (!haveNumber && !haveSymbol) continue;

    // A number and a symbol together is an aliased entry; either alone is the
    // address it spells.
    const Address& address = haveNumber ? number : symbol;
    if (!StoreAt(address, text + valueBegin, valueEnd - valueBegin)) continue;
    if (haveNumber && haveSymbol) Associate(number.index, symbol.text, symbol.length);
  }
  return true;
}

void gColl::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // Control thread, and the one place both halves of the object's plumbing can
  // be built: the shared name is resolved here so a message never resolves one
  // (issue #684), and the patcher's file table is built here so a `read`
  // arriving later on the audio thread finds it already there (issue #683).
  Rebind();
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
    storeGuard guard(store->busy);
    if (!guard.Held()) return;
    if (store->count == 0) return;
    if (pointer >= store->count) pointer = 0;
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
    storeGuard guard(store->busy);
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
  //
  // Every .coll bound to a shared store writes the contents, not one nominated
  // owner: they are all reading one table, so the copies are identical, and a
  // single writer would mean the collection silently stopped being saved the
  // day that one object was deleted from the patch. Restoring is where the
  // duplication is resolved — see RestoreState.
  storeGuard guard(store->busy);
  if (!guard.Held()) return;
  if (store->count == 0) return;

  for (std::size_t i = 0; i < store->count; i++) {
    nlohmann::json entry;
    entry["key"] = store->entries[i].key;
    entry["value"] = store->entries[i].value;
    // Written only when there is one, so a collection with no aliases saves
    // byte for byte what it saved before #695 (and a patch written against the
    // older form still loads, since the key is simply absent).
    if (!store->entries[i].alias.empty()) entry["alias"] = store->entries[i].alias;
    json["entries"].push_back(entry);
  }
}

void gColl::RestoreState(const nlohmann::json::value_type& json) {
  // Only the object that created the store fills it. Within one patch that
  // makes the first .coll of a name the one that loads and its siblings adopt
  // what it loaded; across patchers it means a patch opened into an engine
  // where its name is already live joins the running collection rather than
  // resetting it under whatever is already using it. `.value`'s rule for its
  // `initial` argument, for `.value`'s reason.
  if (!createdStore) return;

  const auto stored = json.find("entries");
  if (stored == json.end() || !stored->is_array()) return;

  storeGuard guard(store->busy);
  if (!guard.Held()) return;

  store->count = 0;
  pointer = 0;
  for (const auto& entry : *stored) {
    const std::string key = entry.value("key", std::string());
    const std::string message = entry.value("value", std::string());
    const std::string alias = entry.value("alias", std::string());
    Address address;
    // Written back through the same reader the inlet uses, so a numeric key
    // restores as a numeric address and a symbol one as a symbol — the round
    // trip is only exact if both ends classify identically.
    if (!ReadAddress(key.c_str(), key.size(), address)) continue;
    if (!StoreAt(address, message.c_str(), message.size())) continue;
    // The second address, through the same door `assoc` uses — so a hand-edited
    // save that names one symbol twice cannot restore into a collection where a
    // lookup's answer depends on storage order (issue #695).
    if (!alias.empty() && address.numeric) {
      Associate(address.index, alias.c_str(), alias.size());
    }
  }
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::string gColl::KeyAt(std::size_t position) const {
  storeGuard guard(store->busy);
  if (!guard.Held()) return std::string();
  if (position >= store->count) return std::string();
  return store->entries[position].key;
}

std::string gColl::ValueAt(std::size_t position) const {
  storeGuard guard(store->busy);
  if (!guard.Held()) return std::string();
  if (position >= store->count) return std::string();
  return store->entries[position].value;
}

std::string gColl::AliasAt(std::size_t position) const {
  storeGuard guard(store->busy);
  if (!guard.Held()) return std::string();
  if (position >= store->count) return std::string();
  return store->entries[position].alias;
}

std::string gColl::Lookup(const std::string& key) const {
  Address address;
  if (!ReadAddress(key.c_str(), key.size(), address)) return std::string();

  storeGuard guard(store->busy);
  if (!guard.Held()) return std::string();
  const int position = Find(address);
  if (position < 0) return std::string();
  return store->entries[(std::size_t)position].value;
}

#undef className
