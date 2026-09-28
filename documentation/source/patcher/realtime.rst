Real-time model
===============

A patch is edited on one thread and rendered on another, and messages reach it
from several more. This page explains how that works without the audio thread
ever waiting: how edits are published, when the memory they replace is freed,
what happens at the start of each block, and which thread a message handler
really runs on. It ends with the rules that follow for code on the message
path, and for the host.

The pages on building, messages and the host routes already say what each
call does. This page is about where it runs and why it is safe.

The threads
-----------

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Thread
     - What it does to a patch
   * - **Control thread**
     - Any thread of yours that calls the patcher, ``pHandle`` or the C API:
       building and editing, ``PassData``, ``pHandle::Set*``, loading and
       saving, polling GUI values. There can be more than one.
   * - **Audio thread**
     - Renders the patcher one block of 128 samples at a time. This is the
       engine's audio callback, or a worker of the engine's render pool that
       renders a channel on the callback's behalf. Everything this page says
       about "the audio thread" applies to both.
   * - **Timer thread**
     - One engine thread that runs every millisecond ``.metro`` and
       ``.clocker`` (see :doc:`time`).
   * - **Background pool**
     - Frees retired graph memory (below), reads and writes files for
       ``.coll``, ``.textfile`` and the other file objects (see :doc:`files`),
       and opens MIDI ports.
   * - **MIDI input threads**
     - One per open port. They only copy incoming bytes into lock-free
       queues. The patch reads them on the audio thread (see :doc:`midi`).
   * - **Update thread**
     - The thread that calls ``System::update()`` /
       ``yse_system_update()``. It delivers bus values published from other
       threads and writes queued log lines (see :doc:`host_io`).

The audio thread never takes a lock the other threads hold, never allocates
and never waits for them. Everything below is built around that.

Edits: build a new graph, swap one pointer
------------------------------------------

The wiring of a patch lives in a **graph snapshot**, separate from the
objects. A snapshot lists every object, the targets of every outlet, which
inlets have a signal cord, the signal objects the render starts from, and the
``~dac`` and ``~adc`` objects. Once a snapshot is published, nothing changes it
again.

Every structural edit (``CreateObject``, ``DeleteObject``, ``Connect``,
``Disconnect``, ``Clear``, a ``SetParams`` that rebuilds an object, a
``ParseJSON``) does the same four things on the calling thread:

1. take the patcher's lock, which serialises edits from several control
   threads. The audio thread never takes this lock;
2. change the objects and cords;
3. compile a complete new snapshot. This allocates, which is fine here;
4. publish it by swapping one atomic pointer.

At the start of each block the audio thread reads that pointer once and keeps
the snapshot it found for the whole block. Every send, every "has this inlet
got a signal?" question and the output sum of that block use the same
snapshot. So:

- **A block is never half-edited.** It renders the graph from before an edit
  or the graph after it. ``ParseJSON`` builds the whole file into one snapshot
  and publishes once, so a load is one step too (see :doc:`file_format`).
- **Objects keep their state.** Objects are allocated once. A new snapshot
  points at the same objects with different cords, so an oscillator that the
  edit does not touch keeps its phase and a delay line keeps its contents.
- **Each call is published on its own.** A block can render between two
  calls. :doc:`building` explains how to order a group of edits so the
  in-between graph never sounds wrong.

.. rubric:: Freeing what an edit replaced

The old snapshot, and an object that ``DeleteObject`` removed, may still be
in use by a block that is rendering at that moment. So neither is freed at
once, and neither is ever freed on the audio thread. The edit tags them with
the current block count and hands them to a job on the background pool. The
job frees them once the audio thread has **started two more blocks**. By then
no block can still hold a pointer into them. The only thing the audio thread
does for this is add one to its block counter.

That count only says something about the thread rendering the block, so a
block's snapshot is only ever read by that thread. A send made on a control
thread (a host ``SetFloatData``, a ``.preset`` recall, a ``.metro`` tick),
whether a block is rendering or not, reads the *latest published* snapshot
instead: the cords as the last finished edit left them.

A control thread never reads the cords themselves, because another control
thread may be editing them: a ``DeleteObject`` or ``Disconnect`` rewrites them
under the patcher's lock, and a send cannot take that lock (it may need it
again further down). So a send marks itself as in use before it loads the
snapshot and drops the mark when its whole fan-out has returned. While any
thread holds that mark the job frees nothing, neither old snapshots nor
removed objects, so everything the send can reach stays allocated. An object
deleted while a send is under way can therefore still receive that send, just
as it can from a block that started before the delete. A ``.preset`` recall
sets the same mark before it looks objects up under the lock, so an object it
found cannot be freed before it is done with it. Setting the mark is one
atomic add, so it never waits and never involves the audio thread. What was
retired is freed by the first pass after the last mark is dropped.

