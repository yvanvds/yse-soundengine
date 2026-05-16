Reverb
======

Goal: add a global reverb, then layer positioned reverb zones so the
listener walks through a sequence of distinct acoustic environments.

Walks through ``Demo05_Reverb``: one global reverb, four positioned zones
(bathroom, hall, sewer pipe, and a custom-parameter zone) lined up along
the z-axis. As the listener moves forward they pass through each in turn.

Source: `Demo05_Reverb.cpp
<https://github.com/yvanvds/yse-soundengine/blob/master/Demo.Windows.Native/Demo05_Reverb.cpp>`_.

Global reverb
-------------

The global reverb is a singleton always available via
``YSE::System().getGlobalReverb()``. It is disabled by default — turn it
on and pick a preset:

.. literalinclude:: ../../../Demo.Windows.Native/Demo05_Reverb.cpp
   :language: cpp
   :lines: 13-16

The last line attaches the reverb processor to the master channel.
Without that step, the reverb runs but is not routed to the output.

Positioned reverb zones
-----------------------

Each positioned reverb is a :cpp:class:`YSE::reverb` instance with a
position, a size (full-strength radius), and a rolloff (the fade-out
distance beyond the radius). The demo places four of them along the
z-axis:

.. literalinclude:: ../../../Demo.Windows.Native/Demo05_Reverb.cpp
   :language: cpp
   :lines: 22-35

Setter chaining is the idiomatic style — every setter returns
``reverb&`` so you can stack ``setPosition(...).setSize(...).setRollOff(...)``
on one line.

The engine blends overlapping zones by proximity. The global reverb fills
in wherever no positioned zone reaches at full strength.

Custom reverb parameters
------------------------

Beyond presets, every individual reverb parameter is exposed. The fourth
zone in the demo skips ``setPreset`` and dials in its own settings:

.. literalinclude:: ../../../Demo.Windows.Native/Demo05_Reverb.cpp
   :language: cpp
   :lines: 37-44

What the parameters do:

- ``setRoomSize`` — simulated room size. Larger values mean longer tails.
- ``setDamping`` — high-frequency damping. Higher values darken the tail
  faster (soft materials).
- ``setDryWetBalance(dry, wet)`` — pass-through level vs. reverb level.
  Usually ``dry + wet`` ≈ 1.
- ``setModulation(frequency, width)`` — slow LFO across the tail to break
  up metallic resonances.
- ``setReflection(n, time, gain)`` — one of the four early reflections.
  Reflections are short echoes layered on top of the diffuse tail to
  suggest specific surfaces.

Walking through the zones
-------------------------

Moving the listener forward in the demo also moves the test source — keep
the sound near the listener so it's audible at every position:

.. literalinclude:: ../../../Demo.Windows.Native/Demo05_Reverb.cpp
   :language: cpp
   :lines: 63-77

As ``z`` increases, the listener crosses into each zone's full-strength
radius and the reverb character shifts smoothly.

What you learned
----------------

- Enable the global reverb via ``System().getGlobalReverb().setActive(true)``
  and route it with ``channel.attachReverb()``.
- Positioned zones are :cpp:class:`YSE::reverb` instances with a position,
  size, and rolloff. Build them with the chainable setters.
- Use ``REVERB_PRESET`` enums for quick configurations or tune the
  individual room-size / damping / dry-wet / modulation / reflection
  knobs yourself.

Next
----

- :cpp:class:`YSE::reverb` — full reverb API.
- :doc:`/tutorials/index` — index of remaining tutorials.
