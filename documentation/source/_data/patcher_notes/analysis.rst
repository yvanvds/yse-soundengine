.. Notes for the Analysis category. Format: _data/patcher_notes/README.md.

.. patcher-object:: ~snapshot

Turns a signal into numbers, for the parts of a patch that work with
messages.

- A bang reports the value in the last rendered block, at once.
- With an ``interval`` (in milliseconds), it also reports on its own: on the
  first block after reporting starts, then once per interval. Reports happen
  at the end of a block, after every signal object has rendered it. An
  interval shorter than a block reports once per block.
- A non-zero number at the left inlet, or ``start``, turns interval reporting
  on; ``0`` or ``stop`` turns it off. It is on when the object is created.
- A number at the right inlet, or ``interval <ms>``, sets the interval. ``0``
  stops interval reporting. ``sampleinterval <n>`` sets it in samples
  instead, until the next ``interval``.
- ``offset <n>`` picks which sample of the block is reported, from 0. A
  number past the end of the block reports the last sample.

.. code-block:: text

   [~snapshot 100]   reports its input ten times a second

.. patcher-object:: ~where

Measures a 0-1 ramp, usually a ``~phasor``, in time rather than phase. The
left outlet gives the time since the ramp last started its cycle and the
right outlet the time until it ends it, both in milliseconds. To make
something happen 30 ms before each reset, wait for the right outlet to drop
to 30 or below.

The elapsed time is counted from the moment the ramp wrapped, between two
samples if need be, and keeps counting if the ramp slows down or stops. The
remaining time is a prediction from how fast the ramp moved on its last
step, so it follows a change of speed at once; while the ramp stands still
it keeps its last value. At a constant speed the two add up to the length of
a cycle.

A falling ramp, from a ``~phasor`` with a negative frequency, works the same
way: its cycle starts at 1 and ends at 0. When the ramp turns around without
wrapping, or first starts to move, the elapsed time is estimated from its
speed. Both outlets give 0 until the input first moves.

.. code-block:: text

   [~phasor 2] -> [~where]   right outlet: 500 at each reset, falling to 0

.. patcher-object:: ~what

Sends an impulse, a 1 for one sample, each time a 0-1 ramp such as a
``~phasor`` passes one of a list of phase values: rhythmic triggers on a
phasor clock. ``0 0.25 0.5 0.75`` gives four even beats per cycle,
``0 0.375 0.75`` a 3 + 3 + 2 rhythm. A list, a number or
``matches <values...>`` replaces the values; ``clear`` empties them. The
list holds up to 64 values, and each is a phase, so 1 is the same as 0:
the reset.

``triggermode`` says what counts as passing a value: 0 rising past it, 1
falling past it, 2 either (the default, for a ``~phasor`` with a positive
or a negative frequency) and 3 being exactly equal to it, which a ramp
seldom is. When rising or falling, a sample that lands exactly on a value
fires on the next sample, the one that moves on from it. The reset of a
ramp counts as moving on through 1 to 0, not as a fall through every value.

With ``syncupdate 1`` a new list waits for the ramp to wrap, so a rhythm
changes on the beat. The creation arguments are the first values, with
``triggermode <n>`` and ``syncupdate <n>`` among them if needed; they are
saved with the patch, later messages are not.

.. code-block:: text

   [~phasor 2] -> [~what 0 0.25 0.5 0.75]   eight impulses a second

.. patcher-object:: ~stepcounter

Counts the jumps in a signal, such as the resets of a ``~phasor``, through
a sequence of counts, and sends an impulse each time a count is reached. On
a phasor clock the sequence ``2 1 3`` gives impulses 2 cycles apart, then
1, then 3, and over again: a rhythm of durations in beats. The middle
outlet gives the jumps counted in the step playing, the right outlet the
step's place in the sequence. A list, a number or ``seq <counts...>``
replaces the sequence, up to 64 counts, each rounded to a whole number of at
least 1. With no sequence every jump fires.

