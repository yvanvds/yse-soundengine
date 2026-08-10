#include "gZl.h"
#include "../../implementations/logImplementation.h"
#include "../pAtomList.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gZl

namespace {

  // Max's command words on the left inlet. `zlclear` and `zlmaxsize` carry the
  // object's own prefix precisely so they cannot be mistaken for data; `mode`
  // does not, and that is Max's choice rather than this port's — a list whose
  // first item is the literal symbol `mode` is swallowed here exactly as it is
  // there. The `.prepend` discipline would argue for no reserved words at all
  // on an inlet that carries arbitrary lists, and it loses to compatibility:
  // `mode` is how every zl patch in existence switches modes.
  constexpr char kWordMode[] = "mode";
  constexpr char kWordClear[] = "zlclear";
  constexpr char kWordMaxsize[] = "zlmaxsize";

  // True when the token spanning [begin, end) of `value` is exactly `word`.
  // Compared against the character range in place: a substr here would
  // allocate on whichever thread the message arrived on.
  bool TokenIs(const std::string& value, std::size_t begin, std::size_t end, const char* word,
               std::size_t length) {
    if (end - begin != length) return false;
    return value.compare(begin, length, word, length) == 0;
  }

  // The first whitespace-separated token of `value`, as a range. False when
  // there is no token at all.
  bool LeadingToken(const std::string& value, std::size_t& begin, std::size_t& end) {
    begin = 0;
    while (begin < value.size() && IsSelectorSeparator(value[begin]))
      begin++;
    end = begin;
    while (end < value.size() && !IsSelectorSeparator(value[end]))
      end++;
    return end > begin;
  }

  // True when the whole of `token` is a decimal integer — the test that tells
  // Max's optional leading maximum-length argument from a mode word. Stricter
  // than ReadIntArgAt on its own, which would read `4nd` as 4.
  bool WholeInt(const std::string& token, int& out) {
    std::size_t cursor = 0;
    if (!ReadIntArgAt(token, cursor, out)) return false;
    return cursor == token.size();
  }

  constexpr char kInletDocLeft[] =
      "The list inlet, and the hot one. A list arriving here replaces the stored list and is "
      "processed under the current mode straight away; an int or a float is a list of one, as it "
      "is in Max. A bang runs the current mode over the stored list again, which is how a patch "
      "asks for the same list back under a mode it has just changed. Three command words are read "
      "rather than stored: 'mode <name>' switches the mode and keeps the stored list, 'zlclear' "
      "empties it, and 'zlmaxsize <n>' narrows the working maximum list length to anywhere in "
      "1-256. None of the three emits anything. 'mode' is a bare word rather than a prefixed one, "
      "which is Max's choice and not this port's, so a list whose first item is the literal symbol "
      "'mode' is swallowed here as it is there. A mode word this object does not know yet leaves "
      "the mode where it was rather than silently falling back to another one.";

  constexpr char kInletDocRight[] =
      "The mode's argument, and cold: setting it never emits. What it means depends on the mode — "
      "for 'nth' it is the 1-based index of the item to pick, Max's numbering, so 1 is the first "
      "item; 'len' and 'rev' take no argument and ignore it. An int, a float or a list whose "
      "leading token is a number all set it; the float is truncated, an index being a whole "
      "number. The creation arguments set it too, so '.zl nth 2' needs no cord here at all.";

  constexpr char kOutletDocLeft[] =
      "The result of the current mode. A result of one atom leaves as the int, float or symbol it "
      "spells rather than as a list of one, so it reaches the inlets an uncollected value would "
      "have reached; a longer one leaves as list text. A result of no atoms sends nothing at all "
      "rather than an empty message. In 'len' mode this is the number of items in the stored list, "
      "in 'rev' mode the stored list in reverse order, and in 'nth' mode the item the index names.";

