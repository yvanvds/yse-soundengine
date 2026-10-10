#pragma once
#include "../pObject.h"
#include <cstddef>
#include <cstdint>
#include <string>

// Declares one half of the character-code pair (issue #493).
// Mirrors SYMBOL_CLASS from gSymbol.h and AFFIX_CLASS from gAffix.h: the whole
// three-inlet/one-outlet body lives in gCharCodeBase, and the constructor in
// gCharCode.cpp only has to pick a direction and fill in the side-specific
// documentation.
#define CHARCODE_CLASS(className, typeName)                                                        \
  class className : public gCharCodeBase {                                                         \
  public:                                                                                          \
    className();                                                                                   \
    const char* Type() const override {                                                            \
      return typeName;                                                                             \
    }                                                                                              \
    CREATE(className)                                                                              \
  };

namespace YSE {
  namespace PATCHER {

    /** @brief Which way round the conversion runs. */
    enum class charCodeDirection : std::uint8_t {
      ToCodes, //!< ``.atoi`` — characters in, character codes out.
      ToText //!< ``.itoa`` — character codes in, characters out.
    };

    /**
     *  @brief Shared body for ``.atoi`` and ``.itoa`` (issue #493) — the
     *         inverse pair that takes text apart into character codes and puts
     *         it back together again.
     *
     *  Max's ``atoi`` ("convert characters to integers") and ``itoa`` ("convert
     *  character codes to symbol"). One implementation, two directions:
     *  everything about them — the three inlets, the accumulator behind them,
     *  the single outlet — is the same except which way the conversion runs,
     *  which is why they share a base the way ``.tosymbol`` / ``.fromsymbol``
     *  and ``.prepend`` / ``.append`` do.
     *
     *  ### What the pair is for, and how it differs from ``.spell``
     *
     *  ``.spell`` (#492) already converts a message to the character codes of
     *  its text, and ``.atoi`` gives the same answer for the same message —
     *  deliberately, since both read a character through the same decoder in
     *  ``pCharCodes.h``. The distinction is not *what* one conversion produces
     *  but what the objects are **for**:
     *
     *  - ``.spell`` is a **one-shot converter with a fixed output width**. Its
     *    ``size`` and ``fill`` arguments pad every result to the same length,
     *    which is what a patch wants when the codes are a record some reader
     *    downstream counts.
     *  - ``.atoi`` / ``.itoa`` are an **accumulator pair**. Each holds the thing
     *    it last converted, can be added to a piece at a time from a second
     *    inlet, can be re-sent with a bang, and can be emptied — the state Max
     *    gives them and ``spell`` does not have. That is what makes them the
     *    pair for *building and decomposing a symbol byte by byte*: a patch
     *    assembles a name one character at a time and sends it when it is
     *    complete, rather than having to have the whole message in hand at once.
     *  - and ``.itoa`` is the direction neither ``.spell`` nor anything else in
     *    the message family can go at all. Codes have been a dead end until now:
     *    ``.spell`` leaves the text model and nothing brought a patch back into
     *    it.
     *
     *  So ``.atoi`` into ``.itoa`` returns the message that went in, and
     *  ``.itoa`` into ``.atoi`` returns the codes that went in — the round trip
     *  is the point of the pair, and the reason both ends share one decoder.
     *
     *  ### The three inlets
     *
     *  Max gives each object three inlets and a ``clear`` message; this keeps
     *  the inlets exactly and spells ``clear`` differently, for a reason:
     *
     *  - **inlet 0** converts and sends. An int, a float or a list replaces
     *    what the object holds and the result goes straight out; a **bang**
     *    sends what it holds again, which is Max's "a bang message can be used
     *    to trigger the output of the currently stored" value.
     *  - **inlet 1** appends to what the object holds and sends **nothing** —
     *    Max's middle inlet. Appending happens at the *code* level, so feeding
     *    ``a`` and then ``b`` to a ``.atoi`` gives ``97 98``, the same as the
     *    one message ``ab`` and not the ``97 32 98`` that the two-token message
     *    ``a b`` gives.
     *  - **inlet 2** replaces what the object holds and sends nothing — Max's
     *    right inlet.
     *
     *  Max's ``clear`` is an **empty message on inlet 2**: replacing the
     *  contents with nothing is what clearing them is, and it needs no reserved
     *  word. That matters more here than almost anywhere else, because this pair
     *  exists to carry arbitrary text — a reserved ``clear`` on inlet 0 would
     *  swallow the very message a patch asked it to convert, which is the
     *  ``.prepend`` discipline the whole family keeps. ``.fromsymbol``'s empty
     *  separator is the same move.
     *
     *  A **bang** is registered on inlet 0 alone: there is nothing for one to do
     *  on the two silent inlets, so it stays out of
     *  ``inlet::GetAcceptedTypes()`` for them and the object's real contract is
     *  what a reader sees.
     *
     *  ### Refused rather than truncated, and never half-applied
     *
     *  At most ``MAX_CODES`` characters, which is Max's own "up to 256 integer
     *  character codes" and the longest list the patcher's queues carry
     *  (``patcherImplementation::kValueListCap``). A message that would take the
     *  contents past that is refused **whole**: nothing is sent, and what the
     *  object already held is left exactly as it was. Half a name is not a
     *  shorter name but a different one, and a patch assembling one a piece at a
     *  time would have no way to tell that a piece had gone missing.
     *
     *  ``.itoa`` refuses the same way anything that is not a character: a token
     *  that is not a number at all, or a number that is not a code point —
     *  negative, above U+10FFFF, or a surrogate half, none of which has an
     *  encoding. Writing something else in its place would produce bytes that
     *  ``.atoi`` reads back as a *different* code, which would break the one
     *  guarantee the pair makes.
     *
     *  Refusals are silent: an inlet may be the audio thread.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlets, and one
     *  that emitted would re-send on every DSP tick after an inlet fired — the
     *  rule ``.route``, ``.sel``, ``.prepend``, ``.spell`` and ``.tosymbol``
     *  establish.
     *
     *  No message path allocates, locks or blocks. Three buffers are reserved at
     *  construction for the widest result the direction can produce: the
     *  contents, the scratch space a conversion is built in before it is
     *  committed, and the buffer a send is made from. The scratch buffer is what
     *  makes a refusal leave no trace, and the send buffer is what keeps a patch
     *  that loops the outlet back into inlet 1 from mutating the message
     *  mid-fan-out.
     */
    class gCharCodeBase : public pObject {
    public:
      explicit gCharCodeBase(charCodeDirection convert);

