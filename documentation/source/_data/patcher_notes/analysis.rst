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
