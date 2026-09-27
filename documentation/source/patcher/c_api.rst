Embedding through the C API
===========================

``yse_c/yse_patcher.h`` is the patcher's C interface. It mirrors
:cpp:class:`YSE::patcher` and :cpp:class:`YSE::pHandle` function for
function, and adds what a binding needs that C++ gets for free: owned
strings, error codes, and the object metadata an editor builds its palette
from. This page groups the header by task, states the ownership and error
rules once, and links to the page that explains each concept. Every function's
own documentation is in the generated :doc:`/api/c_api` reference.

.. code-block:: c

   #include "yse_c/yse_patcher.h"   /* or yse_c/yse_all.h for everything */

A first patch
-------------

A patcher is created empty, given its output count with
``yse_patcher_init``, filled with objects, and handed to a sound, which plays
it:

.. code-block:: c

   #include <stdio.h>
   #include "yse_c/yse_patcher.h"
   #include "yse_c/yse_sound.h"

   YsePatcher* p = yse_patcher_create();
   if (p == NULL || yse_patcher_init(p, 2) != YSE_OK) {
     fprintf(stderr, "patcher: %s\n", yse_last_error());
     return;
   }
   yse_patcher_set_name(p, "lead");   /* the bus scope: patcher.lead.<slot> */

   YsePHandle* note = yse_patcher_create_object(p, ".r", "note");
   YsePHandle* mtof = yse_patcher_create_object(p, ".mtof", NULL);
   YsePHandle* osc  = yse_patcher_create_object(p, "~sine", "440");
   YsePHandle* amp  = yse_patcher_create_object(p, "~*", "0.2");
   YsePHandle* dac  = yse_patcher_create_object(p, "~dac", NULL);

   yse_patcher_connect(p, note, 0, mtof, 0);
   yse_patcher_connect(p, mtof, 0, osc, 0);
   yse_patcher_connect(p, osc, 0, amp, 0);
   yse_patcher_connect(p, amp, 0, dac, 0);
   yse_patcher_connect(p, amp, 0, dac, 1);

   YseSound* voice = yse_sound_create();
   if (yse_sound_load_patcher(voice, p, NULL, 1.0f) != YSE_OK) {  /* NULL: master channel */
     fprintf(stderr, "sound: %s\n", yse_last_error());
   }
   yse_sound_play(voice);

   yse_patcher_pass_int(p, 64, "note");   /* reaches ".r note" at the next block */

   /* ... later, in this order: */
   yse_sound_destroy(voice);
   yse_patcher_destroy(p);

To use a patch as an effect instead, wrap it with
``yse_dsp_patcher_insert_create`` (``yse_c/yse_dsp_modules.h``) and attach
the result with ``yse_sound_set_dsp`` or ``yse_channel_set_dsp``. See
:doc:`index` for both ways of hosting a patch.

Ownership
---------

.. list-table::
   :header-rows: 1
   :widths: 32 68

   * - What you hold
     - Who releases it
   * - ``YsePatcher*`` from ``yse_patcher_create``
     - You, with ``yse_patcher_destroy``. It owns every object in it.
       Keep it alive while a sound plays it or an insert wraps it: neither
       takes ownership.
   * - ``YsePHandle*`` from ``yse_patcher_create_object``, the enumeration
       calls or ``yse_patcher_get_container``
     - The patcher. Remove an object with ``yse_patcher_delete_object``; never
       free a handle. The handle is invalid once its object is deleted,
       cleared, or its patcher destroyed.
   * - Text copied into your buffer by a ``size_t ..._get_*(…, char* buf,
       size_t cap)`` call
     - You. The engine only writes into the buffer you pass.
   * - ``address`` from a ``YsePatcherSendCallback``
     - You, with ``yse_patcher_free_message(address)``. That also frees ``s``.
       Never free ``s`` on its own.
   * - The ``char*`` from ``yse_patcher_get_metadata_json``
     - You, with ``yse_free_string``.
   * - ``const char*`` from the other metadata calls
     - Nobody. The engine keeps these strings until the process ends, so they
       can be cached.

A ``YseDspObject*`` from ``yse_dsp_patcher_insert_create`` is owned by you
and released with ``yse_dsp_object_destroy``. It **borrows** the patcher: the
patcher must outlive it.

``yse_patcher_destroy`` detaches the send callback first and returns only
once no other thread is still inside it. Do not call it from inside that
callback.

Errors and "no such" answers
----------------------------

No C++ exception crosses the C boundary. A function that fails leaves the
reason in ``yse_last_error()``, which is kept **per thread** and stays valid
until your next ``yse_*`` call on that thread. How the failure itself shows
depends on the return type:

- **Status functions** (``yse_patcher_init``,
  ``yse_patcher_set_name``, ``yse_patcher_parse_json``,
  ``yse_patcher_set_send_callback``) return ``YSE_OK`` or an error code:
  ``YSE_ERR_INVALID_HANDLE`` for a ``NULL`` patcher,
  ``YSE_ERR_INVALID_ARGUMENT`` for a bad argument,
  ``YSE_ERR_NOT_INITIALIZED`` for ``yse_patcher_parse_json`` before
  ``yse_patcher_init``, and ``YSE_ERR_EXCEPTION`` when the engine threw.
