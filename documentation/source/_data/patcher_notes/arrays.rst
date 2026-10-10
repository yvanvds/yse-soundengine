.. Notes for the Arrays category. Format: _data/patcher_notes/README.md.

.. patcher-object:: .array

Each element is a single value: an int, a float or a word. ``append 0 4 7``
adds three elements, and ``getvalue`` sends them back as the list ``0 4 7``.
An element keeps the spelling it arrived with, so ``7`` and ``7.`` are
different elements. On the way out, ``7`` leaves as an int, ``7.`` as a float
and anything that is not a number as a symbol.

**Messages**

- ``append <value...>``: add elements at the end.
- ``insert <index> <value...>``: add elements before position ``index``, in
  the order given. Inserting at the length appends.
- ``set <index> <value>``: replace one element.
- ``get <index>``: send one element out of outlet 0, or bang outlet 2 when
  there is no element at ``index``.
- ``delete <index>``: remove one element and close the gap.
- ``clear``: remove every element.
- ``getsize``: send the number of elements out of outlet 0.
- ``getvalue``: send the whole array out of outlet 0. One element leaves as
  a single value, several as a list.
- bang: send the reference ``array <name>`` out of outlet 1.

Positions start at 0. A position outside the array, and any negative
position, is refused: nothing is wrapped or clamped. Use ``.array.wrap`` or
``.array.rotate`` when you want wrapping.

**Sharing**

Every ``.array`` with the same name in a patcher uses the same contents, and
two patchers with the same name share theirs. An ``.array`` without a name
keeps a private array and sends no reference. The contents are saved with
the patch. See :ref:`data-sharing` and :ref:`data-saved`.

**The other .array objects**

The other objects in this category each work on a named array. They take
the array's name as their first argument, and it cannot be changed while
the patch runs. They share these rules:

- A bang on the left inlet runs the object. So does the reference
  ``array <name>``, so outlet 1 of an ``.array`` can be wired straight into
  them. A reference that names a different array is refused (see
  :ref:`data-references`).
- The rightmost inlet accepts the array's reference and does nothing with
  it, so the wiring can show which array an object works on.
- Objects that change the array send its reference after each change, so
  they can be chained: ``.array.push`` into ``.array.length`` reports the new
  length after every push.
- A result of one element leaves as a single value. When there is no result
  (an empty array, a missing position), a separate outlet bangs instead of
  sending a placeholder value.
- Each operation reads or changes the array in one step, so its result is
  the array as it was at that moment, even if another object changes it
  right after.
- An object without a name works on a private, empty array and sends no
  reference.

.. code-block:: text

   append 60 64 67  ->  [.array chord]
   bang             ->  [.array chord]         outlet 1: array chord
   array chord      ->  [.array.length chord]  ->  3

**Limits**

An array holds at most 256 elements of at most 64 characters each. A list
sent out of an outlet holds at most 256 items and 1024 characters. A message
that does not fit is refused: it changes nothing, sends nothing and is not
logged. ``append`` and ``insert`` are the exception: they add the elements
that fit and drop the rest. The same refusal happens when two threads use
one array at the same moment, for example a millisecond ``.metro`` and the
audio thread: the second one does nothing rather than wait. See
:ref:`data-limits`.

Safe on the audio thread: no message allocates memory, takes a lock or
waits.

.. patcher-object:: .array.at

An int moves the remembered position even when the fetch misses, so a bang
after the array has grown finds the element. A list of positions is a
one-off fetch: it does not change the remembered position, and it is
answered whole or not at all. Each ``.array.at`` keeps its own position, so
several of them can read one array independently.

Use ``.array.wrap`` when positions past the end should wrap around instead
of missing.

.. patcher-object:: .array.change

A change is any difference in length, or in the spelling of an element at
any position: ``7`` and ``7.`` differ, and ``10 20`` differs from ``20 10``.
The object starts out remembering an empty array, so the first poll of an
array that already holds elements reports a change.

Use the right inlet when the array was changed on purpose and that change
should not count: it takes the current contents as the new starting point
and sends nothing. Changing the object's arguments, or renaming the patcher,
also resets the remembered state to empty.

