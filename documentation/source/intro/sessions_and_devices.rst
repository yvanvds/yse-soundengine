Sessions and devices
====================

A **session** is everything between ``System().init()`` (or
``initOffline()``) and ``System().close()``. This page covers how a session
starts, which sample rate it runs at, how to pick and switch audio devices,
how to tell whether audio is flowing, and what happens to MIDI devices. For
which thread may make these calls, and what ``System().update()`` does each
time you call it, see :doc:`/intro/threading`.

Starting a session
------------------

There are two ways to start one:

.. list-table::
   :header-rows: 1
   :widths: 26 74

   * - Call
     - What you get
   * - ``System().init()``
     - Starts the audio backend and opens the platform's default output
       device. The audio thread renders the mix.
   * - ``System().initOffline()``
     - The same engine, channel tree and DSP graph, but no audio backend and
       no audio thread. You render blocks yourself with
       ``System().renderOffline(blocks)``. Use it for tests, benchmarks and
       headless tools.

Both create the master channel and the five built-in channels (ambient, FX,
music, GUI, voice), install a stereo speaker layout and set ``maxSounds`` to
50. Calling ``init()`` again while a session is running logs a message and
returns ``true`` without doing anything.

.. note::

   On desktop, ``init()`` returns ``false`` only when the audio backend itself
   fails to start. If there is no default output device, or the device refuses
   the stream, the reason is logged and ``init()`` still returns ``true``
   (tracked as `#973 <https://github.com/yvanvds/yse-soundengine/issues/973>`_).
   To confirm a device is open, check that ``System().getActiveSampleRate()``
   is not 0. See also `Is audio flowing?`_.

``System().close()`` ends the session. It closes the device, stops the engine
threads and frees the engine's side of every sound and channel. You can call
``init()`` again afterwards to start a new session. The requested sample rate
and the ``renderThreads()`` setting carry over to it.

Sample rate
-----------

The sample rate is fixed for a whole session. To choose it, call
``System().requestSampleRate(rate)`` **before** ``init()`` or
``initOffline()``:

.. code-block:: cpp

   YSE::System().requestSampleRate(44100);
   YSE::System().init();
   double rate = YSE::System().getSampleRate();   // what the device agreed to

- **With a device**, the engine asks the device for the requested rate. If
  the device refuses it, the engine logs a warning and opens at the device's
  own default rate instead. This is not an error. ``getSampleRate()``
  reports the rate the session really runs at.
- **Without a request** (or after ``requestSampleRate(0)``), ``init()`` opens
  the device at its default rate.
- **Offline**, there is no device to ask, so the session runs at exactly the
  requested rate, or at **48 kHz** if you did not request one.

A request made during a session is only stored for the next one. To change
the rate of a running engine, call ``close()`` and then ``init()``. The
request survives ``close()``, so you can set it once for repeated sessions.

Two getters report the rate:

- ``getSampleRate()`` is the session rate. It stays the same through
  ``pause()`` and ``resume()``, and is 0 before ``init()``. Use it for
  anything you count in samples.
- ``getActiveSampleRate()`` is the rate of the device that is open right now.
  It is 0 while no device is open: before ``init()``, after ``pause()`` or
  ``close()``, and in an offline session.

Offline sessions
----------------

``System().renderOffline(blocks)`` runs the same work the audio thread does
(applying queued changes, rendering the channel tree, mixing, reverb) for
``blocks`` × 128 samples, on the thread that calls it. The rendered audio is
discarded. Call ``System().update()`` between renders as you would in a
device session: without it, new sounds never start.

.. code-block:: cpp

   YSE::System().requestSampleRate(48000);
   YSE::System().initOffline();
   YSE::System().setChannelConfiguration(YSE::CT_51, 6);   // optional

   // ... create sounds, channels, patchers ...

   for (int i = 0; i < 100; ++i) {
       YSE::System().update();
       YSE::System().renderOffline(1);
   }
   YSE::System().close();

