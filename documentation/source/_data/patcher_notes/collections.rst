.. Notes for the Collections category. Format: _data/patcher_notes/README.md.

.. patcher-object:: .bag

The left inlet takes the numbers, the right inlet says what to do with them:
while the flag is non-zero, numbers are added; while it is 0, they are
removed. The flag starts at 0, so set it before sending numbers. A float is truncated to an int on either inlet.

Adding and removing send nothing. A typical use is tracking held notes: add
the pitch on note-on, remove it on note-off, and bang to get the notes still
down.

**Messages**

- an int or float: add it or remove it, depending on the flag.
- ``<value> <flag>``: set the flag, then apply the value. ``60 1`` adds 60,
  ``60 0`` removes it. The flag stays set for later numbers.
- bang: send every stored number, newest first.
- ``cut``: send the oldest number and remove it.
- ``length``: send how many numbers are stored.
- ``clear``: remove everything.
- ``send <name>``: send what a bang would, but to every ``.r <name>`` (and the
  bus) instead of the outlet. It is a one-off: a later bang uses the outlet
  again. A bare ``send`` does nothing.

Without an argument each number is stored at most once, and adding one that
is already there changes nothing. With any argument (``.bag dup``) the same
number can be stored several times; a remove then takes the most recently
added copy.

**Limits**

At most 256 numbers; further adds are ignored. A ``send`` name is at most 63
characters. The contents are not saved with the patch (see
:ref:`data-saved`).

.. patcher-object:: .capture

Every value that arrives is recorded. A list is recorded as separate items,
one per element, and each item keeps its type: an int stays an int, a float a
float, a word a word. Recording sends nothing.

When the record is full, each new item pushes out the oldest one, so it
always holds the most recent values. That makes it a debugging tool: run the
patch, then ``dump`` to see what went past.

**Messages**

- any value or list: record it.
- ``dump``: send every recorded item out of the left outlet, oldest first, one
  at a time. A word leaves as a one-word list.
- ``count``: send how many items have arrived since the last ``count`` out of
  the right outlet, and restart the count. ``count 1`` sends it without
  restarting. This counts arrivals, including items already pushed out, so it
  can be larger than the number of items held.
- ``clear``: erase the record. The ``count`` total is not reset.
- ``open``, ``wclose`` and ``write`` are accepted and do nothing: there is no
  editing window, and the record cannot be written to a file. Use
  ``.textfile`` for that.

A bang does nothing.

**Limits**

The argument sets the size, 1-512 items (default 512). A word longer than 64
characters is not recorded. The record is not saved with the patch.

.. patcher-object:: .coll

A collection of entries. Each entry is a message stored at an **address**,
which is a number or a word: the address ``1`` and the address ``one`` are
different entries. Numeric addresses don't have to be consecutive.

Entries are kept in the order they were stored, not sorted by address.
``dump``, ``next`` and ``prev`` walk that order; use ``sort`` to change it.

.. code-block:: text

   1 60 64 67          store "60 64 67" at address 1
   store intro 0 0 0   store "0 0 0" at address intro
   1                   send "60 64 67"
   intro               send "0 0 0"

**Storing and recalling**

- ``<number> <message>``: store the message at that numeric address,
  replacing what was there.
- ``store <address> <message>``: the same, for a number or a word address.
- an int or float: send the entry at that address (a float is truncated).
- a word on its own: send the entry at that word address. Anything after the
  word is ignored.
- ``insert <number> <message>``: store at that address, first moving every
  numeric address at or above it up by one.
- ``append <message>``: store at one past the highest numeric address (0 when
  there is none).
- ``merge <address> <data>``: add the data to the end of the entry's message,
  or create the entry.
- ``remove <address>``: delete the entry.
- ``delete <address>``: delete the entry and move every higher numeric
  address down by one.
- ``clear``: delete every entry.

An entry's message leaves the left outlet as an int or a float when it is one
number, and as a list otherwise (a single word is a one-word list). A plain
recall sends only the message; nothing is sent for an address that holds
nothing.

**Walking the collection**

Each ``.coll`` has a pointer into the stored order.

- bang: send the entry at the pointer.
- ``next`` / ``prev``: send the entry at the pointer, then move the pointer
  one forward or back. It wraps around at either end.
- ``goto <address>``: put the pointer on that entry, without sending.
- ``start`` / ``end``: put the pointer on the first or last entry.
- ``dump``: send every entry in order, then bang outlet 2.

These, and ``sub``, also send the entry's address out of outlet 1, just
before its message.

