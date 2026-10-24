.. Notes for the Time category. Format: _data/patcher_notes/README.md.

.. patcher-object:: .bline

The ramp moves one step on every bang, and sends nothing between bangs.

**Messages**

- bang: send the next step. With no ramp running it does nothing.
- ``<target> <bangs>``: reach the target over that many bangs.
- four or more numbers: target/bangs pairs, travelled one after another. An
  odd last number is ignored. Three numbers are read as one pair and the third
  is ignored.
- a number on its own: a target reached on the next bang.
- ``stop``: freeze where the ramp is and forget the rest, without a bang on
  the right outlet.
- ``set <value>``: move to that value without sending it, and stop the ramp.

A count of 0 or less takes one bang. A fractional count arrives on the first
bang at or past it.

.. code-block:: text

   [.bline]   1 10 0 10, then 20 bangs  =>  0.1 0.2 ... 1. 0.9 ... 0.
                                            right outlet bangs on the 20th

New ramps, the output type and the length limit work as described under
``.line``. Each value goes out on the thread of the bang that asked for it.

.. patcher-object:: .clocker

Use it to drive anything that needs a position in time rather than a pulse:
an envelope, a progress display, a sweep.

**Messages**

- a non-zero number: start, counting from 0. A running clocker starts again.
- ``0`` or ``stop``: stop.
- bang: start a stopped clocker. On a running one, set the count back to 0
  and keep the reports at the same moments.
- ``reset``: the same reset. It does nothing while the clocker is stopped.
- ``clock <name>``: bind a clock for the beats outlet and for a beat
  interval. A bare ``clock`` removes it.

The first report comes one interval after the start, not at the start. The
milliseconds are measured on the system's steady clock, so a report that is
late still carries the real time since the start.

The interval can be changed while the clocker runs; the reports carry on from
where they were. A beat interval works as described under ``.delay``: the
reports then fall on that clock's beat grid, and a clocker given a beat
interval but no clock does not start.

**The beats outlet**

Whenever a clock is bound, each report also sends the beats that clock has
moved since the start, just before the milliseconds. The two differ when the
tempo changes, and the beats stand still while the clock is stopped. If the
clock does not exist yet when the clocker starts, the beats are counted from
the first report that finds it. Binding another clock while running starts
the beat count again on the new clock and leaves the milliseconds alone.

The millisecond engine runs on the timer thread, like ``.metro``'s. See
:doc:`/patcher/time`.

.. patcher-object:: .delay

**Messages**

On the left inlet:

- bang: start the wait. A bang that is already waiting is forgotten and the
  wait starts again.
- a number or a beat time: set the time and start the wait.
- ``stop``: cancel the waiting bang without sending it.
- ``clock <name>``: the clock for beat times (see below).

A number in the right inlet sets the time for the next bang only. A bang that
is already waiting keeps the time it started with.

A delay of 0 waits one audio block, so a ``.delay 0`` wired into its own inlet
bangs once per block instead of looping. Waits are rounded up to whole blocks
and stop while nothing renders; see :doc:`/patcher/time`.

**Beat time**

``.delay``, ``.metro``, ``.clocker``, ``.speedlim`` and ``.qlim`` also take a
time in beats:

- A note value (``4n``, ``8nd``, ``8nt``) or a tick count (``1440 ticks``)
  sets the time in beats.
- The beats are counted on the clock named by ``clock <name>`` in the left
  inlet. A bare ``clock`` removes it.
- A plain number switches back to milliseconds.

The wait then follows the clock: it stretches when the tempo falls, follows a
glide, and stands still while the clock is stopped. On ``.delay``, a beat time
with no clock bound does nothing; send ``clock <name>`` and the next bang
works. The binding is not saved with the patch. Bar-based times are not
supported, because a domain clock has no bars. The note-value syntax and the
rules for binding are in :doc:`/patcher/time`.

.. patcher-object:: .line

**Messages**

On the left inlet:

- a number: the target. It is reached over the time last sent to the middle
  inlet, and that time is then used up: the next target without a new time
  jumps there at once.
- ``<target> <time>``: a target and its time together. Use this form to ramp
  every target.
- ``<target> <time> <grain>``: also sets the grain, which stays set.
- four or more numbers: target/time pairs, travelled one after another. An
  odd last number is ignored.
- ``stop``: freeze where the ramp is and forget the rest, without a bang on
  the right outlet.
- ``pause`` / ``resume``: hold the ramp and carry on with what was left.
- ``set <value>``: move to that value without sending it, and stop the ramp.

.. code-block:: text

   [.line 0.]   1 1000 0 1000  =>  rises to 1. over 1 s, falls to 0. over
                                   the next second, then bangs the right outlet

Every new ramp starts from the current value and replaces the segments still
to come, so it never jumps. A target reached with no time still bangs the
right outlet.

**Timing**