A jump is a change of at least ``thresh`` (0.75) from one sample to the
next. ``direction`` picks which: 0 both (the default), 1 only rises (the
reset of a ``~phasor`` with a negative frequency) and 2 only falls (the
reset of one with a positive frequency). An impulse rises and falls, so it
is two jumps in direction 0: count impulses with ``direction 1``.

``reset`` starts the sequence over. With ``startmode 1`` (the default) the
first jump after a reset fires and starts the sequence; with ``startmode 0``
the sequence starts at the reset and the first jump is the first one
counted. ``syncupdate`` says when a new sequence takes effect: 0 at once, 1
when the step playing ends, 2 when the whole sequence ends. The creation
arguments are the first sequence, with ``direction``, ``startmode``,
``syncupdate`` and ``thresh`` among them if needed; they are saved with the
patch, later messages are not.

.. code-block:: text

   [~phasor 4] -> [~stepcounter 2 1 3]   impulses 0.5, 0.25 and 0.75 s apart

.. patcher-object:: ~change

Compares each input sample with the one before it: the output is 1 when the
sample is higher, -1 when it is lower and 0 when it is the same. Only the
direction counts, not the size of the step, so the smallest rise gives 1.
The sample before the first is 0; after that the last sample of each block
is carried into the next.

A rising ramp gives a steady 1 and a falling one a steady -1. On a sine the
output flips at every peak and trough. On a ``~phasor`` it is 1 except at
each wrap, where one sample is -1. A number in the inlet, used while no
signal is connected, shows as one sample of 1 or -1 when it goes up or down
and as 0 while it stays the same. An input sample that is not a number gives
0, both for itself and for the sample after it.

.. code-block:: text

   [~cycle 2] -> [~change] -> [~== 1]   1 while the sine rises, 0 while it falls

.. patcher-object:: ~edge

Turns a signal that switches on and off into events. The left outlet bangs
when the input goes from zero to non-zero, the right outlet when it goes from
non-zero back to zero. Only zero counts as off: any other value, however
small, is on, and so is a sample that is not a number. The sample before the
first is zero, so a signal that starts on gives a left bang after the first
block.

The bangs go out after the audio block in which the transition happened, the
right outlet first. A block with several transitions of one kind gives one
bang on that outlet. A number in the inlet, used while no signal is
connected, is a transition at the start of the next block when it turns zero
or non-zero.

.. code-block:: text

   [~cycle 1] -> [~> 0] -> [~edge]   left bang as the sine turns positive, right bang as it turns back

.. patcher-object:: ~thresh

A gate with hysteresis, also called a Schmitt trigger. The output turns 1 on
the first sample at or above the high threshold and stays 1 until a sample at
or below the low threshold turns it back to 0. Between the two it holds, so
a noisy signal hovering around one level does not make the gate chatter. The
output starts at 0.

The creation arguments are the low threshold, then the high one, both 0 by
default. The middle and right inlets take a number or a signal for each, and
a list of two numbers in any inlet sets both. With equal thresholds there is
no hysteresis: the output is 1 while the input is at or above the threshold.
A low threshold above the high one is used as the high one. An input sample
that is not a number leaves the output as it was.

.. code-block:: text

   [~cycle 1] -> [~thresh -0.5 0.5]   1 from the moment the sine reaches 0.5 until it falls to -0.5

.. patcher-object:: ~spike

Measures the time between onsets. Each time the input goes from zero to
non-zero, the outlet sends the time in milliseconds since the previous
reported transition, measured to the sample. The first one reports the time
since the object started. Zero means exactly 0, so any other value, however
small, is non-zero.

The refractory period, the creation argument or a number in the right inlet,
is a time after each reported transition during which further transitions
are ignored, so a bouncing trigger counts once and every interval is at least
that long. A bang in the left inlet starts the refractory period again from
the next block. With several transitions in one block the outlet sends the
interval of the last one.

.. code-block:: text

   [~phasor 2] -> [~> 0.5] -> [~spike]   500, twice a second

.. patcher-object:: ~zerox

