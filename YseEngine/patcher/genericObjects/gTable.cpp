#include "gTable.h"
#include "../../internal/global.h"
#include "../../internal/namedBus.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "../patcherImplementation.h"
#include <cstdint>

using namespace YSE::PATCHER;

#define className gTable

namespace {

  using YSE::INTERNAL::Bus;
  using YSE::INTERNAL::BusValue;

  // The bus is owned by INTERNAL::Global() between init() and close(); skip
  // publishing outside that window so tests instantiating a patcher without
  // first calling `System::init()` keep working through the local path — the
  // gSend / gForward rule, and for the same reason.
  inline bool busAvailable() {
    return YSE::INTERNAL::Global().isActive();
  }

  // The bus truncates a published name at kNameCapacity while the in-patcher
  // PassData path does not, so the two would disagree about where an over-long
  // destination points. `send` refuses such a name outright; this keeps the limit
  // it refuses by pinned to the limit that motivates it — gForward's assertion.
  static_assert(gTable::MAX_NAME_LENGTH == YSE::INTERNAL::NamedBus::kNameCapacity,
                "gTable::MAX_NAME_LENGTH must track NamedBus::kNameCapacity");

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

  // The next whitespace-separated token read as a number, advancing `from` past
  // it. False when there is no token left or the one there is not a number.
  bool NextNumber(const char* text, std::size_t length, std::size_t& from, float& out) {
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!NextToken(text, length, from, begin, end)) return false;
    // Strict, as the rest of the family is: ExprParseFloatList would read `5abc`
    // as 5 and fold `1e999` to 0, and neither answers "is this token a number at
    // all" — which is the question, since a message that is not numbers is not
    // one of Max's methods and has to be ignored rather than half-read.
    if (!ReadNumericToken(text + begin, end - begin, out)) return false;
    from = end;
    return true;
  }

  // Whether the `length` characters at `text` are exactly `word`.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  constexpr char kAddressDoc[] =
      "The address, and the hot inlet — it both writes and reads, and which one happens is decided "
      "by whether a value is waiting on inlet 1. Max's right inlet 'stores the value at the next "
      "index number received at the left inlet', and the waiting value is consumed by exactly one "
      "address: Max's 'cancel' message exists precisely so a patch can drop an armed value it no "
      "longer wants, which it would not need if the value stayed behind. Otherwise the number "
      "stored at that address is sent out the outlet. A float is converted to an int, Max's rule. "
      "An address outside 0..size-1 reads nothing and writes nothing — Max does not say what it "
      "does, and staying quiet is the family's rule and the safer reading, since clamping would "
      "answer a wrong question with a plausible number no wavetable lookup could detect. A "
      "two-number list stores directly and arms nothing: Max's 'the second number is stored at the "
      "address specified by the first number'; anything past the second item is ignored, the "
      "multi-value form being 'set'. A bang is Max's weighted random draw — a 'quantile' with a "
      "random number between 0 and 32768. The messages are 'set <start> <values...>', 'const "
      "<value>', 'clear', 'dump', 'goto <address>', 'next', 'prev', 'min', 'max', 'sum', 'length', "
      "'inv <value>', 'quantile <n>', 'fquantile <f>', 'getbits <address> <start> <bits>', "
      "'setbits "
      "<address> <start> <count> <value>', 'send <name> <address>', 'load', 'normal', 'cancel', "
      "'embed <flag>' and 'flags <save> <dontsave>'. While 'load' is on every number arriving here "
      "is stored at successive addresses from 0 instead, until the table is full or a 'normal' "
      "message arrives; words still dispatch as messages, or 'normal' could never turn it off.";

  constexpr char kValueDoc[] =
      "The value that will be stored at the next address arriving on inlet 0 — Max's 'stores the "
      "value at the next index number received at the left inlet'. Cold: setting it sends nothing "
      "and changes nothing already stored, it only arms the next address to be a store rather than "
      "a read. It is consumed by that one address and not by the one after it, and a 'cancel' on "
      "inlet 0 drops it unused, which is Max's 'so that the next number received in the left inlet "
      "will output a number, rather than storing a number at that address'. A float is converted "
      "to "
      "an int. Nothing is armed until a value arrives, so a fresh object reads rather than writes; "
      "the armed value is run-time state and does not survive a save.";

  constexpr char kOutDoc[] =
      "Everything the object sends, and all of it ints — Max's 'all numbers sent out by table are "
      "sent out the left outlet'. A plain address on inlet 0 sends the value stored there; 'next' "
      "and 'prev' send the value at the pointer, which then advances or retreats and wraps at "
      "either end; 'dump' sends every value in address order from 0; 'min', 'max' and 'sum' send "
      "the smallest, largest and total of the stored values; 'length' sends the size. Four "
      "messages "
      "send an *address* rather than a value: 'inv', which answers with the address of the first "
      "value greater than or equal to the number asked for, and 'quantile' / 'fquantile' / a bare "
      "bang, which read the table as a probability distribution and answer with the address where "
      "the running sum reaches the requested fraction of the total. 'getbits' sends a bit field of "
      "one stored value. Nothing is sent when there is no answer: an address out of range, or an "
      "'inv' no stored value satisfies. Max's second outlet is not here — it bangs only when the "
      "graphic editing window changes the contents, and the YSE patcher is headless; it is left "
      "off "
      "rather than repurposed to fire on message-driven stores, which Max does not do and which "
      "would fan out from a path that may be the audio thread.";

} // namespace

