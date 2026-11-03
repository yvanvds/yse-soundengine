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