      _NO_MESSAGES
      _NO_CALCULATE

      _BANG_IN(ResendBang)
      _INT_IN(ReplaceInt)
      _FLOAT_IN(ReplaceFloat)
      _LIST_IN(ReplaceList)

      _INT_IN(AppendInt)
      _FLOAT_IN(AppendFloat)
      _LIST_IN(AppendList)

      _INT_IN(StoreInt)
      _FLOAT_IN(StoreFloat)
      _LIST_IN(StoreList)

      /**
       *  @brief Most characters the object will hold — 256.
       *
       *  Max's own limit for ``itoa`` ("up to 256 integer character codes"), and
       *  the longest list the patcher's queues carry
       *  (``patcherImplementation::kValueListCap``), so anything that can reach
       *  this object through a patch can also be converted by it.
       */
      static constexpr std::size_t MAX_CODES = 256;

      /** @brief What the object is holding, in the form it would send it: the
       *         codes for ``.atoi``, the characters for ``.itoa``. Empty until
       *         something has been converted into it, and empty again after it
       *         has been cleared. */
      const std::string& Contents() const {
        return store;
      }

      /** @brief How many characters ``Contents()`` stands for — the number of
       *         codes, which is not the number of bytes for ``.itoa``. */
      std::size_t Count() const {
        return count;
      }

    protected:
      // Fills in the pieces of documentation that differ per direction; the
      // base constructor already set the category. RT-cold — constructor use
      // only.
      void Document(const char* summary, const char* dataDoc, const char* appendDoc,
                    const char* storeDoc, const char* outDoc);

    private:
      // Convert the `length` characters at `text` into `scratch`, and report
      // how many codes that is in `codes`. False means the input held something
      // that is not a character, or more than MAX_CODES of them; `scratch` is
      // then meaningless and the caller must not commit it.
      bool Build(const char* text, std::size_t length, std::size_t& codes);

      // Convert and then either replace the contents or add to them, sending
      // the result only when `emit`. Leaves the contents untouched when the
      // conversion is refused. Runs on whichever thread the message arrived on,
      // so it neither allocates nor logs.
      void Accept(const char* text, std::size_t length, bool replace, bool emit,
                  YSE::THREAD thread);

      // Same, for a number arriving on an inlet: the text of the number is what
      // gets converted, which is Max's "each of the digits" for .atoi and its
      // "a float is converted to an int" for .itoa.
      void AcceptValue(bool isInt, int intValue, float floatValue, bool replace, bool emit,
                       YSE::THREAD thread);

      // Send the contents, or nothing at all when there are none — inert rather
      // than a source of empty messages.
      void Send(YSE::THREAD thread);

      // Which way the conversion runs. Fixed by the derived constructor, so
      // choosing costs one branch on a register.
      charCodeDirection direction;

      // What the object is holding. Reserved at construction for MAX_CODES
      // characters at their widest in this direction.
      std::string store;

      // How many codes `store` holds — its length only in the .atoi direction,
      // and not even there once the codes are more than one digit.
      std::size_t count = 0;

      // Where a conversion is built before it is committed to `store`, so a
      // refused message leaves nothing half-applied behind it. Same capacity as
      // `store`.
      std::string scratch;

      // What a send is made from. A copy rather than `store` itself: the send
      // path is synchronous, so a patch looping the outlet back into inlet 1
      // would otherwise append to the very string being handed to the targets
      // that have not been reached yet.
      std::string sendBuffer;
    };

    CHARCODE_CLASS(gAtoi, YSE::OBJ::G_ATOI)
    CHARCODE_CLASS(gItoa, YSE::OBJ::G_ITOA)

  } // namespace PATCHER
} // namespace YSE
