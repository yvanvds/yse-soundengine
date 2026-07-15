Your first synth
================

Goal: build a polyphonic synthesiser, attach it behind a sound, and play
notes on it.

Up to now every sound has come from a file or a hand-built DSP source. A
:cpp:class:`YSE::synth` is different: it owns a *pool of voices* and turns
note-on / note-off events into sound, handling polyphony, voice allocation
and voice stealing for you. This tutorial walks through
``Demo18_FMKeyboard`` — a 16-voice FM synth played from a MIDI keyboard or
the console note keys.

Source: `Demo18_FMKeyboard.cpp
<https://github.com/yvanvds/yse-soundengine/blob/master/Demo.Windows.Native/Demo18_FMKeyboard.cpp>`_.

Mental model
------------

Three objects cooperate:

- A **voice prototype** — any :cpp:class:`YSE::SYNTH::dspVoice` subclass
  (here :cpp:class:`YSE::SYNTH::fmVoice`). It describes what *one* note
  sounds like. The synth clones it once per voice.
- The **synth** — :cpp:class:`YSE::synth`. It owns the cloned voices and the
  keyboard state, and receives your ``noteOn`` / ``noteOff`` calls.
- A **sound** — an ordinary positioned :cpp:class:`YSE::sound`. The synth is
  rendered *behind* it, so 3D panning, channels and reverb all apply exactly
  as they do to a file-backed sound.

Building the synth
------------------

Create a prototype voice, hand it to ``synth::addVoices`` to size the pool,
then attach the synth to a sound and start it:

.. literalinclude:: ../../../Demo.Windows.Native/Demo18_FMKeyboard.cpp
   :language: cpp
   :lines: 47-55

``create()`` registers the synth with the engine. ``addVoices(prototype,
16)`` clones the prototype 16 times — that is the polyphony. Cloning happens
off the audio thread on the engine's setup pool, so the synth becomes
playable a moment after ``addVoices`` returns, exactly like a file-backed
sound is not playable until its buffer finishes loading. ``sound::create``
calls ``synth::create`` for you if you have not, and takes the same optional
``channel`` and ``volume`` arguments as any other sound.

.. note::

   The prototype must outlive ``addVoices`` — the engine reads it to clone
   but neither copies nor owns it. In the demo it is a member
   (``proto_``), so it lives as long as the synth.

Playing notes
-------------

Once the synth is playing, drive it with note events. ``noteOn`` /
``noteOff`` take a MIDI channel (1–16, or 0 for omni), a note number, and a
velocity in [0, 1]:

.. literalinclude:: ../../../Demo.Windows.Native/Demo18_FMKeyboard.cpp
   :language: cpp
   :lines: 162-173

``allNotesOff(channel)`` releases everything on a channel (0 = all
channels); the voices enter their normal release, they are not cut.

Tearing it down safely
----------------------

A synth is attached to a sound, and the sound must stop and be reclaimed
*before* the synth (and then the prototype) go away — the same lifetime
discipline a file-backed sound follows. The demo stops the sound, pumps a
few engine updates so the slow pool reclaims it, then releases the synth:

.. literalinclude:: ../../../Demo.Windows.Native/Demo18_FMKeyboard.cpp
   :language: cpp
   :lines: 79-93

What you learned
----------------

- A synth is a pool of voices behind an ordinary positioned sound.
- ``create().addVoices(prototype, n)`` sizes the polyphony; the prototype
  must outlive the call.
- Drive it with ``noteOn`` / ``noteOff`` / ``allNotesOff``.
- Destroy the sound before the synth before the prototype.

Next
----

- :doc:`07_custom_dspvoice` — write your own voice from scratch.
- :doc:`08_instruments` — load SFZ instruments and DX7 banks.
- :doc:`10_per_note_3d` — give every note its own position.
- :doc:`/api/synth` — the full synth API reference.
