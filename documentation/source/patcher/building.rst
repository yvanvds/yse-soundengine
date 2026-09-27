Building and editing graphs
===========================

This page covers how a host builds a patch in code and changes it while it
plays: creating objects, drawing and removing cords, changing an object's
arguments, and how objects and cords are identified when an editor needs to
save, draw or walk the graph. The same calls work before the patcher is
attached to a sound and while it is rendering.

How messages move along the cords once they exist is the subject of
:doc:`messages`. Loading a whole graph from a file is covered in
:doc:`file_format`.

Creating objects
----------------

:cpp:class:`YSE::patcher` creates an object with ``CreateObject``. It takes a
type name and an optional argument string, and returns a
:cpp:class:`YSE::pHandle`:

.. code-block:: cpp

   YSE::patcher patch;
   patch.create(2);  // must come first: CreateObject returns nullptr before it

   YSE::pHandle* osc  = patch.CreateObject("~sine", "220");
   YSE::pHandle* amp  = patch.CreateObject("~*", "0.2");
   YSE::pHandle* gate = patch.CreateObject(YSE::OBJ::G_GATE, "3");
   YSE::pHandle* dac  = patch.CreateObject(YSE::OBJ::D_DAC);

The patcher owns the object and the handle. Never delete a handle yourself;
use ``DeleteObject`` (see below).

``CreateObject`` returns ``nullptr`` when the type name is unknown. Check
the result, or test the name first with the static
``patcher::IsValidObject``. ``Connect`` and ``Disconnect`` refuse a
``nullptr`` handle: they log an error and leave the graph unchanged.

Arguments
~~~~~~~~~

The argument string holds the object's creation arguments separated by
spaces. They are assigned in order to the parameters that the object
reference (:doc:`objects/index`) lists for that type, so ``"~sine 220"``
sets the frequency and ``".gate 3"`` gives the gate three outlets. The
rules:

- **Missing arguments** take the default shown in the object reference.
  ``CreateObject(".gate")`` has two outlets.
- **Surplus arguments** are ignored. The string is still stored exactly as
  you passed it, so ``GetParams()`` and ``DumpJSON`` return it unchanged and a
  saved patch does not get rewritten on load. This includes objects that take
  no arguments at all: ``.mean 5`` is a plain ``.mean``.
- **A number argument that does not read as a number** makes
  ``CreateObject`` throw ``std::invalid_argument`` (or
  ``std::out_of_range``). Nothing is added to the patcher. Through the C API,
  ``yse_patcher_create_object`` returns ``NULL`` and the reason is in
  ``yse_last_error()``.
- **Any run of whitespace separates arguments**, as in a list message:
  spaces, tabs and line breaks, one or several. ``"0  10"`` and ``"0\t10"``
  both mean ``0 10``, and leading or trailing whitespace is ignored. An
  argument string that is only whitespace is the same as ``""``. The string
  itself is still stored as you passed it.
- ``~dac`` and ``~adc`` ignore their arguments. Their channel count is
  always the one passed to ``patcher::create``.

``GetParams()`` returns the current argument string, and ``Type()`` returns
the type name.

Connecting and disconnecting
----------------------------

``Connect(from, outlet, to, inlet)`` draws a cord from one of ``from``'s
outlets to one of ``to``'s inlets. Pins are numbered from 0, left to right.
``Disconnect`` takes the same four arguments and removes that one cord:

.. code-block:: cpp

   patch.Connect(osc, 0, amp, 0);
   patch.Connect(amp, 0, dac, 0);     // left
   patch.Connect(amp, 0, dac, 1);     // right: one outlet can feed many inlets

   patch.Disconnect(amp, 0, dac, 1);  // back to the left channel only

Neither call returns a result. When a connection cannot be made, the
patcher writes a log line and leaves the graph as it was. That happens when:

- the outlet or inlet number does not exist on that object;
- the same cord already exists (drawing it twice does not double it);
- the inlet already has a **signal** cord. A signal inlet takes exactly one
  signal source, so to mix two signals, send them into the two inlets of a
  ``~+`` and connect its outlet instead. Message inlets accept any number of
  cords.

``Connect`` does not compare types. A cord from an outlet whose messages
the inlet cannot handle is drawn, but nothing arrives along it (see
:doc:`messages`). Use ``pHandle::IsDSPInput`` and
``pHandle::OutputDataType``, or the object reference, if an editor has to
prevent such cords.

