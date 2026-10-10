Messages and dispatch
=====================

Control objects talk to each other with **messages**. This page covers what
a message can be, which inlets act on one, and in what order a message and
everything it causes travel through the graph. Every patch depends on these
rules. Most of them match Max, and this page notes where the patcher
differs.

Message types
-------------

A cord carries one of these kinds of message. The ``YSE::OUT_TYPE`` of an
outlet (``pHandle::OutputDataType``) tells you which kind it sends:

``BANG``
   A bare "do it now" with no value.

``INT`` / ``FLOAT``
   One number. The patcher keeps the two types apart, and most number inlets
   accept both.

``LIST``
   Text. Every list, symbol and word-plus-arguments message (``note 60 100``,
   ``stop``, ``1 2 3``) travels as one string whose atoms are separated by
   whitespace. There is no separate symbol type: a symbol is a one-token list.

``ANY``
   Used by an outlet that can send more than one of the kinds above. The
   rightmost outlet of ``.sel``, for example, passes on whatever arrived in
   its own type.

``BUFFER``
   A block of audio samples. Only signal (``~``) objects send these, once per
   audio block. See :doc:`index` for the difference between signal and control
   objects.

Each inlet registers a handler for each kind it accepts. The object
reference (:doc:`objects/index`) lists them in the *Accepts* column. The
patcher does **not** convert at an inlet. A message the inlet has no handler
for is dropped without a trace, and the object does nothing. ``Connect`` does
not check types either. It will draw a cord from a ``LIST`` outlet to an
inlet that only takes floats, and nothing is delivered along it.

How numbers are spelled inside lists
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Objects that read lists split the text on whitespace, and they all agree on
what a token is:

- A token is a **number** only when the whole token reads as one finite
  number. ``5abc``, ``inf`` and ``nan`` are symbols.
- A number is a **float** when it is spelled with a ``.`` or an exponent
  (``5.``, ``0.5``, ``1e3``), and an **int** otherwise. When an object sends
  a list back out, it keeps that spelling, so ``1 2 3`` does not come back as
  ``1. 2. 3.``.
- When a list object (``.zl``, ``.unpack``, ``.route`` and the like) ends up
  sending a *single* atom, it sends it as the int, float or one-token list it
  spells, not as a one-element list. That way the result can
  still reach an inlet that only takes numbers.

List length limits
~~~~~~~~~~~~~~~~~~

List objects (``.zl``, ``.pack``, ``.unpack``, ``.join``, ``.iter``, the
``.array`` family and others) work on a fixed-size, pre-allocated list. This
keeps them from allocating when a message arrives on the audio thread. The
limits are:

