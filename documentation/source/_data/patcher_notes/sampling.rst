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
