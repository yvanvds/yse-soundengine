C ABI (language bindings)
=========================

libYSE ships a flat ``extern "C"`` API alongside the C++ one, folded into the
same shared library by the ``YSE_BUILD_C_API=ON`` CMake option (default on).
It exists so language bindings — Dart FFI, Python ctypes, etc. — can consume
``libyse.dll`` / ``libyse.so`` without C++ ABI compatibility.

Single entry point:

.. code-block:: c

   #include "yse_c/yse_all.h"

This umbrella header pulls in every subsystem header below. The headers
repeat forward declarations of the handle types, so compile C code against
them as C11 or later (or as C++).

The first half of this page is a guide: the rules every function follows, in
one place, so a binding can be written against them. The second half is the
reference, generated from the headers. The patcher has its own guide,
:doc:`/patcher/c_api`.

.. _c-api-guide:

Guide
-----

A first session
~~~~~~~~~~~~~~~

.. code-block:: c

   #include <stdio.h>
   #include "yse_c/yse_all.h"

   int main(void) {
     YseSystem* sys = yse_system_get();           /* borrowed: never destroyed */

     yse_system_request_sample_rate(sys, 48000);  /* before init; 0 clears it */
     if (yse_system_init(sys) != YSE_OK) {
       fprintf(stderr, "init: %s\n", yse_last_error());
       return 1;
     }
     if (yse_system_get_active_sample_rate(sys) == 0.0) {
       fprintf(stderr, "no audio device is open\n");
     }

     YseSound* snd = yse_sound_create();          /* owned: a handle, not yet a sound */
     YseStatus st = yse_sound_load_file(snd, "drums.ogg", yse_channel_master(), 1, 0.8f, 0);
     if (st != YSE_OK) {
       /* snd is still a live handle: its calls are no-ops until a load works */
       fprintf(stderr, "load (%d): %s\n", (int)st, yse_last_error());
     } else {
       yse_sound_play(snd);
     }

     for (int frame = 0; frame < 600; ++frame) {
       yse_system_update(sys);                    /* same thread as init, once per frame */
       yse_system_sleep(sys, 16);
     }

     yse_sound_destroy(snd);
     yse_system_close(sys);
     return 0;
   }

``yse_system_init()`` can return ``YSE_OK`` although no audio device opened
(`#973 <https://github.com/yvanvds/yse-soundengine/issues/973>`_), so check
``yse_system_get_active_sample_rate()`` as above. :doc:`/intro/sessions_and_devices`
covers sessions, sample rates and devices, and :doc:`/intro/threading`
explains what ``yse_system_update()`` does on your thread.

Handles and ownership
~~~~~~~~~~~~~~~~~~~~~

Every object crosses the ABI as a pointer to an opaque struct (``YseSound*``,
``YseChannel*``, …). The comment on each typedef says which of these kinds
it is:

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Kind
     - Rule
   * - **Owned**
     - You created it with a ``yse_<type>_create*`` function, and you release
       it with the matching ``yse_<type>_destroy``. The library never frees
       it for you. Every destroy function accepts ``NULL``.
   * - **Borrowed**
     - The engine owns it: the singletons (``yse_system_get``,
       ``yse_listener_get``, ``yse_log_get``), the pre-built channels
       (``yse_channel_master`` and its siblings), the global reverb, and the
       device descriptors from ``yse_system_get_device``. Never destroy one.
   * - **Owned by a parent**
     - A ``YsePHandle`` belongs to its ``YsePatcher``. Remove it with
       ``yse_patcher_delete_object``; it goes away with the patcher.
   * - **Reference-counted asset**
     - ``YseSfzInstrument`` and ``YseDx7Bank``. A synth keeps its own share,
       so you may destroy the handle right after handing it over, and before
       or after ``yse_system_close()``. A double destroy, or a call on a
       destroyed handle, is logged and ignored instead of crashing. No other
       handle type has this protection.

**Destroy a handle with its own type's function.** A ``YseSound*`` goes to
``yse_sound_destroy``, a ``YseDspObject*`` to ``yse_dsp_object_destroy``,
and so on. The handles are distinct C types, so passing one to another type's
destroy needs a cast and is undefined behaviour. Two handle types cover a
family, and each family has one destroy for all its members:

