.. Notes for the Control flow category. Format: _data/patcher_notes/README.md.

.. patcher-object:: .bondo

Every inlet is hot: a value arriving at any inlet is stored there, and then
every outlet sends what its inlet holds, the last outlet first and outlet 0
last (see :doc:`/patcher/messages`). Wire outlet 0 to whatever acts on the set
and the others to cold inlets, and the whole set is in place when outlet 0
fires.

Stored values stay until replaced, so each send repeats the values that did
not change. An inlet that has received nothing sends 0.

**Messages**

Every inlet takes all of these:

- an int, float, list or word: store it here and send the set.
- a list that starts with a number: spread over this inlet and the ones to its
  right, one item each; items past the last inlet are dropped. With the ``n``
  argument the list is stored whole in this inlet instead.
- a message that starts with a word (``note 60``): stored whole in this inlet.
- bang: send the set without storing anything.
- ``set <message>``: store as above without sending. A bare ``set`` is an
  ordinary word: it is stored and the set is sent.

**Arguments**

In order: the number of inlet/outlet pairs (1-256, default 2), then a delay in
milliseconds, then ``n`` to store lists whole (``n`` may stand anywhere). With
a delay, a stored value sends the set that many milliseconds later instead of
at once; another value before then restarts the wait, so a burst of changes
sends one set. A bang still sends at once. Waits are counted in whole audio
blocks; see :doc:`/patcher/time`.

``.buddy`` is the other way round: it waits until every inlet has a value.

.. patcher-object:: .buddy

Each inlet stores the last value it received. When the last empty inlet
receives one, every outlet sends its inlet's value, the last outlet first and
outlet 0 last (see :doc:`/patcher/messages`), and all inlets are empty again.
Nothing is ever sent twice, and nothing is sent for an incomplete set.

.. code-block:: text

   [.buddy 2]   in1 <- 7    =>  nothing
                in0 <- 3    =>  out1: 7, out0: 3
                in0 <- 4    =>  nothing (in1 is empty again)

**Messages**

- an int, float, list or word: stored in this inlet, replacing what it held. A
  list is stored whole and leaves the matching outlet unchanged.
- bang: stores the number 0, so a source with no value of its own can still
  take part.
- ``clear``, on inlet 0 only: empty every inlet without sending. On the other
  inlets the word is stored like any other value.

To send a set on any change, keeping the values that did not change, use
``.bondo``.

.. patcher-object:: .if

The argument is a statement:

.. code-block:: text

   <condition> then <message> [else <message>]

   [.if $i1 > 64 then bang else out2 $i1]

When the condition is not 0 the ``then`` message is sent, otherwise the
``else`` message. Without ``else``, a false condition sends nothing, which
filters a stream.

**The condition** is an expression in the language of ``.expr`` (see
:doc:`/patcher/objects/math`): the same ``$i1``-``$i9`` and ``$f1``-``$f9``
placeholders, operators, functions and int/float rules.

**A message** is up to 16 items separated by spaces. Each item is itself an
expression without spaces, such as ``1``, ``0.5``, ``$i1`` or ``$i1*2``. One
item is sent as an int or a float, depending on its type; two or more are sent
as a list. ``bang`` on its own sends a bang. ``out2`` in front of a message
sends it from a second, right outlet, which exists only when a message uses
it.

**Inlets**

There is one inlet per placeholder number, up to the highest one used (at
most 9). Inlet 0 stores its number and evaluates; the others only store. A
bang on inlet 0 evaluates with the stored numbers, and a list on inlet 0 fills
the inlets from the left, then evaluates. An inlet that has received nothing
reads 0.

**Errors**

A statement that cannot be read is reported in the log when the object is
created, and the object then sends nothing. Text placeholders (``$s``) are
not supported, and neither is a ``send <name>`` message to a named receiver:
wire the outlet to a ``.s`` instead.

.. patcher-object:: .loadbang

The bang is sent once, after the whole patch has been loaded with
``ParseJSON``: every object created, every cord connected. Objects inside
subpatchers fire in the same pass.

An object created with ``CreateObject`` in a running patch does not fire until
the patch is saved and loaded again. Send it a bang to fire it by hand; a host
that builds a patch object by object can do this to initialise it.

The order in which two ``.loadbang`` objects fire is not defined. When one
initialisation must come before another, use one ``.loadbang`` and a
``.trigger``. See :doc:`/patcher/file_format` for the whole load sequence.

.. patcher-object:: .loadmess

Fires at the same moment as ``.loadbang`` and follows the same rules (see
there). ``.loadmess 0.5`` into a gain does what ``.loadbang`` into a message
box holding ``0.5`` would.

The message is sent in the form it has:

- nothing at all when the object has no arguments;
- a bang for the single word ``bang``;
- an int or a float for a single number, an int when written without a ``.``
  or an exponent: ``60`` is an int, ``60.0`` a float;
- otherwise a list with the text unchanged.

**Messages**

- bang: send the stored message.
- ``set <message>``: replace the stored message without sending. A bare
  ``set`` empties it. This works from a list and from a message box.
- any other list is ignored, so values passing through cannot replace the
  initialisation.

A ``set`` longer than 256 characters is refused, and the old message is kept.
Arguments longer than that are cut at the last whole word that fits. ``set``
does not change the saved arguments: the patch saves the message the object
was created with.

.. patcher-object:: .match

The object keeps the most recent numbers, as many as the pattern is long. When
those spell the pattern, it sends them as a list and forgets them, so matches
never overlap.

