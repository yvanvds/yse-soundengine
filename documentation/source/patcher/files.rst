File input and output
=====================

Several patcher objects can load their contents from a file and save them to
one: ``.coll``, ``.textfile``, ``.qlist``, ``.mtr``, ``.seq``, and the
dictionary pair ``.dict.serialize`` / ``.dict.deserialize``. They all use one
mechanism, so they all behave the same way: a ``read`` or ``write`` never
touches the disk inside the message. It is a request, and the result arrives
a block or more later.

This page explains that model, its limits and its failure behaviour, and then
lists what each object reads and writes. The stores themselves are described
in :doc:`data`.

Why file access is asynchronous
-------------------------------

A message handler runs on whichever thread delivered the message. Inside a
patch that is usually the audio thread (see :doc:`messages`). Opening a file
there would block the audio callback and cause a dropout. A ``read`` that
arrives on another thread this time may arrive on the audio thread the next
time. So every file-capable object treats every ``read`` and ``write`` as if
it were on the audio thread, and hands the disk work to a background thread.

How a request travels
---------------------

1. **The request.** ``read notes.txt`` claims one of the patcher's file slots
   and copies the path into it. For ``write``, the object first builds the
   file's text and copies it into the slot too, so the object may change or
   even be deleted afterwards. Claiming a slot does not wait, lock or
   allocate, so this is safe on any thread.
2. **The disk work.** The engine's background thread pool (the one that also
   loads sound files) reads or writes the file.
3. **The delivery.** At the start of the next block the patcher renders,
   after the deferred messages that are due (see :doc:`time`), each finished
   request is handed back to the object that asked. A read replaces the
   object's contents and then bangs its *file* outlet. Everything this
   triggers counts as one new logical event, as if the file had been the
   stimulus (see :doc:`messages`).

Some consequences:

- **Nothing arrives inside the message.** In Max, ``read`` followed by
  ``bang`` plays the file just read. Here the ``bang`` would reach the old
  contents. Wire the object's *file* outlet to whatever should happen next.
  ``.mtr`` and ``.seq`` have a *file* outlet that Max does not, for exactly
  this reason.
- **Results arrive only while the patcher renders.** A patcher that is not
  attached to a sound or an insert (see :doc:`index`) keeps its finished
  requests until it renders.
- **Deleting the object cancels the delivery.** If the requesting object is
  deleted or replaced by an edit before the result arrives, the result is
  dropped. The background work itself cannot be cancelled, so a ``write``
  still reaches the disk.
- **The file outlet is always the last outlet.** It was added to objects that
  already existed, so it comes after all their other outlets. This keeps the
  cords of older saved patches in place.

Limits
------

The file slots belong to the patcher and are shared by all its objects. The
patcher allocates them (about half a megabyte) the first time a
file-capable object joins it.

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Limit
     - Value
   * - Requests in progress at once, per patcher
     - 4
   * - Longest path
     - 511 characters
   * - Largest file that can be read or written
     - 128 KiB (131072 bytes)

A request is **refused** when all four slots are busy, when the path is
empty or too long, or when the text to write is larger than 128 KiB. A
refused request does nothing at all. A file larger than 128 KiB is refused
whole on reading. It is never cut short, because half a collection is a
different collection. ``.coll`` at its full size of 256 entries writes less
than 100 KiB, so this limit is only reached by files written elsewhere.

When a file does fit, the object's own capacity still applies (see
:ref:`data-limits`). Records past it are dropped, and a record that is too
long is skipped, but the rest of the file loads.

Where paths point
-----------------

- An **absolute** path is used as given.
- A **relative** path is relative to the process's current working
  directory, not to the location of the patch file.
- When the host has installed a **custom file reader** with ``YSE::IO()``
  (for example to read from packed assets), reads go through it and the path
  is passed to it unchanged. That layer can only read, so every ``write``
  fails while it is active.

When something goes wrong
-------------------------

Every failure is reported the same way, whatever the cause:

- the file does not exist or cannot be opened,
- the file is larger than 128 KiB,
- a ``write`` while a custom file reader is installed,
- the request was refused (see *Limits*).

**A failed read changes nothing.** The object keeps its current contents and
its *file* outlet stays silent. Nothing is logged, because the failure is
handled on the audio thread. If a *file* outlet never bangs, check the path
(relative to the working directory), the size, and whether a custom file
reader is installed.

**A write has no success signal.** None of these objects has an outlet that
reports a finished write, as in Max. A failed write is silent too.

``.dict.deserialize`` and ``.seq`` also check what they read. A document
that is not valid JSON, or a file that is not a MIDI sequence, is a failed
read.

File names and bare messages
----------------------------

A headless patcher has no file dialog. So a bare ``read`` or ``write``
without a name reuses the last name the object was given. If it has never
been given one, it does nothing.

