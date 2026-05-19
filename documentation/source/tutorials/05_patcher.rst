Patcher: modular synthesis in code
==================================

Goal: build a small synthesis graph programmatically, drive its
parameters at runtime, then save and reload the graph as JSON.

This tutorial walks through ``Demo13_Patcher`` and ``Demo14_LoadPatcher``
in tandem. By the end you will have a sine oscillator modulated by a
low-frequency oscillator, with three named controls for note, LFO rate,
and volume — and a JSON file you can reload from disk to restore the
patch verbatim.

Source: `Demo13_Patcher.cpp
<https://github.com/yvanvds/yse-soundengine/blob/master/Demo.Windows.Native/Demo13_Patcher.cpp>`_
and `Demo14_LoadPatcher.cpp
<https://github.com/yvanvds/yse-soundengine/blob/master/Demo.Windows.Native/Demo14_LoadPatcher.cpp>`_.

Mental model
------------

A :cpp:class:`YSE::patcher` is a graph of small objects. Each object has
zero or more **inlets** (left side, accepting data) and zero or more
**outlets** (right side, emitting data). You wire them together with
``Connect(from, outletIndex, to, inletIndex)``; data flows from outlets
into inlets.

Object type strings follow a two-character convention that mirrors
Max/MSP:

- ``~`` prefix — audio-rate (DSP) object. ``~sine`` evaluates one sample
  per audio frame.
- ``.`` prefix — control-rate (event) object. ``.r`` (receive) fires only
  when you push data into it with ``PassData``.

The full list of types lives in :file:`YseEngine/patcher/pObjectList.hpp`
as ``YSE::OBJ::*`` constants, and is rendered in the
:doc:`/api/patcher_objects` reference page.

Creating a patcher and attaching it to a sound
----------------------------------------------

A patcher is just an object; you initialise it with the number of audio
outputs it should expose, then hand it to ``sound::create`` so the
engine drives the graph as a sound source:

.. literalinclude:: ../../../Demo.Windows.Native/Demo14_LoadPatcher.cpp
   :language: cpp
   :lines: 18-20

The first argument to ``patcher::create`` is the number of main outputs.
``1`` is mono; pass ``2`` for stereo. ``sound::create(patcher, channel,
volume)`` takes optional ``channel`` and ``volume`` arguments; omit them
to attach to ``MainMix`` at full volume.

Building a graph
----------------

``CreateObject`` returns a :cpp:class:`YSE::pHandle` — an owned pointer
into the patcher's object list. There are two ways to identify a type:
the literal string (``"~sine"``) or the corresponding ``YSE::OBJ``
constant. Both are interchangeable; the demo mixes them to show this:

.. literalinclude:: ../../../Demo.Windows.Native/Demo13_Patcher.cpp
   :language: cpp
   :lines: 12-25

The optional second argument to ``CreateObject`` is the object's
creation arguments, parsed by the object exactly as it would in a saved
patch. The ``~sine`` oscillator above starts silent (no frequency given);
``.r`` takes the receive name (``"pitch"``); ``D_LINE`` is configured by
calling ``SetParams("0 100")`` after creation (start at 0, ramp over
100 ms).

Connecting objects is the next step. ``Connect(from, outletIndex, to,
inletIndex)`` wires one outlet to one inlet — both are zero-indexed:

.. literalinclude:: ../../../Demo.Windows.Native/Demo13_Patcher.cpp
   :language: cpp
   :lines: 27-32

The signal flow is: MIDI note → ``mtof`` → ``line`` (smoothed) →
``~sine`` frequency. The sine output is multiplied by the LFO, then
scaled by the master ``~*`` volume, and finally sent to ``D_DAC`` which
hands the buffer back to the engine.

Driving parameters at runtime
-----------------------------

The ``.r`` (receive) objects are the patcher's external interface. Each
one has a name; calling ``PassData(value, name)`` on the patcher delivers
``value`` to every ``.r`` registered under that name. To use a receive,
connect its outlet to the inlet of whatever should react to the value:

.. literalinclude:: ../../../Demo.Windows.Native/Demo13_Patcher.cpp
   :language: cpp
   :lines: 34-36

Once the graph is wired, push initial values in (and update them
whenever the application wants to change a parameter):

.. literalinclude:: ../../../Demo.Windows.Native/Demo13_Patcher.cpp
   :language: cpp
   :lines: 38-44

``PassData`` is overloaded for ``int``, ``float``, and ``std::string``;
``PassBang(name)`` fires a bare trigger with no payload. The demo's
hotkeys increment the cached values and re-send them on each press:

.. literalinclude:: ../../../Demo.Windows.Native/Demo13_Patcher.cpp
   :language: cpp
   :lines: 64-75

Persistence
-----------

The graph — every object, parameter, and connection — can be serialised
to JSON with ``DumpJSON``. The demo writes it to a sibling file:

.. literalinclude:: ../../../Demo.Windows.Native/Demo13_Patcher.cpp
   :language: cpp
   :lines: 92-97

Reloading is the inverse: read the file, hand the contents to
``ParseJSON``, and the patcher rebuilds itself in place. ``Demo14``
attaches an empty patcher to a sound first, then populates it on
demand:

.. literalinclude:: ../../../Demo.Windows.Native/Demo14_LoadPatcher.cpp
   :language: cpp
   :lines: 23-41

``ParseJSON`` replaces the current graph wholesale — any objects already
in the patcher are discarded. After loading, push the initial control
values back in so the receives have something to forward:

.. literalinclude:: ../../../Demo.Windows.Native/Demo14_LoadPatcher.cpp
   :language: cpp
   :lines: 43-49

This is the round trip: a patch built once in code in ``Demo13``, saved
to ``patcher.yap``, and rebuilt from that file in ``Demo14`` with no
code-side knowledge of its internal structure.

Where to find the full object reference
---------------------------------------

The 37 registered object types — every inlet, outlet, parameter, default
value, and accepted message type — are listed on the
:doc:`/api/patcher_objects` reference page. That page is generated
directly from the engine source, so it can never drift from what
``CreateObject`` actually accepts.

What you learned
----------------

- A patcher is a node graph. Objects have inlets and outlets; data
  flows along ``Connect`` edges.
- ``~`` types run at audio rate, ``.`` types fire on events.
- Use ``.r`` (receive) objects plus ``PassData`` / ``PassBang`` to drive
  the graph from application code.
- ``DumpJSON`` and ``ParseJSON`` round-trip the whole graph — ship
  patches as plain text or build an external editor on top.

Next
----

- :doc:`/api/patcher_objects` — every patcher object, with inlets,
  outlets, parameters, and value ranges.
- :cpp:class:`YSE::patcher` — patcher class reference.
- :cpp:class:`YSE::pHandle` — per-object handle reference.
- :doc:`/tutorials/index` — index of remaining tutorials.
