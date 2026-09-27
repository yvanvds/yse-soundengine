Time, scheduling and clocks
===========================

Every timed object in a patch gets its timing from one of three sources.
Knowing which one an object uses tells you how accurate it is, whether it keeps
running while the engine is paused, and which thread its output runs on.

.. list-table::
   :header-rows: 1
   :widths: 20 27 27 26

   * -
     - Block clock
     - Millisecond timer
     - Domain clock
   * - Unit
     - milliseconds, rounded up to whole audio blocks
     - whole milliseconds
     - beats, at a tempo that can change
   * - Resolution
     - one block (128 samples, about 2.9 ms at 44.1 kHz)
     - about 1 ms, plus operating-system wake-up jitter
     - one block (see :ref:`time-clock-resolution`)
   * - Runs while nothing renders
     - no, pending messages wait
     - yes
     - no, the clock only advances with the audio callback
   * - Output runs on
     - the audio thread, at the start of a block
     - the engine's timer thread
     - the audio thread, at the start of a block
   * - Used by
     - ``.delay``, ``.pipe``, ``.line``, ``.qlim``, ``.speedlim``,
       ``.thresh``, ``.quickthresh``
     - ``.metro`` and ``.clocker`` in milliseconds
     - ``.tempo``, ``.timepoint``, and ``.metro``, ``.delay``, ``.clocker``,
       ``.qlim`` and ``.speedlim`` once they are given a beat time

The rest of this page explains each source, lists the objects, and ends with
the limits. :doc:`objects/time` has the full reference for every object named
here.

The block clock
---------------

Each patcher counts the audio blocks it renders. A block is 128 samples. Most
objects that wait, such as ``.delay``, ``.pipe`` and ``.line``, use this
counter through the patcher's **deferred-message scheduler**. The object asks
for a message "N milliseconds from now". The scheduler turns N into a block
count and delivers the message when the counter gets there.

How a wait becomes blocks
~~~~~~~~~~~~~~~~~~~~~~~~~

The scheduler rounds the wait **up** to whole blocks, at the current sample
rate:

.. code-block:: text

   blocks = ceil(ms × sampleRate / 1000 / 128), and at least 1

So a wait is never shorter than asked, and is at most one block longer. At
48 kHz, 100 ms is 37.5 blocks, which becomes 38 blocks (101.3 ms). At 44.1 kHz
it becomes 35 blocks (101.6 ms).

A wait of 0 ms still takes **one block**. A deferred message is never
delivered inside the message that scheduled it. That is why a ``.delay 0``
wired back into its own inlet makes a fast metronome (one bang per block)
instead of an endless loop.

When the message arrives
~~~~~~~~~~~~~~~~~~~~~~~~

At the start of every block, before any audio is computed, the patcher does
these steps in order:

1. apply queued parameter changes (see :doc:`building`),
2. deliver the values the host sent with ``PassData`` / ``PassBang`` (see
   :doc:`host_io`),
3. deliver every deferred message that is due,
4. render the block.

So a deferred message runs **on the audio thread**, and whatever it changes
is heard in the same block. Messages due in the same block arrive in the order
they were scheduled. Each one is a new logical event (see :doc:`messages`):
everything it sets off counts as one event.

Where the count starts depends on where the wait was requested:

- **From inside the patch**, for example a ``.delay`` banged by a ``.r`` or by
  another deferred message, the wait starts at the current block. The
  message arrives exactly the rounded-up number of blocks later, in audio
  time.
- **From outside a block**, for example a ``pHandle`` call on the host's
  thread, or a millisecond ``.metro`` banging a ``.delay``, the count starts
  at the last block rendered. The message can therefore arrive up to one
  block earlier than the rounded-up wait, measured from the moment of the
  request.

The audio device may ask for more than 128 samples per callback. The engine
then renders several blocks back to back in one callback. Deferred messages
stay accurate to the block in the **audio** you hear. Measured on a wall
clock, they arrive in bursts, one burst per device callback.

What stops the block clock
~~~~~~~~~~~~~~~~~~~~~~~~~~

The counter only moves while the patcher renders. A patcher renders only when
it is attached to a sound or an insert and the engine is running (see
:doc:`index`). Until then, every pending message waits. "100 ms from now"
means 100 ms of rendered audio.

A pending message belongs to one object. If you delete that object, or
``SetParams`` rebuilds it, the message is dropped and does not reach the new
object (see :doc:`building`).

The millisecond timer
---------------------

``.metro`` and ``.clocker`` with a millisecond interval do not use the block
clock. They use a real operating-system timer: one engine thread serves every
millisecond timer in the process.

- **Resolution.** Intervals are whole milliseconds, 1 ms at the least. On
  Windows, ``System::init`` sets the system timer resolution to 1 ms.