**Speaker layout.** An offline session starts with the stereo layout. Change
it with ``System().setChannelConfiguration(conf, outputs)``, called *after*
``initOffline()`` (initialisation installs stereo itself and overwrites an
earlier call). The next rendered block switches to the new layout. ``outputs``
must be at least 1; a zero-output layout is refused with a log line.
``CT_CUSTOM`` creates ``outputs`` speakers whose positions you set yourself.
In a device session this call configures the mixer without asking the
device, so use ``openDevice()`` there instead.

**An offline session stays offline.** ``resume()`` restarts the device a
session already had. An offline session has none, so ``resume()`` does
nothing there, and ``autoReconnect()`` cannot open a device on it either.

The one way to give an offline session a device is ``openDevice()`` (see
below). When it succeeds, the session becomes a device session:
``pause()`` and ``resume()`` work on it, and an audio thread now renders the
mix. **Stop calling** ``renderOffline()`` **after that.** On desktop this
only works in a process where an earlier ``init()`` has already started
PortAudio. In a process that has only ever run offline sessions, the device
list is empty and ``openDevice()`` returns ``false`` (tracked as
`#972 <https://github.com/yvanvds/yse-soundengine/issues/972>`_).

Choosing an audio device
------------------------

``init()`` opens the default output device. To use a different one, list the
devices after ``init()`` and open one with ``openDevice()``:

.. code-block:: cpp

   YSE::System().init();

   const YSE::device* chosen = nullptr;
   for (unsigned int i = 0; i < YSE::System().getNumDevices(); ++i) {
       const YSE::device& d = YSE::System().getDevice(i);
       if (d.getNumOutputChannelNames() >= 6) { chosen = &d; break; }
   }

   if (chosen != nullptr) {
       YSE::deviceSetup setup;
       setup.setOutput(*chosen).setBufferSize(0);   // 0: let the backend choose
       if (!YSE::System().openDevice(setup, YSE::CT_AUTO)) {
           // Refused and logged. The previous device is still playing.
       }
   }

**Listing devices.**

- ``getNumDevices()`` and ``getDevice(i)`` work in every build.
  ``getDevices()`` returns the whole ``std::vector`` and is only for builds
  that link libYSE statically.
- ``getDevice(i)`` throws ``std::out_of_range`` for an index at or past
  ``getNumDevices()``. The list is empty in an offline session and on a
  machine with no audio hardware, so even index 0 can be out of range.
- The indexed getters on ``YSE::device`` (``getOutputChannelName``,
  ``getAvailableSampleRate``, ``getAvailableBufferSize`` and the input
  equivalent) throw ``std::out_of_range`` in the same way.
- ``getDefaultDevice()`` and ``getDefaultHost()`` name the platform default
  device and its host API (WASAPI, ALSA, ...).

**Special values on a device.**

- ``getID()`` is the backend's index for the device, or **-1** when no device
  has been assigned. -1 is what a ``YSE::device`` you construct yourself
  reports. Opening such a device is refused with a log line. Every device
  from ``getDevice()`` has a real index, 0 or higher.
- ``getDefaultBufferSize()`` is **0** when the host does not advertise one,
  which on desktop is every device. 0 means *unspecified*, not zero frames.
  Passed to ``deviceSetup::setBufferSize()`` it lets the backend pick the
  size. After the device is open, ``System().getActiveBufferSize()`` reports
  the size it chose.

**Opening the device.** ``openDevice(setup, conf)`` returns ``true`` when a
stream is running on the new device. It returns ``false``, logs the reason,
and leaves the running stream and the speaker layout alone when:

- the setup has no output device;
- the device ID does not exist on this system (for example -1);
- the backend could not open or start the stream;
- the backend was never started (see `Offline sessions`_).

On success the speaker layout follows the device that opened: ``conf`` is
applied with that device's number of output channels. ``CT_AUTO`` derives the
layout from the channel count. A layout with a different number of outputs
makes the next audio block reallocate the mix buffers, so switch devices at
setup, not while something audible plays.

