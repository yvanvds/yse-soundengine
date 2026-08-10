#pragma once
#include "math/gExprEval.h"
#include "outlet.h"
#include "pListArgs.h"
#include "pSelector.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The patcher's bounded, pre-allocated list — the storage every
     *         list-processing object shares (issue #523).
     *
     *  ### Why the patcher needed one
     *
     *  A patcher message is text: an outlet hands a ``std::string`` on and
     *  every reader splits it on whitespace. That is a fine *transport* and a
     *  hopeless *working representation* — reversing a list, taking its nth
     *  item or sorting it means knowing where its atoms begin and end, and
     *  every object that has needed that so far worked it out again from the
     *  characters. ``.zl`` (#523) is the first object whose whole job is that
     *  work, and ``.pack`` (#517), ``.pak`` (#518) and ``.unpack`` (#519)
     *  follow it immediately, so the representation is settled here once
     *  rather than four times.
     *
     *  It is deliberately **not** a replacement for the text transport. A list
     *  still travels between objects as list text; this is what an object
     *  holds *while it is working*, and ``Render`` is the way back.
     *
     *  ### The bound, and where the memory is
     *
     *  Two ceilings, both fixed at compile time and both reached by refusal
     *  rather than by truncation:
     *
     *  - ``MAX_ATOMS`` (256) atoms. Max's own default maximum list length for
     *    ``zl`` — "the default maximum list length is 256 elements" — which
     *    makes it the number a patch author already expects. Here it is the
     *    hard ceiling as well as the default: an object may narrow its own
     *    working limit below it (``.zl``'s ``zlmaxsize``), never above.
     *  - ``TEXT_CAPACITY`` (1024) characters of backing text, which is what
     *    bounds the memory rather than the atom count: 256 atoms of four
     *    characters each fit, 256 atoms of thirty do not.
     *
     *  The backing text is one ``std::string`` reserved in the constructor —
     *  **the one allocation an AtomList ever makes** — and the atom table is a
     *  fixed array inside the object. So appending, reordering and rendering
     *  all run on memory the object already owns, which is the property that
     *  matters: a list arrives on whichever thread sent it, routinely the
     *  audio callback.
     *
     *  An atom is a slice of that text plus its numeric reading, decided once
     *  by ``Add`` through the strict ``ReadNumericToken``. Deciding it on the
     *  way *in* is what keeps the arithmetic modes (``sum``, ``median``,
     *  ``sort``) from re-parsing the same characters on every pass, and it is
     *  the same number/symbol split ``SelectorTable`` makes for the same
     *  reason.
     *
     *  ### Overflow
     *
     *  ``Add`` returns **false** and changes nothing when the atom does not
     *  fit; ``AddTokens`` returns how many atoms it refused. Nothing is ever
     *  silently shortened, and what is already collected is kept — an
     *  over-long list loses its **tail**, not its head, which is the failure
     *  that costs a patch least and the one ``.thresh`` already chose.
     *
     *  Reporting the refusal is the caller's job, and the caller has two
     *  routes because it has two kinds of thread:
     *
     *  - From a **message handler**, a monotonic counter the object publishes
     *    (``.zl``'s ``Dropped()``). Not a log line: formatting one builds a
     *    ``std::string`` on whichever thread the message arrived on, which may
     *    be the audio callback, and that is the objection ``.thresh``,
     *    ``.bondo`` and ``.combine`` all record.
     *  - From **parameter parsing**, which is control-thread only by
     *    construction, a log line naming what was dropped —  ``.combine``'s
     *    split: "silently on the inlet, which may be the audio thread; loudly
     *    on a creation argument".
     *
     *  ### Reordering, and why the text is append-only
     *
     *  ``Reverse``, ``Swap`` and the render helpers touch the **atom table**
     *  only; the backing text never moves. That makes a reorder a permutation
     *  of a few bytes per atom rather than a rewrite of the characters, and it
     *  is why the text is append-only until ``Clear``: an atom dropped from
     *  the table leaves its characters behind, so a list that is rebuilt over
     *  and over must be cleared rather than edited down. Every object here
     *  builds a fresh list per message, which is exactly that pattern.
     *
     *  ### Real-time behaviour
     *
     *  After construction nothing allocates, locks, blocks or reads locale
     *  state. Parsing goes through ``pListArgs.h``'s readers and
     *  ``IsSelectorSeparator``, formatting through ``ExprFormatValue`` into a
     *  stack buffer, and rendering appends into a caller-owned string the
     *  caller has reserved with ``ReserveRender``.
     *
     *  An AtomList is **not** thread-safe and does not pretend to be. Two
     *  threads sending to the same object genuinely race for it, so the owning
     *  object serialises the access — ``.zl`` with a single test-and-set guard
     *  whose loser is dropped and counted, rather than made to spin on a path
     *  the audio callback takes.
     */
    class AtomList {
    public:
      /**
       *  @brief Most atoms a list holds — Max's own default maximum length for
       *         ``zl``, and here the hard ceiling too.
       *
       *  An object may narrow its working limit below this (``.zl``'s
       *  ``zlmaxsize``); nothing may raise it, because the storage behind it
       *  is allocated once when the object is built.
       */
      static constexpr std::size_t MAX_ATOMS = 256;

      /**
       *  @brief Characters of backing text a list holds.
       *
       *  The bound that actually limits the memory: the atom count says how
       *  many slices there may be, this says how many characters they may
       *  span between them. Four times ``MAX_ATOMS``, which fits a list of
       *  short numbers — the overwhelmingly common case — with room to spare,
       *  and refuses a list of long symbols before it costs a kilobyte a
       *  piece.
       */
      static constexpr std::size_t TEXT_CAPACITY = 1024;

      /**
       *  @brief What a string being rendered into must be reserved to.
       *
       *  Every atom's characters plus a separator between each pair. Use
       *  ``ReserveRender`` rather than this number directly.
       */
      static constexpr std::size_t RENDER_CAPACITY = TEXT_CAPACITY + MAX_ATOMS;

      /** @brief Reserves the backing text. The one allocation this class
       *         makes, and it happens where an object is built rather than
       *         where a message arrives. */
      AtomList() {
        text_.reserve(TEXT_CAPACITY);
      }

      /** @brief Reserve @p out so that rendering into it cannot allocate.
       *         Call it once, from a constructor, on every buffer a message
       *         handler will render into. */
      static void ReserveRender(std::string& out) {
        out.reserve(RENDER_CAPACITY);
      }

      /** @brief Drop every atom, and with them the backing text. The only way
       *         characters are ever reclaimed — see the class notes on why the
       *         text is append-only. */
      void Clear() {
        text_.clear();
        count_ = 0;
      }

      /** @brief How many atoms the list holds. */
      std::size_t Size() const {
        return count_;
      }

      /** @brief Whether the list holds no atoms at all. */
      bool Empty() const {
        return count_ == 0;
      }

      /**
       *  @brief Append the @p length characters at @p token as one atom.
       *
       *  False, changing nothing, when the atom does not fit — the atom count
       *  is at @p limit (or at ``MAX_ATOMS``), or the text would pass
       *  ``TEXT_CAPACITY``. The caller reports the refusal; see the class
       *  notes on which of the two routes applies.
       *
       *  @p limit lets an object narrow the ceiling below ``MAX_ATOMS``
       *  without a second bound of its own; it is clamped to ``MAX_ATOMS``, so
       *  passing a larger one cannot widen the storage.
       */
      bool Add(const char* token, std::size_t length, std::size_t limit = MAX_ATOMS) {
        if (limit > MAX_ATOMS) limit = MAX_ATOMS;
        if (count_ >= limit) return false;
        if (text_.size() + length > TEXT_CAPACITY) return false;

        Entry& entry = entries_[count_];
        entry.offset = (std::uint16_t)text_.size();
        entry.length = (std::uint16_t)length;
        // Decided once, on the way in: the arithmetic modes must not re-read
        // the same characters on every pass. Strict, as SelectorTable is — a
        // token that is only partly a number is a symbol.
        entry.numeric = ReadNumericToken(token, length, entry.value);
        entry.isFloat = entry.numeric && TokenLooksLikeFloat(token, length);
        if (!entry.numeric) entry.value = 0.f;

        // Into the text the constructor reserved: no allocation, whichever
        // thread the message arrived on.
        text_.append(token, length);
        count_++;
        return true;
      }

      /** @brief ``Add`` over a whole ``std::string`` token. */
      bool Add(const std::string& token, std::size_t limit = MAX_ATOMS) {
        return Add(token.c_str(), token.size(), limit);
      }

      /** @brief Append @p value as the text that spells it — an int, so no
       *         decimal point. Rendered by ``ExprFormatValue`` into a stack
       *         buffer, so the patcher has one spelling of a number and this
       *         path allocates nothing. */
      bool AddInt(int value, std::size_t limit = MAX_ATOMS) {
        char text[kExprValueTextMax];
        const int written = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
        if (written <= 0) return false;
        return Add(text, (std::size_t)written, limit);
      }

      /** @brief Append @p value as the text that spells it — a float, so it
       *         keeps a decimal point and stays visibly a float in the middle
       *         of a list. */
      bool AddFloat(float value, std::size_t limit = MAX_ATOMS) {
        char text[kExprValueTextMax];
        const int written = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
        if (written <= 0) return false;
        return Add(text, (std::size_t)written, limit);
      }

      /**
       *  @brief Split the @p length characters at @p text on whitespace and
       *         append every token.
       *
       *  @return how many tokens were **refused**, so a caller can count an
       *          overflow without comparing sizes. 0 when the whole of the
       *          text fitted. Tokens keep arriving after the first refusal
       *          only in the sense that the walk finishes; nothing that has
       *          already been stored is disturbed.
       */
      std::size_t AddTokens(const char* text, std::size_t length, std::size_t limit = MAX_ATOMS) {
        std::size_t refused = 0;
        std::size_t i = 0;
        while (i < length) {
          while (i < length && IsSelectorSeparator(text[i]))
            i++;
          if (i >= length) break;
          const std::size_t begin = i;
          while (i < length && !IsSelectorSeparator(text[i]))
            i++;
          if (!Add(text + begin, i - begin, limit)) refused++;
        }
        return refused;
      }

      /** @brief ``AddTokens`` over a whole ``std::string``. */
      std::size_t AddTokens(const std::string& text, std::size_t limit = MAX_ATOMS) {
        return AddTokens(text.c_str(), text.size(), limit);
      }

      /**
       *  @brief Append atom @p index of @p other as a **new** atom of this
       *         list, with its own copy of the characters.
       *
       *  ``Assign`` and ``AssignOrder`` both take @p other's backing text
       *  whole, which is what makes them free; this is the other operation —
       *  building one list out of *two*, which ``.zl``'s ``join`` and ``lace``
       *  do (issue #527) and its ``union`` (#526) already did by hand.
       *
       *  False, changing nothing, when the atom does not fit or @p index names
       *  no atom of @p other. Allocation-free for ``Add``'s reason. @p other
       *  must not be this same list — the characters are read from its text
       *  while this one's is being appended to.
       */
      bool AddAtom(const AtomList& other, std::size_t index, std::size_t limit = MAX_ATOMS) {
        const char* token = other.AtomText(index);
        if (token == nullptr) return false;
        return Add(token, other.AtomLength(index), limit);
      }

      /**
       *  @brief ``AddAtom`` over a run: @p count atoms of @p other starting at
       *         @p begin.
       *
       *  @return how many were **refused**, as ``AddTokens`` does. The run is
       *          clamped to @p other first, so a count past its end is not
       *          reported as a refusal — there was no atom to refuse.
       */
      std::size_t AddRange(const AtomList& other, std::size_t begin, std::size_t count,
                           std::size_t limit = MAX_ATOMS) {
        if (begin >= other.Size()) return 0;
        const std::size_t available = other.Size() - begin;
        if (count > available) count = available;

        std::size_t refused = 0;
        for (std::size_t i = 0; i < count; i++) {
          if (!AddAtom(other, begin + i, limit)) refused++;
        }
        return refused;
      }

      /**
       *  @brief Keep only @p count atoms starting at @p begin, dropping the
       *         rest — and **reclaim their characters**.
       *
       *  The one operation that shortens a list rather than rebuilding it, and
       *  the reason it exists is that ``.zl``'s accumulating modes (#527) are
       *  the first here to consume a list from an end and keep going: a
       *  ``queue`` popped a thousand times would otherwise walk the
       *  append-only text off its ceiling and start refusing atoms it has room
       *  for.
       *
       *  The retained characters are moved down in place — a retained atom's
       *  new offset is never past its old one, so one pass in increasing order
       *  is enough — and the table follows them. Allocation-free: nothing is
       *  copied out and the text only ever shrinks. A @p begin past the end
       *  empties the list, and @p count is clamped to what is there.
       */
      void Keep(std::size_t begin, std::size_t count) {
        if (begin >= count_) {
          Clear();
          return;
        }
        const std::size_t available = count_ - begin;
        if (count > available) count = available;

        std::size_t at = 0;
        for (std::size_t i = 0; i < count; i++) {
          Entry entry = entries_[begin + i];
          // Never upwards: `at` is the total length of the atoms already kept,
          // and every one of them sat at or after that offset to begin with.
          // memmove rather than memcpy all the same, the two runs overlapping
          // when nothing was dropped in front of this atom.
          if (at != entry.offset && entry.length != 0)
            std::memmove(&text_[at], text_.data() + entry.offset, entry.length);
          entry.offset = (std::uint16_t)at;
          at += entry.length;
          entries_[i] = entry;
        }
        // Shrinking, so this cannot allocate.
        text_.resize(at);
        count_ = count;
      }

      /**
       *  @brief Copy @p other's atoms over this list's.
       *
       *  Allocation-free: both lists reserve ``TEXT_CAPACITY`` in their
       *  constructors and @p other cannot hold more than that, so the text
       *  assignment reuses storage this list already owns. Only the atoms
       *  actually held are copied, not the whole table.
       *
       *  This is how an object works on a list without destroying the one it
       *  keeps — ``.zl``'s stored list stays as it arrived while a scratch
       *  copy is reversed or picked apart.
       */
      void Assign(const AtomList& other) {
        text_.assign(other.text_);
        count_ = other.count_;
        for (std::size_t i = 0; i < count_; i++)
          entries_[i] = other.entries_[i];
      }

      /**
       *  @brief Rebuild this list as @p other's atoms taken in the order the
       *         @p count entries at @p order name — the shared reordering
       *         primitive (issue #524).
       *
       *  Every mode that *rearranges* rather than *selects* comes down to this:
       *  ``.zl``'s ``rot``, ``scramble``, ``sort``, ``swap`` and ``indexmap``
       *  each compute an index order over the stored list and then apply it
       *  here, so the rearranging itself is written once and each mode is only
       *  the arithmetic that produces its order.
       *
       *  An entry naming no atom of @p other **contributes nothing** rather
       *  than a placeholder, which is what lets a caller mark a rejected index
       *  by pointing it past the end instead of compacting its own array first.
       *  So the result may be shorter than @p count, and ``Size()`` afterwards
       *  is what actually landed.
       *
       *  Entries may repeat: an index map that names the same atom twice
       *  produces it twice. Nothing is copied but the atom table — the backing
       *  text is taken from @p other whole, exactly as ``Assign`` takes it — so
       *  a repeat costs no characters.
       *
       *  Allocation-free for the reason ``Assign`` is, and @p other must not be
       *  this same list: the atoms are read from it while this table is being
       *  rewritten.
       */
      void AssignOrder(const AtomList& other, const std::uint16_t* order, std::size_t count) {
        text_.assign(other.text_);
        if (count > MAX_ATOMS) count = MAX_ATOMS;
        count_ = 0;
        for (std::size_t i = 0; i < count; i++) {
          const std::size_t at = order[i];
          if (at >= other.count_) continue;
          entries_[count_++] = other.entries_[at];
        }
      }

      /** @brief Reverse the atom order. Permutes the table; the backing text
       *         does not move. */
      void Reverse() {
        for (std::size_t i = 0, j = count_; i + 1 < j; i++, j--)
          Swap(i, j - 1);
      }

      /** @brief Exchange atoms @p a and @p b. Out-of-range indices are
       *         ignored, so a sort's bounds mistake is a no-op rather than a
       *         wild write. */
      void Swap(std::size_t a, std::size_t b) {
        if (a >= count_ || b >= count_ || a == b) return;
        const Entry tmp = entries_[a];
        entries_[a] = entries_[b];
        entries_[b] = tmp;
      }

      /** @brief The characters of atom @p index. **Not** NUL-terminated — take
       *         it with ``AtomLength``. Null for an index out of range. */
      const char* AtomText(std::size_t index) const {
        if (index >= count_) return nullptr;
        return text_.c_str() + entries_[index].offset;
      }

      /** @brief How many characters atom @p index spans. 0 out of range. */
      std::size_t AtomLength(std::size_t index) const {
        if (index >= count_) return 0;
        return entries_[index].length;
      }

      /** @brief Whether atom @p index reads as one whole finite number.
       *         False out of range, and false for a symbol. */
      bool AtomIsNumber(std::size_t index) const {
        if (index >= count_) return false;
        return entries_[index].numeric;
      }

      /** @brief Whether a numeric atom is spelled as a **float** rather than
       *         as an int — what decides whether it leaves as one. */
      bool AtomIsFloat(std::size_t index) const {
        if (index >= count_) return false;
        return entries_[index].isFloat;
      }

      /** @brief The value of numeric atom @p index; 0 for a symbol or an index
       *         out of range. */
      float AtomValue(std::size_t index) const {
        if (index >= count_) return 0.f;
        return entries_[index].value;
      }

      /** @brief Append atom @p index to @p out, preceded by a single space
       *         when @p out is not empty. Allocation-free as long as @p out
       *         was reserved with ``ReserveRender``. */
      void AppendAtom(std::string& out, std::size_t index) const {
        if (index >= count_) return;
        if (!out.empty()) out.push_back(' ');
        out.append(text_.c_str() + entries_[index].offset, entries_[index].length);
      }

      /**
       *  @brief Write the whole list into @p out as space-separated list text
       *         — the form a list outlet carries.
       *
       *  @p out is cleared first, so it comes back holding exactly this list.
       *  Allocation-free as long as it was reserved with ``ReserveRender``.
       */
      void Render(std::string& out) const {
        out.clear();
        for (std::size_t i = 0; i < count_; i++)
          AppendAtom(out, i);
      }

      /**
       *  @brief ``Render`` over a **slice**: @p count atoms starting at
       *         @p begin, as space-separated list text.
       *
       *  What an object that cuts a list into pieces sends per piece —
       *  ``.unjoin`` (#520) writes one group per outlet through this. Both
       *  bounds are clamped to the list, so a slice that runs off the end
       *  renders what is there and a @p begin past the end renders nothing.
       *
       *  @p out is cleared first, and allocation-free as long as it was
       *  reserved with ``ReserveRender``.
       */
      void RenderRange(std::string& out, std::size_t begin, std::size_t count) const {
        out.clear();
        if (begin >= count_) return;
        const std::size_t available = count_ - begin;
        if (count > available) count = available;
        for (std::size_t i = 0; i < count; i++)
          AppendAtom(out, begin + i);
      }

      /** @brief ``Render``, but leaving atom @p skip out — the remainder a
       *         picking mode sends out its second outlet. Skipping an index
       *         the list does not have renders the whole list. */
      void RenderExcept(std::string& out, std::size_t skip) const {
        out.clear();
        for (std::size_t i = 0; i < count_; i++) {
          if (i == skip) continue;
          AppendAtom(out, i);
        }
      }

    private:
      // One atom: where its characters are, and what they read as. 12 bytes,
      // so the whole table is 3 KB inside the owning object — the price of
      // never allocating on a message path.
      struct Entry {
        std::uint16_t offset = 0;
        std::uint16_t length = 0;
        float value = 0.f;
        bool numeric = false;
        bool isFloat = false;
      };

      // Reserved to TEXT_CAPACITY by the constructor and append-only until
      // Clear(): the atom table indexes into it, so nothing may move.
      std::string text_;
      Entry entries_[MAX_ATOMS];
      std::size_t count_ = 0;
    };

    /**
     *  @brief Send one atom out @p out the way the patcher spells a value.
     *
     *  **The transport convention for the list-processing family**, and the
     *  reason it is a shared function rather than a habit: a list of one atom
     *  is not a list in Max, it is the int, float or symbol it spells, and an
     *  object that always retyped its single-atom output to a list would stop
     *  it reaching the ``.i`` a patch wired it to. This patcher does no
     *  coercion at an inlet, so the decision has to be made here.
     *
     *  A numeric atom leaves as an **int** or a **float** by its spelling —
     *  the same ``ReadNumericToken`` / ``TokenLooksLikeFloat`` pair
     *  ``.thresh``, ``.bondo`` and ``.trigger`` already use — and anything
     *  else leaves as one-token list text.
     *
     *  @p scratch is the caller's buffer, reserved with
     *  ``AtomList::ReserveRender``, and is only touched on the symbol path.
     *  Nothing here allocates.
     */
    inline void SendAtom(outlet& out, const char* text, std::size_t length, std::string& scratch,
                         THREAD thread) {
      if (text == nullptr || length == 0) return;

      float number = 0.f;
      if (ReadNumericToken(text, length, number)) {
        if (TokenLooksLikeFloat(text, length)) {
          out.SendFloat(number, thread);
        } else {
          // ReadNumericToken only promised a finite float, so the token may
          // still be wider than an int: truncate through the range-checked
          // conversion rather than casting.
          out.SendInt(ExprToInt(number), thread);
        }
        return;
      }

      scratch.assign(text, length);
      out.SendList(scratch, thread);
    }

    /** @brief ``SendAtom`` for atom @p index of @p list. */
    inline void SendAtom(outlet& out, const AtomList& list, std::size_t index, std::string& scratch,
                         THREAD thread) {
      SendAtom(out, list.AtomText(index), list.AtomLength(index), scratch, thread);
    }

    /**
     *  @brief Send a whole list out @p out — **nothing** when it is empty, the
     *         atom itself when it holds one, and list text otherwise.
     *
     *  The empty case is the ``.sprintf`` / ``.prepend`` rule that an object
     *  with nothing to say says nothing rather than sending an empty message;
     *  the single-atom case is ``SendAtom``'s. @p scratch is the caller's
     *  render buffer.
     */
    inline void SendAtoms(outlet& out, const AtomList& list, std::string& scratch, THREAD thread) {
      if (list.Empty()) return;
      if (list.Size() == 1) {
        SendAtom(out, list, 0, scratch, thread);
        return;
      }
      list.Render(scratch);
      out.SendList(scratch, thread);
    }

    /**
     *  @brief ``SendAtoms`` over a **slice** — @p count atoms starting at
     *         @p begin.
     *
     *  The same three cases ``SendAtoms`` makes, applied to a piece of a list
     *  rather than to the whole of one: nothing at all when the slice is empty,
     *  the atom itself when it holds one, and list text otherwise. Both bounds
     *  are clamped to the list, so an object need not check its own arithmetic
     *  before calling.
     *
     *  This is what an object that **cuts** a list sends per piece, and it is
     *  shared rather than local because more than one of them does:
     *  ``.unjoin`` (#520) sends one group per outlet, and the iteration objects
     *  that follow send one piece per bang. @p scratch is the caller's render
     *  buffer, reserved with ``AtomList::ReserveRender``.
     */
    inline void SendAtomRange(outlet& out, const AtomList& list, std::size_t begin,
                              std::size_t count, std::string& scratch, THREAD thread) {
      if (begin >= list.Size()) return;
      const std::size_t available = list.Size() - begin;
      if (count > available) count = available;
      if (count == 0) return;
      if (count == 1) {
        // One atom leaves as the int, float or symbol it spells rather than as
        // a list of one — SendAtom's rule, and the reason a slice goes through
        // here rather than through Render directly.
        SendAtom(out, list, begin, scratch, thread);
        return;
      }
      list.RenderRange(scratch, begin, count);
      out.SendList(scratch, thread);
    }

    /** @brief ``SendAtoms`` with atom @p skip left out — the remainder a
     *         picking mode sends out its second outlet. Sends nothing when
     *         nothing is left. */
    inline void SendAtomsExcept(outlet& out, const AtomList& list, std::size_t skip,
                                std::string& scratch, THREAD thread) {
      const bool skips = skip < list.Size();
      const std::size_t remaining = skips ? list.Size() - 1 : list.Size();
      if (remaining == 0) return;
      if (remaining == 1) {
        // The one atom left over, sent as the value it spells rather than as a
        // list of one — SendAtom's rule, and the whole point of applying it
        // here too.
        const std::size_t index = (skips && skip == 0) ? 1 : 0;
        SendAtom(out, list, index, scratch, thread);
        return;
      }
      list.RenderExcept(scratch, skip);
      if (scratch.empty()) return;
      out.SendList(scratch, thread);
    }

  } // namespace PATCHER
} // namespace YSE