- **No drift.** Each tick is scheduled at the previous *deadline* plus the
  interval, not at the time the previous tick actually ran. A late tick does
  not push the ones after it back.
- **Jitter.** A tick runs when the operating system wakes the timer thread,
  which can be a little late. The thread also runs callbacks one at a time.
  If one metro's tick sets off a lot of work, other timers wait until it is
  done. When a tick is overdue by more than an interval, the missed ticks run
  straight away, one after another.
- **Keeps running.** The timer does not depend on rendering. A millisecond
  ``.metro`` keeps banging while the engine is paused or the patcher is not
  attached.

Which thread
~~~~~~~~~~~~

A timer tick runs the object's output **on the timer thread**. Everything
downstream runs there too, in the same call. This matters in three places:

- A ``.s`` driven by a millisecond ``.metro`` reaches the send callback on the
  timer thread (see :doc:`host_io`).
- A bus publish from the timer thread is delivered at the next
  ``System::update()``.
- A signal object gets the new value at the next block it renders. So a
  metro tick is audible at a block boundary. How far that is from the tick
  depends on the device buffer size.

If you need a pulse that stays in step with the audio, or that stops when
the engine stops, use the beat engine (below), or a ``.delay`` wired into
itself.

.. _time-metro-changes:

``.metro`` in 3.0
~~~~~~~~~~~~~~~~~

.. versionchanged:: 3.0
   **Live interval** (issue `#625
   <https://github.com/yvanvds/yse-soundengine/issues/625>`_). A number in
   the right inlet changes the interval of a running metro without
   restarting it. The next tick moves to *previous tick + new interval*. When
   that time is already past, the tick fires straight away. A ``SetParams``
   change to ``period`` also reaches a running metro, one tick later.

.. versionchanged:: 3.0
   **Left inlet** (issue `#711
   <https://github.com/yvanvds/yse-soundengine/issues/711>`_). The left
   inlet now takes all four of Max's messages:

   - a non-zero int or float starts the metro and 0 stops it. A float is
     compared with 0, not rounded, so ``0.5`` starts it.
   - ``bang`` starts it. On a metro that is already running, a bang restarts
     it from now. One button wired to several metros brings them into step.
   - ``stop`` stops it.

   Every start sends a bang straight away, as Max does.

.. versionchanged:: 3.0
   Starting and stopping a millisecond metro from the audio thread is now
   real-time safe (issue `#718
   <https://github.com/yvanvds/yse-soundengine/issues/718>`_). There the
   request is handed to a background thread, so the second tick may be one
   thread hop late, and a stop may let one more tick through. From any other
   thread, the start and stop happen before the call returns, and no tick
   comes after a stop.

Domain clocks and beat time
---------------------------

A **domain clock** is a named musical clock owned by the engine. It counts
beats at a tempo in BPM. The tempo can jump, glide over a number of seconds,
drop to 0 (paused) or even go negative. The beat position is the running
total of tempo over time, so a tempo change changes how fast beats go by and
never makes the position jump.

The host creates clocks with ``System::createClock``, ``setTempo`` and
``beatPosition`` (``yse_system_create_clock`` and friends in the C API).
Clip transports play on the same clocks (see :doc:`/api/c_api`). A patch
refers to a clock by name, so the host, the clips and the patch all share
it. The guide to clocks and clips is tracked in `#886
<https://github.com/yvanvds/yse-soundengine/issues/886>`_.

Waiting on a clock
~~~~~~~~~~~~~~~~~~

An object on a clock waits for a **beat position**, not for a number of
milliseconds. It uses the same scheduler as the block clock. The scheduler
checks the clock at the start of every block and delivers the message once
the clock has reached the target beat. So the output runs on the audio thread,
like a ``.delay``, and a beat wait:

- gets shorter when the tempo rises, and longer when it falls;
- follows a tempo glide smoothly;
- never finishes while the tempo is 0.

Objects that repeat (``.metro``, ``.tempo`` and ``.clocker`` on a clock) do
not count their wake-ups. At every wake-up they read the clock and work out
how many grid points it has passed. Each wake-up is late by up to one block,
but that error never adds up, and a pulse faster than one block still gives
the right number of outputs. If the clock jumps a long way in one block, a
``.metro`` sends at most 64 catch-up bangs and skips the rest.

Several objects on the same clock stay in exact step with each other and with
every clip on that clock. Two millisecond metros cannot do that.

Tempo-relative time values
~~~~~~~~~~~~~~~~~~~~~~~~~~

Objects that accept beat time read Max's tempo-relative syntax. A quarter
note is one beat.

.. list-table::
   :header-rows: 1
   :widths: 30 20 50

   * - Written
     - Beats
     - Meaning
   * - ``1n`` ``2n`` ``4n`` ``8n`` … ``128n``
     - 4, 2, 1, 0.5 …
     - Note values. The number must be a power of two from 1 to 128.
   * - ``4nd``
     - 1.5
     - Dotted: the value × 3/2.
   * - ``8nt``
     - 1/3
     - Triplet: the value × 2/3.
   * - ``1440 ticks``
     - 3
     - Ticks, 480 per beat. Use this for any other length.

