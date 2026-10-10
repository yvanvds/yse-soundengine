.. Notes for the Lists category. Format: _data/patcher_notes/README.md.

.. patcher-object:: .append

The mirror of ``.prepend``: the stored message goes after the incoming one
instead of in front. Everything else, including the right inlet, works as
described under ``.prepend``.

.. code-block:: text

   [.append 1]   60 100   =>  60 100 1

.. patcher-object:: .iter

Each item is sent and fully handled downstream before the next one leaves
(see :doc:`/patcher/messages`), so a counter or accumulator after ``.iter``
sees the items in order. Items keep the type they are spelled as. A list of
up to 256 items is accepted; see *List length limits* in
:doc:`/patcher/messages` for what happens to a longer one. A bang before any
list has arrived sends nothing.

A list wired back from the outlet into the inlet while ``.iter`` is still
sending is ignored, so a loop cannot restart the iteration.

.. patcher-object:: .join

Each inlet holds a whole message, so the length of the result is the total of
the pieces, not the number of inlets:

.. code-block:: text

   [.join 2]      inlet 1 <- 3 4,  inlet 0 <- 1 2   =>  1 2 3 4
   [.pack 0 0]    inlet 1 <- 3 4,  inlet 0 <- 1 2   =>  1 2

In ``.pack`` each element holds one item: the ``4`` is dropped, and ``1 2``
at the leftmost inlet fills both elements.

Nothing is converted: a piece keeps whatever it was sent. Every inlet starts
out holding ``0``, so a bang before anything has arrived sends ``0 0`` from a
``.join 2``.

**Which inlets send**

By default only the leftmost inlet sends the list. Arguments after the inlet
count name the inlets that send, counted from 0; ``-1`` makes every inlet
send. ``.join 3 2`` sends from inlet 2 only, ``.join 3 0 2`` from inlets 0 and
2. A bang sends the list from any inlet, whether or not that inlet sends when
written.

**Messages**

Every inlet takes these:

- an int, float or list: store it as this inlet's piece, replacing the old
  one, and send if this inlet sends.
- bang: send the list without storing anything.
- ``set <message>``: store without sending.
- ``set`` followed by a space and nothing else: empty this inlet's piece, so
  it adds nothing to the list. When every piece is empty, nothing is sent.

A message that would make the whole list longer than 256 items or 1024
characters is refused, and every inlet keeps what it had (see *List length
limits* in :doc:`/patcher/messages`).

.. patcher-object:: .listfunnel

Each item leaves as its own list: the index, then the item.

.. code-block:: text

   [.listfunnel]     a b c   =>  0 a,  1 b,  2 c
   [.listfunnel 1]   60 64   =>  1 60,  2 64

The pairs leave first to last, each fully handled downstream before the
next. The index is always an int, and the item keeps its exact spelling.
Unlike ``.iter``, the output is always a list, so ``.route``, ``.sel`` or
``.spray`` can act on the index. A ``.listfunnel`` into a ``.spray`` with the
same offset sends item k to outlet k, whatever the length of the list.

``offset <n>`` sets the index of the first item for the lists that follow.
It is not saved with the patch; the creation argument is. A bare ``offset`` is
ignored. Lists longer than 256 items lose their tail, as described under
*List length limits* in :doc:`/patcher/messages`.

.. patcher-object:: .pack

Arguments set both the number of inlets and the type of each element, by how
they are spelled:

- a whole number (``0``, ``60``): an int element. A float sent here is
  truncated.
- a number with a decimal point or exponent (``0.``, ``0.5``, ``1e3``): a
  float element. An int sent here becomes a float.
- anything else (``foo``): a symbol element, which stores whatever it is sent
  unchanged.

A word sent to an int or float element is refused: the element keeps its
value. With no arguments the object has two int elements starting at 0.

.. code-block:: text

   [.pack 0 0. foo]   inlet 2 <- bar, inlet 1 <- 3, inlet 0 <- 1.7
                      =>  1 3. bar