CONSTRUCT() {
  // The whole array, taken once here on the control thread. Nothing on a message
  // path ever resizes it, which is what makes a store from a rendering graph
  // allocation-free. Zeroed, so every live address holds a value from the start:
  // a dense store with holes in it would not be one.
  //
  // First, before the parameter callbacks are registered: both of them write the
  // whole array, so an implementation change that ever let one run during
  // construction would index an empty vector.
  values.assign(MAX_SIZE, 0);

  // The name and the size are both built by ParseParams, so a saved `.table
  // mytable 512` comes back as the object it was. The clear callback is what
  // makes `SetParams("")` return the object to Max's no-argument shape rather
  // than leaving the previous size in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // Inlet 0 is the address and is hot; inlet 1 is the value and is cold. Max's
  // split, and .funbuff's.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);

  // One outlet, and everything on it is an int — see the class documentation for
  // why Max's second outlet is not here.
  ADD_OUT_INT;

  // The one allocation `send` would otherwise need on the message path. The
  // address buffer is sized again in RefreshBusPrefix() once the patcher name it
  // is prefixed with is known.
  sendName.reserve(MAX_NAME_LENGTH);
  busAddress.reserve(MAX_NAME_LENGTH);

  ADD_DESCRIPTION(
      "A fixed-size array of numbers addressed by index — Max's table, 'store and edit an array of "
      "numbers'. The dense store, and the one the patcher had no answer for: .coll is keyed by "
      "arbitrary addresses, .bag has no keys, .capture is a tape and .funbuff is a sparse function "
      "of a handful of points, but none of them is an array. Wavetables, velocity curves, "
      "probability distributions and step sequences all want N slots, every one present and "
      "reachable by index in constant time. Inlet 0 is the address and is hot, inlet 1 is the "
      "value "
      "and is cold, and the same hot inlet writes and reads: Max's right inlet 'stores the value "
      "at "
      "the next index number received at the left inlet', and that armed value is consumed by "
      "exactly one address — Max's 'cancel' message exists to drop one unused, which it would not "
      "need if the value stayed behind. A two-number list stores directly, Max's 'the second "
      "number "
      "is stored at the address specified by the first number'. An address out of range reads and "
      "writes nothing rather than clamping, Max's reference not saying which and silence being the "
      "family's rule. 'quantile' and 'fquantile' read the table as a probability distribution and "
      "answer with the address where the running sum reaches the requested fraction of the total, "
      "and a bang is Max's 'quantile with a random number between 0 and 32768' — a weighted random "
      "draw in one object. 'load' fills the table from a stream of numbers starting at address 0 "
      "and 'normal' ends it; 'inv' answers with the address of the first value at or above a "
      "number; 'getbits' and 'setbits' read and write bit fields of one entry, the field being the "
      "count of bits ending at and including the start bit, which is the reading Max's 'how many "
      "bits to the right of the starting bit location' leaves open. 'set', 'const', 'clear', "
      "'dump', 'goto', 'next', 'prev', 'min', 'max', 'sum', 'length' and 'send' complete Max's "
      "surface. Where issue #498 and Max part company is the creation argument: the issue asks for "
      "a size, while Max's one argument is a name and the size is its 'size' attribute defaulting "
      "to 128. The patcher has no attribute mechanism, so both are honoured — a numeric argument "
      "token is the size and a non-numeric one is Max's name, making '.table mytable', '.table "
      "512' "
      "and '.table mytable 512' all mean what they look like. The array is allocated whole at 4096 "
      "entries on the control thread and never resized, so a larger size request is clamped and "
      "there is no size message; a resize on a message path is exactly the allocation this object "
      "avoids. The name is held and saved so a patch brought across from Max builds, but tables do "
      "not share contents and 'refer' is not ported: issue #498 proposes sharing through "
      "INTERNAL::NamedBus, which is a publish/subscribe value bus with no storage and no way to "
      "answer 'give me the object called X', so sharing would need exactly the shared-name "
      "registry "
      "the issue says not to build — the context .coll, .bag and .funbuff each deferred. Where the "
      "naming scheme does fit is Max's 'send', which hands the value at an address to every .r of "
      "a "
      "given name, and that is ported through .forward's pre-reserved strings so building the "
      "address allocates nothing. Storage is .coll's model: a fixed array plus .value's "
      "non-blocking guard, claimed with one exchange by a loser that drops rather than spinning, "
      "since a .table is written by whichever thread its message arrived on and in-patcher "
      "delivery "
      "dispatches on the audio thread. 'dump' walks item by item rather than capturing the burst "
      "the way .funbuff must: address i is always slot i, so a re-entrant write can change a value "
      "the walk has not reached but never move it, and the walk avoids a 16 KB stack snapshot. "
      "Calculate() does nothing. The contents ride the DumpState / RestoreState hook by default, "
      "which is Max's 'the default behavior is 1 (save the data)' and the opposite of .funbuff's "
      "opt-in embed; the flag is always written so that turning it off survives a reload, while "
      "the "
      "pointer, the armed value and load mode are run-time state and are not saved. Not ported: "
      "read and write (file I/O on a path that may be the audio thread), open and wclose and the "
      "range / signed / notename attributes (all describe the graphic editing window, which is a "
      "non-goal), refer, and the second argument of 'flags', which concerns writing the data to "
      "the "
      "table's own file — the first argument is embed under another name and is honoured.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "address", kAddressDoc, "0 to size-1");
  INLET_DOC(1, "value", kValueDoc, "any int");
  OUTLET_DOC(0, "out", kOutDoc, "any int");
  PARAM_DOC(
      "name size", "128",
      "Max's argument list plus the one thing Max keeps in an attribute. Max's single "
      "argument is a name — 'the argument gives a name to the table' — while the number of "
      "addresses is its 'size' attribute, 'the default is 128 values, indexed with numbers "
      "from 0 to 127'. The patcher has no attribute mechanism, so a token that reads as a "
      "number is taken as the size and a token that does not is taken as the name: '.table "
      "mytable' is Max's object exactly, '.table 512' is issue #498's, and '.table mytable "
      "512' is both. The size is clamped to 1..4096, the array being allocated whole at 4096 "
      "entries once at construction so that nothing on a message path ever has to grow it; a "
      "float is truncated. The name is held so a patch brought across from Max builds the "
      "object it names and so the argument the author typed survives a save unchanged, but it "
      "addresses nothing: two tables of the same name do not share values here, and 'refer' "
      "is not ported. Re-parsing the arguments zeroes the contents, since the object that "
      "comes back is the one the arguments describe.",
      "optional name, optional size 1-4096");
}

// ─── parameters ───────────────────────────────────────────────────────────────

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous size.
  creationArgs.clear();
  tableName.clear();
  size = DEFAULT_SIZE;
  pointer = 0;
  loading = false;
  loadAt = 0;
  hasPending = false;
  pending = 0;
  for (std::size_t i = 0; i < MAX_SIZE; i++)
    values[i] = 0;
}

