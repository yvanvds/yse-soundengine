.. Notes for the GUI controls category. Format: _data/patcher_notes/README.md.

.. patcher-object:: .b

Every bang, int or float sends a bang, whatever the number.

The GUI value is ``on`` once after a press and ``off`` otherwise. Reading it
clears the press, so poll it from one place only, or presses get lost. The
value cannot be written back, and ``.preset`` skips the button. For a
momentary control whose value can be saved, use ``.led momentary`` or
``.textbutton momentary``. See :doc:`/patcher/gui`.

.. patcher-object:: .dial

The inlet takes the knob position from 0 to 1, as ``.slider`` does, and the
outlet sends that position mapped onto the range:

.. code-block:: text

   out = minimum + (maximum - minimum) * position ^ exponent

.. code-block:: text

   [.dial 20 20000 3]   0.5  =>  2517.5

An exponent above 1 gives finer control at the bottom of the range, which
suits a frequency or a gain; one below 1 does the opposite. The range may run
from high to low. When ``minimum`` equals ``maximum`` the dial sends
``minimum``, and a result that is not a finite number sends 0. With
``intMode`` set to 1 the result is rounded to the nearest whole number (a
half rounds away from 0) and sent as an int.

With the default arguments (0, 1, exponent 1, float) the dial behaves exactly
like ``.slider``.

The GUI value is the position, not the mapped value, so a host that displays
"440 Hz" needs the arguments as well. Restoring the value works as described
under ``.slider``.

.. patcher-object:: .f

The right inlet stores a number without sending it; send a bang to the left
inlet to send it later. An int becomes a float. Restoring the value works as
described under ``.slider``.

.. patcher-object:: .function

A function holds up to ``points`` breakpoints. Each point has an x, a y and a
curve. The points are kept sorted by x, and there is one point per x.

- x is never below 0: a negative x is stored as 0.
- y is clamped to the range set by the arguments, 0 to 1 by default.
- The curve is clamped to -1 to 1.

**Messages**

- a number: the x to look up. The y at that x leaves the left outlet. Below
  the first point the first y is sent, past the last point the last y. An
  empty function sends nothing.
- bang: send the whole function from the middle outlet (see below).
- ``<x> <y>``: add a point with curve 0. A point already at that x is
  replaced.
- three numbers per point, ``<x> <y> <curve> ...``: replace every point.
- ``set <index> <x> <y> [<curve>]``: rewrite the point at that position,
  counted from 0 in x order. Without a curve it keeps its curve. The point
  moves to its new place in the order.
- ``setcurve <index> <curve>``: change one point's curve.
- ``nth <index>``: send that point's y from the left outlet.
- ``clear``: remove every point. ``clear <index>``: remove one.
- ``dump``: send every point from the right outlet as
  ``<index> <x> <y> <curve>``, in x order.

Edits send nothing, so drawing into a function that feeds a ``.line`` does
not restart the ramp. A list of any other length is ignored. When the
function is full, a new point is ignored.

**Curves**

A segment is shaped by the curve of the point at its right end; the first
point's curve is not used. Between two points, at a fraction ``t`` of the way
along, the y moves by ``t ^ ((1 + c) / (1 - c))`` of the distance. A curve of
0 is a straight line. A positive curve leaves the first value slowly and
moves late; a negative one moves early and arrives slowly. A curve of 1
holds the first value until the end of the segment, and -1 jumps at once.

**The ramp**

A bang sends the function as ``<y> <time>`` pairs, the form ``.line`` and
``.bline`` read. The first time is 0, so the ramp jumps to the first y; each
later time is the distance in x from the point before.

.. code-block:: text

   [.function]   0 0,  100 1,  300 0.5,  bang  =>  0. 0. 1. 100. 0.5 200.
                 50  =>  0.5

The ramp is made of straight segments: curves only shape the values sent for
a looked-up x.

**Saving**

The points are saved with the patch and come back when it loads (see
:ref:`gui-saved`). The GUI value has one cell per point, ``<x> <y> <curve>``;
an empty function reports the word ``clear``. Writing that value back to the
inlet restores the points and sends nothing.