Any plain number means **milliseconds**, as in Max, and switches a
two-unit object back to its millisecond engine. ``bars.beats.units``,
``hh:mm:ss``, ``quantize`` and Max's ``transport`` attribute are not read: a
domain clock counts beats but has no bars and no time signature.

Binding a clock
~~~~~~~~~~~~~~~

``.metro``, ``.delay``, ``.clocker``, ``.qlim`` and ``.speedlim`` take a beat
time in the right inlet. They also need a ``clock <name>`` message in the
left inlet to say which clock to count on. A bare ``clock`` returns the object
to milliseconds. ``.tempo``, ``.timepoint``, ``.when``, ``.timer`` and
``.translate`` take the clock name as a creation argument instead.

- A beat time with no clock bound does nothing. ``.delay`` does not fire and
  ``.metro`` does not start. ``.speedlim`` and ``.qlim`` let messages through,
  so a stream is never swallowed.
- ``.metro`` and ``.clocker`` choose their engine when they are started. A
  ``clock`` message or a change of unit takes effect at the next start.
- A name may refer to a clock that does not exist yet. The object then
  waits. The patcher looks for the clock again every 64 blocks, and the wait
  starts counting when the clock appears.
- A clock that is destroyed while objects are bound to it freezes. Objects on
  it stop and never fire. Creating a new clock with the same name does not
  reconnect them.

Only ``.transport`` and ``.setclock`` create clocks, and neither one ever
destroys a clock. All other objects only use a clock that already exists.

.. _time-clock-resolution:

How accurate a beat is
~~~~~~~~~~~~~~~~~~~~~~

The engine advances every domain clock once per 128-sample **block**, just
before it renders that block, by the length of one block. The patcher checks
beat deadlines at the start of the same block, so a beat wait is accurate to
one block. This holds for any device buffer size: when the device asks for
512 samples per callback, the engine renders four blocks and the clock moves
between each of them. Offline rendering (``renderOffline``) behaves the same.

Clip transports advance with the clocks, one block at a time, so a clip and a
patch on the same clock fire in the same block.

While no sound is playing, nothing renders. The clocks then keep moving once
per device callback, by the length of that callback.

.. versionchanged:: 3.0
   Clocks used to advance once per device callback, so with a device buffer
   larger than 128 samples every beat deadline in a callback came due in its
   first block, up to one buffer early (issue `#944
   <https://github.com/yvanvds/yse-soundengine/issues/944>`_).

The objects
-----------

Delays and ramps
~~~~~~~~~~~~~~~~

``.delay``
   Sends a bang after a set time: 5 ms by default, or a beat time on a bound
   clock. It holds only one bang. A new bang restarts the wait, and ``stop``
   cancels it. A number in the right inlet sets the time for the next bang
   and leaves a bang already waiting alone. Block clock or domain clock.

``.pipe``
   Delays ints, floats and lists. Unlike ``.delay`` it holds many values, and
   each value keeps its own deadline, so ten values in give ten values out in
   the same order. ``clear`` drops everything waiting and ``flush`` sends it
   all now. Milliseconds only.

``.line``
   Ramps a number to a target over a set time, sending a value every *grain*
   milliseconds (20 by default) and banging its right outlet when it arrives.
   Each value is computed from the time elapsed, so a late step jumps
   straight to the right value. A grain shorter than one block gives one
   value per block. Milliseconds only.

``.bline``
   The same ramp, but it moves one step each time it is banged instead of on
   a timer. It has no clock of its own. Drive it from a ``.metro`` or a
   ``.tempo`` to make a ramp in beats.

Rate limiting and grouping
~~~~~~~~~~~~~~~~~~~~~~~~~~

``.speedlim``
   Passes a message only if the interval has gone by since the last one it
   passed, and **drops** the rest. Time is read from the block counter, so
   two messages in the same block are 0 ms apart. It never schedules
   anything. Takes a beat time, plus ``threshold`` and ``quantize`` messages.

``.qlim``
   The same limit, but it **holds** early messages and sends them when the
   interval is up. By default a new message replaces the one waiting;
   ``usurp 0`` queues up to 32 instead. Takes a beat time.

``.thresh``
   Collects values that arrive close together into one list. Every value
   restarts the wait, so the list goes out once the input has been quiet for
   the threshold (10 ms by default). Milliseconds only.

``.quickthresh``
   The same grouping, but the window starts at the first value. The group
   goes out a fixed time after it began, which is what chord detection
   needs. Milliseconds only.

Pulses and measuring
~~~~~~~~~~~~~~~~~~~~