**Working inside entries**

Element positions count from 1.

- ``nth <address> <position>``: send one element of an entry.
- ``sub <address> <position> <data>``: replace one element, then send the
  entry. ``nsub`` does the same without sending.
- ``min [position]`` / ``max [position]``: send the lowest or highest number
  found at that position across all entries (default 1). Entries without a
  number there are skipped.
- ``length``: send the number of entries.

**Reordering and renumbering**

- ``sort [order] [position]``: reorder the entries. ``order`` -1 (the
  default) is ascending, 1 descending. The key is the element at
  ``position`` (default the first), or the address when ``position`` is -1.
  Numbers sort before words, and entries without that element come first.
  Equal keys keep their order.
- ``swap <address> <address>``: exchange the addresses of two entries; the
  messages stay where they are.
- ``renumber [start]``: give the numeric entries consecutive addresses from
  ``start`` (default 0), in stored order. Word addresses are not touched.
- ``renumber2 [from]``: move every numeric address at or above ``from``
  (default 0) up by one.
- ``separate <number>``: the same as ``renumber2``, but the address is
  required. It opens a free address at ``number``.

**Word aliases**

A numeric entry can have a second address, a word. From then on the word
reaches that entry in every message: recall, ``store``, ``remove``, ``nth``
and so on. The outlet still reports the number.

- ``assoc <word> <number>``: give the existing entry at ``number`` the alias
  ``word``. If another entry already answered to that word, it is removed.
- ``deassoc <word> <number>``: take the alias away again. Both must match.
- ``nstore <number> <word> <message>``: store at the number and give it the
  alias, in one message. The word may also come first.
- ``subsym <new> <old>``: rename a word address or alias. Refused when
  ``new`` is already in use.

An entry has at most one alias, and the alias moves with the entry through
``insert``, ``delete``, ``sort``, ``swap`` and renumbering.

**Files**

``read <file>`` replaces the collection with the contents of a text file,
then bangs outlet 3. ``write <file>`` saves the collection. ``readagain`` and
``writeagain``, or a bare ``read`` or ``write``, reuse the last file name.
``filetype`` is accepted and does nothing. The file format, and how reading
and writing work, are described in :doc:`/patcher/files`.

**Sharing and saving**

The argument is a name. Every ``.coll`` with the same name uses the same
collection, and each keeps its own pointer. Without a name the collection is
private. A second argument is accepted and ignored. The contents are saved
with the patch. See :ref:`data-sharing` and :ref:`data-saved`.

**Limits**

At most 256 entries. A message is at most 256 characters and an address at
most 64. A store that does not fit is ignored. There is no editing window, and
a ``.coll`` cannot be pointed at another name while the patch runs.

.. patcher-object:: .funbuff

A function stored as points: (x, y) pairs of ints, kept sorted by x, with at
most one y per x. Use it for breakpoint curves, zone maps and timed event
lists.

The left inlet both stores and looks up. A number on the right inlet waits
for the next x: that x stores the pair. An x with nothing waiting is a
lookup. The waiting y is used once.

.. code-block:: text

   [.funbuff]   right <- 100, left <- 60   store (60, 100)
                60 100                     the same, as one list
                left <- 64                 send 100 (nearest lower x is 60)

A lookup sends the y stored at x. When there is no pair at x, it sends the y
of the nearest lower x, so the function is a staircase. An x below every
stored x sends nothing. Floats are truncated on both inlets.

**Messages**

- ``set <x> <y> [<x> <y> ...]``: store one or more pairs.
- ``delete <x>``: remove the pair at x. ``delete <x> <y>`` removes it only if
  its y matches.
- ``clear``: remove every pair.
- ``interp <x>``: send the y linearly interpolated between the pairs either
  side of x, as a float. ``x`` may be a float. Outside the stored range the
  first or last y is sent.
- ``min`` / ``max``: send the lowest or highest y.
- ``find <y>``: send, out of the left outlet, every x whose y equals ``y``.
- ``dump``: send every pair in x order: x out of the middle outlet, then y out
  of the left outlet.
- ``goto <x>``: move the pointer to x.
- ``next``: send the y at the pointer (or the next pair above it), with the
  distance from the previous x out of the middle outlet first, then move past
  it. When no pair is left, bang the right outlet instead. The pointer does
  not wrap; send ``goto`` to start again.
- ``embed 1``: save the pairs with the patch. ``embed 0`` (the default) does
  not.

The distance from ``next`` is what a sequencer needs: store event times as x,
and the middle outlet gives the wait before each event. After ``goto`` the
distance is measured from the ``goto`` position; on a fresh object, from 0.