A message that arrives while another thread is changing the points is
ignored, and a GUI read at that moment comes back empty (see
:doc:`/patcher/gui`).

.. patcher-object:: .i

The right inlet stores a number without sending it; send a bang to the left
inlet to send it later. A float is truncated toward 0, so 2.9 is stored as 2.
Restoring the value works as described under ``.slider``.

.. patcher-object:: .incdec

**Messages**

On the left inlet:

- bang: send the value without moving it.
- a number, or ``set <n>``: set the value without sending it. A float is
  truncated.
- ``inc`` / ``dec``: move one step up or down and send the result.

The middle inlet moves several steps at once: an int ``n`` moves ``n`` steps,
down when it is negative, and sends the result. A MIDI encoder that sends +1
and -1 can drive it directly. 0 moves nothing and just sends the value.

The right inlet sets the step size. A negative step makes ``inc`` go down, and
a step of 0 never moves the value.

**The range**

The value always stays between ``minimum`` and ``maximum``, given in either
order. With ``wrap`` 0 it stops at the ends. With ``wrap`` 1 it carries round
to the other end, and both ends are reachable, so one step past the maximum
is the minimum:

.. code-block:: text

   [.incdec 1 0 11 1]   at 11, inc  =>  0

Without arguments the range is the whole int range, and the value stops at
its ends instead of overflowing.

The value is kept inside the range even before anything moves it: an
``.incdec 1 60 72`` starts at 60. With ``wrap`` on, an untouched stepper
starts wherever 0 wraps to (65 for 60-72), so send ``set`` first when the
starting point matters.

The GUI value is the number. A one-number list or ``set 0 <value>`` sets it
without sending, so a restore is silent (see :doc:`/patcher/gui`).

.. patcher-object:: .kslider

The state is the keyboard: the velocity held on each of the 128 keys, with 0
for a key that is up.

**Messages**

On the left inlet:

- a pitch: press that key at the stored velocity.
- ``<pitch> <velocity>``: store the velocity, then play the pitch with it.
  Velocity 0 releases the key.
- ``set <pitch> <velocity>``: the same, but the stored velocity stays as it
  was.
- 128 numbers: set every key at once, and send a pair only for each key that
  changed.
- ``clear``: release every held key, sending velocity 0 for each.
- bang: send the pair for every held key, from low to high.

The right inlet stores the velocity for the pitches that follow. It sends
nothing.

A pitch outside 0-127 is ignored; a velocity is clamped to 0-127. A float is
truncated, and a list of any other length is ignored.

.. code-block:: text

   [.kslider]   60    =>  100, then 60
                60 0  =>  0, then 60

A single-key message sends even when the velocity has not changed, so playing
the same note twice sends two note-ons.

**Output**

The velocity is sent before the pitch, so the pair can be wired straight into
``.noteon``, ``.makenote`` or ``.flush``, which take the velocity on a cold
inlet. There is no mode that holds one note at a time.

The GUI value has 128 cells, one velocity per key. Held keys are not saved
with the patch; only the ``velocity`` argument is.

.. patcher-object:: .l

Unlike ``.m``, the box always sends its text as a list, even when it is a
single number or starts with a word.

.. patcher-object:: .led

``.led`` and ``.textbutton`` are the same object under two names. A host
draws ``.led`` as a lamp and ``.textbutton`` as a labelled button; they take
the same arguments and messages.

**Arguments**

An optional ``momentary`` keyword, then the label. The words of the label are
joined with single spaces, so ``.textbutton filter cutoff`` is one control
called "filter cutoff". A label cannot start with the word ``momentary``. The
label is fixed: changing the arguments replaces the object.

**Latching and momentary**

Without ``momentary`` the control latches: a number sets the state (0 off,
anything else on) and a bang flips it.

With ``momentary`` every number or bang is a press, whatever the number. The
state stays 1 until the host next reads the GUI value, which clears it. Poll
it from one place only.

