Collections, dictionaries and arrays
====================================

A patch often needs to remember more than one value: a preset table, the
notes that are held down, a cue list, a mapping curve. The patcher has a
family of **store objects** for this. Each one keeps its data inside the
object, in a table of fixed size, and answers messages that add, change,
look up and dump that data.

This page explains what the stores have in common: how some of them share
their contents by name, how dictionaries and arrays travel between objects,
what their size limits are, what happens when a limit is reached, and what is
saved with the patch. The full message list of each object is on its
reference page: :doc:`objects/collections`, :doc:`objects/dictionaries`,
:doc:`objects/arrays` and :doc:`objects/sequencing`. Reading and writing
files is covered in :doc:`files`.

The stores at a glance
----------------------

.. list-table::
   :header-rows: 1
   :widths: 14 34 12 20 20

   * - Object
     - Holds
     - Shared by name
     - Saved with the patch
     - Files
   * - ``.coll``
     - messages at number or symbol addresses
     - yes
     - yes
     - read and write
   * - ``.dict``
     - key/value pairs, nested with ``::``
     - yes
     - yes
     - through ``.dict.serialize`` / ``.dict.deserialize``
   * - ``.array``
     - an ordered list of single values
     - yes
     - yes
     - no
   * - ``.bag``
     - an unordered set (or multiset) of ints
     - no
     - no
     - no
   * - ``.table``
     - a dense array of ints, addressed by index
     - no
     - yes, unless ``embed 0``
     - no
   * - ``.funbuff``
     - (x, y) pairs sorted by x
     - no
     - only after ``embed 1``
     - no
   * - ``.capture``
     - a ring of the last values that arrived
     - no
     - no
     - no
   * - ``.textfile``
     - lines of text
     - no
     - no (the file is the storage)
     - read and write
   * - ``.qlist``
     - a timed cue list
     - no
     - yes
     - read and write
   * - ``.mtr``
     - recorded message tracks with timing
     - no
     - only after ``embed 1``
     - read and write
   * - ``.seq``
     - recorded raw MIDI bytes with timing
     - no
     - no (the file is the storage)
     - read (MIDI file or text), write (MIDI file)

``.value`` is a store too, but it holds a single value. It is described with
the other host-facing objects in :doc:`host_io`.

.. _data-sharing:

Sharing a store by name
-----------------------

``.coll``, ``.dict`` and ``.array`` take a **name** as their first creation
argument. Every object of the same type and the same name uses one shared
store. A write through one of them is seen by all the others straight away:

.. code-block:: cpp

   YSE::patcher patch;
   patch.name("song").create(2);

   // Both objects use the collection "patcher.song.notes".
   YSE::pHandle* writer = patch.CreateObject(".coll", "notes");
   YSE::pHandle* reader = patch.CreateObject(".coll", "notes");

   // A .r feeds the writer, so the host can store into it.
   YSE::pHandle* in = patch.CreateObject(".r", "store");
   patch.Connect(in, 0, writer, 0);
   patch.PassData(std::string("store 1 60 64 67"), "store");
   // Once the patch has rendered a block, a "1" sent to `reader`
   // sends "60 64 67" out of its outlet 0.

How names work:

- **The key is** ``patcher.<patcherName>.<name>``, the same address form
  that ``.s``, ``.r`` and ``.value`` use (see :doc:`host_io`). A
  ``.coll notes`` in a patcher named ``song`` is the store
  ``patcher.song.notes``. Two patchers with the same name therefore share
  their stores, just as they share their sends.
- **Each store type has its own namespace.** A ``.coll notes``, a
  ``.dict notes`` and an ``.array notes`` are three different stores.
- **No name means a private store.** A ``.coll`` without a name gets a store
  of its own. It does not share with other unnamed ``.coll`` objects.
- **A store lives as long as some object uses it.** The engine holds stores
  weakly. When the last object with a name is deleted, the store and its
  contents are freed. A new object with that name later starts empty. There
  is no owner object and nothing to free by hand.
- **The first object brings the store into existence.** An object that joins
  a name that is already in use adopts what is there. Adding a second
  ``.coll notes`` to a running patch does not reset the collection.
- **The name is fixed when the object is created.** The name is looked up
  once, on the control thread, because the lookup takes a lock. There is no
  message that points an object at another name while the patch runs, so
  Max's ``refer`` is not supported. To change the name, change the object's
  arguments (see :doc:`building`).
- **Renaming the patcher moves the objects, not the contents.** After
  ``patcher::name()`` gives a running patcher a new name, each named store
  object binds to the key under the new name. That key starts empty unless
  another patcher already uses it. The old contents are freed once no object
  holds the old key.

