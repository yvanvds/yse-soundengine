Threading model
===============

libYSE runs on several threads. Your code talks to it from one of them, the
**control thread**, and the engine takes care of the rest. This page lists
the threads, says what ``System().update()`` does on yours, and gives the
rules for calling the engine and for code the engine calls back.

The threads
-----------

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Thread
     - What it does
   * - **Control thread**
     - Your thread: the one that calls ``System().init()``,
       ``System().update()`` and ``System().close()``. It creates, changes and
       releases sounds, channels, synths and the rest. See `What update()
       drives`_.
   * - **Audio thread**
     - The audio device's callback (PortAudio on desktop, Oboe on Android).
       Once per block of 128 samples it applies what the control thread
       queued, advances clocks, clips and MIDI file playback, and renders the
       mix. Under ``System().initOffline()`` there is no device, and
       ``System().renderOffline()`` runs the same work on the thread that
       calls it.
   * - **Render workers**
     - A pool that helps the audio thread render each block (see
       `Rendering`_). They only run render tasks.
   * - **Background pool**
     - One engine thread for slow work that must stay off the audio thread:
       opening and decoding sound files, refilling streamed sounds, freeing
       released objects, closing unused sound files, and patcher file and
       MIDI-port work.
   * - **Patcher timer thread**
     - Runs the millisecond ``.metro`` and ``.clocker`` objects (see
       :doc:`/patcher/time`).
   * - **MIDI threads**
     - RtMidi runs one input thread per open ``midiIn`` port; your
       ``midiIn`` callbacks run there. A MIDI sender thread sends what clips
       and ``.midiout`` queue for a MIDI output port, so the RtMidi send never
       happens on the audio thread. Desktop builds with
       ``YSE_ENABLE_MIDI_DEVICE`` only.
   * - **Script thread**
     - Runs Python live-coding scripts. Only in builds with
       ``YSE_ENABLE_PYTHON``.

The engine keeps allocation, locks and waiting off the audio thread and the
render workers. Everything the control thread asks of them travels through
lock-free queues, and everything slow they need done goes to the background
pool. The one exception is a change to the number of speaker outputs, which
resizes the mix buffers on the audio thread (see
``setChannelConfiguration()`` below).

Rendering
---------

The channel tree is rendered as a graph of small tasks. A channel's sounds
are grouped into one or more **voice slices**, each a task, and each channel
has a mix task that runs once its slices and its child channels are done.
The audio thread renders tasks itself and shares them with the render
workers, which take whichever tasks are free.

- **Channels are not threads.** A busy channel is split into more slices by
  itself: the engine measures what each sound costs and opens a new slice
  when one grows more expensive than waking a worker. Put sounds on the
  channels you want to mix them on. Adding channels does not make the render
  faster.
- **Small scenes stay on one thread.** When a whole block costs less than
  waking a worker, no worker is woken for it: the audio thread renders it,
  helped only by workers still awake from the block before. After a short spin at the end of a block, idle workers sleep
  until a block needs them.
- **The output does not depend on the threads.** Slices and channels are
  always summed in the same order, so the mix is bit-identical whatever the
  worker count.
- **Your DSP can run on a worker.** A sound's DSP source, a patcher played
  as a sound and a channel's inserts run on the audio thread *or* on a
  render worker. Both follow the audio thread's rules.

``System().renderThreads(count)`` sets the pool size:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - ``count``
     - Workers
   * - ``-1`` (default)
     - Auto: the physical cores the process may use, minus one, at most 8.
       The audio thread renders too, and your application keeps a core. On
       a CPU with performance and efficiency cores, workers go to the
       performance cores first.
   * - ``0``
     - None. The audio thread renders everything. Use it on constrained
       hardware.
   * - ``n``
     - Exactly ``n`` (at most 64).

Call it before ``init()``. During a session with an audio device the value is
stored and applied by the next ``init()``; with no session, or an offline one,
it applies at once. The setting survives ``close()``.
``System().activeRenderThreads()`` returns the number of workers running now.
The C API has the same calls: ``yse_system_set_render_threads``,
``yse_system_get_render_threads`` and ``yse_system_get_active_render_threads``.

