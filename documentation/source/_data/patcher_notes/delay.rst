.. Notes for the Delay category. Format: _data/patcher_notes/README.md.

.. patcher-object:: ~tapin

Records its input into a delay line. The outlet is not an audio output: it
connects only to the left inlet of ``~tapout``, which reads the line. One
``~tapin`` can feed any number of ``~tapout`` objects.

- The argument is the longest delay, in milliseconds (default 100). The
  memory for it is allocated when the object is created.
- A number at the inlet sets a new longest delay, up to the argument, and
  clears the line. ``clear`` clears it without changing the length.
- ``freeze 1`` stops recording: the line keeps replaying its last longest
  delay of audio. ``freeze 0`` records again.

A cord from a ``~tapout`` back into its ``~tapin`` is feedback, and is
allowed. A loop of ordinary signal cords is refused, because no object in it
could ever run first; going through a delay line is how a patch builds an
echo, a comb or a reverb.

.. code-block:: text

   [~tapin 2000]   a line up to two seconds long

.. patcher-object:: ~tapout

Reads the delay line of the ``~tapin`` connected to its left inlet. Each
argument is one tap with its own delay inlet and output, so
``[~tapout 100 250 400]`` has three.

- A number at a tap's inlet sets that tap's delay in milliseconds. The delay
  is rounded to whole samples, and a change jumps.
- A signal at a tap's inlet sets the delay sample by sample, read with
  interpolation, so a moving delay bends the pitch smoothly (chorus,
  flanger). A connected signal wins over the number.
- A list at the left inlet sets the delays of several taps, from the first
  one on.
- The shortest delay is one block of samples, the longest is the
  ``~tapin``'s. A delay outside that range is held at the nearest end.
- With no ``~tapin`` connected, the outputs are silent.

.. code-block:: text

   [~tapin 1000]
    |
   [~tapout 300]   an echo 300 ms later; send it through [~* 0.5] back
                   into ~tapin for repeats that fade by half each time

.. patcher-object:: ~comb

A comb filter: the input, the input some time ago and the output the same
time ago, each with its own gain.

.. code-block:: text

   y(n) = a x(n) + b x(n-D) + c y(n-D)

With only ``b`` the filter is a plain delay; ``a`` and ``b`` together cut
notches into the spectrum, evenly spaced, the classic flanger sound when the
delay moves. ``c`` feeds the output back, so the filter rings at the
frequency ``1000 / D`` (D in ms) and its multiples, each echo ``c`` times the
one before: a resonator, or a plucked string when a short burst of noise goes
in.

The creation arguments are the maximum delay in ms, the delay in ms and the
gains ``a``, ``b`` and ``c`` (``~comb 50 5 1 0 0.9``); they default to 10 and
zeros, which is silent. The maximum sizes the delay lines when the object is
made, for rates up to 192 kHz, so nothing is allocated while it runs; changing
it rebuilds the object. The delay and the gains are numbers or signals in the
four right inlets, read per sample, or a list of up to four numbers in any
inlet. A connected signal wins over the number, which is used again once the
cord is gone.

The delay follows the sample rate and is interpolated between samples, so it
can be swept. It is held between one sample and the maximum. ``c`` is held
between -1 and 1; at exactly 1 or -1 the filter rings forever. ``clear`` in
any inlet silences the delay lines. An input sample that is not a finite
number reads as 0. Unconnected, the left inlet reads 0.

.. code-block:: text

   [~noise] -> [~comb 50 2.27 0.2 0 0.97] -> [~dac]
   a buzzing resonance near 440 Hz; sweep the delay with a slow ~cycle

.. patcher-object:: ~teeth

The comb filter of ``~comb`` with a separate delay for each path.

.. code-block:: text

   y(n) = a x(n) + b x(n-F) + c y(n-B)

The second inlet is the feedforward delay ``F``, the third the feedback delay
``B``, both in ms; then the gains ``a``, ``b`` and ``c``. With ``F`` and
``B`` equal it is ``~comb``. Apart from the second delay it works the same:
the creation arguments are the maximum delay, the two delays and the three
gains (``~teeth 50 3 7 1 0.5 0.8``); a list in any inlet sets up to five
numbers in that order; ``clear`` silences the lines.

.. code-block:: text

   [~click] -> [~teeth 50 1.3 4.1 1 0.5 0.8] -> [~dac]
   notches from the short path, ringing from the long one

.. patcher-object:: ~delay

Delays a signal by a number of samples. The delay can be anything from 0,
where the signal passes unchanged, up to the longest delay, even less than
one block, which ``~tapout`` cannot do. Not to be confused with ``.delay``,
which delays a bang.

- The creation arguments are the memory in samples, the initial delay in
  samples and the ramp time in ms (``~delay 44100 200 10``); they default to
  512, 0 and 0. The memory is allocated when the object is made, so nothing
  is allocated while it runs; changing it rebuilds the object.
- A number at the right inlet sets the delay in samples; a fraction is
  dropped. ``<n> samples`` means the same, and ``<n> ms`` sets it in
  milliseconds, rounded to whole samples at the current sample rate. A delay
  in samples lasts longer at a lower sample rate; one in milliseconds does
  not. ``delay <time>`` takes the same forms.
- With a ramp time (``ramp <ms>``), a new delay crossfades from the old one
  instead of jumping, so the change does not click. A change during a
  crossfade starts a new crossfade from the delay that was fading in.
- A signal at the right inlet sets the delay sample by sample, in samples,
  read with interpolation, so it can be swept smoothly. It skips the ramp,
  and the number is used again once the cord is gone.
- ``maxsize <n>`` sets the longest delay in samples, up to the memory, and
  clears the line. ``clear`` silences the line. A delay outside 0 and the
  longest delay is held at the nearest end.
- All of these words work in either inlet. An input sample that is not a
  finite number is written as 0. Unconnected, the left inlet reads 0.

The output cannot be fed back into the input: a loop of signal cords is
refused. For echoes that repeat, use ``~tapin`` and ``~tapout`` or
``~comb``.

.. code-block:: text

   [~adc] -> [~delay 4800 96] -> [~dac]
   the input, two milliseconds late at 48 kHz