``Disconnect`` for a cord that does not exist does nothing. Both handles must
be objects of the patcher you call the method on. A call with a handle from
another patcher is refused: it logs an error, and no cord is drawn or cut.

A subpatcher's pins are numbered by the boundary objects inside it. When you
pass a subpatcher to ``Connect``, the cord is drawn to that boundary object
directly. See :doc:`subpatchers`.

Deleting objects
----------------

``DeleteObject(handle)`` removes the object together with every cord to and
from it. The handle is invalid as soon as the call returns. Before the cords
are cut, the object gets a chance to finish what it started: a running
``.metro`` stops, and objects that hold MIDI notes send their note-offs down
the cords that are still there. Deleting a subpatcher deletes everything
inside it.

``Clear()`` removes every object the same way. ``DeleteObject(nullptr)``
does nothing.

Changing arguments while it plays
---------------------------------

``pHandle::SetParams(args)`` gives a live object a new argument string. What
happens depends on the object's parameters:

.. list-table::
   :header-rows: 1
   :widths: 22 39 39

   * -
     - Objects whose arguments are all numbers
     - Objects with a text argument, or whose arguments set their number of
       pins
   * - Examples
     - Every signal object (``~sine``, ``~lp``, ``~*`` …), ``.metro``,
       ``.clip``, ``.+``
     - ``.gate``, ``.switch``, ``.route``, ``.sel``, ``.trigger``, ``.r``,
       ``.s``, ``.coll``
   * - What happens
     - The new values are written into the running object.
     - A new object of the same type is built with the new arguments and
       swapped in for the old one.
   * - When it takes effect
     - At the start of the next audio block the patcher renders.
     - Immediately. ``GetInputs()`` and ``GetOutputs()`` already report the
       new pin counts when the call returns.
   * - Internal state
     - Kept. An oscillator keeps its phase and a filter its memory, so a
       frequency change does not click.
     - Starts fresh, as in Max. A ``.gate`` forgets which outlet was open, and
       anything the old object had scheduled is dropped.
   * - Cords
     - Unchanged.
     - Kept on every pin that still exists. Cords on pins that the new
       arguments removed are dropped.

In both cases the handle stays the same, the object keeps its ID, its GUI
properties and the subpatcher it is in, and ``GetParams()`` and
``DumpJSON`` return the new string as soon as the call returns.

.. code-block:: cpp

   osc->SetParams("330");    // in place: the next block plays 330 Hz, phase intact
   gate->SetParams("2");     // rebuilt: gate now has 2 outlets, same handle and ID
   // A cord that left gate's third outlet (outlet 2) is gone.

Some rules to keep in mind:

- **Arguments you leave out take their defaults.** Both kinds of object end
  up exactly as ``CreateObject`` would build them from the new string, so
  ``clip->SetParams("5")`` on a ``.clip 0 10`` sets the lower limit to 5 and
  resets the upper one to its default of 1. An empty string resets every
  argument. Because the object always matches its argument string, what
  ``DumpJSON`` saves reloads to the object that is playing. To change one
  argument and keep the others, pass them all again.
- **An argument that does not parse** throws on the calling thread, like
  ``CreateObject``, and leaves the object unchanged. The C API catches the
  exception and leaves the reason in ``yse_last_error()``.
- **Pin numbers can go stale.** After a rebuild the object may have fewer
  pins than before. Read ``GetOutputs()`` and ``GetInputs()`` again rather
  than reusing numbers from before the call. The edge queries below answer
  safely for a pin that no longer exists.
- **In-place updates travel in a queue** that the audio thread empties once
  per block. It holds 64 updates. If more arrive before the next block, for
  example because the patcher is not attached to anything yet, the extra ones
  are dropped with a log line while ``GetParams()`` already reports them.
  Changing a value on every GUI frame is well within this limit.

``SetParams`` changes an object's *creation arguments*. To change a value
the object also accepts on an inlet, such as the frequency of ``~sine``,
sending it a message is the more direct route (see :doc:`messages`).

Editing while audio runs
------------------------