Each ``.coll`` keeps its own read position (the pointer used by ``next``,
``prev`` and a bang), so two ``.coll`` objects on one name walk the
collection independently.

The other stores do not share by name. ``.table`` accepts a name argument
so that patches from Max still load, but two tables with the same name keep
separate contents. ``.bag`` and ``.table`` can still *send*
their contents to named receivers: ``send <name>`` delivers them to every
``.r <name>`` in the patcher and on the bus.

.. _data-references:

Dictionaries and arrays travel by name
--------------------------------------

A cord carries a bang, a number, a list or an audio buffer. It cannot carry
a reference to a dictionary or an array. So ``.dict`` and ``.array`` do not
send their contents down a cord. They send their **name**, as a message:

- A bang on a ``.dict mydict`` sends ``dictionary mydict`` out of outlet 1.
- A bang on an ``.array mylist`` sends ``array mylist`` out of outlet 1.

This message is the *reference*. It holds the local name, without the
``patcher.<patcherName>.`` prefix. The receiving object adds its own
patcher's name, the same way the sender did.

The ``.dict.*`` and ``.array.*`` objects (``.dict.iter``, ``.dict.serialize``,
``.array.length``, ``.array.at`` …) bind their store from their creation
argument, just like ``.dict`` and ``.array``. A reference arriving on their
inlet is accepted only when it names the store they are already bound to. A
reference that names any other store is refused. So write the name into
each object:

.. code-block:: text

   [.dict voices]                 [.dict.serialize voices]
     outlet 1 ("dictionary voices") -> inlet 0      -> JSON text

A reference can also go through ``.s`` and ``.r``, or through the named bus
to the host. What travels is still only the name. The contents stay in the
store.

Dictionaries
------------

``.dict`` holds key/value pairs. The main messages are:

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Message
     - Effect
   * - ``set <path> <value…>``
     - store a value at a key path, replacing what was there
   * - ``get <path>``
     - send the value out of outlet 0, or bang outlet 2 if there is none
   * - ``delete <path>``
     - remove one entry
   * - ``clear``
     - empty the dictionary
   * - ``getsize``
     - send the number of entries out of outlet 0
   * - ``getkeys``
     - send the top-level keys out of outlet 0
   * - bang
     - send the reference ``dictionary <name>`` out of outlet 1

A value is stored as the text that arrived, so ``set chord 0 4 7`` stores one
value of three numbers. On the way out it is typed by its spelling: a single
number leaves as an int or a float, anything else as a list.

Nested keys
~~~~~~~~~~~

Keys nest with ``::``, as in Max: ``voice::1::freq`` is one entry, three
levels deep. Inside the object the table is flat and the nesting is part of
the key. That keeps every change a fixed-size write, which is what makes the
object safe on the audio thread. The price is that a sub-tree is not a value:
``get voice::1`` does not return the ``freq`` entry below it; it is a miss.
To work on a sub-tree, use ``.dict.slice`` (split it off into another
dictionary), ``.dict.strip`` (remove it) or ``.dict.iter`` (walk the
entries).

When the dictionary leaves the engine, the nesting becomes real again. A
saved patch and ``.dict.serialize`` both write a nested JSON object:

.. code-block:: json

   {"voice": {"1": {"freq": 440, "gain": 0.5}}}

and ``.dict.deserialize`` flattens such a document back into ``::`` keys.
If a key path runs into a key that already holds a value (``a`` holds a value
and ``a::b`` is stored as well), the JSON output skips the later entry rather
than overwrite the value.

Arrays
------

``.array`` holds an ordered list. Each element is **one** value: a single
int, float or word. ``append 0 4 7`` adds three elements, and ``getvalue``
sends them back as the list ``0 4 7``. Because every element is a single
token, an array and the list it spells always match. A value with inner
structure belongs in a ``.dict``.

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Message
     - Effect
   * - ``append <value…>``
     - add elements at the end
   * - ``insert <index> <value…>``
     - add elements before a position
   * - ``set <index> <value>``
     - replace one element
   * - ``get <index>``
     - send one element out of outlet 0, or bang outlet 2 if the index is
       out of range
   * - ``delete <index>``
     - remove one element and close the gap
   * - ``clear``
     - empty the array
   * - ``getsize``
     - send the length out of outlet 0
   * - ``getvalue``
     - send the whole array as a list out of outlet 0
   * - bang
     - send the reference ``array <name>`` out of outlet 1

Indices start at 0. An index outside the array, and any negative index, is
refused. It is never wrapped or clamped. ``.array.wrap`` and ``.array.rotate``
exist for the cases where you want wrapping.

