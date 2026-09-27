Patcher: modular synthesis in code
==================================

Goal: build a small synthesis graph in code, change its parameters while it
plays, then save it as a JSON patch and load it back.

This tutorial walks through two demos. ``Demo13_Patcher`` builds a sine
oscillator with a tremolo (a second, slow sine that modulates its level) and
three named controls: note, tremolo rate and volume. It saves the patch to
``patcher.yap``. ``Demo14_LoadPatcher`` starts with an empty patcher and loads
that file. The loaded patch starts playing with the right note, rate and
volume, because the patch holds its own starting values.

Source: `Demo13_Patcher.cpp
<https://github.com/yvanvds/yse-soundengine/blob/dev/Demo.Windows.Native/Demo13_Patcher.cpp>`_
and `Demo14_LoadPatcher.cpp
<https://github.com/yvanvds/yse-soundengine/blob/dev/Demo.Windows.Native/Demo14_LoadPatcher.cpp>`_.
The patch file both demos use ships as ``TestResources/patcher.yap``, and the
test suite loads it and checks that it plays, so this page cannot drift from
what the engine does.

Mental model
------------

A :cpp:class:`YSE::patcher` is a graph of small objects. Each object has
zero or more **inlets** (input) and zero or more **outlets** (output).
``Connect(from, outlet, to, inlet)`` draws a cord from one outlet to one
inlet, and data flows along it.

The first character of a type name says what kind of object it is:

- ``~`` — a **signal** object. ``~sine`` computes a block of audio samples
  every time the patch renders.
- ``.`` — a **control** object. ``.mtof`` only does something when a message
  (a number, a list or a bang) arrives at an inlet.

:doc:`/patcher/index` explains both kinds in more detail, and
:doc:`/patcher/objects/index` lists every object type with its inlets,
outlets and arguments.

Creating a patcher
------------------

Initialise the patcher with the number of audio outputs it should have:

.. literalinclude:: ../../../Demo.Windows.Native/Demo13_Patcher.cpp
   :language: cpp
   :start-after: tutorial:create:begin
   :end-before: tutorial:create:end
   :dedent: 2

``1`` is mono; pass ``2`` for stereo. A patcher does not render by itself.
It renders once it is attached to a sound, which the demo does after the
graph is built:

.. literalinclude:: ../../../Demo.Windows.Native/Demo13_Patcher.cpp
   :language: cpp
   :start-after: tutorial:sound:begin
   :end-before: tutorial:sound:end
   :dedent: 2

``sound::create`` also takes an optional channel and volume. A patcher can
also run as an insert effect on a sound or a channel; see
:doc:`/patcher/index`.

Building the graph
------------------

``CreateObject`` returns a :cpp:class:`YSE::pHandle`, a handle to the new
object. The type is either the literal name (``"~sine"``) or the matching
``YSE::OBJ`` constant (``OBJ::D_SINE``). Both do the same thing, and the demo
uses both:

.. literalinclude:: ../../../Demo.Windows.Native/Demo13_Patcher.cpp
   :language: cpp
   :start-after: tutorial:objects:begin
   :end-before: tutorial:objects:end
   :dedent: 2

The optional second argument of ``CreateObject`` holds the object's creation
arguments, written as they would appear in a saved patch. ``.r`` takes the
name it listens to (``"pitch"``). The ``~line`` gets its arguments later
with ``SetParams("0 100")``: start at 0 and ramp over 100 ms. Passing
``"0 100"`` to ``CreateObject`` would do the same. A ``~sine`` created
without arguments runs at 440 Hz until something sets its frequency.

Now wire the objects. Pin numbers count from 0:

.. literalinclude:: ../../../Demo.Windows.Native/Demo13_Patcher.cpp
   :language: cpp
   :start-after: tutorial:connect:begin
   :end-before: tutorial:connect:end
   :dedent: 2

The signal flow is: MIDI note → ``.mtof`` (note to Hz) → ``~line`` (glides
to the new frequency) → ``~sine``. The sine is multiplied by the tremolo
sine, then by the volume in the ``~*``, and ends in the ``~dac``, the
patch's audio output.

Driving parameters at runtime
-----------------------------

The ``.r`` (receive) objects are the patch's interface to the host. Each has
a name, and ``PassData(value, name)`` on the patcher delivers the value to
every ``.r`` with that name. Wire each receiver to what it should control:

.. literalinclude:: ../../../Demo.Windows.Native/Demo13_Patcher.cpp
   :language: cpp
   :start-after: tutorial:receive:begin
   :end-before: tutorial:receive:end
   :dedent: 2

