Install
=======

libYSE builds with CMake 3.20+ on Windows (MSYS2 Clang64, MSVC), Linux, and
Android. The audio backends are PortAudio and libsndfile; MIDI device I/O
uses RtMidi.

Linux (Debian / Ubuntu)
-----------------------

.. code-block:: sh

   sudo apt install \
     cmake ninja-build clang \
     libportaudio-dev libsndfile1-dev librtmidi-dev

   cmake -B build -G Ninja
   cmake --build build

Linux (Fedora / RHEL)
---------------------

.. code-block:: sh

   sudo dnf install \
     cmake ninja-build clang \
     portaudio-devel libsndfile-devel rtmidi-devel

   cmake -B build -G Ninja
   cmake --build build

Windows (MSYS2 Clang64)
-----------------------

Open an **MSYS2 CLANG64** shell:

.. code-block:: sh

   pacman -S --needed \
     mingw-w64-clang-x86_64-cmake \
     mingw-w64-clang-x86_64-ninja \
     mingw-w64-clang-x86_64-clang \
     mingw-w64-clang-x86_64-portaudio \
     mingw-w64-clang-x86_64-libsndfile \
     mingw-w64-clang-x86_64-rtmidi

   cmake -B build -G Ninja
   cmake --build build

The shared library and demo executables land in ``build/bin/``.

Adding libYSE to your CMake project
-----------------------------------

The simplest setup pulls libYSE in as a subdirectory:

.. code-block:: cmake

   add_subdirectory(third_party/yse-soundengine)

   add_executable(my_game src/main.cpp)
   target_link_libraries(my_game PRIVATE yse)

Then in your code:

.. code-block:: cpp

   #include "yse.hpp"

   int main() {
       YSE::System().init();
       // ... use the engine ...
       YSE::System().close();
   }

Verifying the install
---------------------

Run one of the bundled demos to confirm the audio device is reachable:

.. code-block:: sh

   cd build/bin
   ./Demo00          # plays drone.ogg
   ./Demo12          # tone generator (audio self-test)

Demos hard-code paths relative to ``build/bin/``, so they must be launched
from that directory.

Next
----

- :doc:`hello_sound` — a 15-line program that plays a sound.
- :doc:`/tutorials/index` — the full tutorial series.
