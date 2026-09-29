Upgrading from 2.4 to 3.0
=========================

libYSE 3.0 is a major release. Most of the C++ API is source compatible
with 2.4, but the C ABI changed, the patcher's bus addresses moved, and a
number of engine and patcher behaviours changed without any change to a
signature. That last group is the one to read carefully: code that
compiles cleanly against 3.0 can still behave differently.

This page lists every change that can break or alter a program written
against 2.4.0, grouped by the layer you talk to. Each entry names the
issue that made the change, so you can read the full reasoning on GitHub.
New features that change nothing existing are not listed here; see the
`release notes <https://github.com/yvanvds/yse-soundengine/releases>`_ for
those.

Quick checklist
---------------

- **Everyone:** the default sample rate is now 48 kHz, ``System().init()``
  returns ``false`` when no audio device opens, and the ``autoReconnect``
  delay is in milliseconds.
- **C / FFI hosts:** rebuild your bindings. Four functions now return
  ``YseStatus``, one getter changed its signature, two functions and three
  callback typedefs were renamed, and several "nothing here" answers moved
  from ``0`` to a sentinel. See `C API`_.
- **Patcher hosts:** anything that publishes to or subscribes on a patcher
  slot from outside must add the ``patcher.`` prefix. See
  `Bus and store addresses`_.
- **Patches:** re-check anything that uses ``.route``, ``.metro``,
  ``.counter``, ``~saw``, several ``~dac`` objects or message boxes. See
  `Patcher objects`_.

Engine and audio I/O
--------------------

These apply to C++ and C hosts alike. The C function is named where it
differs from the C++ one.