.. code-block:: text

   [.metro 100] -> [.array.change steps] -> reference -> [.array.iter steps]

The walk only runs when ``steps`` has actually changed.

.. patcher-object:: .array.compare

Elements are compared by spelling, so ``7`` and ``7.`` differ, and order
matters: ``10 20`` does not equal ``20 10``. Both arguments may name the same
array.

.. patcher-object:: .array.concat

The result is a list, not a new array. To store it, send it to
``.array.push`` on another array, which appends a whole list. If the joined
list is longer than a list can be, nothing is sent. Both arguments may name
the same array, which sends it twice in a row.

.. patcher-object:: .array.deserialize

The document uses the same JSON form a saved patch uses for an ``.array``.
Each value becomes one element:

- numbers keep their form: ``60`` becomes an int, ``62.5`` a float,
- strings become words; an empty string is skipped,
- ``true`` and ``false`` become ``1`` and ``0``,
- ``null``, nested arrays and objects are skipped,
- elements longer than 64 characters, and elements past the 256th, are
  dropped while the rest still loads.

Text that is not a JSON array (malformed text, a number, an object) changes
nothing: the array keeps its contents and no reference is sent.

The text is parsed on the engine's background pool, so the new contents
appear one audio block after the message. The object is safe to use from
the audio thread.

**Limits**

A document is at most 255 characters. At most 16 ``.array.deserialize``
objects can exist in the engine at the same time; a seventeenth logs an
error when it is created and refuses every document.

.. patcher-object:: .array.every

Words are skipped: the expression only sees elements that read as numbers.
An array without numbers passes, because nothing in it fails. The test stops
at the first element that fails.

.. code-block:: text

   [.array.every notes $i1 >= 60 && $i1 < 72]   are all notes between C4 and B4?

.. patcher-object:: .array.expr

Each numeric element is replaced in the output by the expression's result,
an int or a float following the expression's types (see ``.expr``). Words
pass through as they are, so the output has one item per element. If the
output is longer than a list can be, the items past the limit are dropped.

``.array.map`` performs the same calculation and writes the results back
into the array.

.. code-block:: text

   [.array.expr chord $i1 + 12]   chord 60 64 67  ->  72 76 79

.. patcher-object:: .array.fill

A fill replaces everything: the array becomes exactly ``count`` copies of the
value, so a smaller count shrinks the array and 0 empties it.

Because the other objects refuse positions past the end of an array, a fill
is the usual way to give an array its size before writing to it by
position:

.. code-block:: text

   [.array.fill steps 16 0]   bang  ->  steps holds 16 zeros
   set 3 1  ->  [.array steps]       steps now has a 1 at position 3

.. patcher-object:: .array.filter

An element stays when the expression gives a non-zero result for it. Words
are always removed, because the expression cannot see them. The elements
that stay keep their order and move down to close the gaps, so positions
change for every object that uses the array.

.. code-block:: text

   [.array.filter notes $i1 >= 60]   notes 48 60 55 72  ->  60 72

.. patcher-object:: .array.flatten

An element is a single value, so an array cannot contain other arrays. To
work with a group of arrays, give each one a name and list the names:
``.array.flatten verse chorus bridge`` sends the three arrays, one after the
other, as one list. A name may appear more than once.

The arrays are read one after the other, not all in the same instant. If the
combined list is longer than a list can be, nothing is sent.

.. patcher-object:: .array.foreach

Works like ``.array.iter`` with the expression applied on the way out: each
numeric element leaves as the expression's result, an int or a float
following the expression's types (see ``.expr``). Words leave unchanged.

The object walks a copy of the array taken when the bang arrives. Changes to
the array during the walk, including changes made by the objects connected
to outlet 0, do not affect it. A bang that arrives during a walk, for example
through a cord from an outlet back to the inlet, is refused.

.. patcher-object:: .array.group

Elements are equal when they are spelled the same, so ``7`` and ``7.`` form
separate groups. The length of each group is how often its value occurs.

