Patch file format
=================

A patch is saved as one JSON document. ``patcher::DumpJSON()`` writes it and
``patcher::ParseJSON(text)`` reads it back. The C API has the same pair:

.. code-block:: c

   size_t len = yse_patcher_dump_json(p, NULL, 0);   /* length without the NUL */
   char* text = malloc(len + 1);
   yse_patcher_dump_json(p, text, len + 1);

   YseStatus st = yse_patcher_parse_json(q, text);
   if (st != YSE_OK) fprintf(stderr, "%s\n", yse_last_error());

``yse_patcher_dump_json`` works like ``snprintf``: it writes at most
``cap - 1`` bytes plus a terminator and returns the full length.
``yse_patcher_parse_json`` needs a patcher that ``yse_patcher_init`` has
set up, and returns ``YSE_ERR_EXCEPTION`` when the text cannot be loaded
(see `Errors`_).

This page describes the document: every key, what writes it, what the loader
does with it, and what the engine guarantees about the round trip. The file
stores what :doc:`building` builds: objects with their IDs and creation
arguments, and the cords between them.

An example
----------

This patch sends 60 through ``.mtof`` into a subpatcher that holds a
``~sine``. The subpatcher's signal outlet drives both channels of ``~dac``.
It was built with ``CreateObject`` / ``Connect`` / ``SetContainer``, named
``"demo"``, and saved with ``DumpJSON``:

.. code-block:: json

   {
     "name": "demo",
     "object 0": {
       "ID": 0,
       "outputs": {
         "output 0": {
           "0": { "Inlet": 0, "Object": 1 },
           "Count": 1
         }
       },
       "parms": "60",
       "type": ".loadmess"
     },
     "object 1": {
       "ID": 1,
       "outputs": {
         "output 0": {
           "0": { "Inlet": 0, "Object": 3 },
           "Count": 1
         }
       },
       "parms": "",
       "type": ".mtof"
     },
     "object 2": {
       "ID": 2,
       "gui": { "x": "120", "y": "40" },
       "parms": "",
       "type": "patcher"
     },
     "object 3": {
       "ID": 3,
       "container": 2,
       "outputs": {
         "output 0": {
           "0": { "Inlet": 0, "Object": 4 },
           "Count": 1
         }
       },
       "parms": "0",
       "type": ".inlet"
     },
     "object 4": {
       "ID": 4,
       "container": 2,
       "outputs": {
         "output 0": {
           "0": { "Inlet": 0, "Object": 5 },
           "Count": 1
         }
       },
       "parms": "440",
       "type": "~sine"
     },
     "object 5": {
       "ID": 5,
       "container": 2,
       "outputs": {
         "output 0": {
           "0": { "Inlet": 0, "Object": 6 },
           "1": { "Inlet": 1, "Object": 6 },
           "Count": 2
         }
       },
       "parms": "0",
       "type": "~outlet"
     },
     "object 6": {
       "ID": 6,
       "parms": "",
       "type": "~dac"
     }
   }

``DumpJSON`` puts every key on its own line with a two-space indent. The
inner cord records are folded onto one line here to save space. Otherwise
this is the engine's output, keys and order included.

Things to notice:

- The code connected ``.mtof`` to *the subpatcher*, inlet 0. The file
  records the cord where it really goes, to the ``.inlet`` object inside it
  (object 3). The same holds for the cords leaving the subpatcher, which
  start at its ``~outlet``. See :doc:`subpatchers`.
- ``~dac`` has no outlets, so it has no ``outputs`` key. The ``patcher``
  object has no pins of its own and no ``outputs`` either.
- Only the subpatcher has GUI properties, so only it has a ``gui`` key.

Top level
---------

The document is a JSON object. Its keys are:

``"name"`` (string, optional)
   The patcher's name, as set with ``patcher::name()``. It is written only
   when the host set one. The automatic ``patcher_<N>`` name is not saved,
   because it is a process-wide counter and not part of the patch. The name
   matters because it scopes the addresses of ``.s`` / ``.r`` and of shared
   stores, so a patch that talks across patchers only works under the name it
   was built with (see :doc:`host_io`).

