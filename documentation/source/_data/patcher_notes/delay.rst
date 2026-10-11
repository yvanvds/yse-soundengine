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