In both modes, a one-number list or ``set 0 <value>`` sets the state
outright. In momentary mode that is the only way to clear a press without
reading it.

**Output**

Every message sends: the label from the right outlet first, then the state
from the left outlet. A control without a label sends an empty list. Wire a
bank of labelled controls into one ``.route`` and the label says which one
fired:

.. code-block:: text

   [.textbutton mute]   bang  =>  right: mute,  left: 1

A message starting with any other word changes nothing, and the state is
still sent. There is no message to change the label, and no way to set the
state without sending it.

The GUI value is ``0`` or ``1``. A host or ``.preset`` can write it back; in
momentary mode, restoring a value taken during a press replays the press
(see :doc:`/patcher/gui`).

.. patcher-object:: .m

The text is sent as the message it spells:

.. code-block:: text

   [.m 440]       =>  the int 440
   [.m 0.5]       =>  the float 0.5
   [.m 1 2 3]     =>  a list
   [.m stop]      =>  the command stop, to objects that take one

A list, or text starting with a word, that arrives on the inlet replaces the
stored text without sending it. ``.l`` always sends its text as a list.

.. patcher-object:: .matrixctrl

**Arguments**

``columns rows minimum maximum``: each axis 1-256, default 8, and a cell
range of 0 to 1 by default. A count outside 1-256 is clamped with a warning.
Changing the arguments replaces the object, and every cell starts at 0 again.

**Messages**

The inlet takes lists and bangs only:

- ``<column> <row> <value>``: set that cell.
- one number per cell, ``columns * rows`` of them: set the whole grid.
- ``set <index> <value>``: set one cell by index, where the index of
  ``(column, row)`` is ``column * rows + row``.
- ``clear``: set every cell to 0 (or to the bound nearest 0).
- ``getrow <row>`` / ``getcolumn <column>``: send that row's or column's
  values from the right outlet as one list.
- bang: send every cell.

A cell outside the grid, or a list of any other length, is ignored. Values
are clamped to the range. On a grid of exactly three cells, a three-number
list sets the whole grid; use ``set`` to address one cell.

**Output**

Every change is sent from the left outlet as ``<column> <row> <value>``: one
list for a one-cell change, and one per cell, column by column, for a whole
grid, a ``clear`` or a bang. That is the form the control inlets of
``.matrix`` and ``.router`` take, so the grid can drive either directly.

.. code-block:: text

   [.matrixctrl 4 4]   2 1 1     =>  2 1 1.
                       set 9 1   =>  2 1 1.

The GUI value has one cell per grid cell, in index order. There is no mode
that allows only one active cell per row or column.

.. patcher-object:: .multislider

**Messages**

- a list: set the cells from its numbers, and make the bank as long as the
  list (up to 1024 cells).
- a number: set every cell to it.
- ``set <index> <value>``: set one cell.
- ``fetch <index>``: send that cell's value from the right outlet.
- bang: send the bank again.

Every message sends the whole bank from the left outlet, after a ``fetch``
answer. An index outside the bank is ignored, and every value is clamped to
the range.

**Size**

The ``size`` argument sets the starting number of cells, 1 to 1024. A list
of another length changes it. Shrinking hides the cells at the end rather
than clearing them, so lengthening the bank again brings their values back.

Changing the range takes effect at once: an untouched
``.multislider 4 20 20000`` reads back as four 20s. The values and the live
size are not saved with the patch. To sum, sort or scale the bank, process
the list it sends with ``.zl``.

.. patcher-object:: .nodes

The field holds up to 64 nodes, each a circle with an x, a y and a radius. A
new ``.nodes 4`` has four nodes at 0 0 with radius 1.

**Messages**

- ``<x> <y>``: move the cursor and send.
- three or more numbers, ``<x> <y> <radius> ...``: set every node, and make
  the field as long as the number of complete triples. A partial triple at
  the end is ignored.
- ``set <index> <x> <y> [<radius>]``: move one node; without a radius it
  keeps its radius.
- bang: send again.

There is no int or float message: build the cursor pair with ``.pack 0. 0.``,
or wire in the right outlet of ``.xyslider``, which sends ``x y``.

