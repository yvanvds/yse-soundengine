Hello, sound
============

The smallest libYSE program — init the engine, play an audio file, hold it
open until the user presses Enter, shut down.

.. code-block:: cpp

   #include "yse.hpp"
   #include <iostream>

   int main() {
       if (!YSE::System().init()) {
           std::cerr << "Could not open the audio device.\n";
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

       std::cout << "Press Enter to stop.\n";
       std::cin.get();

       YSE::System().close();
       return 0;
   }

What just happened
------------------

- ``YSE::System().init()`` opens the default audio device and starts the
  DSP threads. Wrap it in an error check; if no device is available, the
  return value is ``false``.
- ``YSE::sound s; s.create("drone.ogg");`` allocates a sound object and
  loads an audio file. The file path is relative to the working directory.
- ``s.isValid()`` is the safety check after ``create``. False means the
  file could not be found at the given path — log and exit. (Decoding
  happens asynchronously on a background worker; ``isReady()`` reports when
  the sound is fully loaded, but ``play()`` is safe to call even while
  loading — it queues the start.)
- ``s.play()`` starts playback. The call returns immediately; the sound
  plays on the audio thread.
- ``std::cin.get()`` is there to keep the program alive long enough to
  hear the sound. A game would loop on its frame timer instead.
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
