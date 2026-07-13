Loading SFZ instruments and DX7 banks
=====================================

Goal: play a sampled instrument from an SFZ file, and an FM instrument from a
vintage DX7 SysEx bank.

The synth voice model ships two instrument voices that load their sound from
standard, portable formats rather than being coded in C++:

- :cpp:class:`YSE::SYNTH::samplerVoice` reads an **SFZ** instrument — a text
  file mapping key/velocity regions to PCM samples.
- :cpp:class:`YSE::SYNTH::fmVoice` plays a **DX7** voice; the
  :cpp:class:`YSE::SYNTH::dx7SysEx` importer fills its patch from a ``.syx``
  bank dump.

Both draw on the optional bundled content pack; the demos degrade with a
clear message when it is absent.

Sources: `Demo19_SfzPiano.cpp
<https://github.com/yvanvds/yse-soundengine/blob/master/Demo.Windows.Native/Demo19_SfzPiano.cpp>`_
and `Demo18_FMKeyboard.cpp
<https://github.com/yvanvds/yse-soundengine/blob/master/Demo.Windows.Native/Demo18_FMKeyboard.cpp>`_.

Loading an SFZ instrument
-------------------------

Construct a :cpp:class:`YSE::SYNTH::samplerVoice`, load an SFZ file into it,
then use it as the prototype for a synth. ``loadSFZ`` parses the file and
decodes every referenced sample into memory off the audio thread; it returns
``false`` if the instrument is not playable:

.. literalinclude:: ../../../Demo.Windows.Native/Demo19_SfzPiano.cpp
   :language: cpp
   :lines: 40-52

From here it is an ordinary synth: ``noteOn`` plays a mapped region,
pitch-shifting and layering as the SFZ describes.

The sustain pedal
-----------------

Sampled instruments feel right only with pedals. The synth handles CC 64
(sustain) for you — while it is down, note-offs are deferred until the pedal
lifts:

.. literalinclude:: ../../../Demo.Windows.Native/Demo19_SfzPiano.cpp
   :language: cpp
   :lines: 146-151

``sostenuto`` (CC 66) and ``softPedal`` (CC 67) work the same way; see
:cpp:class:`YSE::synth`.

Loading a DX7 bank
------------------

An FM instrument comes from a DX7 SysEx bank. Parse the ``.syx`` file into a
:cpp:class:`YSE::SYNTH::dx7Bank`, then apply one of its patches to an
:cpp:class:`YSE::SYNTH::fmVoice` prototype with ``setPatch``:

.. literalinclude:: ../../../Demo.Windows.Native/Demo18_FMKeyboard.cpp
   :language: cpp
   :lines: 40-51

A bank holds up to 32 voices; ``bank_.size()`` and ``bank_.name(i)`` let you
browse them. Because the FM core bakes operator state at key-down, a
``setPatch`` takes effect on the *next* note-on — so switching patches while
notes ring does not glitch the held voices; retrigger to hear the change.

What you learned
----------------

- ``samplerVoice::loadSFZ(path)`` builds a multi-layer sampled instrument;
  check the return value.
- The synth handles the sustain / sostenuto / soft pedals as CC 64 / 66 / 67.
- ``dx7SysEx::loadBank(path, bank)`` parses a DX7 ``.syx``; apply a patch to
  an ``fmVoice`` with ``setPatch``, which lands on the next note-on.

Next
----

- :doc:`06_first_synth` — the synth basics.
- :doc:`09_mixing_inserts_sends` — run instruments through a mixer.
- :doc:`/api/synth` — sampler, FM and importer reference.
