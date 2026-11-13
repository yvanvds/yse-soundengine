.. Notes for the Sampling category. Format: _data/patcher_notes/README.md.

.. patcher-object:: ~buffer

Holds audio samples under a name. The sampling objects that name the same
buffer play it, record into it or read it. The name is shared by every
patch and by the host program: a host can load ``drums`` and a patch can
play it, or the other way round. Without a name the buffer is private.

The arguments are ``name [file] [duration] [channels]``. The file is loaded,
and the size (in milliseconds) and channel count are set, when the object is
created. A number in the file's place is the duration. A ``~buffer`` that
gives only a name keeps whatever the buffer already holds.

**Messages**

- ``read <file> [start] [duration] [channels]``: load an audio file. Times
  are in milliseconds. Without a duration the whole file is read and the
  buffer takes its length. With one, the buffer keeps its length, is cleared,
  and that much is read into its start. The buffer keeps its own channel
  count unless ``channels`` is given (``0`` takes the file's); extra file
  channels are mixed into the buffer's channels in turn.
- ``replace <file>``: load the whole file and take its length and channel
  count.
- ``readagain``: repeat the last ``read`` or ``replace``.
- ``write <file>``: save every channel. The extension picks the format:
  ``.aif`` / ``.aiff``, ``.flac``, anything else WAV.
  ``writewave``, ``writeaiff`` and ``writeflac`` choose it explicitly.
- ``clear``; ``fill <value>``; ``fill sin [cycles] [divisor]`` and
  ``fill cos ...`` (``fill cos 1 4`` is a quarter cycle).
- ``apply <window> [half]``: multiply by a ``triangle``, ``hamming``,
  ``hanning``, ``blackman``, ``welch`` or ``kaiser [beta]`` window.
  ``half`` uses the falling half only, as a fade out. Also
  ``apply gain <factor>``, ``apply offset <value>`` and ``apply getdeltas``
  (each sample minus the one before).
- ``normalize [peak]``: scale so the loudest sample is ``peak``
  (default 1).
- ``setsize <ms> [channels]`` and ``sizeinsamps <samples> [channels]``:
  resize, keeping what fits.
- ``crop <start> <end>`` (ms) and ``crop_samples <start> <end>``: keep only
  that range.
- ``duplicate <name>``: copy another buffer.
- ``sr <rate>``: set the sample rate the samples are at, without changing
  them.
- ``getsamps``, ``getsize``, ``getsr``, ``getchans``: send ``samps <n>``,
  ``size <ms>``, ``sr <Hz>`` or ``chans <n>`` out of the right outlet.

Every message is carried out in the background, in the order it was sent,
so the audio never waits for a file. A query answers after the messages sent
before it. The left outlet bangs when a ``read``, ``replace`` or
``readagain`` has finished; a file that cannot be read sends nothing. Up to
four messages can be waiting at once in one patcher; more are ignored.

.. code-block:: text

   [~buffer drums kit.wav]       loads kit.wav when the patch opens
   [~buffer scratch 2000 2]      two seconds of silence, in stereo

.. patcher-object:: ~info

Reports what the buffer of its name holds. The argument names the buffer;
``set <name>`` names another one, and ``set`` alone names none. A bang sends
the report, from the right outlet to the left, so the sample rate arrives
last:

- outlet 9: the path of the file last loaded into the buffer, as it was given
  to ``read`` or ``replace``;
- outlet 8: how many channels that file has. Without a file, the buffer's own
  channel count;
- outlet 7: the file's name, without its folders;
- outlet 6: the buffer's length in milliseconds;
- outlets 5 and 4: the end and start of the file's release loop, in
  milliseconds;
- outlets 3 and 2: the end and start of its sustain loop;
- outlet 1: the file's instrument data as one list: base note, detune in
  cents, lowest and highest note, lowest and highest velocity, and gain;
- outlet 0: the rate the samples are at, in Hz.

What the buffer does not have is not sent: no file name, path, loops or
instrument data before a file is loaded, and no loop the file does not
define. The loops and instrument data come from an AIFF file's instrument
chunk (sustain and release loop) or a WAV file's sampler chunk (its first
two loops; a WAV file states no key or velocity range and no gain, so those
come out as 0 127 0 127 1). Loop points are where the file puts them: they do
not move when the buffer is cropped or resized afterwards. The file details
stay until another file is loaded.

