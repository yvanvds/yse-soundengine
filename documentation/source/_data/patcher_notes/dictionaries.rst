.. Notes for the Dictionaries category. Format: _data/patcher_notes/README.md.

.. patcher-object:: .dict

A dictionary holds entries: a key path and a value. Keys nest with ``::``,
so ``voice::1::freq`` is one entry three levels deep. A value is the text of
any message, so ``set chord 0 4 7`` stores one value of three numbers. See
*Nested keys* in :doc:`/patcher/data` for how the nesting works.

**Messages**

- ``set <path> <value...>``: store a value, replacing the one already there.
  ``set <path>`` with no value stores an empty value.
- ``get <path>``: send the value out of outlet 0. One number leaves as an
  int or a float, anything else as a list. When the path is missing, outlet
  2 bangs instead. A path holding an empty value sends nothing at all.
- ``delete <path>``: remove one entry. A sub-tree is not an entry:
  ``delete voice`` does not remove ``voice::1::freq`` (use ``.dict.strip``).
- ``clear``: remove every entry.
- ``getsize``: send the number of entries out of outlet 0.
- ``getkeys``: send the top-level keys out of outlet 0, each once, in the
  order they were first written.
- bang: send the reference ``dictionary <name>`` out of outlet 1.

**Sharing**

Every ``.dict`` with the same name in a patcher uses the same dictionary, and
two patchers with the same name share theirs. A ``.dict`` without a name
keeps a private dictionary and sends no reference. The contents are saved
with the patch as nested JSON. See :ref:`data-sharing` and
:ref:`data-saved`.

**The other .dict objects**

The other objects in this category each work on one or more named
dictionaries. They take the names as creation arguments, and the names
cannot be changed while the patch runs. A cord cannot carry a dictionary,
so an object that produces one writes it into a dictionary you name, rather
than sending it (see :ref:`data-references`). They share these rules:

- A bang on the left inlet runs the object. So does the reference
  ``dictionary <name>`` of the dictionary it reads, so outlet 1 of a
  ``.dict`` can be wired straight into it. A reference that names any other
  dictionary is refused.
- Any extra inlet accepts the reference of the dictionary it stands for and
  does nothing with it, so the wiring can show which dictionaries an object
  uses.
- An object that writes a dictionary replaces its whole contents
  (``.dict.strip`` edits it in place), then sends that dictionary's
  reference. So the objects chain: the reference that leaves one can run the
  next.
- Each run works on the dictionary as it was when the run started. Changes
  made while it sends, even by objects it is wired to, show up only on the
  next run.
- An object without a name works on a private, empty dictionary and sends no
  reference.

.. code-block:: text

   set voice::1::freq 440  ->  [.dict synth]
   bang                    ->  [.dict synth]              outlet 1: dictionary synth
   dictionary synth        ->  [.dict.serialize synth]    ->  {"voice":{"1":{"freq":440}}}

**Limits**

A dictionary holds at most 256 entries. A key path is at most 128 characters,
including the ``::`` separators, and a value at most 256. A message that
does not fit is refused: it changes nothing, sends nothing and is not
logged. The same refusal happens when two threads use one dictionary at the
same moment: the second one does nothing rather than wait. See
:ref:`data-limits`.

Safe on the audio thread: no message allocates memory, takes a lock or
waits. Only ``.dict.deserialize`` does part of its work elsewhere.

.. patcher-object:: .dict.compare

Two dictionaries are equal when they hold the same key paths with the same
values, in any order. Values are compared as text, so ``7`` and ``7.``
differ.

On a difference, the paths that differ follow the verdict: first the paths
that are only in the left dictionary or hold a different value, in the left
dictionary's order, then the paths that are only in the right one. The
verdict is sent first, so a patch can act on it before the list arrives.

Both arguments may name the same dictionary. At most 256 paths are sent.

.. patcher-object:: .dict.deserialize

The message must be one JSON object, such as the text ``.dict.serialize``
sends. It replaces the contents of the named dictionary, and the reference
then leaves the outlet:

- Nested objects become ``::`` key paths.
- An array becomes a list value. Arrays and objects inside an array are
  dropped.
- ``true`` and ``false`` become ``1`` and ``0``; ``null`` becomes an empty
  value.

The JSON is read in the background, so the dictionary changes and the
reference is sent one audio block after the message arrives, not at once.
A document that is not valid JSON, or is not an object, changes nothing and
sends nothing. A document that arrives while the previous one is still being
read is refused.

**Files**

A message holds at most 255 characters. For a larger document use ``read
<file>``, which loads a JSON file of up to 128 KiB and then works like a
message. A bare ``read`` reads the last file again. See :doc:`/patcher/files`.

Entries past 256, or with a key path or value that is too long, are dropped
and the rest of the document still loads.

.. patcher-object:: .dict.group

Each entry of the source is written into the target as
``<group>::<original path>``, with its value unchanged. Without a key
argument, an entry's group is its own value, which turns a table into an
index. With a key argument, the source is read as records: the first part of
each path names a record, and every entry of the record groups under the
value the record holds at that key.

.. code-block:: text

   source notes:   n1::inst bass   n1::pitch 40   n2::inst lead   n2::pitch 72
   [.dict.group notes byinst inst]
   target byinst:  bass::n1::inst bass   bass::n1::pitch 40
                   lead::n2::inst lead   lead::n2::pitch 72

An entry is skipped when its group is empty, when its record has no entry at
the key, or when the new path would be longer than 128 characters.

.. patcher-object:: .dict.iter

Each entry leaves as one list: its whole ``::`` path, then its value. The
entries leave in the order they were first written. When the last one has
been sent, the right outlet bangs, also for an empty dictionary.

