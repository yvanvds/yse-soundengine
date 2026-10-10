#pragma once
#include "../pObject.h"
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Spell whatever arrives out as the character codes of its text —
     *         ``.spell`` (issue #492).
     *
     *  Max's ``spell``, "convert input to UTF-8 (Unicode) codes". Max documents
     *  it as four separate rules, one per atom type:
     *
     *  - **int** — "the ASCII value of each of the digits of the number is sent
     *    out the outlet, one digit at a time";
     *  - **symbol** — "the ASCII value of each letter, digit, or other character
     *    in the symbol is sent out the outlet, one character at a time";
     *  - **list** — "each int in the list is converted to ASCII as described
     *    above, and a space character (32) is sent out between items in the
     *    list";
     *  - **anything** — "all int and symbol items in the message are converted
     *    to ASCII one character at a time".
     *
     *  ### One rule, because this patcher has one kind of message
     *
     *  Those four collapse into a single rule here, and not by cutting a corner:
     *  a message in this patcher **is text** — there are no atom types, and
     *  every object that reads a message apart splits that text on whitespace
     *  (see ``gSymbolBase``). So ``.spell`` sends the code of every character of
     *  the message as it stands, and each of Max's four rules falls out of that
     *  one:
     *
     *  - an int is spelled digit by digit, because the digits *are* its text —
     *    ``ExprFormatValue`` writes the number the way a patch author would have
     *    typed it, so 123 spells 49 50 51;
     *  - a symbol is spelled character by character, which is the rule itself;
     *  - a list gets Max's space **32** between its items, not as a rule of its
     *    own but because a space is the character that is actually there between
     *    them;
     *  - and ``anything`` needs no separate treatment, since nothing here
     *    distinguishes it from a list in the first place.
     *
     *  Runs of whitespace collapse to one space and the ends are trimmed, so
     *  what is spelled is the message's tokens with a single 32 between them —
     *  Max's own spacing — rather than however much incidental whitespace the
     *  sender happened to leave in. That also means ``.spell`` is stable under
     *  the whitespace normalisation every other object in the family performs.
     *
     *  ### What a "character" is: the encoding, stated
     *
     *  A patcher message is a ``std::string``, which is **bytes**; nothing
     *  upstream promises an encoding, and a patch that reads a file, takes a
     *  name from a host application or types an accented word into a ``.m`` can
     *  and does put non-ASCII bytes in one. Max calls its codes UTF-8 (Unicode)
     *  and so does this, concretely:
     *
     *  - bytes forming a **well-formed UTF-8 sequence** are decoded to the one
     *    code point they spell, so ``é`` (0xC3 0xA9) spells **233**, not 195
     *    206. ASCII is the one-byte case of that, unchanged;
     *  - a byte that is **not** part of a well-formed sequence — a stray
     *    continuation byte, a truncated sequence, an overlong encoding, a
     *    surrogate half, or a value above U+10FFFF — spells **its own value,
     *    0–255**, and decoding resumes at the next byte.
     *
     *  The fallback is the whole point of saying this out loud: the object never
     *  fails, never drops a byte and never has to guess, so a patch fed Latin-1
     *  or raw binary gets one code per byte for the parts that are not UTF-8 and
     *  the right code point for the parts that are. What it must *not* do is
     *  pretend non-ASCII cannot arrive, since that is exactly where an
     *  encoding-blind implementation would silently emit two codes for one
     *  visible character.
     *
     *  ### ``size`` and ``fill``, Max's two arguments
     *
     *  ``size`` is Max's first argument: "sets the minimum output size. Any
     *  input that doesn't 'spell' to the minimum length is followed by enough
     *  fill characters." ``fill`` is Max's second: the code padded with, 32 (a
     *  space) by default. Both are creation arguments, as they are in Max, so
     *  inlet 0 keeps no reserved words in it — the ``.prepend`` discipline,
     *  which matters here for the same reason it does for ``.combine``: this
     *  object exists to carry arbitrary text, and a reserved word would swallow
     *  the very messages it is asked to spell.
     *
     *  Max's "if you want to use '0' as a fill character, use any negative
     *  number" is **not** reproduced, and the reason is that it solves a problem
     *  this patcher does not have. It exists because Max cannot tell an absent
     *  second argument from one that is 0, so 0 has to mean "default" and some
     *  other value has to stand in for the character ``0``. Arguments are read
     *  as tokens here, so ``.spell 8 48`` says "pad with the character 0"
     *  directly and 0 means the code 0. A negative fill therefore means nothing:
     *  it is reported and the default kept, rather than silently turning into
     *  48, which would be a trap for anyone who wrote ``-1`` meaning something
     *  else.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlets, and one
     *  that emitted would re-spell on every DSP tick after an inlet fired — the
     *  rule ``.route``, ``.sel``, ``.prepend``, ``.substitute``, ``.sprintf``
     *  and ``.combine`` establish.
     *
     *  No message path allocates, locks or blocks. At most ``MAX_CODES`` codes
     *  are produced and the output buffer is reserved for that many at full
     *  width at construction, so a spelling is one walk of the bytes and a
     *  series of writes into memory the object already owns. Input that would
     *  spell past that limit is **refused rather than truncated** — silently on
     *  the inlet, which may be the audio thread — because half a spelling is not
     *  a shorter word but a different one, and a patch reassembling text from
     *  codes downstream would have no way to tell the two apart.
     */
    PATCHER_CLASS(gSpell, YSE::OBJ::G_SPELL)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(SpellInt)
    _FLOAT_IN(SpellFloat)
    _LIST_IN(SpellList)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief Most codes one message may spell to, padding included. 256, the
     *         longest list the patcher's own queues carry
     *         (``patcherImplementation::kValueListCap``), so anything that can
     *         reach this object through a patch can also be spelled by it.
     *         Longer input is refused rather than truncated. */
    static constexpr std::size_t MAX_CODES = 256;

    /** @brief The code padded with when no ``fill`` argument is given — 32, a
     *         space, which is Max's default too. */
    static constexpr int DEFAULT_FILL = 32;

    /** @brief Highest code point a ``fill`` argument may name. */
    static constexpr int MAX_CODE_POINT = 0x10FFFF;

    /** @brief Fewest codes the object will send, padding with the fill code to
     *         reach it. 0 — no minimum — unless a creation argument says
     *         otherwise. */
    int MinimumSize() const {
      return minimumSize;
    }

    /** @brief The code short output is padded with. */
    int FillCode() const {
      return fillCode;
    }

    /** @brief The last list this object sent, for tests and for a host reading
     *         the object's state. Empty until it has sent once, and emptied
     *         again by a message it refused or had nothing to spell of. */
    const std::string& LastOutput() const {
      return outText;
    }

  private:
    // Spell the `length` bytes at `text` into `outText` and send it. Refuses,
    // sending nothing, when the spelling would run past MAX_CODES; sends
    // nothing when there is nothing to send. Runs on whichever thread the
    // message arrived on, so it neither allocates nor logs.
    void Spell(const char* text, std::size_t length, YSE::THREAD thread);

    // Append one code to `outText`, space-separated from the previous one, and
    // count it. Returns false once MAX_CODES has been passed, which is the
    // caller's signal to abandon the spelling.
    bool AppendCode(unsigned int code, std::size_t& count);

    // Max's `size`. Control thread only between messages: written by
    // Parameters::Set through the parse callback, read by Spell().
    int minimumSize = 0;

    // Max's `character`. Same threading as minimumSize.
    int fillCode = DEFAULT_FILL;

    // Where the codes are written. Reserved at construction for MAX_CODES codes
    // at full width, so no spelling allocates.
    std::string outText;
  };

} // namespace PATCHER
} // namespace YSE