Every call on this page is safe while the patcher renders, and none of them
makes the audio thread wait. Each edit builds a new copy of the graph's
wiring on the calling thread and publishes it in one step. The audio thread
picks it up at the start of its next block. A block always renders either
the graph from before an edit or the graph after it, never a half-edited
one. Objects that the edit does not touch keep running with their state
intact, so adding a branch to a playing patch does not disturb what is
already sounding. How this works is described in :doc:`realtime`.

Keep in mind that:

- **Each call is published on its own.** A block can render between two of
  your calls. If a sequence of edits only makes sense as a whole, build the
  new part first and draw the cord into the sounding graph (usually into
  ``~dac``) last.
- **Glitch-free does not mean faded.** Deleting an object or cutting a cord
  that carries sound stops that sound at the next block, as it does in Max.
  To remove a voice without a click, ramp it to zero first, for example with
  ``~line`` driving a ``~*``.
- **Edits and queries can come from several threads.** They are serialized
  on the patcher's lock, ``Objects``, ``GetHandleFromList`` and
  ``GetHandleFromID`` included. Each call is answered on its own, though: a
  walk over the object list can see an edit made between two of its calls,
  and a handle it returned is freed if another thread deletes that object.
  If you edit from one thread and read from another, coordinate object
  lifetime yourself. Never edit or query a patch from the audio callback.

Object IDs
----------

Every object has a storage ID, ``pHandle::GetID()``. This is the number
``DumpJSON`` writes for the object and for the cords that point at it, and
the number ``GetHandleFromID`` looks up.

- IDs belong to one patcher. Each patcher numbers its own objects **from
  0**, so two patchers built the same way have the same IDs, and the same
  patch saves the same way on every run.
- A new object gets the **smallest number no live object holds**. The IDs
  of a patch stay dense, and a deleted object's ID goes to the next object
  you create:

  .. code-block:: cpp

     YSE::pHandle* a = patch.CreateObject(".+");  // ID 0
     YSE::pHandle* b = patch.CreateObject(".+");  // ID 1
     YSE::pHandle* c = patch.CreateObject(".+");  // ID 2
     patch.DeleteObject(b);
     YSE::pHandle* d = patch.CreateObject(".+");  // ID 1 again

  After ``Clear()`` numbering starts at 0 again.
- A rebuild by ``SetParams`` keeps the ID.

Because IDs are reused, an ID names an object only while that object is
alive. An ID you stored before a delete can resolve to the object that
inherited the number, not to ``nullptr``. Hold on to the ``pHandle*`` when
you need a lasting reference, and use IDs for saving and for talking about a
patch.

.. versionchanged:: 3.0
   IDs are counted per patcher from 0 and reused after a delete (issues
   `#730 <https://github.com/yvanvds/yse-soundengine/issues/730>`_ and
   `#733 <https://github.com/yvanvds/yse-soundengine/issues/733>`_). Before,
   they came from one counter shared by the whole process that never went
   back, so a patch's IDs depended on everything else the program had built.
   A host that treated 0 as "no object" must now use the sentinels below.

Walking a graph
---------------

A host that draws a patch, or loads one with ``ParseJSON`` and needs to find
its objects, can walk it:

.. code-block:: cpp

   for (unsigned int i = 0; i < patch.Objects(); i++) {
     YSE::pHandle* h = patch.GetHandleFromList(i);
     for (int out = 0; out < h->GetOutputs(); out++) {
       for (unsigned int c = 0; c < h->GetConnections(out); c++) {
         unsigned int toID    = h->GetConnectionTarget(out, c);
         unsigned int toInlet = h->GetConnectionTargetInlet(out, c);
         std::printf("%u:%d -> %u:%u\n", h->GetID(), out, toID, toInlet);
       }
     }
   }

``GetHandleFromList`` returns the objects in **no particular order**. It
is not creation order or ID order, and it changes as objects come and go.
Sort by ``GetID()`` if the order matters. Index a list you build yourself,
because each ``GetHandleFromList`` call walks the patcher's object list
from the start.

.. versionchanged:: 3.0
   ``Objects``, ``GetHandleFromList`` and ``GetHandleFromID`` take the
   patcher's lock (`#937
   <https://github.com/yvanvds/yse-soundengine/issues/937>`_). Before, they
   read the object list without it, and a call that ran while another thread
   created or deleted an object could crash.

Cords are recorded on the outlet side only. To find the cords arriving at an
object, walk every object's outlets as above. A cord drawn to a subpatcher
reports the ID of the boundary object inside it (see :doc:`subpatchers`).