Counts zero crossings. The left outlet is the number of times the signal
changed sign in the current block, the same value on every sample of the
block: a noisy signal crosses often, a low tone rarely, so the count is a
rough measure of noisiness or pitch. The right outlet is a click on every
crossing, a single sample at the click volume with 0 between.

A crossing is a sample above 0 after the last one below it, or the other way
round. Samples that are exactly 0 are on neither side, so a signal that only
touches 0 does not cross, and the first non-zero sample after the object
starts is never a crossing. The click volume is the creation argument, 1 by
default, and ``set`` followed by a number changes it.

.. code-block:: text

   [~cycle 1000] -> [~zerox]   about 5.3 per block of 128 samples at 48 kHz, two clicks per cycle

.. patcher-object:: ~peakamp

Reports the peak amplitude of a signal: the largest absolute sample value
since the last report. A bang reports the peak so far and starts measuring
again. With a reporting interval, the creation argument or a number sent to
the inlet, it also reports every interval, each time the peak of just that
interval, so a level meter needs no ``.metro``. An interval of 0 reports only
on a bang.

The second argument, or ``signed 1``, turns on signed mode: the report is
the sample with the largest magnitude, with its sign, so a window holding
0.5 and -0.8 reports -0.8 instead of 0.8. A window with no samples in it
reports 0. A bang is answered at the start of the next block with the peak
up to there, and several reports in one block send only the last.

.. code-block:: text

   [~cycle 440] -> [~* 0.5] -> [~peakamp 100]   0.5, ten times a second

.. patcher-object:: ~average

Averages a signal over its last N samples and outputs the result as a
signal, one value per sample. It follows the level of a signal: an envelope
follower, a level meter or a smoothed control. There are three modes.
``bipolar``, the default, is the plain mean, so a tone centred on 0 averages
near 0 and only the DC offset remains. ``absolute`` is the mean of the
absolute values. ``rms`` is the root mean square, the usual measure of
loudness.

The first argument is the longest interval in samples, 100 by default,
and the object starts with that interval. The second argument is the mode
word, such as ``~average 1000 rms``. A number sent to the inlet changes the
interval, clamped to 1 and the maximum. The words ``bipolar``, ``absolute``
and ``rms``, or ``mode`` followed by one of them, change the mode. Until a
full interval of samples has arrived, the missing ones count as 0, so the
output ramps up from silence. A sample that is not a number or is infinite
also counts as 0.

.. code-block:: text

   [~cycle 480] -> [~average 100 rms]   about 0.707: one full cycle at 48 kHz

.. patcher-object:: ~avg

Reports the average absolute value of a signal since the last report: a
cheap way to poll the level of a signal from control logic. Every bang
outputs the mean of the absolute sample values that arrived since the
previous bang, then starts measuring again, so a ``.metro`` driving the bang
sets how often the level is read. A bang is answered at the start of the
next block with the average up to there. With no samples since the last
bang, the report is 0. A sample that is not a number or is infinite counts
as 0. Use ``~average`` for a running average as a signal.

.. code-block:: text

   [~cycle 440] -> [~avg]   about 0.637 (2/pi) on every bang

.. patcher-object:: ~minmax

Tracks the lowest and the highest sample value of a signal since the last
reset. The left outlets carry both as signals, updated on every sample; a
bang sends them as numbers from the right outlets, answered at the start of
the next block with the values up to there. A bang does not start over:
``reset`` does, at the start of the next block, after any bang sent with it.
The right inlet resets sample-accurately: every sample where it is not 0
starts over at that sample, so a held non-zero number there makes both
outputs follow the input. Until the first sample both are 0, and a sample
that is not a number is skipped. Use ``~peakamp`` for the largest magnitude
since the last report.

.. code-block:: text

   [~cycle 440] -> [~minmax]   about -1 and 1, the swing of the sine

.. patcher-object:: ~capture

