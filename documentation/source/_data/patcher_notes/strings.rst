.. Notes for the Strings and symbols category. Format: _data/patcher_notes/README.md.

.. patcher-object:: .atoi

The message is converted as it stands, as text: an int digit by digit
(``123`` gives ``49 50 51``), a float including its decimal point, and a list
character by character with ``32`` (a space) between its items. Runs of
spaces count as one, and spaces at the ends are dropped.

The text is read as UTF-8, so an accented letter gives one code, not two:
``é`` gives ``233``. A byte that is not part of a valid UTF-8 character gives
its own value, 0 to 255. ``.spell`` reads text the same way.

**Inlets**

- Inlet 0 replaces the stored codes and sends them. A bang sends them again
  without converting anything.
- Inlet 1 adds codes to the end and sends nothing. Characters are added with
  no space between messages: ``a`` then ``b`` stores ``97 98``, while the one
  message ``a b`` gives ``97 32 98``.
- Inlet 2 replaces the stored codes and sends nothing.

An empty message on inlet 0 or inlet 2 clears the stored codes. Nothing is
sent while nothing is stored.

The codes leave together as one list. Use ``.iter`` to get them one at a
time.

**Limits**

At most 256 codes. A message that would take the total past 256 is refused
whole: the stored codes stay as they were, and nothing is logged.

.. patcher-object:: .itoa

An int or a float gives one character (a float is truncated first), and a
list gives one character per code. The text is written as UTF-8, so
``.atoi`` after ``.itoa`` gives back the codes that went in, and ``.itoa``
after ``.atoi`` gives back the message.

The result is always a list, even when it reads as a number: ``49 50`` gives
the text ``12``. Send it through ``.fromsymbol`` to turn it into the int
``12``.

A token that is not a number, or a code that is not a character (negative,
above 1114111, or in the range 55296-57343), refuses the whole message.

**Inlets**

The three inlets work as on ``.atoi``: inlet 0 replaces and sends (a bang
sends again), inlet 1 adds and sends nothing, inlet 2 replaces and sends
nothing. An empty message on inlet 0 or inlet 2 clears the stored text. To
build a word one character at a time, send each code to inlet 1 and bang
inlet 0 when it is complete.

**Limits**

At most 256 characters. A message that would take the total past 256 is
refused whole: the stored text stays as it was, and nothing is logged.

.. patcher-object:: .combine

The creation arguments are the items. Each one gets an inlet and is that
inlet's starting value. The output is the items written one after another,
with nothing in between. To put a separator in, make it an item of its own:

.. code-block:: text

   [.combine synth / lead / note]   inlet 4 <- 60, then bang   =>   synth/lead/60

**What an inlet stores**

- A number is stored as its text.
- A list fills this item and the ones to its right, one token each. Tokens
  past the last item are dropped.
- An empty message clears the item. An item that was never set, or was
  cleared, adds nothing to the output.

When every item is empty, nothing is sent.

**Which inlets send**

By default inlet 0 stores and sends and the other inlets only store. Start
the arguments with ``triggers`` to change that:

- ``triggers -1``: every inlet sends.
- ``triggers <n>``: only inlet ``n`` sends.

A bang on inlet 0 always sends the current items. If ``triggers`` is not
followed by a whole number, or names an inlet that does not exist, the
problem is logged: ``triggers`` is then read as an item, or inlet 0 stays the
one that sends.

There is no option to pad numbers to a fixed width; use ``.sprintf`` with a
width (``%03d``) for that.

**Limits**

At most 32 items of at most 64 characters each. An argument that is too long
is logged and the item left empty. A longer item arriving at an inlet is
ignored and the item keeps its value.

.. patcher-object:: .fromsymbol

Each token of the message is split on the separator, and the pieces leave as
an ordinary list. Spaces in the message separate pieces too. Empty pieces
are dropped, so ``a//b`` split on ``/`` gives ``a b``.

When the result is a single token, it leaves as the type it reads as: a
number leaves as an int or a float, and the word ``bang`` leaves as a bang.
Anything longer leaves as a list. An int, a float or a bang that arrives
passes through as it is.

**The separator**

The separator is the creation argument, and inlet 1 changes it. The default
is a single space, which only evens out the spacing and restores the type.
An empty separator splits a symbol into single characters, so ``1234``
gives ``1 2 3 4``. A separator is one token of at most 16 characters; a
longer one is ignored (and logged when it is the argument).

.. code-block:: text

   [.tosymbol /]  <- voice 3 freq   =>   voice/3/freq
   [.fromsymbol /] <- voice/3/freq  =>   voice 3 freq

.. patcher-object:: .regexp

The first creation argument is the pattern. Everything after it, joined with
single spaces, is the substitution, which is optional:

.. code-block:: text

   [.regexp ^/synth/(\d+)/(\w+)$ %2]   <- /synth/3/freq
   outlet 2: /synth/3/freq   outlet 1: 3 freq   outlet 0: freq

The pattern is one token, so it cannot hold a space: write ``\s`` for any
whitespace or ``\x20`` for exactly one space.

**What is sent**

For each match, from left to right, the matched text leaves outlet 2 and
then its capture groups leave outlet 1 as one list. Outlet 1 stays silent
when the pattern has no groups, and a group that took no part in the match
is empty. After the last match, when a substitution is set, outlet 0 sends
the whole message with every match replaced.

A message the pattern matches nowhere leaves outlet 3 unchanged, and nothing
else is sent, so the object also works as a filter. An int or a float is
matched as the text that spells it.

In the substitution, ``%1`` to ``%9`` insert a capture group, ``%0`` the whole
match and ``%%`` a percent sign. A ``%`` before anything else is copied as
it is.

**Pattern syntax**