PARM_PARSE() {
  size = DEFAULT_SIZE;
  tableName.clear();

  for (const std::string& token : creationArgs) {
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens; an empty argument is neither a name nor a size.
    if (token.empty()) continue;

    float number = 0.f;
    // Strict on purpose, as the rest of the family is: this is exactly the
    // "is this token a number at all" question, and it is what tells Max's name
    // argument from the size the patcher has nowhere else to put.
    if (ReadNumericToken(token, number)) {
      const int asked = ExprToInt(number);
      // Clamped rather than honoured at the top: honouring a larger request
      // would mean an array a message path might later have to grow. Raised to 1
      // at the bottom, since a zero-address table could be neither read nor
      // written.
      if (asked < (int)MIN_SIZE) {
        size = MIN_SIZE;
      } else if ((std::size_t)asked > MAX_SIZE) {
        size = MAX_SIZE;
      } else {
        size = (std::size_t)asked;
      }
    } else if (tableName.empty()) {
      tableName = token;
    }
  }

  // A re-parse also zeroes the contents: the object that comes back is the one
  // the arguments describe, and values left over from a different size would be
  // a fragment of a different table.
  for (std::size_t i = 0; i < MAX_SIZE; i++)
    values[i] = 0;
  pointer = 0;
  loading = false;
  loadAt = 0;
  hasPending = false;
  pending = 0;
}

