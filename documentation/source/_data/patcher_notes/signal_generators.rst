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

.. patcher-object:: ~saw

A sawtooth that rises from -1 to 1 once per cycle and drops back: every
harmonic, the bright starting point of subtractive synthesis. The drop is
antialiased, so high notes do not fill with folded, out-of-tune tones.
Earlier versions of ``~saw`` were a plain ramp, which aliased audibly at high
pitches; patches that use it now sound cleaner and a little softer at the
top. Without an argument the frequency is 440 Hz.

- A number at the left inlet sets the frequency while no signal is connected
  to it; a connected signal sets it sample by sample (FM). A negative
  frequency runs the ramp backwards.
- The right inlet is a sync input. Whenever its signal rises across 0.5 the
  cycle restarts, so a ``~phasor`` at a lower frequency hard-syncs the
  oscillator. ``synctrig <x>`` at the left inlet moves the level from 0.5.
  The restart itself is not antialiased.

.. code-block:: text

   [~phasor 110] -> right inlet of [~saw 330]   a hard-synced saw; sweep 330
                                                 for the classic sync lead

.. patcher-object:: ~rect

A pulse wave: 1 for the first ``width`` of each cycle, -1 for the rest, with
both edges antialiased. A width of 0.5 is a square wave, hollow and woody;
narrow pulses sound thin and nasal. A signal at the middle inlet sweeps the
width (pulse-width modulation), the chorused sound of analog string pads.
The width wraps into 0-1. Any width but 0.5 carries a DC offset, as the
analog original does. Frequency, the sync inlet (on the right) and
``synctrig`` work as for ``~saw``.

.. code-block:: text

   [~rand 0.5] -> [~* 0.2] -> [~+ 0.5] -> middle inlet of [~rect 110]

.. patcher-object:: ~tri

A triangle wave, rising for the first ``duty`` of each cycle and falling for
the rest, with the corners antialiased. At 0.5 it is symmetric, soft and
flute-like; towards 0.05 or 0.95 it leans into a sawtooth and gets brighter.
The duty, at the middle inlet, wraps into 0-1 and is then held within
0.05-0.95. Frequency, the sync inlet (on the right) and ``synctrig`` work as
for ``~saw``.

.. code-block:: text

   [~tri 220 0.5]   a soft 220 Hz triangle

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

.. patcher-object:: ~click

An impulse: each bang outputs a single sample of 1, followed by silence. A
number plays it scaled by that number. The creation arguments, or
``set <values...>``, store a short waveform of up to 256 values to play
instead (``set`` alone goes back to the single sample). A click is the usual
excitation for a filter or a resonator: its response is the filter's own
sound. A bang or a number lands at the start of the next audio block; for a
trigger on an exact sample, connect a signal, which plays the waveform at
every sample where it moves from 0 to another value. A ``set`` is not saved
with the patch; the arguments are.

.. code-block:: text

   [.metro 250] -> [~click 1 0.5 0.25] -> [~lp 800]   a soft tick four times a second

.. patcher-object:: ~kink

Bends a phase, a ramp from 0 to 1 such as the output of ``~phasor``. The
ramp is multiplied by the slope until it reaches 0.5; from there a second,
straight line takes it on to 1, so the bent ramp still runs from 0 to 1 in
the same cycle, with a kink where the two lines meet. A slope of 1 changes
nothing; a larger slope reaches 0.5 sooner and then rises more slowly. At a
slope of 0.5 or less the ramp never reaches 0.5 and there is no second line:
the output is the input times the slope.

Read a cosine with the bent phase and the waveform's first half is squeezed
and its second half stretched: the more slope, the brighter the sound. This
is phase distortion synthesis. Sweep the slope with a signal for a filter-like
sweep without a filter.

Inputs outside 0-1 are not wrapped; they follow the same two lines.

.. code-block:: text

   [~phasor 110] -> [~kink 3] -> [~cycle 0] (right inlet)   a bright 110 Hz tone

.. patcher-object:: ~oscbank

A bank of sine oscillators, summed into one signal: additive synthesis, and
resynthesis from analysis data. The first argument is how many oscillators
the bank has room for (64 by default, up to 4096); ``size <n>`` runs fewer.