  constexpr char kOutletDocRight[] =
      "The second half of the result, for the modes that produce two, and sent before the left "
      "outlet — Max's right-to-left rule. In 'nth' mode it carries the stored list with the picked "
      "item removed, Max's 'the right outlet outputs all elements except the selected one'. Modes "
      "that produce a single result ('len', 'rev') send nothing here at all, rather than a copy of "
      "the input, so that a patch can tell 'there is no second half' from 'the second half is the "
      "whole list'.";

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Again);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // ANY rather than LIST on both: a result of one atom comes out as that atom,
  // an int or a float by its spelling, and this patcher does no coercion at an
  // inlet — a list object that always retyped its output would stop a single
  // picked value from reaching the `.i` a patch wired it to.
  ADD_OUT_ANY;
  ADD_OUT_ANY;

  ADD_PARAM(args);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // The one allocation the object makes outside its two lists, and it happens
  // here rather than on an arrival: a result is built into storage that is
  // already long enough, on whichever thread the list came in on — routinely
  // the audio callback.
  AtomList::ReserveRender(render);

  ADD_DESCRIPTION(
      "The patcher's list-processing workhorse — Max's zl, 'multi-purpose list processing object': "
      "one object with two inlets, two outlets and a mode word that decides what happens between "
      "them. A list arriving at the left inlet is stored and processed under the current mode; a "
      "bang runs the mode over the stored list again, which is how a patch asks for the same list "
      "back under a mode it has just switched to with 'mode <name>'. Three modes are implemented "
      "so far and the remaining groups follow in their own issues: 'len' sends the number of items "
      "in the list, 'rev' sends the list in reverse order, and 'nth' picks one item by its 1-based "
      "index — the item out the left outlet and everything else out the right one, Max's 'the "
      "right outlet outputs all elements except the selected one'. Where a mode fills both "
      "outlets, the right one is sent first, which is Max's right-to-left rule. Max ships a second "
      "spelling of every mode as its own object ('zl.rev'), and it is deliberately not ported: it "
      "is sugar for the mode argument, thirty registered names for one class, and a '.zl.rev' that "
      "can still be told 'mode nth' would not be what its name says. The list storage is bounded "
      "and pre-allocated — at most 256 items, Max's own default maximum list length, spanning at "
      "most 1024 characters, in memory reserved when the object is built. A leading integer "
      "creation argument or the 'zlmaxsize <n>' message narrows the working limit to anywhere in "
      "1-256; it cannot be widened past 256, because a limit that could grow the storage would "
      "allocate on whichever thread sent the list. An item that does not fit is refused and "
      "counted rather than silently truncated, so an over-long list loses its tail and keeps its "
      "head; the count is a counter rather than a log line because the refusing thread may be the "
      "audio callback, while an over-long creation argument is logged, parameter parsing being "
      "control-thread only. A result of one item leaves as the int, float or symbol it spells "
      "rather than as a list of one, and a result of no items sends nothing at all. Re-typing the "
      "creation arguments rebuilds the object and so empties the stored list, which is what "
      "re-typing an object does in Max; the 'mode' message keeps it, which is what a patch "
      "changing modes live wants. Calculate() does nothing and no message path allocates, locks or "
      "blocks: the command words are matched in place, the items are parsed into reserved storage, "
      "numbers are rendered into a stack buffer, and the mode dispatch is one atomic load and a "
      "switch. Two threads sending to the same object are serialised by a single test-and-set "
      "guard whose loser is dropped and counted rather than made to spin, which is also what stops "
      "an object wired back into its own inlet from recursing on the audio thread.");
  ADD_CATEGORY(pCategory::GENERIC);

  INLET_DOC(0, "list", kInletDocLeft,
            "a list, a number, bang, 'mode <name>', 'zlclear', "
            "'zlmaxsize <n>'");
  INLET_DOC(1, "argument", kInletDocRight, "mode dependent; 1-based index for 'nth'");

  OUTLET_DOC(0, "result", kOutletDocLeft, "any");
  OUTLET_DOC(1, "rest", kOutletDocRight, "any");

  PARAM_DOC("maxsize mode argument", "",
            "The optional maximum list length, the mode word, and the mode's argument, in that "
            "order — Max's own argument shape. A leading token that is wholly an integer is the "
            "maximum list length and is clamped to 1-256; anything else is read as the mode word, "
            "so '.zl nth 2' and '.zl 64 nth 2' are both legal and mean the same thing but for the "
            "ceiling. The mode words implemented so far are 'len', 'rev' and 'nth'; one this "
            "object does not know is named in the log and ignored, leaving an object that stores "
            "what it is sent and emits nothing. With no mode word at all the object is inert for "
            "the same reason — Max's undocumented no-argument default is 'reg', which belongs to "
            "the register group and is not ported yet, and behaving as a mode the patch did not "
            "ask for would be worse than staying quiet. The first numeric token after the mode "
            "word is the mode's argument, which for 'nth' is the 1-based index; the right inlet "
            "overwrites it afterwards.",
            "[<1-256>] [len|rev|nth] [<argument>]");
}

// ─── the mode vocabulary ────────────────────────────────────────────────────

bool gZl::ReadMode(const char* text, std::size_t length, Mode& out) {
  // Strict: the whole token has to be the word, so a `mode length` does not
  // quietly become `len`.
  if (length == 3 && text[0] == 'l' && text[1] == 'e' && text[2] == 'n') {
    out = Mode::LEN;
    return true;
  }
  if (length == 3 && text[0] == 'r' && text[1] == 'e' && text[2] == 'v') {
    out = Mode::REV;
    return true;
  }
  if (length == 3 && text[0] == 'n' && text[1] == 't' && text[2] == 'h') {
    out = Mode::NTH;
    return true;
  }
  return false;
}