The new stream runs at the session's sample rate. The rate set with
``deviceSetup::setSampleRate()`` is currently ignored (tracked as
`#971 <https://github.com/yvanvds/yse-soundengine/issues/971>`_); choose the
rate with ``requestSampleRate()`` before ``init()``.

On Android there is a single device ("Android Audio", stereo, through Oboe).
``openDevice()`` does not switch anything there. It only applies the speaker
layout.

**Live device state.** ``getActiveSampleRate()``, ``getActiveBufferSize()``
(frames per device callback, which is not the engine's 128-sample block) and
``getActiveOutputLatency()`` (in samples) describe the open device and return
0 while none is open. ``cpuLoad()`` is the audio callback's run time as a
fraction of the buffer it produces. Near 1.0, dropouts are likely.

Is audio flowing?
-----------------

``System().missedCallbacks()`` counts the ``update()`` calls in a row during
which the device delivered no audio. It goes back to 0 at the first
``update()`` that sees a callback, so **0 means audio is flowing** and any
other value means it is not.

A non-zero value does not always mean something is wrong. A stream that has
just started (by ``init()``, ``resume()`` or ``openDevice()``) takes some
time, typically tens of milliseconds, to deliver its first callback. To wait
for a device to come up, poll until the count is 0:

.. code-block:: cpp

   YSE::System().init();
   for (int i = 0; i < 100 && YSE::System().missedCallbacks() != 0; ++i) {
       YSE::System().update();
       YSE::System().sleep(10);
   }

A value that keeps climbing means the device has gone away or the audio
thread is starved.

**Reconnecting automatically.** ``System().autoReconnect(on, delay)`` makes
``update()`` restart the output stream when the device has delivered nothing
for ``delay`` **milliseconds**. The restart picks up whatever the default
device is at that moment, so it covers cases like unplugged headphones. While
the device stays unavailable, the engine tries again every ``delay``
milliseconds. A negative delay counts as 0.

- A stream that has just started and has not delivered its first callback
  yet gets at least **half a second** before the watchdog acts, whatever the
  delay. A small delay therefore cannot tear down a device that is still
  starting.
- Up to and including v2.4.0, ``delay`` counted ``update()`` calls, not
  milliseconds. If you passed a number of frames, convert it: at 60 frames per
  second, 120 frames is 2000 ms.
- The watchdog runs inside ``update()``. If you stop calling ``update()``,
  nothing reconnects.

Each ``init()`` turns ``autoReconnect`` off again, so enable it after
``init()``.

``System().pause()`` closes the stream and ``resume()`` opens it again.
Changes you make while paused are queued and applied, in order, on
``resume()``.

MIDI devices
------------

MIDI device I/O exists only in builds with ``YSE_ENABLE_MIDI_DEVICE`` (on by
default on Windows and Linux, off on Android). Without it, the functions below
are not declared, so guard your code with ``#if YSE_ENABLE_MIDI_DEVICE``.

- ``System().getNumMidiInDevices()`` and ``getNumMidiOutDevices()`` count the
  ports. They return 0 when the MIDI backend could not start.
- ``getMidiInDeviceName(id)`` and ``getMidiOutDeviceName(id)`` return an
  **empty string** for an ID at or past the count, and for every ID when the
  MIDI backend is down. Do not treat an empty name as a device.
- Open a port by index with ``YSE::midiIn::create(port)`` or
  ``YSE::midiOut::create(port)``. ``midiIn`` callbacks run on RtMidi's input
  thread, not on your control thread (see :doc:`/intro/threading`).

Where to go next
----------------

- :doc:`/intro/threading`: the threads, what ``update()`` drives, and which
  calls belong on the control thread.
- :doc:`/api/core` and :doc:`/api/devices`: the reference for ``system``,
  ``device`` and ``deviceSetup``.
- :doc:`/api/midi`: ``midiIn`` and ``midiOut``.