- ``yse_dsp_buffer_destroy`` releases every ``YseDspBuffer``, whichever of the
  four constructors made it. The handle remembers its constructor, so destroy
  frees the right object
  (`#662 <https://github.com/yvanvds/yse-soundengine/issues/662>`_).
- ``yse_dsp_object_destroy`` releases every effect, including a patcher
  insert. Destroying an insert never destroys the patcher it wraps.

**Lifetimes that you enforce.** Several calls keep a pointer to an object you
own, and the library does not track it. The object must outlive its user:

- A ``YseDspBuffer``, ``YseDspMultiBuffer`` or ``YsePatcher`` loaded into a
  sound must outlive the sound. Destroy the sound first.
- A synth attached to a sound, or connected to a ``YseMidiIn`` or MIDI file,
  must outlive that connection.
- An effect set with ``yse_sound_set_dsp`` or ``yse_channel_set_dsp`` is
  borrowed by the sound or channel: the audio thread reads it. Detach it
  (``set_dsp(..., NULL)``) or destroy the owner before destroying the effect.
- A patcher insert borrows its patcher.

``yse_system_close()`` frees the engine's side of every sound and channel,
but never your handles. Bus taps and subscriptions stop firing at close and
do not come back after a new init; release them and create new ones.

Handles and loaded objects
~~~~~~~~~~~~~~~~~~~~~~~~~~

A handle that is not ``NULL`` is not necessarily usable yet. A ``YseSound*``
from ``yse_sound_create`` is an empty shell until a ``yse_sound_load_*``
call succeeds. Until then, and after a failed load, its setters and transport
calls do nothing and its queries return 0
(`#579 <https://github.com/yvanvds/yse-soundengine/issues/579>`_). They never
crash. ``yse_sound_is_valid()`` returns 1 only once a load has succeeded, so
it tells the two states apart. Channels, synths, reverbs and SFZ instruments
have an ``_is_valid`` function too; it returns 1 only when a live engine
object stands behind the handle.

The general rule for a ``NULL`` handle is the same:

- A ``void`` function (a setter, ``play``, ``stop``) does nothing.
- A query returns 0, false or ``NULL``. Where 0 is itself a real answer the
  header names a sentinel instead, for example ``YSE_PATCHER_ID_NONE``.
- A function that returns ``YseStatus`` returns ``YSE_ERR_INVALID_HANDLE``.

A dangling handle (one you already destroyed) is not detected, except for
the reference-counted assets above. Clear your own pointer when you destroy.

The DSP subclass contract
~~~~~~~~~~~~~~~~~~~~~~~~~

``YseDspBuffer`` stands for four engine classes that form one chain:
``buffer`` ← ``drawableBuffer`` ← ``fileBuffer`` ← ``wavetable``. The chain
has no virtual functions, so the library cannot check at run time which one
a handle is. Functions for a subclass (``yse_dsp_buffer_draw_line``,
``yse_dsp_buffer_load_file``, ``yse_dsp_wavetable_create_saw``, …)
``static_cast`` the handle and trust you
(`#582 <https://github.com/yvanvds/yse-soundengine/issues/582>`_):

- A handle works with the functions of its own class and of every class
  above it. A wavetable handle may be drawn on and filled; a file buffer
  handle may be drawn on.
- Passing a handle to a function of a class *below* it (a plain
  ``yse_dsp_buffer_create`` handle to ``yse_dsp_wavetable_create_saw``) is
  undefined behaviour. The call returns ``YSE_OK`` and writes through the
  wrong type.

.. code-block:: c

   YseDspBuffer* wt = yse_dsp_wavetable_create(2048);
   yse_dsp_wavetable_create_saw(wt, 32, 2048);  /* wavetable entry point: fine */
   yse_dsp_buffer_draw_flat(wt, 0, 16, 0.0f);   /* drawableBuffer is above it: fine */
   yse_dsp_buffer_fill(wt, 0.0f);               /* buffer is above it: fine */
   yse_dsp_buffer_destroy(wt);                  /* one destroy for all four */

   YseDspBuffer* plain = yse_dsp_buffer_create(2048, 0);
   /* yse_dsp_wavetable_create_saw(plain, 32, 2048);   undefined behaviour */
   yse_dsp_buffer_destroy(plain);