const char* gZl::ModeName(Mode mode) {
  switch (mode) {
  case Mode::LEN:
    return "len";
  case Mode::REV:
    return "rev";
  case Mode::NTH:
    return "nth";
  case Mode::NONE:
  default:
    return "";
  }
}

std::size_t gZl::Limit() const {
  const int stored_ = limit.load(std::memory_order_relaxed);
  if (stored_ < 1) return 1;
  if ((std::size_t)stored_ > AtomList::MAX_ATOMS) return AtomList::MAX_ATOMS;
  return (std::size_t)stored_;
}

// ─── the creation arguments ─────────────────────────────────────────────────

PARM_CLEAR() {
  // The whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave a usable object
  // behind rather than one still holding the previous creation argument.
  args.clear();
  mode.store((int)Mode::NONE, std::memory_order_relaxed);
  limit.store((int)AtomList::MAX_ATOMS, std::memory_order_relaxed);
  argument.store(0, std::memory_order_relaxed);
  stored.Clear();
  work.Clear();
}

PARM_PARSE() {
  // Control thread only (Parameters::Set), so unlike the inlet path this can
  // afford a reason for what it refuses.
  std::size_t index = 0;

  // Skip the empty tokens Parameters::Set leaves behind for a run of spaces.
  while (index < args.size() && args[index].empty())
    index++;

  // Max's optional leading maximum list length. A token that is wholly an
  // integer is that; anything else is the mode word, which is what makes
  // `.zl nth 2` and `.zl 64 nth 2` both unambiguous.
  if (index < args.size()) {
    int requested = 0;
    if (WholeInt(args[index], requested)) {
      if (requested < 1 || (std::size_t)requested > AtomList::MAX_ATOMS) {
        INTERNAL::LogImpl().emit(E_WARNING, std::string("patcher: ") + Type() +
                                                " maximum list length " + args[index] +
                                                " is outside 1-256; clamped");
      }
      limit.store(requested, std::memory_order_relaxed);
      index++;
    }
  }

  while (index < args.size() && args[index].empty())
    index++;

  // The mode word.
  if (index < args.size()) {
    Mode parsed = Mode::NONE;
    if (ReadMode(args[index].c_str(), args[index].size(), parsed)) {
      mode.store((int)parsed, std::memory_order_relaxed);
    } else {
      // Named rather than silently ignored: an object left inert because its
      // mode word was misspelled is otherwise indistinguishable from one that
      // is simply not wired up yet.
      INTERNAL::LogImpl().emit(E_WARNING, std::string("patcher: ") + Type() + " has no '" +
                                              args[index] +
                                              "' mode; the object stores its input and emits "
                                              "nothing until told a mode it knows");
    }
    index++;
  }

  // The mode's argument: the first token after the mode word that reads as a
  // number. `nth`'s index is the only one so far.
  for (; index < args.size(); index++) {
    if (args[index].empty()) continue;
    float number = 0.f;
    if (!ReadNumericToken(args[index].c_str(), args[index].size(), number)) continue;
    argument.store(ExprToInt(number), std::memory_order_relaxed);
    break;
  }
}

// ─── the guard ──────────────────────────────────────────────────────────────

bool gZl::Enter() {
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    // Another thread is mid-message, or this object is wired back into its own
    // inlet and a send has come round again. Counted rather than spun on: this
    // is a path the audio callback takes.
    CountDrop();
    return false;
  }
  return true;
}

void gZl::Leave() {
  busy.store(false, std::memory_order_release);
}

// ─── the modes ──────────────────────────────────────────────────────────────

void gZl::Run(YSE::THREAD thread) {
  switch (CurrentMode()) {
  case Mode::LEN:
    // Max: "outputs number of elements in the list out the left outlet." An
    // empty stored list is honestly 0 rather than silence — the question has an
    // answer even when the list has nothing in it.
    outputs[0].SendInt((int)stored.Size(), thread);
    break;

  case Mode::REV:
    // Max: "sends the list out the left outlet in reverse order." Reversed on
    // the scratch copy, so the stored list stays as it arrived and a later bang
    // reverses it again rather than un-reversing it.
    work.Assign(stored);
    work.Reverse();
    SendAtoms(outputs[0], work, render, thread);
    break;

  case Mode::NTH: {
    // Max: "outputs the nth element of the list out the left outlet", 1-based,
    // "the right outlet outputs all elements except the selected one."
    const int index = argument.load(std::memory_order_relaxed);
    if (index < 1 || (std::size_t)index > stored.Size()) {
      // An index naming no item has no item to send — and no "everything else"
      // that means anything either, since the whole list is not the remainder
      // of a pick that did not happen. Silence on both outlets.
      return;
    }
    const std::size_t at = (std::size_t)(index - 1);
    // Right before left, Max's rule and `.trigger`'s: a patch downstream may
    // depend on the remainder having arrived before the item does.
    SendAtomsExcept(outputs[1], stored, at, render, thread);
    SendAtom(outputs[0], stored, at, render, thread);
    break;
  }

  case Mode::NONE:
  default:
    // No mode word yet. The list is stored — a later `mode <name>` and a bang
    // still reach it — but nothing is emitted. See the class notes.
    break;
  }
}