// ─── the patcher's name, for `send` ───────────────────────────────────────────

// `parent` is a patcherImplementation by construction (the patcher hands itself
// to every object via SetParent); the cast mirrors the PassData call below.
void gTable::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  RefreshBusPrefix();
}

void gTable::RefreshBusPrefix() {
  if (parent == nullptr) {
    busPrefix.clear();
  } else {
    auto* p = static_cast<patcherImplementation*>(parent);
    busPrefix = p->Name() + ".";
  }
  // Control thread. Size the address for the longest destination `send` will
  // ever accept under the current prefix, so the message path only refills it.
  busAddress.reserve(busPrefix.size() + MAX_NAME_LENGTH);
}

// ─── reading ──────────────────────────────────────────────────────────────────

bool gTable::Output(std::size_t address, YSE::THREAD thread) {
  int value = 0;
  {
    storeGuard guard(busy);
    if (!guard.Held()) return false;
    // The size is re-read here on every step of a dump rather than captured, and
    // that is safe because address i is always slot i: a re-entrant write can
    // change a value this walk has not reached yet, but it can never move one.
    if (address >= size) return false;
    value = values[address];
  }

  // Sent with the guard released: holding it across a synchronous fan-out would
  // make a patch that wires the outlet back into this object's inlet lose its own
  // message to the guard it is still holding.
  outputs[0].SendInt(value, thread);
  return true;
}

void gTable::LoadOne(int value) {
  // Max: "beginning at address 0 and continuing until the table is filled ... if
  // more numbers are received than will fit in the size of the table, additional
  // numbers are ignored". Load mode stays on: only `normal` ends it.
  if (loadAt >= size) return;
  values[loadAt] = value;
  loadAt++;
}

void gTable::ApplyAddress(int address, YSE::THREAD thread) {
  bool send = false;
  int value = 0;
  {
    storeGuard guard(busy);
    if (!guard.Held()) return;

    // Max's load mode reads the number as data rather than as an address, so it
    // is tested before anything else the inlet does.
    if (loading) {
      LoadOne(address);
      return;
    }

    // Max: the right inlet "stores the value at the next index number received
    // in the left inlet". Consumed by this one address and not by the next —
    // which is what `cancel` exists to interrupt.
    if (hasPending) {
      hasPending = false;
      if (address >= 0 && (std::size_t)address < size) {
        values[(std::size_t)address] = pending;
      }
      return;
    }

    if (address >= 0 && (std::size_t)address < size) {
      value = values[(std::size_t)address];
      send = true;
    }
    // Out of range reads nothing — see the class documentation for why that is
    // preferred to clamping.
  }

  if (send) outputs[0].SendInt(value, thread);
}

void gTable::Quantile(double fraction, YSE::THREAD thread) {
  bool send = false;
  std::size_t address = 0;
  {
    storeGuard guard(busy);
    if (!guard.Held()) return;

    // The total and the walk under one acquisition, so the fraction is measured
    // against the same table the answer comes from: taking them separately would
    // let a re-entrant store change the denominator between the two.
    std::int64_t total = 0;
    for (std::size_t i = 0; i < size; i++)
      total += values[i];
    const double result = fraction * (double)total;

    // Max: "table sends out the address at which the sum of all values up to
    // that address is greater than or equal to the result". One pass, bounded by
    // the size — no allocation and no search.
    std::int64_t running = 0;
    for (std::size_t i = 0; i < size; i++) {
      running += values[i];
      if ((double)running >= result) {
        address = i;
        send = true;
        break;
      }
    }
    if (!send) {
      // A result past the total — reachable with a `quantile` argument above
      // 32768 — answers with the last address rather than nothing: a quantile is
      // a position in the table, and the table has an end. Max does not state
      // this edge. `size` is never 0, so there is always a last address.
      address = size - 1;
      send = true;
    }
  }

  if (send) outputs[0].SendInt((int)address, thread);
}

