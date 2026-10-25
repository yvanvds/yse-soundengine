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

.. patcher-object:: ~stash

Stores signal values in a memory of places and plays them back. It has four
inlets: the signal to record, a sample trigger, an advance trigger and a
write index. Depending on its settings it works as a sample and hold that
remembers, an interpolating lookup table or an audio-rate step sequencer.

**Recording.** When the second inlet's trigger fires, the left inlet is
written into the memory. ``writemode`` says where:

- 0, the default: the next place, wrapping at the size.
- 1: the place the right inlet names.
- 2: the right inlet as a 0-1 share of the size.

**Playback.** ``mode`` says what the third inlet does:

- 0, the default: it is a trigger, and each time it fires the output moves to
  another place, as ``dir`` says. 0 is the next place (the default), 1 the
  one before, 2 any place at random, 3 a random place not played since every
  place last came round.
- 1: its value is the place to read.
- 2: it is a 0-1 share of the size. A ``~phasor`` then plays the whole memory
  once per cycle.

Both triggers fire as ``sampletriggermode`` and ``advancetriggermode`` say,
with the numbering ``~sah`` uses. The default, 0, fires on a rise above the
level (``samplelevel`` and ``advancelevel``, 0.5 by default). 6 fires when
the trigger passes the level as a phase, so ``advancetriggermode 6
advancelevel 0`` steps once per cycle of a ``~phasor``.

``interp`` decides how an index between two places is read: 0 rounds it
down, 1 mixes the two places, 2 rounds to the nearest. ``extend`` decides
how an index outside the memory is read: 0 reads 0, 1 the first or last
place, 2 (the default) wraps it into the size, 3 leaves the output as it
was. ``duration <n>`` makes each new output last n samples and then drop to
0, so ``duration 1`` turns the steps into impulses.

A list of numbers in the left inlet fills the memory and sets the size to
its length. A single number is written at the write position. A number in
the right inlet sets that position, and a bang there moves it one place on.
A bang in the second inlet records the first sample of the next block. A
bang in the third inlet advances the output. A number in the second or third
inlet sets ``samplelevel`` or ``advancelevel``.

The memory holds up to ``maxsize`` values (default 1000). Only a creation
argument can set that. Unconnected, the left and right inlets read the last
number sent to them and the triggers read 0. The creation arguments are the
two levels, then keyword pairs (``~stash 0.5 0.5 size 8 mode 2``). They are
saved with the patch; later messages and the memory are not.

.. code-block:: text

   the list 0 3 7 10 -> [~stash advancetriggermode 6 advancelevel 0]   third: [~phasor 8]
   a four-step sequence at eight steps a second, as a signal

.. patcher-object:: ~onepole

The cheapest lowpass: each output sample moves a fraction ``a0`` of the way
from the previous output toward the input, rolling off 6 dB per octave.
Use it to take the edge off a bright signal or to smooth a control signal.
The cutoff in the right inlet (a number, or a signal read once per block)
or the first creation argument defaults to 0, where nothing gets through:
give it a cutoff (``~onepole 1000``).

The frequency is read in one of three modes, set by a word in either inlet
or among the arguments (``~onepole 0.5 linear``):

- ``hz`` (or ``Hz``), the default: a cutoff in Hz from 0 to a quarter of the
  sample rate, where the filter is fully open;
  ``a0 = sin(2 * pi * frequency / samplerate)``.
- ``linear``: 0-1 across that same range, so 0.5 is an eighth of the sample
  rate.
- ``radians``: 0-1 sets ``a0`` itself. The cutoff rises slowly at first and
  quickly near 1, a curve closer to how pitch is heard.

Values outside the range are clamped. Unlike ``~lp``, whose cutoff is a
low-frequency approximation that runs out near a sixth of the sample rate,
the cutoff here holds up to a quarter of it, and it follows the sample rate
when that changes. ``clear`` sets the filter state to 0. An input sample that
is not a finite number is skipped. Unconnected, the left inlet reads 0.

.. code-block:: text

   [~noise] -> [~onepole 500] -> [~dac]   a soft rumble

.. patcher-object:: ~lores

A resonant lowpass with two poles, rolling off 12 dB per octave above the
cutoff: the classic synth filter. Give it a cutoff in Hz and a resonance from
0 to 1, as creation arguments (``~lores 800 0.7``), as numbers or signals in
the middle and right inlets, or as ``cutoff <x>`` and ``resonance <x>`` in
any inlet. Both are read per sample when a signal drives them, so an envelope
or an LFO can sweep the cutoff smoothly.

Resonance 0 is a flat roll-off with no peak. Toward 1 a peak grows at the
cutoff (about 17 dB at 0.9) and the filter rings, until near 1 a click makes
it sing at the cutoff for a long time. The resonance stops at 0.999, and the
filter stays stable all the way there. Below the cutoff the gain is 1, so
the loudness of the bass does not change as the resonance does; the peak
adds to it.

