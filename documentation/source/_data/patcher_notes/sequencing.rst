.. Notes for the Sequencing category. Format: _data/patcher_notes/README.md.

.. patcher-object:: .mtr

``.mtr 4`` has four tracks. Each track has its own inlet and outlet: inlet 1
and outlet 1 are track 1, and so on. The leftmost inlet controls the whole
recorder, the leftmost outlet reports what ``next`` sends, and the last outlet
bangs when a file has been read.

While a track records, every message on its inlet (bang, number, list) is
stored with the time since the previous one. Playback sends each message out
of the track's outlet after the same wait, as the type it arrived as. Each
track has its own position, speed and mute, so tracks can be recorded and
played independently.

Timing is measured in audio blocks, so its resolution is one block (see
:doc:`/patcher/time`). The clock stops while the engine is paused.

**Transport**

These work in the leftmost inlet on every track, or on the tracks whose
numbers follow (``play 1 3``). In a track's own inlet they act on that track.

- ``record``: start a new recording, erasing the old one.
- ``play``: play from the beginning. In a track inlet, ``play <repeats>
  <timescale>`` plays the track several times and at a speed (``play 3 200``:
  three times at double speed).
- ``stop``: stop recording or playing. The position stays where it is.
- ``next``: send the next event of each track at once, without waiting, and
  report ``<track> <delta> <time>`` out of the leftmost outlet for each.
- ``rewind``: go back to the beginning, without playing.
- ``clear``: erase the recording.
- ``mute`` / ``unmute``: silence a track. It keeps playing, so unmuting picks
  up where the track has got to.

**Settings** (leftmost inlet only, for all tracks)

- ``timescale <percent>``: playback speed. 100 is as recorded, 200 twice as
  fast. ``play`` resets it to 100, so send it after ``play``.
- ``first <ms>``: wait this long after ``play`` before the first event. The
  recording is not changed.
- ``delay <ms>``: set the wait before the first event of every track. This
  changes the recording.
- ``embed 1``: save the recordings with the patch (off by default). See
  :ref:`data-saved`.

**Files**

``read <file>`` and ``write <file>`` in the leftmost inlet load or save every
track; in a track's inlet they load or save that track only. A read replaces
the tracks and stops them, then bangs the last outlet. A bare ``read`` or
``write`` reuses the last file name. See :doc:`/patcher/files` for the
format.

**Limits**

1-32 tracks. Each track holds 256 events of at most 128 characters; later
events are not recorded. A bang on the leftmost inlet does nothing. There is
no editing window.

.. patcher-object:: .qlist

A cue list is a list of lines. What a line does depends on how it starts:

- **Numbers only** (``500`` or ``500 60 100``): sent out of the left outlet as
  an int, a float or a list. During playback, the first number is then the
  wait, in milliseconds, before the next line.
- **A word first** (``synth 60 100``): sent to every ``.r synth``, with the
  rest of the line as the message (a bare word sends a bang). The next line
  follows at once.
- **Numbers then a word** (``500 synth 60``): stored as two lines, ``500``
  and ``synth 60``.

.. code-block:: text

   set synth 60 100; 500; synth 64 100; 500; synth 67 100

   bang  =>  "60 100" to .r synth, 500 out of the left outlet, wait 500 ms,
             "64 100" to .r synth, 500 out of the left outlet, wait 500 ms,
             "67 100" to .r synth, bang out of the middle outlet

**Playing**

- bang: play from the first line, waiting out each number line. A bang always
  starts again from the top.
- ``stop``: stop playback. The position stays where it is.
- ``next``: step by hand. Sends word lines until it reaches a number line,
  sends that and stops. It does not wait: to play by hand, wire the left
  outlet into a delay whose bang goes back into ``next``. ``next 1`` skips the
  word lines instead of sending them.
- ``fwd <n>``: ``next 1``, ``n`` times: the number lines are sent, the word
  lines skipped.
- ``rewind``: go back to the first line.
- ``tempo <f>``: speed factor for the waits. 2 plays twice as fast, 0.5 at
  half speed.
- ``clock <name>``: wait on the YSE domain clock of that name. The leading
  numbers are then beats instead of milliseconds, so playback follows that
  clock's tempo changes and stops when it is paused. A bare ``clock`` goes back
  to milliseconds. A clock that doesn't exist yet holds playback until it
  does.

The middle outlet bangs when playback or ``next`` runs past the last line,
and for an empty list.

**Writing the list**

``;`` separates lines in all three messages.

- ``set <lines>``: replace the whole list. A bare ``set`` empties it.
- ``insert <lines>``: add lines at the end.
- ``append <text>``: add the text to the end of the last line.
- ``clear``: empty the list.

