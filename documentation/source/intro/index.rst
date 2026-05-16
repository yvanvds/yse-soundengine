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
- **Linux** (Clang or GCC)
- **Android** (out of scope of the CMake build but supported as a target)

The audio backends are PortAudio and libsndfile; MIDI device I/O uses
RtMidi.

Licence
-------

See ``LICENSE`` at the repository root.

Where next
----------

- :doc:`mental_model` — the five concepts you need to know.
- :doc:`install` — wire libYSE into your CMake project.
- :doc:`hello_sound` — get a sound playing in 15 lines.