void gZl::Take(const char* text, std::size_t length, YSE::THREAD thread) {
  if (!Enter()) return;

  stored.Clear();
  const std::size_t refused = stored.AddTokens(text, length, Limit());
  // Refused rather than truncated, and counted rather than logged: this may be
  // the audio callback. The head is kept, which is the loss that costs a patch
  // least.
  if (refused != 0) CountDrop(refused);

  Run(thread);
  Leave();
}

void gZl::TakeInt(int value, YSE::THREAD thread) {
  if (!Enter()) return;
  stored.Clear();
  if (!stored.AddInt(value, Limit())) CountDrop();
  Run(thread);
  Leave();
}

void gZl::TakeFloat(float value, YSE::THREAD thread) {
  if (!Enter()) return;
  stored.Clear();
  if (!stored.AddFloat(value, Limit())) CountDrop();
  Run(thread);
  Leave();
}

// ─── the command words ──────────────────────────────────────────────────────

bool gZl::Command(const std::string& value, std::size_t begin, std::size_t end) {
  if (TokenIs(value, begin, end, kWordClear, sizeof(kWordClear) - 1)) {
    // Max: "zlclear reinitializes the zl object." The mode and the limit are
    // configuration rather than contents, so they stay.
    if (!Enter()) return true;
    stored.Clear();
    work.Clear();
    Leave();
    return true;
  }

  if (TokenIs(value, begin, end, kWordMode, sizeof(kWordMode) - 1)) {
    std::size_t wordBegin = end;
    while (wordBegin < value.size() && IsSelectorSeparator(value[wordBegin]))
      wordBegin++;
    std::size_t wordEnd = wordBegin;
    while (wordEnd < value.size() && !IsSelectorSeparator(value[wordEnd]))
      wordEnd++;

    Mode parsed = Mode::NONE;
    if (wordEnd > wordBegin && ReadMode(value.c_str() + wordBegin, wordEnd - wordBegin, parsed)) {
      // The stored list survives, which is the whole reason to change modes at
      // run time rather than by re-typing the arguments. Nothing is emitted: a
      // bang asks for the list back under the new mode.
      mode.store((int)parsed, std::memory_order_relaxed);
    }
    // A word this object does not know leaves the mode where it was —
    // `.translate`'s answer, and silent because this may be the audio thread.
    return true;
  }

  if (TokenIs(value, begin, end, kWordMaxsize, sizeof(kWordMaxsize) - 1)) {
    int requested = 0;
    std::size_t cursor = end;
    // Clamped by Limit() on read rather than here, so a live SetParams re-parse
    // is covered by the same range.
    if (ReadIntArgAt(value, cursor, requested)) {
      limit.store(requested, std::memory_order_relaxed);
    }
    // The stored list is left as it is: a narrower limit applies to the next
    // list to arrive, rather than reaching back and shortening one a patch has
    // already been given.
    return true;
  }

  return false;
}

// ─── the inlets ─────────────────────────────────────────────────────────────

BANG_IN(Again) {
  // Registered on the left inlet only, which is where Max documents it: the
  // right inlet holds an argument and has nothing to do with a bang.
  if (inlet != 0) return;
  if (!Enter()) return;
  Run(thread);
  Leave();
}

INT_IN(IntIn) {
  if (inlet == 0) {
    TakeInt(value, thread);
    return;
  }
  argument.store(value, std::memory_order_relaxed);
}

FLOAT_IN(FloatIn) {
  if (inlet == 0) {
    TakeFloat(value, thread);
    return;
  }
  // An index is a whole number; truncated through the range-checked conversion
  // rather than cast, casting a float outside the int range being undefined.
  argument.store(ExprToInt(value), std::memory_order_relaxed);
}

LIST_IN(ListIn) {
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!LeadingToken(value, begin, end)) return;

  if (inlet == 0) {
    if (Command(value, begin, end)) return;
    // Everything else is data, and the whole message is the list — trimmed of
    // its leading separators in place, a substr here being an allocation on
    // whichever thread the message arrived on.
    Take(value.c_str() + begin, value.size() - begin, thread);
    return;
  }

  float number = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, end - begin, number)) return;
  argument.store(ExprToInt(number), std::memory_order_relaxed);
}