A binding enforces this by remembering which constructor made each handle,
for example with one wrapper class per subclass. The effect handles in
``yse_dsp_modules.h`` follow the same contract: ``yse_dsp_object_*``
functions accept any effect, and a module's own setters (such as
``yse_dsp_lowpass_set_frequency``) need a handle from that module's
constructor.

Errors
~~~~~~

Functions that can fail in a way you need to know about return
``YseStatus``. Loaders and constructors that return a handle return ``NULL``
on failure instead.

.. list-table::
   :header-rows: 1
   :widths: 36 64

   * - Status
     - Meaning
   * - ``YSE_OK`` (0)
     - Success.
   * - ``YSE_ERR_INVALID_HANDLE``
     - The handle argument was ``NULL``. That is all it means: it is never a
       type check, and a handle of the wrong subclass is not reported (see
       above). One call adds a case, and says so:
       ``yse_synth_fm_set_patch`` also returns it for a synth that has no FM
       voice group.
   * - ``YSE_ERR_INVALID_ARGUMENT``
     - A ``NULL`` or empty string, an out-of-range index, a duplicate name,
       lengths that do not match.
   * - ``YSE_ERR_NOT_INITIALIZED``
     - The call needs a running engine, for example a bus publish before
       ``yse_system_init``.
   * - ``YSE_ERR_FILE_NOT_FOUND``
     - A sound file could not be loaded.
   * - ``YSE_ERR_AUDIO_DEVICE``
     - ``yse_system_open_device`` could not open the setup you gave it. The
       running stream is left alone.
   * - ``YSE_ERR_EXCEPTION``
     - The engine threw a C++ exception. The library catches it at the ABI
       boundary and reports it as this status.
   * - ``YSE_ERR_GENERIC``
     - Any other failure.

**The last error.** ``yse_last_error()`` returns a message about the most
recent failure:

- Every error status except ``YSE_ERR_INVALID_HANDLE`` comes with a
  message, and so does a constructor or loader that fails and returns
  ``NULL``. A ``NULL`` handle may return ``YSE_ERR_INVALID_HANDLE`` without
  one. Test the status first, and read the message second.
- A ``void`` function that catches an exception also records the message,
  though it has no status to return.
- The message is kept per thread. A failure on another thread never
  overwrites yours.
- A successful call does **not** clear it. The message stays until the next
  failure on the same thread, or until you call ``yse_clear_last_error()``.
  Reading ``yse_last_error()`` after a call that returned ``YSE_OK`` can show
  an old message.
- The returned pointer belongs to the library and is valid until the next
  ``yse_*`` call on the same thread. Copy the text if you keep it.

Strings
~~~~~~~

Strings are NUL-terminated UTF-8 in both directions. Strings you pass in are
copied before the call returns unless the header says otherwise. Strings come
back in one of three ways:

- **Into your buffer, snprintf-style.** Functions shaped
  ``size_t f(..., char* buf, size_t cap)`` write at most ``cap - 1`` bytes and
  a NUL, and return the full length. Pass ``buf = NULL, cap = 0`` to ask for
  the size first. A return value of ``cap`` or more means the text was cut
  short. On a ``NULL`` handle they write an empty string and return 0.
- **Owned by the library.** ``yse_version()``, ``yse_last_error()`` and the
  patcher metadata getters return ``const char*`` that you never free.
- **Owned by you.** ``yse_patcher_get_metadata_json()`` returns a ``char*``
  that you release with ``yse_free_string()``. Callbacks that pass you a
  string you own say so, and name the function that frees it (see
  `Callbacks`_).

Bound-checked getters
~~~~~~~~~~~~~~~~~~~~~

Getters that take an index check it against the matching count, so a count
that changed since you read it cannot make them read out of bounds
(`#565 <https://github.com/yvanvds/yse-soundengine/issues/565>`_,
`#581 <https://github.com/yvanvds/yse-soundengine/issues/581>`_):