void gTable::SendTo(const char* name, std::size_t nameLength, int address, YSE::THREAD thread) {
  // Nothing but whitespace names nothing, and an over-long name would address one
  // receiver locally and a truncated one on the bus — gForward's rule, refused
  // silently since this may be the audio thread.
  if (nameLength == 0 || nameLength > MAX_NAME_LENGTH) return;
  if (parent == nullptr) return;

  int value = 0;
  {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    if (address < 0 || (std::size_t)address >= size) return;
    value = values[(std::size_t)address];
  }

  // Both strings were reserved for this on the control thread, so assigning into
  // them here allocates nothing.
  sendName.assign(name, nameLength);
  busAddress.assign(busPrefix);
  busAddress.append(sendName);

  auto* p = static_cast<patcherImplementation*>(parent);
  p->PassData(value, sendName, thread);
  if (busAvailable()) {
    Bus().publish(busAddress, BusValue{value}, thread);
  }
}

// ─── commands ─────────────────────────────────────────────────────────────────

bool gTable::HandleCommand(const char* word, std::size_t length, const std::string& message,
                           std::size_t argOffset, YSE::THREAD thread) {
  const char* text = message.c_str();
  const std::size_t total = message.size();

  if (TokenIs(word, length, "set", 3)) {
    // Max: "the first argument specifies an address. The next number is the value
    // to be stored in that address, and each number after that is stored in a
    // successive address."
    std::size_t cursor = argOffset;
    float start = 0.f;
    if (!NextNumber(text, total, cursor, start)) return true;

    storeGuard guard(busy);
    if (!guard.Held()) return true;
    int at = ExprToInt(start);
    float value = 0.f;
    while (NextNumber(text, total, cursor, value)) {
      // Max's `setresizes` attribute defaults to 0, so a `set` that runs past the
      // end does not grow the table; those values are dropped.
      if (at >= 0 && (std::size_t)at < size) values[(std::size_t)at] = ExprToInt(value);
      at++;
    }
    return true;
  }

  if (TokenIs(word, length, "const", 5)) {
    // Max: "fill the table with a number".
    std::size_t cursor = argOffset;
    float value = 0.f;
    if (!NextNumber(text, total, cursor, value)) return true;
    const int filled = ExprToInt(value);

    storeGuard guard(busy);
    if (!guard.Held()) return true;
    for (std::size_t i = 0; i < size; i++)
      values[i] = filled;
    return true;
  }

  if (TokenIs(word, length, "clear", 5)) {
    // Max: "set all values to 0" — not a resize. A dense table with no entries
    // would not be one, so the addresses stay and only their contents go.
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    for (std::size_t i = 0; i < size; i++)
      values[i] = 0;
    return true;
  }

  if (TokenIs(word, length, "dump", 4)) {
    // Max: "sends all the numbers stored in the table out the left outlet in
    // immediate succession, beginning with address 0". Walked item by item, each
    // step taking the guard on its own — see Output() for why that is sound here
    // and was not in .funbuff.
    for (std::size_t i = 0; Output(i, thread); i++) {}
    return true;
  }

  if (TokenIs(word, length, "goto", 4)) {
    // Max: "sets a pointer to the address specified by the number". Clamped into
    // the table rather than refused: the pointer is a position in a dense array
    // and every out-of-range request has a nearest legal answer, unlike a lookup,
    // where the nearest legal answer would be a wrong value.
    std::size_t cursor = argOffset;
    float where = 0.f;
    if (!NextNumber(text, total, cursor, where)) return true;
    const int asked = ExprToInt(where);

    storeGuard guard(busy);
    if (!guard.Held()) return true;
    if (asked < 0) {
      pointer = 0;
    } else if ((std::size_t)asked >= size) {
      pointer = size - 1;
    } else {
      pointer = (std::size_t)asked;
    }
    return true;
  }

  if (TokenIs(word, length, "next", 4) || TokenIs(word, length, "prev", 4)) {
    // Max: `next` "sends the value stored in the address pointed at by the
    // pointer out the left outlet, then sets the pointer to the next address. If
    // the pointer is currently at the last address in the table, it wraps around
    // to the first address"; `prev` "causes the same output as the next message,
    // but the pointer is then decremented rather than incremented". So the value
    // read is the one at the pointer in both cases, and only the step differs.
    const bool forward = TokenIs(word, length, "next", 4);
    int value = 0;
    {
      storeGuard guard(busy);
      if (!guard.Held()) return true;
      // `pointer` is kept inside the table by goto's clamp and by the wrap below,
      // and `size` is never 0, so there is always a value to read.
      if (pointer >= size) pointer = 0;
      value = values[pointer];
      if (forward) {
        pointer = (pointer + 1 >= size) ? 0 : pointer + 1;
      } else {
        pointer = (pointer == 0) ? size - 1 : pointer - 1;
      }
    }
    outputs[0].SendInt(value, thread);
    return true;
  }

  if (TokenIs(word, length, "min", 3) || TokenIs(word, length, "max", 3)) {
    // Max: "retrieve the minimum / maximum stored value".
    const bool wantMin = TokenIs(word, length, "min", 3);
    int best = 0;
    {
      storeGuard guard(busy);
      if (!guard.Held()) return true;
      best = values[0];
      for (std::size_t i = 1; i < size; i++) {
        if (wantMin ? values[i] < best : values[i] > best) best = values[i];
      }
    }
    // Unlike .funbuff there is no empty case to stay quiet about: a table always
    // has at least one address, and every address always holds a value.
    outputs[0].SendInt(best, thread);
    return true;
  }

  if (TokenIs(word, length, "sum", 3)) {
    // Max: "output the sum of all values". Accumulated in 64 bits — 4096 entries
    // near INT_MAX overflow an int, and a signed overflow is undefined rather
    // than merely wrong — then clamped into the int the outlet carries.
    std::int64_t total64 = 0;
    {
      storeGuard guard(busy);
      if (!guard.Held()) return true;
      for (std::size_t i = 0; i < size; i++)
        total64 += values[i];
    }
    if (total64 > INT32_MAX) total64 = INT32_MAX;
    if (total64 < INT32_MIN) total64 = INT32_MIN;
    outputs[0].SendInt((int)total64, thread);
    return true;
  }

  if (TokenIs(word, length, "length", 6)) {
    // Max: "output the table size". Read under the guard like everything else,
    // even though a re-parse is control-thread only, so the reported size and the
    // contents a dump reports can never come from two different tables.
    std::size_t reported = 0;
    {
      storeGuard guard(busy);
      if (!guard.Held()) return true;
      reported = size;
    }
    outputs[0].SendInt((int)reported, thread);
    return true;
  }

  if (TokenIs(word, length, "inv", 3)) {
    // Max: "finds the first value which is greater than or equal to that number,
    // and sends the address of that value out the left outlet". The inverse of a
    // plain lookup, and the reason it is a scan rather than a search: the values
    // are in address order, not sorted.
    std::size_t cursor = argOffset;
    float wanted = 0.f;
    if (!NextNumber(text, total, cursor, wanted)) return true;
    const int target = ExprToInt(wanted);

    bool found = false;
    std::size_t address = 0;
    {
      storeGuard guard(busy);
      if (!guard.Held()) return true;
      for (std::size_t i = 0; i < size; i++) {
        if (values[i] >= target) {
          address = i;
          found = true;
          break;
        }
      }
    }
    // No value reaches the target, so there is no address to name and nothing is
    // sent — the family's rule, and Max does not state this edge.
    if (found) outputs[0].SendInt((int)address, thread);
    return true;
  }

  if (TokenIs(word, length, "quantile", 8)) {
    // Max: "multiplies the incoming number by the sum of all the numbers in the
    // table. This result is then divided by 2^15 (32,768)."
    std::size_t cursor = argOffset;
    float n = 0.f;
    if (!NextNumber(text, total, cursor, n)) return true;
    Quantile((double)ExprToInt(n) / (double)QUANTILE_SCALE, thread);
    return true;
  }

  if (TokenIs(word, length, "fquantile", 9)) {
    // Max: "given a number between zero and one, multiplies the number by the sum
    // of all the numbers in the table" — the same walk with the multiplier given
    // directly instead of scaled by 32768.
    std::size_t cursor = argOffset;
    float fraction = 0.f;
    if (!NextNumber(text, total, cursor, fraction)) return true;
    Quantile((double)fraction, thread);
    return true;
  }

  if (TokenIs(word, length, "getbits", 7) || TokenIs(word, length, "setbits", 7)) {
    // Max numbers bit locations "0 to 31, from the least significant bit to the
    // most significant bit" and counts "bits to the right of the starting bit
    // location", so the field is the `count` bits ending at and including
    // `start` — see the class documentation for why that reading was chosen.
    const bool writing = TokenIs(word, length, "setbits", 7);
    std::size_t cursor = argOffset;
    float address = 0.f;
    float start = 0.f;
    float count = 0.f;
    if (!NextNumber(text, total, cursor, address)) return true;
    if (!NextNumber(text, total, cursor, start)) return true;
    if (!NextNumber(text, total, cursor, count)) return true;
    float written = 0.f;
    if (writing && !NextNumber(text, total, cursor, written)) return true;

    const int at = ExprToInt(address);
    const int first = ExprToInt(start);
    const int bits = ExprToInt(count);
    // A start outside the word, a count below one, or a field reaching past bit 0
    // names no bits at all and is refused whole rather than clipped to whatever
    // part of it is legal.
    if (first < 0 || first > 31) return true;
    if (bits < 1 || bits > first + 1) return true;

    const int shift = first - bits + 1;
    const std::uint32_t mask = (bits >= 32) ? 0xFFFFFFFFu : ((1u << (unsigned int)bits) - 1u);

    int field = 0;
    bool send = false;
    {
      storeGuard guard(busy);
      if (!guard.Held()) return true;
      if (at < 0 || (std::size_t)at >= size) return true;
      const auto stored = (std::uint32_t)values[(std::size_t)at];
      if (writing) {
        const auto replacement = ((std::uint32_t)ExprToInt(written) & mask) << (unsigned int)shift;
        values[(std::size_t)at] = (int)((stored & ~(mask << (unsigned int)shift)) | replacement);
      } else {
        field = (int)((stored >> (unsigned int)shift) & mask);
        send = true;
      }
    }
    if (send) outputs[0].SendInt(field, thread);
    return true;
  }

  if (TokenIs(word, length, "send", 4)) {
    // Max: "sends the value stored at the incoming address to all receive objects
    // with that name" — the one place issue #498's NamedBus suggestion actually
    // fits, since .s / .r are exactly that mechanism here.
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!NextToken(text, total, argOffset, begin, end)) return true;
    std::size_t cursor = end;
    float address = 0.f;
    if (!NextNumber(text, total, cursor, address)) return true;
    SendTo(text + begin, end - begin, ExprToInt(address), thread);
    return true;
  }

  if (TokenIs(word, length, "load", 4)) {
    // Max: "places the table in load mode ... every number received in the left
    // inlet gets stored in the table, beginning at address 0".
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    loading = true;
    loadAt = 0;
    // An armed value would be consumed by the first number of the load, storing
    // it at that number's *address* instead of loading it. Dropping it here is
    // what makes the load start where Max says it does.
    hasPending = false;
    return true;
  }

  if (TokenIs(word, length, "normal", 6)) {
    // Max: "takes the table out of load mode and reverts it to normal operation".
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    loading = false;
    loadAt = 0;
    return true;
  }

  if (TokenIs(word, length, "cancel", 6)) {
    // Max: "causes table to ignore a number received in the right inlet, so that
    // the next number received in the left inlet will output a number, rather
    // than storing a number at that address".
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    hasPending = false;
    return true;
  }

  if (TokenIs(word, length, "embed", 5)) {
    // Max: "toggles the ability to embed the table and save its data as part of
    // the main patch. The default behavior is 1 (save the data)."
    std::size_t cursor = argOffset;
    float flag = 0.f;
    if (!NextNumber(text, total, cursor, flag)) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    embed = ExprToInt(flag) != 0;
    return true;
  }

  if (TokenIs(word, length, "flags", 5)) {
    // Max: "the first argument affects the Save with Patcher option, and the
    // second argument affects the Don't Save option". The first is `embed` under
    // another name; the second decides whether the data is written to the table's
    // own file, and there are no files here, so it is read and ignored.
    std::size_t cursor = argOffset;
    float withPatcher = 0.f;
    if (!NextNumber(text, total, cursor, withPatcher)) return true;
    storeGuard guard(busy);
    if (!guard.Held()) return true;
    embed = ExprToInt(withPatcher) != 0;
    return true;
  }

  // Max's bang, arriving as a word rather than through the bang inlet — a
  // .message or a .trigger spells it this way. Read as the same weighted draw.
  if (TokenIs(word, length, "bang", 4)) {
    BangIn(0, thread);
    return true;
  }

  return false;
}