.. code-block:: text

   notes 60 64 60 67 64 60  ->  60 60 60,  then 64 64,  then 67,  then done

The groups come from a copy of the array taken when the bang arrives. If any
group would be longer than a list can be, nothing is sent. A bang that
arrives while the groups are being sent is refused.

.. patcher-object:: .array.index

A value matches an element with exactly the same spelling: an int ``7`` finds
``7`` but not ``7.``, and a float ``7.`` finds ``7.`` but not ``7``. Words
match case-sensitively. Only the first match is reported.

``.array.indexof`` does the same search but reports a miss as -1 instead of
on a separate outlet.

.. patcher-object:: .array.indexof

A value matches an element with exactly the same spelling: an int ``7`` finds
``7`` but not ``7.``, and a float ``7.`` finds ``7.`` but not ``7``. Words
match case-sensitively. Only the first match is reported.

``.array.index`` does the same search but bangs a separate outlet on a miss,
which is easier to route on.

.. patcher-object:: .array.indexmap

Each entry in the map picks one element of the array as it was before the
reorder. An entry may appear more than once, which copies the element, and
positions that are left out drop their element. A position past the end of
the array picks nothing, so the result can be shorter than the map.

.. code-block:: text

   notes 60 62 64 67,  map 3 2 1 0  ->  67 64 62 60
   notes 60 62 64 67,  map 0 0 2    ->  60 60 64

``.array.sort``, ``.array.scramble`` and ``.array.shuffle`` send the order
they applied in this form. Send that order to the left inlet of an
``.array.indexmap`` on a second array, and the second array gets the same
new order.

.. patcher-object:: .array.insert

A list is inserted in the order sent: ``a b c`` inserted at position 1 leaves
``a b c`` starting at position 1. The insert is all or nothing: if the
position is past the end, or the array cannot take the whole list, nothing
is inserted.

The array's reference is refused on the left inlet, so a cord wired there
by mistake does not insert the words ``array <name>``. ``.array.remove`` is
the opposite operation.

.. patcher-object:: .array.iter

Each element's message is fully handled by everything connected to outlet 0
before the next one leaves. The object walks a copy of the array taken when
the bang arrives, so changes to the array during the walk, including changes
made by the objects connected to outlet 0, do not affect it: every element
the array held at that moment is sent exactly once.

A bang that arrives during a walk, for example through a cord from an outlet
back to the inlet, is refused.

.. patcher-object:: .array.join

The result leaves as the value it spells: ``c4 e4 g4`` joined with ``-``
gives the symbol ``c4-e4-g4``, and ``1 2`` joined without a separator gives
the int ``12``.

The joined word may be up to 1280 characters long, which is more than an
array element can hold, so storing it back into an array can be refused. A
longer result is not sent.

``.array.tosymbol`` joins without a separator and always sends a symbol;
``.array.tostring`` separates the elements with spaces.

.. patcher-object:: .array.map

Each numeric element is replaced by the expression's result, an int or a
float following the expression's types (see ``.expr``). Words stay where they
are, so the length and the positions do not change. The whole array is
rewritten in one step; no other object sees it half done.

``.array.expr`` performs the same calculation and sends the results without
changing the array.

.. code-block:: text

   [.array.map chord $i1 + 12]   chord 60 64 67  ->  72 76 79

.. patcher-object:: .array.push

Pair it with ``.array.pop`` for a stack (last in, first out) or with
``.array.shift`` for a queue (first in, first out).

The array's reference is refused on the left inlet, so a cord wired there by
mistake does not append the words ``array <name>``.

.. patcher-object:: .array.random

Every pick is independent, so the same element can come up twice in a row.
Use ``.urn`` to draw without repeats.

A bang on an empty array does not use up a random number, so a seeded
sequence of picks is not shifted by it.

.. patcher-object:: .array.reduce

The running result starts as the first numeric element. The expression then
runs once for each following numeric element, and its result becomes the new
running result. Words are skipped.

- ``$f1 + $f2``: the sum,
- ``$f1 * $f2``: the product,
- ``max($f1, $f2)``: the largest value.