``"object 0"``, ``"object 1"``, … (object, one per object)
   One record per object, described below. ``DumpJSON`` numbers the keys
   from 0 in ID order. The loader does not read the number in the key: any
   top-level key other than ``"name"`` is taken as an object record, and the
   record's ``"ID"`` is what identifies it.

A patcher with no objects and no name dumps as ``null``. ``ParseJSON``
accepts that, and ``{}``, as an empty patch.

The object record
-----------------

.. list-table::
   :header-rows: 1
   :widths: 14 16 12 58

   * - Key
     - Type
     - Required
     - Meaning
   * - ``ID``
     - integer
     - yes
     - The object's storage ID, ``pHandle::GetID()``. Cords in other records
       name their target by this number. It must be unique in the file.
   * - ``type``
     - string
     - yes
     - The type name, as passed to ``CreateObject``: ``"~sine"``,
       ``".coll"``, ``"patcher"``. :doc:`objects/index` lists them all.
   * - ``parms``
     - string
     - yes
     - The creation arguments, exactly as ``GetParams()`` returns them. An
       object without arguments writes ``""``. The rules from
       :doc:`building` apply on load: surplus arguments are ignored but kept
       verbatim, and any run of whitespace separates arguments.
   * - ``outputs``
     - object
     - no
     - The cords leaving the object, per outlet. See below. Written for every
       object with at least one outlet, including outlets with no cords.
   * - ``gui``
     - object of strings
     - no
     - The object's GUI properties (``pHandle::SetGuiProperty``). Keys and
       values are both strings. The engine stores them but never reads them,
       so an editor can put its own layout here (position, size, colour).
       Written only when the object has at least one.
   * - ``container``
     - integer
     - no
     - The ID of the ``patcher`` object this object sits in. Absent for
       objects at the top level. See `Subpatchers`_.
   * - ``state``
     - object
     - no
     - Contents the object holds beyond its creation arguments, such as the
       entries of a ``.coll``. Written only by the object types listed under
       `Saved contents`_, and only when they have something to save.

Optional keys are left out rather than written as ``null``. A patch that uses
no subpatchers and no stores therefore saves exactly as it did before those
keys existed.

Outlets and cords
~~~~~~~~~~~~~~~~~

``outputs`` has one key per outlet, ``"output 0"``, ``"output 1"`` and so on,
numbered from 0, left to right. Each holds:

``"Count"`` (integer)
   How many cords leave this outlet.

``"0"``, ``"1"``, … (object)
   One entry per cord, numbered from 0 up to ``Count - 1``. Each is
   ``{"Object": <target ID>, "Inlet": <target inlet>}``.

Cords are stored on the outlet side only. There is no inlet list, which is
also how the live patcher records them (see :doc:`building`).

The loader takes the outlet number from the key, so ``"output 10"`` is outlet
10 whatever order the keys come in. A key that does not read as
``output <number>`` is skipped. An outlet record without ``Count`` is skipped.
The cord entries are read by counting from 0 up to ``Count``, so the numbers
must be consecutive.

A cord whose target ID is not in the file is dropped. A cord that the live
``Connect`` would refuse (an outlet or inlet that does not exist, a second
signal source on a signal inlet, a duplicate cord) is refused the same way,
with a log line, and the rest of the patch loads.

Subpatchers
~~~~~~~~~~~

A subpatcher is an ordinary record of type ``"patcher"`` with no arguments.
Its contents are ordinary records at the top level of the file. What makes
them its contents is their ``container`` key. There is no nested document
and no list of members on the subpatcher itself, which matches how the engine
stores a subpatcher (see :doc:`subpatchers`). Deeper nesting works the same
way: an object's ``container`` names a ``patcher`` that has a ``container``
of its own.

The loader resolves ``container`` after it has created every object, so a
container may appear anywhere in the file, before or after its contents. It
refuses two things a file written by the engine cannot contain, logs them,
and leaves the object at the top level:

- a ``container`` that names an object that is not a ``patcher``;
- a containment cycle (a subpatcher that ends up inside itself).

A ``container`` that names an ID not in the file is ignored without a log
line, and the object stays at the top level.

Cords into and out of a subpatcher are stored against the boundary objects
(``.inlet``, ``~inlet``, ``.outlet``, ``~outlet``) inside it, as in the
example.

Saved contents
~~~~~~~~~~~~~~

Most objects are fully described by their type and arguments. The types
below also hold data that a patch builds up while it runs, and they save it
under ``state``. Each one writes nothing when it has nothing to save.

.. list-table::
   :header-rows: 1
   :widths: 16 44 40

   * - Type
     - ``state``
     - Written when
   * - ``.coll``
     - ``{"entries": [{"key": "1", "value": "60 64 67"}, …]}``. An entry
       also has ``"alias"`` when a symbol is associated with a numeric key.
     - the collection is not empty
   * - ``.dict``
     - ``{"contents": { … }}``, the dictionary as a nested JSON object
     - the dictionary is not empty
   * - ``.array``
     - ``{"contents": [ … ]}``, one JSON value per element
     - the array is not empty
   * - ``.table``
     - ``{"embed": true, "values": [0, 12, …]}``. ``embed`` is always
       written, and ``values`` only when it is ``true``.
     - always
   * - ``.funbuff``
     - ``{"embed": true, "pairs": [{"x": …, "y": …}, …]}``
     - the object's ``embed`` flag is on
   * - ``.mtr``
     - ``{"embed": true, "tracks": [[{"d": 250, "v": "60"}, {"d": 0, "b": true}], …]}``.
       One array per track. ``d`` is the delta in milliseconds, followed by
       either ``v`` (the message text) or ``b`` (a bang).
     - the object's ``embed`` flag is on
   * - ``.qlist``
     - ``{"cues": ["1 0.5", …]}``, one string per cue
     - the list is not empty
   * - ``.function``
     - ``{"points": [{"x": …, "y": …, "curve": …}, …]}``
     - it has points
   * - ``.preset``
     - ``{"active": 2, "slots": [{"slot": 0, "objects": [{"object": 3, "type": ".number", "value": "64"}]}]}``.
       ``object`` is the target's position in the file's own record order, so
       it still points at the right object after the load renumbers the
       patch.
     - a slot is filled or one is active

What each object does with these contents is described on its object page
and in :doc:`data` and :doc:`gui`. Two rules apply to all of them:

- ``.coll``, ``.dict`` and ``.array`` share their contents by name. Only the
  object that creates the store fills it on load, so several objects of one
  name in a file load the contents once. If a store of that name already
  exists in the engine when the patch loads, the patch joins it and the
  saved contents are not applied.
- A ``state`` key on a type that does not save anything is ignored.

.. note::

   A save made while one of these objects is busy with a message can
   currently leave its ``state`` out of the file without a warning. For
   ``.funbuff`` and ``.mtr`` that also turns ``embed`` off after reloading.
   This is tracked in `#940
   <https://github.com/yvanvds/yse-soundengine/issues/940>`_.

The round-trip guarantee
------------------------

**Loading a file into an empty patcher and saving it again produces the
same bytes.** A saved file is a fixed point of load-then-save. In
particular:

- **Records are written in ID order.** The ``N`` in ``"object N"`` is the
  position in that order, not the ID.
- **The loader creates objects in ID order.** It sorts the records by their
  ``"ID"`` rather than following the order the keys come out of the JSON
  object, which is alphabetical (``"object 10"`` before ``"object 2"``).
  Because an empty patcher numbers its objects from 0 in creation order (see
  :doc:`building`), each loaded object gets its position in the file as its
  new ID.
- **IDs with gaps are closed up on the first load.** A patch where objects
  were deleted and no new object took their number has gaps in its IDs, and
  ``DumpJSON`` writes them as they are: records ``"object 0"``,
  ``"object 1"`` with ``"ID": 0`` and ``"ID": 2``. Loading that file numbers
  the objects 0 and 1, so the next save differs from the first one in its IDs
  and cord targets, and from then on stays the same. A patch built without
  such gaps, including every patch loaded from a file, saves the same bytes
  every time.
