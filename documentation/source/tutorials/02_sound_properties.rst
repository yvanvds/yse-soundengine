Volume, pitch, and looping
==========================

Goal: control a sound's volume and playback speed at runtime.

Building on :doc:`01_play_a_sound`, this tutorial walks through
``Demo01_SoundProperties``: load a sound, then change its level and pitch
on the fly.

Source: `Demo01_SoundProperties.cpp
<https://github.com/yvanvds/yse-soundengine/blob/master/Demo.Windows.Native/Demo01_SoundProperties.cpp>`_.

Volume
------

``volume()`` is overloaded — call it with no arguments to read, with a
value to set.

.. literalinclude:: ../../../Demo.Windows.Native/Demo01_SoundProperties.cpp
   :language: cpp
   :lines: 45-53

The value is a linear gain in [0.0, 1.0]. ``volume(value, time)`` also
exists and fades over ``time`` milliseconds — much friendlier than the
default instant change for runtime gain riding.

Playback speed
--------------

``speed()`` controls both pitch and playback rate together — there is no
independent pitch-shift in libYSE. Doppler effect from listener / source
velocity layers on top of this unless ``doppler(false)`` is set.

.. literalinclude:: ../../../Demo.Windows.Native/Demo01_SoundProperties.cpp
   :language: cpp
   :lines: 35-43

- ``1.0`` is the natural speed (and pitch).
- ``2.0`` is one octave up, ``0.5`` is one octave down.
- Negative values play backwards (not supported for streaming sounds).

Looping
-------

The third argument to ``sound::create`` is the loop flag. The demo passes
``true`` because the test asset is short:

.. literalinclude:: ../../../Demo.Windows.Native/Demo01_SoundProperties.cpp
   :language: cpp
   :lines: 16-17

Toggle it at runtime with ``sound::looping(bool)``.

What you learned
----------------

- ``volume(value)`` sets, ``volume()`` reads. Use ``volume(value, time)`` to
  fade.
- ``speed(value)`` changes pitch and playback rate together.
- The ``create`` loop flag enables looping at load time; ``looping(bool)``
  toggles it later.

Next
----

- :doc:`01_3d_positioning` — place the sound in 3D space.
- :doc:`03_channels` — group sounds for shared volume control.
- :cpp:class:`YSE::sound` — full sound API.