- ``yse_system_get_device(sys, i)`` returns ``NULL`` and sets the last error
  when ``i`` is not below ``yse_system_num_devices()``. An offline session
  has no devices at all.
- The device descriptor getters (channel names, sample rates, buffer sizes)
  return an empty string or 0 for an index out of range, the same as for a
  ``NULL`` descriptor.
- The MIDI device name getters return an empty string for an ``id`` with no
  device, so a name that is not empty always means a real port.

A ``NULL`` descriptor is accepted too, so a loop over devices needs no extra
checks:

.. code-block:: c

   unsigned int n = yse_system_num_devices(sys);
   for (unsigned int i = 0; i < n; ++i) {
     YseDevice* dev = yse_system_get_device(sys, i);  /* borrowed; NULL if i is stale */
     char name[128];
     size_t len = yse_device_get_name(dev, name, sizeof name);
     printf("%u: %s%s\n", i, name, len >= sizeof name ? " (truncated)" : "");
   }

Threading
~~~~~~~~~

The C API follows the engine's threading model, described in
:doc:`/intro/threading`. In short:

- Call ``yse_system_init``, ``yse_system_update`` and ``yse_system_close``
  from one thread, the **control thread**, and make the object calls from
  there too. The functions marked "control thread" in the headers (clocks,
  clips, bus subscriptions, ``yse_midi_in_connect_synth``,
  ``yse_patcher_set_name``, …) must be called from it.
- The patcher is the exception: several of your threads may drive one
  patcher at once. See :doc:`/patcher/realtime`.
- Never call the library from a real-time audio callback of your own.
- ``yse_last_error()`` is per thread, so read it on the thread that made the
  failed call.

Callbacks
~~~~~~~~~

Callback typedefs carry ``YSE_C_CALLBACK``, which fixes the calling
convention (``__cdecl`` on Windows, nothing elsewhere). Declare your
functions the same way or use the typedef. Except for the synth note hook,
every callback comes with a ``void* user_data`` that the library passes back
untouched. Installing a new callback replaces the old callback and
``user_data`` together: no call ever receives one install's callback with
another's ``user_data``. A call already in progress on another thread may
still finish with the old pair, so keep the old ``user_data`` alive until it
has.

Each callback runs on a known thread:

.. list-table::
   :header-rows: 1
   :widths: 30 32 38

   * - Callback
     - Thread
     - Rules
   * - ``YseLogCallback`` (``yse_log_set_callback``)
     - Whichever engine thread logged: the control thread, the background
       pool, a MIDI thread, a render worker while it starts. Never the audio
       thread.
     - You own ``msg``; release it with ``yse_log_free_message``. Calls are
       one at a time, under the engine's log lock, so they never overlap.
       Do not call the library from inside it (see below). Do not block.
   * - ``YseOcclusionCallback`` (``yse_system_set_occlusion_callback``)
     - The thread that calls ``yse_system_update()``.
     - May lock, allocate and query a physics engine. The positions are
       copies, valid for the call only.
   * - ``YseBusTapCallback``, ``YseBusSubCallback``
     - The control thread: inline for a publish made on it, otherwise during
       ``yse_system_update()``.
     - Every pointer is valid for the call only. Copy what you keep. Create
       and release taps and subscriptions on the control thread; then none
       fires after the release returns.
   * - ``YseScriptErrorCallback``
     - The thread that calls ``yse_system_update()``. On a build without
       Python, inside ``yse_python_run_script``.
     - The traceback is valid for the call only.
   * - ``YsePatcherSendCallback``
     - The thread that sent the message: yours for a ``yse_patcher_pass_*``
       call, the patcher timer thread for a ``.metro``. Never the audio
       thread.
     - You own ``address`` (and ``s`` with it); release it with
       ``yse_patcher_free_message(address)``. ``yse_patcher_destroy`` waits
       for calls in progress, so do not destroy the patcher from inside its
       own callback.
   * - ``YseMidiInRawCallback``, ``YseMidiInParsedCallback``
     - RtMidi's input thread for that port.
     - Return quickly. You own the raw callback's ``bytes``; release them with
       ``yse_midi_in_free_message``. The parsed callback transfers nothing.
       Do not call ``yse_midi_in_connect_synth`` from inside either.
   * - ``YseSynthNoteCallback`` (``yse_synth_set_note_callback``)
     - The thread that renders the synth: the audio thread or a render
       worker.
     - Real-time rules: no allocation, no locks, no I/O, no library calls.
       There is no ``user_data``; reach your state through globals.