.. code-block:: text

   [.match 1 nn 3]   1, 5, 3       =>  1 5 3
   [.match 1 2 3]    1 2 1 2 3     =>  1 2 3
   [.match 1 1 1]    1 1 1 1 1     =>  1 1 1   (once)

A value that ends one attempt can start the next, as in the second example.

**Matching**

- A number in the pattern matches only that exact number. Ints and floats are
  compared as floats, so ``5`` matches ``5.0``. A calculated float can miss a
  number that looks equal; put ``.round`` in front.
- ``nn`` matches any number.
- A word in the stream, or a value that is not finite, takes a place and
  matches nothing, not even ``nn``.
- A pattern word other than ``nn`` (``NN`` included) matches nothing, so the
  object never fires. With no pattern at all it never fires either.

The output keeps the values as they arrived, so the places filled by ``nn``
show what came in. An int is sent as an int and a float as a float.

**Messages**

- an int, float or list: each number, or each list item, takes the next place
  in the stream.
- ``clear``: forget the numbers received so far. The pattern stays.
- ``set <pattern>``: a new pattern, written like the arguments. It also
  forgets the numbers received so far. A bare ``set`` leaves no pattern. The
  saved arguments do not change.

The pattern holds at most 256 elements.

.. patcher-object:: .next

A *logical event* is everything one stimulus causes: one host call, one GUI
action, one tick of a timed object, one MIDI message (see *Logical events* in
:doc:`/patcher/messages`). Time does not matter: two host calls are two events
however close together, and the whole fan-out of one ``.trigger`` is one event
however long it takes.

The first message of an event sends a bang from the left outlet, and every
later message of the same event sends one from the right. Exactly one outlet
fires per message. Typical use is to do something once per burst, such as
resetting a counter at the start of a ``.coll`` dump without knowing how long
the dump is.

.. code-block:: text

   [.uzi 3] -> [.next]   bang   =>  left, right, right

The content of a message is ignored, so a 0 counts like any other value.

.. patcher-object:: .onebang

The object is a gate with two states. When armed, the next message on the
left inlet sends a bang from the left outlet and closes the gate. When
closed, a message sends a bang from the right outlet instead. Every message
gives exactly one bang, so the right outlet can count or report the triggers
that were blocked; leave it unconnected for a plain one-shot.

- Anything on the right inlet arms the gate, including the word ``stop``.
  Arming an armed gate changes nothing: five arms and five triggers give one
  pass and four rejects.
- Everything on the left inlet is a trigger, including 0. The one exception is
  the bare word ``stop``, which closes the gate without sending anything.
  ``stop 1`` is a trigger.
- The gate starts closed. A non-zero argument starts it armed; ``0.5`` counts
  as non-zero, and an argument that is not a number counts as 0.

To open and close a path with numbers, use ``.gate``.

.. patcher-object:: .trigger

Each argument makes one outlet, in order. When a message arrives, every outlet
sends, the last outlet first and outlet 0 last. Each outlet's branch finishes
before the next outlet sends (see :doc:`/patcher/messages`).

.. code-block:: text

   [.trigger b i]   7   =>  out1: 7, then out0: bang

The common use is ``.trigger b i``: the number reaches a cold inlet first, and
only then does the bang make the object compute with it.

**Formats**

- ``i``: an int. A float is truncated toward zero; a list or a bang sends 0.
- ``f``: a float. A list or a bang sends 0.
- ``b``: a bang, whatever arrived.
- ``l``: a list unchanged. Anything else sends the list ``0``.
- ``s``: text unchanged. Text travels as a list, so a list passes through
  ``s`` too; a number or a bang sends the empty symbol.

``i`` and ``f`` do not read lists: a list ``5 6`` sends 0 from an ``i``
outlet. Use ``.unpack`` or ``.route`` to take numbers out of a list.

Any other argument is a constant, sent on every input whatever arrived: a
number without a ``.`` or an exponent is an int (``7``), one with them is a
float (``0.5``), and anything else is sent as text. The format letters are
lower case and single characters, so ``I`` or ``bang`` are constants.

With no arguments there are two ``i`` outlets. At most 256 outlets.

.. patcher-object:: .uzi

A bang starts a run. For each iteration, the index goes out the right outlet,
then a bang out the left outlet, so whatever the bang drives already has the
new index. After the last iteration the middle outlet sends a bang.

.. code-block:: text

   [.uzi 3]     bang   =>  index 1, bang, index 2, bang, index 3, bang, carry
   [.uzi 4 0]   bang   =>  indices 0 1 2 3

Each send finishes everything it triggers before the next one, and the whole
run is one logical event (see :doc:`/patcher/messages`). A run of 0 iterations
still sends the carry bang.

**Messages**

On the left inlet:

- bang: run.
- int or float: set the count, then run. A float is truncated.
- ``pause`` (or ``break``): stop the run that is in progress after the current
  iteration. Only a message triggered by the run itself can arrive during a
  run, so this is how a loop exits early, such as stopping a search once the
  value is found. A paused run sends no carry bang. Outside a run it does
  nothing.
- ``resume`` (or ``continue``): finish a paused run, numbering on from where it
  stopped. It does nothing if no run is paused.
- ``offset <n>``: skip the first n iterations of every run from now on, so the
  run sends count minus n bangs and the index starts at base plus n.
  ``offset 0`` turns this off.

Other words are ignored. The right inlet sets the count for the next run
without starting it.

**Limits**

The count is clamped to 0-4096. A creation argument above that is reported in
the log. A bang or ``resume`` that arrives while a run is in progress, for
example from an outlet wired back to the left inlet, is ignored, so such a
loop always ends. A count changed during a run applies to the next run.
