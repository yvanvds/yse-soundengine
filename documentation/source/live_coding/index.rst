Live coding
===========

When libYSE is built with ``YSE_ENABLE_PYTHON=ON`` it embeds a CPython
interpreter and exposes a small Python module, ``yse``, for *live coding* —
driving the running engine from scripts you can re-evaluate while sound is
playing. A composer reasons in terms of **names on a bus** and **callbacks
tied to those names**; sounds, channels, patchers, and freeform
script-to-script signals all share that one model.

This page is a worked tour of the surface. The authoritative specification —
address grammar, value types, threading and hot-reload semantics — is
`docs/design/live_coding_dsl.md
<https://github.com/yvanvds/yse-soundengine/blob/master/docs/design/live_coding_dsl.md>`_.

Running a script
----------------

Scripts are submitted from the host through the C API and executed
asynchronously on a dedicated script thread. Errors arrive on a callback you
install once:

.. code-block:: c

   #include "yse_c/yse_python.h"

   void on_error(const char *traceback, void *user) {
       fprintf(stderr, "%s\n", traceback);
   }

   yse_set_script_error_callback(on_error, NULL);
   yse_run_script("yse.send('synth1.cutoff', 800)\n");

The ``yse`` module is already bound into the script's namespace — scripts call
``yse.send(...)`` directly, no ``import`` needed.

The primitives
--------------

``yse.send(name, value)``
   Publish ``value`` to address ``name`` on the global bus. Accepts ``int``,
   ``float``, ``str``, or a ``list`` of numbers; anything else (including
   ``bool``) raises ``TypeError``, and an empty name raises ``ValueError``.
   Publishing is fire-and-forget.

   .. code-block:: python

      yse.send("sound.kick.volume", 0.7)
      yse.send("synth1.cutoff", 800)
      yse.send("position", [0.0, 1.5, -2.0])

``yse.on(name, callback)``
   Subscribe ``callback`` to ``name``; returns an integer handle. The callback
   fires on the script thread, once per tick, with the published value.

   .. code-block:: python

      def on_trigger(v):
          yse.send("kick.gate", v)

      h = yse.on("section_a.trigger", on_trigger)

``yse.unsubscribe(handle)``
   Drop a subscription. Idempotent — an already-released or unknown handle is a
   no-op.

``yse.latch(name)``
   Sugar over ``on`` that caches the most recent value. ``latch.value`` is
   ``None`` until the first publish; the subscription is released when the
   latch is garbage-collected or you call ``latch.unsubscribe()``.

   .. code-block:: python

      cutoff = yse.latch("synth1.cutoff")
      # ... a tick or two after a publish ...
      if cutoff.value is not None and cutoff.value > 1000:
          yse.send("alarm", 1)

``yse.schedule(ticks, fn, *args, **kwargs)``
   Run ``fn`` ``ticks`` engine update ticks from now, on the script thread.
   ``ticks=0`` fires on the next update (never the current one). Returns a
   handle; cancellation is via :func:`yse.cancel_all`.

   .. code-block:: python

      def kick():
          yse.send("kick.trigger", 1)

      yse.schedule(4, kick)   # 4 ticks from now
      yse.schedule(8, kick)   # 8 ticks from now

``yse.tick``
   A read-only attribute: the monotonic engine tick counter, incremented once
   per ``system::update()`` and reset to 0 on ``System::init``.

   .. code-block:: python

      print(f"now at tick {yse.tick}")

``yse.cancel_all()``
   Deregister every subscription and pending schedule from a *strictly older*
   evaluation than the current one. Registrations made in the same script that
   calls ``cancel_all`` survive, so the editor idiom is to prefix each
   re-evaluation with it:

   .. code-block:: python

      yse.cancel_all()   # wipe the previous evaluation
      # ... the rest of the script ...

``yse.fresh_scope()``
   A context manager that calls :func:`yse.cancel_all` on entry — sugar for
   composers who prefer to scope a fresh start to a block rather than prefix the
   whole script. Registrations made *inside* the ``with`` block belong to the
   current evaluation and survive; everything from older evaluations is torn
   down before the body runs.

   .. code-block:: python

      with yse.fresh_scope():
          yse.on("trigger", on_trigger)
          yse.schedule(4, kick)

   This is exactly equivalent to opening the block's body with a bare
   ``yse.cancel_all()``.

Hot-reload pattern
------------------

Because libYSE never auto-clears state between evaluations, layering is the
default: re-evaluating a script *adds* its subscriptions and schedules on top of
what earlier evaluations installed. To get *replacement* behaviour instead — the
usual expectation when iterating on a patch — start each evaluation from a clean
slate. Either prefix the script:

.. code-block:: python

   yse.cancel_all()
   # ... the patch ...

or wrap it in :func:`yse.fresh_scope`. The Phi editor's "Evaluate" button can be
configured to inject the ``yse.cancel_all()`` prefix automatically; composers who
want to layer can omit it per evaluation.

Addressing
----------

Three prefixes are owned by the engine and let scripts reach engine objects by
the name assigned through the C++/C API:

==============================  =======================================
Address                         Produced / consumed by
==============================  =======================================
``sound.<name>.<prop>``         ``YSE::sound`` properties
``channel.<name>.<prop>``       ``YSE::channel`` properties
``patcher.<name>.<slot>``       patcher ``gSend`` / ``gReceive`` slots
==============================  =======================================

Every other name is *freeform* and exists purely for script-to-script
communication. A dotted path such as ``"section_a.gate"`` is just a
human-readable convention — the bus treats the whole string as opaque.

A worked example
-----------------

A self-rescheduling sweep that reads a latched value back and nudges it each
tick:

.. code-block:: python

   yse.cancel_all()

   cutoff = yse.latch("synth1.cutoff")

   def sweep():
       if cutoff.value is None:
           yse.schedule(1, sweep)   # not ready yet, try next tick
           return
       yse.send("synth1.cutoff", cutoff.value * 1.01)
       yse.schedule(1, sweep)

   sweep()

Threading in one sentence
-------------------------

Every callback runs on the script thread, in batch, once per
``system::update()`` tick — a value published on the audio thread at tick *t*
is delivered to a Python handler at tick *t+1*. Scripts never run on the audio
thread, and the surface offers no way to make them.
