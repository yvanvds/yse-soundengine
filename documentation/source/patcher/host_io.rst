Talking to the host
===================

A patch runs inside a host program, and most patches need to exchange
values with it. The host sends notes and parameter changes in, and reads
triggers, values and debug output back. This page covers each route in and
out, which thread each one runs on, and how the patcher's **name** decides
who can hear what.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Route
     - Use it for
   * - ``PassData`` / ``PassBang`` → ``.r``
     - Sending a value into one patcher by receiver name.
   * - ``pHandle::Set*``
     - Sending a value straight into one object's inlet.
   * - The named bus, ``patcher.<name>.<slot>``
     - Talking to ``.s`` / ``.r`` from the C API or a Python script, and
       linking patchers to each other.
   * - The send callback (``SetOscHandler``)
     - Receiving what a ``.s`` sends when nothing in the patcher listens.
   * - ``.print``
     - Watching values pass, through the engine log.
   * - ``.value``
     - Keeping a value that other objects read later.

Sending into a patch
--------------------

PassData and PassBang
~~~~~~~~~~~~~~~~~~~~~

``PassData`` delivers a value to every ``.r`` in the patcher whose name
matches. ``PassBang`` does the same with a bang:

.. code-block:: cpp

   YSE::pHandle* note = patch.CreateObject(YSE::OBJ::G_RECEIVE, "note");  // ".r note"
   YSE::pHandle* mtof = patch.CreateObject(".mtof");
   patch.Connect(note, 0, mtof, 0);

   patch.PassData(64, "note");            // int
   patch.PassData(0.5f, "gain");          // float
   patch.PassData("60 100", "chord");     // list
   patch.PassBang("start");               // bang

The call does not deliver the value itself. It puts the value in a queue and
returns. The value reaches the ``.r`` at the start of the next block the
patcher renders, on the audio thread, and everything the ``.r`` sets off runs
there too. A patcher that is neither a sound nor an insert renders no blocks,
so it never processes these values (see :doc:`index`).

The return value says whether anything took the value:

- ``true`` when a ``.r`` with that name exists at the time of the call.
- ``true`` when no ``.r`` matches but a send callback is installed. The
  callback gets the value instead (see `The send callback`_).
- ``false`` otherwise. The engine also logs the names of the receivers the
  patcher does have, which helps you find a typo.

Some limits apply because the queue holds fixed-size records, so the audio
thread never allocates:

- The receiver name must be shorter than 64 characters. A ``.r`` refuses
  longer names anyway.
- A list must be shorter than 256 characters. A longer one is dropped and
  logged. If a send callback is installed, the callback gets it.
- The queue holds 256 values. It is emptied every block, so it only fills up
  when a host sends many values between two blocks. When it is full, the
  value is dropped and logged.

In the C API these calls are ``yse_patcher_pass_bang``,
``yse_patcher_pass_int``, ``yse_patcher_pass_float`` and
``yse_patcher_pass_string``. They return ``1`` or ``0`` where C++ returns
``true`` or ``false``.

Straight into an inlet
~~~~~~~~~~~~~~~~~~~~~~

``pHandle::SetBang``, ``SetIntData``, ``SetFloatData`` and ``SetListData``
skip the receiver lookup. They deliver to one inlet of one object **right
away, on the calling thread**, and the whole chain of messages that follows
runs before the call returns. :doc:`building` describes this path. Two
things to know when you use it to drive a patch:

- A value sent to a signal object this way is only stored. It takes effect
  in the next block (see :doc:`messages`).
- On a subpatcher handle, the value goes to the ``.inlet`` with that index
  (see :doc:`subpatchers`).

Use ``PassData`` when the host should not have to keep object handles. Use a
handle when you need the result right away, for example to read something
back through the send callback, as in the ``.value`` example below.

Send and receive: ``.s`` and ``.r``
-----------------------------------

``.s <name>`` sends whatever reaches its inlet to every ``.r <name>``. It
does this in two ways:

1. **Inside the patcher.** It passes the value to the patcher's own
   ``.r <name>`` objects, the same way ``PassData`` does. Unlike a
   ``PassData`` call from the host, a ``.s`` that runs while the patch
   renders delivers in the same block, without a queue.