The cutoff runs from 0, where nothing gets through, to half the sample
rate, and follows the sample rate when that changes. ``clear`` sets the
filter state to 0, silencing a ringing filter at once. An input sample that
is not a finite number is skipped. Unconnected, the left inlet reads 0.

.. code-block:: text

   [~saw 110] -> [~lores 600 0.85] -> [~dac]
   a buzzy bass with a resonant edge; sweep the middle inlet for a wah

.. patcher-object:: ~reson

A resonant bandpass: it lets through a band around a center frequency and
rings there, more narrowly and for longer as the Q rises. Give it a gain, a
center frequency in Hz and a Q (the center frequency over the bandwidth), as
creation arguments (``~reson 1 800 20``), as numbers or signals in the three
right inlets, as ``gain <x>``, ``cf <x>`` and ``q <x>``, or as a list of up
to three numbers in any inlet. All three are read per sample when a signal
drives them, so a bank of them can glide between vowel formants.

The gain is the level at the center frequency, whatever the frequency and
the Q, so sweeping either keeps the resonance at the same loudness. Below and
above the band two zeros pull the response down towards 0 Hz and half the
sample rate, the more so the higher the Q; it differs from ``~bp`` in that,
and in how it sets its gain. The filter follows the
constant-gain resonator of Smith and Angell, with the equation the original
object documents.

The center frequency runs from 0, where nothing gets through, to half the
sample rate, and follows the sample rate when that changes. The Q runs from
0.01 to 10000 and the filter stays stable over the whole range. The defaults
are a gain of 0, a center frequency of 0 and a Q of 0.01, so set at least the
first two. ``clear`` sets the filter state to 0, silencing a ringing filter
at once. An input sample that is not a finite number is skipped. Unconnected,
the left inlet reads 0.

.. code-block:: text

   [~noise] -> [~reson 1 700 30] -> [~dac]
   a whistling tone at 700 Hz; raise the Q for a purer pitch

.. patcher-object:: ~svf

A state-variable filter: one center frequency and one resonance drive four
outputs at once, a lowpass, a highpass, a bandpass and a notch, from left to
right. Set the frequency and the resonance as creation arguments
(``~svf 1000 0.8``), as numbers or signals in the two right inlets, as
``freq <x>`` and ``q <x>``, or as a list of two numbers in any inlet. Signals
are read per sample, so the filter can be swept at audio rate.

The frequency is read in one of three modes, chosen by ``hz`` (the default),
``linear`` or ``radians`` in any inlet or among the arguments. In hz mode it
runs from 0 to a quarter of the sample rate. In linear mode 0 to 1 covers that
same range evenly. In radians mode 0 to 1 is the sine of the frequency in
radians per sample, as for ``~onepole``. A frequency of 0 silences the
lowpass and the bandpass and passes the input through the highpass and the
notch.

The resonance runs from 0 to 1. At 0 there is no peak (a Q of 0.5); at 0.5 the Q is
1; towards 1 the peak sharpens to a Q of 1000, which still dies away. At the center frequency the lowpass,
highpass and bandpass all have a gain of the Q, and the notch lets nothing
through. The defaults are a frequency of 0 and a resonance of 0.01.

The responses are those of Hal Chamberlin's state-variable filter, computed
in the zero-delay-feedback form of Zavalishin and Simper. That form stays
stable at every frequency and resonance and puts the center frequency
exactly where it is set, so near the top of the range it can sound slightly
different from Chamberlin's original arrangement. The mapping from the
resonance to the Q is this object's own. The coefficients follow the sample
rate when that changes. ``clear`` sets the filter state to 0. An input
sample that is not a finite number is skipped. Unconnected, the left inlet
reads 0.

.. code-block:: text

   [~saw 110] -> [~svf 800 0.9] -> (third outlet) -> [~dac]
   a resonant band; sweep the middle inlet, or take the notch from the fourth

.. patcher-object:: ~biquad

A two-pole, two-zero filter driven directly by its five coefficients:

.. code-block:: text

   y(n) = a0 x(n) + a1 x(n-1) + a2 x(n-2) - b1 y(n-1) - b2 y(n-2)

The feedback coefficients b1 and b2 are subtracted, so the signs differ from
the textbooks that add them. A lowpass from the usual design with
``k = tan(pi f / samplerate)`` and ``n = 1 / (1 + k/Q + k^2)`` is
``a0 = n k^2``, ``a1 = 2 a0``, ``a2 = a0``, ``b1 = 2 n (k^2 - 1)`` and
``b2 = n (1 - k/Q + k^2)``. Set the coefficients as creation arguments
(``~biquad 0.5 0 0 -0.5 0``), as numbers or signals in the five right inlets,
or as a list of up to five numbers in any inlet. A connected signal is read
per sample and wins over the number, which is used again once the cord is
gone. All five default to 0, which passes nothing.