**The log callback must not call back into the library.** The engine holds
its log lock while your callback runs. A call that logs, or one that takes
the same lock (``yse_log_send_message``, ``yse_log_set_callback``,
``yse_log_set_logfile``), waits for that lock on the thread that already
holds it, and the program hangs. Most ``yse_*`` functions can log, so the safe
rule is: copy or queue the message, free it, and return.

.. code-block:: c

   static void on_log(char* msg, void* user_data) {
     FILE* out = (FILE*)user_data;
     fprintf(out, "%s\n", msg);    /* no yse_* call in here except the free below */
     yse_log_free_message(msg);    /* the callback owns msg */
   }

   yse_log_set_callback(yse_log_get(), on_log, stderr);

Language bindings that marshal callbacks asynchronously (Dart's
``NativeCallable.listener``, for one) are the reason several callbacks hand
you an allocated copy: the string is still valid when your handler runs
later, and you free it there.

MIDI channel numbers
~~~~~~~~~~~~~~~~~~~~

The API counts MIDI channels in two ways. ``yse_midi_out_*`` and the parsed
MIDI input callback use the wire value, 0 to 15. The synth calls, clip events
and the ``channel_filter`` of ``yse_midi_in_connect_synth`` use 1 to 16, and
0 there means every channel. MIDI channel 1 is therefore 0 on a
``yse_midi_out_*`` call and 1 on a synth or clip call.

Changes since v2.4.0
~~~~~~~~~~~~~~~~~~~~

The C ABI breaks once, with v3.0. Update a v2.4.0 binding for these changes.

**Signatures that changed:**

.. list-table::
   :header-rows: 1
   :widths: 44 56

   * - Function or type
     - Change
   * - ``yse_patcher_init``, ``yse_patcher_parse_json``
     - ``void`` → ``YseStatus``. Malformed JSON now returns an error, and
       parsing before init returns ``YSE_ERR_NOT_INITIALIZED``.
   * - ``yse_system_create_clock``, ``yse_clip_bind``
     - ``int`` (1 = success) → ``YseStatus`` (0 = success). **The meaning of
       0 is reversed.**
   * - ``yse_channel_get_name``
     - Returned ``const char*``; now copies into your buffer, snprintf-style.
   * - ``yse_run_script``, ``yse_set_script_error_callback``
     - Renamed ``yse_python_run_script``,
       ``yse_python_set_script_error_callback``.
   * - ``yse_bus_tap_cb``, ``yse_bus_sub_cb``, ``yse_script_error_cb``
     - Renamed ``YseBusTapCallback``, ``YseBusSubCallback``,
       ``YseScriptErrorCallback``.
   * - ``yse_free_string``
     - Moved from ``yse_patcher.h`` to ``yse_common.h``. Same symbol.
   * - ``yse_system_open_device``
     - Returns ``YSE_ERR_AUDIO_DEVICE`` when the engine refuses the setup; it
       used to return ``YSE_OK``.