``PassData`` takes an ``int``, a ``float`` or a ``std::string`` (a list), and
``PassBang(name)`` sends a bang. The value is queued and reaches the ``.r``
at the start of the next block the patcher renders. The demo's hotkeys
change a cached value and send it again:

.. literalinclude:: ../../../Demo.Windows.Native/Demo13_Patcher.cpp
   :language: cpp
   :start-after: tutorial:hotkeys:begin
   :end-before: tutorial:hotkeys:end

:doc:`/patcher/host_io` covers every way values get into and out of a patch,
and which thread each one runs on.

Starting values that travel with the patch
------------------------------------------

The patch needs a note, a tremolo rate and a volume before the host sends
anything. Those values belong in the patch, so that a saved copy starts the
same way. A ``.loadmess`` holds a message and sends it when the patch is
loaded:

.. literalinclude:: ../../../Demo.Windows.Native/Demo13_Patcher.cpp
   :language: cpp
   :start-after: tutorial:loadmess:begin
   :end-before: tutorial:loadmess:end
   :dedent: 2

The spelling of the argument decides what is sent: ``60`` is an int, and
``4.0`` and ``0.5`` are floats. That matters here, because the frequency
inlet of ``~sine`` takes a float.

A ``.loadmess`` fires when ``ParseJSON`` has built, wired and published a
loaded patch. It never fires for an object you create with
``CreateObject``, because at that moment it has no cords yet. That is why
Demo13, which builds its patch in code, bangs each one once. A bang on the
inlet sends the same message a load would. ``.loadbang`` is the same object
without a message: it sends a bang. :doc:`/patcher/file_format` describes
exactly when both fire.

Saving and loading
------------------

``DumpJSON`` writes the whole graph as JSON: every object, its creation
arguments and its cords. Key ``7`` in Demo13 saves it:

.. literalinclude:: ../../../Demo.Windows.Native/Demo13_Patcher.cpp
   :language: cpp
   :start-after: tutorial:save:begin
   :end-before: tutorial:save:end

Each object is one record. This is the ``.loadmess`` that sets the volume,
wired to inlet 1 of the object with ID 3 (the ``~*``):

.. literalinclude:: ../../../TestResources/patcher.yap
   :language: json
   :start-at: "object 12"
   :end-before: "object 2"
   :dedent: 2

:doc:`/patcher/file_format` describes the format in full.

Demo14 attaches an empty patcher to a sound first:

.. literalinclude:: ../../../Demo.Windows.Native/Demo14_LoadPatcher.cpp
   :language: cpp
   :start-after: tutorial:attach:begin
   :end-before: tutorial:attach:end
   :dedent: 2

Key ``7`` loads the patch. It reads the ``patcher.yap`` that Demo13 saved in
the working directory, or the copy in ``TestResources/`` when there is none:

.. literalinclude:: ../../../Demo.Windows.Native/Demo14_LoadPatcher.cpp
   :language: cpp
   :start-after: tutorial:load:begin
   :end-before: tutorial:load:end
   :dedent: 2

``ParseJSON`` **adds** the file's objects to the patcher and leaves the
objects already there in place, so call ``Clear()`` first to replace a patch.
When it returns, the ``.loadmess`` objects have fired and the patch plays at
note 60, with a 4 Hz tremolo, at half volume. Demo14 sends nothing to set that
up. From there its hotkeys drive the same ``.r`` names as Demo13's.

A patcher's name is saved with the patch too, if you set one with ``name()``.
``.s`` / ``.r`` addresses and shared stores are scoped by that name (see
:doc:`/patcher/host_io`). ``ParseJSON`` restores the saved name, unless the
host has already named the patcher.

What you learned
----------------

- A patcher is a graph. Objects have inlets and outlets, and ``Connect``
  draws cords between them.
- ``~`` objects process audio every block. ``.`` objects react to messages.
- ``.r`` objects plus ``PassData`` / ``PassBang`` drive a patch from the host.
- ``.loadmess`` and ``.loadbang`` give a patch its starting values, so a
  loaded patch needs nothing from the host to start.
- ``DumpJSON`` and ``ParseJSON`` save and load the whole graph as text.

Next
----

- :doc:`11_patcher_step_sequencer` — a step sequencer with ``.metro``,
  ``.counter`` and ``.coll``.
- :doc:`12_patcher_subpatched_voice` — package a voice as a subpatcher.
- :doc:`13_patcher_midi_synth` — a polyphonic synth played from a MIDI
  keyboard.
- :doc:`14_patcher_presets` — store and recall a patch's settings.
- :doc:`/patcher/index` — the patcher guide.
