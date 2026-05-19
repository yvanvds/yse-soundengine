Install
=======

libYSE builds with CMake 3.20+ on Windows (MSYS2 Clang64, MSVC), Linux, and
Android. The audio backends are PortAudio (desktop) and Oboe (Android);
libsndfile decodes sample files. MIDI device I/O uses RtMidi on desktop and
is gated by the ``YSE_ENABLE_MIDI_DEVICE`` CMake option — on by default on
Windows/Linux, off on Android. Configure with ``-DYSE_ENABLE_MIDI_DEVICE=OFF``
to build without RtMidi.

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

Android
-------

The Android build is wired through Gradle in ``Tests/Android/`` and produces
a NativeActivity APK that ships the test suite for two ABIs
(``arm64-v8a`` and ``x86_64``). libsndfile 1.2.2 and Oboe 1.9.3 are fetched
from source on first configure — no system packages required.

Prerequisites:

- NDK r27+ (installed via Android Studio's SDK Manager → NDK (Side by side))
- Gradle 8+ (the wrapper at ``Tests/Android/gradlew`` will use it
  automatically)
- An attached device or emulator at API 26+

.. code-block:: sh

   cd Tests/Android
   ./gradlew installDebug
   adb shell am start -n net.attrx.yse.tests/.MainActivity
   adb logcat -s yse_tests

The release workflow at ``.github/workflows/release.yml`` builds production
multi-ABI archives the same way and publishes them as release assets.

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
