What is libYSE?
================

libYSE is a cross-platform C++ sound engine for games and interactive
applications. It plays audio files and procedural sources at any position in
a 3D scene, mixes them through a hierarchical channel tree, and applies
positioned reverb zones that the listener moves through naturally.

What libYSE is
--------------

- A **runtime sound engine**. Load audio files (wav, ogg, flac), procedurally
  generate audio from custom DSP, or build modular Max/MSP-style graphs with
  the patcher.
- **3D positional**. Every ``sound`` has an ``(x, y, z)`` position; the
  listener has a position and an orientation. Distance attenuation, panning,
  and doppler are computed automatically.
- **Mixable**. Channels group sounds for shared volume control and effects.
  Pre-built channels for music, ambient, voice, GUI, and SFX are available
  out of the box.
- **Embeddable**. Build as a shared library and drop into a C++ application
  or game engine. Custom file IO callbacks (``YSE::io``) and in-memory
  buffer loading (``YSE::BufferIO``) let you read audio from a virtual file
  system or asset pack.

What libYSE is not
------------------

- Not a digital audio workstation. There is no timeline editor, no plug-in
  host, no automation lanes.
- Not a game engine. libYSE is the audio layer of a larger application — you
  supply the rendering, physics, input handling, and game loop.
- Not a music notation library. Notes and chords exist as MIDI-style values
  for the generative ``YSE::player``, not as score symbols.

Supported platforms
-------------------

- **Windows** (MSYS2 / Clang64, MSVC)
- **Linux** (Clang or GCC, x64)
- **Android** (NDK r27+, API 26+, ``arm64-v8a`` and ``x86_64`` — built via
  Gradle in ``Tests/Android/``; release archives published by CI)

The audio backends are PortAudio on desktop and Oboe (AAudio with OpenSL ES
fallback) on Android. libsndfile decodes sample files everywhere. MIDI device
I/O uses RtMidi on desktop and is gated behind the ``YSE_ENABLE_MIDI_DEVICE``
CMake option (on by default on Windows/Linux, off on Android).

A flat ``extern "C"`` ABI (``yse_c/yse_*.h``) is folded into the same shared
library so language bindings (Dart FFI, Python ctypes, …) can call the engine
without C++ ABI compatibility — enabled by default via ``YSE_BUILD_C_API=ON``.

Licence
-------

See ``LICENSE`` at the repository root.

Where next
----------

- :doc:`mental_model` — the five concepts you need to know.
- :doc:`install` — wire libYSE into your CMake project.
- :doc:`hello_sound` — get a sound playing in 15 lines.