**Functions added:**

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Area
     - Functions
   * - Session
     - ``yse_system_request_sample_rate``,
       ``yse_system_get_requested_sample_rate``,
       ``yse_system_set_channel_configuration``,
       ``yse_system_set_render_threads``, ``yse_system_get_render_threads``,
       ``yse_system_get_active_render_threads``, ``yse_block_size``,
       ``yse_randomize``, ``yse_random_seed``,
       ``yse_system_set_occlusion_callback``,
       ``yse_device_setup_get_output_channels``
   * - Named bus
     - ``yse_bus_publish_bang`` / ``_int`` / ``_float`` / ``_string`` /
       ``_list``, ``yse_bus_subscribe``, ``yse_bus_unsubscribe``,
       ``yse_sound_set_name``, ``yse_channel_set_name``
   * - Sounds and buffers
     - ``yse_sound_load_multi_buffer``, ``yse_dsp_multi_buffer_create`` /
       ``_destroy`` / ``_get_channel_count``, ``yse_dsp_buffer_add_buffer`` /
       ``_sub_buffer`` / ``_mul_buffer`` / ``_div_buffer``,
       ``yse_dsp_buffer_copy_from``, ``yse_dsp_buffer_swap``,
       ``yse_dsp_buffer_get_file_sample_rate``, ``yse_buffer_io_exists``,
       ``yse_buffer_io_remove``
   * - Effects and reverb
     - ``yse_dsp_underwater_create`` / ``_set_depth`` / ``_get_depth``,
       ``yse_dsp_object_get_next``, ``yse_reverb_preset_get_values``,
       ``yse_reverb_preset_morph``
   * - Patcher
     - ``yse_patcher_set_name``, ``yse_patcher_get_name``,
       ``yse_patcher_set_container``, ``yse_patcher_get_container``,
       ``yse_patcher_subpatcher_inlets`` / ``_outlets``,
       ``yse_patcher_set_send_callback``, ``yse_patcher_free_message``,
       ``yse_phandle_get_gui_value_count``,
       ``yse_phandle_get_gui_value_at``,
       ``yse_phandle_gui_value_is_settable``
   * - Synth, MIDI and music
     - ``yse_synth_note_on_note``, ``yse_synth_note_off_note``,
       ``yse_dx7_import_sysex_memory``, ``yse_dx7_find_patch``,
       ``yse_midi_out_raw``, ``yse_pnote_set_channel``,
       ``yse_pnote_get_channel``

**Enums.** ``YsePCategory`` gained values 8 to 18. Existing values keep their
numbers; enum values are only ever appended.

Reference
---------

Umbrella header
~~~~~~~~~~~~~~~

.. doxygenfile:: c_api/include/yse_c/yse_all.h
   :project: libYSE

System and listener
~~~~~~~~~~~~~~~~~~~

.. doxygenfile:: c_api/include/yse_c/yse_system.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_listener.h
   :project: libYSE

Sound objects, channels, and reverb
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. doxygenfile:: c_api/include/yse_c/yse_sound.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_channel.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_reverb.h
   :project: libYSE

Audio device
~~~~~~~~~~~~

.. doxygenfile:: c_api/include/yse_c/yse_device.h
   :project: libYSE

DSP and patcher
~~~~~~~~~~~~~~~

.. doxygenfile:: c_api/include/yse_c/yse_dsp.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_dsp_modules.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_patcher.h
   :project: libYSE

Synth and instruments
~~~~~~~~~~~~~~~~~~~~~

.. doxygenfile:: c_api/include/yse_c/yse_synth.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_instrument.h
   :project: libYSE

MIDI and music
~~~~~~~~~~~~~~

.. doxygenfile:: c_api/include/yse_c/yse_midi.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_music.h
   :project: libYSE

Clip transport
~~~~~~~~~~~~~~

Beat-timed note clips dispatched from the audio thread against a domain
clock, targeting internal synths or an external MIDI-out port. The clocks
themselves are created and steered with the ``yse_system_*_clock`` functions
in ``yse_system.h`` above. :doc:`/tutorials/15_clocks_and_clips` walks through
both, with a C example.

.. doxygenfile:: c_api/include/yse_c/yse_clip.h
   :project: libYSE

Live coding: scripting and bus tap
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The embedded-Python live-coding surface (``YSE_ENABLE_PYTHON`` builds; the
symbols exist in every build) and the host bus tap, which subscribes the
host to a bus-address prefix and delivers ``(address, value)`` frames on the
thread that drives ``yse_system_update()``.

.. doxygenfile:: c_api/include/yse_c/yse_python.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_bus.h
   :project: libYSE

Logging and buffer I/O
~~~~~~~~~~~~~~~~~~~~~~

.. doxygenfile:: c_api/include/yse_c/yse_log.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_buffer_io.h
   :project: libYSE

Common types and enums
~~~~~~~~~~~~~~~~~~~~~~

.. doxygenfile:: c_api/include/yse_c/yse_common.h
   :project: libYSE

.. doxygenfile:: c_api/include/yse_c/yse_enums.h
   :project: libYSE
