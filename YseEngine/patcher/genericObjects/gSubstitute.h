#pragma once
#include "../pObject.h"
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Find-and-replace inside a message — ``.substitute`` (issue #488).
     *
     *  Max's ``substitute``, whose one-line summary is "Matches messages to its
     *  own arguments; whenever it finds a match, will make the appropriate
     *  substitution". Two creation arguments, a **match** and a
     *  **replacement**: every element of an incoming message equal to the match
     *  leaves as the replacement, and everything else leaves untouched.
     *
     *  ### What it adds that the patcher could not already express
     *
     *  The message-construction pair ``.prepend`` / ``.append`` (#487) can put
     *  words on the *ends* of a message, and the routing family — ``.route``,
     *  ``.routepass``, ``.sel`` — can branch on the word a message *starts*
     *  with. Neither can change a word in the **middle** of one. Until now the
     *  only way to rewrite a selector or remap a symbolic value in flight was to
     *  take the message apart in the host application and build a new one, which
     *  puts a round trip out of the patcher into a path that is otherwise pure
     *  message passing. This is that edit, in one object: ``.substitute note
     *  ctl`` re-tags a stream, ``.substitute 127 1`` remaps a value, and a
     *  ``.routepass`` downstream sees the result as if it had always been
     *  spelled that way.
     *
     *  ### Exactly one outlet fires, and which one is the answer
     *
     *  Max gives the object two outlets: the left one carries the message with
     *  its substitutions made, and of the right one it says "if no substitution
     *  occurred when sending out the incoming message, the original input
     *  message is passed out the rightmost outlet". They are exclusive — the
     *  reading under which the right outlet means anything at all, since a left
     *  outlet that also fired for an unsubstituted message would make the right
     *  one a duplicate of it.
     *
     *  That makes the outlet a *report*: "this message was rewritten" versus
     *  "this message matched nothing", which is what lets a patch chain
     *  substitutions the way ``.routepass`` chains matches — reject outlet into
     *  the next inlet, each object trying its own pair — and what lets it tell
     *  a rewritten message from an untouched one without comparing texts. The
     *  rightmost outlet forwards **in the type that arrived**, exactly as
     *  ``.routepass``'s does, so an int that matched nothing is still an int.
     *
     *  ### With nothing to match, the object is the identity
     *
     *  A bare ``.substitute`` matches nothing and passes everything out the
     *  rightmost outlet, unchanged and in its own type — the ``.prepend`` rule,
     *  and the only reading under which dropping an unconfigured object into a
     *  working patch and configuring it afterwards is safe. Max instead defaults
     *  both arguments to ``0``, which would make a bare ``.substitute`` quietly
     *  rewrite zeroes into zeroes and route them out the *other* outlet than
     *  everything else; that is a branch a patch did not ask for, the reason
     *  ``.routepass`` also declines to invent a default selector.
     *
     *  A match given without a replacement does take Max's ``0``, because
     *  "replace this with nothing" is not expressible here: a message is carried
     *  as text and an empty replacement would leave a doubled separator, so the
     *  result would have an empty token in it that no ``.route`` matches — the
     *  hazard ``.prepend`` trims for on its own stored message. Deletion is a
     *  different object.
     *
     *  ### What matches what
     *
     *  The matcher is ``.sel``'s and ``.routepass``'s, deliberately: three
     *  objects that decide whether a message element equals a creation argument
     *  must decide it the same way, or swapping one for another changes a
     *  patch's meaning silently. The match is a **number** or a **symbol**,
     *  classified once when it is read, and the two never match each other:
     *
     *  - a number matches by value, an ``int`` widened to a ``float``, so
     *    ``.substitute 5 x`` answers the int 5 and the float 5.0 alike;
     *  - a symbol matches by exact text;
     *  - a **bang** matches a match spelled ``bang``, Max's "the bang message
     *    matches a 'bang' symbol in the arguments";
     *  - a **list** is walked element by element — this is the one place the
     *    family differs from ``.routepass``, which only ever looks at the first
     *    element, because substituting is not routing and a match anywhere in
     *    the message is a match.
     *
     *  The comparison is exact, with the consequences ``.sel`` documents: a
     *  computed float may miss a match it looks equal to (a ``.round`` upstream
     *  is the fix) and a NaN matches nothing.
     *
     *  A replaced element is written as the **text** of the replacement, so it
     *  is spelled in the message exactly as it was typed. When a *scalar*
     *  matched — an int, a float or a bang, where the whole message is the one
     *  element — the replacement leaves as the type it reads as, so
     *  ``.substitute 5 7`` turns the int 5 into the int 7 rather than into the
     *  one-token list that spells it, and the numeric objects downstream keep
     *  working. A numeric replacement is carried as a ``float`` like every other
     *  number in this patcher, so an integer replacement beyond 2^24 loses
     *  precision the same way a ``.f`` would.
     *
     *  ### The match and the replacement have their own inlets
     *
     *  Max spells the run-time change as a message into its right inlet. Here
     *  each half gets an inlet of its own: they are single tokens, so an ``int``
     *  and a ``float`` mean something on both, and a patch can re-aim the
     *  replacement without restating the match. Inlet 0 keeps no reserved words
     *  in it at all — the ``.prepend`` / ``.forward`` / ``.router`` discipline,
     *  and it matters more here than anywhere, since the messages this object
     *  edits are exactly the ones that begin with a word. A **bang** is declined
     *  on both setting inlets: nothing for it to do, so it stays out of
     *  ``inlet::GetAcceptedTypes()``.
     *
     *  An empty message on the match inlet clears the match and returns the
     *  object to being a wire, which is a real request rather than a malformed
     *  one — ``.prepend``'s rule. An empty message on the replacement inlet is
     *  ignored, because there is no such thing as an empty replacement.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlets, and one
     *  that emitted would substitute again on every DSP tick after the inlet
     *  fired — the rule ``.route``, ``.sel`` and ``.prepend`` establish.
     *
     *  No message path allocates, locks or blocks. The match and the replacement
     *  are refilled into ``TOKEN_CAPACITY`` reserved at construction, and a
     *  longer one is refused rather than truncated (silently on the inlets,
     *  which may be the audio thread; loudly on the creation arguments, which
     *  are control-thread only). The rewritten message is built in a buffer
     *  reserved at construction for the worst case the patcher's own queues can
     *  produce: a 256-character list — ``patcherImplementation::kValueListCap``
     *  — is at most 128 single-character elements, each of which may become a
     *  ``TOKEN_CAPACITY``-character replacement plus a separator. So a
     *  substitution is a bounded walk plus ``append``s into memory the object
     *  already owns, and a message that matched nothing is forwarded **by
     *  reference** without being copied through that buffer at all.
     */
    PATCHER_CLASS(gSubstitute, YSE::OBJ::G_SUBSTITUTE)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(SetBang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _INT_IN(SetMatchInt)
    _FLOAT_IN(SetMatchFloat)
    _LIST_IN(SetMatchList)

    _INT_IN(SetReplacementInt)
    _FLOAT_IN(SetReplacementFloat)
    _LIST_IN(SetReplacementList)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Longest match or replacement the object will hold, in characters.
     *
     *  64 — long for a symbol, and short enough that the worst-case rewritten
     *  message the object reserves room for stays a few kilobytes rather than
     *  the sixty-four the full ``kValueListCap`` bound would cost. A longer
     *  token is refused rather than truncated: half a symbol is a different
     *  symbol, and the whole job here is to produce a message the objects
     *  downstream still recognise.
     */
    static constexpr std::size_t TOKEN_CAPACITY = 64;

    /** @brief The element being matched, or "" when the object is a wire. The
     *         first creation argument, and thereafter whatever inlet 1 was last
     *         given. */
    const std::string& Match() const {
      return match.text;
    }

    /** @brief What a matched element is replaced by — never empty. The second
     *         creation argument (``"0"`` when there is none), and thereafter
     *         whatever inlet 2 was last given. */
    const std::string& Replacement() const {
      return replacement.text;
    }

    /** @brief Whether only the first matching element of a list is replaced —
     *         Max's third creation argument, "any third number or symbol sets
     *         the 'replace first message only' mode". */
    bool FirstOnly() const {
      return firstOnly;
    }

  private:
    // A match or a replacement, classified once when it is set. `numeric`
    // decides whether `value` means anything, and a numeric token never matches
    // a symbolic one — the three fields `.sel` and `.routepass` resolve their
    // selectors into, plus the int/float spelling `.match` needs to reproduce a
    // number the way it was written.
    struct Token {
      std::string text;
      float value = 0.f;
      bool numeric = false;
      bool isFloat = false;
    };

    // Refill `token` from the `length` characters at `text` and decide whether
    // it is a number. Runs on whichever thread set it, so it neither allocates
    // (the text was reserved to TOKEN_CAPACITY at construction) nor throws.
    static void ClassifyToken(Token& token, const char* text, std::size_t length);

    // Refill `token` from the first whitespace-delimited word of the `length`
    // characters at `text`. `emptyClears` decides what an empty message means:
    // the match takes it as a request to become a wire, the replacement ignores
    // it, since there is no empty replacement. A word longer than
    // TOKEN_CAPACITY is refused and the previous token kept.
    void StoreToken(Token& token, const char* text, std::size_t length, bool emptyClears);

    // Whether the `length` characters at `text` are the element being matched.
    // Takes a range rather than a std::string so a list can be walked without a
    // substr on whichever thread the message arrived on.
    bool TokenMatches(const char* text, std::size_t length) const;

    // Whether `value` is the element being matched — the scalar path, which
    // knows it has a number and so compares one without spelling it out first.
    // A symbolic match answers false, since the two kinds never match.
    bool NumberMatches(float value) const;

    // Send the replacement out outlet 0 as the type it reads as — the scalar
    // case, where the replacement is the whole outgoing message.
    void SendReplacement(YSE::THREAD thread);

    Token match;
    Token replacement;

    // Max's third argument: only the first matching element of a list is
    // replaced. Set on the control thread by the parameter callbacks and only
    // read afterwards.
    bool firstOnly = false;

    // The creation arguments. Control thread only: written by Parameters::Set,
    // read by ParseParams(), never by a message handler.
    std::string matchArg;
    std::string replacementArg;
    std::string modeArg;

    // Where the rewritten message is built. Reserved at construction for the
    // worst case a 256-character list can grow into — see the class
    // documentation — so no substitution allocates.
    std::string outText;
  };

} // namespace PATCHER
} // namespace YSE