With a single numeric element, that element is the answer and the
expression does not run.

.. patcher-object:: .array.regexp

Each element is matched as text, and the pattern may match anywhere in it:
``^c`` keeps elements that start with ``c``, and ``^[0-9]+$`` keeps whole
numbers. Anchor the pattern with ``^`` and ``$`` to match the whole element.

The result is sent whole or not at all: when the matching elements do not
fit in one list, or the matching takes too long, nothing is sent.

For capture groups or substitutions per element, feed ``.array.iter`` into
``.regexp``.

.. patcher-object:: .array.remove

An int moves the remembered position even when nothing is removed. Only one
position can be removed per message: after each removal the later elements
move down, so a list of positions would no longer point at the elements it
meant.

``.array.insert`` is the opposite operation. ``.array``'s own ``delete``
removes an element without sending it.

.. patcher-object:: .array.replace

Matches are exact: ``7`` replaces elements spelled ``7`` but not ``7.``, and
words match case-sensitively. Positions do not change; only the matching
elements get the new value.

A replacement value must be set, as the third argument or on the middle
inlet, before anything can be replaced. To replace only the first occurrence,
find it with ``.array.indexof`` and send ``set <index> <value>`` to the
``.array``.

.. patcher-object:: .array.rotate

Rotating ``1 2 3 4`` by 1 gives ``4 1 2 3``; by -1 it gives ``2 3 4 1``. Any
amount works: it is taken modulo the length, so rotating by the length leaves
the array as it was.

.. patcher-object:: .array.routepass

The values after the name each get an outlet, in order, and one more outlet
on the right catches the arrays that hold none of them. A trigger checks the
values from left to right and sends the array's reference, not its
contents, out of the outlet of the first value the array holds anywhere.
Exactly one outlet fires.

.. code-block:: text

   [.array.routepass chord 60 61 62]
   chord 64 61 67  ->  outlet 1 (61)
   chord 64 67     ->  outlet 3 (none of the values)

A value matches an element with exactly the same spelling, so ``5`` does not
match ``5.0``. A value that appears twice in the arguments only uses its
first outlet. An empty array always goes to the rightmost outlet, so a chain
of ``.array.routepass`` objects can test one array against more values.

A trigger that arrives while a reference is being sent, for example through
a cord from an outlet back to the inlet, is refused.

.. patcher-object:: .array.scramble

The order outlet sends, for each new position, the position the element came
from. Send it to the left inlet of an ``.array.indexmap`` on a second array,
and that array is shuffled the same way:

.. code-block:: text

   [.array.scramble notes]  order outlet  ->  [.array.indexmap velocities]

``.array.shuffle`` is the same object under another name.

.. patcher-object:: .array.sect

Elements are compared by spelling, so ``7`` and ``7.`` are different
elements. A value that repeats in the left array appears once in the result.

.. code-block:: text

   [.array.sect left right]   left 60 64 67,  right 67 71 74  ->  67

.. patcher-object:: .array.shuffle

The order outlet sends, for each new position, the position the element came
from. Send it to the left inlet of an ``.array.indexmap`` on a second array,
and that array is shuffled the same way:

.. code-block:: text

   [.array.shuffle notes]  order outlet  ->  [.array.indexmap velocities]

``.array.scramble`` is the same object under another name.

.. patcher-object:: .array.slice

The range runs from ``start`` up to, but not including, ``end``. An end of 0,
or one past the end of the array, means "to the end". When ``end`` is at or
before ``start``, the range is empty.

.. code-block:: text

   notes 60 62 64 67
   [.array.slice notes 1 3]  ->  62 64
   [.array.slice notes 2]    ->  64 67

``.array.subarray`` includes the end position and can read backwards.

.. patcher-object:: .array.some

Words are skipped: the expression only sees elements that read as numbers.
An array without numbers gives 0. The test stops at the first element that
passes.

.. code-block:: text

   [.array.some notes $i1 < 48]   is any note below C3?

.. patcher-object:: .array.sort

Numbers come before words in both directions. Numbers are compared by value,
so ``7`` and ``7.`` count as equal; words are compared character by
character, with the shorter first when one starts the other. Equal elements
keep their order.