**Messages**

- an int, float or symbol: store it in this inlet's element. At the leftmost
  inlet, also send the list.
- a list: spread it over this inlet's element and the ones to its right, one
  item each. Items past the last element are dropped.
- bang (leftmost inlet): send the list as it stands.
- ``set <message>``: store as above, without sending.

To build a list, send the right-hand values first and the leftmost one last
(``.trigger`` makes this order explicit; see :doc:`/patcher/messages`).

A list may hold up to 256 elements and 1024 characters. Arguments that do not
fit are cut off and a warning is logged. A message that would make the list
too long is refused, and the elements keep their values.

.. patcher-object:: .pak

Works like ``.pack`` (element types, ``set``, spreading of lists), except
that every inlet sends the list when it receives a value, and every inlet
takes a bang. Use it when each change should go out at once, for example x, y
and z of a position arriving on separate cords. Feeding three values into a
``.pak 0 0 0`` sends three lists; use ``.pack`` to send one list after all
three have arrived.

.. patcher-object:: .prepend

The stored message goes in front of every message that arrives, and the
result leaves as one list:

.. code-block:: text

   [.prepend note]   60 100   =>  note 60 100
   [.prepend set]    0.5      =>  set 0.5

**Messages**

- left inlet, an int, float or list: send it with the stored message in
  front.
- left inlet, bang: send the stored message on its own.
- right inlet, an int, float or list: replace the stored message, without
  sending anything. The whole list is stored. An empty list clears it.
  The right inlet does not take a bang.

With no stored message, the object passes everything through unchanged, in
the type it arrived as. The stored message holds at most 256 characters; a
longer one is refused and the old one kept.

.. patcher-object:: .substitute

.. code-block:: text

   [.substitute 60 72]        60 64 60     =>  left:  72 64 72
   [.substitute 60 72 1]      60 64 60     =>  left:  72 64 60
   [.substitute 60 72]        62 64        =>  right: 62 64

Exactly one outlet sends for each message: the left outlet when something was
replaced, the right outlet when nothing matched. The right outlet sends the
message unchanged, so ``.substitute`` objects can be chained, each one's right
outlet into the next one's left inlet.

**Matching**

- A match that reads as a number matches items with the same value, so
  ``60`` matches both ``60`` and ``60.``. Any other match is compared as text.
  A number never matches a word.
- A bang matches the word ``bang``.
- The comparison is exact: a computed float that only looks equal may not
  match. Put a ``.round`` in front if needed.
- Every item of a list is checked, not just the first. With a third argument
  (any value), only the first match is replaced.

When an int, float or bang matches, the replacement leaves alone, as the
int, float or word it spells. In a list, the replacement is written as it is
spelled. Without a replacement argument, matches become ``0``. Without a
match argument, nothing matches and every message leaves the right outlet.

The match and the replacement are single words of at most 64 characters.
Inlets 1 and 2 change them; a longer word is refused.

.. patcher-object:: .unjoin

The first argument is the number of group outlets, the second the number of
items per group. There is one more outlet on the right for the remainder.

.. code-block:: text

   [.unjoin 2 2]   1 2 3 4 5   =>  rest: 5,  out1: 3 4,  out0: 1 2
   [.unjoin 2 2]   1 2 3       =>  rest: 3,  out0: 1 2
   [.unjoin 2]     1 2 3 4     =>  rest: 3 4,  out1: 2,  out0: 1

A group outlet only sends a full group. The items that do not make a full
group, and any items after the last group outlet, leave the rightmost outlet
together, so nothing is dropped. The outlets send right to left, the remainder
first. Use ``.unpack`` when each item needs its own type, and ``.unjoin`` when
the list's length is not known in advance.

There is no bang: the object keeps nothing between messages. Put a ``.l`` in
front to send a list again. Lists longer than 256 items lose their tail, as
described under *List length limits* in :doc:`/patcher/messages`.

.. patcher-object:: .unpack

