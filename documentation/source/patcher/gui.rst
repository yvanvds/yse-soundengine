GUI controls and presets
========================

The patcher has no user interface of its own. Controls such as ``.slider``,
``.t``, ``.umenu`` or ``.matrixctrl`` are ordinary objects that hold a value.
The **engine owns that value**. A host-side editor draws each control, polls
its value to repaint it, and sends messages to change it. The engine does not
push changes to the editor, and the editor never writes into an object's
state directly.

This page describes that contract, the **GUI value protocol**: how the host
reads a control's state, how it writes state back, which objects take part,
and how ``.preset`` uses the same contract to store and recall a whole patch.
The inlets, outlets and arguments of each control are listed in
:doc:`objects/gui`.

GUI value or GUI property?
--------------------------

An object carries two kinds of editor data. They are not interchangeable.

.. list-table::
   :header-rows: 1
   :widths: 18 41 41

   * -
     - GUI value
     - GUI property
   * - What
     - The live control state: a slider's position, a menu's selection, a
       grid's cells.
     - Editor decoration: position on the canvas, size, colour, anything
       the host wants to keep with the patch.
   * - Who writes it
     - The object itself, when a message arrives from the host or down a
       cord, often on the audio thread.
     - Only the host.
   * - Calls
     - ``GetGuiValue``, ``GetGuiValueCount``, ``GetGuiValueAt``,
       ``GuiValueIsSettable``
     - ``GetGuiProperty(key)``, ``SetGuiProperty(key, value)``
   * - Threads
     - Safe to poll from the host thread while the patch renders.
     - Control thread only. Not synchronised.
   * - Saved with the patch
     - No (except for ``.preset`` and ``.function``, see
       :ref:`gui-saved`).
     - Yes, under the ``"gui"`` key (see :doc:`file_format`).

The engine never reads a GUI property. Store layout and styling there, and
never live state.

The GUI value
-------------

GUI state is a **list of string cells**. A scalar control such as ``.slider``
has one cell. A structured control has several: ``.rslider`` has two (the
two ends of the range), ``.kslider`` has 128 (one per key), a ``.matrixctrl``
has one per grid cell.

.. list-table::
   :header-rows: 1
   :widths: 30 38 32

   * - C++ (:cpp:class:`YSE::pHandle`)
     - C API
     - Returns
   * - ``GetGuiValue()``
     - ``yse_phandle_get_gui_value(h, buf, cap)``
     - The whole state as one string: the value of a scalar control, or
       every cell space-separated in index order. Empty for an object with
       no GUI state.
   * - ``GetGuiValueCount()``
     - ``yse_phandle_get_gui_value_count(h)``
     - How many cells there are. 1 for a scalar control. The C function
       answers 0 for a ``NULL`` handle.
   * - ``GetGuiValueAt(i)``
     - ``yse_phandle_get_gui_value_at(h, i, buf, cap)``
     - Cell ``i``, or an empty string at or past the count. Cell 0 of a
       scalar control is exactly what ``GetGuiValue`` returns.
   * - ``GuiValueIsSettable()``
     - ``yse_phandle_gui_value_is_settable(h)``
     - Whether the object accepts its own state back on inlet 0 (see
       `Writing state back`_). The C function returns 1 or 0.

The C string functions work like ``snprintf``: they return the full length
and accept a ``NULL`` buffer as a size query. See the caution under
`Polling`_ before you use that size query on a GUI value.

A cell is not always one token. A ``.function`` cell is one breakpoint,
``<x> <y> <curve>``, and a ``.nodes`` cell is one node, ``<x> <y> <radius>``.
Numbers are written as text, so a float ``.slider`` reports ``0.500000``, an
``.i`` reports ``42`` and a ``.t`` reports ``on`` or ``off``. Treat the
strings as data to draw from, and hand them back unchanged when you restore.

Polling
-------

A host editor usually polls every visible control once per frame, on its own
thread, while the audio thread keeps rendering. The calls take no lock, so
the audio thread never waits on a repaint. The price is a few rules:

**Use one form per object per frame.** Either read the whole state with
``GetGuiValue``, or read the count and then the cells with
``GetGuiValueAt``, but never both for the same object in the same frame.
Some reads are destructive (see below), and an event would then reach one
read and not the other.

