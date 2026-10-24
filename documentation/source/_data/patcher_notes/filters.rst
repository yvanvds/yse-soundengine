.. Notes for the Filters category. Format: _data/patcher_notes/README.md.

.. patcher-object:: ~lp

The cutoff argument defaults to 0 Hz, where the filter lets nothing through:
give it a cutoff (``~lp 800``). The cutoff is an approximation that holds
well at low frequencies. From about a sixth of the sample rate up (about
7 kHz at 44.1 kHz), the filter is fully open and passes the input unchanged.

.. patcher-object:: ~hp

At 0 Hz, the default, the input passes unchanged. As for ``~lp``, the cutoff
is an approximation that holds well at low frequencies; from about a sixth of
the sample rate up, the output is the difference between successive samples.

.. patcher-object:: ~bp

The center frequency is in Hz; below 0.001 Hz, including the default 0, the
filter uses 10 Hz. A higher ``Q`` makes the band narrower; the default is 1.
A tone at the center frequency comes out at the input's level for every
``Q``. A ``Q`` low enough to open the band fully (below 0.001, or below about
``2 * pi * frequency / samplerate``) turns filtering off and passes the input
unchanged.

.. patcher-object:: ~vcf

Unlike ``~bp``, the center frequency comes in as a signal, in Hz, so it can
be swept smoothly or modulated at audio rate. Feed it from ``~line`` or an
oscillator scaled to a frequency range.

``sharpness`` sets how narrow the band is: higher is narrower. At 0, the
default, the left outlet passes the input unchanged and the right outlet is
silent.

Nothing comes out unless both the audio input and the center frequency are
connected.

.. patcher-object:: ~sah

Samples the left inlet each time the trigger signal in the right inlet
meets the trigger condition, and outputs that value until the next
trigger. Clocked by a ``~phasor`` it latches one value per cycle: a stepped
random melody from noise, or one LFO value per beat. A number in the right
inlet, ``thresh <x>`` or the first creation argument sets the threshold
(default 0).

``triggermode`` sets the condition:

- 0, the default: the trigger rises from at or below the threshold to above
  it. On a rising ``~phasor`` with threshold 0 that is the first sample of
  each cycle after the reset.
- 1: it falls from at or above the threshold to below it.
- 2: either of the two.
- 3: it is exactly the threshold. Every such sample is taken, so the output
  follows the input while the trigger stays there.
- 4: it leaves 0; 5: it returns to 0. The threshold is ignored.
- 6: the trigger is a 0-1 phase and the threshold a phase on it, passed in
  either direction. A jump of more than half a cycle counts as a wrap, so
  with threshold 0 every reset of a ``~phasor`` triggers, for a positive or
  a negative frequency, as ``~what`` reads a ramp.

With ``duration <samples>`` (or ``dur``) above 0 the held value lasts that
many samples from the trigger and the output is 0 after that, until the
next trigger. A trigger sample that is not a finite number is skipped.
Unconnected, the left inlet reads the last number sent to it and the right
inlet reads 0. The creation arguments may hold ``thresh``, ``triggermode``
and ``duration`` keywords (``~sah 0.5 triggermode 1``); they are saved with
the patch, later messages are not.

.. code-block:: text

   [~noise] -> [~sah]   right inlet: [~phasor 4]   four new values a second

.. patcher-object:: ~sash

A sample and hold with a memory. Recording and playback have separate
triggers: each time the middle inlet rises above ``samplelevel``, the left
inlet is written into the next place of the memory, and the right inlet
picks which stored value comes out. With ``size 8`` it remembers the last
eight sampled values, so a random melody can loop instead of changing on
every step.

``mode`` sets how the right inlet picks the value:

- 0, the default: each time it rises above ``advancelevel`` the output moves
  to another place, as ``dir`` says: 0 the next one (the default), 1 the one
  before, 2 any place at random, 3 a random place not played since every
  place last came round.
- 1: its whole value is the place to play, wrapped into the size, so
  -1 is the last.
- 2: it is a 0-1 phase divided into ``size`` equal parts and the part it is
  in is played. A ``~phasor`` then plays the whole memory once per cycle.

A list of numbers in the left inlet fills the memory and sets the size to
its length. That is how to play a fixed sequence with nothing connected to
the middle inlet. Recording comes before playback in each sample, so with
the default size of 1 and one trigger on both inlets, ``~sash`` behaves as a
plain sample and hold.

The two levels are 0.5 by default. A ``~phasor`` therefore triggers halfway
through each cycle, and a 0/1 pulse triggers on each rise. A number in the
middle or right inlet sets ``samplelevel`` or ``advancelevel``. The memory
holds up to ``maxsize`` values (default 1000), and only a creation argument
can set that. Unconnected, the left inlet reads the last number sent to it
and the triggers read 0. The creation arguments are keyword pairs
(``~sash size 8 mode 2``). They are saved with the patch; later messages
and the memory are not.

.. code-block:: text

   [~noise] -> [~sash size 8]   middle: [~phasor 0.5]   right: [~phasor 4]
   eight remembered values loop at four steps a second, one replaced every two seconds