A change of the numbers moves each coefficient in a straight line to its new
value over 64 samples, so a new list does not click; ``smooth 0`` in any
inlet or among the arguments makes changes apply at once, ``smooth 1`` turns
the smoothing back on. The coefficients first set apply from the first
sample. Signals are never smoothed.

The coefficients are not checked for stability. The filter is stable while
``|b2| < 1`` and ``|b1| < 1 + b2``; outside that it grows. Lower the
coefficients again and send ``clear``, which sets the previous inputs and
outputs to 0. An output too large for a float starts the filter again from
silence. ``stoke <y1> <y2> <x1> <x2>`` preloads the previous outputs and
inputs (missing values are 0), so a filter with its poles on the unit circle
rings as a sine from the first sample. Nothing depends on the sample rate. An
input sample that is not a finite number is skipped. Unconnected, the left
inlet reads 0.

.. code-block:: text

   [~noise] -> [~biquad 0.0572 0.1144 0.0572 -1.219 0.4477] -> [~dac]
   a Butterworth lowpass at 4 kHz (44.1 kHz rate); send a new list to move it

.. patcher-object:: ~allpass

A Schroeder allpass: a delay line with a feedforward and a feedback path of
opposite gain.

.. code-block:: text

   y(n) = -g x(n) + x(n-D) + g y(n-D)

Every frequency passes at the same level and only the phase changes, so a
click comes out as a train of echoes ``D`` apart, each ``g`` times the one
before. Several in series, with delays that share no common factor, are the
diffuser at the front of a classic reverb. A gain of 0 is a plain delay.

The creation arguments are the maximum delay in ms, the delay in ms and the
gain (``~allpass 50 12.5 0.7``); they default to 10, 0 and 0. The maximum
sizes the delay lines when the object is made, for rates up to 192 kHz, so
nothing is allocated while it runs; changing it rebuilds the object. The delay
and the gain are numbers or signals in the two right inlets, read per sample,
or a list of up to two numbers in any inlet. A connected signal wins over the
number, which is used again once the cord is gone.

The delay follows the sample rate and is interpolated between samples, so it
can be swept; at a fractional delay the very top of the spectrum is slightly
dulled. It is held between one sample and the maximum. The gain is held
between -1 and 1. At exactly 1 or -1 the filter passes the signal times
``-gain``, with nothing of its earlier state left ringing. ``clear`` in any
inlet silences the delay lines. An input sample that is not a finite number
reads as 0. Unconnected, the left inlet reads 0.

.. code-block:: text

   [~click] -> [~allpass 50 4.7 0.7] -> [~allpass 50 3.6 0.7] -> [~dac]
   a short burst of diffuse echoes; add stages with other delays to thicken it

.. patcher-object:: ~cascade

Several biquad filters in series, the output of each the input of the next.
Each section is the filter of ``~biquad``, with the same coefficients and the
same signs:

.. code-block:: text

   y(n) = a0 x(n) + a1 x(n-1) + a2 x(n-2) - b1 y(n-1) - b2 y(n-2)

A filter of higher order is split into such second-order sections, so a list
of coefficients in sets of five is a whole filter design: the first five
numbers are the first section, the next five the second, and so on. Send the
list to either inlet. The number of complete sets is the number of sections
that run; an incomplete last set is ignored, and a list without one complete
set changes nothing. With no list yet the input passes unchanged.

The first creation argument is the number of sections the object has room
for, 16 when it is left out and at most 64. The room is made when the object
is created, so nothing is allocated while it runs; changing it rebuilds the
object. A list with more sets than that uses only as many as fit. Starting
coefficients and ``smooth 0`` may follow (``~cascade 2`` and then two sets of
five).

A section already running moves to its new coefficients in a straight line
over 64 samples, so a new design does not click; ``smooth 0`` makes changes
apply at once, ``smooth 1`` turns the smoothing back on. A section that starts
running starts from silence at its coefficients. ``clear`` sets every
coefficient and the previous inputs and outputs to 0, which silences the
output until the next list, which then applies at once. ``bypass 1`` passes the input unfiltered and
``zero 1`` silences the output, each turned off again with 0; bypass wins over
zero. The sections keep running under both. As with ``~biquad`` the
coefficients are not checked for stability, an output too large for a float
starts a section again from silence, and an input sample that is not a finite
number is skipped. Nothing depends on the sample rate. Unconnected, the left
inlet reads 0.

.. code-block:: text

   [~noise] -> [~cascade 2] -> [~dac]
   send it 0.004479 0.008957 0.004479 -1.750143 0.768058
           0.004805 0.00961 0.004805 -1.877705 0.896926
   a fourth-order Butterworth lowpass at 1 kHz (44.1 kHz rate)
