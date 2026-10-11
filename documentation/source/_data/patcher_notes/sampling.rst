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
