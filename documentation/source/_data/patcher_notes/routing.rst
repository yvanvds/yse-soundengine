.. Notes for the Routing category. Format: _data/patcher_notes/README.md.

.. patcher-object:: .bucket

By default the register runs one step behind: a number first sends what the
register holds, then shifts and stores itself at outlet 0. It shows up on the
next input. With echo mode on (a non-zero second argument), the number is
stored first and is part of the same burst.

.. code-block:: text

   [.bucket 3]     1, 2, 3   =>  (out0 out1 out2)  0 0 0,  1 0 0,  2 1 0
   [.bucket 3 1]   1, 2, 3   =>  (out0 out1 out2)  1 0 0,  2 1 0,  3 2 1

Every outlet sends on every input, the last outlet first and outlet 0 last
(see :doc:`/patcher/messages`). A value keeps the int or float type it
arrived with.

**Messages**

- a number: shift and send.
- a list that starts with a number: each number is fed in on its own, as if
  sent separately. Words in the list are skipped.
- bang: send the stored values without shifting.
- ``set <n>``: fill every stage with n and send it out every outlet.
- ``clear``: set every stage to 0 without sending anything.
- ``freeze``: stop all output, including bang, ``set`` and ``roll``, while
  numbers go on shifting in. ``thaw`` turns output back on; it sends nothing
  by itself, so follow it with a bang to see what was collected.
- ``L2R`` / ``R2L``: new values enter at outlet 0 (the default) or at the
  last outlet. Spelled in capitals.
- ``roll``: feed the value at the far end, the one the next input would push
  out, back in as the input. The register rotates and nothing is lost. A
  number after ``roll`` is ignored.

Any other message is ignored. The direction and the freeze are not saved, and
changing the arguments starts again from zeros.

.. patcher-object:: .cycle

**Messages**

- bang, int or float: goes out the next outlet whole.
- a list that starts with a number: dealt one item per outlet, each number
  keeping its int or float type and each word going out as a one-word list.
- a message that starts with a word (``note 60 100``): goes out the next
  outlet whole.
- ``set <n>``: the next message goes out outlet n. Nothing is sent, and an
  outlet number the object does not have is ignored.
- ``thresh <n>``: event mode on (non-zero) or off. The creation argument sets
  the same mode; ``thresh`` is not saved.

.. code-block:: text

   [.cycle 3]   1, 2, 3, 4   =>  out0: 1, out1: 2, out2: 3, out0: 4
   [.cycle 3]   10 20        =>  out0: 10, out1: 20, and out2 is next

In event mode the rotation starts again at outlet 0 for every new logical
event (see :doc:`/patcher/messages`). A chord that arrives as one burst, from
a list or a ``.trigger``, is dealt from outlet 0 each time, however many notes
the last chord had. Two separate host calls are two events, however close
together. A ``set`` holds for the rest of its own event.

The rotation always starts at outlet 0. It does not know whether a receiver is
still busy; to give notes to free voices, use ``.poly``.

.. patcher-object:: .decode

Every message sends the whole state, one int out every outlet, the last
outlet first. A bank wired to the outlets therefore always matches the
selection, whatever it missed before. Before anything arrives, outlet 0 is
selected.

The right inlet overrides the middle one, and the middle one overrides the
selection. Each inlet remembers its last number, so turning an override off
brings back the state underneath it without sending the index again:

.. code-block:: text

   [.decode 3]                (out0 out1 out2)
     left 2              =>   0 0 1
     middle 1            =>   1 1 1
     right 1             =>   0 0 0
     right 0             =>   1 1 1
     middle 0            =>   0 0 1

Any non-zero number mutes in the right inlet, negative numbers too; the middle
inlet only turns on for numbers above 0. The inlets take ints, and the left
inlet also takes a bang. Floats and lists are not accepted.

.. patcher-object:: .funnel

.. code-block:: text

   [.funnel 3]     inlet 2 <- 60            =>  2 60
                   inlet 0 <- note 60 100   =>  0 note 60 100
   [.funnel 3 1]   inlet 0 <- 0.5           =>  1 0.5

The number in front is always an int. The message after it keeps its
spelling, so a float stays a float.

**Messages**

Every inlet takes all of these:

- a number: sent with the inlet's number in front, and stored.
- a list or a word message: sent with the inlet's number in front. It is not
  stored.
- bang: send the inlet's stored number, with the inlet's number in front. An
  inlet that has stored nothing sends 0.
- ``set <n0> <n1> ...``: store numbers for inlets 0, 1, 2 ... in order,
  without sending anything. Extra numbers are ignored, a short list leaves the
  other inlets alone, and a word in the list skips that inlet.
- ``offset <n>``: the number added to every inlet's number. The second
  argument sets the same value; ``offset`` is not saved.

A bare ``set`` or ``offset`` does nothing.

A ``.funnel n`` into a ``.spray n`` with the same offset carries n separate
streams over one cord: what enters inlet i leaves outlet i. ``.route`` and
``.sel`` can also branch on the number in front.

.. patcher-object:: .matrix

**Layout**

The leftmost inlet takes the connection messages and nothing else. The data
inlets are to its right, and connection messages number them from 0, so
``connect 0 0`` means the first data inlet. The data outlets are numbered from
0 as well. The rightmost outlet only sends the answer to ``dumpconnections``.
``.router`` has the same layout.

**Connection messages**