**Per-cell reads are not atomic across cells.** Each call samples the state
on its own. A repaint can see cell 3 from before an edit and cell 4 from
after it, or a count that a live ``SetParams`` has just changed. A cell past
the current count simply reads as empty. That is fine for drawing. When you
need a consistent snapshot, for example to save it, use ``GetGuiValue``: it
reads everything in one call.

The per-cell form exists for large controls. A 256 × 256 ``.matrixctrl``
builds a string of more than 100 KB for ``GetGuiValue``. A host that repaints
one changed cell should read only that cell.

**Some reads are destructive.** ``.b`` reports ``on`` once for a press and
clears it in the same step, so the next read says ``off``. A ``.led`` or
``.textbutton`` created with the ``momentary`` keyword does the same with
``1`` and ``0``. Poll each of these from one place only, or presses get lost.

.. warning::

   The C API's size query is a read too. Calling
   ``yse_phandle_get_gui_value(h, NULL, 0)`` to learn the length and then
   again to fetch the text polls the object **twice**, and a ``.b`` press is
   consumed by the first call. Poll with a buffer that is large enough (a
   few bytes suffice for ``.b``). If the result is longer than the buffer,
   the second call is a new poll, not a re-read of the first.

**An empty read can be transient.** ``.function`` guards its breakpoints with
a flag that a reader does not wait for. When a write holds it at the moment
you poll, the read comes back empty. Treat an empty result from an object
that normally has state as "nothing new this frame" and keep the last value
you drew. ``.textedit`` is the exception in the other direction: its read
waits the few nanoseconds a write holds the text, so it is always complete.

Writing state back
------------------

There is **no GUI-value setter**, and there will not be one. State goes into
an object the same way every other value does: as a message on an inlet,
with ``SetListData``, ``SetFloatData``, ``SetIntData`` or ``SetBang`` (C:
``yse_phandle_set_list`` and friends). The message passes through the
object's own handler, so every clamp, side effect and outlet send happens
exactly as if the value had come down a cord.

An object that answers true to ``GuiValueIsSettable()`` promises two things
about inlet 0:

1. **The round trip.** A list holding the exact string ``GetGuiValue()``
   returned restores the state it described:

   .. code-block:: cpp

      std::string saved = h->GetGuiValue();
      // ... the user moves things ...
      h->SetListData(0, saved);        // back to where it was

2. **The cell write.** ``set <index> <value>`` writes a single cell:

   .. code-block:: cpp

      range->SetListData(0, "set 1 96");   // .rslider: move one end to 96

The ``set`` keyword is what tells the two apart. For a two-cell control such
as ``.rslider``, ``0 1`` is the whole state and ``set 0 1`` means "cell 0
becomes 1". Without the keyword the two could not be told apart. For
``.function`` and ``.nodes``, whose cells hold several numbers, the value
part is all of them: ``set 2 0.5 0.8 0.`` rewrites breakpoint 2.

**The one exception is** ``.textedit``. Its single cell is free text, and
free text can start with the word ``set``. So ``.textedit`` takes everything
on inlet 0 verbatim, and there is no ``set`` form. With one cell, the
whole-state write already is the cell write, so the round trip still holds
and ``GuiValueIsSettable()`` is still true.

**Whether a restore emits is up to the object.** Most controls have a hot
inlet 0 and send their new value on, so restoring a snapshot drives the rest
of the patch just like the user moving the control. A few are silent, and
``.kslider`` sends only what changed. The table under `The controls`_ lists
which is which. Control objects handle a host message synchronously, on the
calling thread: when ``SetListData`` returns, the object has its new state
and whatever it emitted has already run downstream.

Round trip in a host editor
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The example below is a small editor. It repaints the controls every frame,
takes a snapshot of every settable object, and puts the patch back later.

