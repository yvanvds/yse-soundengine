#include "gItemList.h"
#include "../../implementations/logImplementation.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className gItemListBase

namespace {

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only: the item names are built once, before
  // the object is published.
  std::string IntText(int value) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(value, digits);
    return std::string(digits, written);
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

  // The next token that reads as a number, advancing `from` past it. Tokens that
  // are not numbers are *skipped* rather than ending the walk, which is
  // ExprParseFloatList's policy and the one `.multislider` and `.matrixctrl`
  // walk a long list with.
  bool NextNumber(const char* text, std::size_t length, std::size_t& from, float& out) {
    std::size_t begin = 0;
    std::size_t end = 0;
    while (NextToken(text, length, from, begin, end)) {
      from = end;
      if (ReadNumericToken(text + begin, end - begin, out)) return true;
    }
    return false;
  }

  // Whether the `length` characters at `text` are exactly `word`. The command
  // word is compared in place for the same reason the tokens are walked in
  // place: a list may arrive on the audio thread.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  constexpr char kInDoc[] =
      "A number selects that item (toggles it with 'multi'); an item name selects that item; a "
      "list of numbers sets the whole selection; 'set <index> <value>' writes one cell; a bang "
      "sends again. Everything here sends.";

  constexpr char kIndexOutDoc[] =
      "The item number: the selection, or with 'multi' the item just changed. Sent last.";

  constexpr char kItemOutDoc[] = "The name of that item.";

  constexpr char kStateOutDoc[] =
      "The whole selection, as the GUI value spells it: the item number, or with 'multi' a 0 or "
      "1 per item. Sent first.";

  constexpr char kItemsParamDoc[] =
      "An optional 'multi' keyword, then the item names. A single number is a count of items "
      "named by their numbers; no items gives two.";

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  REG_BANG_IN(BangIn);

  ADD_OUT_INT; // 0: the item the object points at
  ADD_OUT_LIST; // 1: that item's name
  ADD_OUT_LIST; // 2: the whole selection, as GetGuiValue() spells it

  // The item list is built by ShapeItems(), so a saved `.umenu sine square saw`
  // comes back with the same three items. The clear callback is what makes
  // `SetParams("")` return the object to the no-argument shape rather than
  // leaving the previous items in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // Also the shape ClearParams() restores: DEFAULT_ITEMS items, single select.
  ShapeItems();

  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "selection", kInDoc, "0 to item count - 1");
  OUTLET_DOC(0, "index", kIndexOutDoc, "0 to item count - 1");
  OUTLET_DOC(1, "item", kItemOutDoc, "one item name");
  OUTLET_DOC(2, "state", kStateOutDoc, "the index, or one flag per item");
  PARAM_DOC("items", "", kItemsParamDoc,
            "optional 'multi', then either item names or a single item count 1-256");
}

void gItemListBase::Document(const char* summary) {
  ADD_DESCRIPTION(summary);
}

// ─── the item list ────────────────────────────────────────────────────────────

