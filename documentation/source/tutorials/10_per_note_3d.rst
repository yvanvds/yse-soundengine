Per-note 3D: position handlers and swarms
=========================================

Goal: give every note its own position in space, so a single chord becomes a
swarm of independently-moving sound sources orbiting the listener.

A synth is rendered behind one positioned :cpp:class:`YSE::sound`, so by
default every voice shares that one position. Attach a
:cpp:class:`YSE::SYNTH::positionHandler` and each voice gets its *own* 3D
position, updated every block — the basis of the "swarm" effect. libYSE ships
three handlers: :cpp:class:`static <YSE::SYNTH::staticHandler>`,
:cpp:class:`random-spread <YSE::SYNTH::randomSpreadHandler>` and
:cpp:class:`orbit <YSE::SYNTH::orbitHandler>` (the swarm workhorse), or you
can derive your own.

This tutorial walks through ``Demo20_Swarm``: a chord of sine voices orbiting
the listener, steerable live.

Source: `Demo20_Swarm.cpp
<https://github.com/yvanvds/yse-soundengine/blob/master/Demo.Windows.Native/Demo20_Swarm.cpp>`_.

Attaching a handler
-------------------

Build a voice prototype *and* a handler prototype, then chain
``positionHandler`` after ``addVoices``. The synth clones the handler once
per voice slot, so each voice steers independently:

.. literalinclude:: ../../../Demo.Windows.Native/Demo20_Swarm.cpp
   :language: cpp
   :lines: 24-36

The :cpp:class:`orbit handler <YSE::SYNTH::orbitHandler>` reads its base
radius from ``radius``, widens with note velocity (``velocityRadius``) and
aftertouch (``aftertouchWiden``), spins at ``rate`` radians per second, and
slows on release (``releaseSlow``). Like a voice, a handler prototype must
outlive setup — the engine reads it to clone but neither copies nor owns it.

.. note::

   With no handler attached, ``positionHandler`` is simply never called and
   every voice uses the synth's aggregate position — so adding per-note
   positioning is purely additive to the :doc:`06_first_synth` flow.

Steering the swarm live
-----------------------

Two live controls, both bounded and allocation-free, so they are safe to call
every frame:

.. literalinclude:: ../../../Demo.Windows.Native/Demo20_Swarm.cpp
   :language: cpp
   :lines: 82-99

- ``handlerParam(index, value)`` sets a *shared* handler parameter every live
  handler reads next block. Indices 0–2 are the swarm centre
  (:cpp:enumerator:`YSE::SYNTH::HP_CENTER_X` and friends), so one call
  recentres the entire swarm.
- ``aftertouch(channel, -1, value)`` applies channel-wide pressure; the orbit
  handler widens the radius with it.

To place a single note explicitly instead (app-driven trajectories rather
than a handler), use :cpp:func:`YSE::synth::notePosition`.

Reading a voice back
--------------------

``getVoicePosition`` returns a best-effort snapshot of where a sounding voice
currently is — handy for driving visuals off the audio:

.. literalinclude:: ../../../Demo.Windows.Native/Demo20_Swarm.cpp
   :language: cpp
   :lines: 63-68

What you learned
----------------

- Attach a ``positionHandler`` with ``synth::positionHandler(prototype)`` to
  give every voice its own 3D position; the prototype must outlive setup.
- The ``orbitHandler`` makes a chord orbit the listener; velocity and
  aftertouch shape the radius.
- Steer the whole swarm with ``handlerParam`` (centre at indices 0–2), place
  one note with ``notePosition``, and read a voice back with
  ``getVoicePosition``.

Next
----

- :doc:`/api/synth` — position-handler reference.
- :doc:`01_3d_positioning` — the listener and 3D basics this builds on.