**Limits**

At most 256 pairs; further pairs are ignored. The file-name argument is
accepted and ignored: ``.funbuff`` does not read or write files. Copy, paste
and the editing window are not supported.

.. patcher-object:: .table

An array of ints at addresses 0 to size-1, all set to 0 at the start. The
left inlet both reads and writes: a number on the right inlet waits for the
next address, which stores it. An address with nothing waiting sends the
value stored there. Floats are truncated on both inlets. An address outside
the table does nothing.

.. code-block:: text

   [.table 8]   right <- 50, left <- 3   store 50 at address 3
                3 50                     the same, as one list
                left <- 3                send 50

**Reading and writing**

- ``<address> <value>``: store directly.
- ``set <address> <value> [<value> ...]``: store values at successive
  addresses. Values past the end are dropped.
- ``const <value>``: set every address to the value.
- ``clear``: set every address to 0.
- ``load``: from now on, every number on the left inlet is stored at the next
  address, starting at 0, until the table is full. ``normal`` ends this.
- ``cancel``: drop a value waiting on the right inlet, so the next address
  reads instead of writing.
- ``getbits <address> <start> <count>``: send, as a number, the ``count``
  bits of one value from bit ``start`` downwards (bit 0 is the lowest).
  ``setbits <address> <start> <count> <value>`` writes those bits.

**Reading out**

- ``dump``: send every value, from address 0.
- ``goto <address>``: move the pointer (clamped into the table).
- ``next`` / ``prev``: send the value at the pointer, then move it one
  forward or back, wrapping at the ends.
- ``min``, ``max``, ``sum``, ``length``: send the lowest value, the highest,
  the total, or the size.
- ``inv <value>``: send the first address whose value is at least ``value``,
  or nothing.
- ``send <name> <address>``: send the value at that address to every
  ``.r <name>`` (and the bus) instead of the outlet.

**As a distribution**

The values can be read as weights. ``fquantile <f>``, with ``f`` from 0 to 1,
sends the first address where the running total of the values reaches that
fraction of the sum. ``quantile <n>`` is the same with ``n`` from 0 to 32768.
A bang does a ``quantile`` with a random number, so the table becomes a
weighted dice: an address with twice the value is picked twice as often.

**Arguments and saving**

A number sets the size, 1-4096 (default 128). A word is a name. It is
accepted so that named tables load, but tables with the same name do not
share their values. Changing the arguments sets every value back to 0.

The values are saved with the patch unless the table is sent ``embed 0``
(``flags 0`` does the same). See :ref:`data-saved`.

**Limits**

There is no editing window, the size cannot be changed by a message, and
tables do not read or write files.

.. patcher-object:: .textfile

Each message is added to the end of the current line, separated from the
previous one by a space. ``cr`` ends the line, so the next message starts a
new one. Numbers are written as text the way the rest of the patcher writes
them; a list is written as it arrived. Adding text sends nothing.

.. code-block:: text

   60 64 67
   cr
   intro
   0 0
   cr          =>   line 1: 60 64 67
                    line 2: intro 0 0

**Messages**

- any message: add it to the current line.
- ``cr``: end the current line. A ``cr`` when no line is open adds an empty
  line.
- ``tab``: add a tab character, with no space around it.
- ``symbol <word>``: add a word that would otherwise be read as one of these
  messages (``symbol clear`` adds the word ``clear``).
- ``dump``: send every line out of the left outlet, in order. A line that is
  one number leaves as an int or a float; any other line as a list, and an
  empty line as an empty list.
- ``line <n>``: send line ``n`` (counting from 1) preceded by ``set``, so it
  can set another object's contents. ``n`` below 1 means 1; a line that
  doesn't exist sends nothing.
- ``query``: send the number of lines out of the middle outlet.
- ``clear``: erase every line.

**Files**

``read <file>`` replaces the lines with those of a text file, then bangs the
right outlet. ``write <file>`` saves them, one line per line. A bare ``read``
or ``write`` reuses the last file name. A file name given as the argument is
read when the object is created, and is the default for a bare ``read`` or
``write``. See :doc:`/patcher/files`.

The lines themselves are not saved with the patch: the file is where they are
kept.

**Limits**

At most 256 lines of at most 256 characters. A message that would make the
line too long, or a new line past the 256th, is ignored. There is no editing
window: ``open``, ``wclose``, ``settitle``, ``filetype``, ``precision`` and
``stringout`` are accepted and do nothing.