The outlets send right to left (see :doc:`/patcher/messages`), so a patch can
wire the right outlets into cold inlets and the leftmost outlet into a hot
one.

Arguments set each outlet's type by how they are spelled, as for ``.pack``:
a whole number makes an int outlet, a number with a decimal point or exponent
a float outlet, and anything else an outlet that sends items unchanged.

- A float into an int outlet is truncated; an int into a float outlet becomes
  a float.
- A word into an int or float outlet is refused: that outlet sends the value
  it held before.
- Only the outlets the list reaches send. ``1 2`` into ``.unpack 0 0 0`` sends
  outlets 1 and 0, and outlet 2 stays silent. Items past the last outlet are
  dropped.
- A bang sends every outlet's last value. An outlet no list has reached sends
  its creation argument.

.. code-block:: text

   [.unpack 0 0. s]   1.7 2 foo   =>  out2: foo,  out1: 2.,  out0: 1

There is no ``set`` message. A list that would not fit in 256 items and 1024
characters is refused, and nothing is sent.

.. patcher-object:: .zl

The mode word selects what the object does: ``.zl rev``, ``.zl nth 2``,
``.zl sort -1``. Anything after the mode word is the mode's argument, which
the right inlet replaces later. A whole number before the mode word sets the
maximum list length (``.zl 16 rev``).

A list at the left inlet is stored and processed straight away. A bang
processes the stored list again, so after ``mode <name>`` a bang gives the
same list under the new mode. Except in the four accumulating modes, the
stored list is not changed by processing: two bangs in ``scramble`` mode give
two shuffles of the same list.

Positions and indices count from 1 in every mode except ``mth``, so what one
mode reports can be fed to another: ``sub`` and ``filter`` report positions
that ``nth`` takes. When a mode uses both outlets, the right outlet sends
first.

**Reading the list**

- ``len``: the number of items. An empty list gives 0.
- ``nth <i>``: the item at position i out the left outlet, and the list
  without it out the right outlet. A position outside the list sends nothing.
- ``mth <i>``: as ``nth``, but counting from 0.
- ``sum``: the numbers in the list added up. Words are skipped; a list with
  no numbers gives 0.
- ``median``: the middle number once sorted. With an even count, the mean of
  the two middle numbers. Words are skipped; a list with no numbers sends
  nothing.

``sum`` and ``median`` send an int when every number they used is an int and
the result is whole, and a float otherwise: the median of ``1 3`` is 2, of
``1 2`` it is 1.5.

**Reordering**

- ``rev``: the list reversed.
- ``rot <n>``: the list rotated n places toward the end (negative: toward
  the start). ``rot 1`` turns ``1 2 3`` into ``3 1 2``.
- ``scramble``: the list in random order. ``zlseed <n>`` with a non-zero n
  makes the shuffles repeat each run; 0 makes them unpredictable.
- ``sort``: the list sorted, upward, or downward when the argument is
  negative. Numbers come before words in both directions, and equal items
  keep their order. The right outlet sends, first, each sorted item's
  original position: send it to a second ``.zl indexmap`` to put a parallel
  list in the same order.
- ``swap <i> <j>``: the list with the items at positions i and j exchanged.
  If either position is outside the list, nothing is sent.
- ``indexmap <i...>``: the items at the positions the argument names, in that
  order. A position may be named twice or left out; positions outside the list
  are skipped.

**Cutting and searching**

- ``slice <n>``: the first n items out the left outlet, the rest out the
  right.
- ``ecils <n>``: the last n items out the right outlet, the rest out the
  left.
- ``sub <list>``: the position of every place the argument list occurs,
  overlapping ones included, out the left outlet; the number of places out
  the right outlet, also when it is 0. Without an argument nothing is sent.
- ``lookup <table>``: each item of the incoming list is a position in the
  argument list, and the items found there are sent. Positions outside the
  table, and words, are skipped.

**Comparing and sets**

The argument is the second list.

