#pragma once
#include "../pObject.h"
#include <cstddef>
#include <cstdint>
#include <string>

// Declares one half of the case-folding pair (issue #810).
// Mirrors SYMBOL_CLASS from gSymbol.h and CHARCODE_CLASS from gCharCode.h: the
// whole one-inlet/one-outlet body lives in gCaseBase, and the constructor in
// gCase.cpp only has to pick a direction and fill in the side-specific
// documentation.
#define CASE_CLASS(className, typeName)                                                            \
  class className : public gCaseBase {                                                             \
  public:                                                                                          \
    className();                                                                                   \
    const char* Type() const override {                                                            \
      return typeName;                                                                             \
    }                                                                                              \
    CREATE(className)                                                                              \
  };

namespace YSE {
  namespace PATCHER {

    /** @brief Which way the fold runs. */
    enum class caseDirection : std::uint8_t {
      Lower, //!< ``.tolower`` — every A-Z becomes a-z.
      Upper //!< ``.toupper`` — every a-z becomes A-Z.
    };

    /**
     *  @brief Shared body for ``.tolower`` and ``.toupper`` (issue #810) — the
     *         pair that changes the case of a message's text and nothing else.
     *
     *  Max's ``string.tolower`` and ``string.toupper``. One implementation, two
     *  directions: everything about them — the single inlet, the single outlet,
     *  the absence of arguments — is the same except which way the fold runs,
     *  which is why they share a base the way ``.tosymbol`` / ``.fromsymbol``,
     *  ``.atoi`` / ``.itoa`` and ``.prepend`` / ``.append`` do.
     *
     *  ### The gap this fills
     *
     *  Nothing anywhere in this patcher could change the case of a character
     *  until now. The symbol family can rewrite a word (``.substitute``), build
     *  one (``.sprintf``, ``.combine``), collapse or expand one (``.tosymbol``,
     *  ``.fromsymbol``), spell one out as codes (``.spell``, ``.atoi``) and
     *  match one (``.match``, ``.regexp``, ``.route``, ``.sel``) — and every one
     *  of those comparisons is **exact**. So a patch that receives a name from
     *  outside itself, where the case is whatever a user or a host application
     *  happened to type, has no way to match it against a name written in the
     *  patch. Folding both sides to one case before the comparison is how that
     *  is done everywhere else, and this is the object that does it: a
     *  ``.tolower`` in front of a ``.route`` makes the route case-insensitive
     *  without the route having to learn a second way to compare.
     *
     *  Building it out of the parts that already exist is possible and awful:
     *  ``.spell`` to codes, arithmetic on each code, ``.itoa`` back — an
     *  object-per-character patch for what is one comparison of a byte.
     *
     *  ### ASCII, deliberately, and why that is safe on UTF-8
     *
     *  Only ``A``-``Z`` and ``a``-``z`` are folded. That is the issue's scope
     *  and it is the right one twice over.
     *
     *  It is **honest**: real case mapping is locale- and language-dependent
     *  (Turkish dotless ı, German ß, Greek final sigma), it is not
     *  length-preserving, and every implementation of it allocates, consults a
     *  table or reads locale state another thread may be mutating. None of that
     *  belongs on a path an inlet can drive from the audio thread, and none of
     *  it is what the address-matching use case needs.
     *
     *  It is also **safe on text that is not ASCII**, which is the part worth
     *  stating out loud, since a patcher message is bytes and nothing upstream
     *  promises an encoding. Every byte of a multi-byte UTF-8 sequence is
     *  ``0x80`` or above, and the folded range is entirely below it — so a
     *  byte-wise ASCII fold provably cannot touch, split or corrupt one. An
     *  accented letter, a CJK character or a run of raw binary passes through
     *  byte for byte. The object is not encoding-blind; it is encoding-safe by
     *  construction, and the ASCII restriction is what makes it so.
     *
     *  ### The identity everywhere else
     *
     *  Apart from those 52 letters the message leaves exactly as it arrived —
     *  the same bytes, the same length, the same tokens, the same whitespace.
     *  That is a deliberate difference from the rest of the family: ``.tosymbol``
     *  and ``.spell`` normalise whitespace because they are *rebuilding* the
     *  message and have to decide what its spacing is, whereas this object is
     *  rewriting characters in place. Collapsing a run of spaces here would be
     *  an edit no patch asked for, and it would be the wrong one precisely in
     *  the use case: text taken apart by a ``.route`` or a ``.zl`` after the
     *  fold must have the same token structure it had before it, or the fold
     *  changed more than the case.
     *
     *  For the same reason an **empty** message is forwarded rather than
     *  swallowed. Elsewhere in the family an empty result is suppressed because
     *  the object *produced* it out of nothing to send; here it is what arrived,
     *  and dropping it would make the object something other than a wire.
     *
     *  ### A bang and a number pass straight through
     *
     *  A number's text has no letters in it and a bang has no text at all, so
     *  the fold is the identity on both — and they leave in the **type** they
     *  arrived in, an int as an int and a bang as a bang, rather than as the
     *  list that spells them. ``.fromsymbol``'s rule, and the reason for it is
     *  the same: an object that is transparent by definition must not be the
     *  thing in a chain that quietly changes a message's type. That is also why
     *  they are accepted at all rather than declined the way ``.spell`` declines
     *  a bang — ``.spell`` has nothing to spell of one, while this object has a
     *  perfectly good answer for it, and a case-folder that broke a patch by
     *  dropping its bangs would not be droppable into a working patch at all.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlet, and one
     *  that emitted would re-fold on every DSP tick after the inlet fired — the
     *  rule ``.route``, ``.sel``, ``.prepend``, ``.substitute``, ``.tosymbol``
     *  and ``.spell`` establish.
     *
     *  No message path allocates, locks or blocks. The fold is one walk of the
     *  bytes into a buffer reserved at construction for ``TEXT_CAPACITY``, the
     *  longest list the patcher's own queues carry, and the transform is
     *  length-preserving — so every message that can reach this object over the
     *  deferred (audio-thread) path is folded into memory the object already
     *  owns. Nothing is refused: a longer message can only arrive through a
     *  synchronous send, which takes the value by reference and has no length
     *  limit, and there the buffer grows once and thereafter holds. Refusing
     *  instead would mean dropping a message this object promised to pass on,
     *  which is a worse answer than one allocation on a path that is already
     *  outside the queue's bound.
     */
    class gCaseBase : public pObject {
    public:
      explicit gCaseBase(caseDirection fold);

