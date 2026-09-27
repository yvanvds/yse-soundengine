Subpatchers
===========

A subpatcher groups part of a patch into one object. The parent patch
connects to the group by pin number, the way it connects to any other object,
and does not need to know what is inside. Use subpatchers to keep a large
patch readable and to build a piece once (a voice, a filter bank, an
envelope) and create it again wherever it is needed. It works like Max's
``patcher`` object.

.. versionadded:: 3.0
   Subpatchers (issue `#545
   <https://github.com/yvanvds/yse-soundengine/issues/545>`_), and signal pins
   on their boundary (issue `#764
   <https://github.com/yvanvds/yse-soundengine/issues/764>`_).

The objects
-----------

Five object types are involved. :doc:`objects/subpatchers` has their full
reference.

.. list-table::
   :header-rows: 1
   :widths: 16 22 62

   * - Type
     - ``YSE::OBJ`` constant
     - What it is
   * - ``patcher``
     - ``PATCHER``
     - The subpatcher itself. It has no inlets, no outlets and no arguments
       of its own.
   * - ``.inlet``
     - ``G_INLET``
     - A message inlet of the subpatcher it is in. Bang, int, float and list
       pass through it unchanged.
   * - ``.outlet``
     - ``G_OUTLET``
     - A message outlet of the subpatcher it is in.
   * - ``~inlet``
     - ``D_INLET``
     - A signal inlet of the subpatcher it is in. Only audio passes; a message
       arriving on it is dropped, as on any signal inlet.
   * - ``~outlet``
     - ``D_OUTLET``
     - A signal outlet of the subpatcher it is in.

The four boundary objects take one argument, their **index**, which defaults
to 0. A subpatcher's inlet N is the ``.inlet`` or ``~inlet`` inside it with
index N. Outlet N is the ``.outlet`` or ``~outlet`` with index N. Messages and
signals share one numbering per side, so a ``.inlet 0`` next to a
``~inlet 1`` gives the subpatcher two inlets: a message inlet 0 and a signal
inlet 1.

Two boundary objects in one subpatcher that claim the same index are not an
error, but only one of them is used and which one is not defined. Give every
boundary object its own number.

.. versionchanged:: 3.0
   ``YSE::OBJ::D_OUT`` (``"~out"``) was removed (issue `#436
   <https://github.com/yvanvds/yse-soundengine/issues/436>`_). No object was
   ever registered under that name, so ``CreateObject`` always returned
   ``nullptr`` for it. Use ``~outlet`` for a signal that leaves a subpatcher
   and ``~dac`` for the audio output of the whole patch.

Putting objects inside
----------------------

A subpatcher is not a separate :cpp:class:`YSE::patcher`. You create its
contents with the same ``CreateObject`` on the same patcher, then place them
inside with ``SetContainer(object, subpatcher)``:

- ``SetContainer(obj, nullptr)`` moves an object back to the top level.
- ``GetContainer(obj)`` returns the subpatcher an object is directly inside,
  or ``nullptr`` at the top level.
- Subpatchers nest: pass a ``patcher`` object to ``SetContainer`` like any
  other object.

``SetContainer`` refuses the call, writes a log line and changes nothing when
the container is not a ``patcher`` object of this patcher, when the object
belongs to another patcher, or when the move would put a subpatcher inside
itself or inside one of its own subpatchers.

Moving an object changes no cords. Cords already drawn to or from it stay
where they are. Draw the cords after you have placed the objects, so that the
subpatcher's pin numbers resolve (see below).

Connecting to a subpatcher
--------------------------

Pass the subpatcher to ``Connect`` and ``Disconnect`` with a pin number, as
for any object. The patcher looks up the boundary object that claims that
number and draws the cord to it directly:

- ``Connect(source, 0, sub, 2)`` draws a cord from ``source`` to inlet 0 of
  the ``.inlet`` or ``~inlet`` with index 2 inside ``sub``.
- ``Connect(sub, 1, target, 0)`` draws a cord from outlet 0 of the
  ``.outlet`` or ``~outlet`` with index 1 inside ``sub``.
- When no boundary object in the subpatcher claims the number, the cord is
  refused with a log line, like any other invalid connection
  (:doc:`building`).

Only the subpatcher's **direct** contents count. A ``.inlet`` inside a nested
subpatcher is a pin of that nested subpatcher, not of the outer one.

After ``Connect`` returns, the cord is an ordinary cord between two ordinary
objects. That has some consequences:

- When you walk the graph, a cord drawn to a subpatcher reports the ID of the
  boundary object, not of the ``patcher`` object. ``DumpJSON`` saves it the
  same way (see :doc:`file_format`).
- Cords are attached to the boundary *object*, not to its index. Changing a
  boundary object's index later does not move cords that already exist.