**Files and saving**

``read <file>`` replaces the list with a text file and bangs the right
outlet; ``write <file>`` saves the list. A bare ``read`` or ``write`` reuses
the last file name. A read during playback lets it carry on from the top of
the new list. See :doc:`/patcher/files` for the format.

The list is saved with the patch. The position, tempo and clock are not.

**Limits**

At most 256 lines of 256 characters; longer or extra lines are ignored. Waits
shorter than one audio block take one block. ``open`` and ``wclose`` are
accepted and do nothing: there is no editing window.

.. patcher-object:: .seq

``.seq`` stores MIDI as raw bytes, the way a MIDI port sends them: a note-on
is three bytes. Feed it bytes from a MIDI input or from the ``.midi*``
formatting objects, and send its output to a MIDI output or parser.

While recording, each byte is stored with the time since the previous one.
Playback sends each byte after the same wait. Bytes that arrived together are
sent together, so a message is never split across blocks.

**Recording and playing**

- ``record``: start a new recording, erasing the old one.
- ``append``: record on after the end of the current sequence.
- a number 0-255, or a list of them: one byte each. Ignored when not
  recording. Floats are truncated; other numbers are ignored.
- bang or ``start``: play from the beginning.
- ``start <speed>``: play at a speed, where 1024 is as recorded, 512 half
  speed and 2048 double. The speed can only be set when playback starts.
- ``stop``: stop recording or playing.
- ``clear``: erase the sequence.

``record`` and ``start`` don't need a ``stop`` first.

The middle outlet bangs **just before** the last event is sent, so a patch
knows the next byte is the final one. A ``start`` on an empty sequence sends
nothing.

**External timing**

``start -1`` plays by ``tick`` messages instead of by its own clock: 48 ticks
per second play the sequence at its recorded speed. Use it to follow another
timing source.

``clock <name>`` makes the YSE domain clock of that name supply the ticks, 24
per beat, so the sequence follows that clock's tempo changes and stops when
it is paused. While a clock drives it, ``tick`` messages are ignored. A bare
``clock`` hands the ticks back to the patch. At 120 BPM, 24 ticks per beat is
the same 48 per second.

**Editing the timing**

- ``delay <ms>``: set when the first event happens; the rest move with it.
- ``addeventdelay <ms>``: add to that time.
- ``hook <factor>``: multiply every event time. 2 plays twice as slowly. It
  works during playback, from the next event on.

**Files**

``read <file>`` loads a standard MIDI file (format 0, or format 1 with up to
32 tracks, merged into one sequence) or a text file with one event per line:
a time in milliseconds, then the bytes. A read replaces the sequence and stops
playback, then bangs the right outlet. A file name given as the argument is
read when the object is created.

``write <file>`` saves a standard MIDI file, format 0. ``write <file> 1``
saves format 1: one track for meta events and anything without a channel,
then one track per MIDI channel used. A bare ``read`` or ``write`` reuses the
last file name, which starts as the argument. See :doc:`/patcher/files`.

The sequence is not saved with the patch: the file is where it is kept.

**Meta events and tempo**

Meta events from a MIDI file stay in the sequence at their place and are sent
out of the third outlet as ``meta <name> <data>``: for example ``meta tempo
120.`` (in BPM) or ``meta marker verse``. The names are ``sequencenumber``,
``text``, ``copyright``, ``sequenceortrackname``, ``instrumentname``,
``lyric``, ``marker``, ``cuepoint``, ``midichannelprefix``, ``endoftrack``,
``tempo``, ``smpteoffset``, ``timesignature``, ``keysignature`` and
``sequencerspecific``; any other type sends its number instead of a name.
Text is sent as words, everything else as numbers. A recorded sequence has no
meta events.

A read uses the file's tempo map to turn its ticks into milliseconds.
``tempo <bpm>`` sets the tempo written into files. With ``overridetempo 1``
the next read uses that tempo instead of the file's. Without it, a tempo event
passing during playback updates ``tempo``.

**Limits**

4096 events, where each byte is one event: about 1365 three-byte messages.
Further bytes are not recorded. ``dump``, ``print`` and ``sequencetempo`` are
accepted and do nothing; there is no editing window.


.. patcher-object:: ~techno

A step sequencer that runs at signal rate: a phase from 0 to 1 (usually a
``~phasor``) goes in, and a pitch signal, an amplitude envelope and the
position in the sequence come out. Multiply an oscillator at the pitch by the
envelope for drum patterns or an acid line, timed to the sample.

The argument is how many steps the sequencer has room for (16 by default, up
to 1024). It starts with one step; ``length <n>`` uses the first ``n``.