When nothing renders (the engine is paused, or the patcher is not attached to
anything), the block count stands still and nothing retired is freed. The
memory is kept until rendering resumes and the next edit runs a new pass, or
until the patcher is destroyed. A patch that is edited many times while
nothing renders therefore grows until then.

Values from the host: queues, resolved at the block
---------------------------------------------------

Two kinds of host call do not change the wiring, and they reach the audio
thread through fixed-size lock-free queues instead of a new snapshot:

.. list-table::
   :header-rows: 1
   :widths: 30 20 50

   * - Call
     - Queue size
     - What rides the queue
   * - ``pHandle::SetParams`` on an object whose arguments are all numbers
     - 64
     - The parsed numbers. The audio thread writes them into the object.
   * - ``PassData`` / ``PassBang``
     - 256
     - The value and the receiver *name*, inline, up to 64 and 256
       characters.

Both queues are emptied at the start of every block. A queued item names its
target by name or by address, and the audio thread looks it up again in the
block's snapshot. An object that was deleted after the call is simply not in
that snapshot, so the item is dropped rather than delivered into freed memory.
A full queue drops the new item and logs it, on the control thread. See
:doc:`building` and :doc:`host_io` for the rest of these rules.

``pHandle::SetBang``, ``SetIntData``, ``SetFloatData`` and ``SetListData`` do
not use a queue. They run the object's handler at once, on the calling thread
(see below).

The start of a block
--------------------

Every block the patcher renders does these steps, in this order, on the audio
thread:

1. Read the published snapshot and keep it for the block. Add one to the block
   counter.
2. Apply queued ``SetParams`` numbers.
3. Deliver queued ``PassData`` / ``PassBang`` values to their ``.r``
   objects.
4. Give domain-clock bindings that have not found their clock yet another
   chance to find it (see :doc:`time`).
5. Deliver every deferred message that is due: ``.delay``, ``.pipe``,
   ``.line`` and the other block-clock and beat-clock objects.
6. Deliver finished file reads and writes (see :doc:`files`).
7. Run the **pollers** (below).
8. Clear every signal buffer, then render the signal graph from its start
   points. Signal objects pass buffers along cords, and each one computes as
   soon as all of its signal inputs have arrived.
9. Add every ``~dac`` into the patcher's output.
10. Let go of the snapshot.

Steps 2 to 7 run before the render, so everything they cause is heard in the
same block.

Pollers
~~~~~~~

Most control objects only run when a message reaches them. A few have no
inlet the outside world can push into, because their input arrives on another
thread. The MIDI input objects are the main case: RtMidi's thread puts events
in their queues, and nothing would ever read them. Such an object asks to be a
**poller**. The snapshot keeps a list of pollers, and step 7 calls each one
once per block, whether or not the patch sends it anything. A poller does what
any handler on the audio thread does: it drains its queue into its outlets
without allocating, locking or waiting.

Which thread a handler runs on
------------------------------

Messages between objects are synchronous: an outlet calls the next inlet's
handler directly (see :doc:`messages`). So a handler runs on whichever thread
started the chain it is part of:

.. list-table::
   :header-rows: 1
   :widths: 50 50

   * - Where the chain starts
     - Thread
   * - ``pHandle::Set*`` / ``yse_phandle_set_*``, ``.loadbang`` after a load,
       teardown messages when an object is deleted
     - the control thread that made the call
   * - ``PassData`` / ``PassBang`` (step 3)
     - the audio thread, at the next block
   * - Deferred messages, file completions, pollers (steps 5 to 7)
     - the audio thread
   * - A signal object sending a control message while it renders
     - the audio thread
   * - A millisecond ``.metro`` or ``.clocker``
     - the timer thread
   * - A value arriving at a ``.r`` over the named bus
     - the update thread

Two things follow.

**A message the host sends can be handled on the audio thread.** ``PassData``
returns at once, and the ``.r``, everything it is wired to, and any ``.s``
further down all run in the next block, on the audio thread. That is why a
``.s`` driven by ``PassData`` never reaches the send callback (see
:doc:`host_io`).

**One object can be reached from two threads at once.** A host
``pHandle::SetFloatData`` into an object can run while the audio thread
delivers a queued value to the same object. Objects with state that must stay
consistent protect it with a guard that never waits: the second thread's
message is dropped and counted (see `Refusals`_). Named stores that several
objects share are covered in :doc:`data`.

.. rubric:: The thread tag is not the thread