``.metro``
   Sends a bang at a regular interval, 1000 ms by default. The millisecond
   engine is the timer thread; a beat interval with ``clock <name>`` runs on
   the domain clock. See :ref:`time-metro-changes` for the 3.0 changes.

``.clocker``
   Sends the time since it was started, every interval (5 ms by default).
   The elapsed milliseconds are measured with the system's steady clock, not
   added up from ticks, so they stay right even when a tick is late. With a
   clock bound, a second outlet sends the beats that clock has moved since
   the start. The millisecond engine is the timer thread.

``.timer``
   A bang in the left inlet marks a start, and a bang in the right inlet
   sends the time since that start, measured with the steady clock. With a
   clock name as argument, a second outlet sends the same interval in beats.
   It has no timing of its own and runs on the thread of the bang.

Musical time
~~~~~~~~~~~~

``.transport``
   Starts and stops a named clock and sets its tempo:
   ``.transport main 120``. If no clock of that name exists yet, it creates
   one when it joins the patcher, **stopped**, so adding one never starts
   anything. Stopping sets the tempo to 0, which pauses the clock. Starting
   continues from the same beat, and ``tempo 140 2`` glides to 140 BPM over
   two seconds. A bang sends the beat position and the tempo.

``.setclock``
   Creates a named clock that is **already running**: ``.setclock fast 240``.
   The number it receives is the tempo, and 0 stops it. Use it to give part of
   a patch a tempo of its own.

``.tempo``
   Counts notes on a clock: ``.tempo main 120 1 16`` sends 0, 1 … 15, 0 …
   in sixteenth notes. The interval is 4 × multiplier / division beats.
   Starting it writes its tempo to the clock. Stopping it leaves the clock
   alone, so the clips on that clock keep playing. Clock only.

``.timepoint``
   Bangs once when a clock passes a beat position: ``.timepoint main 128``.
   It fires on the way past, then waits until it is armed again. A target that
   is already behind the clock never fires, so loading a patch halfway through
   a piece does not fire every cue at once. Clock only.

``.when``
   On any input, sends the clock's beat position and tempo. It only reads the
   clock and never creates one.

``.translate``
   Converts between ``ms``, ``hz``, ``samples``, ``beats`` and ``ticks``, and
   accepts note values: ``.translate ms beats main``. Conversions between
   milliseconds and beats use the clock's tempo at that moment. A bang
   converts the last value again. With no clock, or a clock at tempo 0, it
   sends nothing.

``.qlist``, ``.seq``, ``.mtr``, ``.bondo``, ``.makenote`` and ``.borax`` also
schedule messages through the block clock, and some of them can use a
domain clock. They are described with their own categories in
:doc:`objects/index`.

Examples
--------

A beat-synced pattern. The host creates the clock. A ``.tempo`` counts
quarter notes on it, and ``PassBang`` starts the count at the next block:

.. code-block:: cpp

   YSE::System().createClock("main", 120.f);

   YSE::patcher patch;
   patch.create(2);

   YSE::pHandle* run   = patch.CreateObject(".r", "run");
   YSE::pHandle* count = patch.CreateObject(".tempo", "main 120 1 4");  // quarter notes
   YSE::pHandle* print = patch.CreateObject(".print", "beat");
   patch.Connect(run, 0, count, 0);
   patch.Connect(count, 0, print, 0);

   // ... attach the patcher to a sound and play it ...
   patch.PassBang("run");                  // starts counting: 0 1 2 3 0 1 ...
   YSE::System().setTempo("main", 90.f, 4.f);  // glides to 90 BPM over 4 s

The same clock drives every object bound to it. A ``.metro`` sent
``clock main`` in its left inlet and ``8n`` in its right inlet would tick
eighth notes that stay locked to this count through the glide.

An echo on the block clock. Each value comes back 250 ms later, rounded up to
whole blocks:

.. code-block:: cpp

   YSE::pHandle* in   = patch.CreateObject(".r", "note");
   YSE::pHandle* echo = patch.CreateObject(".pipe", "250");
   patch.Connect(in, 0, echo, 0);

Limits
------

- **128 pending deferred messages per patcher.** This is shared by every
  object that uses the scheduler, whether it waits on blocks or on a clock.
  A ``.pipe`` uses one slot per waiting value (and holds at most 64); a
  ``.qlim`` uses one slot however long its queue is. When the table is
  full, a new request is refused and counted, not sent early. List payloads
  must be shorter than 256 characters.
- **8 clock names per patcher.** Objects that name the same clock share one
  entry. A clock name can be up to 63 characters.
- **256 millisecond timers per process.** Each ``.metro`` and ``.clocker``
  takes one when it is created. Past that, a new one has no millisecond
  timer (its beat engine still works).
- **Nothing is logged** when one of these limits is reached, because the
  refusal may happen on the audio thread.