2. **On the global named bus.** It publishes the value at the address
   ``patcher.<patcherName>.<name>``. Every ``.r <name>`` in every patcher with
   the same name is subscribed to that address, and so are any host or script
   subscribers.

A second argument of ``1`` (``.s cutoff 1``) turns off the first way. The
value then only goes out on the bus, and the patcher's own ``.r`` objects get
it from there. ``.r`` accepts the same second argument but ignores it for now.

Names are at most 63 characters. A longer name is refused with a log line,
and the object is left without a name.

.. warning::

   While the engine is running, a ``.r`` in the **same** patcher as its
   ``.s`` currently receives each value **twice**, once through each of the
   two ways above. A value sent from the control thread arrives right away
   through the bus and again at the start of the next block. A value sent
   while rendering arrives in the same block and again on the next
   ``System::update()``. This is tracked in
   `#943 <https://github.com/yvanvds/yse-soundengine/issues/943>`_. Until
   it is fixed, use ``.s <name> 1`` when a ``.r`` in the same patcher must
   see each value exactly once, or connect the two objects with a cord.

``.forward`` is a ``.s`` that can change its target while it runs. Its right
inlet takes a new name. The first word of a list is used, and an int is read
as the name it spells. Values then go to that name in the same two ways,
at the address ``patcher.<patcherName>.<destination>``. A ``.forward``
without a destination sends nothing. It accepts the same ``globalOnly``
argument as ``.s``.

The patcher name is the scope
-----------------------------

Every patcher has a name. By default it is ``patcher_<N>``, where the number
comes from a counter in the process. Set your own name with
``patcher::name()`` in C++ or ``yse_patcher_set_name`` in C:

.. code-block:: cpp

   YSE::patcher lead;
   lead.name("lead").create(2);

The name scopes everything inside the patcher that works by name:

- ``.s``, ``.r`` and ``.forward`` use the bus addresses
  ``patcher.<name>.<slot>``, and so do the ``send`` messages of ``.bag`` and
  ``.table``.
- The shared stores (``.value``, ``.coll``, and the ``.dict`` and ``.array``
  families) are keyed on the same ``patcher.<name>.<store>`` string. A ``.value tempo``, a ``.s tempo``
  and a ``.r tempo`` in one patcher therefore all refer to the same word.
  :doc:`data` covers the stores themselves.

The name applies to the whole patcher, including every subpatcher in it.
There is no per-copy prefix like Max's ``#0`` (see :doc:`subpatchers`).

What follows from this:

- **Patchers with the same name share on purpose.** A ``.s`` in one reaches a
  ``.r`` in the other, and a ``.coll notes`` in each is one collection. This
  is how two patchers talk to each other directly, and the engine does not
  force names to be unique. Patchers with different names stay separate,
  even when their slot names are the same.
- **Unnamed patchers never share by accident.** Every auto-name is different.
  But the number changes from run to run and is never saved, so do not use
  it as an address in a script. Name any patcher a script or host must
  reach.
- **Renaming moves everything.** Existing ``.r`` objects subscribe again
  under the new name, ``.s`` objects publish under it, and stores rebind to
  the new key. Passing an empty name restores the auto-name.
- **The name is saved with the patch.** ``DumpJSON`` writes a name you set,
  and ``ParseJSON`` restores it unless the host has already named the
  patcher (see :doc:`file_format`).
- **Length limits.** A patcher name is at most 55 characters and a slot name
  at most 63, so that a full address fits the bus. Longer names are refused
  and logged, and the old name is kept.

The ``patcher.`` prefix is reserved, like ``sound.``, ``channel.`` and
``synth.``. A freeform script address ``lead.cutoff`` does **not** reach a
patcher named ``lead``. Only ``patcher.lead.cutoff`` does.

The bus from the host
~~~~~~~~~~~~~~~~~~~~~

The C API can publish to and subscribe to bus addresses. That lets a host
talk to ``.s`` and ``.r`` without holding a patcher pointer:

