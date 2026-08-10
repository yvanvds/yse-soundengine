#include "gZl.h"
#include "../../implementations/logImplementation.h"
#include "../pAtomList.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
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
  // `scramble`'s seed (#524). Spelled with the object's own prefix rather than
  // as a bare `seed`, which is the choice `zlclear` and `zlmaxsize` already
  // made and for their reason: a word Max does not oblige this port to reserve
  // should not be taken away from the data. A patch really can send the list
  // `seed 3` through a `.zl rev`.
  constexpr char kWordSeed[] = "zlseed";

  // The mode vocabulary, in one place. A table rather than a chain of
  // hand-written character comparisons because there are now eight words and
  // two functions that have to agree about them — a spelling that appeared in
  // ReadMode and not in ModeName would be a mode a patch could select and the
  // documentation could not name.
  struct ModeWord {
    char word[9];
    std::size_t length;
    gZl::Mode mode;
  };

  constexpr ModeWord kModeWords[] = {
      {"len", 3, gZl::Mode::LEN},           {"rev", 3, gZl::Mode::REV},
      {"nth", 3, gZl::Mode::NTH},           {"rot", 3, gZl::Mode::ROT},
      {"scramble", 8, gZl::Mode::SCRAMBLE}, {"sort", 4, gZl::Mode::SORT},
      {"swap", 4, gZl::Mode::SWAP},         {"indexmap", 8, gZl::Mode::INDEXMAP},
  };

  // An order entry that names no atom. AtomList::AssignOrder drops it, which is
  // how `indexmap` refuses an index without compacting its own array first —
  // 0xFFFF is free for it because MAX_ATOMS is 256.
  constexpr std::uint16_t kNoAtom = 0xFFFFu;

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
      "empties it, 'zlmaxsize <n>' narrows the working maximum list length to anywhere in 1-256, "
      "and 'zlseed <n>' restarts the random sequence 'scramble' draws from, a non-zero seed "
      "replaying the same shuffles every run and 0 taking an arbitrary stream. None of the four "
      "emits anything. 'mode' is a bare word rather than a prefixed one, "
      "which is Max's choice and not this port's, so a list whose first item is the literal symbol "
      "'mode' is swallowed here as it is there. A mode word this object does not know yet leaves "
      "the mode where it was rather than silently falling back to another one.";

  constexpr char kInletDocRight[] =
      "The mode's argument, and cold: setting it never emits. What it means depends on the mode — "
      "for 'nth' it is the 1-based index of the item to pick, Max's numbering, so 1 is the first "
      "item; for 'rot' the number of places to rotate by, positive toward the end of the list and "
      "negative toward its start; for 'sort' the direction, negative sorting downwards and "
      "anything else upwards; for 'swap' the two 1-based indices to exchange; for 'indexmap' the "
      "whole index map, a list of 1-based positions in the stored list naming what to send and in "
      "what order. 'len', 'rev' and 'scramble' take no argument and ignore it. An int, a float or "
      "a list all set it, and a list sets as many numbers as it carries — up to 256 — so this one "
      "inlet serves the modes that read one number and the two that read several; floats are "
      "truncated, an index being a whole number. Non-numeric items in a list are passed over, and "
      "a list carrying no numbers at all leaves the argument standing rather than clearing it. The "
      "creation arguments set it too, so '.zl nth 2' and '.zl swap 2 4' need no cord here at all.";

  constexpr char kOutletDocLeft[] =
      "The result of the current mode. A result of one atom leaves as the int, float or symbol it "
      "spells rather than as a list of one, so it reaches the inlets an uncollected value would "
      "have reached; a longer one leaves as list text. A result of no atoms sends nothing at all "
      "rather than an empty message. In 'len' mode this is the number of items in the stored list, "
      "in 'rev' mode the stored list in reverse order, in 'nth' mode the item the index names, and "
      "in the reordering modes the stored list rearranged: rotated by 'rot', shuffled by "
      "'scramble', sorted by 'sort', with two items exchanged by 'swap', and re-picked in the "
      "order the map gives by 'indexmap'. None of them consumes the stored list, so a bang "
      "rearranges the same list again rather than rearranging the previous answer — two bangs on a "
      "'scramble' give two shuffles of the input, not a shuffle of a shuffle.";

  constexpr char kOutletDocRight[] =
      "The second half of the result, for the modes that produce two, and sent before the left "
      "outlet — Max's right-to-left rule. In 'nth' mode it carries the stored list with the picked "
      "item removed, Max's 'the right outlet outputs all elements except the selected one'. In "
      "'sort' mode it carries the index map: for each item of the sorted list, the 1-based "
      "position it held in the input. That map is why it is sent first — it can be sent straight "
      "into a second '.zl indexmap' to put a parallel list, the durations beside the pitches, into "
      "the same new order, and it has to be in place before the sorted list arrives and sets that "
      "patch running. Modes that produce a single result ('len', 'rev', 'rot', 'scramble', 'swap', "
      "'indexmap') send nothing here at all, rather than a copy of the input, so that a patch can "
      "tell 'there is no second half' from 'the second half is the whole list'.";

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
      "back under a mode it has just switched to with 'mode <name>'. Eight modes are implemented "
      "so far and the remaining groups follow in their own issues. Three of them read the list: "
      "'len' sends the number of items in it, 'rev' sends it in reverse order, and 'nth' picks one "
      "item by its 1-based index — the item out the left outlet and everything else out the right "
      "one, Max's 'the right outlet outputs all elements except the selected one'. Five of them "
      "rearrange it: 'rot' rotates it by the number of places its argument gives, positive toward "
      "the end and negative toward the start, always modulo the length so any magnitude is legal; "
      "'scramble' shuffles it, from a per-object random sequence that 'zlseed <n>' makes "
      "replayable; 'sort' sorts it, upwards unless its argument is negative, with numbers before "
      "symbols in both directions and equal items keeping the order they arrived in; 'swap' "
      "exchanges the two items its two 1-based indices name; and 'indexmap' re-picks the list in "
      "the order an index map gives, which may name an item twice or leave one out. 'sort' also "
      "publishes the index map of what it did on the right outlet, and that is the object's "
      "headline idiom: sort one list and send the map to a second '.zl indexmap' to put a parallel "
      "list into the same new order. Every index the object reads or writes is 1-based, so the "
      "modes compose. None of the reordering modes consumes the stored list, so a bang rearranges "
      "the same list again rather than rearranging the previous answer. Where a mode fills both "
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
            "'zlmaxsize <n>', 'zlseed <n>'");
  INLET_DOC(1, "argument", kInletDocRight,
            "mode dependent; 1-based index for 'nth', places for 'rot', direction for 'sort', two "
            "indices for 'swap', an index map for 'indexmap'");

  OUTLET_DOC(0, "result", kOutletDocLeft, "any");
  OUTLET_DOC(1, "rest", kOutletDocRight, "any");

  PARAM_DOC("maxsize mode argument", "",
            "The optional maximum list length, the mode word, and the mode's argument, in that "
            "order — Max's own argument shape. A leading token that is wholly an integer is the "
            "maximum list length and is clamped to 1-256; anything else is read as the mode word, "
            "so '.zl nth 2' and '.zl 64 nth 2' are both legal and mean the same thing but for the "
            "ceiling. The mode words implemented so far are 'len', 'rev', 'nth', 'rot', "
            "'scramble', 'sort', 'swap' and 'indexmap'; one this object does not know is named in "
            "the log and ignored, leaving an object that stores what it is sent and emits nothing. "
            "With no mode word at all the object is inert for the same reason — Max's undocumented "
            "no-argument default is 'reg', which belongs to the register group and is not ported "
            "yet, and behaving as a mode the patch did not ask for would be worse than staying "
            "quiet. Every numeric token after the mode word is the mode's argument, in order: one "
            "number is all 'nth', 'rot' and 'sort' read, 'swap' reads the first two as the 1-based "
            "indices to exchange, and 'indexmap' reads the whole run as its map, so '.zl swap 2 4' "
            "and '.zl indexmap 3 1 2' are both legal. The right inlet overwrites the argument "
            "afterwards. The 'scramble' seed is not a creation argument — the argument slot is "
            "taken by the index list — so a patch that needs a reproducible shuffle sends "
            "'zlseed <n>' to the left inlet.",
            "[<1-256>] [len|rev|nth|rot|scramble|sort|swap|indexmap] [<argument> ...]");
}