- **Keys are written in alphabetical order**, which is why ``ID`` comes
  first (upper case sorts before lower case) and ``"Count"`` comes after the
  numbered cords.
- **Two patchers built by the same sequence of calls save the same file**, on
  any run and in any process. Nothing in the file depends on memory addresses
  or on other patchers.
- Non-ASCII characters in strings are written as ``\uXXXX`` escapes, so the
  file is plain ASCII.

The guarantee covers what the file describes: objects, arguments, cords,
nesting, GUI properties, saved contents and the name. It does not cover
values an object picked up from messages at run time and does not save,
such as the current value of a ``.number``, the phase of an oscillator, or a
live override of a random seed. Those start from the creation arguments
again. The object reference notes where an object behaves that way.

Loading
-------

``ParseJSON`` loads a file in this order:

1. Parse the text. Malformed JSON throws before anything changes.
2. Apply ``"name"``, if present (see below).
3. Sort the records by ``"ID"`` and create the objects in that order, with
   their arguments, GUI properties and ``state``.
4. Restore ``container`` membership.
5. Restore the cords.
6. Publish the finished graph to the audio thread in one step. The audio
   thread never renders a partly loaded patch.
7. Send ``.loadbang`` / ``.loadmess`` their load signal.

Loading adds to the patcher
~~~~~~~~~~~~~~~~~~~~~~~~~~~

``ParseJSON`` does not clear the patcher first. The loaded objects are added
next to whatever is already there, and get the smallest IDs the existing objects
do not hold (see :doc:`building`), so the IDs in the file are not kept. To replace a patch, call
``Clear()`` (``yse_patcher_clear``) before loading.

The saved ``"name"`` is applied only while the patcher still has its
automatic name. A name the host set before loading wins over the file. A
name that is not a string, or is longer than a patcher name may be, is
logged and ignored.

Files with old IDs
~~~~~~~~~~~~~~~~~~

The IDs in a file are only used to match cords and containers to records
while it loads. Any unique integers work. Patches saved before version 3.0
carry large, arbitrary IDs from a process-wide counter, for example:

.. code-block:: json

   {
     "object 0": { "ID": 35798, "type": ".mtof", "parms": "" },
     "object 1": {
       "ID": 35777, "type": ".loadmess", "parms": "60",
       "outputs": { "output 0": { "Count": 1, "0": { "Object": 35798, "Inlet": 0 } } }
     }
   }

This loads correctly. The objects are created in ID order (the ``.loadmess``
first, because 35777 < 35798), so they are numbered 0 and 1, and the next
``DumpJSON`` writes the patch with the new numbers. The same applies to a
hand-written file: pick any unique IDs, and the engine renumbers them from
0.

``.loadbang`` and ``.loadmess``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``.loadbang`` sends a bang and ``.loadmess`` sends its arguments when the
patch they are in has finished loading. "Finished" means step 7 above: after
every object has been created, every cord drawn and the whole graph
published. At that point the patch is the patch the file describes, and the
messages travel down cords that exist.

- **Once per load, and only for the objects this load created.** Loading a
  second file into a patcher does not fire the ``.loadbang`` objects that
  were already there.
- **Not on** ``CreateObject``. A ``.loadbang`` added to a running patch stays
  silent until the patch is saved and loaded again. Send it a bang to fire it
  by hand, which works on both objects, as in Max.
- **Subpatchers change nothing.** Objects inside a subpatcher fire in the
  same pass as the top level. There is no "inner patchers first" rule,
  because the whole tree is published at the same instant.
- **The order between two load objects is not defined.** If one
  initialisation must come before another, use one ``.loadbang`` and a
  ``.trigger``.
- The messages are sent on the thread that called ``ParseJSON``, before it
  returns. The control objects they reach have their new values when
  ``ParseJSON`` returns. Signal objects pick up a value at the next block
  they render.