.. code-block:: c

   #include "yse_c/yse_bus.h"

   /* Into the patch: every ".r cutoff" in patchers named "lead". */
   yse_bus_publish_float("patcher.lead.cutoff", 800.f);

   /* Out of the patch: every ".s level" in patchers named "lead". */
   static void on_level(const char* address, YseBusValueKind kind, int i, float f,
                        const char* str, const float* list, size_t len, void* user) {
       if (kind == YSE_BUS_FLOAT) meter_set(f);
   }
   YseBusSub* sub = yse_bus_subscribe("patcher.lead.level", on_level, NULL);

   /* Every bus address in patcher "lead", for an editor or a monitor. */
   YseBusTap* tap = yse_bus_tap_create("patcher.lead.", on_any, NULL);

Bus subscribers and taps run on the **control thread**, the thread that calls
``System::update()`` / ``yse_system_update()``:

- A publish made on the control thread is delivered before the publish call
  returns.
- A publish from any other thread is delivered on the next update tick. That
  includes the audio thread, the engine's timer thread and the script
  thread.

This also applies to a ``.r`` subscribed on the bus. When a value reaches a
``.r`` through the bus, the ``.r`` sends it on right away, on the control
thread, just like a ``pHandle::Set*`` call. It does not use the
``PassData`` queue.

Not every value survives the trip from the audio thread. The audio thread
may not allocate, so it can only put ints and floats on the bus. A bang or a
list sent by a ``.s`` while the patch renders reaches only the patcher's own
``.r`` objects: bus subscribers, other patchers and the host never see it.
That includes any ``.s`` driven by a ``PassData`` value, because those run
while the patch renders. When the host must see such an event, send a number
instead, for example ``.trigger i`` → ``.s``, which turns a bang into a
``0``.

``.r`` objects subscribe to the bus when they are created, but only while
the engine is running. Create patchers after ``System().init()``.
``System().close()`` ends every subscription.

From a Python script
~~~~~~~~~~~~~~~~~~~~

The live-coding module speaks the same addresses. ``yse.send`` publishes to
a ``.r`` and ``yse.on`` or ``yse.latch`` listens to a ``.s``:

.. code-block:: python

   yse.send("patcher.lead.cutoff", 800)        # reaches .r cutoff in "lead"
   level = yse.latch("patcher.lead.level")     # follows .s level in "lead"
   yse.on("patcher.a.x", lambda v: yse.send("patcher.b.x", v))  # link two patchers

Script callbacks run on the script thread, once per update tick. A script's
``yse.send`` reaches the ``.r`` on the next tick. See :doc:`/live_coding/index`
for the rest of the module.

The send callback
-----------------

``patcher::SetOscHandler`` installs an ``oscHandler`` that receives every
message the patcher sends to a name **no** ``.r`` in it answers. That covers
both a ``.s`` in the patch and a ``PassData`` / ``PassBang`` call from the
host with no receiver. Override the ``Send`` overloads you need:

.. code-block:: cpp

   struct ToHost : YSE::oscHandler {
     void Send(const std::string& name) override { /* bang */ }
     void Send(const std::string& name, int v) override { /* ... */ }
     void Send(const std::string& name, float v) override { /* ... */ }
     void Send(const std::string& name, const std::string& list) override { /* ... */ }
   };

   ToHost toHost;
   patch.SetOscHandler(&toHost);   // nullptr removes it

``name`` is the bare slot name, the ``.s`` argument or the ``PassData``
target, not the ``patcher.<name>.<slot>`` bus address. The ``.s`` still
publishes on the bus as usual. The callback gets a copy of what nobody in
the patcher heard.

The callback runs **synchronously on the thread that sent the message**.
That is the host's own thread for a ``PassData`` call or for a
``pHandle::Set*`` call that sets off a ``.s``. It is an engine timer thread
for a ``.s`` driven by a millisecond ``.metro``. It is **never** the audio
thread: a ``.s`` that runs while the patch renders keeps its message inside
the patch, and the callback does not fire. Values that come in through
``PassData`` are processed while rendering, so a chain like
``PassData`` → ``.r`` → ``.s`` does not reach the callback either. Use a bus
subscription for numbers from the render path. Keep the callback short and
thread safe.

