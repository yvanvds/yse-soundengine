Hello, sound
============

The smallest libYSE program: start the engine, play an audio file for ten
seconds while calling ``update()``, shut down.

.. code-block:: cpp

   #include "yse.hpp"
   #include <iostream>

   int main() {
       if (!YSE::System().init()) {
           std::cerr << "Could not start the audio engine.\n";
           return 1;
       }

       YSE::sound s;
       s.create("drone.ogg");
       if (!s.isValid()) {
           std::cerr << "Could not load drone.ogg\n";
           YSE::System().close();
           return 1;
       }

       s.play();

       // Keep the engine running for about ten seconds.
       for (int i = 0; i < 1000; ++i) {
           YSE::System().update();
           YSE::System().sleep(10);
       }

       YSE::System().close();
       return 0;
   }

What just happened
------------------

- ``YSE::System().init()`` starts the engine and opens the default audio
  device. It returns ``false`` if the audio backend cannot start. A missing
  or busy device is only logged, so ``init()`` can succeed without sound;
  :doc:`sessions_and_devices` shows how to check that audio is flowing.
- ``YSE::sound s; s.create("drone.ogg");`` sets up a sound and loads an
  audio file. The file path is relative to the working directory.
- ``s.isValid()`` is the check after ``create``. It is ``false`` when the
  sound has no engine object behind it: ``create`` failed (for example, the
  file was not found) or was never called. Every other call on such a sound
  does nothing, so this is the one place to notice the failure. (Decoding
  happens on a background thread; ``isReady()`` reports when the sound is
  fully loaded, but ``play()`` is safe to call while it loads. The start is
  queued.)
- ``s.play()`` starts playback. The call returns immediately; the sound
  plays on the audio thread.
- The loop calls ``YSE::System().update()`` every 10 ms. The engine needs
  it: ``update()`` hands new sounds and the changes you made to the audio
  thread, so without it ``play()`` would never take effect.
  :doc:`threading` lists everything it drives. A real application calls it
  once per frame from its main loop.
- ``YSE::System().close()`` stops the engine and releases the audio device.

What's not here
---------------

Every call you would make in a real application — but that this minimal
example skips — is covered in the tutorials:

- 3D positioning of the sound and the listener — see
  :doc:`/tutorials/01_3d_positioning`.
- Volume, pitch, and looping — :doc:`/tutorials/02_sound_properties`.
- Routing sounds through channels — :doc:`/tutorials/03_channels`.
- Reverb zones — :doc:`/tutorials/04_reverb`.

Or jump straight to the :doc:`/tutorials/index` overview.
