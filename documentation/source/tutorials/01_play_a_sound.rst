Play a sound
============

Goal: load an audio file from disk and start it playing.

This tutorial walks through ``Demo00_PlaySound``, the simplest libYSE
program. By the end you will have a sound playing, with hot keys to toggle,
play, pause, and stop it.

Source: `Demo00_PlaySound.cpp
<https://github.com/yvanvds/yse-soundengine/blob/master/Demo.Windows.Native/Demo00_PlaySound.cpp>`_.

Loading the sound
-----------------

A ``YSE::sound`` is an instance — one object per voice you want in the
scene. Load a file with ``create``:

.. literalinclude:: ../../../Demo.Windows.Native/Demo00_PlaySound.cpp
   :language: cpp
   :lines: 9-15

The third argument is the loop flag; the demo loops the drone for as long
as it plays. The path is resolved relative to the working directory, so
the demo must be launched from ``build/bin/``.

``isValid()`` returns ``false`` if the file could not be opened or
decoded — always check it after ``create`` and fail fast.

Controlling playback
--------------------

Four methods cover the basics:

.. literalinclude:: ../../../Demo.Windows.Native/Demo00_PlaySound.cpp
   :language: cpp
   :lines: 25-43

- ``play()`` — start playback. Starts immediately, or queues if the sound
  is still loading.
- ``pause()`` — freeze the playhead. The next ``play()`` resumes from there.
- ``stop()`` — halt and rewind to the start of the source.
- ``toggle()`` — cycle ``playing → paused``, ``paused → playing``,
  ``stopped → playing``. Handy for one-key mappings.

What you learned
----------------

- One ``YSE::sound`` instance per playing voice.
- ``create(path, channel, loop)`` — the third arg is loop, leave ``channel``
  as ``nullptr`` to use ``MainMix``.
- ``isValid()`` is the safety check after ``create``.
- ``play`` / ``pause`` / ``stop`` / ``toggle`` are the playback primitives.

Next
----

- :doc:`02_sound_properties` — set volume, pitch, and looping at runtime.
- :doc:`01_3d_positioning` — place the sound in 3D space.
- :cpp:class:`YSE::sound` — full API reference for the sound class.