void gItemListBase::ShapeItems() {
  // Rebuilt rather than patched: the item count is what every index means, and
  // the flag bank has to agree with it. Safe because every caller runs before
  // the object is wired or published — the constructor, and the two parameter
  // callbacks, which patcherImplementation::CreateObjectUnlocked runs before
  // AssignGraphIds. A *live* SetParams never reaches here on a published
  // object: registering the callbacks makes ParamsNeedRebuild() true, so #234
  // replaces the object instead.
  multi = false;
  labels.clear();

  // Parameters::Set splits on single spaces, so a run of them yields empty
  // tokens; an empty token is not an item name.
  std::vector<const std::string*> tokens;
  tokens.reserve(creationArgs.size());
  for (const std::string& token : creationArgs) {
    if (!token.empty()) tokens.push_back(&token);
  }

  std::size_t first = 0;
  if (!tokens.empty() && *tokens[0] == "multi") {
    multi = true;
    first = 1;
  }

  const std::size_t given = tokens.size() - first;
  int requested = 0;

  // Exactly one token, and it reads as a whole number of at least MIN_ITEMS, is
  // a *count* rather than a name — see the class comment on why the one-token
  // case is free to mean that. Strict on purpose, as the rest of the family is:
  // ExprParseFloatList would read `5abc` as 5, and that is a name.
  float asCount = 0.f;
  const bool isCount =
      (given == 1) && ReadNumericToken(*tokens[first], asCount) && asCount >= (float)MIN_ITEMS;

  if (given == 0) {
    // Nothing named: the smallest list that is a choice at all, named by index.
    for (int i = 0; i < DEFAULT_ITEMS; i++)
      labels.push_back(IntText(i));
  } else if (isCount) {
    requested = ExprToInt(asCount);
    int count = requested;
    if (count > MAX_ITEMS) count = MAX_ITEMS;
    for (int i = 0; i < count; i++)
      labels.push_back(IntText(i));
  } else {
    for (std::size_t i = first; i < tokens.size(); i++) {
      if ((int)labels.size() >= MAX_ITEMS) break;
      labels.push_back(*tokens[i]);
    }
    requested = (int)given;
  }

  // The control thread, before the object is wired or published, so this is the
  // one place a clamp can be *said* rather than merely observed through
  // ItemCount().
  if (requested > MAX_ITEMS) {
    INTERNAL::LogImpl().emit(E_WARNING, "patcher: item list of " + IntText(requested) +
                                            " is longer than " + IntText(MAX_ITEMS) +
                                            "; clamped to " + IntText(MAX_ITEMS));
  }

  itemCount = (int)labels.size();

  // Replaced whole rather than resized — a vector of atomics can be neither
  // resized nor element-assigned, and there is nothing to preserve across a
  // re-shape, since the count is what every index is relative to. Every flag is
  // then stored explicitly at rest: an atomic that is merely default-constructed
  // holds no defined value under C++17, and "every item always has a flag" is
  // what lets a bare control be read and dumped.
  flags = std::make_unique<std::atomic<int>[]>((std::size_t)itemCount);
  for (int i = 0; i < itemCount; i++)
    flags[i].store(0, std::memory_order_relaxed);

  current.store(0, std::memory_order_relaxed);

  // The one allocation an outlet-2 send would otherwise need: one flag and one
  // separator per item in multi select, one rendered int in single select.
  stateText.reserve(multi ? (std::size_t)itemCount * 2 : (std::size_t)FORMAT_INT_WIDTH);
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave the no-argument
  // object behind rather than one still holding the previous items.
  creationArgs.clear();
  ShapeItems();
}

PARM_PARSE() {
  ShapeItems();
}

int gItemListBase::Index() const {
  // Bounded on the way out as well as in, `.incdec`'s rule: every value this
  // object reports must name an item it actually has.
  const int at = current.load(std::memory_order_relaxed);
  if (at < 0) return 0;
  if (at >= itemCount) return itemCount - 1;
  return at;
}

const std::string& gItemListBase::Label(int item) const {
  static const std::string none;
  if (item < 0 || item >= itemCount) return none;
  return labels[(std::size_t)item];
}

bool gItemListBase::IsSelected(int item) const {
  if (item < 0 || item >= itemCount) return false;
  if (multi) return flags[(std::size_t)item].load(std::memory_order_relaxed) != 0;
  return item == Index();
}

void gItemListBase::Touch(int item) {
  // An item the list does not have is not an error to report, it is a message
  // with nothing to address — `.gate`'s, `.spray`'s and `.matrixctrl`'s reading
  // of an index out of range.
  if (item < 0 || item >= itemCount) return;
  if (multi) {
    // A click toggles: with several items selectable at once there is no other
    // reading of "the user picked this one again".
    const int now = flags[(std::size_t)item].load(std::memory_order_relaxed);
    flags[(std::size_t)item].store(now != 0 ? 0 : 1, std::memory_order_relaxed);
  }
  current.store(item, std::memory_order_relaxed);
}