What update() drives
--------------------

Call ``System().update()`` regularly, typically once per frame, from the
thread that called ``init()``. Each call does this on your thread:

1. **Flags an engine update for the audio thread.** At its next block the
   audio thread picks up new and released objects and applies what their
   setters queued. It hands file loading, freeing of released objects and
   unused-file cleanup to the background pool, and decides which sounds
   become virtual. Without ``update()`` a new sound never starts and a
   released one is never freed.
2. **Runs the occlusion callback** for every sound that has occlusion
   enabled (see ``System().occlusionCallback``). It runs here, never on the
   audio thread, so it may raycast through your physics engine. Keep it
   short: it runs inside ``update()``.
3. **Delivers named-bus messages.** Values that the audio thread, the timer
   thread, the script thread or another thread of yours published since the
   last call reach their subscribers here, on your thread. A publish made on
   the control thread itself is delivered straight away.
4. **Writes queued log lines** from code that may not log directly, such as
   the patcher's ``.print``.
5. **Wakes the script thread** and passes any script errors to the C API's
   error callback (Python builds only).
6. **Rebuilds a lost audio device** when the backend reported the loss on a
   thread of its own.
7. **Runs the device watchdog.** It counts ``update()`` calls without an
   audio callback (``System().missedCallbacks()``) and, when
   ``System().autoReconnect()`` is on, reopens the device after the delay you
   set.

``init()`` records which thread is the control thread. Call ``update()`` and
``close()`` from that same thread. Calling ``update()`` from another thread
still works, but every bus publish then waits for the next ``update()``
instead of being delivered at once.

Rules for the host
------------------

.. rubric:: Call the engine from the control thread

Setters on ``sound``, ``channel``, ``synth``, ``reverb`` and the other object
classes put a message on that object's queue. Each queue takes one writer, so
call a given object from one thread, the control thread. These calls are
**control-thread only**:

- ``System().init()``, ``update()`` and ``close()`` (see above), and
  ``System().renderThreads()``.
- ``System().setChannelConfiguration()``. It sets the speaker layout that
  the next audio block switches to. A different number of outputs makes that
  block reallocate the mix buffers, so change it at setup, not while
  something audible plays.
- Domain clocks: ``System().createClock()``, ``destroyClock()`` and
  ``setTempo()``. ``beatPosition()`` and ``currentTempo()`` may be read from
  a UI thread, for example to draw a playhead.
- Clips: ``clip::create()``, ``setEvents()``, ``loopLength()`` and
  ``connect()``.
- ``midiIn::connect()``. Do not call it from inside a ``midiIn`` callback.
- Named-bus subscriptions and taps in the C API (``yse_bus_subscribe``,
  ``yse_bus_tap_create`` and their release calls). Their callbacks then run
  on the control thread, and none runs after the release returns.

The patcher is the exception: any number of your threads may edit and drive
it at once. :doc:`/patcher/realtime` has its rules.

.. rubric:: Never call the engine from the audio thread

Code the engine runs on the audio thread or a render worker — a DSP source,
an insert, a patcher object you wrote — must not call the engine's public
API, allocate, lock, log or do I/O. Hand work to another thread through a
lock-free queue instead.

.. rubric:: Log handlers

A ``logHandler`` (``YSE::Log().setHandler``) or a C log callback
(``yse_log_set_callback``) receives lines from several engine threads: the
control thread, the background pool, the MIDI threads and the render workers
while they start. The engine delivers one line at a time and holds its log
lock during the call. So a handler:

- is never called from two threads at once, and may keep ordinary state
  without its own locking;
- may be called on any of those threads, never on the audio thread;
- must not log back through ``YSE::Log()``. That deadlocks;
- should not block. Every other thread that logs waits for it, the
  background pool's file loading included. Pass slow work to a thread of your
  own.

Where to go next
----------------

- :doc:`/patcher/realtime`: the same model from the patcher's side, with the
  timer thread, the background pool and message handlers in detail.
- :doc:`/patcher/time`: the block clock, the timer thread and domain clocks.
- :doc:`/tutorials/03_channels`: building a channel tree.
