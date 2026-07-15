Writing a custom dspVoice
=========================

Goal: implement your own synthesiser voice by subclassing
:cpp:class:`YSE::SYNTH::dspVoice`.

The built-in voices (sine, virtual-analog, sampler, FM) cover a lot of
ground, but the whole point of the voice model is that *you* can define what
a note sounds like while the engine keeps owning polyphony, allocation and
lifecycle. This tutorial dissects the reference voice,
:cpp:class:`YSE::SYNTH::sineVoice` — one sine oscillator shaped by an ADSR
envelope. It is the smallest legal voice, and every synth test builds on it.

Source: `sineVoice.hpp
<https://github.com/yvanvds/yse-soundengine/blob/master/YseEngine/synth/sineVoice.hpp>`_
and `sineVoice.cpp
<https://github.com/yvanvds/yse-soundengine/blob/master/YseEngine/synth/sineVoice.cpp>`_.

The contract
------------

A ``dspVoice`` is a ``DSP::dspSourceObject`` — it already owns the
``samples`` output buffers — extended with the two methods you **must**
implement:

.. literalinclude:: ../../../YseEngine/synth/sineVoice.hpp
   :language: cpp
   :lines: 76-86

- ``process(SOUND_STATUS& intent)`` fills one block of ``samples`` on the
  **audio thread**. It must be allocation-free, lock-free and non-blocking.
- ``clone()`` returns a fresh, fully-allocated heap copy of your voice. It
  runs only on the **setup thread**, so allocation there is fine — and
  necessary, because everything ``process()`` touches must already exist.

Reading note state
------------------

The engine delivers the current note's parameters as atomics you read inside
``process()``: ``getFrequency()`` (Hz), ``getVelocity()`` ([0, 1]),
``getAftertouch()`` and ``getPitchWheel()`` ([-1, 1]). You never write them.

Allocate everything up front
----------------------------

The voice builds its oscillator and envelope in the constructor and in the
(off-thread) setter methods — never in ``process()``:

.. literalinclude:: ../../../YseEngine/synth/sineVoice.cpp
   :language: cpp
   :lines: 38-46

``clone()`` is then a one-liner that copy-constructs, and the copy
constructor rebuilds independent state so a clone shares nothing mutable with
its prototype:

.. literalinclude:: ../../../YseEngine/synth/sineVoice.cpp
   :language: cpp
   :lines: 107-109

.. literalinclude:: ../../../YseEngine/synth/sineVoice.cpp
   :language: cpp
   :lines: 48-59

Honouring the intent
--------------------

``process()``'s ``intent`` argument *is* this voice's ``SOUND_STATUS``. You
read it to drive your envelope and — crucially — you **write it back** to
tell the engine what the voice is doing:

.. literalinclude:: ../../../YseEngine/synth/sineVoice.cpp
   :language: cpp
   :lines: 111-175

The lifecycle in that block:

- ``SS_WANTSTOPLAY`` (note start) → restart the oscillator and envelope
  attack, then settle the intent to ``SS_PLAYING``.
- ``SS_PLAYING`` → hold the sustain plateau.
- ``SS_WANTSTOSTOP`` (note off) → take the release transition, then tail out.
- When the release tail reaches zero, set ``intent = SS_STOPPED`` so the
  allocator can free the slot. **If you never settle to ``SS_STOPPED`` the
  voice is never reclaimed.**

Using it
--------

A custom voice is used exactly like a built-in one — it *is* a
``dspVoice``:

.. code-block:: cpp

   MyVoice proto;
   proto.attack(0.01f).release(0.2f);   // your own setters

   YSE::synth syn;
   syn.create().addVoices(proto, 8);

   YSE::sound snd;
   snd.create(syn);
   snd.play();
   syn.noteOn(1, 60, 0.9f);

What you learned
----------------

- Subclass ``dspVoice`` and implement ``process()`` (audio thread) and
  ``clone()`` (setup thread).
- Allocate in the constructor / setters, never in ``process()``.
- Read note state with ``getFrequency()`` / ``getVelocity()`` / etc.
- Drive the envelope off ``intent`` and settle it to ``SS_STOPPED`` when the
  release tail ends.

Next
----

- :doc:`06_first_synth` — the synth basics this builds on.
- :doc:`/api/synth` — the ``dspVoice`` and built-in-voice reference.