The report is gathered in the background and arrives at the start of a later
audio block, after any message sent to a ``~buffer`` in the same patch before
the bang. So ``read`` followed by a bang reports the new file. The usual
patch wires the left outlet of ``~buffer`` (a bang when a file has loaded)
into ``~info``:

.. code-block:: text

   [~buffer drums]
    |
   [~info drums]      reports every file loaded into drums

.. patcher-object:: ~peek

Reads and writes one sample of the buffer of its name at a time, by message.
The arguments are ``name [channel] [clip]``. Channels count from 1, sample
indexes from 0.

- An index (an int, or a float, which is truncated) in the left inlet sends
  the sample there out of the outlet.
- A value sent to the middle inlet first is written at the next index
  instead, and nothing is sent. The value is used once.
- A list ``index value [channel]`` in the left inlet writes at once. A
  channel given there stays the channel afterwards.
- The right inlet sets the channel.
- ``clip 1`` (the default, also the third argument) limits written values to
  -1 to 1; ``clip 0`` writes them as they are.

An index or channel outside the buffer reads and writes nothing. Everything
happens as the message arrives, so ``~peek`` works with the audio stopped,
and a value it writes is heard by every object playing the buffer from the
next sample they read. Messages that reach one ``~peek`` from two threads
at the same moment are not queued: the second one is ignored.

``set <name>`` switches to another buffer; ``set`` alone leaves ``~peek``
without one. The buffer is looked up in the background and the next index
or list uses it, with the audio stopped too. The switch is not saved with
the patch: a reloaded ``~peek`` uses the buffer of its argument.

.. code-block:: text

   [~peek table]     5 in the left inlet sends sample 5
                     0.5 in the middle, then 5 on the left, writes 0.5 there

.. patcher-object:: ~poke

Writes a signal into the buffer of its name, each sample at the position a
second signal gives. The arguments are ``name [channel]``; channels count
from 1.

- Left inlet: the values. A number is used while no signal is connected. A
  list ``value index [channel]`` writes one value at once; a channel given
  there stays the channel afterwards.
- Middle inlet: where each value goes, in samples from 0, truncated to a
  whole sample. A number is used while no signal is connected; it starts at
  -1. An index below 0, or past the end of the buffer, writes nothing, so
  -1 pauses writing.
- Right inlet: the channel.

Values are written as they are, without clipping, and every reader of the
buffer hears them at once. A signal counter into the middle inlet records:

.. code-block:: text

   [~adc]          -> [~poke loop] left inlet: the samples to record
   [~count 0 0 1]  -> [~poke loop] middle inlet: 0, 1, 2 ... one per sample

Changing the buffer from a ``~buffer`` message while ``~poke`` writes is
safe; whatever ``~poke`` writes while the change is being made is lost, and
it carries on in the changed buffer.

``set <name>`` in the left inlet switches to another buffer, looked up in
the background and written from a later block on; ``set`` alone stops
writing. The switch is not saved with the patch: a reloaded ``~poke``
writes the buffer of its argument.

.. patcher-object:: ~index

Reads the buffer of its name at the positions a signal gives, one sample
each, without interpolation. The arguments are ``name [channel]``; channels
count from 1.

- Left inlet: where to read, in samples from 0. A fractional position reads
  the whole sample below it. A number is used while no signal is connected;
  it starts at 0. Below 0 reads the first sample and past the end the last.