``SetOscHandler`` returns once no other thread is still inside the handler it
replaced, so you may destroy the old handler afterwards. The patcher does not
own the handler. A handler set before ``create()`` is kept and installed at
``create()``.

In the C API the callback is ``yse_patcher_set_send_callback``. There, the
receiver owns the strings and frees them with ``yse_patcher_free_message``,
so an asynchronous host can read them later.

Watching values: ``.print``
---------------------------

``.print`` writes everything that arrives at its inlet to the engine log, one
line per message. It is Max's ``print``. There is no Max window, so the
engine log takes its place. The first argument is the label in front of each
line (default ``print``, at most 32 characters):

.. code-block:: cpp

   YSE::pHandle* probe = patch.CreateObject(YSE::OBJ::G_PRINT, "bass");
   patch.Connect(mtof, 0, probe, 0);
   // a log handler receives "(App Message)  bass: 440."

A bang prints ``bang``. An int prints its digits. A float always has a
decimal point, so ``440.`` is still visibly a float. Lists and message box
text print as they are. ``.print`` has no outlet.

The object never writes to the log directly, because it may run on the audio
thread. It puts the line in a lock-free queue, and ``System::update()``
empties that queue into the log. So a line appears on the next update tick,
on the control thread, and it is tagged ``(App Message)``. That tag passes
the default ``EL_ERROR`` log level, so release builds show these lines too.
To catch the lines in your program, install a handler with
``YSE::Log().setHandler()`` (``yse_log_set_callback`` in C). To send the
default log to another file, use ``setLogfile()``.

Two limits keep a ``.print`` that runs once per block from flooding the log:

- Each object prints at most 16 lines per update tick. A second argument
  changes this, from 1 to 256 (``.print bass 64``). Messages over the limit
  are dropped, and the first drop in a tick logs one notice.
- The queue is shared by all ``.print`` objects in the process. Lines that
  do not fit are counted, and the next update logs how many were lost.

A line longer than about 240 characters is cut and ends in ``...``. Nothing
is ever dropped silently: every loss is reported in the log.

Stored values: ``.value``
-------------------------

``.value <name>`` is a named cell shared by every ``.value`` with the same
name, following the scope rules above. It is Max's ``value``:

- An int, float or list at the inlet **stores** the value for every
  ``.value`` with that name and sends nothing.
- A **bang** sends out the stored value. Nothing comes out until something
  has been stored.

``.s`` / ``.r`` push a value at the moment it changes. ``.value`` holds it
until someone asks. Use it for questions like "what is the tempo now?" that
come up later. Storing does not publish anything on the bus. When other
objects must also react to a change, send the value to a ``.value`` and a
``.s`` from the same outlet.

More about ``.value``:

- The rest of the arguments give a starting value (``.value tempo 120``,
  ``.value pos 0 0``). Only the ``.value`` that creates the cell applies it.
  A later ``.value tempo`` adopts whatever is already stored, so adding a
  reader never resets the value.
- A ``.value`` without a name has a private cell. It does not share with
  other unnamed ones.
- A list longer than 256 characters is refused, and the old value stays.

The host has no direct way to read or write a cell. Use the routes above:

.. code-block:: cpp

   // Write: the host feeds ".r tempo" -> ".value tempo".
   patch.PassData(128, "tempo");

   // Read: bang the .value from the host, its outlet feeds ".s tempo_now",
   // and with no ".r tempo_now" in the patch the send callback gets it.
   tempoValue->SetBang(0);   // ToHost::Send("tempo_now", 128) runs before this returns

Where to go next
----------------

- :doc:`messages`: message types, hot and cold inlets, and dispatch order.
- :doc:`time`: ``.metro``, delays and the clocks that decide which thread a
  timed message runs on.
- :doc:`data`: the shared stores in more detail.
- :doc:`objects/input_output`: the reference for ``.s``, ``.r``,
  ``.forward``, ``.value`` and ``.print``.
- :doc:`/live_coding/index`: the Python live-coding module and its address
  rules.