**Output**

The right outlet sends the distance from the cursor to each node, then the
left outlet sends one weight per node. A node's closeness is 1 at its centre,
falls in a straight line to 0 at its edge, and is 0 outside it. Each weight
is that closeness divided by the sum over all nodes:

- inside at least one node the weights add up to 1, so a crossfade built on
  them keeps its level as the cursor moves;
- outside every node all weights are 0. There is no fallback to the nearest
  node.

A node with a radius of 0 or less never gets any weight, which mutes it
without deleting it.

.. code-block:: text

   [.nodes 2]   0 0 1  1 0 1,  then 0.5 0  =>  right: 0.5 0.5,  left: 0.5 0.5

The GUI value has one cell per node, ``<x> <y> <radius>``. Writing the whole
value back restores the number of nodes as well. Positions are not saved with
the patch; only the ``count`` argument is.

.. patcher-object:: .nslider

The state is a pitch and how it is spelled. MIDI pitch 61 can be written as
C sharp or as D flat; the accidental says which, so a host knows the sign to
draw and, from ``pitch - accidental``, the line or space to draw it on.

**Spelling**

A white key is always natural (0). A black key is spelled with the accidental
the last message chose, or else by the ``spelling`` argument: 0 or above for
a sharp, below 0 for a flat. Only the sign of an accidental counts.

**Messages**

- a number: a pitch, spelled the default way again.
- ``<pitch> <accidental>``: a pitch with its spelling.
- ``set 0 <pitch>``: move the note and keep the chosen spelling.
- ``set 1 <accidental>``: re-spell the note without moving it.
- bang: send again.

The pitch is clamped to ``minimum``-``maximum`` (given in either order,
within 0-127). Every message sends the accidental from the right outlet,
then the pitch from the left.

.. code-block:: text

   [.nslider 0 127 -1]   61    =>  -1, then 61    (D flat)
                         61 1  =>  1, then 61     (C sharp)

A patch in a flat key sets ``spelling`` to -1 once, and a single note
re-spelled as a sharp does not change it. There is no empty state and no
chords: the staff always shows one note.

.. patcher-object:: .preset

``store <n>`` captures every control whose GUI value can be written back, in
the whole patch including subpatchers, and a recall sends each one its value
again. :ref:`gui-preset` covers the messages, what a slot captures, and when
a recall runs. The slots are saved with the patch, but loading does not
recall one (see :ref:`gui-saved`).

.. code-block:: text

   [.preset 8]   store 0  ...  0  =>  every control back as it was, then 0

The GUI value is the active slot, or -1 when none is active. It is
read-only. A recall is instant: there is no blending between slots.

.. patcher-object:: .radiogroup

It works exactly like ``.umenu``; see there. A host draws radio buttons, or
check boxes with ``multi``: ``.radiogroup multi 8`` is eight check boxes. In
``multi`` mode a number toggles that box, and a list of one 0 or 1 per box
sets them all.

.. patcher-object:: .rslider

The two ends are set separately, and either may be the higher one: the
object always sends the lower end from the left outlet and the higher end
from the middle one, with the pair, lower first, from the right outlet. The
outlets fire right to left.

.. code-block:: text

   [.rslider]   20 -> right inlet, then 100 -> left inlet  =>  20. 100.,  100,  20

- a number on the left inlet sets that inlet's end and sends.
- a number on the right inlet sets the other end silently.
- a list sets both ends from its first two numbers; a single number sets the
  left inlet's end.
- ``set 0 <value>`` moves the current low end and ``set 1 <value>`` the high
  end.
- bang: send again.

Both ends are clamped to ``minimum``-``maximum``, which may be given in
either order. The GUI value has two cells, low end first.

.. patcher-object:: .slider

The position runs from 0 to 1. A number outside that range is clamped, so an
int sets 0 or 1. Every message sends the position. For another range, use
``.dial`` or put ``.scale`` after the slider.

**Restoring a value**