Each oscillator has a frequency and a magnitude. The fourth inlet names an
oscillator by number, from 0; the first two set its frequency and magnitude.
Numbers do this one at a time, in the order they arrive, so a loop that sends
an index, a frequency and a magnitude for each partial sets them all.
Signals do it sample by sample: at every sample, the oscillator the index
signal names gets that sample's frequency and magnitude. That is how an
analysis stream with one bin per sample drives the bank. An unconnected inlet
reads its last number. ``set f m f m ...`` sets the first oscillators from a
list of pairs and fades all others out.

A new frequency or magnitude is not jumped to but ramped to, in a straight
line over the number of samples the second and third arguments give
(``freqsmooth`` and ``magsmooth``; 0 or 1 jumps). This smooths the steps
between analysis frames.

With ``framesync 1`` values sent by signal are held back, and all of them
take effect together at the next sample whose index is 0: the start of the
next frame. Only then does the phase inlet do anything: a phase sent to an
oscillator is jumped to when its frame takes effect.

``silence`` mutes every oscillator at once; ``clear`` also sets every
frequency to 0. ``~oscbank`` reads its sine table without interpolation, so
its size matters: the fourth argument, or ``tabpoints <n>``, sets it to a
power of two (4096 by default). ``~ioscbank`` interpolates and needs no
choice. Messages are not saved with the patch; the arguments are.
``copybuf`` (a waveform from a buffer) is not available yet (issue #1196).

.. code-block:: text

   [~phasor 0.5] -> [~* 300] -> [~+ 200] -> [~oscbank 1 1 0 256] -> [~dac]
   [~sig 0.3] -> [~oscbank] (second inlet)   a sweep through a coarse 256-point sine

.. patcher-object:: ~ioscbank

The interpolating ``~oscbank``: the same inlets, messages and smoothing, but
the sine table is read between its points, so each oscillator is a clean
sine at a little more cost. There is no table size to choose, and no
``tabpoints``. The arguments are the number of oscillators and the
frequency and magnitude smoothing.

.. code-block:: text

   [~sig 330] -> [~ioscbank 1 1 64] (left inlet)
   [~sig 0.5] -> [~ioscbank] (second inlet)   one sine, fading in over 64 samples

.. patcher-object:: ~triangle

Reads a triangle with a phase, a ramp from 0 to 1 such as the output of
``~phasor``. The output rises in a straight line from ``lo`` to ``hi`` at the
peak position, then falls back to ``lo`` by the end of the cycle. A peak of
0.5 is a symmetric triangle; towards 0 it becomes a falling ramp, towards 1 a
rising sawtooth. Driven by a slow ``~phasor`` it is an LFO; driven once
through a cycle by a ramp, an attack-release envelope.

The phase is wrapped, so 1.25 reads as 0.25. The arguments are the peak
position and the range (-1 to 1 by default); ``lo <x>`` and ``hi <x>`` change
the range while it plays, but are not saved with the patch.

.. code-block:: text

   [~phasor 2] -> [~triangle 0.2 0 1] -> [~*] (right inlet)   a fast-rise, slow-fall tremolo

.. patcher-object:: ~trapezoid

Reads a trapezoid with a phase: a straight rise from ``lo`` to ``hi`` up to
the ramp-up point, ``hi`` held until the ramp-down point, then a straight fall
back to ``lo`` by the end of the cycle. The defaults 0.1 and 0.9 give a
trapezoid with short ramps; 0.5 and 0.5 a triangle; 0 and 0 a falling ramp. A
ramp-down point before the ramp-up point is moved to it.

The phase is wrapped to its fractional part by default (``wrapmode 1``), so
-0.4 reads as 0.6; with ``wrapmode 0`` it is clipped to 0-1 instead, so a
one-shot ramp that runs past 1 stays at ``lo``. The arguments are the
ramp-up and ramp-down points, the range (0 to 1 by default) and the wrap
mode. ``lo``, ``hi`` and ``wrapmode`` messages are not saved with the patch.

.. code-block:: text

   [~phasor 2] -> [~trapezoid 0.05 0.5] -> [~*] (right inlet)   a gate with soft edges, twice a second