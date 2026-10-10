#pragma once
#include "../pObject.h"
#include <cstddef>
#include <cstdint>
#include <string>

// Declares one half of the symbol pair (issue #490).
// Mirrors AFFIX_CLASS from gAffix.h: the whole two-inlet/one-outlet body lives
// in gSymbolBase, and the constructor in gSymbol.cpp only has to pick a
// direction and fill in the side-specific documentation.
#define SYMBOL_CLASS(className, typeName)                                                          \
  class className : public gSymbolBase {                                                           \
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
    enum class symbolDirection : std::uint8_t {
      To, //!< ``.tosymbol`` — collapse a message into one symbol.
      From //!< ``.fromsymbol`` — expand a symbol back into a message.
    };

    /**
     *  @brief Shared body for ``.tosymbol`` and ``.fromsymbol`` (issue #490) —
     *         the inverse pair that collapses a message into a single symbol
     *         and expands it back.
     *
     *  Max's ``tosymbol`` ("converts messages, numbers, or lists into a single
     *  symbol") and ``fromsymbol`` ("converts individual characters in a symbol
     *  into numbers or messages"). One implementation, two directions:
     *  everything about them — the data inlet, the settable separator, the one
     *  outlet — is the same except which way the conversion runs, which is why
     *  they share a base the way ``.prepend`` / ``.append`` and ``.cartopol`` /
     *  ``.poltocar`` do.
     *
     *  ### What "symbol" can honestly mean in this patcher
     *
     *  Max has atom *types*: a symbol is one atom, a list is several, and
     *  ``tosymbol`` turns the second into the first — which is why Max prints
     *  the result wrapped in double quotes when it contains spaces. **This
     *  patcher has no atom types.** A message is carried as text, and every
     *  object that reads a message apart splits that text on **whitespace**:
     *  ``.route`` and ``.routepass`` match the leading token and forward the
     *  rest, ``.sel`` and ``.substitute`` compare token by token, ``.spray``
     *  and ``.sprintf`` spread one token per outlet or slot. So the only
     *  reading of "a symbol" this model can represent is *a message that is a
     *  single whitespace-free token*, and the pair does the two conversions
     *  that actually exist here:
     *
     *  - **the token collapse.** ``.tosymbol`` joins the tokens of the incoming
     *    message with its separator, so with any non-whitespace separator the
     *    result is one token: ``voice 3 freq`` becomes ``voice/3/freq``, which
     *    a ``.route`` downstream reads as one element and a ``.forward`` will
     *    take as a destination name. ``.fromsymbol`` splits it back on the same
     *    separator, so the round trip the issue asks for — carry structured
     *    data through anything that takes a *name*, and take it apart again —
     *    is the pair used with a matching separator on both ends.
     *  - **the type collapse.** ``.tosymbol`` also turns a bang, an int and a
     *    float into the text that spells them, sent as a list; ``.fromsymbol``
     *    restores the type, so a single token that reads as a number leaves as
     *    an int or a float and the token ``bang`` leaves as a bang — Max's "the
     *    word bang sent as a part of a symbol will be converted to a message".
     *    That half of the conversion needs no separator at all and is what the
     *    pair does in its default configuration.
     *
     *  What is deliberately **not** faked is Max's default and Max's quoting.
     *  Max's ``separator`` attribute defaults to a space, and a space separator
     *  cannot produce a single token here, because whitespace is precisely what
     *  separates tokens. So with the default separator ``.tosymbol`` performs
     *  the type collapse and normalises the whitespace, and nothing else; no
     *  quotes are invented around the result, because nothing anywhere in this
     *  patcher reads a quote as anything but an ordinary character, and a
     *  quoted message would simply be a *different* message to every object
     *  downstream. A patch that wants a real symbol names a separator.
     *
     *  The same limit bounds what ``.fromsymbol`` can restore: a type is a
     *  property of the *whole* message here, so only a result that came out as
     *  a single token can become an int, a float or a bang. ``bang/5`` split on
     *  ``/`` is the two-token list ``bang 5``, not a list with a bang atom in
     *  it, because there is no such thing here.
     *
     *  ### The separator has its own inlet
     *
     *  Max spells it as an ``@separator`` attribute. Here it is a creation
     *  argument and inlet 1 — the ``.prepend`` / ``.forward`` / ``.substitute``
     *  discipline, which matters as much here as anywhere: these objects exist
     *  to carry arbitrary words, so reserving one out of inlet 0 would cost
     *  them the very messages they are for.
     *
     *  Inlet 1 takes a **list** (its first token), an **int** and a **float**,
     *  the last two spelled through ``ExprFormatValue`` so a separator computed
     *  upstream reads back as the number a patch author would have typed. An
     *  **empty** message on it is Max's ``separator`` *with no arguments*,
     *  documented as "removes all spaces (e.g. ``1 2 3 4`` becomes ``1234``)":
     *  the separator becomes empty, ``.tosymbol`` joins with nothing at all,
     *  and ``.fromsymbol`` — having nothing to split on — falls back to Max's
     *  own phrasing and takes the symbol apart into its *individual
     *  characters*, which is the exact inverse. A **bang** is declined: nothing
     *  for it to do, so it stays out of ``inlet::GetAcceptedTypes()``. The
     *  space default is a creation-time state, so ``SetParams`` restores it,
     *  the way re-configuring an object works everywhere else.
     *
     *  An empty piece is dropped rather than emitted: ``a//b`` split on ``/``
     *  is ``a b``, because an empty token would show up as a doubled separator
     *  that no ``.route`` matches — the hazard ``.prepend`` trims for and
     *  ``.substitute`` refuses an empty replacement over.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlets, and
     *  one that emitted would re-convert on every DSP tick after the inlet
     *  fired — the rule ``.route``, ``.sel``, ``.prepend`` and ``.substitute``
     *  establish.
     *
     *  No message path allocates, locks or blocks. The separator is refilled
     *  into ``SEPARATOR_CAPACITY`` reserved at construction and a longer token
     *  is refused rather than truncated (silently on the inlet, which may be
     *  the audio thread; loudly on the creation argument, which is
     *  control-thread only). The result is built in a buffer reserved at
     *  construction for the worst case the patcher's own queues can produce: a
     *  256-character list — ``patcherImplementation::kValueListCap`` — is at
     *  most 128 tokens, each of which may be followed by a
     *  ``SEPARATOR_CAPACITY``-character separator, which also covers the
     *  expansion of 256 characters into 256 single-character tokens. So a
     *  conversion is one bounded walk plus ``append``s into memory the object
     *  already owns.
     */
    class gSymbolBase : public pObject {
    public:
      explicit gSymbolBase(symbolDirection convert);

      _NO_MESSAGES
      _NO_CALCULATE

      _BANG_IN(ConvertBang)
      _INT_IN(ConvertInt)
      _FLOAT_IN(ConvertFloat)
      _LIST_IN(ConvertList)

      _INT_IN(SetSeparatorInt)
      _FLOAT_IN(SetSeparatorFloat)
      _LIST_IN(SetSeparatorList)

      _PARM_CLEAR
      _PARM_PARSE

      /**
       *  @brief Longest separator the object will hold, in characters.
       *
       *  16 — generous for what is normally one punctuation mark, and small
       *  enough that the worst-case result the object reserves room for stays
       *  a couple of kilobytes. A longer token is refused rather than
       *  truncated: half a separator is a different separator, and the two
       *  ends of a round trip have to agree on it exactly or the message never
       *  comes back.
       */
      static constexpr std::size_t SEPARATOR_CAPACITY = 16;

      /**
       *  @brief What tokens are joined with, and split on. A single space
       *         until a creation argument or inlet 1 says otherwise — Max's
       *         own default, which in this message model performs the type
       *         conversion but no token collapse. Empty is Max's ``separator``
       *         with no arguments: join with nothing, split per character.
       */
      const std::string& Separator() const {
        return separator;
      }

    protected:
      // Fills in the pieces of documentation that differ per direction; the
      // base constructor already set the category. RT-cold — constructor use
      // only.
      void Document(const char* summary, const char* dataDoc, const char* outDoc,
                    const char* paramDoc);

    private:
      // ``.tosymbol``: join the whitespace-separated tokens of the `length`
      // characters at `text` with the separator, and send the result as a list.
      void Join(const char* text, std::size_t length, YSE::THREAD thread);

      // ``.fromsymbol``: split the `length` characters at `text` on the
      // separator and send the pieces as the type they read as.
      void Split(const char* text, std::size_t length, YSE::THREAD thread);

      // Split one whitespace-delimited token on the separator, appending each
      // non-empty piece to `outText`. With an empty separator every character
      // is its own piece — Max's "individual characters in a symbol".
      void SplitToken(const char* text, std::size_t length);

      // Append one piece to `outText`, space-separated from the previous one.
      // Empty pieces are dropped: they would read back as a doubled separator.
      void AppendPiece(const char* text, std::size_t length);

      // Send whatever `outText` now holds, restoring the type when it came out
      // as a single token — the inverse of the type collapse ``.tosymbol``
      // does. List otherwise.
      void SendExpanded(YSE::THREAD thread);

      // Replace the separator with the first whitespace-delimited token of the
      // `length` characters at `text`; nothing there empties it, which is Max's
      // `separator` with no arguments. Runs on whichever thread sent the
      // message, so it neither allocates (the buffer was reserved on the
      // control thread) nor logs.
      void SetSeparatorText(const char* text, std::size_t length);

      // Which way the conversion runs. Fixed by the derived constructor, so
      // choosing costs one branch on a register.
      symbolDirection direction;

      // The creation argument — a STRING parameter, so it takes one token.
      // Control thread only: written by Parameters::Set, read by ParseParams(),
      // never by a message handler.
      std::string separatorArg;

      // The live separator. Reserved to SEPARATOR_CAPACITY at construction so
      // the inlet path refills it without allocating.
      std::string separator;

      // Where the result is built. Reserved at construction for the worst case
      // a 256-character list can grow into — see the class documentation.
      std::string outText;
    };

    SYMBOL_CLASS(gToSymbol, YSE::OBJ::G_TOSYMBOL)
    SYMBOL_CLASS(gFromSymbol, YSE::OBJ::G_FROMSYMBOL)

  } // namespace PATCHER
} // namespace YSE
