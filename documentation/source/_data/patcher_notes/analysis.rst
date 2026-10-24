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