void gItemListBase::StoreFlag(int item, float value) {
  if (item < 0 || item >= itemCount) return;
  // Anything but 0 selects, which is how the whole patcher reads a switch. A NaN
  // is not 0 and therefore selects; it cannot reach here from a creation
  // argument, only from a patch that computed one.
  flags[(std::size_t)item].store(value == 0.f ? 0 : 1, std::memory_order_relaxed);
  // The object points at the item the message was about, whether it turned it on
  // or off.
  current.store(item, std::memory_order_relaxed);
}

int gItemListBase::LowestSelected() const {
  for (int i = 0; i < itemCount; i++) {
    if (flags[(std::size_t)i].load(std::memory_order_relaxed) != 0) return i;
  }
  return -1;
}

int gItemListBase::FindLabel(const char* text, std::size_t length) const {
  for (int i = 0; i < itemCount; i++) {
    const std::string& label = labels[(std::size_t)i];
    if (label.size() != length) continue;
    // Compared against the character range in place: a substr here would
    // allocate on whichever thread the message arrived on.
    if (label.compare(0, length, text, length) == 0) return i;
  }
  return -1;
}

// ─── inlets ───────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  // Nothing to store; the hot inlet calculates on the way out, which is the
  // re-send.
  (void)inlet;
  (void)thread;
}

INT_IN(IntIn) {
  FloatIn((float)value, inlet, thread);
}

FLOAT_IN(FloatIn) {
  if (inlet != 0) return;
  (void)thread;
  // "The user clicked item N". A NaN fails this compare and never reaches
  // ExprToInt; a fractional item number truncates towards zero, as every other
  // index in the patcher does.
  if (!(value >= 0.f)) return;
  Touch(ExprToInt(value));
}

LIST_IN(ListIn) {
  // Registered on inlet 0 only, but routed anyway: a later inlet must never
  // start writing the selection because someone added a handler above.
  if (inlet != 0) return;
  (void)thread;

  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  // An empty list addresses nothing and falls through to the re-send a bang
  // gives.
  if (!NextToken(text, length, 0, begin, end)) return;

  if (TokenIs(text + begin, end - begin, "set", 3)) {
    // "set <index> <value>" — issue #551's cell write, and the keyword is what
    // tells it from the whole-state form, which in single select is the same
    // single number.
    std::size_t cursor = end;
    float cell = 0.f;
    float cellValue = 0.f;
    if (!NextNumber(text, length, cursor, cell)) return;
    if (!NextNumber(text, length, cursor, cellValue)) return;
    // Range-checked rather than indexed, as the protocol requires: a cell
    // outside the state — or a NaN, which fails this compare — is dropped, never
    // folded onto a real one.
    if (!(cell >= 0.f)) return;
    const int at = ExprToInt(cell);

    if (multi) {
      StoreFlag(at, cellValue);
      return;
    }
    // Single select has exactly one cell and it holds the index, so the value is
    // the item to select — outright, never toggled, a cell write being absolute
    // by definition.
    if (at != 0) return;
    if (!(cellValue >= 0.f)) return;
    Touch(ExprToInt(cellValue));
    return;
  }

  float number = 0.f;
  if (!ReadNumericToken(text + begin, end - begin, number)) {
    // A leading token that is not a number is an item name — Max's `symbol`
    // message. A name no item has is dropped rather than folded onto item 0.
    const int at = FindLabel(text + begin, end - begin);
    if (at < 0) return;
    Touch(at);
    return;
  }

  if (!multi) {
    // The whole state of a single-select control is its index, which is also
    // exactly the string GetGuiValue() produced. Further numbers are ignored,
    // `.rslider`'s policy for a longer list.
    if (!(number >= 0.f)) return;
    Touch(ExprToInt(number));
    return;
  }

  // The whole state of a multi-select control: one flag per item, positionally,
  // and a short list leaves the items it did not reach alone.
  std::size_t cursor = 0;
  float flag = 0.f;
  int written = 0;
  while (written < itemCount && NextNumber(text, length, cursor, flag)) {
    flags[(std::size_t)written].store(flag == 0.f ? 0 : 1, std::memory_order_relaxed);
    written++;
  }
  if (written == 0) return;
  // No single item was touched, so the object points at the first one that ended
  // up selected; a write that selected nothing leaves the pointer where it was.
  const int lowest = LowestSelected();
  if (lowest >= 0) current.store(lowest, std::memory_order_relaxed);
}