.. code-block:: cpp

   #include <map>
   #include <string>
   #include "yse.hpp"

   YSE::patcher patch;
   patch.create(2);

   YSE::pHandle* range = patch.CreateObject(".rslider", "0 127");
   YSE::pHandle* steps = patch.CreateObject(".multislider", "8 0 1");
   YSE::pHandle* mute  = patch.CreateObject(".t");
   YSE::pHandle* flash = patch.CreateObject(".b");
   // ... connect them to the rest of the patch ...

   // Every frame: repaint. One form per object per frame.
   void repaint(YSE::pHandle* h) {
     unsigned int cells = h->GetGuiValueCount();
     for (unsigned int i = 0; i < cells; i++) {
       std::string cell = h->GetGuiValueAt(i);
       if (cell.empty()) break;       // the count changed under us: stop
       // draw cell i ...
     }
   }

   // A snapshot: the whole-state string of every settable object, by ID.
   std::map<unsigned int, std::string> snapshot(YSE::patcher& p) {
     std::map<unsigned int, std::string> out;
     for (unsigned int i = 0; i < p.Objects(); i++) {
       YSE::pHandle* h = p.GetHandleFromList(i);
       if (h == nullptr || !h->GuiValueIsSettable()) continue;  // skips .b
       std::string value = h->GetGuiValue();
       if (!value.empty()) out[h->GetID()] = value;
     }
     return out;
   }

   // Restore: send each object its own string back, as a list on inlet 0.
   void restore(YSE::patcher& p, const std::map<unsigned int, std::string>& s) {
     for (const auto& [id, value] : s) {
       YSE::pHandle* h = p.GetHandleFromID(id);
       if (h != nullptr && h->GuiValueIsSettable()) h->SetListData(0, value);
     }
   }

Checking ``GuiValueIsSettable()`` before reading does two jobs. It skips
objects that could not take the value back, and it means the snapshot never
polls ``.b``, so taking a snapshot cannot eat a press.

An object ID names an object only while it is alive. After a delete, the next
object created can take over the number (see :doc:`building`). A snapshot
that must survive edits should also keep the ``Type()`` of each object and
check it on restore. ``.preset`` does exactly that.

The same round trip in the C API:

.. code-block:: c

   char value[4096];
   unsigned int n = yse_patcher_objects(patch);
   for (unsigned int i = 0; i < n; i++) {
     YsePHandle* h = yse_patcher_get_handle_from_list(patch, i);
     if (!yse_phandle_gui_value_is_settable(h)) continue;
     size_t len = yse_phandle_get_gui_value(h, value, sizeof value);
     if (len == 0 || len >= sizeof value) continue;  /* empty, or too long for this buffer */
     /* store yse_phandle_get_id(h) and value ... */
   }

   /* later, to restore one object: */
   yse_phandle_set_list(h, 0, value);

The controls
------------

Every object in the table answers true to ``GuiValueIsSettable()``.

.. list-table::
   :header-rows: 1
   :widths: 22 16 40 22

   * - Object
     - Cells
     - One cell holds
     - A restore
   * - ``.slider``
     - 1
     - the position, 0 to 1
     - emits
   * - ``.dial``
     - 1
     - the knob position, 0 to 1, not the mapped value
     - emits
   * - ``.i`` / ``.f``
     - 1
     - the number
     - emits
   * - ``.incdec``
     - 1
     - the stored whole number
     - is silent
   * - ``.t``
     - 1
     - ``on`` or ``off``
     - emits
   * - ``.led`` / ``.textbutton``
     - 1
     - ``0`` or ``1``
     - emits
   * - ``.umenu`` / ``.radiogroup`` / ``.tab``
     - 1, or one per item with ``multi``
     - the selected index; with ``multi``, ``0`` or ``1`` per item
     - emits
   * - ``.textedit``
     - 1
     - the text (no ``set`` form)
     - emits
   * - ``.rslider``
     - 2
     - the low end, then the high end
     - emits
   * - ``.xyslider``
     - 2
     - x, then y
     - emits
   * - ``.nslider``
     - 2
     - the pitch, then the accidental (``1`` sharp, ``-1`` flat, ``0``
       natural)
     - emits
   * - ``.multislider``
     - the live bank size
     - one slider value
     - emits the whole bank
   * - ``.matrixctrl``
     - columns × rows
     - one grid cell; cell ``(column, row)`` is index
       ``column * rows + row``
     - emits every cell as ``<column> <row> <value>``
   * - ``.kslider``
     - 128
     - the velocity held on that key, 0 when the key is up
     - emits only the keys that changed
   * - ``.function``
     - the number of breakpoints
     - ``<x> <y> <curve>``, in ascending x
     - is silent
   * - ``.nodes``
     - the number of nodes
     - ``<x> <y> <radius>``
     - emits