**Messages**

Each takes a step number (from 0) and a value. A step number past the room
the argument made is ignored.

- ``pitch <step> <Hz>``: the step's pitch. 440 to start with.
- ``amplitude <step> <a>``: the envelope's value where the step starts. 1
  to start with.
- ``curve <step> <e>``: how the pitch slides from the previous step's over
  the step: 1 is a straight slide, 0.5 a square-root curve, 2 a parabolic
  one. 0 (the start) jumps with no slide.
- ``decay <step> <e>``: how the step's note dies away to 0 by the next step.
  1 (the start) is a straight line, larger is quicker, 0 never decays.
- ``attack <step> <e>``: how the step's note rises from 0 over the step
  before it. 1 (the start) is a straight line, larger rises later and
  sharper.
- ``pos <step> <start>``: where the step starts, as a fraction of the whole
  sequence. A step cannot move before the previous step or past the next.
- ``length <n>``: how many steps the sequence has. Changing it spaces the
  steps evenly again, so send ``pos`` and ``repeatpos`` after it.
- ``repeatpos <v> ...``: start positions repeated over the sequence, for
  swing and other uneven spacing. With ``n`` values the sequence is cut into
  segments of ``n + 1`` steps, which must divide the length; the values,
  increasing and between 0 and 1, are where steps 1 to ``n`` of each
  segment start within it. ``repeatpos 0.66`` swings every pair of steps.

A number in the inlet is a constant phase while no signal is connected.
Outside 0 to 1 the phase is clamped.

**Outputs**

The envelope is the current step's note decaying plus the next step's note
rising, so with gentle shapes on both the two notes overlap. The position
counts steps: 2.5 is halfway through step 2.

Messages are not saved with the patch; the argument is.

.. code-block:: text

   [~phasor 2] -> [~techno 8] -> pitch -> [~saw] -> [~*] <- envelope
.. patcher-object:: ~seq

Stores messages at positions in a cycle and sends each one when a driving
0 to 1 ramp, normally a ``~phasor``, crosses its position. The timing comes
from the ramp, so speeding the ``~phasor`` up plays the sequence faster, and
an event is crossed on the exact sample where the ramp passes it. Not to be
confused with ``.seq``, which plays MIDI files.

**Sequences**

Events are kept in numbered sequences, up to 1024 events in all. One
sequence at a time is current: ``seq <n>`` (or ``seqnum``) picks it, and it
is the one played and recorded into. Sequence 0 is current to start with.

- ``add <seq> <phase> <message>`` stores a message at a phase (0 to 1) in
  any sequence: ``add 2 0.5 honk``. Events at the same phase go out in the
  order they were added.
- ``delete <seq> <phase>`` removes the events at that phase;
  ``delete <seq> <from> <to>`` those in a range; and a message after the
  range only the events that are that message: ``delete 2 0 1 honk``
  removes every ``honk`` in sequence 2.
- ``clear <seq>`` (or ``erase``) empties a sequence, ``clear`` alone the
  current one, and ``clear all`` every one.
- ``offset <n>`` is added to every sequence number a message names from
  then on.

**Playing and recording**

- ``play 1`` plays the current sequence from where the ramp is; ``play 0``
  stops. Each event goes out of the left outlet as the message it was
  stored as: a number, a list or a word message. When the ramp jumps back
  by more than half a cycle, as a ``~phasor`` does when it wraps, the events
  up to the end of the cycle go out and play continues from the start. A
  smaller step back sends nothing.
- While ``overdub 1`` is on, any number, list or message sent to the inlet
  that is not one of the commands here is stored in the current sequence at
  the ramp's position. ``record 1`` does the same after emptying the
  sequence first. A recorded event plays the next time the ramp comes
  round. Both stop with 0.
- All of these take effect at the start of the next audio block, and the
  messages go out at the end of the block they were crossed in. Within a
  block they keep their order.

**Reports and files**

- ``dump`` sends one ``sequence phase message`` list per event out of the
  middle outlet, ``dump <seq>`` only that sequence's.
- A bang sends ``sequence play record overdub events-in-sequence
  events-in-all`` out of the right outlet.
- ``write <file>`` saves every sequence as text, one ``sequence phase
  message`` line per event, and ``read <file>`` replaces every sequence with
  a saved file's. A malformed file changes nothing. Sequences are not saved
  with the patch.

.. code-block:: text

   [.m add 0 0 60] [.m add 0 0.5 67] [.m play 1]
              \          |          /
   [~phasor 1] -> [~seq] -> [.mtof] -> [~cycle]
                  two notes a second, one at each half of the cycle