// ─── the GUI value protocol (issue #551) ──────────────────────────────────────

GUI_VALUE() {
  // A local rather than stateText: that buffer belongs to the send path on the
  // audio thread, and this runs on the host thread. One call, one allocation —
  // which is what the protocol asks a bulk read to be.
  if (!multi) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(Index(), digits);
    return std::string(digits, written);
  }

  std::string out;
  out.reserve((std::size_t)itemCount * 2);
  for (int i = 0; i < itemCount; i++) {
    if (i > 0) out.push_back(' ');
    out.push_back(IsSelected(i) ? '1' : '0');
  }
  return out;
}

GUI_VALUE_COUNT() {
  // Single select is the scalar case pObject supplies for free — one cell,
  // holding the index. Multi select is one cell per item.
  return multi ? (unsigned int)itemCount : 1u;
}

GUI_VALUE_AT() {
  // Range-checked against the count rather than indexed, as the protocol
  // requires: past the end is "", never the whole state again.
  if (!multi) {
    if (index != 0) return std::string();
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(Index(), digits);
    return std::string(digits, written);
  }

  if (index >= (unsigned int)itemCount) return std::string();
  return IsSelected((int)index) ? std::string("1") : std::string("0");
}

// ─── output ───────────────────────────────────────────────────────────────────

CALC() {
  // Sampled once, so all three outlets describe the same choice even if a
  // message arriving down one of the cords moves the selection while this send
  // is in flight.
  const int at = Index();

  // Filled immediately before the send rather than kept between sends: the send
  // path is synchronous, so a patch looping an outlet back into the inlet
  // re-enters here inside SendList, and a buffer filled any earlier would be the
  // inner message's by the time this one was read (`.funnel`'s lesson). Into
  // memory reserved by ShapeItems(), so nothing here allocates.
  stateText.clear();
  if (multi) {
    for (int i = 0; i < itemCount; i++) {
      if (i > 0) stateText.push_back(' ');
      stateText.push_back(IsSelected(i) ? '1' : '0');
    }
  } else {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(at, digits);
    stateText.assign(digits, written);
  }

  // Right to left, `.trigger`'s ordering guarantee: the whole state first, then
  // the name, then the index — so both are in hand by the time the index lands
  // on a hot inlet downstream. The name is the object's own stored copy, which
  // is sound because the item list cannot change under a send.
  outputs[2].SendList(stateText, thread);
  outputs[1].SendList(Label(at), thread);
  outputs[0].SendInt(at, thread);
}

#undef className

// ─── the three renderings ─────────────────────────────────────────────────────

gUMenu::gUMenu() : gItemListBase() {
  Document("A menu of named items. A number or an item name selects an item, and it sends the "
           "item number, its name and the whole selection. With 'multi', several items can be "
           "selected at once.");
}

gRadioGroup::gRadioGroup() : gItemListBase() {
  Document("Radio buttons, or check boxes with 'multi', over named items. It works exactly like "
           ".umenu; a host draws it as a group of buttons.");
}

gTab::gTab() : gItemListBase() {
  Document("A row of tabs over named items. It works exactly like .umenu; a host draws it as "
           "tabs.");
}