Internally each delivery carries a tag. ``T_DSP`` means "render this now, we
are inside the signal traversal". ``T_GUI`` means "set the state, and let the
block's own render use it". The drains in steps 3 to 7 deliver with
``T_GUI``, because that is the right *meaning* for them, but they run on the
audio thread. So an object must never read ``T_GUI`` as "I am on the control
thread and may lock or allocate". The engine asks the patcher which thread it
is really on (``patcherImplementation::CallingThread``) whenever it has to
choose between a lock-free route and a control-thread one. For example, a
``.s`` reached from a deferred delivery uses the lock-free, same-block route,
not the queue that ``PassData`` from the host uses.

.. versionchanged:: 3.0
   A ``.s`` or ``PassData`` reached from a deferred delivery used to take the
   patcher's lock, and could build a log string, on the audio thread, because
   the ``T_GUI`` tag was read as "control thread" (issue
   `#690 <https://github.com/yvanvds/yse-soundengine/issues/690>`_).

Rules for code on the message path
----------------------------------

Since almost any handler can run on the audio thread, every message handler,
poller and signal ``Calculate`` follows the audio thread's rules. It must not:

- allocate or free memory, including building a ``std::string`` that does not
  fit a buffer reserved in advance;
- take a lock, or wait for another thread in any other way;
- do file, socket or console I/O;
- write a log line. Formatting one allocates and the log may write a file.

What the engine's objects do instead:

- **Fixed capacity, reserved ahead.** Lists, text and tables live in storage
  sized when the object is created or its arguments change, which happens on
  the control thread. Input that does not fit is refused, not cut short (see
  :doc:`messages`).
- **Hand slow work to another thread.** File objects post a request that the
  background pool carries out, and pick up the result at step 6
  (see :doc:`files`). A MIDI output object opens its port on a background
  thread, and queues each message for a MIDI sender thread to send
  (see :doc:`midi`).
- **Guard, don't wait.** When two threads could reach the same state, the
  object uses a test-and-set flag. The thread that loses drops its message
  and counts it.
- **Log through the queue.** ``.print`` does not write to the log. It copies
  the line into a lock-free queue that the update thread empties (see
  :doc:`host_io`).

:doc:`extending` shows how a new object meets these rules.

Refusals
--------

Everything above has limits: queue sizes, list lengths, table slots. Whether
the engine tells you about a refusal by a log line or by a counter depends on
which thread refuses.

.. list-table::
   :header-rows: 1
   :widths: 28 30 42

   * - Refused on
     - Reported as
     - Examples
   * - the control thread only
     - a log line, straight away
     - a refused ``Connect`` or ``SetContainer``, a full ``PassData`` or
       ``SetParams`` queue, a receiver name or list too long for the queue,
       ``PassData`` to a name no ``.r`` has, a patcher name that is too long,
       too many creation arguments for a list object
   * - any thread, the audio thread included
     - a counter the object keeps, no log line
     - a list over 256 atoms at an inlet (``.zl``), a full deferred-message
       table, a full file-request table, a note ``.makenote`` cannot track,
       a message that loses a test-and-set guard, a ``.midiout`` message
       that finds the MIDI sender's queue full
   * - ``.print``
     - a count, logged at the next update
     - lines that do not fit the shared print queue

The counters are the ``Dropped()`` accessors on the engine's object classes
and schedulers. They only ever go up, and the test suite reads them. They are
not reachable through ``pHandle`` or the C API. From the host, a refusal on
the audio thread shows up as a message that had no effect.

What the host may do from where
-------------------------------

- **Call the patcher API only from control threads.** That covers
  ``patcher``, ``pHandle`` and the C API. Never from the audio callback, and never from a callback the engine runs on
  the audio thread. Edits take the patcher's lock and allocate.
- **Keep the patcher alive** while a sound plays it or an insert wraps it
  (see :doc:`index`).
- **The send callback never runs on the audio thread.** It runs on the
  control or timer thread that sent the message. Keep it short and thread
  safe. :doc:`host_io` and :doc:`c_api` have its install and destroy rules.
- **GUI reads are control-thread reads.** Polling a GUI value takes no lock
  and never makes the audio thread wait, but some reads clear what they
  report. See :doc:`gui`.
- **Queries see a moment, not a transaction.** ``Objects``,
  ``GetHandleFromList`` and the cord queries (``GetConnections``,
  ``GetConnectionTarget``, ``GetConnectionTargetInlet``) take the patcher's
  lock and answer each call on its own: a cord counted by one call can be
  gone by the next, which then answers the "no such" value. Coordinate object
  lifetime yourself if one thread edits while another walks the graph (see
  :doc:`building`).

Where to go next
----------------

- :doc:`building`: the edit calls, and how to order edits while audio runs.
- :doc:`messages`: synchronous dispatch, logical events and feedback limits.
- :doc:`time`: the block clock, the timer thread and domain clocks.
- :doc:`host_io`: the queue, the bus and the send callback from the host's
  side.
- :doc:`c_api`: the same model through the C API.