Stores the samples of a signal so a patch can look at them: by default the
most recent 4096, or with an ``f`` argument the first ones, until ``clear``.
A bang or ``dump`` sends them out, oldest first: the right outlet first sends
how many follow, then the left outlet sends them as lists of at most 256
numbers, each short enough for any list object to hold. The dump is taken
from the store as it is at the end of the next block; a large store goes out
over several blocks while recording goes on. ``write`` followed by a file
name saves the store as text, one value per line. The arguments are an
optional ``f``, the number of samples to keep, then up to 10 positions in
each block to store instead of every sample. The stored samples are not
saved with the patch.

.. code-block:: text

   [~cycle 440] -> [~capture f 64]   a bang: 64, then the first 64 samples as one list

.. patcher-object:: ~loudness

Measures loudness after EBU R 128 and ITU-R BS.1770 in LUFS (loudness units
relative to full scale): the momentary loudness over the last 400 ms, the
short-term loudness over the last 3 s, and the integrated loudness of
everything since the start or the last ``reset``, leaving out the parts at
-70 LUFS or below and those 10 LU or more below the rest. The fourth outlet
gives the loudness range in LU after EBU Tech 3342: how far the short-term
loudness spreads, from its 10th to its 95th percentile, once 3 s have been
measured. The last two give the sample peak in dBFS and the true peak in
dBTP, which looks between the samples by oversampling them 4 times and so
also catches the crests that fall between them. The signals pass through the
K-weighting filters of the standard, which follow the sample rate. The first argument
sets the number of signal inlets; all of them are measured together, each
with a weight of 1. A bang reports all six values, answered at the start
of the next block with the values up to there; the second argument, or
``interval``, sets a time in milliseconds to report automatically. A number
picks the inlet whose peaks are reported, 0 the highest of all, and reports.
Silence reads -200. ``reset`` starts the integrated loudness, the loudness
range and the peaks over.

.. code-block:: text

   [~cycle 1000] -> [~* 0.1] -> [~loudness 1 100]   about -23 LUFS

.. patcher-object:: ~fzero

Estimates the pitch of a single voice or instrument and sends it in Hz from
the left outlet, with the peak amplitude from the middle outlet. Every
``period`` samples it looks at the last ``size`` samples and searches them
for the shortest lag at which the signal repeats itself (the YIN method). A
signal that does not repeat clearly enough, such as noise or a chord, gives
no pitch; ``tolerance`` sets how strict that is, lower being stricter. The
lowest pitch it can find is the sample rate divided by half the size, less
one: 47 Hz for the default 2048 samples at 48 kHz. A pitch outside
``freqmin`` to ``freqmax`` is not sent, and an analysis whose peak is below
``threshold`` sends nothing at all.

The estimate describes the whole analysis buffer, so it lags the input by
about half the size, and right after an attack or a change of note, while
the buffer holds both sounds, it can land an octave off before it settles.

The right outlet bangs on an onset: the peak rising to ``onsetamp`` from
below, or the pitch moving ``onsetpitch`` semitones or more from the note's
for two analyses in a row. A note's pitch settles for ``size`` samples after
it starts. Onsets closer together than ``onsetperiod`` samples are dropped.
With ``onsetlist 1`` the onset is a list of the pitch at the onset (0 when
none was found yet) and its amplitude instead of a bang.

The settings are also the arguments, in this order: ``size``, ``period``,
``freqmin``, ``freqmax``, ``threshold``, ``onsetamp``, ``onsetpitch``,
``onsetperiod``, ``onsetlist`` and ``tolerance`` (``~fzero 1024 256``).
Arguments are saved with the patch; messages are not. ``quiet`` is accepted
and does nothing.

.. code-block:: text

   [~cycle 220] -> [~fzero] -> [.f]   about 220

.. patcher-object:: ~meter

A peak meter. Every update interval, 50 ms unless the first argument says
otherwise, it measures the largest absolute sample value of that interval
and sends it out. The engine only measures; the host draws. It polls the
GUI value, three numbers: the level and the held peak as amplitudes (1 is
0 dBFS), and the clip indicator, 1 once a sample went above 1 in magnitude.

