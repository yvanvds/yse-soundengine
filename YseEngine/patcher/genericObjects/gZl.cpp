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
  // hand-written character comparisons because there are now twenty-nine words
  // and two functions that have to agree about them — a spelling that appeared
  // in ReadMode and not in ModeName would be a mode a patch could select and
  // the documentation could not name.
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
      {"mth", 3, gZl::Mode::MTH},           {"slice", 5, gZl::Mode::SLICE},
      {"sub", 3, gZl::Mode::SUB},           {"lookup", 6, gZl::Mode::LOOKUP},
      {"sect", 4, gZl::Mode::SECT},         {"union", 5, gZl::Mode::UNION},
      {"unique", 6, gZl::Mode::UNIQUE},     {"thin", 4, gZl::Mode::THIN},
      {"filter", 6, gZl::Mode::FILTER},     {"compare", 7, gZl::Mode::COMPARE},
      {"change", 6, gZl::Mode::CHANGE},     {"iter", 4, gZl::Mode::ITER},
      {"join", 4, gZl::Mode::JOIN},         {"lace", 4, gZl::Mode::LACE},
      {"delace", 6, gZl::Mode::DELACE},     {"ecils", 5, gZl::Mode::ECILS},
      {"reg", 3, gZl::Mode::REG},           {"group", 5, gZl::Mode::GROUP},
      {"stream", 6, gZl::Mode::STREAM},     {"queue", 5, gZl::Mode::QUEUE},
      {"stack", 5, gZl::Mode::STACK},
  };

  // True when atom @p i of @p a and atom @p j of @p b are the same atom —
  // `sub`'s notion of a match (#525).
  //
  // The same number/symbol split `SortsBefore` makes, and for the same reason:
  // the two have to agree, or a pattern `sub` reported at position 3 would be a
  // pattern `sort` put somewhere else. Numbers compare by the value AtomList
  // decided on the way in, so `1` matches `1.0` — they are the same number, and
  // a patcher list that has been through an arithmetic object routinely spells
  // it the second way. Symbols compare by their characters. A number never
  // matches a symbol.
  //
  // An index out of range answers false rather than trapping, which is the
  // accessors' own contract; a zero length can only mean that, atoms being
  // built from non-empty tokens.
  bool AtomsEqual(const AtomList& a, std::size_t i, const AtomList& b, std::size_t j) {
    const bool numberA = a.AtomIsNumber(i);
    if (numberA != b.AtomIsNumber(j)) return false;
    if (numberA) return a.AtomValue(i) == b.AtomValue(j);

    const std::size_t length = a.AtomLength(i);
    if (length == 0 || length != b.AtomLength(j)) return false;
    return std::memcmp(a.AtomText(i), b.AtomText(j), length) == 0;
  }

  // Strictly "atom @p i of @p a sorts before atom @p j of @p b" — the ordering
  // `sort` imposes (#524) and the one membership binary-searches through
  // (#526).
  //
  // Numbers come before symbols in **both** directions: which of the two an
  // atom is, is a type ordering rather than a value one, and a descending sort
  // that swept every symbol to the front would make `sort` and `sort -1` two
  // different questions rather than one asked two ways. Numbers then compare by
  // the value AtomList decided on the way in, symbols by their characters with
  // the shorter first when one is a prefix of the other.
  //
  // It agrees with `AtomsEqual` by construction — neither atom is before the
  // other exactly when the two are equal — which is what lets equal atoms be
  // found by a binary search and what makes them come out of a sort contiguous.
  bool AtomsBefore(const AtomList& a, std::size_t i, const AtomList& b, std::size_t j,
                   bool descending) {
    const bool numberA = a.AtomIsNumber(i);
    const bool numberB = b.AtomIsNumber(j);
    if (numberA != numberB) return numberA;

    int comparison = 0;
    if (numberA) {
      // The value the atom was classified with on the way in — AtomList decides
      // it once precisely so a sort does not re-read the same characters on
      // every comparison.
      const float valueA = a.AtomValue(i);
      const float valueB = b.AtomValue(j);
      if (valueA == valueB) return false;
      comparison = (valueA < valueB) ? -1 : 1;
    } else {
      // Compared in place: this runs O(n log n) times per message.
      const std::size_t lengthA = a.AtomLength(i);
      const std::size_t lengthB = b.AtomLength(j);
      const std::size_t shared = (lengthA < lengthB) ? lengthA : lengthB;
      comparison = (shared == 0) ? 0 : std::memcmp(a.AtomText(i), b.AtomText(j), shared);
      if (comparison == 0) {
        if (lengthA == lengthB) return false;
        comparison = (lengthA < lengthB) ? -1 : 1;
      }
    }
    return descending ? (comparison > 0) : (comparison < 0);
  }

  // Fill the first @p count entries of @p out with 0..count-1 sorted by the
  // atom of @p list they name. @p scratch must hold @p count entries too.
  //
  // Bottom-up merge sort through a fixed scratch. Two properties are being
  // bought, and both of them matter here rather than being taste:
  //
  //   - **stable**, so equal atoms keep the order they arrived in. That makes
  //     the map `sort` publishes one a patch can reason about, and it is what
  //     lets the set modes (#526) take the *first* entry of a run of equal
  //     atoms and know it is the earliest occurrence in the input;
  //   - **O(n log n) whatever the data**, so a full 256-atom list costs about
  //     two thousand comparisons rather than the sixty-five thousand an
  //     insertion sort would cost in its worst case, on a path the audio
  //     callback takes.
  //
  // std::stable_sort has the first and allocates; std::sort has the second and
  // is not stable.
  void SortIndices(const AtomList& list, std::uint16_t* out, std::uint16_t* scratch,
                   std::size_t count, bool descending) {
    for (std::size_t i = 0; i < count; i++)
      out[i] = (std::uint16_t)i;

    for (std::size_t width = 1; width < count; width *= 2) {
      for (std::size_t left = 0; left < count; left += 2 * width) {
        const std::size_t mid = (left + width < count) ? left + width : count;
        const std::size_t right = (left + (2 * width) < count) ? left + (2 * width) : count;
        std::size_t i = left;
        std::size_t j = mid;
        std::size_t at = left;
        // Taken from the right run only when it is *strictly* before the left
        // one, which is exactly what makes the merge stable.
        while (i < mid && j < right)
          scratch[at++] = AtomsBefore(list, out[j], list, out[i], descending) ? out[j++] : out[i++];
        while (i < mid)
          scratch[at++] = out[i++];
        while (j < right)
          scratch[at++] = out[j++];
      }
      for (std::size_t i = 0; i < count; i++)
        out[i] = scratch[i];
    }
  }

  // True when atom @p at of @p needles appears anywhere in @p hay, whose atoms
  // are named in ascending order by the @p count entries at @p order (#526).
  //
  // A binary search rather than a scan: this is asked once per atom of the
  // other list, and the nested pair is the sixty-five thousand comparisons the
  // class notes reject.
  bool ContainsAtom(const AtomList& hay, const std::uint16_t* order, std::size_t count,
                    const AtomList& needles, std::size_t at) {
    std::size_t low = 0;
    std::size_t high = count;
    while (low < high) {
      const std::size_t mid = low + ((high - low) / 2);
      if (AtomsBefore(hay, order[mid], needles, at, false)) {
        low = mid + 1;
      } else {
        high = mid;
      }
    }
    return low < count && AtomsEqual(hay, order[low], needles, at);
  }

  // Mark, for every atom of @p list, whether it is the first occurrence of its
  // value (#526). @p order is the ascending index order over the list, so equal
  // atoms are contiguous and — the sort being stable — the earliest occurrence
  // of each run comes first.
  void MarkFirstOccurrences(const AtomList& list, const std::uint16_t* order, std::size_t count,
                            bool* first) {
    for (std::size_t i = 0; i < count; i++)
      first[i] = false;
    for (std::size_t k = 0; k < count; k++) {
      if (k != 0 && AtomsEqual(list, order[k], list, order[k - 1])) continue;
      first[order[k]] = true;
    }
  }

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
      "emits anything, and 'zlclear' is also what empties a half-filled 'group', 'stream', 'queue' "
      "or 'stack'. In those four modes a bang does not re-run anything: it consumes what the "
      "object has collected, popping the oldest atom from a 'queue', the newest from a 'stack', "
      "flushing 'group''s partial group and re-sending 'stream''s window without sliding it. "
      "'mode' is a bare word rather than a prefixed one, "
      "which is Max's choice and not this port's, so a list whose first item is the literal symbol "
      "'mode' is swallowed here as it is there. A mode word this object does not know yet leaves "
      "the mode where it was rather than silently falling back to another one.";

  constexpr char kInletDocRight[] =
      "The mode's argument, and cold: setting it never emits. What it means depends on the mode — "
      "for 'nth' it is the 1-based index of the item to pick, Max's numbering, so 1 is the first "
      "item; for 'mth' the same index counted from 0, which is the whole difference between the "
      "two modes; for 'rot' the number of places to rotate by, positive toward the end of the list "
      "and negative toward its start; for 'sort' the direction, negative sorting downwards and "
      "anything else upwards; for 'slice' how many items to send out the left outlet, a count "
      "rather than an index and clamped to the list; for 'swap' the two 1-based indices to "
      "exchange; for 'indexmap' the whole index map, a list of 1-based positions in the stored "
      "list naming what to send and in what order; for 'sub' the list to search the stored list "
      "for; and for 'lookup' the table to read, the stored list then being the 1-based indices "
      "into it. 'len', 'rev' and 'scramble' take no argument and ignore it. An int, a float or a "
      "list all set it, and a list sets as many items as it carries — up to the working maximum "
      "list length — so this one inlet serves the modes that read one number, the ones that read "
      "several, and the ones that read a whole list of anything at all. The set modes read it as "
      "the other list: for 'sect' the list to intersect with, for 'union' the list to add, for "
      "'unique' and 'filter' the items to remove, and for 'compare' the list to compare against. "
      "'thin' takes no argument and ignores it. 'change' is the one mode that also *writes* here — "
      "a list sent to this inlet primes the list it compares against, which is Max's arrangement, "
      "and every list that then arrives at the left inlet becomes the new reference in its turn. "
      "The structural modes read it three ways: 'join' and 'lace' take it as the second list, to "
      "be appended or interleaved; 'iter', 'group', 'stream' and 'ecils' take it as a length in "
      "items — the chunk size, the group size, the window length and how many items to cut from "
      "the end — clamped to the working maximum list length, since a window wider than the object "
      "can hold would never fill; and 'reg' is the one mode for which this inlet carries the "
      "object's *contents* rather than an argument, Max's 'a list received in the right inlet is "
      "stored', so a list sent here is what the next bang sends out. 'delace', 'queue' and 'stack' "
      "take no argument and ignore it. The modes that read numbers "
      "truncate floats, an index being a whole number, pass over non-numeric items, and leave the "
      "argument standing when a list carries no numbers at all rather than clearing it — a cord "
      "that delivers the occasional symbol should not silently un-point a 'swap'. The modes that "
      "read the list as a list ('sub', 'lookup') simply take whatever arrived, a list of symbols "
      "being an ordinary pattern or table rather than a malformed argument. The creation arguments "
      "set it too, so '.zl nth 2', '.zl swap 2 4' and '.zl lookup do re mi' need no cord here at "
      "all.";

  constexpr char kOutletDocLeft[] =
      "The result of the current mode. A result of one atom leaves as the int, float or symbol it "
      "spells rather than as a list of one, so it reaches the inlets an uncollected value would "
      "have reached; a longer one leaves as list text. A result of no atoms sends nothing at all "
      "rather than an empty message. In 'len' mode this is the number of items in the stored list, "
      "in 'rev' mode the stored list in reverse order, in 'nth' and 'mth' mode the item the index "
      "names, and in the reordering modes the stored list rearranged: rotated by 'rot', shuffled "
      "by 'scramble', sorted by 'sort', with two items exchanged by 'swap', and re-picked in the "
      "order the map gives by 'indexmap'. In 'slice' mode it carries the first N items, N being "
      "the argument; in 'sub' mode the 1-based position of every occurrence of the searched-for "
      "list, and nothing at all when there are none; in 'lookup' mode the table entries the stored "
      "list's indices name. The set modes send what is left of the list after the operation: in "
      "'thin' mode the list with every repeat after the first dropped, in 'sect' mode the items "
      "the two lists share, in 'union' mode the two lists added together as sets, and in 'unique' "
      "and 'filter' mode the list with the items named in the right inlet removed. In 'compare' "
      "mode it carries 1 when the two lists are the same list and 0 when they are not, and in "
      "'change' mode the list itself, but only when it differs from the one before it. The "
      "structural modes send what they have restructured: in 'reg' mode the stored list itself, "
      "in 'iter' mode the list as a run of separate messages of N items each — the last one short "
      "when the list does not divide — in 'join' mode the two lists one after the other, in 'lace' "
      "mode the two interleaved, in 'delace' mode the items at the odd positions of the input, in "
      "'ecils' mode everything but the last N items, in 'group' mode each complete group of N "
      "items as it becomes complete, and in 'stream' mode the last N items received, sent again "
      "on every arrival once there are that many. In 'queue' and 'stack' mode a bang sends one "
      "item — the oldest received for 'queue', the newest for 'stack' — and removes it, while an "
      "arriving list only adds to the store and sends nothing. Apart from those four, none of "
      "them consumes the stored list, so a bang rearranges the same "
      "list again rather than rearranging the previous answer — two bangs on a 'scramble' give two "
      "shuffles of the input, not a shuffle of a shuffle.";

  constexpr char kOutletDocRight[] =
      "The second half of the result, for the modes that produce two, and sent before the left "
      "outlet — Max's right-to-left rule. In 'nth' and 'mth' mode it carries the stored list with "
      "the picked item removed, Max's 'the right outlet outputs all elements except the selected "
      "one'. In 'slice' mode it carries everything past the first N items, so the two outlets "
      "together are the whole list cut in two. In 'sort' mode it carries the index map: for each "
      "item of the sorted list, the 1-based position it held in the input. That map is why it is "
      "sent first — it can be sent straight into a second '.zl indexmap' to put a parallel list, "
      "the durations beside the pitches, into the same new order, and it has to be in place before "
      "the sorted list arrives and sets that patch running. In 'sub' mode it carries how many "
      "occurrences were found, and it is sent even when that is 0, which is the only way a patch "
      "can tell 'searched, found nothing' from 'no pattern to search for yet' — the left outlet is "
      "silent in both cases. In 'filter' mode it carries the 1-based positions the surviving items "
      "held in the input, which is what makes the mode composable: what it reports is what 'nth' "
      "takes. In 'compare' mode it carries the 1-based positions at which the two lists differ, "
      "and nothing at all when they match; two lists of different lengths differ at every position "
      "past the shorter one. In 'change' mode it carries 1 when the list differs from the one "
      "before it and 0 when it does not, and it is sent either way, which is the only thing that "
      "makes the left outlet's silence readable. In 'sect' mode it carries a bang when the two "
      "lists have nothing in common, Max's own signal for that case and the only way a patch tells "
      "an empty intersection from an object nothing has reached yet. In 'delace' mode it carries "
      "the items at the even positions of the input — the two outlets together being the input "
      "pulled back apart into the two lists a 'lace' would have made it from — and in 'ecils' "
      "mode the last N items, so the two outlets are the whole list cut in two counting from the "
      "end rather than from the start, which is the only difference between 'ecils' and 'slice'. "
      "In 'stream' mode it carries how many more items the window still wants, 0 meaning the left "
      "outlet is carrying a complete one; Max sends its flag here in answer to the right inlet, "
      "which this object's cold inlet cannot do, and a shortfall is that signal in the shape this "
      "object can carry. In 'queue' and 'stack' mode it carries a bang when there was nothing "
      "left to pop, which is how a patch drains the store: bang until this outlet answers. Modes "
      "that produce a single result ('len', 'rev', 'rot', 'scramble', 'swap', 'indexmap', "
      "'lookup', 'thin', 'union', 'unique', 'reg', 'iter', 'join', 'lace', 'group') send nothing "
      "here at all, rather than a copy of the input, so "
      "that a patch can tell 'there is no second half' from 'the second half is the whole list'.";

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
      "back under a mode it has just switched to with 'mode <name>'. Twenty-nine modes are "
      "implemented so far; only Max's 'median' and 'sum' are still to come. Three of them read "
      "the list: "
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
      "list into the same new order. Four more extract from it: 'mth' is 'nth' counted from 0, "
      "which is the whole difference between the two and the object's one deliberate exception to "
      "1-based numbering — 0-based picking keeps a name of its own instead of becoming a trap; "
      "'slice' cuts the list in two, the first N items out the left outlet and the rest out the "
      "right, N being a count rather than an index and so clamped to the list; 'sub' searches the "
      "stored list for the list in the right inlet and sends the 1-based position of every "
      "occurrence, overlapping ones included, with the number of them out the right outlet even "
      "when it is 0; and 'lookup' is 'indexmap' with the two lists swapped — the right inlet holds "
      "a table and the stored list is the 1-based indices to read from it, so a stream of numbers "
      "becomes a stream of table entries. Every index the object reads or writes is 1-based except "
      "'mth', so the modes compose: what 'sub' reports is exactly what 'nth' takes. Seven treat "
      "the list as a set rather than as a sequence, which is how a patch answers 'which of these "
      "notes are in the scale' or 'which are new since last time': 'thin' drops every repeat after "
      "the first, 'sect' sends what the stored list and the right inlet's list have in common, "
      "'union' sends the two added together with shared items appearing once, and 'unique' and "
      "'filter' send the stored list with the right inlet's items taken out of it. Those two "
      "select the same items — which is Max's arrangement rather than a duplication here — and "
      "differ in what the right outlet says: 'filter' reports the 1-based positions the survivors "
      "held, so it composes with 'nth' the way 'sub' does, while 'unique' says nothing. The set "
      "operations really are sets, so 'thin', 'sect' and 'union' each produce every item once, at "
      "the place it first appeared; 'unique' and 'filter' are removals rather than set operations "
      "and keep the list as it arrived, duplicates and positions included, minus what matched. "
      "The last two ask whether two lists are the same list: 'compare' sends 1 or 0 and, when they "
      "differ, the 1-based positions at which they do, counting a length difference as a "
      "difference at every position past the shorter list; and 'change' sends the list on only "
      "when it is not the one that came before it, with 1 or 0 out the right outlet either way. "
      "'change' is the one mode that writes to the right inlet's list, because in Max that list is "
      "its comparison reference — a list sent there primes it, and each list that arrives becomes "
      "the reference in its turn, so banging the same list twice reports no change the second "
      "time. Membership is answered by ranking each list once and binary-searching rather than by "
      "comparing every item against every other, which would be sixty-five thousand comparisons on "
      "two full-length lists and is the same objection that made 'sub' a Knuth-Morris-Pratt search "
      "and 'sort' a merge sort. Ten more are structural — the plumbing that lets list-shaped and "
      "stream-shaped data meet. Six of them restructure whatever arrives: 'reg' is a register, "
      "holding a list and sending it again on a bang, with the right inlet priming it silently; "
      "'iter' sends the list out as a run of separate messages of N items each; 'join' and 'lace' "
      "combine the stored list with the right inlet's, one after the other or interleaved; "
      "'delace' pulls a laced list back apart into two; and 'ecils' is 'slice' counting from the "
      "end. The other four accumulate across messages, which is what makes them the only modes "
      "whose answer depends on what came before: 'group' collects a stream and sends it on in "
      "fixed-size lists, 'stream' keeps a sliding window of the last N items, and 'queue' and "
      "'stack' are a FIFO and a LIFO that a bang pops one item at a time — from the front and "
      "from the back respectively, which is the whole difference between them. Those four share "
      "one store, so switching between them live keeps the material rather than silently starting "
      "a second buffer, and 'zlclear' is what empties it. Apart from those four, none of the "
      "reordering, extracting, set or structural modes consumes the stored list, so a bang "
      "rearranges "
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
            "mode dependent; 1-based index for 'nth', 0-based for 'mth', places for 'rot', "
            "direction for 'sort', a count for 'slice' and 'ecils', a length in items for 'iter', "
            "'group' and 'stream', two indices for 'swap', an index map for "
            "'indexmap', a search list for 'sub', a lookup table for 'lookup', the other list for "
            "'sect', 'union', 'unique', 'filter', 'compare', 'change', 'join' and 'lace', the "
            "stored contents for 'reg'");

  OUTLET_DOC(0, "result", kOutletDocLeft, "any");
  OUTLET_DOC(1, "rest", kOutletDocRight, "any");

  PARAM_DOC("maxsize mode argument", "",
            "The optional maximum list length, the mode word, and the mode's argument, in that "
            "order — Max's own argument shape. A leading token that is wholly an integer is the "
            "maximum list length and is clamped to 1-256; anything else is read as the mode word, "
            "so '.zl nth 2' and '.zl 64 nth 2' are both legal and mean the same thing but for the "
            "ceiling. The mode words implemented so far are 'len', 'rev', 'nth', 'mth', 'rot', "
            "'scramble', 'sort', 'slice', 'swap', 'indexmap', 'sub', 'lookup', 'sect', 'union', "
            "'unique', 'thin', 'filter', 'compare', 'change', 'reg', 'iter', 'join', 'lace', "
            "'delace', 'ecils', 'group', 'stream', 'queue' and 'stack'; one this object "
            "does not know is named in the log and ignored, leaving an object that stores what it "
            "is sent and emits nothing. "
            "With no mode word at all the object is inert for the same reason — Max's undocumented "
            "no-argument default is 'reg', and behaving as a mode the patch did not ask for would "
            "be worse than staying quiet, so 'reg' has to be asked for by name. Everything after "
            "the mode word is the mode's argument, in order, and it is "
            "read two ways at once: as numbers, which is one for 'nth', 'mth', 'rot', 'sort', "
            "'slice', 'ecils', 'iter', 'group' and 'stream', the first two for 'swap' and the "
            "whole run for 'indexmap'; and as a "
            "list of atoms, which is what 'sub' searches for, what 'lookup' reads as its table, "
            "what the set modes 'sect', 'union', 'unique', 'filter', 'compare' and 'change' take "
            "as the other list, what 'join' appends and 'lace' interleaves, and what 'reg' starts "
            "out holding. So '.zl swap 2 4', '.zl indexmap 3 1 2', '.zl sub 60 64', "
            "'.zl lookup do re mi', '.zl sect 60 62 64', '.zl group 3' and '.zl join a b' are all "
            "legal. 'thin', 'delace', 'queue' and 'stack' take no argument "
            "at all. The right inlet overwrites the argument "
            "afterwards. The 'scramble' seed is not a creation argument — the argument slot is "
            "taken by the index list — so a patch that needs a reproducible shuffle sends "
            "'zlseed <n>' to the left inlet.",
            "[<1-256>] [len|rev|nth|mth|rot|scramble|sort|slice|swap|indexmap|sub|lookup|sect|"
            "union|unique|thin|filter|compare|change|reg|iter|join|lace|delace|ecils|group|stream|"
            "queue|stack] [<argument> ...]");
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
  argumentAtoms.Clear();
  pending.Clear();
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

  // The mode's argument: everything after the mode word, kept whole as atoms
  // and then read again as numbers. Two readings of one run of tokens rather
  // than two arguments — `.zl sub 60 64` is a list to search for and `.zl nth 2`
  // is an index, and which of the two a mode wants is the mode's business, not
  // the parser's. Taking more than the first is what lets `swap` name two
  // places, `indexmap` carry a whole map and `lookup` carry a whole table.
  argumentAtoms.Clear();
  std::size_t refused = 0;
  for (; index < args.size(); index++) {
    if (args[index].empty()) continue;
    if (!argumentAtoms.Add(args[index], Limit())) refused++;
  }
  // Unconditionally, unlike the inlet: re-typing an object's arguments is a
  // full reconfiguration, so `.zl lookup do re mi` must not come back still
  // holding the index a `.zl nth 2` left behind.
  SetArgumentNumbers();
  // `reg` (#527) reads the right inlet's list as its contents rather than as an
  // argument, and the creation arguments are the same slot — so `.zl reg do re
  // mi` comes up already holding a list, which is what a patch that typed one
  // there meant. A no-op in every other mode.
  StoreRegister();

  if (refused != 0) {
    // Loudly, this being parameter parsing: `.combine`'s split between the two
    // routes, and the same one the maximum-length argument above takes.
    INTERNAL::LogImpl().emit(E_WARNING, std::string("patcher: ") + Type() +
                                            " argument is longer than the working maximum list "
                                            "length; " +
                                            std::to_string(refused) + " items were dropped");
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

std::size_t gZl::OrderSort(std::size_t size) {
  if (size == 0) return 0;
  // Max's direction argument: negative sorts downwards, anything else upwards,
  // so an unset argument is an ascending sort. The sort itself is the shared
  // one — see `SortIndices`, which #526 lifted out of here so that the set
  // modes could order the *other* list with it too.
  SortIndices(stored, order, merge, size, argument.load(std::memory_order_relaxed) < 0);
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

void gZl::SendOrdered(const AtomList& source, std::size_t count, YSE::THREAD thread) {
  if (count == 0) return;
  // Onto the scratch copy, so the source list stays as it arrived and a later
  // bang reorders it again rather than reordering the previous answer.
  work.AssignOrder(source, order, count);
  SendAtoms(outputs[0], work, render, thread);
}

// ─── the extraction modes (#525) ────────────────────────────────────────────

std::size_t gZl::OrderLookup(std::size_t size) {
  // `size` is how many *indices* the stored list holds; what they index is the
  // right inlet's list. That is the whole of the mode: `indexmap` with the two
  // lists swapped, which is why it fills `order` like the reordering group and
  // leaves through the same path.
  const std::size_t table = argumentAtoms.Size();
  if (size == 0 || table == 0) return 0;

  // Elementwise, as `indexmap` is and for its reason: the stored list is a run
  // of independent lookups, so an index naming no table entry drops its own
  // element and the rest still arrive. An atom that is not a number at all is
  // not an index either, and goes the same way.
  for (std::size_t i = 0; i < size; i++) {
    if (!stored.AtomIsNumber(i)) {
      order[i] = kNoAtom;
      continue;
    }
    const int index = ExprToInt(stored.AtomValue(i));
    order[i] = (index >= 1 && (std::size_t)index <= table) ? (std::uint16_t)(index - 1) : kNoAtom;
  }
  return size;
}

void gZl::Pick(long long at, YSE::THREAD thread) {
  // `nth` reaches here as `argument - 1` and `mth` as `argument`, so the whole
  // difference between the two modes is one subtraction at the call site and
  // this is written once. Taken as a long long because INT_MIN - 1 is not an
  // int.
  if (at < 0 || (unsigned long long)at >= (unsigned long long)stored.Size()) {
    // An index naming no item has no item to send — and no "everything else"
    // that means anything either, since the whole list is not the remainder of
    // a pick that did not happen. Silence on both outlets.
    return;
  }
  const std::size_t index = (std::size_t)at;
  // Right before left, Max's rule and `.trigger`'s: a patch downstream may
  // depend on the remainder having arrived before the item does.
  SendAtomsExcept(outputs[1], stored, index, render, thread);
  SendAtom(outputs[0], stored, index, render, thread);
}

void gZl::SendSlice(YSE::THREAD thread) {
  const std::size_t size = stored.Size();
  if (size == 0) return;

  // Max: "specifies the number of list items to be sent out the left outlet …
  // any remaining list elements are sent out the right outlet". A **count**
  // rather than an index, so this is the one mode with nothing to be off by one
  // about — and it is clamped rather than refused for the same reason: a cut
  // past the end of the list is a whole list and an empty remainder, which is a
  // meaningful answer, where an index past the end names nothing and is not.
  const int requested = argument.load(std::memory_order_relaxed);
  std::size_t head = 0;
  if (requested > 0) head = ((std::size_t)requested > size) ? size : (std::size_t)requested;

  // "Note: Lists are sent out the right outlet first" — Max says so explicitly
  // for this mode, and it is the object's rule anyway.
  SendAtomRange(outputs[1], stored, head, size - head, render, thread);
  SendAtomRange(outputs[0], stored, 0, head, render, thread);
}

std::size_t gZl::FindPattern() {
  const std::size_t size = stored.Size();
  const std::size_t pattern = argumentAtoms.Size();

  // The positions are built into the scratch list, which is where every result
  // of this object is built.
  work.Clear();
  if (pattern == 0 || pattern > size) return 0;

  // Knuth-Morris-Pratt. The obvious nested scan is O(n*m), which on two
  // full-length lists of repeated atoms — a rhythm of `1 1 1 …` searched for a
  // run of `1 1 1` is not a contrived patch — is sixty-five thousand atom
  // comparisons on a path the audio callback takes. This is O(n+m) whatever the
  // data, through an array the object already owns.
  failure[0] = 0;
  for (std::size_t i = 1; i < pattern; i++) {
    std::size_t length = failure[i - 1];
    while (length > 0 && !AtomsEqual(argumentAtoms, i, argumentAtoms, length))
      length = failure[length - 1];
    if (AtomsEqual(argumentAtoms, i, argumentAtoms, length)) length++;
    failure[i] = (std::uint16_t)length;
  }

  std::size_t matched = 0;
  for (std::size_t i = 0; i < size; i++) {
    while (matched > 0 && !AtomsEqual(stored, i, argumentAtoms, matched))
      matched = failure[matched - 1];
    if (AtomsEqual(stored, i, argumentAtoms, matched)) matched++;
    if (matched != pattern) continue;

    // 1-based, like every other position this object reports — what `sub` finds
    // is exactly what `nth` takes.
    work.AddInt((int)(i + 2 - pattern));
    // Back through the failure link rather than to 0, so occurrences may
    // **overlap**: `1 1` occurs twice in `1 1 1`, at positions 1 and 2. "The
    // position of each occurrence" is a search, and a search that silently
    // skipped the second one would under-report exactly the repetitive material
    // a patch searches lists for.
    matched = failure[matched - 1];
  }
  return work.Size();
}

// ─── the set modes (#526) ───────────────────────────────────────────────────
//
// Membership goes through an ordering rather than through a nested scan, for
// the reason `sub` searches with Knuth-Morris-Pratt: the obvious pair of loops
// is O(n*m), which on two full-length lists is sixty-five thousand atom
// comparisons on a path the audio callback takes. Ranking both lists and
// binary-searching is O(n log n + m log m) whatever the data, through arrays
// the object already owns.

void gZl::RankStored(std::size_t size) {
  SortIndices(stored, sortedStored, merge, size, false);
}

void gZl::RankArgument() {
  SortIndices(argumentAtoms, sortedArgument, merge, argumentAtoms.Size(), false);
}

std::size_t gZl::OrderThin(std::size_t size) {
  if (size == 0) return 0;

  // Max: "output a list containing all the elements of the input list which are
  // not duplicates." The first of each run of equal atoms survives — which is
  // what the *stability* of the sort buys, the earliest occurrence coming first
  // inside each run — so the result is in the order the list arrived rather
  // than in sorted order.
  RankStored(size);
  MarkFirstOccurrences(stored, sortedStored, size, firstOccurrence);

  std::size_t count = 0;
  for (std::size_t i = 0; i < size; i++) {
    if (firstOccurrence[i]) order[count++] = (std::uint16_t)i;
  }
  return count;
}

std::size_t gZl::OrderSect(std::size_t size) {
  const std::size_t other = argumentAtoms.Size();
  if (size == 0 || other == 0) return 0;

  // Max: "a list … that contains the elements common to both lists". A set
  // operation, so the result is a set: each shared atom once, at the position
  // of its first occurrence in the stored list.
  RankStored(size);
  RankArgument();
  MarkFirstOccurrences(stored, sortedStored, size, firstOccurrence);

  std::size_t count = 0;
  for (std::size_t i = 0; i < size; i++) {
    if (!firstOccurrence[i]) continue;
    if (!ContainsAtom(argumentAtoms, sortedArgument, other, stored, i)) continue;
    order[count++] = (std::uint16_t)i;
  }
  return count;
}

std::size_t gZl::OrderReject(std::size_t size) {
  if (size == 0) return 0;

  // Max, of `filter`: "a list with elements matching the filtering list
  // removed"; of `unique`: "items from the left-input-list which were not
  // present in the right-input-list". The same selection, and a *filter* rather
  // than a set operation — the survivors keep their duplicates and their
  // places, which is what makes the positions `filter` reports mean something.
  //
  // An empty filtering list removes nothing, which is a meaningful answer
  // rather than an unconfigured one: `sub` is silent without a pattern because
  // a position it has not searched for is not a position, while a list with
  // nothing taken out of it is the list.
  const std::size_t other = argumentAtoms.Size();
  if (other != 0) RankArgument();

  std::size_t count = 0;
  for (std::size_t i = 0; i < size; i++) {
    if (other != 0 && ContainsAtom(argumentAtoms, sortedArgument, other, stored, i)) continue;
    order[count++] = (std::uint16_t)i;
  }
  return count;
}

void gZl::SendUnion(YSE::THREAD thread) {
  const std::size_t size = stored.Size();
  const std::size_t other = argumentAtoms.Size();

  // Max: "a list … that contains the contents of both input lists. If the left
  // and right inlets contain any items in common, only one symbol will be
  // output." A set operation like `sect`, so both halves are thinned and the
  // right half loses whatever the left already has.
  //
  // Built atom by atom rather than through `order` and `AssignOrder`, which
  // takes its atoms from one list: this result is drawn from two. The copy is
  // into the scratch list's own reserved text, so it allocates nothing, and an
  // atom that does not fit is refused and counted like any other.
  work.Clear();
  std::size_t refused = 0;

  if (size != 0) {
    RankStored(size);
    MarkFirstOccurrences(stored, sortedStored, size, firstOccurrence);
    for (std::size_t i = 0; i < size; i++) {
      if (!firstOccurrence[i]) continue;
      if (!work.Add(stored.AtomText(i), stored.AtomLength(i), Limit())) refused++;
    }
  }

  if (other != 0) {
    RankArgument();
    MarkFirstOccurrences(argumentAtoms, sortedArgument, other, firstOccurrence);
    for (std::size_t j = 0; j < other; j++) {
      if (!firstOccurrence[j]) continue;
      // `sortedStored` still names the stored list here, which is why the two
      // orderings are two arrays: this is the one mode that needs both after
      // the other has been built.
      if (size != 0 && ContainsAtom(stored, sortedStored, size, argumentAtoms, j)) continue;
      if (!work.Add(argumentAtoms.AtomText(j), argumentAtoms.AtomLength(j), Limit())) refused++;
    }
  }

  if (refused != 0) CountDrop(refused);
  SendAtoms(outputs[0], work, render, thread);
}

bool gZl::ArgumentMatchesStored() const {
  if (stored.Size() != argumentAtoms.Size()) return false;
  for (std::size_t i = 0; i < stored.Size(); i++) {
    if (!AtomsEqual(stored, i, argumentAtoms, i)) return false;
  }
  return true;
}

void gZl::SendCompare(YSE::THREAD thread) {
  const std::size_t size = stored.Size();
  const std::size_t other = argumentAtoms.Size();
  const std::size_t longer = (size > other) ? size : other;

  // Max: 1 when the lists match, otherwise 0 out the left outlet and "a list of
  // the indices for those elements of the lists that differ" out the right one.
  //
  // Positional rather than set-like: this mode asks whether two lists are the
  // *same list*, so a length difference is a difference at every position past
  // the shorter one rather than a separate kind of answer. 1-based, like every
  // other position this object reports and unlike Max's 0-based numbering here
  // — what `compare` reports is what `nth` takes, which is the whole reason the
  // object settled on one numbering. `mth` remains the documented exception.
  work.Clear();
  for (std::size_t i = 0; i < longer; i++) {
    const bool differs = i >= size || i >= other || !AtomsEqual(stored, i, argumentAtoms, i);
    if (differs) work.AddInt((int)(i + 1));
  }

  // Right before left, Max's rule and `.trigger`'s: the positions have to be in
  // place before the 0 that sets a patch reading them arrives. Nothing at all
  // when the lists match, which is the family's empty-result rule and here also
  // Max's — there are no differing positions to name.
  SendAtoms(outputs[1], work, render, thread);
  outputs[0].SendInt(work.Empty() ? 1 : 0, thread);
}

void gZl::SendChange(YSE::THREAD thread) {
  // Max: the list out the left outlet and 1 out the right when it differs from
  // the previous one; nothing out the left and 0 out the right when it does
  // not. The reference is the right inlet's list, because that is where Max
  // puts it — "receives lists that set the comparison reference" — and this
  // mode then keeps it up to date itself.
  const bool same = ArgumentMatchesStored();

  // The reference becomes what just arrived, before anything is sent: a
  // downstream object that sends back into this inlet finds the object busy and
  // is dropped, but the state it would have found is already settled.
  argumentAtoms.Assign(stored);
  // Unconditionally, so the numeric reading of the right inlet's list cannot
  // drift away from the atoms — the two are one arrival everywhere else.
  SetArgumentNumbers();

  // Right before left: the flag is the half a patch reads to decide whether to
  // expect the other, so it has to be there first. It is sent either way, which
  // is what makes the left outlet's silence readable — `sub`'s count, for
  // `sub`'s reason.
  outputs[1].SendInt(same ? 0 : 1, thread);
  if (!same) SendAtoms(outputs[0], stored, render, thread);
}

// ─── the structural modes (#527) ────────────────────────────────────────────

void gZl::StoreRegister() {
  // The only mode for which the right inlet carries *contents* rather than an
  // argument, and the mirror of `change`. Copied rather than swapped so the
  // numeric and atom readings of the right inlet's list stay exactly as they
  // were: a `reg` primed down a cord is still a `.zl` whose argument a later
  // `mode nth` can read.
  if (CurrentMode() != Mode::REG) return;
  stored.Assign(argumentAtoms);
}

std::size_t gZl::ArgumentLength() const {
  const int requested = argument.load(std::memory_order_relaxed);
  if (requested < 1) return 0;

  // Clamped to the working maximum list length rather than refused: a window or
  // a group wider than the accumulator can ever hold would simply never
  // complete, which is a silence a patch cannot tell from a broken cord.
  const std::size_t limit_ = Limit();
  return ((std::size_t)requested > limit_) ? limit_ : (std::size_t)requested;
}

void gZl::SendIter(YSE::THREAD thread) {
  const std::size_t size = stored.Size();
  const std::size_t chunk = ArgumentLength();
  // Max: "sent out the left outlet as a series of lists consisting of the
  // number of items specified". No chunk size is not a chunk size of one — it
  // is an unconfigured object, and it stays quiet like `sub` without a pattern.
  if (size == 0 || chunk == 0) return;

  // The last chunk is short when the list does not divide, which is Max's "the
  // final list may be shorter than specified"; SendAtomRange clamps, so that
  // costs no arithmetic here. Each send completes in full — the whole subgraph
  // behind the outlet — before the next leaves, which is `.iter`'s (#521)
  // serialising contract and comes free from sending synchronously.
  for (std::size_t at = 0; at < size; at += chunk)
    SendAtomRange(outputs[0], stored, at, chunk, render, thread);
}

void gZl::SendJoin(YSE::THREAD thread) {
  // Max: "accepts a list in both inlets and sends a list out the left outlet
  // which is the combination of both input lists." Built atom by atom, as
  // `union` is and for its reason: the result is drawn from two lists, and
  // AssignOrder reorders one.
  work.Clear();
  std::size_t refused = work.AddRange(stored, 0, stored.Size(), Limit());
  refused += work.AddRange(argumentAtoms, 0, argumentAtoms.Size(), Limit());
  if (refused != 0) CountDrop(refused);
  SendAtoms(outputs[0], work, render, thread);
}

void gZl::SendLace(YSE::THREAD thread) {
  // Max: "if the left input list is 6.2 5.6 3.8 and the right input list is
  // 3 5.3 2.4 the output list is 6.2 3 5.6 5.3 3.8 2.4."
  const std::size_t size = stored.Size();
  const std::size_t other = argumentAtoms.Size();
  const std::size_t both = (size < other) ? size : other;

  work.Clear();
  std::size_t refused = 0;
  for (std::size_t i = 0; i < both; i++) {
    if (!work.AddAtom(stored, i, Limit())) refused++;
    if (!work.AddAtom(argumentAtoms, i, Limit())) refused++;
  }
  // Two lists of different lengths interleave as far as the shorter one goes,
  // and the tail of the longer follows rather than being dropped: `lace` is
  // named for what it does to the pairs, not for a truncation, and `delace`
  // undoing it is what a patch expects. An uneven pair means `delace` gives the
  // atoms back in two lists of different lengths, which is exactly how they
  // arrived.
  refused += work.AddRange(stored, both, size - both, Limit());
  refused += work.AddRange(argumentAtoms, both, other - both, Limit());

  if (refused != 0) CountDrop(refused);
  SendAtoms(outputs[0], work, render, thread);
}

void gZl::SendDelace(YSE::THREAD thread) {
  // Max: "if the input list is 6.2 3 5.6 5.3 3.8 2.4 the left output list is
  // 6.2 5.6 3.8 and the right output list is 3 5.3 2.4." So the atoms at the
  // odd positions go right and the ones at the even positions go left, which is
  // `lace` run backwards.
  const std::size_t size = stored.Size();
  if (size == 0) return;

  // Right before left, Max's rule and `.trigger`'s. Built through `order` and
  // the scratch list like the reordering group, in two passes rather than one,
  // because `SendOrdered` overwrites the scratch with the half it sends.
  std::size_t count = 0;
  for (std::size_t i = 1; i < size; i += 2)
    order[count++] = (std::uint16_t)i;
  if (count != 0) {
    work.AssignOrder(stored, order, count);
    SendAtoms(outputs[1], work, render, thread);
  }

  count = 0;
  for (std::size_t i = 0; i < size; i += 2)
    order[count++] = (std::uint16_t)i;
  SendOrdered(stored, count, thread);
}

void gZl::SendEcils(YSE::THREAD thread) {
  const std::size_t size = stored.Size();
  if (size == 0) return;

  // Max: "the first list contains the number of items specified by the argument
  // beginning from the end of the list and counting backward toward the first
  // list element, and is sent out the right outlet." `slice` measured from the
  // other end, so the two agree about which outlet carries which half and
  // differ only in where the cut is — and, like `slice`, the argument is a
  // count rather than an index, so it is clamped to the list rather than
  // refused.
  const int requested = argument.load(std::memory_order_relaxed);
  std::size_t tail = 0;
  if (requested > 0) tail = ((std::size_t)requested > size) ? size : (std::size_t)requested;
  const std::size_t head = size - tail;

  SendAtomRange(outputs[1], stored, head, tail, render, thread);
  SendAtomRange(outputs[0], stored, 0, head, render, thread);
}

// ─── the accumulating modes (#527) ──────────────────────────────────────────

void gZl::Collect() {
  // Refused and counted rather than truncated, which is what every other
  // arrival here does — the accumulator's ceiling is the working maximum list
  // length, so a `queue` nobody empties stops taking atoms rather than growing.
  const std::size_t refused = pending.AddRange(stored, 0, stored.Size(), Limit());
  if (refused != 0) CountDrop(refused);
}

void gZl::RunGroup(YSE::THREAD thread, Trigger trigger) {
  if (trigger == Trigger::BANG) {
    // Max: "bang outputs the most recent stored items". The partial group, and
    // the accumulator is emptied by it — a flush that left the atoms behind
    // would send them a second time as part of the next complete group.
    if (pending.Empty()) return;
    SendAtoms(outputs[0], pending, render, thread);
    pending.Clear();
    return;
  }

  // Max: "a list received in the left inlet will be stored and the length of
  // the list is compared to a number received in the right inlet or an
  // argument"; the left outlet then "sends the specified quantity of items;
  // remaining elements stay stored". With no group size there is nothing to
  // compare against, so the atoms simply accumulate until one arrives or a bang
  // flushes them.
  const std::size_t size = ArgumentLength();
  if (size == 0) {
    Collect();
    return;
  }

  // Collected in whatever bites the accumulator has room for, and drained
  // between them. Doing it in one go would be shorter and would lose atoms: the
  // store's ceiling is the working maximum list length, and a leftover partial
  // group plus a full-length list is more than that — so a `.zl group 3` fed
  // two 256-atom lists would refuse the tail of the second, which is a note
  // dropped from material the object has plenty of room for. Draining first
  // always leaves room, the remainder after a drain being shorter than one
  // group and a group being no wider than the store.
  const std::size_t incoming = stored.Size();
  std::size_t taken = 0;
  for (;;) {
    // Every complete group, not just the first: one list may carry several, and
    // an object that emitted one group per message would fall further behind
    // the longer the lists were. Shortened once per bite rather than once per
    // group — the sends read from the accumulator, and the guard is held
    // throughout, so nothing can see the intermediate states.
    const std::size_t held = pending.Size();
    std::size_t at = 0;
    while (held - at >= size) {
      SendAtomRange(outputs[0], pending, at, size, render, thread);
      at += size;
    }
    if (at != 0) pending.Keep(at, held - at);

    if (taken >= incoming) break;

    const std::size_t room = Limit() - pending.Size();
    if (room == 0) {
      // Unreachable while the group size is clamped to the store's ceiling, and
      // kept because the alternative to a bounded loop here is an unbounded
      // one on a path the audio callback takes.
      CountDrop(incoming - taken);
      break;
    }
    const std::size_t want = incoming - taken;
    const std::size_t take = (room < want) ? room : want;
    const std::size_t refused = pending.AddRange(stored, taken, take, Limit());
    if (refused != 0) CountDrop(refused);
    taken += take;
  }
}

void gZl::RunStream(YSE::THREAD thread, Trigger trigger) {
  const std::size_t window = ArgumentLength();
  if (window == 0) {
    // Max: "accepts a number in the right inlet which specifies the length of
    // the output list. Following the receipt of this number, the object will
    // collect this number of items." No length, nothing to collect into — and
    // nothing kept either, so a length arriving later starts a clean window
    // rather than one holding whatever went past while the object was
    // unconfigured.
    if (trigger == Trigger::ARRIVAL) pending.Clear();
    return;
  }

  if (trigger == Trigger::ARRIVAL) {
    // Room made *before* the atoms are collected, not after. The accumulator's
    // ceiling is the working maximum list length, so a window as wide as that
    // would otherwise refuse the very atoms whose job is to push its oldest
    // ones out, and the window would freeze the moment it filled.
    const std::size_t incoming = stored.Size();
    const std::size_t room = (incoming >= window) ? 0 : window - incoming;
    if (pending.Size() > room) pending.Keep(pending.Size() - room, room);
    Collect();
  }

  // A sliding window: the oldest atoms fall off the front, so once it is full
  // every arrival sends the last `window` atoms rather than starting a fresh
  // collection. That is what makes `stream` the running view of a stream and
  // `group` the chunking of one — the two modes differ in nothing else.
  //
  // Trimmed on a bang as well as on an arrival, so a window narrowed live — or
  // an accumulator inherited from a `queue`, the four modes sharing one — is
  // answered at its current width rather than reported as a negative shortfall.
  if (pending.Size() > window) pending.Keep(pending.Size() - window, window);

  // Max sends a flag out the right outlet for this mode, in answer to the
  // *right inlet* setting the length. This object's right inlet is cold and
  // never emits, so the flag is carried where it can be: the number of atoms
  // the window still wants, sent on every stimulus, 0 meaning the left outlet
  // is carrying a complete window. It is what makes the left outlet's silence
  // readable — `sub`'s count and `change`'s flag, for their reason — and it is
  // sent first, Max's right-to-left rule.
  const std::size_t held = pending.Size();
  outputs[1].SendInt((int)(window - held), thread);
  if (held < window) return;
  SendAtoms(outputs[0], pending, render, thread);
}

void gZl::RunPop(YSE::THREAD thread, Trigger trigger, bool fromBack) {
  if (trigger == Trigger::ARRIVAL) {
    // Max, of `queue`: "functions as a first-in-first-out (FIFO) stack"; of
    // `stack`: "last-in-first-out". Pushing is not popping, so an arrival sends
    // nothing at all — a queue that emitted what it was given would be a wire.
    Collect();
    return;
  }

  if (pending.Empty()) {
    // Nothing to pop. A bang out the right outlet, which is `sect`'s answer to
    // the same question and for its reason: the left outlet is silent both when
    // the store is empty and when the object is not wired up, so this is the
    // only way a patch tells the two apart — and it is what lets a patch drain
    // a queue by banging it until the right outlet answers.
    outputs[1].SendBang(thread);
    return;
  }

  // Atom by atom, which is the unit everything in this object works in: a list
  // pushed into a queue is pushed as its atoms, so `1 2 3` comes back out as
  // three bangs' worth of values rather than as one list. That is Max's
  // behaviour and it is what makes the mode a *queue* rather than a register of
  // lists.
  //
  // The atom is copied out before the store is shortened, so what the object
  // holds is settled before anything is sent — `change`'s arrangement, and here
  // it costs one atom.
  const std::size_t at = fromBack ? pending.Size() - 1 : 0;
  work.Clear();
  if (!work.AddAtom(pending, at, Limit())) CountDrop();
  pending.Keep(fromBack ? 0 : 1, pending.Size() - 1);
  SendAtoms(outputs[0], work, render, thread);
}

// ─── the modes ──────────────────────────────────────────────────────────────

void gZl::Run(YSE::THREAD thread, Trigger trigger) {
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

  case Mode::NTH:
    // Max: "outputs the nth element of the list out the left outlet", 1-based,
    // "the right outlet outputs all elements except the selected one."
    Pick((long long)argument.load(std::memory_order_relaxed) - 1, thread);
    break;

  case Mode::MTH:
    // Max: "works exactly like nth mode, except the list index numbering begins
    // with 0 as opposed to 1." Exactly like it, then — one subtraction fewer.
    // This is the object's single 0-based mode, and it is 0-based because that
    // is the entire content of the mode: see the class notes.
    Pick((long long)argument.load(std::memory_order_relaxed), thread);
    break;

  case Mode::ROT:
    // Max: rotate the list by the number of places the argument gives.
    SendOrdered(stored, OrderRotate(stored.Size()), thread);
    break;

  case Mode::SCRAMBLE:
    // Max: "output the list in random order". No argument; `zlseed <n>` makes
    // the sequence replayable.
    SendOrdered(stored, OrderScramble(stored.Size()), thread);
    break;

  case Mode::SWAP:
    // Two 1-based indices, and the exchange between them.
    SendOrdered(stored, OrderSwap(stored.Size()), thread);
    break;

  case Mode::INDEXMAP:
    // The stored list re-picked in the order the index map names.
    SendOrdered(stored, OrderIndexMap(stored.Size()), thread);
    break;

  case Mode::LOOKUP:
    // The *argument* list re-picked in the order the stored list names — the
    // one mode whose result is built from the right inlet rather than the left.
    SendOrdered(argumentAtoms, OrderLookup(stored.Size()), thread);
    break;

  case Mode::SLICE:
    // The list cut in two: the first N items left, the rest right.
    SendSlice(thread);
    break;

  case Mode::SUB: {
    if (argumentAtoms.Empty()) {
      // Nothing to search *for* yet. An object with no pattern is unconfigured
      // rather than one that searched and found nothing, and it stays quiet on
      // both outlets — the rule an unconfigured `.zl` follows everywhere else,
      // and what makes the 0 below mean something.
      break;
    }
    const std::size_t matches = FindPattern();
    // Right before left, and here the right outlet carries the only answer a
    // patch gets when there are no matches: the left one has nothing to send.
    outputs[1].SendInt((int)matches, thread);
    SendAtoms(outputs[0], work, render, thread);
    break;
  }

  case Mode::THIN:
    // Max: "all the elements of the input list which are not duplicates."
    SendOrdered(stored, OrderThin(stored.Size()), thread);
    break;

  case Mode::UNIQUE:
    // Max: "items from the left-input-list which were not present in the
    // right-input-list." The same selection `filter` makes, without the
    // positions — see the class notes on why both are ported.
    SendOrdered(stored, OrderReject(stored.Size()), thread);
    break;

  case Mode::UNION:
    // The two lists added together as sets.
    SendUnion(thread);
    break;

  case Mode::COMPARE:
    // Whether the two lists are the same list, and where they are not.
    SendCompare(thread);
    break;

  case Mode::CHANGE:
    // The list, but only when it is not the one that came before it.
    SendChange(thread);
    break;

  case Mode::SECT: {
    const std::size_t count = OrderSect(stored.Size());
    // Max: "the right outlet outputs a bang if the two input lists share no
    // common elements." The left outlet is silent on an empty result, so the
    // bang is the only way a patch tells "nothing in common" from "not wired
    // up yet" — `sub`'s 0, in the shape Max gives this mode.
    if (count == 0) {
      outputs[1].SendBang(thread);
      break;
    }
    SendOrdered(stored, count, thread);
    break;
  }

  case Mode::FILTER: {
    const std::size_t count = OrderReject(stored.Size());
    if (count == 0) break;

    // Max: "a list of the index numbers of list elements not filtered out."
    // Built into the scratch list — which `SendOrdered` then overwrites with
    // the filtered list itself — and sent first, `sort`'s arrangement and for
    // `sort`'s reason: the positions are what a patch feeds onward, so they
    // have to be in place before the list that sets it running arrives.
    work.Clear();
    for (std::size_t i = 0; i < count; i++)
      work.AddInt((int)order[i] + 1);
    SendAtoms(outputs[1], work, render, thread);

    SendOrdered(stored, count, thread);
    break;
  }

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

    SendOrdered(stored, count, thread);
    break;
  }

  case Mode::REG:
    // Max: "a list received in the left inlet is sent out the left outlet
    // immediately … a bang sends the stored list out the left outlet." The
    // stored list is already the register — see the class notes on how the
    // right inlet writes to it.
    SendAtoms(outputs[0], stored, render, thread);
    break;

  case Mode::ITER:
    // The stored list out in chunks.
    SendIter(thread);
    break;

  case Mode::JOIN:
    // The two lists, one after the other.
    SendJoin(thread);
    break;

  case Mode::LACE:
    // The two lists interleaved.
    SendLace(thread);
    break;

  case Mode::DELACE:
    // One list pulled apart into the two `lace` would have made it from.
    SendDelace(thread);
    break;

  case Mode::ECILS:
    // The list cut in two, counting from the end.
    SendEcils(thread);
    break;

  case Mode::GROUP:
    // The stream chunked into fixed-size lists.
    RunGroup(thread, trigger);
    break;

  case Mode::STREAM:
    // The running view of the last N atoms.
    RunStream(thread, trigger);
    break;

  case Mode::QUEUE:
    // First in, first out.
    RunPop(thread, trigger, false);
    break;

  case Mode::STACK:
    // Last in, first out — `queue` popped from the other end.
    RunPop(thread, trigger, true);
    break;

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

  Run(thread, Trigger::ARRIVAL);
  Leave();
}

void gZl::TakeInt(int value, YSE::THREAD thread) {
  if (!Enter()) return;
  stored.Clear();
  if (!stored.AddInt(value, Limit())) CountDrop();
  Run(thread, Trigger::ARRIVAL);
  Leave();
}

void gZl::TakeFloat(float value, YSE::THREAD thread) {
  if (!Enter()) return;
  stored.Clear();
  if (!stored.AddFloat(value, Limit())) CountDrop();
  Run(thread, Trigger::ARRIVAL);
  Leave();
}

// ─── the mode's argument ────────────────────────────────────────────────────

std::size_t gZl::ReadArgumentNumbers() {
  // One tokenizer for the right inlet, not two: the atoms have already been
  // split and classified on the way in, so the numeric reading is a walk over
  // the table rather than a second pass over the characters. Truncated through
  // the range-checked conversion rather than cast, an index being a whole
  // number and casting a float outside the int range being undefined.
  std::size_t found = 0;
  for (std::size_t i = 0; i < argumentAtoms.Size(); i++) {
    if (!argumentAtoms.AtomIsNumber(i)) continue;
    argumentList[found++] = ExprToInt(argumentAtoms.AtomValue(i));
  }

  // Committed only when there was something to commit, so a list carrying no
  // numbers at all leaves the argument standing rather than clearing it — a
  // cord that delivers an occasional symbol should not silently un-point a
  // `swap`. The atom list has no such rule: for `sub` and `lookup` a list of
  // symbols is the ordinary argument.
  if (found != 0) {
    arguments = found;
    argument.store(argumentList[0], std::memory_order_relaxed);
  }
  return found;
}

void gZl::SetArgumentNumbers() {
  // The same reading, committed even when it is empty. What a full
  // reconfiguration wants: the creation arguments, so that `.zl lookup do re
  // mi` does not come back still holding the index a `.zl nth 2` left behind;
  // and `change` (#526), which replaces the right inlet's list itself and must
  // not leave the numeric reading describing a list that is no longer there.
  if (ReadArgumentNumbers() == 0) {
    arguments = 0;
    argument.store(0, std::memory_order_relaxed);
  }
}

void gZl::TakeArgument(int value) {
  // Under the guard, because the argument is an array and a list as well as an
  // atomic word now and the three have to agree — a `swap` that read a new
  // first index beside an old second one would perform an exchange no patch
  // asked for. A message that finds the object busy is dropped and counted,
  // which is the object's answer everywhere else and the same one that stops a
  // `.zl` wired back into its own inlet from recursing.
  if (!Enter()) return;
  argumentList[0] = value;
  arguments = 1;
  argument.store(value, std::memory_order_relaxed);
  // A bare number is a list of one here too, so a `.zl sub` told `3` searches
  // for the one-item list `3` rather than keeping whatever it held before.
  argumentAtoms.Clear();
  if (!argumentAtoms.AddInt(value, Limit())) CountDrop();
  StoreRegister();
  Leave();
}

void gZl::TakeArguments(const char* text, std::size_t length) {
  if (!Enter()) return;

  // The whole list, as atoms — `sub`'s pattern and `lookup`'s table — and then
  // the numeric reading the index modes want, taken from the same atoms.
  argumentAtoms.Clear();
  const std::size_t refused = argumentAtoms.AddTokens(text, length, Limit());
  ReadArgumentNumbers();
  StoreRegister();

  Leave();

  // Counted rather than logged, this being an inlet: the thread may be the
  // audio callback.
  if (refused != 0) CountDrop(refused);
}

// ─── the command words ──────────────────────────────────────────────────────

bool gZl::Command(const std::string& value, std::size_t begin, std::size_t end) {
  if (TokenIs(value, begin, end, kWordClear, sizeof(kWordClear) - 1)) {
    // Max: "zlclear reinitializes the zl object." The mode and the limit are
    // configuration rather than contents, so they stay — and the accumulating
    // modes' store (#527) is contents, so it goes: this is the only way a patch
    // empties a half-filled `group` or an abandoned `queue`.
    if (!Enter()) return true;
    stored.Clear();
    work.Clear();
    pending.Clear();
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
  // The one stimulus the accumulating modes (#527) read as "consume" rather
  // than "collect" — see the class notes.
  Run(thread, Trigger::BANG);
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
