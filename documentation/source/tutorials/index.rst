Tutorials
=========

A guided tour of libYSE, organised in five progressive phases that mirror
the bundled demos in
`Demo.Windows.Native <https://github.com/yvanvds/yse-soundengine/tree/master/Demo.Windows.Native>`_.
Each tutorial corresponds to a real, compilable demo — the snippets shown
here are pulled directly from those files, so they stay in sync as the
demos evolve.

Work through them in order if you are new to libYSE; jump straight to the
one you need if you are looking up a specific capability.

Phase 1 — Fundamentals
----------------------

The minimum you need to put a sound on the speakers.

.. toctree::
   :maxdepth: 1

   01_play_a_sound
   02_sound_properties

Other demos in this phase:

- ``Demo12_AudioTest`` — toggle the engine's built-in sine-wave test tone via
  ``YSE::System().AudioTest(true)``. Useful for diagnosing output routing
  problems.

Phase 2 — Spatial audio and mixing
----------------------------------

3D positioning, virtualisation, channels, and audio devices.

.. toctree::
   :maxdepth: 1

   01_3d_positioning
   03_channels

Other demos in this phase:

- ``Demo03_Virtual`` — load 100 sounds and let the engine virtualise the
  ones too far from the listener. Set the audible cap with
  ``YSE::System().maxSounds(...)``.
- ``Demo06_Devices`` — enumerate audio output devices and switch between
  them at runtime via ``YSE::System().getDevice(...)`` and ``openDevice``.

Phase 3 — Effects and streaming
-------------------------------

Reverb zones, large-file streaming, seeking.

.. toctree::
   :maxdepth: 1

   04_reverb

Other demos in this phase:

- ``Demo09_Streaming`` — stream a large audio file from disk by passing
  ``streaming = true`` to ``YSE::sound::create``.
- ``Demo10_FilePosition`` — seek to arbitrary playback positions with
  ``YSE::sound::time(samples)``.

Phase 4 — Advanced DSP and integration
--------------------------------------

Custom audio sources, occlusion, virtual file I/O.

- ``Demo07_DspSource`` — feed audio from a custom subclass of
  ``YSE::DSP::dspSourceObject`` (a Shepard-tone generator built from 11 sine
  oscillators).
- ``Demo08_Occlusion`` — register an occlusion callback with
  ``YSE::System().occlusionCallback(...)`` so the engine attenuates sounds
  by world geometry.
- ``Demo11_VirtualIO`` — load audio data from in-memory byte buffers via
  ``YSE::BufferIO`` instead of the file system. Useful for game-engine
  asset packs and Android.

Phase 5 — Synthesis and control
-------------------------------

Modular synthesis, presets, device resilience, MIDI.

- ``Demo13_Patcher`` — build a synthesis graph programmatically with
  ``YSE::patcher`` (sine generator → LFO modulator → DAC, saved to JSON).
- ``Demo14_LoadPatcher`` — restore a patcher graph from a JSON file and
  drive its parameters at runtime.
- ``Demo15_RestartAudio`` — pause / resume / handle device disconnection
  with ``YSE::System().pause()`` and ``autoReconnect(...)``.
- ``Demo16_Midi`` — enumerate MIDI output ports with
  ``YSE::System().getNumMidiOutDevices()`` and send note-on/off via
  ``YSE::midiOut``.
- ``Demo17_MidiPatcher`` — pipe MIDI messages into a patcher graph through
  ``.noteon`` / ``.noteoff`` / ``.controlchange`` objects.

How to follow along
-------------------

The demos compile alongside the engine — see
:doc:`/intro/install`. Once built, run them from ``build/bin/`` so the
hard-coded ``../../TestResources/...`` paths resolve correctly:

.. code-block:: sh

   cd build/bin
   ./Demo00       # play a sound
   ./Demo02       # 3D positioning