- **Handle functions** return ``NULL``. ``yse_patcher_create_object``
  returns ``NULL`` for an unknown type name and for an argument that does not
  parse. Only the second one sets ``yse_last_error()``.
- **Functions without a result** do nothing when a handle is ``NULL``. Most refusals
  (a cord to a pin that does not exist, a handle from another patcher) are
  written to the engine log, not to ``yse_last_error()``. Install a log
  callback (``yse_log_set_callback``) to see them.
- **Queries** return 0, ``NULL`` or an empty string for a ``NULL`` handle,
  except where 0 is a real answer. Object IDs and inlet numbers start at 0,
  so those queries answer ``YSE_PATCHER_ID_NONE`` or
  ``YSE_PATCHER_INLET_NONE`` instead, and ``yse_phandle_output_data_type``
  answers ``YSE_OUT_INVALID``. :doc:`building` has the full table.

Reading text
~~~~~~~~~~~~

Every call that returns text copies it into a buffer you provide, like
``snprintf``. It returns the full length (without the terminator) whatever
``cap`` is, truncates to fit, and always terminates the buffer when ``cap``
is not 0. Pass a ``NULL`` buffer to ask for the size first:

.. code-block:: c

   #include <stdlib.h>

   size_t len = yse_patcher_dump_json(p, NULL, 0);
   char* json = malloc(len + 1);
   if (json != NULL) {
     yse_patcher_dump_json(p, json, len + 1);
     /* ... write json to disk ... */
     free(json);
   }

The same pattern works for ``yse_patcher_get_name``,
``yse_phandle_get_type``, ``yse_phandle_get_params``,
``yse_phandle_get_gui_value``, ``yse_phandle_get_gui_value_at`` and
``yse_phandle_get_gui_property``. A value can change between the size query
and the read, for example when a control moves, so compare the second return
value against your buffer size before trusting the copy.

``yse_phandle_get_name`` currently returns the type name, the same string as
``yse_phandle_get_type``.

The header by task
------------------

.. list-table::
   :header-rows: 1
   :widths: 24 46 30

   * - Task
     - Functions
     - Explained in
   * - Create, name, destroy
     - ``yse_patcher_create``, ``yse_patcher_init``,
       ``yse_patcher_set_name``, ``yse_patcher_get_name``,
       ``yse_patcher_destroy``
     - :doc:`host_io` (the name as bus scope)
   * - Build and edit
     - ``yse_patcher_create_object``, ``yse_patcher_delete_object``,
       ``yse_patcher_clear``, ``yse_patcher_connect``,
       ``yse_patcher_disconnect``, ``yse_patcher_is_valid_object``,
       ``yse_phandle_set_params``
     - :doc:`building`
   * - Subpatchers
     - ``yse_patcher_set_container``, ``yse_patcher_get_container``,
       ``yse_patcher_subpatcher_inlets``,
       ``yse_patcher_subpatcher_outlets``
     - :doc:`subpatchers`
   * - Save and load
     - ``yse_patcher_dump_json``, ``yse_patcher_parse_json``
     - :doc:`file_format`
   * - Walk the graph
     - ``yse_patcher_objects``, ``yse_patcher_get_handle_from_list``,
       ``yse_patcher_get_handle_from_id``, ``yse_phandle_get_id``,
       ``yse_phandle_get_type``, ``yse_phandle_get_params``,
       ``yse_phandle_get_inputs``, ``yse_phandle_get_outputs``,
       ``yse_phandle_is_dsp_input``, ``yse_phandle_output_data_type``,
       ``yse_phandle_get_connections``,
       ``yse_phandle_get_connection_target``,
       ``yse_phandle_get_connection_target_inlet``
     - :doc:`building`
   * - Send values in
     - ``yse_patcher_pass_bang`` / ``_int`` / ``_float`` / ``_string``
       (queued, by receiver name); ``yse_phandle_set_bang`` / ``_int`` /
       ``_float`` / ``_list`` (at once, into one inlet)
     - :doc:`host_io`
   * - Get messages out
     - ``yse_patcher_set_send_callback``, ``yse_patcher_free_message``; for
       values sent on the bus, ``yse_bus_subscribe`` in ``yse_c/yse_bus.h``
     - :doc:`host_io`
   * - Draw controls
     - ``yse_phandle_get_gui_value``, ``yse_phandle_get_gui_value_count``,
       ``yse_phandle_get_gui_value_at``,
       ``yse_phandle_gui_value_is_settable``
     - :doc:`gui`
   * - Store editor layout
     - ``yse_phandle_get_gui_property``, ``yse_phandle_set_gui_property``
     - :doc:`gui`, :doc:`file_format`
   * - Build a palette
     - ``yse_patcher_get_type_count`` and the other metadata calls,
       ``yse_patcher_get_metadata_json``
     - `Metadata for editors`_ below

Two things about sending values in are easy to miss:

- ``yse_patcher_pass_*`` returns 1 when a ``.r`` with that name exists **or**
  a send callback is installed, which then gets the value instead. It does not
  mean the value has been handled yet: that happens at the next block.
- ``yse_phandle_set_*`` runs the object and everything wired after it before
  it returns, on your thread. On a subpatcher handle, the inlet number is the
  subpatcher's pin.

Metadata for editors
--------------------

The metadata calls describe every registered object type without a patcher:
its description and category, whether it is a signal object, and a label, doc
string and range for every inlet, outlet and argument. The object reference in
this documentation (:doc:`objects/index`) is generated from the same data. A
binding can build its node palette, tooltips and "which cords are legal"
checks from it:

.. code-block:: c

   #include <stdio.h>
   #include "yse_c/yse_patcher.h"

   int n = yse_patcher_get_type_count();
   for (int t = 0; t < n; t++) {
     const char* type = yse_patcher_get_type_name(t);
     printf("%s  (%s)\n", type, yse_patcher_get_type_description(type));

     for (int i = 0; i < yse_patcher_get_inlet_count(type); i++) {
       const char* label = NULL;
       unsigned int accepts = 0;
       yse_patcher_get_inlet_info(type, i, &label, NULL, NULL, &accepts);  /* NULL: not needed */
       printf("  in %d %s%s\n", i, label,
              (accepts & YSE_IN_ACCEPTS_BUFFER) ? " [signal]" : "");
     }
     for (int o = 0; o < yse_patcher_get_outlet_count(type); o++) {
       const char* label = NULL;
       YseOutType kind = YSE_OUT_INVALID;
       yse_patcher_get_outlet_info(type, o, &label, NULL, NULL, &kind);
       printf("  out %d %s%s\n", o, label, kind == YSE_OUT_BUFFER ? " [signal]" : "");
     }
   }

The rules:

- Types are listed in name order. An index outside ``0`` to
  ``yse_patcher_get_type_count() - 1`` answers ``""``.
- An unknown type name answers 0, ``""``, ``YSE_PCAT_UNSET`` or
  ``YSE_OUT_INVALID``. Out-pointers you pass as ``NULL`` are skipped.
- The accept mask of an inlet is a combination of ``YSE_IN_ACCEPTS_BUFFER``,
  ``_FLOAT``, ``_INT``, ``_BANG`` and ``_LIST``.
- These counts describe a type created **without arguments**. Objects whose
  arguments set their pin count (``.gate``, ``.route``, ``.trigger``) have a
  different count once created; ask the handle with
  ``yse_phandle_get_inputs`` and ``yse_phandle_get_outputs``.
- The first metadata call builds a cache by creating every object type once.
  That takes a moment, so make it early, for example at startup.

``yse_patcher_get_metadata_json`` returns all of this as one JSON document,
keyed by type name, with the fields ``name``, ``category``, ``is_dsp``,
``requires_midi_device``, ``description``, ``inlets``, ``outlets`` and
``params``. It is the same layout ``python yse.py dump-patcher-meta`` writes,
so a binding can generate code from a file at build time and read the live
data at run time. ``requires_midi_device`` marks the objects that exist only
in builds with MIDI device support, so a palette built on one platform can
flag them. Release the string with ``yse_free_string``.

Threads
-------

:doc:`realtime` explains the threading model. For the C API it comes down to
this:

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Functions
     - May be called from
   * - Everything in ``yse_patcher.h``
     - Your own threads, never the audio callback. Edits from several
       threads are serialised by the patcher, but coordinate object lifetime
       yourself: a handle one thread holds is freed when another deletes the
       object.
   * - ``yse_patcher_init``, ``yse_patcher_set_name``,
       ``yse_patcher_parse_json``, and the metadata calls
     - One control thread, as their documentation says.
   * - GUI value reads
     - Your GUI thread. Read a given object either whole or cell by cell
       each frame, never both: some reads clear what they report (see
       :doc:`gui`).

And for the send callback:

- It runs **synchronously on the thread that sent the message**: yours for a
  ``yse_patcher_pass_*`` or ``yse_phandle_set_*`` call, the engine's timer
  thread for a millisecond ``.metro``. Never the audio thread.
- It may be called from several threads at once. Keep it short and thread
  safe.
- Installing or clearing it may still let one call that had already started
  reach the previous callback, always with its own ``user_data``.
- ``yse_patcher_set_send_callback`` and ``yse_patcher_destroy`` wait for calls
  running on other threads to finish. Do not call them while holding a lock
  the callback also takes. Calling ``yse_patcher_set_send_callback`` from
  inside the callback is allowed; calling ``yse_patcher_destroy`` there is not.
- The strings stay valid after the callback returns, so an asynchronous host
  (a Dart ``NativeCallable.listener``, for example) can read them later and
  free them with ``yse_patcher_free_message``.

Where to go next
----------------

- :doc:`building`: creating, connecting and editing objects, with the full
  "no such" table.
- :doc:`host_io`: the queue, the bus and the send callback.
- :doc:`gui`: polling controls and writing their state back.
- :doc:`realtime`: which thread runs what, and why edits never block the
  audio.
- :doc:`/api/c_api`: every C function, generated from the headers.