      _NO_MESSAGES
      _NO_CALCULATE

      _BANG_IN(FoldBang)
      _INT_IN(FoldInt)
      _FLOAT_IN(FoldFloat)
      _LIST_IN(FoldList)

      /**
       *  @brief How much text the object can fold without allocating — 256, the
       *         longest list the patcher's queues carry
       *         (``patcherImplementation::kValueListCap``), so anything that can
       *         reach this object over the deferred path is covered. The fold is
       *         length-preserving, so this bounds the result as well as the
       *         input.
       */
      static constexpr std::size_t TEXT_CAPACITY = 256;

      /** @brief The last text this object sent. Empty until it has folded once,
       *         and for a bang, an int or a float, which pass through without
       *         being folded at all. For tests and for a host reading the
       *         object's state. */
      const std::string& LastOutput() const {
        return outText;
      }

    protected:
      // Fills in the pieces of documentation that differ per direction; the base
      // constructor already set the category. RT-cold — constructor use only.
      void Document(const char* summary, const char* inDoc, const char* outDoc);

    private:
      // Fold the `length` bytes at `text` into `outText` and send it. Runs on
      // whichever thread the message arrived on, so it neither logs nor (for any
      // message the patcher's queues can carry) allocates.
      void Fold(const char* text, std::size_t length, YSE::THREAD thread);

      // Which way the fold runs. Fixed by the derived constructor, so choosing
      // costs one branch on a register, hoisted out of the byte loop.
      caseDirection direction;

      // Where the folded text is built. Reserved at construction for
      // TEXT_CAPACITY — see the class documentation.
      std::string outText;
    };

    CASE_CLASS(gToLower, YSE::OBJ::G_TOLOWER)
    CASE_CLASS(gToUpper, YSE::OBJ::G_TOUPPER)

  } // namespace PATCHER
} // namespace YSE
