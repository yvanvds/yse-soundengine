#pragma once
#include "../pObject.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Format a message from words and numbers — ``.sprintf`` (issue
     *         #489).
     *
     *  Max's ``sprintf``, whose one-line summary is "Format a message of words
     *  and numbers" and whose description is "Uses the common C-language
     *  'printf' function inside Max. You can combine symbols, organize lists of
     *  numbers, or format messages or menu items". The creation arguments are a
     *  format: literal text with **changeable arguments** in it — ``%s`` for a
     *  symbol, ``%ld`` for an int, ``%f`` for a float and ``%c`` for an int
     *  rendered as a character. "The number of inlets is determined by the
     *  number of changeable arguments, with each inlet corresponding to a
     *  changeable argument, in order."
     *
     *  ### What it adds that the patcher could not already express
     *
     *  ``.prepend`` / ``.append`` (#487) can put a word on the end of a message
     *  and ``.substitute`` (#488) can swap one word for another, but all three
     *  work in whole tokens: none of them can build a token *out of* a value.
     *  Composing ``/voice/3/freq`` out of the number 3, or ``chan 07`` out of
     *  the number 7, meant leaving the patcher for the host application and
     *  coming back — which is exactly the round trip the message-construction
     *  family exists to remove. This object closes it: a value goes in, a
     *  spelled-out message comes out, and a ``.forward`` or a ``.s`` downstream
     *  can use the result as a destination name.
     *
     *  ### The format is compiled once, and never handed to printf
     *
     *  A format string here comes from a patch author, and a patch may be
     *  loaded from a file. Handing it to ``printf`` would let the *data* decide
     *  how many arguments to read off the stack and what to make of them, which
     *  is the classic format-string vulnerability and is undefined behaviour
     *  besides. So the format is **parsed here**, once, on the control thread,
     *  into a list of literal runs and typed slots; nothing on the message path
     *  ever sees a ``%``, and every argument is rendered explicitly by the
     *  patcher's own writers — ``WriteInt`` for an int and a hand-rolled
     *  fixed-point writer for a float, both of which are allocation-free and
     *  locale-free, so the decimal separator is always ``.`` no matter what
     *  another thread has done to the locale.
     *
     *  The accepted grammar is ``%`` , optional ``-`` and ``0`` flags, an
     *  optional field width, an optional ``.precision``, optional ``l`` / ``h``
     *  length modifiers (read and ignored — this patcher has one integer type
     *  and one floating-point one), and one of ``d i f F s c``. ``%%`` is a
     *  literal per cent sign. Anything else is **not** a specifier: it is
     *  reported at ``E_ERROR`` naming the object and copied into the output as
     *  the text it was typed as, so a typo shows up in the log *and* in the
     *  message rather than silently eating an inlet. Width is capped at
     *  ``WIDTH_MAX`` and precision at ``PRECISION_MAX``, and at most
     *  ``MAX_SLOTS`` changeable arguments are taken; past that the specifiers
     *  are literal text too, for the same reason.
     *
     *  ### Which inlet acts
     *
     *  Max: "Any of the above messages in the left inlet will format the message
     *  and send it out." So inlet 0 is hot — a bang, an int, a float or a list
     *  formats and sends — and every other inlet is cold and only stores. Inlet
     *  0 is also the first changeable argument's inlet, which is why an int into
     *  it both stores and fires.
     *
     *  Inlet 0 keeps **no reserved words** — the ``.prepend`` / ``.substitute``
     *  discipline, and it matters here because the values this object formats
     *  are often symbols. A list into any inlet is spread one token per slot
     *  from the receiving one rightwards, Max's "each item in the list is
     *  treated as if it had been received in a separate inlet, up to the number
     *  of inlets"; tokens past the last slot are dropped.
     *
     *  Every slot inlet accepts an int, a float and a list, rather than only the
     *  one type its conversion names. Max routes each type to the inlets that
     *  can use it and ignores the rest; refusing here would mean a patch that
     *  feeds a counter into a ``%s`` gets silence rather than the digits it
     *  obviously meant. So a number arriving at a ``%s`` is stored as the text
     *  that spells it, and a token arriving at a ``%ld`` or a ``%f`` is stored
     *  as the number it reads as — or, when it does not read as one, refused,
     *  leaving the previous value in place rather than a zero the patch never
     *  sent. A bang means nothing on a cold inlet and so stays out of
     *  ``inlet::GetAcceptedTypes()``.
     *
     *  ### What an unset slot renders as
     *
     *  Max: "If no value has been received for a changeable number argument
     *  (``%ld`` or ``%f``), 0 will be substituted for that argument. If no value
     *  has been received for a ``%s`` or ``%c`` argument, that argument will be
     *  left blank." Both are kept, including the asymmetry: a number has a
     *  neutral value and a symbol does not.
     *
     *  A ``.sprintf`` with **no format at all** emits nothing — the ``.prepend``
     *  rule that makes it safe to drop an unconfigured object into a working
     *  patch. A format with no changeable arguments in it is a constant message
     *  generator, which is what Max's is too: one inlet, and every message into
     *  it sends the text.
     *
     *  ### symout
     *
     *  Max's first-argument flag, "the ``sprintf`` object outputs the string it
     *  generates as a single symbol. Otherwise the output is a list of symbols
     *  and/or numbers". This patcher carries every message as one piece of text,
     *  so the distinction it draws does not exist here and there is nothing for
     *  the flag to switch. It is still recognised and consumed, because Max also
     *  says "the word ``symout`` itself is not included in the output" — reading
     *  it as part of the format would put a stray word at the front of every
     *  message a patch copied over from Max.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlets, and one
     *  that emitted would re-send on every DSP tick after the inlet fired — the
     *  rule ``.route``, ``.sel``, ``.prepend`` and ``.substitute`` establish.
     *
     *  No message path allocates, locks or blocks. The compiled format, the slot
     *  table and every buffer are built by ``ShapePorts()`` on the control
     *  thread before the object is wired or published; a symbol slot holds
     *  ``TOKEN_CAPACITY`` characters reserved there and refuses a longer token
     *  rather than truncating it, and the output buffer is reserved for the
     *  literal text plus the widest thing every slot can render. So formatting
     *  is a bounded walk of the compiled pieces and a series of ``append``s into
     *  memory the object already owns.
     */
    PATCHER_CLASS(gSprintf, YSE::OBJ::G_SPRINTF)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(SetBang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief Most changeable arguments a format may have, and so the most
     *         inlets the object will build. Past this the specifiers are copied
     *         through as literal text and reported. */
    static constexpr std::size_t MAX_SLOTS = 32;

    /** @brief Longest symbol a ``%s`` slot will hold, in characters. Refused
     *         rather than truncated past that: half a symbol is a different
     *         symbol, and the point of the object is a message the boxes
     *         downstream still recognise. */
    static constexpr std::size_t TOKEN_CAPACITY = 64;

    /** @brief Widest field a specifier may ask for. Bounds the output buffer,
     *         which is reserved before the object is published. */
    static constexpr int WIDTH_MAX = 64;

    /** @brief Most decimals (or, for ``%s``, most characters) a ``.precision``
     *         may ask for. C's default of 6 applies to ``%f`` without one. */
    static constexpr int PRECISION_MAX = 9;

    /** @brief The format, as it will be rendered: the creation arguments joined
     *         back together, less a leading ``symout``. Empty when the object is
     *         unconfigured, which is when it emits nothing. */
    const std::string& Format() const {
      return format;
    }

    /** @brief How many changeable arguments the format compiled to — one inlet
     *         each, and the object still has inlet 0 when this is 0. */
    int SlotCount() const {
      return (int)slots.size();
    }

    /** @brief Whether Max's ``symout`` flag was given. Recognised and consumed
     *         so it does not end up in the output; it switches nothing, because
     *         this patcher has one message type rather than Max's symbol/list
     *         pair. */
    bool SymOut() const {
      return symOut;
    }

    /** @brief The last text this object sent, for tests and for a host reading
     *         the object's state. Empty until it has formatted once. */
    const std::string& LastOutput() const {
      return outText;
    }

  private:
    // What a changeable argument renders. `INT` and `CHAR` both consume an
    // integer — Max: "a %c argument will convert the int to its ASCII character
    // equivalent" — but they differ in what an *unset* slot renders as, which is
    // Max's own asymmetry: a number defaults to 0, a character to nothing.
    enum class Kind { INT, FLOAT, STRING, CHAR };

    // One changeable argument: how it is spelled, and what it is currently
    // holding. Built by ShapePorts() on the control thread; the value fields are
    // the only part a message handler touches.
    struct Slot {
      Kind kind = Kind::INT;
      bool leftAlign = false;
      bool zeroPad = false;
      int width = 0; // 0 = no minimum field width
      int precision = -1; // -1 = none given

      bool hasValue = false;
      int intValue = 0;
      float floatValue = 0.f;
      std::string text; // reserved to TOKEN_CAPACITY when the ports are shaped
    };

    // The compiled format: a run of literal characters, optionally followed by
    // the changeable argument that comes after it. Literal runs are spans into
    // `format` rather than copies, so compiling costs one vector and rendering
    // costs no lookups.
    struct Piece {
      std::size_t begin = 0;
      std::size_t length = 0;
      int slot = -1; // -1 = the run is the whole piece
    };

    // Outcome of reading one `%...` sequence.
    enum class SpecResult { OK, ESCAPE, INVALID };

    // Rebuild the format text, the compiled pieces, the slots and the ports from
    // the creation arguments. Control thread only, and only before the object is
    // wired or published — see the note in the definition.
    void ShapePorts();

    // How a conversion is spelled, for the documentation range of the inlet it
    // built. Max's own spellings, so a reader recognises them.
    static const char* KindSpelling(Kind kind);

    // Read the specifier that starts at `pos` in `format` into `slot`, leaving
    // `next` at the first character after it. Does not touch `slot` unless it
    // returns OK.
    SpecResult ReadSpec(std::size_t pos, Slot& slot, std::size_t& next) const;

    // Store into slot `index`, converting to whatever the slot's conversion
    // wants. Out-of-range indices and tokens the slot cannot use are ignored,
    // silently: these run on whichever thread sent the message.
    void StoreInt(std::size_t index, int value);
    void StoreFloat(std::size_t index, float value);
    void StoreToken(std::size_t index, const char* text, std::size_t length);

    // Spread the whitespace-separated tokens of `text` over the slots from
    // `first` rightwards — Max's "each item in the list is treated as if it had
    // been received in a separate inlet, up to the number of inlets".
    void Distribute(const std::string& text, std::size_t first);

    // Render one slot into `outText`, padded to its field width.
    void RenderSlot(const Slot& slot);

    // Format the whole message into `outText` and send it out outlet 0. Does
    // nothing at all when no format was given.
    void Emit(YSE::THREAD thread);

    std::string format;
    std::vector<Piece> pieces;
    std::vector<Slot> slots;
    bool symOut = false;

    // The creation arguments. Control thread only: written by Parameters::Set,
    // read by ShapePorts(), never by a message handler.
    std::vector<std::string> creationArgs;

    // Where the message is built. Reserved by ShapePorts() for the literal text
    // plus the widest rendering every slot can produce, so no format allocates.
    std::string outText;
  };

} // namespace PATCHER
} // namespace YSE