- Literal characters, and ``.`` for any character.
- Character classes: ``[abc]``, ranges ``[a-z]``, negation ``[^0-9]``.
- ``\d \D \w \W \s \S``, inside a class or outside it.
- Escapes for the special characters (``\.``, ``\(``, ``\\`` …), ``\t \n
  \r``, and ``\xHH`` for a character by its hexadecimal code.
- ``^`` and ``$`` anchor to the start and end of the whole message.
- ``* + ?`` and ``{n}``, ``{n,}``, ``{n,m}``. They are greedy; add ``?``
  for the shortest match (``*?``).
- Alternation with ``|``.
- Capturing groups ``( )``, at most nine, and non-capturing ``(?: )``.

Backreferences, lookahead and lookbehind, named groups, ``\b`` and ``\B``,
``\A`` and ``\Z``, possessive quantifiers and inline flags are not
supported. Neither is a repeat of something that can match nothing, such as
``(a*)*``. Such a pattern is logged when the object is created, and the
object then sends every message out of outlet 3.

**Limits**

A pattern is at most 256 characters, a ``{n,m}`` count at most 64 and groups
nest at most 16 deep. A message longer than 512 characters leaves outlet 3
unmatched. A pattern that takes too long on one message stops searching
there; when nothing had matched yet, the message counts as unmatched. A
substitution result longer than 4096 characters is not sent. Safe on the
audio thread: matching never allocates memory and its time is bounded.

.. patcher-object:: .spell

The message is spelled as it stands, by the same rules as ``.atoi``: an int
digit by digit, a float including its decimal point, a list character by
character with ``32`` between its items, and UTF-8 text one code per
character. Unlike ``.atoi`` it stores nothing and does not take a bang.

The two creation arguments pad a short result to a fixed length, which is
useful for fixed-width records:

.. code-block:: text

   [.spell 5]      <- hi   =>   104 105 32 32 32
   [.spell 5 48]   <- hi   =>   104 105 48 48 48

The first argument is the minimum number of codes (at most 256), the second
the fill code, 32 (a space) by default. A negative fill code is logged and
the default kept.

Nothing is sent when there is nothing to spell and no minimum. A message that
would spell to more than 256 codes sends nothing at all.

.. patcher-object:: .sprintf

The creation arguments are the format: text with placeholders in it. Each
placeholder gets one inlet, in order. Inlet 0 formats and sends; the other
inlets only store.

.. code-block:: text

   [.sprintf /voice/%d/%s]   inlet 1 <- freq   inlet 0 <- 3   =>   /voice/3/freq

**Placeholders**

- ``%d`` or ``%i``: an int. ``%ld`` and other ``l`` / ``h`` forms are read
  the same way.
- ``%f``: a float, with 6 decimals unless a precision is given.
- ``%s``: a word.
- ``%c``: an int written as the character with that code (1 to 255).
- ``%%``: a percent sign.

Between the ``%`` and the letter a placeholder may have, in this order: the
flags ``-`` (align left) and ``0`` (pad a number with zeros), a width of at
most 64, and a ``.precision`` of at most 9 (decimals for ``%f``, the most
characters for ``%s``). So ``%03d`` writes ``7`` as ``007`` and ``%.2f``
writes ``0.5`` as ``0.50``. Anything else after a ``%``, including a larger
width or precision, is logged and copied into the output as it was typed. A leading word ``symout`` is accepted and
dropped; it changes nothing.

**What an inlet stores**

- Every inlet takes an int, a float or a list, whatever its placeholder.
- A number sent to a ``%s`` is stored as its text. A word sent to a ``%d`` or
  ``%f`` is ignored and the previous value kept. A word sent to a ``%c``
  uses its first character.
- A list fills this placeholder and the ones to its right, one token each;
  tokens past the last placeholder are dropped.
- A bang on inlet 0 formats with the values stored so far.

A ``%d`` or ``%f`` that was never set writes ``0``; a ``%s`` or ``%c`` writes
nothing. A format with no placeholders sends the same text every time.
Without a format, nothing is sent.

``%f`` rounds a value exactly halfway between two decimals away from zero. A
value too large to write in full is written with an exponent.

**Limits**

At most 32 placeholders; more are logged and copied through as text. A word
for a ``%s`` is at most 64 characters; a longer one is ignored and the
previous value kept.

.. patcher-object:: .tolower

Only the letters A-Z change. Letters outside ASCII (``É``, ``Ж``) are left
as they are, because their case depends on the language. Numbers,
punctuation and spacing are unchanged, so the tokens of the message stay
the same.

A bang, an int or a float passes through in its own type, and an empty
message is passed on as an empty message.

.. code-block:: text

   [.tolower] -> [.route play stop]   accepts Play, PLAY and play

.. patcher-object:: .tosymbol

A symbol is a message of one token (see :doc:`/patcher/messages`). This
object makes one by joining the tokens of a message with its separator, so
the result can be matched by ``.route`` as one item, or used as a name. A
bang becomes the word ``bang`` and a number becomes its text. The result
always leaves as a list.

Runs of spaces count as one, and spaces at the ends are dropped, so the
result never holds an empty token.

**The separator**

The separator is the creation argument, and inlet 1 changes it. The default
is a single space, which leaves the tokens apart and only turns numbers and
bangs into text. An empty separator joins the tokens with nothing between
them: ``1 2 3 4`` becomes ``1234``. A separator is one token of at most 16
characters; a longer one is ignored (and logged when it is the argument).

``.fromsymbol`` with the same separator turns the result back into the
original message.

.. patcher-object:: .toupper

Works like ``.tolower`` in the other direction: only the letters a-z
change, and everything else, including letters outside ASCII, is left as it
was.