- The patcher does not check that a cord respects the boundary. A cord drawn
  straight from an object inside a subpatcher to one outside it is accepted
  and works, but the parent cannot see it through the subpatcher's pins.
  Route through ``.outlet`` / ``~outlet`` to keep the group self-contained.

Crossing a boundary costs one pass-through object per boundary. A signal is
passed on by pointer, without a copy. The patcher stores every object in one
flat graph, whatever the nesting depth, so the audio thread's work does not
grow with nesting depth. Nothing about subpatchers is read on the audio
thread.

Reading a subpatcher's shape
----------------------------

A ``patcher`` object has no pins of its own, so ``pHandle::GetInputs()`` and
``GetOutputs()`` return 0 for it. Ask the patcher instead:

``SubpatcherInlets(sub)`` / ``SubpatcherOutlets(sub)``
   One more than the highest index claimed on that side, messages and
   signals together. A subpatcher whose only ``.inlet`` has index 2 reports 3
   inlets, and inlets 0 and 1 refuse a connection. For a handle that is not a
   subpatcher, the answer is 0.

Other per-pin queries on a subpatcher's handle behave differently depending
on the side:

- **Inlets resolve through the boundary.** ``pHandle::IsDSPInput(n)`` answers
  for the boundary object behind inlet n (true for a ``~inlet``), and
  ``SetBang``, ``SetIntData``, ``SetFloatData`` and ``SetListData`` deliver to
  it. A host can push a value into a subpatcher's inlet without knowing what
  is inside. For a pin no boundary object claims, ``IsDSPInput`` returns false
  and the setters do nothing.
- **Outlets do not.** ``pHandle::OutputDataType(n)`` returns
  ``OUT_TYPE::INVALID`` for every pin of a subpatcher (issue `#942
  <https://github.com/yvanvds/yse-soundengine/issues/942>`_). Ask the
  boundary object itself until this is fixed.

Worked example: a voice
-----------------------

This voice takes a MIDI note on inlet 0 and a level from 0 to 1 on inlet 1,
and sends its signal out of outlet 0. The code that builds it is a function,
so a patch can create as many voices as it needs:

.. code-block:: cpp

   //  inlet 0 (note)        inlet 1 (level)
   //    .inlet 0              .inlet 1
   //       |                     |
   //     .mtof                   |
   //       |                     |
   //     ~sine                   |
   //       |                     |
   //      ~* 0  <---------------+   (right inlet: gain)
   //       |
   //   ~outlet 0
   //  outlet 0 (signal)

   YSE::pHandle* CreateVoice(YSE::patcher& patch) {
     YSE::pHandle* voice = patch.CreateObject(YSE::OBJ::PATCHER);
     YSE::pHandle* pitch = patch.CreateObject(YSE::OBJ::G_INLET, "0");
     YSE::pHandle* level = patch.CreateObject(YSE::OBJ::G_INLET, "1");
     YSE::pHandle* mtof  = patch.CreateObject(".mtof");
     YSE::pHandle* osc   = patch.CreateObject("~sine");
     YSE::pHandle* amp   = patch.CreateObject("~*", "0");  // silent until a level arrives
     YSE::pHandle* out   = patch.CreateObject(YSE::OBJ::D_OUTLET, "0");

     for (YSE::pHandle* h : {pitch, level, mtof, osc, amp, out}) {
       patch.SetContainer(h, voice);
     }

     patch.Connect(pitch, 0, mtof, 0);
     patch.Connect(mtof, 0, osc, 0);
     patch.Connect(osc, 0, amp, 0);
     patch.Connect(level, 0, amp, 1);
     patch.Connect(amp, 0, out, 0);
     return voice;
   }

The parent patch wires the voice by its pin numbers only:

.. code-block:: cpp

   YSE::patcher patch;
   patch.create(2);

   YSE::pHandle* voice = CreateVoice(patch);
   YSE::pHandle* note  = patch.CreateObject(".r", "note");
   YSE::pHandle* level = patch.CreateObject(".r", "level");
   YSE::pHandle* dac   = patch.CreateObject(YSE::OBJ::D_DAC);

   patch.Connect(note, 0, voice, 0);
   patch.Connect(level, 0, voice, 1);
   patch.Connect(voice, 0, dac, 0);  // left
   patch.Connect(voice, 0, dac, 1);  // right

   patch.SubpatcherInlets(voice);    // 2
   patch.SubpatcherOutlets(voice);   // 1
   voice->GetInputs();               // 0: the pins belong to the boundary objects

   // Once the patcher is attached to a sound or an insert:
   patch.PassData(69.f, "note");     // through .r note into inlet 0: A4
   patch.PassData(0.5f, "level");    // into inlet 1: half level

   // The host can also skip the .r and push into the voice's pins directly:
   voice->SetFloatData(0, 72.f);