- ``<inlet> <outlet> <gain>``: connect with that gain. A gain of 0
  disconnects.
- ``connect <inlet> <outlet> [gain]``: connect. Without a gain the third
  argument is used, 1 by default.
- ``disconnect <inlet> <outlet>``: remove the connection.
- ``clear``: remove every connection.
- ``dumpconnections``: send one ``<inlet> <outlet> <gain>`` line per
  connection out the rightmost outlet, by inlet and then by outlet. The
  lines have the same form as the first message, so ``clear`` followed by the
  stored lines restores a routing. A request that arrives during a dump is
  ignored.

A port number the object does not have, extra text after a message, or a
message in any other form is ignored. ``dictionary <name>`` is accepted and
ignored: reading the connections from a dictionary is not supported.

**Scaling**

A message at a data inlet goes out every connected outlet, the last outlet
first, scaled by that connection's gain:

- At a gain of exactly 1 the message passes unchanged: an int stays an int and
  a list is not rewritten. A ``.matrix`` at gain 1 works like a ``.router``.
- At any other gain each number is multiplied and leaves as a float, also
  when it arrived as an int.
- A bang stays a bang.
- A list is scaled item by item when every item is a number. Otherwise, as
  with ``note 60 100``, it passes unchanged. A list of more than 256 items
  also passes unscaled.

.. code-block:: text

   [.matrix 2 2]   connect 0 1 0.5, then 10 into data inlet 0   =>  out1: 5.

When several inlets reach one outlet, each message leaves separately; they are
not added together. Connection words that arrive at a data inlet are passed on
as data.

The connections are not saved with the patch, and changing the arguments
removes them. Store the output of ``dumpconnections`` to keep a routing.

.. patcher-object:: .route

What the matching outlet sends is the rest of the message: a list, a single
int or float, or a bang when nothing is left. The matching rules, the
no-argument case and examples are under *Selector matching* in
:doc:`/patcher/messages`.

To keep the matched item on the message, use ``.routepass``.

.. patcher-object:: .routepass

The matching rules are the same as for ``.sel`` and ``.route``; see
*Selector matching* in :doc:`/patcher/messages`. The matched message leaves
exactly as it arrived, matched item included, so an object further down can
still see it.

Without arguments there are no selectors and only the rightmost outlet, which
everything passes through.

.. patcher-object:: .router

The layout and port numbering are as described under ``.matrix``. A data
message goes out every connected outlet unchanged, the last outlet first.

**Connection messages**

- ``<inlet> <outlet> <state>``: a non-zero state connects, 0 disconnects.
- ``connect <inlet> <outlet>`` / ``disconnect <inlet> <outlet>``.
- ``patch <inlet> <outlet>``: connect, and disconnect every other inlet from
  that outlet, in one step.
- ``clear``: remove every connection.
- ``dump``: send one ``<inlet> <outlet> <state>`` line for every pair,
  connected or not, by inlet and then by outlet. A request that arrives during
  a dump is ignored.
- ``print``: accepted and ignored. Use ``dump`` to read the routing.

A port number the object does not have, or a message in any other form, is
ignored. The connect messages take exactly the numbers shown; ``clear``,
``dump`` and ``print`` ignore anything after the word.

An inlet may reach several outlets and several inlets may reach one outlet,
all at once. Changing a connection takes effect from the next message, so a
patch can rewire itself while it runs.

.. code-block:: text

   [.router 2 2]   connect 0 1, connect 1 1   =>  both data inlets reach out1

The connections are not saved with the patch, and changing the arguments
removes them. Store the output of ``dump`` to keep a routing.

.. patcher-object:: .sel

The matching rules are under *Selector matching* in
:doc:`/patcher/messages`. A list matches on its first item, so ``.sel 5``
bangs for ``5 6`` as well.

When there is exactly one selector and it is a number, a right inlet appears.
A number sent there replaces the selector without sending anything. With more
than one selector, the selectors cannot be changed after creation.

There is no setting for near matches. To match computed floats, put a
``.round`` in front.

.. patcher-object:: .split

Chain the right outlet into another ``.split`` to test the next range, for
keyboard splits or velocity layers. The default range is 0 to 127.

Limits given the wrong way round still work: the lower of the two is used as
the low limit. Numbers leave as floats, also when they arrive as ints. A NaN
goes out the right outlet.

.. patcher-object:: .spray

Value k after the index goes out outlet index + k. Values that would land
past the last outlet are dropped. The outlets send right to left (see
:doc:`/patcher/messages`), and each value keeps its int or float type. A word
in the list is skipped but still uses up its outlet.

.. code-block:: text

   [.spray 4]     1 60 64   =>  out2: 64, then out1: 60
   [.spray 4 1]   1 60      =>  out0: 60
   [.spray 4]     -1 0 1    =>  out3: 1, out2: 0, out1: 1, out0: 0

**Messages**

- a list that starts with a number: the index, then the values.
- ``-1`` as the index: the values are repeated across all the outlets, so a
  single value reaches every outlet. This is checked before the offset is
  applied.
- ``offset <n>``: an index of n reaches outlet 0. The second argument sets the
  same value; ``offset`` is not saved. Values pushed below outlet 0 by the
  offset are dropped.

A bare number, a bang, an index with nothing after it, or a message that
starts with any other word is ignored.

In list mode (a non-zero third argument), the whole rest of the list goes out
the one indicated outlet as a list. With ``-1`` every outlet gets it.
