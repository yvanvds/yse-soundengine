#pragma once
#include "../pObject.h"
#include "gRegexEngine.h"

namespace YSE {
  namespace PATCHER {

    /// Longest subject ``.regexp`` will match against. A longer message is
    /// passed straight out the no-match outlet: everything the object writes
    /// is written into buffers reserved at construction, so the bound has to
    /// exist somewhere and it is cheaper to state it than to grow a string on
    /// the audio thread.
    constexpr int kRegexpMaxSubject = 512;

    /// Capacity reserved for each of the object's output buffers. Larger than
    /// the subject cap because a substitution can expand what it replaces
    /// (``%1%1%1``); an output that would grow past this is dropped rather
    /// than reallocated.
    constexpr int kRegexpMaxOutput = 4096;

    /// VM steps one incoming message may spend, across every match attempt of
    /// the scan. See the RT contract below.
    constexpr int kRegexpStepBudget = 200000;

    /**
     *  @brief Regular-expression matching and substitution on symbols —
     *         ``.regexp`` (issue #452).
     *
     *  Max's ``regexp``: pattern-matching and search-and-replace on the
     *  symbolic messages flowing through a patch, so an address, a name or a
     *  field can be picked apart without a round trip through the host.
     *
     *      .regexp ^/synth/(\d+)/(\w+)$ %2
     *
     *  ### Parameters
     *
     *  - **pattern** — the regular expression, one token. The language, the
     *    escapes and everything it refuses are documented on RegexProgram in
     *    gRegexEngine.h.
     *  - **substitution** — optional; everything after the pattern, joined
     *    with single spaces. ``%1``-``%9`` stand for the capture groups,
     *    ``%0`` for the whole match and ``%%`` for a literal percent sign.
     *    With no substitution the object only reports what it matched.
     *
     *  The patcher's parameter tokenizer splits on spaces, so a pattern
     *  cannot contain one literally; write ``\s`` for any whitespace or
     *  ``\x20`` for exactly a space.
     *
     *  ### Outlets
     *
     *  Four, and they fire right to left as Max's do:
     *
     *  - **3 (no match)** — the message, unchanged, when the pattern did not
     *    match it anywhere. Nothing else is sent in that case, so this outlet
     *    is the "pass it on" path of a filter.
     *  - **2 (match)** — the matched substring, once per match.
     *  - **1 (groups)** — that match's capture groups as a list, once per
     *    match, and only when the pattern declares any.
     *  - **0 (substitution)** — the whole subject with every match replaced,
     *    sent once, and only when a substitution parameter is set.
     *
     *  The scan finds every non-overlapping match from left to right, so a
     *  subject with three matches sends three pairs on outlets 2 and 1 before
     *  the single substitution result leaves outlet 0.
     *
     *  ### The RT contract, and why not std::regex
     *
     *  The issue proposed ``std::regex`` compiled once at SetParams time.
     *  Compiling once is necessary but not sufficient: ``std::regex_search``
     *  allocates a ``match_results`` on every call, its executors allocate
     *  and recurse internally, it *throws* ``std::regex_error`` at match time
     *  when it runs out of room, and its backtracking has no step limit —
     *  ``(a+)+b`` is exponential. A patcher inlet can be fired from the audio
     *  callback, so none of that is acceptable here, and adding a
     *  third-party engine was out of scope. gRegexEngine.h implements the
     *  subset this object needs instead, with the same split the rest of the
     *  patcher uses:
     *
     *  - **ParseParams** (control thread, at construction or on SetParams)
     *    compiles the pattern into a fixed-size instruction array and joins
     *    the substitution. A malformed pattern fails here and loudly: the
     *    error goes to the log at E_ERROR and the object is left inert — it
     *    then passes every message out the no-match outlet, deliberately not
     *    "matching everything", which would be a plausible-looking wrong
     *    answer.
     *  - **The message path** runs a backtracking VM over that array with an
     *    explicit stack in its own frame, and writes into buffers reserved at
     *    construction. No allocation, no lock, no exception, no I/O, no
     *    recursion. Every message gets kRegexpStepBudget VM steps for the
     *    whole scan; a pattern that would need more stops matching there, so
     *    the worst case is a number chosen here rather than one chosen by
     *    whoever typed the pattern.
     *
     *  A SetParams on a live object cannot mutate it in place either — the
     *  registered clear/parse callbacks make ParamsNeedRebuild() true, so the
     *  patcher takes the structural-replacement route from issue #234.
     */
    PATCHER_CLASS(gRegexp, YSE::OBJ::G_REGEXP)
    _NO_MESSAGES
    _NO_CALCULATE

    _FLOAT_IN(SetFloatValue)
    _INT_IN(SetIntValue)
    _LIST_IN(SetListValue)

    _PARM_CLEAR
    _PARM_PARSE

  public:
    /// True when a pattern compiled and the object will try to match.
    bool Valid() const {
      return valid;
    }
    /// Empty when the current pattern compiled; otherwise the compile error
    /// that was logged. Control thread only.
    const std::string& CompileError() const {
      return error;
    }
    /// How many capture groups the current pattern declares, 0-9.
    int GroupCount() const {
      return program.GroupCount();
    }
    /// Whether a substitution parameter is set, i.e. whether outlet 0 can
    /// ever fire.
    bool HasSubstitution() const {
      return hasReplacement;
    }
    /// The substitution string as it was joined from the parameter tokens.
    const std::string& Substitution() const {
      return replacement;
    }

  private:
    // The message path. Scans `subject` for every non-overlapping match,
    // sending as it goes; see the outlet documentation above for the order.
    void Process(const std::string& subject, YSE::THREAD thread);

    // Append to one of the reserved output buffers, refusing rather than
    // reallocating when the result would outgrow kRegexpMaxOutput. Returns
    // false on refusal so the callers read as a chain of `ok = ok && ...`.
    static bool Append(std::string& destination, const char* data, int length);

    // Expand the substitution string for one match into `result`.
    bool AppendReplacement(const char* text, const RegexMatch& match);

    // Build the space-separated capture-group list for one match. A group
    // that did not take part contributes an empty token.
    void BuildGroups(const char* text, const RegexMatch& match);

    // Parameters. `pattern` is one token; `substitution` is a LIST so a
    // multi-word substitution survives Parameters::Set intact.
    std::string pattern;
    std::vector<std::string> substitution;

    // Built by ParseParams from the two parameters above, read by the
    // message path. Control thread writes, and only while the object is
    // unpublished (see ParamsNeedRebuild above).
    RegexProgram program;
    std::string replacement;
    std::string error;
    bool hasReplacement = false;
    bool valid = false;

    // The outgoing strings, reserved at construction so appending to them on
    // the message path cannot reallocate. `input` holds the text form of a
    // numeric message; `fragment` the matched substring.
    std::string result;
    std::string groups;
    std::string fragment;
    std::string input;
  };
} // namespace PATCHER
} // namespace YSE