To build a bigger instrument, put voices inside another subpatcher. Call
``patch.SetContainer(voice, synth)`` where ``synth`` is a ``patcher`` object
with its own ``.inlet`` and ``~outlet`` objects, and wire those to the voice
inside it. The voice's pins work the same at any depth.

``DeleteObject(voice)`` removes the voice and everything inside it, including
nested subpatchers and their contents. Every object in the group gets the
same last chance to clean up that a single deleted object gets
(:doc:`building`).

Editing a subpatcher while it plays
-----------------------------------

An edit inside a subpatcher is an ordinary edit. ``CreateObject``,
``Connect``, ``Disconnect``, ``DeleteObject`` and ``SetParams`` on objects
inside a subpatcher publish a new graph snapshot exactly as they do at the
top level, and the rules in :doc:`building` apply unchanged.
``SetContainer`` itself changes no cord, so it publishes nothing.

Adding a boundary object is also an ordinary edit. Create a ``.inlet 2``
inside a subpatcher and the subpatcher has an inlet 2 from then on. The
``patcher`` object does not change.

**Renumbering a boundary object is delayed.** The index is a number argument,
so ``SetParams`` on a boundary object updates it in place, at the start of the
next block the patcher renders (see :doc:`building`). Until then,
``GetParams()`` and ``DumpJSON`` already report the new index, but
``Connect``, ``Disconnect``, the ``pHandle`` setters and
``SubpatcherInlets`` / ``SubpatcherOutlets`` still use the old one. On a
patcher that is not attached to anything, the new index never takes effect.
This is tracked in `#941
<https://github.com/yvanvds/yse-soundengine/issues/941>`_. Until it is fixed,
give a boundary object its final index when you create it, or delete it and
create a new one.

Loading and saving
------------------

``DumpJSON`` writes a subpatcher as an ordinary object record of type
``"patcher"``. Its contents are ordinary records that carry a ``container``
key with the subpatcher's ID. There is no nested document. The
``Subpatchers`` section of :doc:`file_format` describes the key, the order in
which ``ParseJSON`` restores it, and what it refuses. A nested patch loads
back into the same shape, and saving it again gives the same bytes.

On load, ``.loadbang`` and ``.loadmess`` inside a subpatcher fire in the
same pass as those at the top level, after the whole graph has been
published. There is no "inner patchers first" order. If one initialisation
has to happen before another, wire them through a ``.trigger``, which works
across a boundary like any other cord (see :doc:`messages`).

Names are not local
-------------------

A subpatcher does not have a name scope of its own. ``.s`` / ``.r`` and named
stores such as ``.value``, ``.coll`` and ``.array`` are scoped by the name of
the whole :cpp:class:`YSE::patcher`, whatever subpatcher they are in (see
:doc:`host_io`). If you create the same voice twice and each copy contains a
``.r freq``, both copies receive every ``freq`` message. There is no
equivalent of Max's ``#0`` per-instance prefix. To address one copy, send to
its inlets, or give each copy's names a different argument when you build it.

The same in C
-------------

The C API has one function for each C++ call on this page. Handles are
borrowed from the patcher, as elsewhere:

.. code-block:: c

   YsePHandle* voice = yse_patcher_create_object(p, "patcher", NULL);
   YsePHandle* pitch = yse_patcher_create_object(p, ".inlet", "0");
   YsePHandle* out   = yse_patcher_create_object(p, "~outlet", "0");
   /* ... the rest of the voice as in C++ ... */

   yse_patcher_set_container(p, pitch, voice);
   yse_patcher_set_container(p, out, voice);

   YsePHandle* note = yse_patcher_create_object(p, ".r", "note");
   yse_patcher_connect(p, note, 0, voice, 0);   /* lands on the .inlet */

   int ins  = yse_patcher_subpatcher_inlets(p, voice);
   int outs = yse_patcher_subpatcher_outlets(p, voice);
   YsePHandle* owner = yse_patcher_get_container(p, pitch);  /* voice */
   yse_phandle_set_float(voice, 0, 69.f);                     /* into inlet 0 */

``yse_patcher_set_container`` with ``NULL`` as the container moves an object
back to the top level. ``yse_phandle_get_inputs`` returns 0 for a subpatcher,
as in C++. :doc:`c_api` covers embedding a patcher through the C API.

Where to go next
----------------

- :doc:`building`: creating, connecting and deleting objects, and how edits
  are published.
- :doc:`file_format`: how subpatchers are saved.
- :doc:`objects/subpatchers`: the reference for ``patcher``, ``.inlet``,
  ``.outlet``, ``~inlet`` and ``~outlet``.
- :doc:`host_io`: send/receive and the named bus, and how names are scoped.
- :doc:`realtime`: how the flat graph reaches the audio thread.