- at most **256 atoms**, Max's default for ``zl``. An object can lower its own
  limit (``.zl``'s ``zlmaxsize``) but cannot raise it;
- at most **1024 characters** of atom text in total.

When a list goes over a limit, the atoms that do not fit are **refused**,
not cut short. The atoms that already fit are kept. So a list that is too
long loses its tail, and the object records the refusal. From an inlet it
increments a counter (``.zl`` exposes it as ``Dropped()``) because it cannot
log from the audio thread. When the list comes from a creation argument, which
is parsed on the control thread, it writes a log line. Many objects whose
port count comes from their arguments (``.sel``, ``.trigger``, ``.bondo``)
use the same limit of 256.

Hot and cold inlets
-------------------

Each inlet is either **hot** or **cold**:

- A **hot** inlet stores the value and then makes the object compute and
  send its result.
- A **cold** inlet only stores the value. Nothing leaves the object until
  something reaches a hot inlet.

Unless the object reference says otherwise, the leftmost inlet (inlet 0) is
hot and every other inlet is cold. ``.+`` is the typical case:

.. code-block:: cpp

   YSE::pHandle* add = patch.CreateObject(".+");
   add->SetFloatData(1, 3.f);  // cold: stores 3, sends nothing
   add->SetFloatData(0, 4.f);  // hot: sends 7
   add->SetBang(0);            // .+ does not accept a bang: dropped, nothing sent
   add->SetFloatData(0, 1.f);  // hot again: sends 4, the stored 3 is still there

Objects can differ. ``.bondo`` makes **every** inlet hot, because releasing
the whole set whenever any one of its values changes is the point of that
object. The object reference describes each inlet's role. Words like "fires"
or "stored until" in a description tell you whether it is hot or cold.

Signal objects have one more rule. When a host sends a number to a signal
object's hot inlet (for example the frequency inlet of ``~sine``), the value
is stored and takes effect in the next audio block. Signal objects only
compute while a block is rendering.

Dispatch is synchronous and depth first
---------------------------------------

The patcher has no message queue between objects. When an object sends a
message, ``outlet::Send*`` calls the connected inlets' handlers directly, one
after another. Each of those handlers runs to completion, including every
message it sends in turn, before the outlet moves on to the next cord. The
whole cascade happens inside the call that started it, on the thread that
started it:

- A host call such as ``pHandle::SetFloatData`` returns only after
  everything that value caused has run.
- A branch that is still running cannot be interrupted by another branch.
  The second one waits until the first has finished completely.

Two consequences follow:

**The order of cords from one outlet is not guaranteed.** When one outlet
feeds several inlets, the patcher does not promise which one is served first,
and neither does Max. Do not build a patch that depends on it. Use
``.trigger`` instead.

**Feedback loops are cut off, not queued.** A cord path that leads back to
where it started (directly, or through ``.s`` / ``.r``) would recurse
forever. Instead, the patcher stops passing a message on once a chain of sends
is 64 deep on one thread, and the rest of that fan-out is dropped. This keeps a
wiring mistake from crashing the audio thread. It is not a way to build
loops. To repeat something, use ``.uzi`` or a timed object (see
:doc:`time`).

Right to left, and .trigger
---------------------------

Every object that sends from more than one outlet for one input (``.trigger``,
``.bangbang``, ``.bondo``, ``.unpack``, ``.uzi`` and others) sends from its
**rightmost outlet first** and its leftmost outlet last. That is Max's rule.
Combined with depth-first dispatch, it gives a firm guarantee: everything
connected to the right outlet has finished before the left outlet sends
anything.

This order is designed to work together with hot and cold inlets. Connect
right outlets to cold inlets and the left outlet to the hot inlet, and the
values arrive before the hot inlet makes the object compute.

``.trigger`` exists only to make this order explicit. Take a number that
must reach both inlets of ``.+`` so that the patch computes *x + x*. With
two cords from one outlet, the order is not defined. If the hot inlet happens
to be served first, ``.+`` adds the *previous* value. A ``.trigger i i`` fixes
the order:

.. code-block:: cpp

   YSE::pHandle* in   = patch.CreateObject(YSE::OBJ::G_RECEIVE, "x");  // ".r x"
   YSE::pHandle* trig = patch.CreateObject(".trigger", "i i");
   YSE::pHandle* add  = patch.CreateObject(".+");

   patch.Connect(in,   0, trig, 0);
   patch.Connect(trig, 1, add,  1);  // right outlet: sent first, into the cold inlet
   patch.Connect(trig, 0, add,  0);  // left outlet: sent last, into the hot inlet

   patch.PassData(5, "x");           // on the next block: .+ sends 10, never 5 + (old x)

``.trigger`` takes one argument per outlet and converts its input for each
outlet: ``i`` (int), ``f`` (float), ``b`` (bang), ``l`` (list), ``s``
(text), or a constant that the outlet always sends. ``.trigger b i`` is the
common "store, then fire" pattern. The int goes to a cold inlet first, and
only then does the bang leave the left outlet to fire the object with the new
value. The full list of conversions is in :doc:`objects/control_flow`.

Logical events
--------------

Some objects need to know whether two messages came from **the same
stimulus**. A stimulus is one host call, one GUI action, one tick of a timed
object, or one incoming MIDI event. Max calls this a *logical event*: two
clicks on a bang are two events, but the two bangs of a ``bang, bang``
message box are one event, however long the patch takes to process them.

The patcher has no scheduler to group messages by, but synchronous dispatch
gives the same grouping. Everything a stimulus causes runs inside the call
that started it. Each inlet dispatch opens a *dispatch frame*. The outermost
frame on a thread starts a new event, and every frame nested inside it belongs
to that event. The engine exposes the current event as
``YSE::PATCHER::CurrentMessageEvent()`` in ``inlet.h``. It returns a 64-bit
id that is unique across threads and never reused, and it returns 0 when no
dispatch is in progress. So:

- two host calls are two events, however close together they come;
- the whole right-to-left fan-out of one ``.trigger`` is one event;
- two ticks of a ``.metro`` are two events.

Messages that are deferred, such as a ``.bondo`` release with a delay
argument or anything the patcher's deferred-message scheduler delivers later,
get their own fresh frame when they are delivered. The engine uses the
``messageEventScope`` guard in ``inlet.h`` for this. Such a message therefore
counts as one new event, not as several unrelated ones. Audio buffers do not
open a frame. A control message sent from a signal object's handler starts its
own event.

Two objects depend on this:

``.next``
   Sends a bang from its left outlet for the first message of an event, and
   from its right outlet for every later message of the same event. "Once
   per burst" is its main use. For example, it can reset a counter once per
   ``.coll`` dump without knowing how long the dump is:

   .. code-block:: cpp

      YSE::pHandle* trig = patch.CreateObject(".trigger", "b b b");
      YSE::pHandle* next = patch.CreateObject(".next");
      patch.Connect(trig, 0, next, 0);
      patch.Connect(trig, 1, next, 0);
      patch.Connect(trig, 2, next, 0);

      trig->SetBang(0);  // .next: one bang left (first), then two bangs right
      trig->SetBang(0);  // a new host call is a new event: again one left, two right

   ``.next`` measures no time. Two messages a microsecond apart belong to
   different events if they came from two different stimuli.

``.bondo``
   Releases its whole set of stored values inside the call frame of the
   message that triggered the release. The release is therefore one logical
   event, and a ``.next`` downstream of it sees one separated message and the
   rest continued. With a delay argument the release is deferred, and it
   still arrives as one fresh event.

Selector matching: .sel, .route and .routepass
----------------------------------------------

Three objects branch on what a message *is*, and they use one shared matcher
so that swapping one for another does not change which messages match.

Each creation argument is a **selector**, and it is classified once, when
the object is created:

- An argument that reads as one whole finite number is a **numeric**
  selector. It matches by value, with an int widened to a float, so ``5``
  matches the int 5 and the float 5.0.
- Anything else is a **symbol** selector and matches by exact text.
- A numeric selector never matches a symbol, and a symbol selector never
  matches a number.

Each incoming message is matched on **one item**:

- an int or float: the number itself;
- a bang: the symbol ``bang``;
- a list: its **first token only**, which counts as a number if it reads as
  one and as a symbol otherwise.

The comparison is exact. A computed float can miss a selector it looks equal
to: 0.1 + 0.2 is not 0.3 in binary floating point, so put a ``.round`` in
front if that matters. A NaN matches nothing. If the same selector appears
twice, only the leftmost of its outlets is used. Exactly one outlet fires per
message. A message that matches no selector leaves the **rightmost** outlet
unchanged and in its own type, so you can chain several of these objects by
connecting each rightmost outlet to the next object's inlet.

The three objects differ in what they send when a message matches:

.. list-table::
   :header-rows: 1
   :widths: 16 42 42

   * - Object
     - On a match, the matching outlet sends
     - With no arguments
   * - ``.sel``
     - A bang.
     - Matches the number 0 (two outlets).
   * - ``.route``
     - The rest of the message, with the matched item removed.
     - Matches the number 0 (two outlets).
   * - ``.routepass``
     - The whole message, unchanged.
     - No selectors. Everything leaves the one outlet.

``.sel`` with exactly one numeric argument also gets a cold right inlet. A
number sent there silently replaces the selector's value.

What .route sends after removing the matched item depends on what is left:

.. code-block:: text

   .route note
     "note 60 100"  ->  outlet 0: the list "60 100"
     "note 60"      ->  outlet 0: the int 60
     "note 0.5"     ->  outlet 0: the float 0.5
     "note C4"      ->  outlet 0: the one-token list "C4"
     "note"         ->  outlet 0: bang
     "ctl 7 64"     ->  outlet 1: "ctl 7 64", unchanged
   .route 5
     5  or  5.0     ->  outlet 0: bang   (the number was the whole message)

.. versionchanged:: 3.0
   ``.route`` now removes the matched item, as Max's ``route`` does (issue
   #672). Before this change it forwarded the whole message, which is what
   Max's ``routepass`` does. If a saved patch relied on the old behaviour,
   replace that ``.route`` with ``.routepass``. The same change fixed float
   matching: previously ``.route 5`` never matched the float 5.0.

.. versionchanged:: 3.0
   A ``.route`` with no arguments now has two outlets and matches the number
   0, like ``.sel`` and Max's ``route`` (issue #679). Before this change it
   had no outlets and dropped every message.

The message box
---------------

When banged, ``.m`` sends its text as the message it spells, as a Max
message box does. Each receiving inlet reads the text by the same rules as
the list objects above:

- one number is an **int** or a **float**, by its spelling (``60``, ``0.5``);
- the single word ``bang`` is a **bang**;
- text that starts with a number is a **list** (``1 2 3``);
- text that starts with any other word (``stop``, ``note 60 100``) is a
  *command*. The objects that take commands get it as one: ``~line``
  (``stop``), ``.midiout`` (``allnotesoff`` and the other control words),
  ``.midiinfo``, ``.loadmess`` (``set ...``), ``.print``, ``.m`` and ``.l``.
  Every other object gets it as a list, which is how ``.route`` and ``.sel``
  see it.

So a ``.m 60`` wired into ``.mtof`` sends the frequency of note 60, and a
``.m note 60`` into ``.route note`` sends the int 60. A message that the
inlet has no handler for is dropped like any other, and the object does not
compute. The one exception is an object that takes commands: a message its
inlet has no handler for reaches it as a command instead. That is how a
number still reaches ``.m`` and ``.l``, which have no number handler:
``.m 60`` into a ``.m`` stores ``60``.

.. versionchanged:: 3.0
   ``.m`` used to hand its text only to the seven objects above, and a
   cord from it to any other object's hot inlet made that object compute with
   the value it already held: ``.m 60`` into ``.mtof`` sent 8.18 Hz (issue
   `#933 <https://github.com/yvanvds/yse-soundengine/issues/933>`_).

Where to go next
----------------

- :doc:`building`: creating and connecting objects, and editing a graph while
  it plays.
- :doc:`host_io`: getting messages into and out of a patch from the host.
- :doc:`time`: the timed objects whose ticks are separate events.
- :doc:`subpatchers`: how messages cross a subpatcher's boundary.
- :doc:`objects/index`: every object's inlets, outlets and accepted types.
