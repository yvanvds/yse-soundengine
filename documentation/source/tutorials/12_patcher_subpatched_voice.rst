Patcher: a voice in a subpatcher
================================

Goal: build a synth voice once, as a subpatcher with one inlet and one
outlet, and use it like any other object. The voice takes a note as the list
``pitch velocity``, fades in when the velocity is above 0 and fades out when
it is 0.

This tutorial assumes you have worked through :doc:`05_patcher`. Its code is
compiled and run by the test suite
(`Tests/patcher/test_patcher_tutorials.cpp
<https://github.com/yvanvds/yse-soundengine/blob/dev/Tests/patcher/test_patcher_tutorials.cpp>`_),
which plays a note, checks its pitch, releases it, and loads a saved copy of
the patch. :doc:`13_patcher_midi_synth` uses four of these voices.

The voice
---------

.. code-block:: text

      inlet 0: "pitch velocity"
          .inlet 0
             |
         .unpack 0 0
          |        \
        .mtof      ./ 127      velocity to 0-1
          |          |
        ~saw      ~line 0 20   20 ms ramp to the new level
          |          |
          ~*  <------+          (right inlet: gain)
          |
       ~outlet 0
      outlet 0: audio

A subpatcher is an object of type ``patcher``. You create its contents with
the same ``CreateObject`` as everything else, and put them inside with
``SetContainer``. Its pins are the boundary objects inside it: ``.inlet 0``
is its inlet 0 (for messages), and ``~outlet 0`` is its outlet 0 (for audio).

.. literalinclude:: ../../../Tests/patcher/test_patcher_tutorials.cpp
   :language: cpp
   :start-after: tutorial:voice:begin
   :end-before: tutorial:voice:end
   :dedent: 2

What each part does:

- ``.unpack 0 0`` splits the list into two ints. Its outlets fire right to
  left, so the velocity leaves before the pitch.
- ``.mtof`` → ``~saw`` sets the pitch.
- ``./ 127`` scales the velocity to 0-1, and ``~line 0 20`` ramps to that
  level over 20 ms. The ramp is the envelope: it rises on a note-on and
  falls back to 0 on a release (velocity 0). Without it, every note would
  click on and off.
- ``~*`` multiplies the oscillator by the envelope. Its right inlet takes an
  audio signal here, not a number.

Draw the cords *after* the objects are inside the subpatcher, as the function
does. :doc:`/patcher/subpatchers` explains why.

Using the voice
---------------

From the outside, the voice is one object with inlet 0 and outlet 0. Connect
to it by pin number, as to any other object:

.. literalinclude:: ../../../Tests/patcher/test_patcher_tutorials.cpp
   :language: cpp
   :start-after: tutorial:voice-parent:begin
   :end-before: tutorial:voice-parent:end
   :dedent: 2

``Connect(note, 0, voice, 0)`` draws a cord to the ``.inlet 0`` inside the
voice, and ``Connect(voice, 0, dac, 0)`` draws one from its ``~outlet 0``. The
parent never names what is inside.

Attach the patcher to a sound (see :doc:`05_patcher`), then play a note and
release it:

.. literalinclude:: ../../../Tests/patcher/test_patcher_tutorials.cpp
   :language: cpp
   :start-after: tutorial:voice-play:begin
   :end-before: tutorial:voice-play:end
   :dedent: 4

.. literalinclude:: ../../../Tests/patcher/test_patcher_tutorials.cpp
   :language: cpp
   :start-after: tutorial:voice-release:begin
   :end-before: tutorial:voice-release:end
   :dedent: 4

The host can also skip the ``.r`` and send straight into the voice's inlet:
``voice->SetListData(0, "57 100")``.

Several voices
--------------

``CreateVoice`` is a function, so a patch can hold as many voices as it
calls it. Two things to know when you do:

- **A signal inlet takes one cord.** To play several voices through one
  ``~dac``, add them up with ``~+`` first (or give each its own ``~dac``: a
  patch's ``~dac`` objects are summed). :doc:`13_patcher_midi_synth` sums
  four voices with a chain of ``~+``.
- **Names are not local to a subpatcher.** A ``.r freq`` inside a voice would
  receive every ``freq`` sent to the patcher, in every copy of the voice.
  That is why this voice has an inlet instead of a receiver: a parent can
  address one copy through its pins. :doc:`/patcher/subpatchers` covers this.

Saving and loading
------------------

``DumpJSON`` saves the voice as a record of type ``patcher``, and every
object inside it with a ``container`` key that points to it. ``ParseJSON``
rebuilds the same nesting, and the loaded copy plays the same way. The test
for this page saves the patch, loads it into a new patcher and plays a note
through the copy. :doc:`/patcher/file_format` describes the records.

Next
----

- :doc:`13_patcher_midi_synth` — four of these voices, played from a MIDI
  keyboard.
- :doc:`/patcher/subpatchers` — pins, nesting, editing a subpatcher while it
  plays, and the C API.
- :doc:`/patcher/objects/subpatchers` — reference for ``patcher``,
  ``.inlet``, ``.outlet``, ``~inlet`` and ``~outlet``.