The first value goes out one grain after the ramp starts. Each value is
worked out from the time that has passed, so a late step jumps to the right
value and the ramp arrives on time. Times are counted in audio blocks: a
grain shorter than a block gives one value per block, and the ramp stands
still while nothing renders (see :doc:`/patcher/time`). A grain below 1 ms
counts as 20.

Times are in milliseconds only, as described under ``.pipe``.

**Output type**

A starting value written as a float (``0.``, ``1.5``) makes the object send
floats. Otherwise it sends ints, truncated toward 0, except on a segment that
covers 1 or less in steps smaller than 0.4: that segment is sent as floats,
so a ramp from 0 to 1 still moves.

**Limits**

A ramp holds up to 64 segments; numbers past the 129th are ignored. If the
patcher's scheduler is full, the ramp jumps to its target.

.. patcher-object:: .metro

**Messages**

On the left inlet:

- a non-zero number: start. A float is compared with 0, so ``0.5`` starts.
- bang: start, or on a running metro, start again from now. One button wired
  to several metros brings them into step.
- ``0`` or ``stop``: stop.
- ``clock <name>``: the clock for a beat interval.

Every start sends a bang straight away.

A number in the right inlet changes the interval of a running metro without
restarting it: the next tick is due at the last tick plus the new interval,
or at once if that has passed. Floats are truncated, and the shortest
interval is 1 ms.

**Beat interval**

A note value or tick count works as described under ``.delay``. A beat metro
stays in step with every other object and clip on its clock. Whether a run
uses milliseconds or beats is fixed when it starts, so a ``clock`` message
or a switch between the two units takes effect at the next start. A new beat
interval reaches a running beat metro at once and keeps its phase. A beat
interval with no clock bound does not start the metro.

**Timing**

A millisecond metro runs on the engine's timer thread, keeps running while
nothing renders, and drives everything downstream on that thread. A beat
metro runs at block boundaries on the audio thread. When its clock jumps
ahead, it sends at most 64 catch-up bangs. See :doc:`/patcher/time` and
:ref:`time-metro-changes`.

.. patcher-object:: .pipe

Each value waits on its own: a new value never cancels one that is waiting,
so ten values in give ten values out. A number in the right inlet sets the
delay for values that arrive after it.

**Messages**

- an int, a float or a list: delayed and sent unchanged. A list that is a
  single number comes out as that number. A list travels whole; split it
  afterwards if needed.
- ``clear``: drop every waiting value.
- ``flush``: send every waiting value now, in the order they arrived.

A delay of 0 waits one audio block, so a ``.pipe 0`` wired into itself does
not loop. Waits are rounded up to whole blocks; see :doc:`/patcher/time`.

**Milliseconds only**

``.pipe``, ``.line``, ``.thresh`` and ``.quickthresh`` take times in
milliseconds only. A note value or tick count sent to a time inlet is
ignored, and the time stays as it was.

**Limits**

Up to 64 values can wait at once, and a list can be up to 256 characters
long. Each waiting value also takes one of the patcher's 128 scheduler
slots. A value that does not fit is dropped, not sent early.

.. patcher-object:: .qlim

A message that arrives too soon is held and sent when the interval is up,
one interval after the previous output. What happens to the next message
that arrives while one waits depends on ``usurp``:

- ``usurp 1`` (the default): the new message replaces the waiting one, so a
  burst comes out as its last message. Use it for values such as a fader
  position, where the latest one matters.
- ``usurp 0``: messages queue, up to 32, and go out one per interval in the
  order they arrived. Use it for events that must all happen. When the queue
  is full, new messages are dropped.

``usurp`` on its own means ``usurp 1``.

A message that arrives inside the threshold window is dropped, not held. The
interval, the threshold, ``quantize`` and the message rules work as described
under ``.speedlim``; with a grid set, a held message goes out on the first
grid line at or after the end of its interval.

A held list can be up to 256 characters long. Holding uses one of the
patcher's scheduler slots, however long the queue; if none is free, the
waiting messages are dropped.

.. patcher-object:: .quickthresh

The window opens with the first value of a group and closes ``threshold``
milliseconds later, however many values arrive in it. If a value lands in the
last ``fudge`` milliseconds of the window, the window is extended once, by
``extension``. A group therefore lasts at most threshold plus extension,
and a steady stream cannot hold it back.

.. code-block:: text

   [.quickthresh]   60, 64 5 ms later, 67 12 ms later  =>  60 64 67
                                                           40 ms after the 60

**Messages**

- an int, a float or a list: added to the group.
- bang: send the group now.
- ``set <threshold> [<fudge> [<extension>]]``: set the times given.

A new threshold applies from the next group. A fudge or extension of 0 turns
the extension off. Times are counted in whole audio blocks (about 2.9 ms at
44.1 kHz), which matters most for the short fudge time.