// ─── inlets ───────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  // Max: "same as a quantile message with a random number between 0 and 32,768".
  // One draw per bang, from the patcher's own real-time-safe generator.
  const int drawn = (int)random.Bounded((UInt)QUANTILE_SCALE);
  Quantile((double)drawn / (double)QUANTILE_SCALE, thread);
}

INT_IN(IntIn) {
  if (inlet == 1) {
    // Cold: it arms the next address and sends nothing.
    storeGuard guard(busy);
    if (!guard.Held()) return;
    pending = value;
    hasPending = true;
    return;
  }
  ApplyAddress(value, thread);
}

FLOAT_IN(FloatIn) {
  // Max's float method on this object is "convert to int", on both inlets.
  const int truncated = ExprToInt(value);
  if (inlet == 1) {
    storeGuard guard(busy);
    if (!guard.Held()) return;
    pending = truncated;
    hasPending = true;
    return;
  }
  ApplyAddress(truncated, thread);
}

LIST_IN(ListIn) {
  (void)inlet;
  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(text, length, 0, begin, end)) return;

  // A command first. None of the reserved words is a number, so nothing this
  // inlet legitimately carries as data can collide with one — and the words keep
  // dispatching while load mode is on, or `normal` could never turn it off.
  if (HandleCommand(text + begin, end - begin, value, end, thread)) return;

  float first = 0.f;
  if (!ReadNumericToken(text + begin, end - begin, first)) return;

  std::size_t cursor = end;
  float second = 0.f;
  if (!NextNumber(text, length, cursor, second)) {
    // A single number is the int method: a lookup, or a store of an armed value,
    // or one item of a load.
    ApplyAddress(ExprToInt(first), thread);
    return;
  }

  // Max's list method: "the second number is stored at the address (index)
  // specified by the first number". A direct store that arms nothing, and
  // anything past the second item is ignored — `set` is the multi-value form.
  storeGuard guard(busy);
  if (!guard.Held()) return;
  if (loading) {
    // In load mode every number is data, so a list fills successive addresses
    // rather than being read as an address/value pair.
    LoadOne(ExprToInt(first));
    LoadOne(ExprToInt(second));
    float extra = 0.f;
    while (NextNumber(text, length, cursor, extra))
      LoadOne(ExprToInt(extra));
    return;
  }
  const int at = ExprToInt(first);
  if (at >= 0 && (std::size_t)at < size) values[(std::size_t)at] = ExprToInt(second);
}