"No such" values
~~~~~~~~~~~~~~~~

0 is a real object ID and inlet 0 is the leftmost inlet, so neither can mean
"nothing here". The queries use these answers instead:

.. list-table::
   :header-rows: 1
   :widths: 34 33 33

   * - Query
     - C++
     - C
   * - ``GetConnectionTarget`` for a missing outlet or connection
     - ``UINT_MAX``
     - ``YSE_PATCHER_ID_NONE``
   * - ``GetConnectionTargetInlet`` for a missing outlet or connection
     - ``UINT_MAX``
     - ``YSE_PATCHER_INLET_NONE``
   * - ``GetID`` on a ``NULL`` handle
     - (not applicable)
     - ``YSE_PATCHER_ID_NONE``
   * - ``GetHandleFromID`` for an ID no object holds
     - ``nullptr``
     - ``NULL``
   * - ``GetConnections`` for a missing outlet
     - 0
     - 0

``YSE_PATCHER_ID_NONE`` and ``YSE_PATCHER_INLET_NONE`` are both
``0xFFFFFFFFu``, the same value as ``UINT_MAX``. They have separate names
because an inlet index is not an object ID. Bindings should mirror both.
``GetConnections`` answers 0 both for an outlet without cords and for an
outlet that does not exist. Compare against ``GetOutputs()`` to tell the two
apart.

.. versionchanged:: 3.0
   These queries used to answer 0 when there was nothing to report, and the
   engine read past the end of the outlet list for an outlet number that did
   not exist (issues
   `#736 <https://github.com/yvanvds/yse-soundengine/issues/736>`_ and
   `#737 <https://github.com/yvanvds/yse-soundengine/issues/737>`_).

The same in C
-------------

The C API mirrors every call on this page. Handles are borrowed from the
patcher, as in C++. Given a ``NULL`` handle, a function does nothing, or
returns 0, ``NULL`` or the "no such" value from the table above:

.. code-block:: c

   #include <stdio.h>
   #include "yse_c/yse_patcher.h"

   YsePatcher* p = yse_patcher_create();
   yse_patcher_init(p, 2);

   YsePHandle* osc = yse_patcher_create_object(p, "~sine", "220");
   YsePHandle* amp = yse_patcher_create_object(p, "~*", "0.2");
   YsePHandle* dac = yse_patcher_create_object(p, "~dac", NULL);
   if (!osc || !amp || !dac) {
     fprintf(stderr, "create failed: %s\n", yse_last_error());
   }

   yse_patcher_connect(p, osc, 0, amp, 0);
   yse_patcher_connect(p, amp, 0, dac, 0);
   yse_patcher_connect(p, amp, 0, dac, 1);

   /* In place: takes effect at the next rendered block. */
   yse_phandle_set_params(osc, "330");

   /* Follow the first cord leaving osc. */
   unsigned int to = yse_phandle_get_connection_target(osc, 0, 0);
   unsigned int inlet = yse_phandle_get_connection_target_inlet(osc, 0, 0);
   if (to != YSE_PATCHER_ID_NONE && inlet != YSE_PATCHER_INLET_NONE) {
     YsePHandle* target = yse_patcher_get_handle_from_id(p, to);  /* amp */
     printf("osc -> object %u, inlet %u (%p)\n", to, inlet, (void*)target);
   }

   yse_patcher_delete_object(p, amp);  /* removes its cords too */
   yse_patcher_destroy(p);

``yse_patcher_create_object`` accepts ``NULL`` for the arguments. Like the
C++ calls, ``yse_patcher_connect`` and ``yse_patcher_disconnect`` ignore a
``NULL`` handle or one from another patcher. No exception crosses the C
boundary: a call that throws in the engine leaves its reason in ``yse_last_error()``. :doc:`c_api` covers embedding a patcher
through the C API in full.

Where to go next
----------------

- :doc:`messages`: what travels along the cords, and in what order.
- :doc:`file_format`: saving a graph with ``DumpJSON`` and loading it with
  ``ParseJSON``.
- :doc:`subpatchers`: grouping objects and connecting to a group's pins.
- :doc:`realtime`: how edits reach the audio thread without blocking it.
- :doc:`objects/index`: every object's arguments, inlets and outlets.
