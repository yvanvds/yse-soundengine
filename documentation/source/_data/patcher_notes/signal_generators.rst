.. Notes for the Signal generators category. Format: _data/patcher_notes/README.md.

.. patcher-object:: ~line

A float at the left inlet starts a ramp from the current value to that
target. The ramp takes the time last set at the right inlet, so set the time
first. A new time does not change a ramp that is already running, and
sending the target the ramp already has does nothing. With a time of 0 the
output jumps to the target.

The time is counted in whole milliseconds and then rounded to whole audio
blocks; a ramp always lasts at least one block. Within a block the ramp is
smooth, sample by sample.

.. code-block:: text

   [~line 0 20]   left <- 1.   =>   rises from 0 to 1 over 20 ms

``stop`` halts the ramp and holds the output at its current value until the
next target arrives. A new time on its own does not restart it. Any target
after a ``stop``, even the one the ramp was heading for, starts a new ramp
from the held value, taking the full ramp time.

.. patcher-object:: ~phasor

The output rises from 0 towards 1 and wraps back to 0, ``frequency`` times a
second. It is a phase, not an audio sawtooth: use it to read a wavetable,
step through a sequence, or shape it into other waveforms. A negative
frequency runs the ramp from 1 down to 0.

- A number at the left inlet sets the frequency while no signal is connected
  to it. With a signal connected, the signal sets it sample by sample.
- A number at the right inlet jumps to that phase at the start of the next
  block. Values outside 0-1 wrap, so ``1.25`` and ``-0.75`` both mean
  ``0.25``.
- ``phaseoffset <x>`` sets the phase the ramp starts at when the patch
  starts, and ``reset`` jumps back to it. A jump from the right inlet does not
  change ``phaseoffset``.

.. code-block:: text

   [~phasor 2 0.25]   ramps twice a second, starting a quarter of the way up

.. patcher-object:: ~cycle

A cosine at ``frequency`` Hz, read from a 16384-point table with linear
interpolation. At phase 0 the output is 1.

- A number at the left inlet sets the frequency while no signal is connected
  to it; a connected signal sets it sample by sample.
- The right inlet is a phase in cycles, added to the running phase. A number
  is a fixed offset (``0.25`` turns the cosine into a sine); a signal is phase
  modulation, which sounds like FM.
- ``reset <phase>`` restarts the running phase there at the next block
  (``reset`` alone restarts at 0), so several oscillators can be brought in
  step. ``frequency <hz>`` and ``phase <x>`` set the two numbers by name.

With a buffer name as the second argument the waveform is the first channel
of that sample buffer instead: ``offset`` samples in, ``size`` samples long
(0 means 512, -1 the rest of the buffer, and it stops at the buffer's end).
``buffer_offset <n>`` and ``buffer_sizeinsamps <n>`` change those two while
the patch runs. The buffer is read fresh every block, so a new file or an
edit is heard at once; an empty buffer gives silence.

To switch buffers while the patch runs, send ``set <name> [offset]`` (from
``offset``, 0 if not given; the size stays), ``setall <name>`` (the whole
buffer, from 0) or ``buffer <name>`` (offset and size stay). ``set`` alone
goes back to the cosine. The new buffer is looked up in the background and
heard from a later block on, without a gap; the phase runs on. A switch is
not saved with the patch: saving keeps the buffer argument, so a reloaded
patch starts from that again.

.. code-block:: text

   [~cycle 220]               a 220 Hz cosine
   [~cycle 110 wave 0 -1]     plays the whole of buffer "wave" as one cycle

.. patcher-object:: ~pink

Noise with the same energy in every octave: the level falls by 3 dB per
octave, where ``~noise`` (white noise) has the same energy per hertz and so
sounds brighter. Pink noise sounds like wind or rain, and it is the usual
signal for checking a room or a speaker by ear. It has no inlets; the output
is about -14 dBFS RMS and stays within -1 to 1.

.. code-block:: text

   [~pink] -> [~lp 400]   a soft wind texture

.. patcher-object:: ~rand

A random signal that moves smoothly: at the given frequency it picks a new
random value between -1 and 1 and glides to it in a straight line. Slow
rates make gentle drift for vibrato, a filter cutoff or a pan position;
audio rates give a rough noise whose energy lies below the frequency. The
frequency is a number or a signal; its sign is ignored and 0 holds the
current value. The output starts at 0.

.. code-block:: text

   [~rand 3] -> [~* 0.01]   a slow random drift, scaled down for vibrato