Default sample rate is 48 kHz (#646)
   The engine's initial rate is now 48000 Hz instead of 44100, on desktop
   and Android. A device still negotiates its own rate, and
   ``System().getSampleRate()`` reports what was agreed. To ask for a rate,
   call ``System().requestSampleRate(rate)``
   (``yse_system_request_sample_rate``) before ``init()`` or
   ``initOffline()``. An offline session runs exactly at the requested
   rate, or at 48 kHz when none is set, so offline renders that assumed
   44.1 kHz change length. See :doc:`/intro/sessions_and_devices`.

``init()`` fails when no device opens (#973)
   ``System().init()`` used to return ``true`` even when there was no
   default output device, or the device refused to open or start. It now
   returns ``false``, leaves no session behind and restores the previous
   sample rate. ``yse_system_init`` reports ``YSE_ERR_AUDIO_DEVICE``.
   Headless tools and CI should call ``initOffline()``, or fall back to it
   when ``init()`` fails.

``deviceSetup::setSampleRate()`` is honoured (#971, #972)
   ``openDevice()`` used to ignore the rate stored on a ``deviceSetup``.
   With no session rate locked, the requested rate now takes precedence
   over the device default. On a running session a rate that differs from
   the session rate is refused with a warning, and the stream opens at the
   session rate. ``System().openDevice()`` now returns ``bool`` instead of
   ``void``.

``autoReconnect`` delay is in milliseconds (#681)
   The ``delay`` argument of ``System().autoReconnect(on, delay)`` was
   counted in ``update()`` calls, despite being documented (and named
   ``delay_ms`` in C) as milliseconds. It is now milliseconds. A host that
   passed a tick count now waits a shorter wall-clock time: at 60
   ``update()`` calls per second, ``20`` used to mean about 333 ms and now
   means 20 ms. Negative values are clamped to 0. A stream that has just
   started gets a 500 ms grace period before the watchdog acts on it.
   ``missedCallbacks()`` still counts that start-up window.

A default ``device`` has ID ``-1`` (#666)
   A hand-built ``device`` descriptor used to have ID 0, which is a real
   PortAudio device index, so opening one silently opened the first
   enumerated device. ``device::getID()`` now defaults to ``-1``, and
   ``openDevice()`` refuses it with a log line and leaves the running
   stream alone. ``yse_device_get_id(NULL)`` returns ``-1`` rather than
   ``0``. Descriptors from ``System().getDevices()`` still carry their
   real index.

Out-of-range device lookups (#581)
   ``System().getDevice(nr)`` throws ``std::out_of_range`` for an index
   past the end of the list instead of reading past it. An offline session
   enumerates no devices, so index 0 is already out of range there.
   ``yse_system_get_device`` returns ``NULL`` with the reason in
   ``yse_last_error()``.

MIDI device names without a backend (#585)
   When the MIDI backend is not running (for example a headless Linux
   machine with no ALSA sequencer), the MIDI device-name getters return an
   empty string instead of ``"Invalid Call"``.

Render threads (#650, #859–#862)
   Rendering now runs on a task-graph scheduler. The number of render
   workers is public: ``System().renderThreads(count)``
   (``yse_system_set_render_threads``). ``-1`` (the default) means physical
   cores minus one, capped at 8; ``0`` renders everything on the audio
   thread; ``n`` starts exactly ``n`` workers. The interim cap of two
   automatic workers from #650 has been replaced by this policy. Set the
   count before ``init()`` if you need a fixed number of threads, for
   example on constrained hardware. The mix is bit-identical at every
   worker count.

``channel::setVirtual(false)`` works (#864)
   ``setVirtual(false)`` used to report ``false`` while the channel kept
   virtualising its sounds, so sounds past ``maxSounds`` never rendered.
   It now turns virtualisation off. If you called it and relied on the old
   (broken) behaviour to cap your voice count, remove the call.

Un-created and failed sounds (#579, #583)
   Every method on a ``YSE::sound`` that has not been created, or whose
   ``create()`` failed, is now a no-op, and its queries answer
   ``false`` / ``0``. In 2.4 they crashed. ``create()`` now also stores
   its ``loop`` and ``volume`` arguments on the interface, so
   ``looping()`` and ``volume()`` report them straight away instead of
   ``false`` and ``0``. Code that read ``volume() == 0`` after
   ``create()`` as a sign of a fresh sound needs changing.

Clocks and clips can be destroyed in any order (#707, #974, #975)
   Destroying a domain clock while a clip or a patcher is still bound to
   it no longer frees memory under them: the clock stops advancing and
   bound holders read a frozen beat. A clip that is still alive survives
   ``System().close()`` and idles until you bind it again. Destroying a
   clip releases the notes it was playing, on its synths and on MIDI out.
   You no longer need to destroy clips before ``close()``, or stop a clip
   and wait a block before destroying it. See
   :doc:`/tutorials/15_clocks_and_clips`.

Clocks advance per rendered block (#944)
   Domain clocks and clip transports used to advance once per device
   callback, so with a device buffer larger than one engine block, beat
   events could fire up to one buffer early. They now advance per rendered
   block, as ``renderOffline()`` always did. Beat-timed events land later
   and more precisely than before on large device buffers.

Random numbers (#410, #908)
   The ``rand()`` / ``srand()`` based helpers in ``utils/misc.hpp`` were
   replaced by a thread-safe generator that is safe on the audio thread.
   Signatures are unchanged, but:

   - ``RandomF()`` and its range overloads return values in ``[0, 1)``
     (half-open) instead of ``[0, 1]``.
   - ``Random(max)`` with ``max <= 0`` returns 0, and ``Random(min, max)``
     with ``max <= min`` returns ``min``. Both used to divide by zero.
   - Calling ``srand()`` no longer affects the engine. Use
     ``YSE::Randomize()`` for a time-based seed, or the new
     ``YSE::RandomSeed(seed)`` for a fixed one. From C (or from any host
     that loads libYSE as a shared library) call ``yse_randomize()`` /
     ``yse_random_seed()``, so the seed reaches the copy the engine draws
     from. Without either call the sequence starts from a fixed default
     seed.

Engine time uses a wall clock (#667)
   The engine's update tick was measured with ``std::clock()``, which is
   processor time and ran fast under load. It now uses a monotonic wall
   clock. Doppler velocities are therefore correct instead of scaling with
   the number of busy threads, so doppler can sound different, most
   noticeably while the engine is busy.

``midiOut::Raw()`` sends what you give it (#748)
   ``midiOut::Raw(const std::string&)`` always sent exactly three bytes,
   padding shorter messages with zeros and truncating longer ones. It now
   sends the whole string. A new
   ``Raw(const unsigned char* data, std::size_t length)`` overload sends a
   byte span, and ``yse_midi_out_raw`` wraps it for C. Use these for
   two-byte messages (program change, channel pressure) and SysEx.

Enum count sentinels (#912)
   The engine enums mirrored in the C API gained a trailing ``*_COUNT_``
   value (for example ``CT_COUNT_``, ``OUT_TYPE_COUNT_``). It is not a
   valid value. A ``switch`` over one of these enums without a ``default``
   now draws a ``-Wswitch`` warning until it handles the sentinel.

C API
-----

The C ABI is not compatible with 2.4. Regenerate your bindings from the
3.0 headers. See :doc:`/api/c_api` for the conventions these changes
follow.

Renamed (#911)
   .. list-table::
      :header-rows: 1
      :widths: 50 50

      * - 2.4
        - 3.0
      * - ``yse_run_script``
        - ``yse_python_run_script``
      * - ``yse_set_script_error_callback``
        - ``yse_python_set_script_error_callback``
      * - ``yse_script_error_cb``
        - ``YseScriptErrorCallback``
      * - ``yse_bus_tap_cb``
        - ``YseBusTapCallback``
      * - ``yse_bus_sub_cb``
        - ``YseBusSubCallback``

   ``yse_free_string`` moved from ``yse_patcher.h`` to ``yse_common.h``.
   The symbol is the same, and ``yse_patcher.h`` still includes it through
   ``yse_common.h``.

Return type changed to ``YseStatus`` (#910)
   .. list-table::
      :header-rows: 1
      :widths: 34 20 46

      * - Function
        - 2.4 returned
        - 3.0 notes
      * - ``yse_patcher_init``
        - ``void``
        - A negative ``main_outputs`` is refused with
          ``YSE_ERR_INVALID_ARGUMENT``.
      * - ``yse_patcher_parse_json``
        - ``void``
        - Malformed JSON returns ``YSE_ERR_EXCEPTION``; parsing before
          ``yse_patcher_init`` returns ``YSE_ERR_NOT_INITIALIZED``.
      * - ``yse_clip_bind``
        - ``int`` (1 / 0)
        - ``YSE_OK`` on success. Note that success used to be ``1`` and is
          now ``0``.
      * - ``yse_system_create_clock``
        - ``int`` (1 / 0)
        - ``YSE_OK`` on success, as above.

   Every call that returns ``YSE_ERR_INVALID_ARGUMENT`` now also sets a
   message for ``yse_last_error()``.

``yse_channel_get_name`` copies into your buffer (#905)
   It used to return a ``const char*`` into engine-owned storage. It is now
   ``size_t yse_channel_get_name(YseChannel* ch, char* buf, size_t cap)``,
   following the same snprintf-style contract as the other string getters.

New "nothing here" sentinels
   Several getters used ``0`` for "no answer", where ``0`` is also a real
   answer:

   - ``yse_phandle_get_id`` and ``yse_phandle_get_connection_target``
     return ``YSE_PATCHER_ID_NONE`` (``0xFFFFFFFF``) for a ``NULL`` handle
     or an absent connection (#732). Object IDs now start at 0 in every
     patcher (see `Patch files`_), so the first object is ID 0.
   - ``yse_phandle_get_connection_target_inlet`` returns
     ``YSE_PATCHER_INLET_NONE`` (``0xFFFFFFFF``) instead of inlet 0 for a
     ``NULL`` handle, an absent outlet or an absent connection (#736). The
     C++ ``pHandle::GetConnectionTargetInlet`` returns ``UINT_MAX`` in the
     same cases.
   - ``yse_device_get_id(NULL)`` returns ``-1`` (#666).

   A binding that stores these in a signed 32-bit integer sees ``-1``.

Other contract changes
   - ``yse_system_get_device`` returns ``NULL`` for an out-of-range index
     (#581).
   - C++ exceptions no longer escape any ``extern "C"`` entry point; they
     are reported through ``yse_last_error()`` and the function's
     documented failure value (#901, #953).
   - ``yse_synth_set_note_callback`` always calls your callback through
     its own ``int`` signature. Up to 256 distinct callbacks can be
     installed across the process; past that the call fails with a
     ``yse_last_error()`` message and leaves the previous callback in
     place (#899).

Patcher
-------

Bus and store addresses
~~~~~~~~~~~~~~~~~~~~~~~

Every patcher-scoped address now carries the reserved ``patcher.`` prefix
(#894), next to the existing ``sound.``, ``channel.`` and ``synth.``
namespaces. This covers ``.s`` / ``.r``, ``.forward``, the ``send``
messages of ``.bag`` and ``.table``, and the shared stores (``.value``,
``.coll``, ``.table``, ``.dict``, ``.array``).

A host that subscribes to or publishes on ``<patcherName>.<slot>`` must
switch to ``patcher.<patcherName>.<slot>``. Patchers with the same name
still share their slots, and an unnamed patcher still gets its own scope
through its ``patcher_<N>`` auto-name. A ``.s`` / ``.r`` name that would
make the full address longer than the bus allows is now refused
(#921, #922). See :doc:`/patcher/host_io`.

A ``.s`` value used to reach a ``.r`` in the same patcher twice once the
engine was running: once directly and once through the bus. It now arrives
once (#943).

Patch files
~~~~~~~~~~~

Object IDs are per patcher (#730, #733)
   The IDs written by ``DumpJSON`` used to come from a process-wide
   counter, so they depended on how many objects the process had created
   before. Each patcher now numbers its objects from 0, reusing the
   smallest free number, and saves them in ID order, so two identical
   patches save identically. Patches saved by 2.4 load unchanged and are
   renumbered on their next save. If you store object IDs outside the
   patch file, look them up again after loading.

The patcher name is saved (#897)
   ``DumpJSON`` writes a name you set with ``SetName`` as a top-level
   ``"name"`` key. Loading applies it to a patcher that still has its
   auto-name; a name the host already set wins. Unnamed patches save
   exactly as before.

``ParseJSON`` adds, and rolls back on failure (#938, #939)
   ``ParseJSON`` has always added the file's objects to whatever the
   patcher already holds; the old documentation said it replaced them.
   Call ``Clear()`` (``yse_patcher_clear``) first to replace. A load that
   fails part-way now removes the objects it had created instead of
   leaving them unwired in the patcher. See :doc:`/patcher/file_format`.

Parameter strings (#935, #936)
   Creation arguments are split on runs of spaces, tabs and line breaks;
   two spaces in a row no longer produce an empty argument. A live
   ``SetParams`` now resets the parameters it leaves out to their
   defaults, as rebuilding the object always did, so ``SetParams("")``
   resets all of them.

Patcher objects
~~~~~~~~~~~~~~~

``.route`` (#672, #679)
   ``.route`` now strips the matched selector and sends the rest, as in
   Max, and matches floats by value. If a patch relied on the selector
   arriving with the message, replace ``.route`` with ``.routepass``,
   which behaves as ``.route`` did in 2.4.

   .. list-table::
      :header-rows: 1
      :widths: 40 30 30

      * - Input to ``.route foo`` / ``.route 5``
        - 2.4 sent
        - 3.0 sends
      * - ``foo 1 2``
        - the list ``foo 1 2``
        - the list ``1 2``
      * - ``foo 1``
        - the list ``foo 1``
        - the int ``1``
      * - ``foo``
        - the list ``foo``
        - a bang
      * - the int ``5`` into ``.route 5``
        - the int ``5``
        - a bang
      * - the float ``5.0`` into ``.route 5``
        - no match
        - a bang

   Unmatched messages still leave the rightmost outlet unchanged. A
   ``.route`` with no arguments now has Max's ``0`` outlet plus the
   fall-through outlet, instead of no outlets at all.

``.metro`` (#625, #711)
   A period change on the cold inlet or through its parameter now takes
   effect while the metro runs, keeping its phase; in 2.4 it only applied
   after a stop and start. The left inlet also accepts ``bang`` (start, or
   restart in phase from now), ``stop``, and a float (non-zero starts).

``.counter`` (#956)
   The start value argument now seeds the count, so ``.counter -1`` sends
   0 on its first bang. Inlet 1 sets the step, as documented, instead of
   the count. An int on inlet 0 sets the count but no longer changes the
   value ``reset`` returns to.

``~saw`` (#955)
   ``~saw`` outputs a bipolar saw from -1 to 1, as documented. In 2.4 it
   sent a 0 to 1 ramp, which carried a DC offset. ``DSP::saw`` in C++ is
   still a 0 to 1 phasor.

Several ``~dac`` objects (#932)
   With more than one ``~dac`` in a patcher, only the last one used to be
   heard, divided by the number of ``~dac`` objects. They are now added
   together, as in Max and Pd. Lower the gain if a patch that used several
   ``~dac`` objects is now louder.

Message boxes (#933)
   ``.m`` sends its text as the message it spells: ``.m 60`` sends the int
   60 and ``.m bang`` sends a bang, instead of text that most objects
   ignored. A ``.m`` wired into ``.mtof``, ``.i`` and similar objects now
   drives them. See :doc:`/patcher/messages`.

``.midiout`` byte lists (#748)
   ``.midiout`` accepts a numeric byte list such as ``144 60 100``, so it
   composes with ``.midiformat`` and ``.sxformat``. A list with a value
   outside 0–255 is refused whole.

``~clip`` and ``~out`` (#436)
   ``~clip`` is now registered and can be created. The ``OBJ::D_OUT``
   constant (``"~out"``) was removed: no object ever implemented it.

Signal through subpatchers (#764)
   ``~inlet`` and ``~outlet`` carry audio through a subpatcher boundary.
   They share one pin numbering with ``.inlet`` / ``.outlet``, so
   ``SubpatcherInlets`` / ``SubpatcherOutlets`` count both rates. See
   :doc:`/patcher/subpatchers`.

Where to go next
----------------

- :doc:`/intro/sessions_and_devices` covers sample rates, devices and the
  offline session in full.
- :doc:`/patcher/index` describes the patcher, which grew from 42 to over
  300 objects in this release.
- :doc:`/api/c_api` is the C API guide and reference.