The output and the limits work as described under ``.thresh``.

.. patcher-object:: .setclock

The clock is created when the object is added to a patcher, already running
at the tempo argument. If a clock of that name exists already, it is left
as it is, tempo included. Deleting the object leaves the clock in place;
only the host destroys clocks. Other objects find the clock by name, so one
``.setclock`` can give part of a patch a tempo of its own:

.. code-block:: text

   [.setclock fast 240]   [.tempo fast 240 1 4]

Every number sets the tempo; 0 stops the clock. ``tempo <bpm> <ramp>`` also
sets the glide time, which then applies to every later tempo change. Where
``.transport`` takes 0 and 1 as stop and start, a number here is always a
tempo, so a slider can drive it directly.

There is no ``clock`` message: the clock is the creation argument. With no
name the object does nothing.

.. patcher-object:: .speedlim

A message passes when at least the interval has gone by since the last
message the object sent. Anything earlier is dropped, so a burst comes out as
its first message. Use ``.qlim`` instead when the last value of a burst
matters.

Messages pass unchanged: bang, int, float or list. A list that is a single
number passes as that number. The first message always passes, and an
interval of 0 lets everything through.

**Settings**

On the left inlet, ``.speedlim`` and ``.qlim`` both read:

- ``clock <name>``: the clock for beat times, as described under ``.delay``.
- ``threshold <time>``: a second window after each output in which every
  message is dropped. ``threshold 0`` turns it off.
- ``quantize <time>``: a beat grid, set with a note value or tick count. At
  most one message passes per grid line, and only once the grid has moved
  past the line of the last output. ``quantize 0`` turns it off. It needs a
  bound clock.

The interval and threshold take milliseconds or a beat time. A beat time
with no clock bound does not limit anything, so the stream is never
swallowed. A new interval is measured from the last output, so shortening it
can open the window at once.

Time is counted in whole audio blocks: two messages in the same block are
0 ms apart.

.. patcher-object:: .tempo

The count moves every ``4 × multiplier / division`` beats of the clock and
runs from 0 to ``division - 1``. A division of 4 counts quarter notes (one
beat each), 16 sixteenths. The multiplier slows the count down without
changing the length of the cycle.

**Messages**

On the left inlet:

- bang or a non-zero number: start, or start again, from 0. The 0 goes out
  at once.
- ``0`` or ``stop``: stop counting. The clock keeps running.
- ``tempo <bpm> [<ramp>]``: set the clock's tempo, gliding over ``ramp``
  seconds if given.
- ``clock <name>``: count on another clock. A bare ``clock`` stops the
  counting until a clock is named again.

**The tempo belongs to the clock**

Starting the count, the tempo inlet and ``tempo`` all write the tempo to the
clock, which changes it for every object and clip on that clock. On a clock
that something else drives, such as a ``.transport``, give the ``.tempo`` the
same tempo. Stopping never writes the clock.

Changing the multiplier or division while running keeps the count's place.
If the clock jumps ahead, the count still lands on the right step, but at
most 64 steps are sent. The clock name works as described under ``.when``.

.. patcher-object:: .thresh

Every value restarts the wait, so the group goes out once the input has been
quiet for the threshold. A stream that never pauses never sends a group; use
``.quickthresh`` when the group has to come out a known time after it
started.

**Output**

For ``.thresh`` and ``.quickthresh``:

- A list that arrives is added whole.
- A group of one number comes out as that number, an int or a float by how
  it was written. A larger group comes out as a list.

Time is counted in whole audio blocks: values in the same block are 0 ms
apart, and a threshold of 0 groups what arrives in one block. Times are in
milliseconds only, as described under ``.pipe``.

**Limits**

A group can be up to 256 characters long. A value that would make it longer
is dropped, and the group keeps what it has.

.. patcher-object:: .timepoint

The bang comes on the way past: the first time the clock is at or past the
target, the object bangs once and is then spent. A target that is already at
or behind the clock when the object is armed never fires, so loading a patch
in the middle of a piece does not fire every cue at once.

These arm the object again, and each one fires only if the target is still
ahead of the clock:

- a number: a new target.
- ``active 1``. ``active 0`` disarms the object.
- ``clock <name>``. A bare ``clock`` stops it watching.

If the clock does not exist yet, the object waits for it. A target the clock
passes while the object is still looking for it fires as soon as the clock
is found. The
bang is accurate to one audio block, and never comes while the clock is
stopped short of the target. The clock name works as described under
``.when``.

.. patcher-object:: .timer

A bang in the right inlet sends the time since the last bang in the left
inlet, and leaves the start where it is, so repeated right bangs give split
times. A right bang before any start sends nothing.

The milliseconds are a float, measured on the system's steady clock at the
moment each bang arrives. A tap tempo:

.. code-block:: text

   tap -> [.t b b]   right outlet -> right inlet of [.timer]
                     left outlet  -> left inlet of [.timer]
                     each tap sends the time since the previous one

With a clock name as argument, the beats that clock moved in the same time go
out just before the milliseconds. Nothing goes out on the beats outlet when
the clock did not exist at the start. The clock name works as described under
``.when``; there is no ``clock`` message.

.. patcher-object:: .translate

**Formats**

- ``ms``: milliseconds.
- ``hz``: repetitions per second. 2 Hz is 500 ms.
- ``samples``: at the current sample rate.
- ``beats``: quarter notes on the clock.
- ``ticks``: 480 per beat.

The arguments give the input format, the output format and the clock, in any
order: the first two format words are the input and the output, and any other
word is the clock. Without arguments the object converts milliseconds to
ticks. ``bars.beats.units``, ``hh:mm:ss`` and ``notevalues`` are not
supported; as arguments they are ignored with an error in the log.

.. code-block:: text

   [.translate ms beats main]   at 120 BPM:  500  =>  1.

**Messages**

- a number: a value in the input format.
- a note value (``4n``, ``8nt``) or tick count (``960 ticks``): a value in
  beats, whatever the input format.
- bang: convert the last value again, at the tempo of the moment. Bang it
  from a ``.metro`` to follow a tempo glide.
- ``in <format>`` / ``out <format>``: change a format. An unknown word is
  ignored.
- ``clock <name>``: use another clock. A bare ``clock`` removes it.

Conversions within ``ms``, ``hz`` and ``samples``, or between ``beats`` and
``ticks``, need no clock. Converting between the two groups uses the clock's
tempo at that moment. Nothing is sent when there is no clock, the tempo is
0, or the value is 0 Hz or 0 ms converted to Hz. A clock running backwards
gives a negative result. The clock name works as described under ``.when``.

.. patcher-object:: .transport

The clock is created when the object is added to a patcher, stopped, so
adding a ``.transport`` never starts anything. A clock of that name that
already exists is left as it is. Deleting the object leaves the clock in
place; only the host destroys clocks.

**Messages**

On the left inlet:

- a non-zero number or ``start``: run the clock at the object's tempo.
- ``0`` or ``stop``: stop the clock at once. It keeps its beat position, and
  the next start carries on from there. There is no way to jump to another
  position.
- ``tempo <bpm> [<ramp>]``: set the tempo. A ramp, when given, is the glide
  time in seconds for every later start and tempo change.
- bang: send the clock's tempo, then its beat position.

A tempo sent while stopped is kept for the next start. Stopping sets the
clock's tempo to 0, even when the host started it. The tempo outlet reports
the clock's actual tempo, so it shows a glide in progress and changes made
by the host.

.. patcher-object:: .when

Any message works as a query, so a ``.when`` can sit at the end of anything
that already fires, such as a ``.metro``. The tempo goes out first, then the
beat position. A stopped clock reports tempo 0.

**Clock names**

``.when``, ``.tempo``, ``.timepoint``, ``.timer`` and ``.translate`` take the
clock name as a creation argument:

- The name refers to an engine clock, shared with the host and with clips.
- These objects never create a clock. Until a ``.transport``, a
  ``.setclock`` or the host creates it, they send nothing, and they start
  working once it exists.
- A clock destroyed while they use it stands still.

See :doc:`/patcher/time` for clocks and the limits on how many a patcher can
use.

.. patcher-object:: ~count

Outputs a signal that goes up by 1 at every sample, for reading a buffer
sample by sample or timing events to the sample. A new counter waits at its
initial value; with a third argument of 1 it counts from the start.

**Messages (left inlet)**

- bang: count from the initial value.
- an int: make it the initial value and count from it.
- a float, ``min <n>`` or ``set <n>``: change the initial value without
  restarting (while stopped, the output changes to it).
- ``stop``: stop and go back to the initial value.
- ``<initial> <limit> <enable>``: set all three; enable 1 counts from the
  initial value, 0 stops. Two numbers set the initial value and the limit
  only.

A signal in the left inlet counts while it is not 0: each time it turns
non-zero the count restarts from the initial value, and where it is 0 the
output is the initial value.

**Limit.** A number in the right inlet, or the second argument, makes the
counter loop: the count goes back to the initial value instead of reaching
the limit, so ``[~count 0 4 1]`` counts 0 1 2 3 0 1 2 3. 0 means no limit.
The right outlet counts how many times the count has gone back since it last
started.

The count is exact up to 16777216 (about 6 minutes at 44.1 kHz); past that a
float signal rounds it. A fourth argument (autoreset) is accepted and has no
effect: the counter starts from its initial value when it is created.
Messages are not saved with the patch; the arguments are.

.. code-block:: text

   [~count 0 44100 1] -> [~/ 44100.]   a 0-1 ramp, once a second at 44.1 kHz