The second argument is the hold time in milliseconds: the held peak stays at
the highest level for that long before it drops back to the level. A number
in the inlet is shown as if a signal with that peak had arrived, and is sent
out as well. ``clear`` turns the clip indicator off and drops the held peak.
How many LEDs the level lights, and in which colours, is up to the host.

.. code-block:: text

   [~cycle 440] -> [~* 0.5] -> [~meter 100]   0.5, ten times a second

.. patcher-object:: ~levelmeter

An RMS level meter in dB. Every update interval, 20 ms unless the second
argument says otherwise, it measures the root mean square of that interval,
moves its reading toward it with attack and release ballistics, and sends
the reading out. The GUI value holds the reading, the held peak, both in dB,
and the clip indicator; silence reads -200.

The first argument, or ``mode``, picks the ballistics of a standard meter:

- 0: fast, no ballistics and no offset
- 1: VU, 330 ms attack and release, 0 at -20 dBFS
- 2: DIN, 10 ms attack, 1500 ms release, 0 at -14 dBFS
- 3: BBC, 10 ms attack, 3000 ms release, 0 at -14 dBFS
- 4: Nordic, 5 ms attack, 2000 ms release, 0 at -14 dBFS
- 5: EBU, no attack time, 1500 ms release, 0 at -14 dBFS

The offset is the reference level, the dBFS that reads 0, so a VU meter
reads 0 for a signal at -20 dBFS RMS and +20 at full scale. ``attack``, ``release`` and ``offset`` change one of the three.
The third argument is the hold time of the held peak. A bang sends the
reading; a number is shown as that reading in dB and sent out. ``clear``
turns the clip indicator off and drops the held peak.

.. code-block:: text

   [~cycle 1000] -> [~* 0.5] -> [~levelmeter 1]   about 11 on a VU meter

.. patcher-object:: ~gridmeter

Shows the levels of several signals as the brightness of a grid of cells,
one cell per signal inlet; the first argument is how many. Each level is a
peak level with attack and release ballistics, 300 ms each by default. The
GUI value holds a brightness per cell, then a mute state per cell. A
brightness is 0 at the bottom of the range, ``-range`` dB (60 by default),
and 1 at 0 dB; above 1 the signal is louder than 0 dB, where the host shows
the hot colour.

The mute states are for the host to draw and for the patch to use: after
``mute <cell> <0|1>``, ``muteall <0|1>``, ``solo <cell>`` or a list of 0s
and 1s, the outlet sends every cell's state at the end of the next block, 1
for muted. Cells count from 1, and ``solo 0`` unmutes all of them. The
arguments are the cell count, the range in dB, the attack, the release and
the update interval in milliseconds (``~gridmeter 8 48 300 300 20``).

.. code-block:: text

   [~cycle 1000] -> [~* 0.5] -> [~gridmeter 1]   a cell at about 0.9

.. patcher-object:: ~number

A number box for signals, for debugging a patch. It does two things at
once. It monitors the signal in its left inlet: every update interval, 100
ms unless the second argument says otherwise, it takes one sample of it and
sends it out the right outlet. And it generates a signal: the left outlet is
the last number sent to the left inlet as a constant, ramping to each new
number over the ramp time, the first argument or the right inlet, in
milliseconds (0 jumps). A list ``<value> <ramp>`` sets both. The output
ignores the monitored signal.

The engine keeps the state; the host draws the box. Its GUI value is three
numbers: the value shown, the display mode and the modes allowed. In monitor
mode (2, the default) the box shows the monitored sample, in output mode (1)
the last number. ``mode`` switches, ``allow`` limits the modes to output (1),
monitor (2) or both (3), and ``monitormode`` and ``sigoutmode`` turn one
half of that on or off. ``set`` shows a number without sending it out, and a
bang in output mode then sends it. ``min`` and ``max`` clamp the numbers; on
their own they remove the limit. How many decimals to show, and in which
colours, is up to the host.

.. code-block:: text

   [.f] -> [~number 50 100 1] -> [~number] -> [.f]   ramps in 50 ms, then shows it