// ─── the mode vocabulary ────────────────────────────────────────────────────

bool gZl::ReadMode(const char* text, std::size_t length, Mode& out) {
  // Strict: the whole token has to be the word, so a `mode length` does not
  // quietly become `len` and a `mode sorted` does not become `sort`.
  for (const ModeWord& entry : kModeWords) {
    if (length != entry.length) continue;
    if (std::memcmp(text, entry.word, length) != 0) continue;
    out = entry.mode;
    return true;
  }
  return false;
}

const char* gZl::ModeName(Mode mode) {
  for (const ModeWord& entry : kModeWords) {
    if (entry.mode == mode) return entry.word;
  }
  // Mode::NONE, which is the absence of a word rather than a word.
  return "";
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
  arguments = 0;
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

  // The mode's argument: every token after the mode word that reads as a
  // number, in order. One is all `nth`, `rot` and `sort` read, and taking the
  // rest as well is what lets `swap` name two places and `indexmap` carry a
  // whole map — `.zl swap 2 4` and `.zl indexmap 3 1 2` without a cord.
  arguments = 0;
  std::size_t refused = 0;
  for (; index < args.size(); index++) {
    if (args[index].empty()) continue;
    float number = 0.f;
    if (!ReadNumericToken(args[index].c_str(), args[index].size(), number)) continue;
    if (arguments >= AtomList::MAX_ATOMS) {
      refused++;
      continue;
    }
    argumentList[arguments++] = ExprToInt(number);
  }
  argument.store(arguments > 0 ? argumentList[0] : 0, std::memory_order_relaxed);

  if (refused != 0) {
    // Loudly, this being parameter parsing: `.combine`'s split between the two
    // routes, and the same one the maximum-length argument above takes.
    INTERNAL::LogImpl().emit(E_WARNING, std::string("patcher: ") + Type() +
                                            " takes at most 256 argument numbers; " +
                                            std::to_string(refused) + " were dropped");
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

// ─── the reordering modes (#524) ────────────────────────────────────────────
//
// Each of the five fills `order` with an index order over the stored list and
// answers how many entries it wrote, and `SendOrdered` applies it. Splitting
// them that way is what keeps them allocation-free without saying so five
// times: the only storage any of them touches is `order`, `merge` and the
// scratch list, all of them members reserved when the object was built.

std::size_t gZl::OrderRotate(std::size_t size) {
  if (size == 0) return 0;

  // Modulo the length, so a rotation of any magnitude is legal and a whole turn
  // is the identity rather than an empty list — the alternative would make
  // `.zl rot` sensitive to a number a patch has no reason to keep in range.
  // Widened to 64 bit first: negating INT_MIN is undefined, and a `% size` on
  // it is not.
  long long shift = (long long)argument.load(std::memory_order_relaxed) % (long long)size;
  if (shift < 0) shift += (long long)size;
  const std::size_t by = (std::size_t)shift;

  // Positive rotates *toward the end*: the item that was last comes out first.
  for (std::size_t i = 0; i < size; i++)
    order[i] = (std::uint16_t)((i + size - by) % size);
  return size;
}

std::size_t gZl::OrderScramble(std::size_t size) {
  if (size == 0) return 0;
  for (std::size_t i = 0; i < size; i++)
    order[i] = (std::uint16_t)i;

  // Fisher-Yates downwards: every permutation equally likely (up to the
  // residual bias RandomSource::Bounded documents), one draw per item moved,
  // and a bounded number of them — a rejection loop would make the running time
  // unbounded, which does not belong on a path the audio callback takes.
  for (std::size_t i = size; i > 1; i--) {
    const auto j = (std::size_t)rng.Bounded((UInt)i);
    const std::uint16_t moved = order[i - 1];
    order[i - 1] = order[j];
    order[j] = moved;
  }
  return size;
}

bool gZl::SortsBefore(std::size_t a, std::size_t b, bool descending) const {
  const bool numberA = stored.AtomIsNumber(a);
  const bool numberB = stored.AtomIsNumber(b);
  // Numbers before symbols, and in *both* directions: which of the two an atom
  // is, is a type ordering rather than a value one, and a descending sort that
  // swept every symbol to the front would make `sort` and `sort -1` two
  // different questions rather than one asked two ways.
  if (numberA != numberB) return numberA;

  int comparison = 0;
  if (numberA) {
    // The value the atom was classified with on the way in — AtomList decides
    // it once precisely so a sort does not re-read the same characters on every
    // comparison.
    const float valueA = stored.AtomValue(a);
    const float valueB = stored.AtomValue(b);
    if (valueA == valueB) return false;
    comparison = (valueA < valueB) ? -1 : 1;
  } else {
    // Symbols by their characters, shorter first when one is a prefix of the
    // other. Compared in place: this runs O(n log n) times per message.
    const std::size_t lengthA = stored.AtomLength(a);
    const std::size_t lengthB = stored.AtomLength(b);
    const std::size_t shared = (lengthA < lengthB) ? lengthA : lengthB;
    comparison = (shared == 0) ? 0 : std::memcmp(stored.AtomText(a), stored.AtomText(b), shared);
    if (comparison == 0) {
      if (lengthA == lengthB) return false;
      comparison = (lengthA < lengthB) ? -1 : 1;
    }
  }
  return descending ? (comparison > 0) : (comparison < 0);
}

std::size_t gZl::OrderSort(std::size_t size) {
  if (size == 0) return 0;
  // Max's direction argument: negative sorts downwards, anything else upwards,
  // so an unset argument is an ascending sort.
  const bool descending = argument.load(std::memory_order_relaxed) < 0;

  for (std::size_t i = 0; i < size; i++)
    order[i] = (std::uint16_t)i;

  // Bottom-up merge sort through a fixed scratch. Two properties are being
  // bought, and both of them matter here rather than being taste:
  //
  //   - **stable**, so equal items keep the order they arrived in and the map
  //     the right outlet publishes is one a patch can reason about;
  //   - **O(n log n) whatever the data**, so a full 256-item list costs about
  //     two thousand comparisons rather than the sixty-five thousand an
  //     insertion sort would cost in its worst case, on a path the audio
  //     callback takes.
  //
  // std::stable_sort has the first and allocates; std::sort has the second and
  // is not stable.
  for (std::size_t width = 1; width < size; width *= 2) {
    for (std::size_t left = 0; left < size; left += 2 * width) {
      const std::size_t mid = (left + width < size) ? left + width : size;
      const std::size_t right = (left + (2 * width) < size) ? left + (2 * width) : size;
      std::size_t i = left;
      std::size_t j = mid;
      std::size_t out = left;
      // Taken from the right run only when it is *strictly* before the left
      // one, which is exactly what makes the merge stable.
      while (i < mid && j < right)
        merge[out++] = SortsBefore(order[j], order[i], descending) ? order[j++] : order[i++];
      while (i < mid)
        merge[out++] = order[i++];
      while (j < right)
        merge[out++] = order[j++];
    }
    for (std::size_t i = 0; i < size; i++)
      order[i] = merge[i];
  }
  return size;
}

std::size_t gZl::OrderSwap(std::size_t size) {
  // Two places, or there is no exchange to make. An object told one index has
  // not been told half a swap; it has not been told a swap.
  if (size == 0 || arguments < 2) return 0;

  const int first = argumentList[0];
  const int second = argumentList[1];
  // 1-based, as `nth` is, and an index naming no item means the exchange asked
  // for cannot be made — so nothing is sent, which is `nth`'s answer to the
  // same question. Sending the list back unswapped would be sending a list that
  // is not the one the patch asked for.
  if (first < 1 || (std::size_t)first > size) return 0;
  if (second < 1 || (std::size_t)second > size) return 0;

  for (std::size_t i = 0; i < size; i++)
    order[i] = (std::uint16_t)i;
  order[first - 1] = (std::uint16_t)(second - 1);
  order[second - 1] = (std::uint16_t)(first - 1);
  return size;
}

std::size_t gZl::OrderIndexMap(std::size_t size) {
  if (size == 0 || arguments == 0) return 0;

  // Elementwise, unlike `swap`: the map is a list of independent picks, so an
  // index naming no item drops its own element and the rest still arrive. The
  // result is therefore as long as the map rather than as long as the list —
  // a map may name the same item twice, or leave one out entirely.
  const std::size_t count = arguments;
  for (std::size_t i = 0; i < count; i++) {
    const int index = argumentList[i];
    order[i] = (index >= 1 && (std::size_t)index <= size) ? (std::uint16_t)(index - 1) : kNoAtom;
  }
  return count;
}

void gZl::SendOrdered(std::size_t count, YSE::THREAD thread) {
  if (count == 0) return;
  // Onto the scratch copy, so the stored list stays as it arrived and a later
  // bang reorders it again rather than reordering the previous answer.
  work.AssignOrder(stored, order, count);
  SendAtoms(outputs[0], work, render, thread);
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

  case Mode::ROT:
    // Max: rotate the list by the number of places the argument gives.
    SendOrdered(OrderRotate(stored.Size()), thread);
    break;

  case Mode::SCRAMBLE:
    // Max: "output the list in random order". No argument; `zlseed <n>` makes
    // the sequence replayable.
    SendOrdered(OrderScramble(stored.Size()), thread);
    break;

  case Mode::SWAP:
    // Two 1-based indices, and the exchange between them.
    SendOrdered(OrderSwap(stored.Size()), thread);
    break;

  case Mode::INDEXMAP:
    // The stored list re-picked in the order the index map names.
    SendOrdered(OrderIndexMap(stored.Size()), thread);
    break;

  case Mode::SORT: {
    const std::size_t count = OrderSort(stored.Size());
    if (count == 0) break;

    // Right before left, Max's rule and `.trigger`'s — and here it is more than
    // a convention: the map is what a patch feeds to a second `.zl indexmap` to
    // put a parallel list into the same new order, so it has to be in place
    // before the sorted list arrives and sets that patch running.
    //
    // Built into the scratch list, which `SendOrdered` then overwrites with the
    // sorted list itself. 1-based, so the map can be sent straight back into an
    // `indexmap` — see the class notes.
    work.Clear();
    for (std::size_t i = 0; i < count; i++)
      work.AddInt((int)order[i] + 1);
    SendAtoms(outputs[1], work, render, thread);

    SendOrdered(count, thread);
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

// ─── the mode's argument ────────────────────────────────────────────────────

void gZl::TakeArgument(int value) {
  // Under the guard, because the argument is an array as well as an atomic
  // word now and the two have to agree — a `swap` that read a new first index
  // beside an old second one would perform an exchange no patch asked for.
  // A message that finds the object busy is dropped and counted, which is the
  // object's answer everywhere else and the same one that stops a `.zl` wired
  // back into its own inlet from recursing.
  if (!Enter()) return;
  argumentList[0] = value;
  arguments = 1;
  argument.store(value, std::memory_order_relaxed);
  Leave();
}

void gZl::TakeArguments(const char* text, std::size_t length) {
  if (!Enter()) return;

  // Written straight into the array and only committed at the end, so a list
  // carrying no numbers at all leaves the argument standing rather than
  // clearing it — a cord that delivers an occasional symbol should not silently
  // un-point a `swap`.
  std::size_t found = 0;
  std::size_t refused = 0;
  std::size_t i = 0;
  while (i < length) {
    while (i < length && IsSelectorSeparator(text[i]))
      i++;
    if (i >= length) break;
    const std::size_t begin = i;
    while (i < length && !IsSelectorSeparator(text[i]))
      i++;

    float number = 0.f;
    if (!ReadNumericToken(text + begin, i - begin, number)) continue;
    if (found >= AtomList::MAX_ATOMS) {
      refused++;
      continue;
    }
    // An index is a whole number; truncated through the range-checked
    // conversion rather than cast, casting a float outside the int range being
    // undefined.
    argumentList[found++] = ExprToInt(number);
  }

  if (found != 0) {
    arguments = found;
    argument.store(argumentList[0], std::memory_order_relaxed);
  }
  Leave();

  // Counted rather than logged, this being an inlet: the thread may be the
  // audio callback.
  if (refused != 0) CountDrop(refused);
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

  if (TokenIs(value, begin, end, kWordSeed, sizeof(kWordSeed) - 1)) {
    int requested = 0;
    std::size_t cursor = end;
    if (ReadIntArgAt(value, cursor, requested)) {
      // A non-zero seed replays the same shuffles every run; 0 takes an
      // arbitrary stream, which is `.urn`'s contract and Max's `@seed 0`.
      // Real-time safe, so this may arrive on the audio thread — and it takes
      // no guard, RandomSource being safe against a concurrent draw by
      // construction.
      rng.Seed((UInt)requested);
    }
    // Nothing is emitted, and the stored list is untouched: a bang asks for it
    // back under the new sequence.
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
  TakeArgument(value);
}

FLOAT_IN(FloatIn) {
  if (inlet == 0) {
    TakeFloat(value, thread);
    return;
  }
  // An index is a whole number; truncated through the range-checked conversion
  // rather than cast, casting a float outside the int range being undefined.
  TakeArgument(ExprToInt(value));
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

  // The whole list, not just its leading token: `swap` names two places and
  // `indexmap` carries a whole map, so the right inlet has to be able to
  // deliver more than one number. Trimmed of its leading separators in place.
  TakeArguments(value.c_str() + begin, value.size() - begin);
}