- Right inlet: the channel. A channel the buffer does not have reads
  silence, as does a name no buffer holds samples for yet.

A signal counter into the left inlet plays the buffer back at its own pace,
one sample per sample, which suits a buffer used as a table of values (a
window shape, a step sequence) rather than a sound to play at another pitch:

.. code-block:: text

   [~count 0 512 1]   0, 1, 2 ... 511, and again
    |
   [~index window]    the samples of window, in order

Reading follows the same rule for a fractional position as ``~poke``
writing, so one index signal into both reads back what was written there.
Changes to the buffer, from a ``~buffer`` message or a ``~poke``, are heard
from the next block.

``set <name>`` in the left inlet switches to another buffer, looked up in
the background and read from a later block on; ``set`` alone reads silence.
The switch is not saved with the patch: a reloaded ``~index`` reads the
buffer of its argument.

.. patcher-object:: ~table

Reads the first channel of the buffer of its name as a table of values:
entry 0 is its first sample. The arguments, all after the name optional,
are ``name interp extend inmap_lo inmap_hi outscale_lo outscale_hi
inputmode triggermode range signed``; a message of the same name changes
each while the patch runs.

- ``interp``: 0 reads the entry at or below the index, 1 a straight line
  between the two around it (1.5 between 5 and 10 reads 7.5), 2 the
  nearest entry.
- ``extend``: an index outside the table reads 0 (0), the first or last
  entry (1), the entry it reaches with the table repeated (2: 130 in a
  table of 128 reads entry 2), or leaves the output as it was (3).
- ``inmap lo hi``: the input from ``lo`` to ``hi`` covers the table,
  whatever its length. ``inmap 0 1`` turns a 0-1 phase into an index, so a
  ``~phasor`` with ``extend 2`` and ``interp 1`` plays the table as a wave.
  ``inmap none``, or two equal numbers, turns it off.
- ``outscale lo hi``: the value range comes out as ``lo`` to ``hi``. The
  value range is 0 to ``range - 1``, or ``-range`` to ``range`` with
  ``signed 1``; ``range`` starts at 128. ``outscale none`` turns it off.
- ``inputmode``: 0 reads at the index the input gives. 1 steps to the next
  entry on every trigger in the input, 2 jumps to a random entry, each
  entry as likely as its value is large (entries at 0 or below are never
  chosen). ``triggermode`` 0 counts a change from 0 to anything else as a
  trigger, 1 any change.

.. code-block:: text

   [~phasor 2]
    |
   [~table curve 1 2 0 1]   the buffer curve, twice a second, smoothly

A number in the inlet is used while no signal is connected: the index, or
a trigger in inputmodes 1 and 2. ``next`` and ``prev`` step one entry up or
down, wrapping round the ends, and keep a fractional part. ``goto <index>``
sets where the next ``next`` or ``prev`` goes without changing the output;
a new ``~table`` reads entry 0 on its first ``next``. ``bang`` jumps to a
random entry by weight, whatever the mode.

The table is as long as the buffer. Fill it with a ``~buffer`` message, a
``~poke``, or from the host; changes are heard from the next block.
``name <name>`` or ``set <name>`` switches to another buffer, looked up in
the background and read from a later block on; ``set`` alone reads
silence. Messages are not saved with the patch: a reloaded ``~table`` reads
the buffer and uses the settings of its arguments.

.. patcher-object:: ~lookup

Uses part of the buffer of its name as a transfer function: a signal from
-1 to 1 reads the table from its first entry to its last, and the value
found there comes out. Draw a curve into the buffer and every signal sent
through is bent by it, which is waveshaping distortion. A straight line from
-1 to 1 lets the signal through unchanged; a curve that flattens towards the
ends softens the peaks; a squaring curve doubles a sine's pitch.

.. code-block:: text

   [~cycle 220]
    |
   [~lookup curve]   the sine, bent by the curve drawn into curve

