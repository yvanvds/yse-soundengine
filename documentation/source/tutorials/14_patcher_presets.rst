Patcher: presets
================

Goal: give a patch a bank of presets. Store the current settings of its
controls in numbered slots, switch between them, and have the patch come up
in slot 0 every time it is loaded.

This tutorial assumes you have worked through :doc:`05_patcher`. Its code is
compiled and run by the test suite
(`Tests/patcher/test_patcher_tutorials.cpp
<https://github.com/yvanvds/yse-soundengine/blob/dev/Tests/patcher/test_patcher_tutorials.cpp>`_),
which stores two presets, recalls them, checks that the sound changes with
them, and loads a saved copy.

The patch
---------

.. code-block:: text

   .f 60        .f 1000      .f 0.5        controls: note, cutoff, level
     |            |            |
   .mtof          |            |
     |            |            |
   ~saw -> ~lp 1000 -> ~* 0.5 -> ~dac (both channels)

   .loadmess 0 -> .preset 8                 8 slots; slot 0 on load

.. literalinclude:: ../../../Tests/patcher/test_patcher_tutorials.cpp
   :language: cpp
   :start-after: tutorial:presets:begin
   :end-before: tutorial:presets:end
   :dedent: 2

The controls
~~~~~~~~~~~~

A ``.f`` is a number box: it holds a float, and sends it on whenever it is
set. The creation argument is only its starting value; it sends nothing until
something sets it. The host sets it with ``SetFloatData(0, value)``, and an
editor would draw it and set it the same way.

``.preset`` captures exactly the objects that can have their state written
back: the controls. ``.f``, ``.i``, ``.slider``, ``.dial``, ``.t`` and the
other controls listed in :doc:`/patcher/gui` take part. The cutoff of the
``~lp`` itself is not captured. The ``.f`` that sets it is, which is why
every setting you want in a preset needs a control in front of it.

Storing and recalling
---------------------

Set the controls, then send ``store <n>`` to the ``.preset``. It captures
every control's current value into slot ``n``:

.. literalinclude:: ../../../Tests/patcher/test_patcher_tutorials.cpp
   :language: cpp
   :start-after: tutorial:presets-store:begin
   :end-before: tutorial:presets-store:end
   :dedent: 4

A number recalls that slot. ``.preset`` sends each stored value back into
its control, and each control sends it on, so the sound changes exactly as if
the controls had been set by hand:

.. literalinclude:: ../../../Tests/patcher/test_patcher_tutorials.cpp
   :language: cpp
   :start-after: tutorial:presets-recall:begin
   :end-before: tutorial:presets-recall:end
   :dedent: 4

Other messages: a bang recalls the active slot again, ``clear <n>`` empties a
slot and ``clearall`` empties them all. ``presets->GetGuiValue()`` returns the
active slot number, or ``-1`` when no slot is active, which is what an editor
highlights in a row of preset buttons.

Call ``.preset`` from the host thread
-------------------------------------

Storing walks the whole patch and recalling runs through it, so ``.preset``
only works on the control thread. **A message that reaches it on the audio
thread is dropped**, without a log line. In practice:

- **Works:** ``SetListData`` / ``SetIntData`` / ``SetBang`` on the
  ``.preset`` handle from the host thread, as above, and a ``.loadmess`` or
  ``.loadbang`` (see below).
- **Does not work:** ``PassData`` / ``PassBang`` into a ``.r`` that feeds the
  ``.preset``. Those values are delivered on the audio thread. The same goes
  for MIDI input, so a ``.pgmin`` wired to a ``.preset`` recalls nothing:
  receive the program change in the host and call the handle instead.

The test for this page checks that a ``PassData`` recall is indeed dropped.
:doc:`/patcher/gui` explains the reason and lists which routes deliver on
which thread.

Coming up in a preset on load
-----------------------------

A saved patch holds the creation arguments of its controls, not their live
values. Loaded on its own, the patch would come up with the note at 60, the
cutoff at 1000 and the level at 0.5 — the ``.f`` arguments — whatever was
playing when it was saved.

The ``.preset`` slots, on the other hand, are saved with the patch. The
``.loadmess 0`` in front of the ``.preset`` sends ``0`` when the patch has
loaded, which recalls slot 0:

.. literalinclude:: ../../../Tests/patcher/test_patcher_tutorials.cpp
   :language: cpp
   :start-after: tutorial:presets-reload:begin
   :end-before: tutorial:presets-reload:end
   :dedent: 4

After ``ParseJSON`` returns, the loaded patch plays slot 0, even though slot
1 was active when it was saved. A load signal is sent on the thread that runs
``ParseJSON``, so the recall is not dropped. To come up in whatever slot was
active instead, use a ``.loadbang``: a bang recalls the active slot, and the
active slot is saved too.

Load a patch with presets into an **empty** patcher. The slots refer to their
controls by position in the file, and that only lines up when the loaded
objects are the only ones in the patcher. :ref:`gui-saved` has the details.

Next
----

- :doc:`/patcher/gui` — the GUI value protocol that ``.preset`` is built on,
  every control, and what a host editor polls.
- :doc:`/patcher/objects/gui` — reference for ``.preset`` and the controls.
- :doc:`/patcher/file_format` — what a saved patch holds.