- ``thin``: the list with repeated items removed. Each item stays at its
  first place.
- ``sect``: the items that are in both lists, each once, in the order of the
  incoming list. When there are none, the right outlet sends a bang.
- ``union``: the incoming list with repeats removed, followed by the
  argument's items that are not in it.
- ``unique`` and ``filter``: the incoming list with every item that is in the
  argument removed. Repeats and order are kept. ``filter`` also sends, first,
  the original positions of the items that remain.
- ``compare``: 1 out the left outlet when the two lists are equal, 0 when not.
  When they differ, the right outlet first sends the positions that differ.
  Lists of different lengths differ at every position past the shorter one.
- ``change``: the incoming list out the left outlet only when it differs from
  the previous one, and 1 or 0 out the right outlet every time. Each list
  becomes the one the next is compared with. A list sent to the right inlet
  sets that reference without sending anything.

Numbers compare by value, so ``1`` equals ``1.``; words compare as text.

**Combining**

- ``join <list>``: the incoming list followed by the argument.
- ``lace <list>``: the two lists interleaved, item by item. When one is
  longer, its remaining items follow at the end.
- ``delace``: the reverse of ``lace``: the 1st, 3rd, 5th... items out the left
  outlet, the 2nd, 4th... out the right.
- ``iter <n>``: the list sent as a series of lists of n items, the last one
  shorter when the list does not divide.
- ``reg``: the list is sent straight on and kept; a bang sends it again. A
  list at the right inlet replaces it without sending.

**Accumulating**

These four modes collect items across messages in one shared store. An
incoming list adds its items; a bang takes from the store.

- ``group <n>``: every time n items have been collected, they are sent as one
  list. A bang sends and clears what has been collected so far. Without n,
  items collect until n is set or a bang arrives.
- ``stream <n>``: the last n items received. Once n have arrived, every new
  list sends the latest n. The right outlet sends how many items are still
  missing, 0 when the window is full. A bang sends the count, and the window when it is full, again. Without
  n, nothing is collected.
- ``queue``: an incoming list is stored and nothing is sent. Each bang sends
  the oldest item and removes it.
- ``stack``: as ``queue``, but each bang sends the newest item.

When the store is empty, a bang in ``queue`` or ``stack`` mode sends a bang
out the right outlet, so a patch can empty the store by banging until that
happens. The store keeps its contents when switching between these four
modes. ``zlclear`` empties it.

.. code-block:: text

   [.zl group 3]   1 2, 3 4, 5 6 7   =>  1 2 3,  4 5 6   (7 is kept)
   [.zl queue]     60 64 67, bang, bang   =>  60,  64

**Messages at the left inlet**

- ``mode <name>``: switch modes. The stored list is kept and nothing is sent.
  An unknown name leaves the mode unchanged.
- ``zlclear``: empty the stored list and the accumulating store. The mode and
  argument stay.
- ``zlmaxsize <n>``: set the maximum list length, 1-256. It applies to the
  next list; the stored list is not shortened.
- ``zlseed <n>``: set the random sequence for ``scramble``.

A list that starts with one of these words is read as that command, not as
data.

**The right inlet**

The right inlet sets the mode's argument and never sends anything. An int, a
float or a list all work; a float is truncated to an int, also when the mode
reads a list. Modes that take a number or a position use the first number
in the list (``swap`` the first two, ``indexmap`` all of them); a list with
no numbers leaves that number unchanged. Modes that take a list use the whole
list. Changing the mode keeps the argument.

**Limits**

Lists hold at most 256 items and 1024 characters, or fewer after
``zlmaxsize``. Items that do not fit are refused and the list keeps the
items before them (see *List length limits* in :doc:`/patcher/messages`).
The accumulating store has the same limit.

Without a mode word the object stores lists and sends nothing; an unknown
mode word in the arguments is logged. Changing the creation arguments
empties the stored list. Separate objects per mode (``.zl.rev`` and so on)
do not exist: use ``.zl rev``.
