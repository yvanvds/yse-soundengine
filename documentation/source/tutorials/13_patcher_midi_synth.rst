Patcher: a synth played from MIDI
=================================

Goal: play a four-voice synth from a MIDI keyboard. ``.notein`` reads the
keyboard, ``.poly`` gives every note a voice of its own, and four copies of
the voice from :doc:`12_patcher_subpatched_voice` make the sound.

Read :doc:`12_patcher_subpatched_voice` first: this page uses its
``CreateVoice`` function unchanged. The code is compiled and run by the test
suite (`Tests/patcher/test_patcher_tutorials.cpp
<https://github.com/yvanvds/yse-soundengine/blob/dev/Tests/patcher/test_patcher_tutorials.cpp>`_),
which plays single notes and chords into the patch through the same MIDI input
path a keyboard uses, and checks that every note sounds and every release
goes quiet.

.. note::

   ``.notein`` needs the MIDI device backend, which is built on Windows and
   Linux by default but not on macOS or Android. There,
   ``CreateObject(".notein")`` returns ``nullptr`` and the patch has no MIDI
   input. See :ref:`midi-platforms`.

The patch
---------

.. code-block:: text

   .notein 0              pitch, velocity (0 = release), channel
     |    |
   .poly 4 1              voice number, pitch, velocity
     |   |   |
   .pack 0 0 0            "voice pitch velocity"
     |
   .route 1 2 3 4         strips the voice number
     |    |    |    |
   voice voice voice voice
     \    |    |    /
      ~+ -> ~+ -> ~+      sums the four voices
               |
             ~* 0.25 -> ~dac (both channels)

.. literalinclude:: ../../../Tests/patcher/test_patcher_tutorials.cpp
   :language: cpp
   :start-after: tutorial:midi-synth:begin
   :end-before: tutorial:midi-synth:end
   :dedent: 2

The keyboard
~~~~~~~~~~~~

``.notein 0`` listens to MIDI input port 0. The port number is an index into
``YSE::System().getMidiInDeviceName()``; the ``.midiinfo`` object lists the
ports from inside a patch. When the port does not open (nothing is plugged
in), the object stays valid and receives nothing, so the patch loads the same
on any machine.

For every note-on and note-off, ``.notein`` sends the channel, the velocity
and the pitch, in that order (outlets fire right to left). A release arrives
as velocity 0, whether the keyboard sent a note-off message or a note-on with
velocity 0. So the rest of the patch only has one kind of release to handle.

MIDI input reaches the patch at the start of the next audio block, on the
audio thread. :doc:`/patcher/midi` explains the path and its limits.

Voice allocation
~~~~~~~~~~~~~~~~

``.poly 4 1`` keeps a table of four voices. For each note-on it picks a free
voice and sends its number (1-4) with the pitch and velocity. For the release
of that note it sends the **same** voice number with velocity 0, so the
release reaches the voice that is playing the note. The ``1`` turns on
stealing: a fifth note takes over the voice that has played longest, and
``.poly`` sends that voice's release first.

``.poly`` also fires right to left: velocity, then pitch, then the voice
number. ``.pack 0 0 0`` stores the velocity and the pitch in its cold inlets,
and the voice number in its hot left inlet sends the finished list
``voice pitch velocity``.

``.route 1 2 3 4`` looks at the first item and sends **the rest** out of the
matching outlet. So ``2 64 100`` leaves outlet 1 as ``64 100``, which is
exactly the list a voice takes.

Mixing
~~~~~~

A signal inlet takes one cord, so the four voices are added up with a chain
of ``~+`` before they reach the ``~dac``. The ``~* 0.25`` keeps four voices
at full velocity from clipping.

Playing it
----------

.. code-block:: cpp

   YSE::System().init();

   YSE::patcher patch;
   patch.create(2);
   BuildMidiSynth(patch, "0");   // MIDI input port 0

   YSE::sound sound;
   sound.create(patch);
   sound.play();
   // ... call YSE::System().update() regularly, and play the keyboard.

To find the right port, list them first:

.. code-block:: cpp

   #if YSE_ENABLE_MIDI_DEVICE
   for (unsigned int i = 0; i < YSE::System().getNumMidiInDevices(); i++) {
     std::cout << i << ": " << YSE::System().getMidiInDeviceName(i) << "\n";
   }
   #endif

When the patcher is cleared or destroyed, ``.poly`` releases every voice
first, so no note is left hanging.

Things to try
-------------

- Add a sustain pedal: put a ``.sustain`` between ``.notein`` and ``.poly``
  (pitch into inlet 0, velocity into inlet 1, its two outlets into the two
  inlets of ``.poly``), and wire a ``.ctlin 0 0 64`` (port 0, every channel,
  controller 64) into its pedal inlet, inlet 2.
- Wire a ``.r level`` into the right inlet of the ``~* 0.25`` so the host
  can set the overall level with ``PassData``.
- Save the patch with ``DumpJSON``. It loads back with its four voices, and
  plays from the keyboard as soon as it is loaded.

Next
----

- :doc:`14_patcher_presets` — store and recall a patch's settings.
- :doc:`/patcher/midi` — every MIDI object, input and output, and the note
  handling objects.
- :doc:`/patcher/objects/midi` — reference for ``.notein``, ``.poly`` and the
  rest.