Notes on individual controls:

- ``.t`` accepts its own ``on`` / ``off`` back, and also a number. A restore
  sets the state; it never flips it the way a bang does.
- ``.incdec`` has two ``set`` forms. ``set <n>`` (one number) is Max's silent
  set; ``set 0 <n>`` (two numbers) is the protocol's cell write.
- ``.rslider`` stores the two ends as they were written and sorts them when
  it reports them, so cell 0 is always the low end.
- ``.multislider`` takes its size from a whole-state list, so restoring a
  snapshot also restores the bank size.
- ``.matrixctrl``: on a grid of exactly three cells, a three-number list is
  read as the whole state, never as Max's ``<column> <row> <value>`` triple.
- ``.function``: an empty function reports the word ``clear`` rather than an
  empty string. ``GetGuiValueCount()`` is then 0. ``clear`` round-trips like
  any other state, so a snapshot of an empty envelope empties it again on
  restore.
- ``.nodes``: the whole state carries the node count, so restoring it into a
  fresh object restores the field's size too.
- A momentary ``.led`` / ``.textbutton`` still holds the round trip, but its
  value is a pending press. Restoring a snapshot taken at the moment of a
  press replays the press.

Objects without a settable value
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

These report a GUI value but answer false to ``GuiValueIsSettable()``, so
neither a host snapshot as above nor ``.preset`` captures them:

- ``.b``: its value is a press event that the read consumes, not a state.
  Writing ``on`` back would replay the press. For a momentary control whose
  state can be restored, use ``.led momentary`` or ``.textbutton momentary``.
- ``.preset``: its value is the active slot, read-only (see below).
- ``.m`` and ``.l``: the value is the message text the box holds.
- Non-GUI objects that expose a read-only display value, such as
  ``.counter``, ``.accum``, ``.drunk`` or ``.uzi``.

``.text`` is a fixed label and has no GUI value at all. Its text is its
creation argument.

Before issue #846 the scalar controls (``.slider``, ``.i``, ``.f``, ``.dial``,
``.incdec``, ``.t``) were outside the protocol too, because their inlet 0 only
took a number. They now accept their own display string back and are
settable.

.. _gui-preset:

Presets
-------

``.preset`` is Max's ``preset`` object: a bank of numbered slots, each
holding a snapshot of the patch's controls. It is the host snapshot from the
example above, built into the patch.

.. code-block:: cpp

   YSE::pHandle* presets = patch.CreateObject(".preset", "8");  // 8 slots

   presets->SetListData(0, "store 0");   // capture the patch into slot 0
   // ... the user moves things ...
   presets->SetIntData(0, 0);            // recall slot 0

Messages
~~~~~~~~

``.preset`` has one inlet:

.. list-table::
   :header-rows: 1
   :widths: 28 72

   * - Message
     - Effect
   * - an int or a float
     - Recalls that slot. A float is truncated.
   * - ``recall <n>``
     - The same, spelled out.
   * - ``store <n>``
     - Captures the patch into slot ``n`` and makes it the active slot.
   * - bang
     - Recalls the active slot again. Does nothing when no slot is active.
   * - ``clear <n>``
     - Empties slot ``n``.
   * - ``clear``
     - Empties the active slot.
   * - ``clearall``
     - Empties every slot.

Outlet 0 sends the number of the slot just recalled, **after** every value
has been pushed, so whatever it triggers already sees the patch in the
preset. Outlet 1 sends the number of the slot just stored. Recalling an empty
or out-of-range slot does nothing and sends nothing. Clearing the active slot
also clears the active marker.

The creation argument is the number of slots: 32 by default, clamped to
1–1024. Changing it with ``SetParams`` replaces the object, and the
replacement starts with empty slots.

The GUI value of ``.preset`` is the active slot number, or ``-1`` when none is
active. That is what a host highlights in a row of preset buttons. It is
read-only.

What a slot captures
~~~~~~~~~~~~~~~~~~~~

A store captures **exactly the objects that answer true to**
``GuiValueIsSettable()``, which are the controls in the table above. The rule
is the protocol's own promise: an object that is captured is also one that
can be restored. As a result:

- ``.preset`` never captures another ``.preset``, or itself.
- ``.b`` is never captured, and a store never even polls it, so storing
  cannot eat a press.
- Objects inside subpatchers are captured too. A preset belongs to the whole
  patch, not to one level of it.
- An object whose value is empty at store time (an empty ``.textedit``) is
  left out, and a recall leaves it untouched.
- Only controls are captured. The cutoff of a ``~lp`` is not; the control
  that sets it is.

A recall sends each captured object the exact string it reported, through
``SetListData`` on inlet 0, the same call a host makes. Each object's own
handler decides whether to emit (see the table). Before an entry is pushed,
``.preset`` checks that its object ID still names a live object **of the same
type**. IDs are reused after a delete, and this check keeps a recall from
writing one control's state into whatever object took over its number. A
stale entry is skipped; the rest of the slot is still recalled.

There is no interpolated recall: a recall is instant, as in Max. To morph
between states, build it in the patch, for example ``.xyslider`` into
``.nodes`` into the gains you want to blend.

Threads
~~~~~~~

Storing walks the whole patch and recalling runs whole subgraphs, so
``.preset`` never does that work on the audio thread. Every way of driving it
works, but not every way runs at the same moment:

- **Runs immediately:** a ``SetListData`` / ``SetIntData`` / ``SetBang`` call
  on the ``.preset`` handle from the host thread, a ``.loadbang`` (see
  below), and a millisecond ``.metro``. These deliver on control-side
  threads, and the recall has happened when the call returns.
- **Runs a moment later:** anything that reaches the ``.preset`` while the
  patch renders, on the audio thread. That is ``PassData`` / ``PassBang``
  into a ``.r`` that feeds the ``.preset`` (host messages sent that way are
  delivered at the start of the next block, see :doc:`host_io`), **MIDI
  input** (the input objects deliver their events while the patch renders,
  see :doc:`midi`), and anything fired inside the render pass, such as a
  ``.delay`` or ``.pipe`` output. The ``.preset`` does not run these on the
  audio thread: it queues them and runs them on the timer thread about a
  millisecond later, in the order they arrived. Queuing is lock-free and
  allocation-free, so it costs the audio thread nothing noticeable.

So a ``.pgmin`` wired into a ``.preset`` recalls a preset on a program change,
as in Max. Keep in mind that the recall lands after the block that carried the
program change, not inside it. A bang or a bare ``clear`` reads the active
slot when it runs, so a ``store 3`` followed by a bang in the same block
recalls slot 3.

At most 64 of these queued messages can wait at once. Past that, further ones
are dropped and counted rather than logged, because the audio thread must not
log.

Editing the patch while a store or recall runs on another thread is safe. An
object deleted halfway through a recall stays allocated until the recall is
done (see :doc:`realtime`). Whether it still receives its value depends on
which comes first; a recall never touches freed memory either way.

.. _gui-saved:

What is saved with the patch
----------------------------

A saved patch (``DumpJSON``) holds each object's creation arguments and GUI
properties. For almost every control it does **not** hold the live GUI value:
reloading brings each control back in its initial state. A host that wants to
reopen a patch as the user left it has two options:

- keep its own snapshot, as in the example above, and restore it after
  ``ParseJSON``; or
- put a ``.preset`` in the patch.

Two controls do save their contents:

- ``.preset`` saves all its slots and the active slot marker. Loading does
  **not** recall anything. A ``.loadbang`` connected to the ``.preset``
  inlet recalls the saved active slot when the patch loads; put a ``.m 0``
  in between to always come up in slot 0. You can also send the recall from
  the host after ``ParseJSON`` returns. ``.loadbang`` fires on the thread
  that ran ``ParseJSON``, so the patch is in the slot when ``ParseJSON``
  returns.
- ``.function`` saves its breakpoints.

In the file, a ``.preset`` entry names its object by its position among the
saved objects rather than by its raw ID. ``ParseJSON`` gives loaded objects
new IDs in that same order, so the presets still point at the right objects
even when deletes had left gaps in the numbering. This relies on loading
into an **empty** patcher. Loaded into a patcher that already holds objects,
the numbering starts past them and the entries point at the wrong objects;
the type check turns most of those into skips rather than wrong writes.