The arguments are ``name [offset] [size] [chan]``. The table starts at
frame ``offset`` (default 0), which -1 reads, and ends before frame
``size`` (default 512), counted from the start of the buffer, so it holds
``size - offset`` entries: a larger offset makes the table shorter. A size
past the end of the buffer stops at its last frame, so a buffer shorter
than 512 samples is used whole. ``chan`` is the buffer channel, from 1.

- Left inlet: the signal looked up, or a number used while no signal is
  connected. 0 reads the middle of the table, and a position between two
  entries reads the straight line between them. Beyond -1 or 1 the output
  is 0, so keep the input in range (a ``~clip -1 1`` before it, for
  instance).
- Middle inlet: the offset. Right inlet: the size. Both take a number, or a
  signal read at the first sample of each block.

``chan <n>``, ``offset <n>`` and ``size <n>`` in the left inlet change the
channel and the table while the patch runs. ``set <name>`` switches to
another buffer, looked up in the background and read from a later block on;
``set`` alone reads silence, as does a table without entries. Changes to the
buffer, from a ``~buffer`` message, a ``~poke`` or the host, are heard from
the next block. Messages and numbers are not saved with the patch: a
reloaded ``~lookup`` uses its arguments.

.. patcher-object:: ~record

Records signals into the buffer of its name. The arguments, all after the
name optional, are ``name channels append loop loopstart loopend``.
``channels`` (1 to 64) sets how many signal inlets there are: the first
signal goes into buffer channel 1, the second into channel 2, and so on. A
buffer with fewer channels drops the signals it has no channel for, and
its channels past the last signal are left as they are.

- The signal inlets record what reaches them. An unconnected one records
  silence.
- A number other than 0 in the left inlet starts recording, 0 stops it.
  Changes take effect at the next block.
- The two inlets on the right set the start point and the end point, in
  milliseconds into the buffer. An end point of 0, the default, is the end
  of the buffer; ``reset`` puts both back to 0, so recording covers the
  whole buffer again. ``loopstart <ms>`` and ``loopend <ms>`` in the left
  inlet do the same as the two inlets.
- A start begins at the start point, also while recording already runs.
  With ``append 1`` it carries on where the last recording stopped instead,
  as long as that is between the points.
- Recording stops when it reaches the end point. With ``loop 1`` it
  carries on from the start point, over and over, until it is stopped.
- The outlet sends a sync signal: while recording, where each sample goes,
  rising from 0 at the start point to 1 at the end point; 0 while not
  recording.

The points are milliseconds at the buffer's own sample rate and follow its
length, so they stay right after the buffer is resized or a file is read
into it. Samples are written as they arrive, not resampled. Every object
playing the buffer hears what was recorded from the next block, so a
recording can be played while it is being made:

.. code-block:: text

   [~adc]
    |
   [~record loop 1 0 1]   1 starts it; the buffer loop, over and over
    |
   [~snapshot 100]        where the recording is, 10 times a second

Editing the buffer from a ``~buffer`` message while ``~record`` writes is
safe; whatever it records while the change is being made is lost, and it
carries on in the changed buffer. With no buffer, or an empty one,
nothing is recorded and the sync signal is 0, but a started recording
waits and begins once the buffer has room.

``set <name>`` in the left inlet switches to another buffer, looked up in
the background and written from a later block on; a recording carries on
there from where it had got to. ``set`` alone stops writing. Messages are
not saved with the patch: a reloaded ``~record`` uses the buffer and the
settings of its arguments.

.. patcher-object:: ~play

Plays the buffer of its name. The arguments, all after the name optional,
are ``name channels loop loopinterp interptime``. ``channels`` (1 to 64)
sets how many signal outlets there are: the first plays buffer channel 1,
the second channel 2, and so on. Outlets the buffer has no channel for are
silent, and buffer channels past the last outlet are mixed in, channel
``n + channels`` into the outlet of channel ``n``. The rightmost outlet
bangs when playback reaches its end point.