``.textfile`` and ``.seq`` take a file name as their creation argument. The
object reads that file as soon as it joins a patcher, which is Max's "read
when the patch loads". The name is also the default for a bare ``read`` or
``write``, so a bare ``write`` saves back over the same file. The other
objects have no file-name argument.

Formats, object by object
-------------------------

.. list-table::
   :header-rows: 1
   :widths: 20 20 40 20

   * - Object
     - Messages
     - Format
     - File outlet
   * - ``.coll``
     - ``read``, ``readagain``, ``write``, ``writeagain``
     - Max's coll text format
     - outlet 3
   * - ``.textfile``
     - ``read``, ``write``
     - plain text, one stored line per line
     - outlet 2
   * - ``.qlist``
     - ``read``, ``write``
     - Max's qlist cue-list text
     - outlet 2
   * - ``.mtr``
     - ``read``, ``write``, in inlet 0 or a track inlet
     - Max's mtr text format
     - the last outlet
   * - ``.seq``
     - ``read``, ``write [file] [format]``
     - read: Standard MIDI File or Max's seq text; write: Standard MIDI File
     - outlet 3
   * - ``.dict.serialize``
     - ``write``
     - JSON
     - none
   * - ``.dict.deserialize``
     - ``read``
     - JSON
     - outlet 0 sends the reference

``.table``, ``.funbuff``, ``.bag``, ``.capture`` and ``.array`` do not read or
write files. ``.table`` and ``.funbuff`` can save their contents with the
patch instead (see :ref:`data-saved`).

``.coll``
~~~~~~~~~

One record per line, ``<address>, <message>;``:

.. code-block:: text

   1, 60 64 67;
   2, 62 65 69;
   intro, 0 0 0;

An entry that has both a number and a symbol address (after ``assoc``)
writes both, the number first. A read accepts them in either order. A read
replaces the whole collection. ``readagain`` and ``writeagain`` repeat the
last read or write. ``filetype`` is accepted and does nothing.

A message that contains a comma or a semicolon cannot survive the round
trip, because those characters end the address and the record. This is the
same limitation as Max's.

``.textfile``
~~~~~~~~~~~~~

Plain text, one stored line per line of the file. The last line ends with a
newline only when it has been closed with ``cr``, so a write followed by a
read gives back exactly what was there. A carriage return at the end of a
line is dropped on reading, so files with Windows line endings load the same
lines. A read replaces all lines.

``.qlist``
~~~~~~~~~~

Max's cue-list format: one cue per line, each ending with a semicolon. On
reading, either a semicolon or a line break ends a cue, so hand-written files
without semicolons also load. A line of numbers followed by a message is split
into two cues, exactly as when it arrives on the inlet. A read replaces the
list and rewinds the cursor. Playback that is running continues into the new
list.

``.mtr``
~~~~~~~~

Max's text format. Each track is written as ``track <n>;``, then one
``<delta> <message>;`` per event, then ``end;``. A bang is written as the word
``bang``. ``read`` and ``write`` in inlet 0 cover all tracks. In a track's own
inlet they cover only that track, and a read there takes the first track in
the file. A read replaces the tracks it covers and stops their playback.

Max's ``.pat`` and JSON formats are not supported. To keep the recorded tracks
inside the patch, use ``embed 1`` instead (see :ref:`data-saved`).

``.seq``
~~~~~~~~

``read`` accepts a Standard MIDI File (format 0 or 1, up to 32 tracks, merged
into one sequence) or Max's seq text format (a time in milliseconds followed
by the bytes of the MIDI message). The file's tempo map is applied, and meta
events are kept in the sequence and sent out of the meta outlet during
playback. A read stops playback.

``write`` always produces a Standard MIDI File: format 0 by default, or format
1 when a non-zero format number follows the name
(``write song.mid 1``). A format-1 file has a first track with the meta events
and anything without a channel, then one track per MIDI channel in use.

``.seq`` saves nothing with the patch, so its file is the only place its
contents are kept.

Dictionaries as JSON
~~~~~~~~~~~~~~~~~~~~

``.dict.serialize <name>`` sends its dictionary as one line of compact JSON
out of its outlet. That message is limited to 255 characters, so a larger
dictionary is refused there. ``write <file>`` writes the same document to a
file instead, up to 128 KiB.

``.dict.deserialize <name>`` reads a JSON file with ``read <file>`` and
replaces the whole dictionary with it. The JSON is parsed on the background
pool as well, and the new contents are installed at the start of a later
block. Then ``dictionary <name>`` goes out of outlet 0, so the rest of
the ``.dict`` family can use the result. A document that fails to parse
changes nothing.

Both use the same nested JSON as a saved patch (see :doc:`data`), so a file
written by one is read back by the other.

Arrays have no file route. ``.array.deserialize`` takes a JSON array on its
inlet only, up to 255 characters.