Errors
~~~~~~

``ParseJSON`` throws (a ``nlohmann::json`` exception, or
``std::invalid_argument`` / ``std::out_of_range`` from an object's
arguments) when:

- the text is not valid JSON;
- a record has no ``ID``, ``type`` or ``parms``, or one of them has the wrong
  type;
- a ``gui`` value is not a string, or a ``container``, ``Count``, ``Object``
  or ``Inlet`` is not an integer;
- a cord entry numbered below ``Count`` is missing;
- a creation argument does not parse as the number the object expects.

Through the C API these come back as ``YSE_ERR_EXCEPTION`` with the message
in ``yse_last_error()``. No exception crosses the C boundary.

A load is all-or-nothing. When ``ParseJSON`` throws, the patcher is left
exactly as it was before the call: every object the load had already created
is removed again, the objects that were there before are untouched and keep
their IDs, a ``"name"`` the file applied is taken back, and no ``.loadbang``
fires. The audio thread never sees any of it. You do not need to ``Clear()``
after a failed load.

These are **not** errors. The loader logs them and carries on:

- An unknown ``type``. That object is skipped, along with every cord to or
  from it.
- A cord the live ``Connect`` would refuse, a cord to an ID not in the file,
  or an ``outputs`` key that is not ``output <number>``.
- A ``container`` that is not a ``patcher`` or would create a cycle.
- A ``"name"`` that is not a string or is too long.
- Unknown keys, in a record or in ``state``. They are ignored, and the next
  ``DumpJSON`` does not write them back.

Compatibility
-------------

The format has no version number. It changes only by adding optional keys,
so older files keep loading:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Added
     - Change
   * - ``state``
     - Saved contents for ``.coll`` (`#494
       <https://github.com/yvanvds/yse-soundengine/issues/494>`_), later for
       the other types in the table above.
   * - ``container``
     - Subpatchers (`#545
       <https://github.com/yvanvds/yse-soundengine/issues/545>`_,
       signal boundary objects in `#764
       <https://github.com/yvanvds/yse-soundengine/issues/764>`_).
   * - ``"name"``
     - The patcher name (`#897
       <https://github.com/yvanvds/yse-soundengine/issues/897>`_).

.. versionchanged:: 3.0
   IDs are counted per patcher from 0, records are written and loaded in ID
   order, and outlets are matched by the number in their key (issues
   `#730 <https://github.com/yvanvds/yse-soundengine/issues/730>`_ and
   `#734 <https://github.com/yvanvds/yse-soundengine/issues/734>`_). Before,
   a patch with more than ten objects, or an object with more than ten
   outlets, could load with its objects or cords in the wrong places, and two
   saves of the same patch did not match. Files written by earlier versions
   still load, and are renumbered from 0 when saved again.

An older engine reading a newer file ignores keys it does not know, so a
patch with subpatchers loads flat and a ``.coll`` loads empty. An older
engine reading a file with a top-level ``"name"`` key fails, because before
#897 it took every top-level key as an object record.

Writing a patch by hand
-----------------------

A minimal hand-written patch needs only ``ID``, ``type`` and ``parms`` on
each record, plus ``outputs`` where there are cords:

.. code-block:: json

   {
     "object 0": {
       "ID": 0, "type": "~sine", "parms": "220",
       "outputs": { "output 0": { "Count": 2,
         "0": { "Object": 1, "Inlet": 0 },
         "1": { "Object": 1, "Inlet": 1 } } }
     },
     "object 1": { "ID": 1, "type": "~dac", "parms": "" }
   }

Load it into a patcher created with two channels, and it plays a 220 Hz sine
on both. Check the pin numbers against :doc:`objects/index`. Keep in mind:

- Pass ``~dac`` and ``~adc`` an empty argument string. They take their
  channel count from ``patcher::create``, not from the file, so a patch
  saved from a stereo patcher and loaded into a mono one loses its cords to
  inlet 1.
- Load and save once to get the canonical form, with keys sorted and IDs
  counted from 0.
