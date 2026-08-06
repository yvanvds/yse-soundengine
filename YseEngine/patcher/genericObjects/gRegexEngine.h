#pragma once
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A small, bounded regular-expression engine for ``.regexp``
     *         (issue #452).
     *
     *  ### Why this is not ``std::regex``
     *
     *  The issue proposed backing ``.regexp`` with ``std::regex``, compiled
     *  once at SetParams time so the construction cost stays on the control
     *  thread. Compiling once is necessary but it is not sufficient, and the
     *  *matching* half is what rules the standard library out here:
     *
     *  - **It allocates on every match.** ``std::regex_search`` fills a
     *    ``std::match_results``, and ``match_results`` owns a heap vector of
     *    sub-matches — there is no way to hand it a caller-provided buffer.
     *    Both libstdc++ and libc++ also allocate inside the executor itself
     *    (the NFA executor keeps per-state visited sets and a results stack).
     *    A patcher inlet can be fired from the audio callback, so this is a
     *    hard stop, not a nicety.
     *  - **It throws.** ``std::regex_error`` with ``error_complexity`` or
     *    ``error_stack`` is raised *at match time*, not only at construction,
     *    precisely on the inputs where the matcher runs out of room.
     *  - **It is unbounded.** The standard specifies leftmost-longest
     *    backtracking semantics with no step limit, so a pattern as ordinary
     *    as ``(a+)+b`` against a run of ``a`` characters takes exponential
     *    time. "Usually fast" is not a real-time contract.
     *  - **libstdc++'s executor recurses** once per NFA state visited, so a
     *    long subject can overflow the audio thread's stack outright.
     *
     *  Adding a third-party engine (PCRE2, RE2) was out of scope for the
     *  issue, so this file implements the subset ``.regexp`` needs with the
     *  same split of work the patcher uses everywhere else:
     *
     *  - RegexProgram::Compile() runs on the **control thread only**, from
     *    ParseParams. It parses the pattern into a fixed-size node array and
     *    lowers it into a flat instruction array. It reports failures as
     *    strings and never runs on the audio path.
     *  - RegexProgram::Search() is the **RT path**. It runs a backtracking
     *    virtual machine over the already-built program using an explicit
     *    stack that lives in the caller's frame: no allocation, no lock, no
     *    exception, no I/O, no recursion — and a caller-supplied step budget,
     *    so the worst case is bounded by a number the caller chooses rather
     *    than by the shape of the pattern.
     *
     *  ### The language
     *
     *  A PCRE-flavoured core, big enough for the addresses, names and fields
     *  the object exists to pick apart:
     *
     *  - literals, and ``.`` for any character
     *  - character classes ``[abc]``, ranges ``[a-z]``, negation ``[^0-9]``
     *  - the shorthands ``\d \D \w \W \s \S``, inside a class as well as out
     *  - escapes ``\\ \. \* \+ \? \( \) \[ \] \{ \} \| \^ \$ \- \/ \t \n \r``
     *    and ``\xHH`` for any byte by its hexadecimal code
     *  - anchors ``^`` and ``$`` (whole-subject, not per line)
     *  - quantifiers ``* + ?`` and ``{n} {n,} {n,m}``, greedy, or lazy with a
     *    trailing ``?``
     *  - alternation ``|``
     *  - groups ``( )``, capturing (at most nine, so a substitution can name
     *    them ``%1``-``%9``) and ``(?: )`` non-capturing
     *
     *  Everything else is rejected at compile time with a message that says
     *  what was not understood and where: backreferences, lookaround, named
     *  groups, ``\b``, possessive quantifiers, inline flags. So is a pattern
     *  that would need more room than the fixed arrays below hold, and a
     *  ``*``/``+``/``{n,}`` whose body can match the empty string — ``(a*)*``
     *  is an infinite loop in a backtracking VM, and refusing it at compile
     *  time is a better answer than a step budget quietly running out.
     */

    /// Longest pattern Compile() accepts, in characters.
    constexpr int kRegexMaxPattern = 256;

    /// Parse-tree capacity. One node per atom plus the joining nodes.
    constexpr int kRegexMaxNodes = 512;

    /// Instruction capacity of the compiled program.
    constexpr int kRegexMaxProgram = 1024;

    /// Character-class bitmaps a single pattern may hold.
    constexpr int kRegexMaxClasses = 32;

    /// Capturing groups a pattern may declare. Nine, because a substitution
    /// names them ``%1``-``%9``.
    constexpr int kRegexMaxGroups = 9;

    /// Save slots: two per capturing group plus two for the whole match.
    constexpr int kRegexMaxSlots = (kRegexMaxGroups + 1) * 2;

    /// ``(`` nesting cap, so a pathological ``((((((...`` cannot overflow the
    /// *compiler's* own C++ stack.
    constexpr int kRegexMaxNesting = 16;

    /// Alternatives one ``|`` chain may hold.
    constexpr int kRegexMaxAlternatives = 32;

    /// Largest bound a ``{n,m}`` quantifier may name. The body is copied once
    /// per repetition, so this is also what keeps a ``a{1000}`` from filling
    /// the program array on its own.
    constexpr int kRegexMaxRepeat = 64;

    /// Depth of Search()'s backtrack stack. A pattern that would need more
    /// fails the match rather than growing anything. Sized against
    /// gRegexp.h's subject cap: a greedy ``.*`` pushes one entry per
    /// character it swallowed, so the longest subject the object accepts has
    /// to fit here twice over or an ordinary pattern would start failing on
    /// long input.
    constexpr int kRegexMaxBacktrack = 1024;

    /// Depth of Search()'s capture-undo log. Written to only by ``Save``, so
    /// it only fills up when a capturing group sits inside a repeat.
    constexpr int kRegexMaxUndo = 1024;

    /// Those two arrays live in Search()'s own frame — about 20 kB of stack,
    /// stated here because the alternative on this thread is a heap
    /// allocation, and 20 kB of a several-hundred-kB audio stack is the
    /// cheaper of the two.

    /// One character class, as a 256-bit membership bitmap: matching a class
    /// is one test, whatever the class holds.
    struct RegexClass {
      unsigned char bits[32];

      void Clear() {
        for (unsigned char& b : bits) {
          b = 0;
        }
      }
      void Add(unsigned char c) {
        bits[c >> 3] |= (unsigned char)(1u << (c & 7u));
      }
      void AddRange(unsigned char from, unsigned char to) {
        for (int c = from; c <= (int)to; c++) {
          Add((unsigned char)c);
        }
      }
      void Negate() {
        for (unsigned char& b : bits) {
          b = (unsigned char)~b;
        }
      }
      void Merge(const RegexClass& other) {
        for (int i = 0; i < 32; i++) {
          bits[i] |= other.bits[i];
        }
      }
      bool Test(unsigned char c) const {
        return (bits[c >> 3] & (unsigned char)(1u << (c & 7u))) != 0;
      }
    };

    /// Opcodes of the flat program Compile() emits.
    enum class RegexOp : unsigned char {
      Char, ///< match RegexInstr::ch and advance
      Class, ///< match against classes[RegexInstr::a] and advance
      Any, ///< match any one character and advance
      Bol, ///< assert start of subject
      Eol, ///< assert end of subject
      Save, ///< record the position into save slot RegexInstr::a
      Split, ///< try RegexInstr::a, keep RegexInstr::b for backtracking
      Jump, ///< continue at RegexInstr::a
      Match, ///< the whole pattern matched
    };

    /// One instruction. POD, so the program is a contiguous array the VM
    /// streams through.
    struct RegexInstr {
      RegexOp op = RegexOp::Match;
      unsigned char ch = 0; ///< Char
      short a = 0; ///< jump/split target, class index, or save slot
      short b = 0; ///< Split's second target
    };

    /// Where one match landed. Byte offsets into the subject; -1 for a group
    /// that did not take part. Index 0 is the whole match, 1-9 the groups.
    struct RegexMatch {
      int begin[kRegexMaxGroups + 1];
      int end[kRegexMaxGroups + 1];

      void Clear() {
        for (int i = 0; i <= kRegexMaxGroups; i++) {
          begin[i] = -1;
          end[i] = -1;
        }
      }
      /// Length of group @p index, or 0 when it did not take part.
      int Length(int index) const {
        if (index < 0 || index > kRegexMaxGroups) return 0;
        if (begin[index] < 0 || end[index] < begin[index]) return 0;
        return end[index] - begin[index];
      }
      bool Taken(int index) const {
        return index >= 0 && index <= kRegexMaxGroups && begin[index] >= 0 &&
               end[index] >= begin[index];
      }
    };

    /**
     *  @brief A compiled pattern: build it once on the control thread, run it
     *         as often as you like on the audio thread.
     */
    class RegexProgram {
    public:
      RegexProgram() {
        Clear();
      }

      /**
       *  Compile @p pattern. Control thread only — it walks a recursive-descent
       *  parser and builds an error string on failure.
       *
       *  @return true when the program is runnable. On failure the program is
       *          emptied (Search() then never matches), Error() says what went
       *          wrong and at which character, and the caller is expected to
       *          log it: a malformed pattern must fail here, loudly, rather
       *          than silently at message time.
       */
      bool Compile(const std::string& pattern);

      /// Drop the program and the error. Control thread only.
      void Clear();

      /// True when a successful Compile() produced a runnable program.
      bool Valid() const {
        return valid;
      }

      /// Empty on success; on failure, what went wrong plus a character offset
      /// into the pattern.
      const std::string& Error() const {
        return error;
      }

      /// How many capturing groups the pattern declares, 0-9.
      int GroupCount() const {
        return groupCount;
      }

      /// Instruction count; 0 for an empty or failed program.
      int Size() const {
        return count;
      }

      /**
       *  Find the leftmost match starting at or after @p from.
       *
       *  RT-safe: a fixed-size backtrack stack and undo log in this frame, a
       *  switch per instruction, no allocation, no lock, no exception, no I/O,
       *  no recursion.
       *
       *  @param text    the subject; not required to be NUL-terminated.
       *  @param length  its length in bytes.
       *  @param from    where to start looking, clamped into [0, length].
       *  @param out     filled with the match on success; untouched otherwise.
       *  @param budget  VM steps this call may spend, decremented in place. A
       *                 call that runs out answers "no match" — the bound is
       *                 the whole point, so an adversarial pattern costs a
       *                 known number of steps rather than an unknown one. Pass
       *                 the *same* counter to every Search() of one message to
       *                 bound the scan as a whole and not just each attempt.
       *  @return true when a match was found.
       */
      bool Search(const char* text, int length, int from, RegexMatch& out, int& budget) const;

    private:
      friend struct RegexCompiler;

      // One match attempt anchored at `start`. Same RT contract as Search().
      bool RunAt(const char* text, int length, int start, int* slots, int& budget) const;

      RegexInstr prog[kRegexMaxProgram];
      RegexClass classes[kRegexMaxClasses];
      std::string error;
      int count = 0;
      int classCount = 0;
      int groupCount = 0;
      bool valid = false;
    };

  } // namespace PATCHER
} // namespace YSE