``~play`` plays in one of two ways, decided by what is connected to its
left inlet.

- **With a signal connected**, the signal is the position to play, in
  milliseconds into the buffer: a rising signal plays forward, a falling
  one backward, a still one repeats one sample. Positions before the start
  or past the end of the buffer hold its first or last sample. A ``~line``
  ramping from one point to another over a time plays that part of the
  buffer at the speed the time gives.
- **With nothing connected**, ``~play`` plays on its own. A number other
  than 0 plays the whole buffer at normal speed, 0 stops.
  ``start <start> <end> <duration>`` plays from the start point to the
  end point, in milliseconds, over the duration; every number is optional.
  Without a duration it plays at normal speed, without an end point to the
  end of the buffer, and without numbers the whole buffer. An end point
  before the start point plays backward. ``stop`` stops, ``pause`` holds
  the position and ``resume`` goes on from it. Changes take effect at the
  next block, and of several sent before a block the last one counts.

When playback reaches the end point it stops and the right outlet bangs,
after the block in which it ended. With ``loop 1`` it carries on from the
start point instead, until it is stopped, and does not bang. With
``loopinterp 1`` as well, the last ``interptime`` milliseconds before each
jump back (50 by default, at most the loop's length) are crossfaded with
the sound leading up to the start point, so the loop does not click. A
loop at the very start of the buffer crossfades toward its first sample.
A ``stop`` is not an end and does not bang, and a position signal never
ends.

Samples are read between the frames with a 4-point interpolation. The
points are milliseconds at the buffer's own sample rate, and normal speed
follows both that rate and the engine's, so a buffer recorded at another
rate plays at its own pitch. Points past the buffer are held at its end
and follow it when it is resized or a file is read into it:

.. code-block:: text

   [start 0 1000 500(
    |
   [~play drums]          the first second, twice as fast
    |          \
   [~dac]      [.counter] how many times it finished

With no buffer, or an empty one, ``~play`` plays silence, but a started
playback waits and begins once the buffer has samples. ``set <name>`` in
the left inlet switches to another buffer, looked up in the background
and played from a later block on, from the position playback had reached;
``set`` alone plays silence. Messages are not saved with the patch: a
reloaded ``~play`` uses the buffer and the settings of its arguments, and
starts stopped.

.. patcher-object:: ~groove

Plays the buffer of its name at the speed of the signal in its left inlet:
the sampler voice. The arguments, all after the name optional, are
``name channels loop loopinterp interptime loopstart loopend``.
``channels`` (1 to 64) sets how many sample outlets there are, as for
``~play``: the first plays buffer channel 1, and so on, with missing
channels silent and extra ones mixed in. The rightmost outlet is the sync
signal.

- **Speed.** 1 plays at normal speed, 2 twice as fast and an octave up,
  0.5 at half speed, -1 backward. The speed can change at every sample, for
  vibrato or scratching. At 0 the position stands still and the outlets
  hold the sample there. With nothing connected the speed is 0.
- **Position.** A number in the left inlet jumps to that position, in
  milliseconds into the buffer, and plays from there. ``startloop`` jumps
  to the loop start and ``endloop`` to the loop end. ``stop`` silences
  ``~groove`` until the next number, ``startloop`` or ``endloop``. A new
  ``~groove`` plays from 0 as soon as its speed is not 0. Changes take
  effect at the next block, and of several sent before a block the last
  one counts.
- **Loop points.** The middle inlet sets the loop start and the right
  inlet the loop end, in milliseconds; an end of 0 is the end of the
  buffer, so the loop is the whole buffer until it is set.
  ``setloop <start> <end>``, ``loopstart <ms>`` and ``loopend <ms>`` set
  them too, and ``reset`` goes back to the whole buffer. A signal
  connected to one of those inlets sets its point once per block, from the
  block's first sample.
- **Looping.** With ``loop 0`` (the default) the loop points are played
  through, and playback stops when it runs off either end of the buffer.
  With ``loop 1``, playing forward past the loop end goes on from the loop
  start, and playing backward past the loop start goes on from the loop
  end; a position before the loop plays into it. With ``loop 2`` playback
  turns round at each loop point and swings back and forth between them.
- **Crossfade.** With ``loop 1`` and ``loopinterp 1``, the last
  ``interptime`` milliseconds before each jump (50 by default, at most the
  loop's length) are crossfaded with the sound on the far side of the point
  it jumps to, so the loop does not click.
- **Sync.** The right outlet ramps from 0 at the loop start to 1 at the
  loop end as playback goes through the loop, whichever way it goes, and
  is 0 outside the loop and while stopped: a phase to drive envelopes or
  other players in time with the loop.

.. code-block:: text

   [~sig 1]   [setloop 250 750(   [loop 1(
    |          |                  |
   [~groove drums]                half a second, round and round
    |          \
   [~dac]      [~snapshot]        where in the loop it is

Samples are read between the frames with a 4-point interpolation. Normal
speed follows both the buffer's sample rate and the engine's, so a buffer
recorded at another rate plays at its own pitch. Points past the buffer are
held at its end and follow it when it is resized or a file is read into it.

With no buffer, or an empty one, ``~groove`` plays silence and keeps its
position. ``set <name>`` in the left inlet switches to another buffer,
looked up in the background and played from a later block on, from the
position playback had reached; ``set`` alone plays silence. Messages are
not saved with the patch: a reloaded ``~groove`` uses the buffer and the
settings of its arguments.

.. patcher-object:: ~phasegroove

Plays the buffer of its name where a ramp in its left inlet says: 0 is the
loop start and 1 the loop end, so a ``~phasor`` plays the loop once per cycle
whatever its length. That locks a loop to a tempo, and a slice to a step. The
arguments, all after the name optional, are
``name channels loopinterp interptime loopstart loopend``. ``channels`` (1
to 64) sets how many sample outlets there are, as for ``~groove``: the first
plays buffer channel 1, and so on, with missing channels silent and extra
ones mixed in. The rightmost outlet is the sync signal. There is no ``~groove``
to connect: ``~phasegroove`` is the player.

- **Phase.** A ramp that rises plays forward, one that falls plays backward,
  and one that stands still holds the sample there; the ramp's speed is the
  loop's. A phase outside 0 to 1 wraps round the loop, so 1.25 reads where
  0.25 does and a ramp with an offset added plays the loop from further on. 1
  itself is the loop end, where a one-shot ramp comes to rest. With nothing
  connected the phase is the last number sent to the inlet, else 0.
- **Loop points.** The middle inlet sets the loop start and the right inlet
  the loop end, in milliseconds; an end of 0 is the end of the buffer, so the
  loop is the whole buffer until it is set. ``setloop <start> <end>``,
  ``loopstart <ms>`` and ``loopend <ms>`` set them too, and ``reset`` goes back
  to the whole buffer. A signal connected to one of those inlets sets its point
  once per block, from the block's first sample. A loop whose end is not after
  its start plays silence.
- **Crossfade.** With ``loopinterp 1``, the last ``interptime`` milliseconds
  before the ramp wraps (50 by default, at most the loop's length) are
  crossfaded with the sound on the far side of the point it wraps to, so the
  loop does not click. It follows the way the ramp is moving.
- **Sync.** The right outlet is the phase each sample was read at, kept in 0
  to 1: the ramp, wrapped.

.. code-block:: text

   [~phasor 2]   [setloop 250 750(
    |             |
   [~phasegroove drums]              half a second of it, twice a second
    |
   [~dac]

Samples are read between the frames with a 4-point interpolation. The ramp
sets the tempo, so the engine's sample rate plays no part; the loop points are
milliseconds of the buffer, held at its end and following it when it is resized
or a file is read into it.

With no buffer, or an empty one, ``~phasegroove`` plays silence.
``set <name>`` in the left inlet switches to another buffer, looked up in the
background and played from a later block on; ``set`` alone plays silence.
Messages and numbers are not saved with the patch: a reloaded ``~phasegroove``
uses the buffer and the settings of its arguments.

.. patcher-object:: ~wave

Reads a region of the buffer of its name as one cycle of a waveform: a phase
from 0 to 1 in the left inlet scans the region from its start to its end. Fed
by a ``~phasor``, it repeats the region at the phasor's frequency, which makes
any sample a wavetable oscillator. A falling ramp plays the region backward.

.. code-block:: text

   [~phasor 110]
    |
   [~wave voice 200 210]   10 ms of the voice, 110 times a second

The arguments, all after the name optional, are
``name start end channels interp interp_bias interp_tension``.

- **Region.** ``start`` and ``end`` are milliseconds into the buffer; an end
  of 0 (the default) is the end of the buffer, so the region is the whole
  buffer until it is set. The middle inlet sets the start and the right inlet
  the end; a signal connected there sets its point once per block, from the
  block's first sample. A list of two numbers in the left inlet sets both,
  and a list of one sets the start. A region whose end is not after its start
  reads silence. Points past the buffer are held at its end and follow it when
  it is resized or a file is read into it.
- **Phase.** The region is a cycle, so the phase wraps: 1 reads where 0
  does, 1.25 where 0.25 does, -0.25 where 0.75 does. With nothing connected
  the phase is the last number sent to the left inlet, else 0.
- **Channels.** ``channels`` (1 to 64, default 1) sets how many outlets
  there are: the first reads buffer channel 1, and so on. Outlets the buffer
  has no channel for are silent, and buffer channels past the last outlet are
  not read.
- **Interpolation.** ``interp`` sets how a position between two samples is
  read: 0 the sample before it, 1 (the default) and 2 a straight line, 3 a
  half cosine, 4 a cubic curve through four samples, 5 a Catmull-Rom spline,
  6 a Hermite curve. ``interp_tension`` (0 normal, 1 straight, -1 rounder) and
  ``interp_bias`` (0 even, above 0 leaning to the sample before, below 0 to
  the sample after) shape the Hermite curve; at 0 and 0 it is the spline. The
  samples next to the region's ends wrap round it, so the last sample leads
  back into the first and the cycle loops without a seam. ``interp <n>``,
  ``interp_tension <n>`` and ``interp_bias <n>`` in the left inlet change
  them while the patch runs.

The region is milliseconds of the buffer at the buffer's own sample rate; the
engine's rate plays no part. ``set <name> [start] [end]`` in the left inlet
switches to another buffer, looked up in the background and read from a later
block on, with the region after the name, or the whole buffer without one;
``set`` alone reads silence, as does an empty buffer. Changes to the buffer,
from a ``~buffer`` message, a ``~poke`` or the host, are heard from the next
block. Messages, lists and numbers are not saved with the patch: a reloaded
``~wave`` uses its arguments.

.. patcher-object:: ~2d.wave

A two-dimensional wavetable. Like ``~wave``, it reads a region of the buffer
of its name at a phase from 0 to 1, but the region is cut into ``rows`` rows
of equal length, each one cycle. The left inlet's phase scans a row, and the
second inlet's row phase picks the row: 0 the first, 1 the last. Between two
rows their outputs are crossfaded, so a slow ramp in the second inlet morphs
the sound from the first cycle to the last while a fast one in the first sets
the pitch.

.. code-block:: text

   [~phasor 220]  [~phasor 0.25]
    |              |
   [~2d.wave cycles 0 0 1 16]   16 single cycles, morphed once every 4 s

The arguments, all after the name optional, are
``name start end channels rows``. ``start``, ``end`` and ``channels`` work as
on ``~wave``, through the third and fourth inlets and a list in the left
inlet. ``rows`` is the number of rows, 1 by default; 0 and 1 are one row, and
``rows <n>`` in the left inlet changes it. The frames left over when the
region does not divide evenly are at its end and are not read, and more rows
than the region has frames reads silence.

- **Phase.** Wraps as on ``~wave``, round each row: a row's last sample leads
  back into its first. Between two samples it reads a straight line.
- **Row phase.** A signal, sample by sample, or the last number sent to the
  second inlet while nothing is connected, else 0. Below 0 reads the first
  row and above 1 the last.

``set <name> [start] [end]``, an empty buffer and saving work as on ``~wave``:
messages, lists and numbers are not saved, and a reloaded ``~2d.wave`` uses
its arguments.

.. patcher-object:: ~buffir

A FIR filter whose coefficients, its taps, are samples of the buffer of its
name: every output sample is the last few input samples, each multiplied by
one tap and added up. Put an impulse response or a filter kernel into the
buffer, and a signal through ``~buffir`` is filtered by it: a custom low or
high pass, an equaliser curve, the short response of a speaker cabinet or an
instrument body. A single click through it plays the taps back in order.

.. code-block:: text

   [~adc 1]
    |
   [~buffir cab 0 512]   the input through 512 taps of cab

The arguments are ``name [offset] [size] [chan]``. The taps start at frame
``offset`` (default 0) and there are ``size`` of them, at most 4096. The
default size is 0, which is silence, so give a size as an argument, a number
or a message. Taps past the end of the buffer are left out. ``chan`` is the
buffer channel, from 1.

- Left inlet: the signal filtered, or a number used while no signal is
  connected.
- Middle inlet: the offset. Right inlet: the size. Both take a number, or a
  signal read at the first sample of each block.

The filter remembers the last 4096 input samples, so a longer kernel takes
in the past at once. ``clear`` forgets them, from the next block on: a sound
that stops then stops at once instead of ringing through the taps.
``chan <n>``, ``offset <n>`` and ``size <n>`` in the left inlet change the
channel and the taps while the patch runs. ``set <name> [offset] [size]``
switches to another buffer, looked up in the background and read from a
later block on; the numbers after the name set the offset and the size, and
one left out keeps its value. ``set`` alone reads silence. The taps are read
again at every block, so changes to the buffer, from a ``~buffer`` message, a
``~poke`` or the host, are heard from the next block. Each output sample
takes one multiplication per tap: keep long kernels for the places that need
them. Messages and numbers are not saved with the patch: a reloaded
``~buffir`` uses its arguments.

.. patcher-object:: ~sfinfo

Reports what a sound file holds, without loading it: only the file's header is
read. ``open <file>`` names a file and reports on it; a bang reports on the
last file named, by ``open`` or by the argument. The file is read again at
every bang, so a file that has changed on disk is reported as it is now. The
report goes out from the right outlet to the left, so the channel count
arrives last:

- outlet 5: the file's name, without its folders;
- outlet 4: the sample type: ``int8``, ``uint8``, ``int16``, ``int24``,
  ``int32``, ``float32``, ``float64``, ``mulaw`` or ``alaw``. A compressed
  encoding without a fixed sample format, such as Ogg Vorbis or ADPCM, is
  ``other``;
- outlet 3: the length in milliseconds;
- outlet 2: the sample rate in Hz;
- outlet 1: the size of one sample in bits, 0 for ``other``;
- outlet 0: the number of channels.

The file is opened in the background and the report arrives at the start of a
later audio block. A file that does not exist, or is not a sound file the
engine can read, sends nothing. Names are found the way the other file objects
find them (see :doc:`/patcher/files`), the host's path resolver included. The
argument is not read when the patch loads: nothing is reported until the first
bang.

.. code-block:: text

   [open drums/kick.wav(
    |
   [~sfinfo]          channels, sample size, rate, length, type and name