// ─── persistence ──────────────────────────────────────────────────────────────

void gTable::DumpState(nlohmann::json::value_type& json) {
  // Control thread — patcherImplementation::DumpJSON holds mtx — but the guard is
  // still taken, because a message may be arriving from a rendering graph while
  // the patch is being saved.
  storeGuard guard(busy);
  if (!guard.Held()) return;

  // The flag is written whether it is on or off, which is what makes `embed 0`
  // survive a reload: this object's default is on, so silence would bring it back
  // saving itself again. .funbuff can write nothing at all because its default is
  // off and silence there means the same thing as "off".
  json["embed"] = embed;
  if (!embed) return;

  for (std::size_t i = 0; i < size; i++)
    json["values"].push_back(values[i]);
}

void gTable::RestoreState(const nlohmann::json::value_type& json) {
  storeGuard guard(busy);
  if (!guard.Held()) return;

  embed = json.value("embed", true);

  const auto stored = json.find("values");
  if (stored == json.end() || !stored->is_array()) return;

  // Written back by address, and bounded by the current size rather than by what
  // the file holds: the creation arguments have already been applied when this
  // runs, so a patch edited by hand to hold more values than the table has
  // addresses fills what it can instead of writing past the array.
  std::size_t at = 0;
  for (const auto& entry : *stored) {
    if (at >= size) break;
    // Checked rather than taken on trust: a saved patch is a file, and a
    // non-number here would otherwise throw out of a load.
    values[at] = entry.is_number() ? entry.get<int>() : 0;
    at++;
  }
  // Anything the file did not cover keeps the zero it was constructed with, so
  // every live address still holds a value.
  for (; at < size; at++)
    values[at] = 0;

  pointer = 0;
  loading = false;
  loadAt = 0;
  hasPending = false;
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::size_t gTable::Size() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return size;
}

int gTable::ValueAt(std::size_t address) const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  if (address >= size) return 0;
  return values[address];
}

std::string gTable::Name() const {
  storeGuard guard(busy);
  if (!guard.Held()) return std::string();
  return tableName;
}

bool gTable::Embeds() const {
  storeGuard guard(busy);
  if (!guard.Held()) return false;
  return embed;
}

std::size_t gTable::Pointer() const {
  storeGuard guard(busy);
  if (!guard.Held()) return 0;
  return pointer;
}

bool gTable::Loading() const {
  storeGuard guard(busy);
  if (!guard.Held()) return false;
  return loading;
}

#undef className
