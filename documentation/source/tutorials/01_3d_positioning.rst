3D positioning
==============

Goal: place sounds at coordinates in the scene, move the listener through
them, and hear the panning and distance attenuation update in real time.

Walks through ``Demo02_3D``, which moves two sounds and the listener
around a small console-driven 3D space.

Source: `Demo02_3D.cpp
<https://github.com/yvanvds/yse-soundengine/blob/master/Demo.Windows.Native/Demo02_3D.cpp>`_.

The Pos class
-------------

Every position in libYSE is a :cpp:class:`YSE::Pos` — a simple struct with
public ``x``, ``y``, ``z`` floats. Construct one in-place, or use ``zero()``
to reset:

.. literalinclude:: ../../../Demo.Windows.Native/Demo02_3D.cpp
   :language: cpp
   :lines: 84-92

This piece resets the demo:

- ``Listener().pos(pos)`` puts the ear at the origin.
- ``sound1.pos(YSE::Pos(-3, 0, 3))`` places the drone three units left and
  three units in front of the listener.
- ``sound2.pos(YSE::Pos(3, 0, 3))`` places the kick three units right.

Reading a position
------------------

The same method (overloaded) reads or writes. With no arguments it returns
the current position:

.. literalinclude:: ../../../Demo.Windows.Native/Demo02_3D.cpp
   :language: cpp
   :lines: 94-117

The pattern — fetch ``pos``, modify it, write it back — is the idiomatic
way to nudge an object's position by a delta. The Listener works the
same way: ``Listener().pos()`` reads, ``Listener().pos(p)`` writes.

Moving every frame
------------------

In a real application, you would call ``Listener().pos(...)`` once per
frame with the camera's current position. The engine derives velocity
from successive positions; the doppler effect kicks in automatically as
long as updates are frequent (typically alongside
``YSE::System().update()``).

If you also need orientation — for example to know which way the camera is
facing — call ``Listener().orient(forward, up)``. The default ``up``
vector is ``(0, 1, 0)``, which constrains rotation to the horizontal
plane.

What you learned
----------------

- :cpp:class:`YSE::Pos` is the universal 3D coordinate. ``x`` / ``y`` / ``z``
  are public floats; build with ``Pos(x, y, z)`` or ``zero()``.
- ``sound.pos(p)`` writes, ``sound.pos()`` reads.
- The ``YSE::Listener()`` singleton works the same way.
- Doppler is automatic — keep updates frequent.

Next
----

- :doc:`03_channels` — group sounds for shared volume / FX.
- :doc:`04_reverb` — add positioned reverb zones.
- :cpp:class:`YSE::listener` — full listener API.