``insert`` and ``delete`` move every element after the position. An object
that walks an array while another object writes to it can therefore see
elements shift under it.

``.array.deserialize`` fills an array from a JSON array such as
``[60,62.5,"kick"]``. Nested arrays and objects in the document are skipped,
because an element cannot hold them.

.. _data-limits:

Size limits
-----------

Every store has a fixed capacity. Its table is allocated in full when the
object is created and never grows, so no message has to allocate memory on
the audio thread.

.. list-table::
   :header-rows: 1
   :widths: 18 82

   * - Object
     - Capacity
   * - ``.coll``
     - 256 entries. A message is at most 256 characters, an address at
       most 64.
   * - ``.dict``
     - 256 entries. A key path is at most 128 characters (including the
       ``::`` separators), a value at most 256.
   * - ``.array``
     - 256 elements of at most 64 characters each.
   * - ``.bag``
     - 256 numbers.
   * - ``.table``
     - 1 to 4096 entries, set by the size argument (default 128). A larger
       size is clamped to 4096.
   * - ``.funbuff``
     - 256 pairs.
   * - ``.capture``
     - 1 to 512 items, set by the first argument (default 512). A word is at
       most 64 characters.
   * - ``.textfile``
     - 256 lines of at most 256 characters.
   * - ``.qlist``
     - 256 cue lines of at most 256 characters.
   * - ``.mtr``
     - 1 to 32 tracks, each with 256 events of at most 128 characters.
   * - ``.seq``
     - 4096 events. An event is one MIDI byte, so this is about 1365
       three-byte messages such as note-ons.

What happens at a limit
~~~~~~~~~~~~~~~~~~~~~~~

A message that does not fit is **refused whole**. A value is never cut
short, because half a message is a different message. The refused message
changes nothing and sends nothing. It is also **not logged**: the message
may be running on the audio thread, where writing a log line is not allowed.
If data goes missing, check it against the limits above.

``.capture`` is the one exception. It is a recorder, so when it is full the
oldest item is dropped to make room for the new one.

The same rule covers a store that is busy. Every store is protected by a
flag that is taken without waiting. Messages usually reach a store on the
audio thread, one at a time. But a message can also arrive on another
thread, for example from a millisecond ``.metro``, which runs on the
engine's timer thread (see :doc:`time`). If two threads touch the same store
at the same moment, the one that loses the flag does nothing at all rather
than wait.

Loading data from a file has its own rules: records past the limit are
dropped and the rest of the file still loads. See :doc:`files`.

.. _data-saved:

What is saved with the patch
----------------------------

``DumpJSON`` writes an object's creation arguments. Some stores also write
their **contents**, under the object's ``state`` key. The exact JSON of each
one is listed under *Saved contents* in :doc:`file_format`.

- ``.coll``, ``.dict``, ``.array`` and ``.qlist`` save their contents when
  they are not empty.
- ``.table`` saves its values unless it was sent ``embed 0``. The flag itself
  is always saved.
- ``.funbuff`` and ``.mtr`` save their contents only after ``embed 1``, as in
  Max.
- ``.bag``, ``.capture``, ``.textfile`` and ``.seq`` save no contents. The
  arguments are still saved, so a ``.textfile notes.txt`` or a
  ``.seq song.mid`` reads its file again when the patch loads (see
  :doc:`files`).
- Run-time positions are never saved: the read pointer of ``.coll`` and
  ``.funbuff``, the cursor and tempo of ``.qlist``, and the transport state of
  ``.mtr`` and ``.seq``.

For a shared store (``.coll``, ``.dict``, ``.array``), every object with the
name writes the contents, but only the object that **creates** the store
reads them back on load. So:

- several objects of one name in a file load the contents once, and
- a patch loaded while its store name is already in use in the engine joins
  the running store. The saved contents are not applied.

``.dict.serialize``, ``.dict.deserialize`` and ``.array.deserialize`` save
only their arguments. The contents belong to the store they are bound to.

Real-time behaviour
-------------------

Stores can be used freely from any message, including messages the audio
thread delivers:

- no message allocates memory, takes a lock or waits,
- the name is looked up once, on the control thread, when the object is
  created (or when the patcher is renamed),
- a store never holds its lock while it sends, so an outlet wired back into
  the same store works,
- the stores do no work of their own per audio block. They only react to
  messages.

The two steps that cannot run on the audio thread happen elsewhere. Parsing
JSON in ``.dict.deserialize`` and ``.array.deserialize`` runs on the
background pool, and the result is installed one block later. File access
runs on the background pool as well (see :doc:`files`). See
:doc:`realtime` for the patcher's overall rules.