Each pair is handled completely, including everything it causes downstream,
before the next pair leaves. Use the done bang for whatever should happen
after the walk. A run that arrives while a walk is still sending is refused,
so an outlet wired back into the inlet does not restart the walk.

.. patcher-object:: .dict.join

The target gets every entry of the left dictionary, then every entry of the
right one. Where both hold the same key path, the right value replaces the
left one, so the left dictionary is the base and the right one the
override. Neither source is changed, unless it is also the target:
``.dict.join base preset base`` merges ``preset`` into ``base``.

When the result would be more than 256 entries, the new key paths of the
right dictionary that do not fit are dropped; the rest of the join still
happens.

.. patcher-object:: .dict.pack

The first argument names the dictionary, and every argument after it is a
key path with its own inlet. The inlets work like those of ``.pack`` (see
:doc:`lists`): the left inlet stores its value and writes, the others only
store, so set the right-hand values first.

.. code-block:: text

   [.dict.pack synth freq gain]   inlet 1 <- 0.5   inlet 0 <- 440
   dictionary synth now holds:    freq 440   gain 0.5

**Messages**

- A value on any inlet is stored for that inlet's key path. A whole list is
  one value, not spread over the inlets.
- bang on the left inlet: write the dictionary without storing anything.
- ``set <value>``: store a value without writing.
- The reference ``dictionary <name>`` of the bound dictionary writes it on
  the left inlet and is ignored on the others.

Writing replaces the whole dictionary with one entry per key path, so any
other key in it is removed. A key path that has not received a value yet is
written with an empty value. With no key paths, a bang writes an empty
dictionary. When the same key path is given twice, the inlet further right
wins.

At most 256 key paths of at most 128 characters; a longer path is logged and
gets no inlet. A value longer than 256 characters is refused and the inlet
keeps its old value.

.. patcher-object:: .dict.print

The dictionary is printed as indented JSON, the same structure a saved patch
holds. Each line starts with the dictionary's name and a colon:

.. code-block:: text

   synth: {
   synth:   "voice": {
   synth:     "1": {
   synth:       "freq": 440
   synth:     }
   synth:   }
   synth: }

The lines reach the engine log the next time the host calls
``YSE::System().update()``. ``YSE::Log().setLogfile()`` and
``YSE::Log().setHandler()`` choose where the log goes.

**Limits**

The second argument limits how many lines the object sends per update,
64 by default and 1 to 256. When a dump runs past it, the rest is dropped and
one line says that the limit was reached. A line too long for the log is cut
and ends in ``...``. Only the first 32 characters of the name are used as the
label.

.. patcher-object:: .dict.route

Each key argument has an outlet, and one more outlet on the right is added
for no match. The dictionary is tested for each key in order, and its
reference leaves the outlet of the first key it holds. Exactly one outlet
sends per run.

A key matches an entry at that path or under it: ``voice`` matches
``voice::1::freq``, but not ``voicing``. A key can be a deeper path such as
``voice::1``. When a key is given twice, only its first outlet is used.

The no-match outlet sends the same reference, so a second ``.dict.route``
wired to it can test more keys. Without a name the object sends nothing,
because a private dictionary has no reference. A run that arrives while the
object is still sending is refused, so an outlet wired back into the inlet
does not loop.

.. patcher-object:: .dict.serialize

The JSON is written on one line with no spaces outside strings. ``::`` paths
become nested objects, and the entries keep the order they were first
written. Values are written as:

- one number: a JSON number;
- several tokens: an array;
- an empty value: ``""``;
- anything else: a string.

An empty dictionary is ``{}``. ``.dict.deserialize`` reads this text back
into a dictionary.

**Limits**

A message holds at most 255 characters. When the JSON is longer, nothing is
sent. Use ``write <file>`` instead, which saves the same JSON to a file of up
to 128 KiB, or walk the entries with ``.dict.iter``. A bare ``write`` writes
to the last file again. See :doc:`/patcher/files`.

.. patcher-object:: .dict.slice

An entry is *under* the path when its key starts with the path followed by
``::``. Those entries go into the slice dictionary with that start removed,
so the sub-tree becomes a dictionary of its own. Every other entry goes into
the remainder dictionary unchanged.

.. code-block:: text

   source:     voice::1::freq 440   voice::1::gain 0.5   voice::2::freq 220
   [.dict.slice synth v1 rest voice::1]
   v1:         freq 440   gain 0.5
   rest:       voice::2::freq 220

- An entry stored at exactly the path is a value, not a sub-tree. It goes
  into the remainder.
- ``voice`` does not match ``voices::1``: the path ends at a ``::``.
- An empty path matches nothing, so the remainder gets everything.

The remainder's reference is sent first, then the slice's. When both
targets have the same name, that dictionary ends up holding the remainder.

.. patcher-object:: .dict.strip

The entries removed are exactly the ones ``.dict.slice`` would put in its
slice for the same path: every entry whose key starts with the path followed
by ``::``. An entry at exactly the path is kept, and ``voice`` does not
match ``voices::1``. An empty path removes nothing.

To remove one entry, send ``delete <path>`` to ``.dict``; to remove
everything, send it ``clear``.

.. patcher-object:: .dict.unpack

The first argument names the dictionary, and every argument after it is a
key path with its own outlet. Each value leaves its outlet right to left, so
a ``.dict.unpack`` can feed the cold inlets of an object before its hot one.
One number leaves as an int or a float, anything else as one list.

A missing path, or one holding an empty value, sends nothing on its outlet.
When the same path is given twice, both outlets send. A run that arrives
while the object is still sending is refused.

``.dict.pack`` and ``.dict.unpack`` with the same name and key paths are a
pair: a reference from the ``.dict.pack`` makes the ``.dict.unpack`` send the
values that were packed. At most 256 key paths of at most 128 characters; a
longer path is logged and gets no outlet.