The order outlet sends, for each new position, the position the element came
from. Send it to the left inlet of an ``.array.indexmap`` on a second array,
and that array is put in the same order. This keeps, for example, an array
of velocities matched to an array of notes after the notes are sorted.

.. code-block:: text

   [.array.sort notes]  order outlet  ->  [.array.indexmap velocities]

.. patcher-object:: .array.split

The position is the number of elements in the left half: 0 puts everything
in the right half, and the length (or more) puts everything in the left
half. An empty half sends nothing, which a patch that keeps splitting off the
first element can use to stop.

.. code-block:: text

   [.array.split notes 1]   notes 60 62 64  ->  right: 62 64,  then left: 60

If either half is longer than a list can be, nothing is sent.

.. patcher-object:: .array.stream

Each value is appended to the array. Once the array holds ``size`` values,
every new value pushes the oldest one off the front, so the array always
holds the latest ``size`` values. A list counts as one value per item.

After every message the right outlet reports how many values are still
missing, and the reference leaves only when the window is full. Wire the
reference to a statistics object to analyse each full window:

.. code-block:: text

   [.array.stream recent 8]  reference  ->  [.array.mean recent]

A bang reports the current state without adding a value. A smaller size
takes effect at the next message, which drops the extra elements.

.. patcher-object:: .array.sub

The range runs from ``start`` to ``end``, both included. A missing end, or
one past the last element, means "to the end". When ``end`` is before
``start``, the elements are sent from ``start`` down to ``end``.

.. code-block:: text

   notes 60 62 64 67
   [.array.sub notes 1 2]  ->  62 64
   [.array.sub notes 3 1]  ->  67 64 62

``.array.subarray`` is the same object under another name. ``.array.slice``
leaves out the end position.

.. patcher-object:: .array.subarray

The range runs from ``start`` to ``end``, both included. A missing end, or
one past the last element, means "to the end". When ``end`` is before
``start``, the elements are sent from ``start`` down to ``end``.

.. code-block:: text

   notes 60 62 64 67
   [.array.subarray notes 1 2]  ->  62 64
   [.array.subarray notes 3 1]  ->  67 64 62

``.array.sub`` is the same object under another name. ``.array.slice``
leaves out the end position.

.. patcher-object:: .array.thin

Each element is compared with the last element that was kept. It is removed
when it is spelled the same, or, with a tolerance above 0, when both are
numbers no further apart than the tolerance. Because the comparison is with
the last kept element, a slow drift still shows:

.. code-block:: text

   [.array.thin values 0.5]   values 0 0.4 0.8 1.2  ->  0 0.8

Words are only removed when they repeat exactly. A value that comes back
later, not next to its earlier copy, is kept; use ``.array.unique`` to
remove every repeat.

.. patcher-object:: .array.tuplewise

For each position up to the length of the shorter array, the expression runs
with the left and right elements. Its result is an int or a float, following
the expression's types (see ``.expr``). Where one of the two elements is a
word, the left element is sent unchanged in that position.

.. code-block:: text

   [.array.tuplewise a b $i1 + $i2]   a 1 2 3,  b 10 20  ->  11 22

If the result is longer than a list can be, nothing is sent. Both arguments
may name the same array.

.. patcher-object:: .array.union

Elements are compared by spelling, so ``7`` and ``7.`` are different
elements. Repeats within either array appear once in the result.

.. code-block:: text

   [.array.union left right]   left 60 64 60,  right 64 67  ->  60 64 67

.. patcher-object:: .array.unique

Elements are compared by spelling, so ``7`` and ``7.`` are both kept. To
remove only repeats that sit next to each other, or values that are close
together, use ``.array.thin``.

.. patcher-object:: .array.wrap

Every position is taken modulo the array's length: with three elements,
position 3 reads element 0, position 5 reads element 2, and -1 reads the
last element. Fed from a counter, ``.array.wrap`` loops over the array
forever, like a step sequencer.

Use ``.array.at`` when a position past the end should be reported as a miss.