``.slider``, ``.dial``, ``.i``, ``.f`` and ``.t`` take their own GUI value
back on the left inlet, which is how a host or ``.preset`` restores them:

- a list holding one value sets it. That is exactly the string the host read.
- ``set 0 <value>`` sets it too. 0 is the cell number; any other cell number
  is ignored.

Both work like a number on the inlet: clamped the same way, and the value is
sent. Only the first number of a longer list is used. A list starting with
any other word changes nothing, and the value is sent again. See
:doc:`/patcher/gui` for the protocol as a whole.

.. patcher-object:: .t

An int sets the state: 0 is off, any other number is on. A bang flips it.
Every message sends the state as 0 or 1.

The GUI value is ``on`` or ``off``. The left inlet takes that word back, or a
number, alone or after ``set 0``, and sets the state without flipping it.
Restoring works as described under ``.slider``.

.. patcher-object:: .tab

It works exactly like ``.umenu``; see there. A host draws a row of tabs.
Wire the name outlet into ``.route`` to switch between named pages of a
patch.

.. patcher-object:: .textbutton

It works exactly like ``.led``; see there. A host draws a button with the
label on it.

.. patcher-object:: .textedit

Everything that arrives is text. A list is stored word for word: no word is
a command, so a field can hold ``set`` or ``clear`` like any other text. A
number is stored as the text that spells it, and an empty list clears the
field. Every message sends the text from the outlet, as a list.

The field holds at most 256 characters. Longer text is ignored and the
stored text is kept (and sent again). A starting text longer than that is
ignored with an error in the log.

The text is never turned back into a number: a field holding ``440`` sends
the text ``440``. Put ``.fromsymbol`` after it to read a number, or
``.tosymbol`` to join the words into one.

The GUI value is the text. It is written back as plain text, with no ``set``
form (see :doc:`/patcher/gui`). The live text is not saved with the patch;
the ``text`` argument is only the starting text.

.. patcher-object:: .umenu

``.umenu``, ``.radiogroup`` and ``.tab`` are the same object under three
names. A host draws ``.umenu`` as a pop-up menu, ``.radiogroup`` as radio
buttons or check boxes and ``.tab`` as a row of tabs; they take the same
arguments and messages.

**Arguments**

An optional ``multi`` keyword, then the items:

- no items: two items, named ``0`` and ``1``.
- a single number of 1 or more: that many items, named by their numbers.
- anything else: one item per word, up to 256. A longer list is cut short
  with a warning.

With ``multi``, several items can be selected at once. The first item cannot
be named ``multi``, and an item named ``set`` cannot be selected by name. The
items are fixed: changing the arguments replaces the object. There are no
messages to add or remove items.

**Messages**

- a number: select that item. With ``multi`` it toggles the item instead. A
  number outside the list is ignored.
- an item name: select that item. An unknown name is ignored.
- a list of numbers: the whole selection, as the right outlet spells it.
  Without ``multi`` that is the item number; with ``multi`` it is one 0 or 1
  per item, and a shorter list leaves the remaining items as they are.
- ``set <index> <value>``: without ``multi``, ``set 0 <item>`` selects an
  item; with ``multi``, ``set <item> <0|1>`` sets one item.
- bang: send again.

**Output**

Every message sends, right to left: the whole selection, then the name of
the item, then its number. With ``multi`` the number is the item that just
changed, or after a whole-selection write the lowest selected item.

.. code-block:: text

   [.umenu sine square saw]   saw  =>  2,  saw,  2

The GUI value is the selection: one cell with the item number, or with
``multi`` one cell per item.

.. patcher-object:: .xyslider

x and y are independent, each clamped to its own pair of arguments, which
may be given in either order. The position is never sorted: the first cell
is always x.

- a number on the left inlet sets x and sends.
- a number on the right inlet sets y silently.
- a list ``x y`` sets both; a single number sets x.
- ``set 0 <value>`` sets x and ``set 1 <value>`` sets y.
- bang: send again.

The outlets fire right to left: the pair, then y, then x. The pair is the
cursor message ``.nodes`` takes, so one cord turns the pad into a morph
control.
