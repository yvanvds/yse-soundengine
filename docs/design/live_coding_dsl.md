# Live-coding DSL surface specification

Status: **design, pre-implementation**. Tracking issue: [#120][gh-120].
Parent epic: [#119 — Embedded Python live-coding for YSE][gh-119].

This document is the single source of truth for the embedded-Python DSL
exposed by libYSE when built with `YSE_ENABLE_PYTHON=ON`. It is written
*before* any binding code so the implementation issues
([#121][gh-121]–[#127][gh-127]) can focus on plumbing without
re-litigating API shape.

Once issue [#126][gh-126] ships, every primitive defined here becomes a
public contract — changes are breaking changes for performers writing
Phi scripts. Treat that as a forcing function for getting the shape
right now.

## Table of contents

1. [Goals and non-goals](#goals-and-non-goals)
2. [Address grammar](#address-grammar)
3. [Value types](#value-types)
4. [Primitive API](#primitive-api)
5. [Error model](#error-model)
6. [Threading model](#threading-model)
7. [Hot-reload semantics](#hot-reload-semantics)
8. [Worked examples](#worked-examples)
9. [Cross-references](#cross-references)

---

## Goals and non-goals

### Goals

- **One mental model.** A composer reasons in terms of *names on a bus*
  and *callbacks tied to those names*. Sounds, channels, patchers, and
  freeform script-to-script signals all share that model.
- **No FFI in user code.** Scripts never see C++ pointers, handles, or
  engine types directly. Addressing is by string; values are plain
  Python types.
- **Control-rate by construction.** One DSL "tick" equals one
  `system::update()`. Scripts cannot accidentally run on the audio
  thread — the surface makes the option unavailable.
- **Predictable failure.** Every error path produces a Python traceback
  routed through one callback. There is no silent failure mode.

### Non-goals (explicit)

The following are out of scope for the whole epic and will not be added
later under the same primitive names without a new design pass:

- **No `async` / `await`.** The script thread is not an asyncio loop.
  Deferred execution is expressed by [`yse.schedule`](#ysescheduleticks-fn-args-kwargs)
  only.
- **No audio-rate scripts.** Anything the audio callback touches is off
  limits. The GIL makes this non-negotiable; the surface enforces it by
  not exposing audio-thread hooks at all.
- **No graph introspection.** Scripts cannot ask "what patchers exist?",
  "what sounds are loaded?", "what subscribers are bound to name X?".
  Names are agreed upon by convention between the composer and the
  patches they author. Listing primitives are deferred to a future
  enhancement and intentionally absent from this version.
- **No sandbox.** `import os` works. The performer is trusted. Capability
  restriction is a separate problem; we will not pretend to solve it
  with a half-measure.
- **No persistent script state across `System::close`/`init`.** The
  interpreter is finalized on close. Re-init starts from a blank
  `__main__`.
- **No third-party packages.** See [#124][gh-124]. *Implementation note:*
  #124 sources CPython via `find_package` (a system / prebuilt libpython)
  and isolates it at **runtime** with `PyConfig` — isolated mode, no
  environment, and `site_import = 0` so site-packages (hence any
  third-party package) is never importable — rather than baking a
  **build-time** frozen module subset as originally envisaged. The
  "no third-party packages" guarantee holds; the practical difference is
  that the interpreter sees the *full* standard library of the located
  install (anchored via `PyConfig.home`), not a curated subset, and its
  version follows the host rather than a pinned 3.12.x. The CPython static
  build + freeze tooling proved impractical (no CMake build; unsupported
  under the project's MSYS2 Clang64 toolchain). See PROJECT_OVERVIEW.md
  "Python embedding" for the full rationale.

---

## Address grammar

All inter-component routing goes through a single global named bus
([#121][gh-121]). The DSL agrees on the following address conventions.

### Reserved prefixes

The engine owns four prefixes. Composer scripts may publish to and
subscribe to these, but the engine is the canonical *producer* (sounds,
channels) or *consumer* (patcher inlets, synth note/controller events)
for these addresses.

| Prefix                       | Owner    | Source of truth                    |
|------------------------------|----------|------------------------------------|
| `sound.<name>.<prop>`        | engine   | [`YSE::sound`][gh-123] properties  |
| `channel.<name>.<prop>`      | engine   | [`YSE::channel`][gh-123] props     |
| `patcher.<name>.<slot>`      | engine   | `gSend` / `gReceive` ([#122][gh-122]) |
| `synth.<name>.<event>`       | engine   | [`YSE::synth`][gh-388] note/controller events |

`<name>` is the name assigned via the engine API (`sound.name(...)`,
`channel.name(...)`, `patcher.name(...)`, `synth.name(...)`). `<prop>`,
`<slot>` and `<event>` are the property, routing slot or event exposed
by that object. All are matched exact-string — case-sensitive, no
globbing, no wildcards.

The set of valid `<prop>`, `<slot>` and `<event>` names is fixed by
the engine (see [#123][gh-123] for the initial sound/channel
properties: `volume`, `speed`, `position`; see [#122][gh-122] for
patcher slot rules; see [#388][gh-388] and the
[synth event mapping](#mapping-to-synth-events) for the synth events:
`note`, `off`, `cc`, `bend`, `aftertouch`, `alloff`). The DSL never
expands or filters this set — whatever the bus exposes is what scripts
see.

### Freeform names

Everything that does not match a reserved prefix is *freeform* and
exists purely for script-to-script communication. By convention:

- A bare word — `"cutoff"`, `"lfo_phase"` — is fine.
- A dotted path — `"synth1.cutoff"`, `"section_a.gate"` — is the
  recommended idiom for namespacing inside a script or between scripts.

The bus does not parse dotted paths. They are opaque strings; the dot
is purely a human-readable separator.

### Collision policy

There is **no per-name ownership**. Multiple publishers to the same
name are permitted; every subscriber receives every publish in arrival
order ([#121][gh-121] guarantees ordering only per name, not across
names). The DSL does not warn or error on overlap.

The one exception is engine-owned names: registering a duplicate
*producer* of an engine address (e.g. two sounds both named `"kick"`)
is rejected at registration time and logged via the engine's existing
error path ([#123][gh-123]). The bus is not involved in that rejection.

### Empty / anonymous instances

Engine objects with no name set are not addressable. Scripts cannot
reach an anonymous sound. There is no fallback identity (no auto-name,
no UUID exposure). This is deliberate: anonymity is the opt-out from
the bus.

### Reserved characters and length

- Names are UTF-8. The bus enforces no character restrictions
  ([#121][gh-121]).
- Names must be non-empty. `yse.send("", v)` raises `ValueError`.
- No length cap. (Composers are trusted to write short names.)

---

## Value types

Four Python types cross the bus, mapped to `INTERNAL::BusValue` from
[#121][gh-121]:

| Python              | `BusValue`              | Notes                                              |
|---------------------|-------------------------|----------------------------------------------------|
| `int`               | `int`                   | 64-bit signed in C++.                              |
| `float`             | `float`                 | 64-bit double.                                     |
| `str`               | `std::string`           | UTF-8.                                             |
| `list` of numbers   | `std::vector<float>`    | Each element coerced via `float(x)`; `int` ok.    |

Any other type — `bool`, `tuple`, `dict`, `bytes`, `None`, custom
class — raises `TypeError` from `yse.send`. The traceback is delivered
through the error callback ([#125][gh-125]).

`bool` is *not* silently coerced to `int`; this is by choice. If a
composer wants boolean semantics, they write `1` and `0`. The
asymmetry of "Python `True == 1`" is exactly the kind of surprise the
DSL avoids by being explicit.

When a `list` reaches a subscriber, the subscriber receives a Python
`list[float]` (always `float`, even if the publisher sent `int`s).
This is the C++ side's only sequence type.

### Mapping to engine properties

The engine-side property setters ([#123][gh-123]) accept specific
shapes for specific names:

- `sound.<name>.volume` → `float`
- `sound.<name>.speed` → `float`
- `sound.<name>.position` → `list[float]` of length 3
- `channel.<name>.volume` → `float`

A published `int` on a scalar `float` address (`volume`, `speed`) is
coerced via `float(x)` rather than treated as a mismatch — consistent
with the per-element coercion the `list` type already applies. This is
the only implicit numeric coercion; everything else follows the
[value type table](#value-types).

A type mismatch on an engine-owned address (e.g. publishing a string
to `sound.kick.volume`, or a `position` list whose length is not 3) is
ignored on the C++ side and does not raise in the script. This is
deliberate: from the script's perspective,
publishes are fire-and-forget. The error appears in engine logs;
the bus does not route it back to the publisher.

### Mapping to synth events

A named [`YSE::synth`][gh-388] consumes note and controller *events*
rather than properties. Every event is delivered through the synth's
existing RT-safe message inbox — the bus subscriber runs on the
control thread and enqueues exactly what the corresponding C++ setter
(`noteOn`, `noteOff`, `controller`, …) would; no new audio-thread
surface is opened.

- `synth.<name>.note` → `list[float]` `[channel, note, velocity]` —
  note-on. Maps to `synth::noteOn(channel, note, velocity)`.
- `synth.<name>.off` → `list[float]` `[channel, note]` or
  `[channel, note, velocity]` — note-off (release velocity defaults
  to 0). Maps to `synth::noteOff(...)`.
- `synth.<name>.cc` → `list[float]` `[channel, number, value]` —
  control change, `value` normalised to [0, 1]. Maps to
  `synth::controller(...)`. CC 64 / 66 / 67 act as the sustain /
  sostenuto / soft pedals, exactly as in the C++ API — the pedals
  deliberately get no addresses of their own.
- `synth.<name>.bend` → `list[float]` `[channel, value]` — pitch
  wheel, `value` normalised to [-1, 1] (0 = centre). Maps to
  `synth::pitchWheel(...)`.
- `synth.<name>.aftertouch` → `list[float]` `[channel, note, value]` —
  pressure normalised to [0, 1]; `note == -1` is channel-wide. Maps to
  `synth::aftertouch(...)`.
- `synth.<name>.alloff` → `int` channel (0 = all channels; `float`
  coerced via the scalar rule above). A bang (an empty publish from a
  patcher `gSend`) is also accepted and releases all channels. Maps to
  `synth::allNotesOff(...)`.

`channel` and `note` list elements are rounded to the nearest integer
(the bus's only sequence type is `list[float]`); `velocity` / `value`
stay `float`. Channel semantics follow the synth API: 0 = omni /
all channels, 1..16 = a specific MIDI channel. As with the property
addresses above, a wrong shape (wrong list length, or a scalar where a
list is expected) is silently ignored engine-side — fire-and-forget.

Latency note: engine-direct delivery does not save a control tick over
a host-mediated send. A script-thread publish is parked in the bus's
control inbox and dispatched on the next `system::update()` either
way; the win is removing the host round-trip and host-side glue, not
a tick.

---

## Primitive API

Every primitive listed in the epic appears here with signature,
return contract, threading guarantee, and a worked example. The
canonical implementation lives in [#126][gh-126].

### `yse.send(name, value)`

```python
yse.send(name: str, value: int | float | str | list[float]) -> None
```

Publish `value` to address `name` on the global bus.

- **Returns:** `None`. Publishing is fire-and-forget; there is no
  confirmation that any subscriber received it.
- **Raises:** `TypeError` if `value` is not one of the four accepted
  types. `ValueError` if `name` is empty.
- **Thread:** runs on the script thread. The bus dispatches the value
  via `INTERNAL::NamedBus::publish` with thread tag = main-equivalent
  ([#121][gh-121]) — i.e. subscribers fire after the next
  `drainPending()`.

```python
yse.send("sound.kick.volume", 0.7)
yse.send("synth1.cutoff", 800)
yse.send("position", [0.0, 1.5, -2.0])
```

### `yse.on(name, callback)`

```python
yse.on(name: str, callback: Callable[[Any], None]) -> int
```

Subscribe `callback` to address `name`. Returns an opaque integer
handle suitable for [`yse.unsubscribe`](#yseunsubscribehandle).

- **Returns:** subscription handle (a Python `int`).
- **Raises:** `ValueError` if `name` is empty. `TypeError` if
  `callback` is not callable.
- **Callback signature:** `callback(value)`. The argument's Python
  type matches the [value type table](#value-types).
- **Thread:** the subscription is registered on the script thread;
  the callback fires on the script thread, with the GIL held, when
  the bus drains a matching publish.
- **Order:** callbacks for the same name fire in subscription order.
  Callbacks for different names fire in publish order.
- **Generation tagging:** every handle is tagged with the current
  script generation ([#127][gh-127]); `yse.cancel_all` uses this tag.

Exceptions raised inside the callback are routed through the error
callback ([#125][gh-125]). The subscription remains active after an
exception — one bad publish does not deregister the listener.

```python
def on_trigger(v):
    yse.send("kick.gate", v)

h = yse.on("section_a.trigger", on_trigger)
```

### `yse.unsubscribe(handle)`

```python
yse.unsubscribe(handle: int) -> None
```

Deregister a subscription previously returned by `yse.on` (or
implicitly by `yse.latch`).

- **Returns:** `None`.
- **Idempotent:** unsubscribing an already-unsubscribed handle is a
  no-op. No exception.
- **Unknown handle:** an `int` that was never returned by `yse.on` is
  also a no-op (this keeps composer scripts robust against stale
  handles surviving across hot-reloads).
- **Thread:** script thread, takes the bus's shared mutex.

```python
h = yse.on("synth1.lfo", lambda v: ...)
yse.unsubscribe(h)
```

### `yse.latch(name)`

```python
yse.latch(name: str) -> Latch
```

Returns a small Python helper that subscribes to `name` and caches
the most recent value.

```python
class Latch:
    value: Any  # last received value, or None
    def unsubscribe(self) -> None: ...
```

- **Returns:** a `Latch` instance. `latch.value` is `None` until the
  first publish.
- **Lifetime:** the underlying subscription is held for as long as the
  `Latch` is reachable from Python. When the object is collected, its
  `__del__` calls `unsubscribe`. Composers can also call
  `latch.unsubscribe()` explicitly to release early.
- **Thread:** subscription registered on script thread; value updated
  on script thread when the bus drains.
- **Generation:** like `yse.on`, the latched handle is tagged with the
  current generation and will be torn down by
  [`yse.cancel_all`](#ysecancel_all) if older than the current
  generation.

`latch` is sugar on top of `on`. The composer-side rationale: read-back
of bus values is opt-in. Subscribing to *every* address by default
would couple the script's working set to bus traffic in ways that
surprise.

```python
cutoff = yse.latch("synth1.cutoff")
# ... later, on the next tick after a publish ...
if cutoff.value is not None and cutoff.value > 1000:
    yse.send("alarm", 1)
```

### `yse.schedule(ticks, fn, *args, **kwargs)`

```python
yse.schedule(ticks: int, fn: Callable, *args, **kwargs) -> int
```

Schedule `fn(*args, **kwargs)` to fire `ticks` engine update ticks
from now, on the script thread.

- **Returns:** a schedule handle (a Python `int`). Cancellable via
  [`yse.cancel_all`](#ysecancel_all) — there is no per-handle
  scheduled-cancel primitive.
- **Raises:** `ValueError` if `ticks < 0`. `TypeError` if `fn` is not
  callable.
- **`ticks == 0`** fires on the *next* `system::update()` tick (not
  the current one). This matches the script-thread cadence: the
  current tick has already been scheduled by the time the script
  reaches `yse.schedule`.
- **Thread:** callback fires on the script thread with the GIL held.
- **Generation:** tagged with the current generation, like `yse.on`.
- **Exceptions** inside the scheduled `fn` route through the error
  callback. The schedule does not re-arm.

```python
def kick():
    yse.send("kick.trigger", 1)

yse.schedule(4, kick)              # 4 ticks from now
yse.schedule(8, kick)              # 8 ticks from now
```

### `yse.tick`

```python
yse.tick: int  # read-only attribute
```

The monotonic engine tick counter. Increments by 1 each
`system::update()`.

- **Type:** `int`.
- **Reset:** reset to 0 on `System::init`. Survives across
  `yse_run_script` calls.
- **Not a function.** Reading `yse.tick` is a cheap attribute access.

```python
print(f"now at tick {yse.tick}")
yse.schedule(4, lambda: print(f"4 ticks later: {yse.tick}"))
```

### `yse.cancel_all()`

```python
yse.cancel_all() -> None
```

Deregister every subscription and pending schedule from a *strictly
older* generation than the current one. See
[hot-reload semantics](#hot-reload-semantics) for the generation
mechanism.

- **Returns:** `None`.
- **Does not cancel** subscriptions or schedules created in the same
  script execution that calls `cancel_all`. This is intentional: a
  script that prefixes itself with `cancel_all()` to wipe previous
  state still wants its own subsequent registrations to survive.
- **Thread:** script thread.

```python
yse.cancel_all()  # wipe everything from the previous evaluation
# ... rest of script ...
```

---

## Error model

There is exactly one error sink: the C API error callback installed
via `yse_set_script_error_callback` ([#125][gh-125]). All error paths
funnel there.

### What triggers the callback

1. **Syntax error** in the script source passed to `yse_run_script`.
   `SyntaxError` traceback, including line and caret.
2. **Uncaught exception** during top-level script execution.
3. **Uncaught exception** inside a `yse.on` callback.
4. **Uncaught exception** inside a `yse.schedule` callback.
5. **Compile-time off-state:** when libYSE is built without
   `YSE_ENABLE_PYTHON`, *every* call to `yse_run_script` invokes the
   callback synchronously with the fixed string
   `"YSE compiled without YSE_ENABLE_PYTHON"`. See [#125][gh-125] for
   the OFF stub contract.

### Traceback format

The runtime calls Python's
`traceback.format_exception(type, value, tb)` and concatenates the
result. The string the host receives is exactly what `traceback`
produces — no engine-side prefix, no JSON wrapper, no escaping.

The script source passed to `yse_run_script` is compiled with the
filename `"<script>"`, so traceback lines read:

```
Traceback (most recent call last):
  File "<script>", line 3, in <module>
    yse.send("kick.volume", {"too": "complex"})
TypeError: yse.send: value must be int, float, str, or list of numbers
```

For `SyntaxError`, Python's own formatting includes the caret
position:

```
  File "<script>", line 2
    def f(:
          ^
SyntaxError: invalid syntax
```

### What happens if no callback is registered

The traceback is **dropped silently**. There is no fallback log file,
no `stderr` print, no buffered "last error" the host can poll later.
This is intentional:

- The `yse_last_script_error()` polling API from the original draft is
  *not* implemented. The epic moved to callback-only delivery
  ([#125][gh-125]).
- A composer running libYSE without a host-side handler is, by
  definition, debugging without telemetry. That is their choice.
- `print()` inside a script writes to the embedded interpreter's
  stderr, which is *not* captured. Capturing it is a deferred concern
  ([#125][gh-125] non-goals).

### What does *not* trigger the callback

- A publish to an unsubscribed name. The bus silently drops it
  ([#121][gh-121]).
- A type mismatch on an engine-owned address (e.g. string to
  `sound.kick.volume`). Silently ignored on the C++ side, not surfaced
  to scripts. (A duplicate *producer* name is the one engine-side case
  that does log — see [collision policy](#collision-policy).)
- `yse.unsubscribe` with a stale handle. No-op.

### Callback lifecycle on exception

After an exception is captured and the callback dispatched, the
interpreter state is cleared (`PyErr_Clear`) and the script thread
remains alive. Subsequent `yse_run_script` calls work normally.
Subscriptions installed *before* the failing line remain active;
subscriptions the failing script intended to install *after* the
failing line are not installed.

---

## Threading model

Three threads matter for the DSL:

1. **Audio thread.** Owned by PortAudio (or platform equivalent). The
   GIL is *never* acquired here. The audio thread can `publish` to the
   bus (the bus enqueues into a lock-free SPSC queue per
   [#121][gh-121]) but the DSL does not run on it.
2. **Main thread.** Owned by the host (Phi, demos, dart-yse host
   process). Calls `system::update()` once per "tick". Drains the bus
   queue, dispatches to subscribers, then signals the script thread.
3. **Script thread.** Owned by the engine when `YSE_ENABLE_PYTHON=ON`.
   Holds the GIL on wake. Started in `System::init`, joined in
   `System::close` ([#124][gh-124]).

### Cadence

The script thread wakes:

- When `system::update()` finishes its bus drain — exactly one wake per
  tick.
- When `yse_run_script` enqueues new source.

On wake, the script thread:

1. Acquires the GIL.
2. Drains any pending `EvalRequest`s — each is `exec`-ed in the
   `__main__` namespace.
3. Drains the bus subscriber queue for callbacks owned by this script
   ([`yse.on`](#yseonname-callback) handlers and
   [`yse.schedule`](#ysescheduleticks-fn-args-kwargs) firings that
   matured).
4. Releases the GIL and sleeps until the next wake.

This means: **every DSL callback runs once per tick, in batch, on the
script thread.** A `yse.send` from C++ on the audio thread at tick `t`
is delivered to a Python `yse.on` handler at tick `t+1` (the next
update drains the audio queue, the script thread wakes after that
drain).

### Forbidden patterns

- **Audio-thread scripting.** There is no API to run Python from the
  audio callback. Don't try to add one; it would require draining the
  GIL on every audio tick.
- **Cross-thread Python objects.** Composers should not hold Python
  references in C++. The bindings ([#126][gh-126]) do not hand out
  Python handles to the C++ side; values cross as `BusValue`, not as
  `PyObject*`.
- **Blocking I/O inside callbacks.** Allowed in the sense that the GIL
  permits it, but a long-running callback delays every other
  callback's tick. The script thread is the wrong place for
  `urllib.request.urlopen(...)`. The performer is trusted to know
  this; the runtime does not enforce it.

### GIL discipline

- The script thread holds the GIL only while running script code.
- `yse_run_script` does **not** acquire the GIL — it enqueues a copy
  of the source onto the inbound queue and returns.
- The error-callback dispatch (from `system::update`) runs on the main
  thread, with the GIL released. The traceback string is already
  formatted to UTF-8 bytes before the callback sees it; the callback
  body must not touch Python.

---

## Hot-reload semantics

A *script evaluation* is one call to `yse_run_script`. The DSL is
designed around the assumption that performers re-evaluate scripts
constantly while playing.

### Namespace persistence

Every evaluation runs in the same `__main__` namespace. Module-level
bindings — functions, classes, lambdas, mutable state — persist across
evaluations:

```python
# Evaluation 1
counter = 0
def step():
    global counter
    counter += 1
yse.on("clock", lambda v: step())

# Evaluation 2 (some time later)
print(counter)  # whatever it has counted up to since eval 1
```

This is plain CPython behaviour. No special handling.

### What `yse.cancel_all` clears

`yse.cancel_all()` clears, **for every generation strictly older than
the current one**:

- subscriptions created via `yse.on` (and the implicit subscription
  inside `yse.latch`)
- pending schedules from `yse.schedule`

That is the *only* thing it clears. It does **not** touch:

- Python global variables (`counter` in the example above survives).
- Imported modules.
- Engine-side state (sounds, channels, patchers, bus subscribers
  owned by the engine).
- Subscriptions or schedules from the *current* generation, even if
  `cancel_all` is called after they were installed in the same
  evaluation.

The generation counter increments once per `yse_run_script`
([#127][gh-127]). Every `yse.on` / `yse.schedule` handle is tagged
with the generation at creation. `cancel_all` walks the registries and
removes anything with `generation < currentGeneration`.

### Recommended editor pattern

The Phi editor's "Evaluate" button can be configured to prefix every
submission:

```python
yse.cancel_all()
# ... rest of script ...
```

This is **not** automatic. The DSL deliberately does not call
`cancel_all` on the composer's behalf. Reasons:

- A performer may want to *layer* — evaluate a new script that adds to
  what previous evaluations installed. Auto-cancelling would prevent
  that.
- The explicit call is one line and self-documenting. Hidden cleanup
  is a debugging hazard during live performance.

For composers who want layering by default but occasional reset, the
optional `yse.fresh_scope()` context manager ([#127][gh-127]) is the
sugar:

```python
with yse.fresh_scope():
    yse.on("trigger", go)
```

Equivalent to `yse.cancel_all()` at the top of the block.

### Surviving objects

Anything Python lets survive, survives:

- A thread spawned by `threading.Thread` in evaluation 1 keeps running.
- A file opened in evaluation 1 stays open.
- A coroutine created in evaluation 1 stays pending (though there's no
  asyncio loop to drive it).

The DSL provides no facility to enumerate or kill these. The
recommended discipline: do not spawn auxiliary threads, do not hold
open handles. Use the bus and schedule for everything.

### Process-level reset

To truly start fresh: call `System::close()` and `System::init()` on
the host side. This finalizes and re-initializes the interpreter; the
new `__main__` is empty.

---

## Worked examples

### Example 1 — minimum viable patch

```python
yse.cancel_all()  # editor prefix

# Each clock pulse → fire the kick.
def on_clock(v):
    yse.send("sound.kick.volume", 1.0)
    yse.send("kick.trigger", 1)

yse.on("clock", on_clock)
```

### Example 2 — latched read-back driving a schedule

```python
yse.cancel_all()

cutoff = yse.latch("synth1.cutoff")

def sweep():
    if cutoff.value is None:
        yse.schedule(1, sweep)  # not ready yet, try again next tick
        return
    yse.send("synth1.cutoff", cutoff.value * 1.01)
    yse.schedule(1, sweep)

sweep()
```

### Example 3 — cross-patcher routing

Assumes patcher `lead` has a `gReceive("trigger")`, patcher `bass`
has a `gSend("trigger")`. With [#122][gh-122] in place, a `gSend` in
`bass` publishes to `patcher.bass.trigger`; a `gReceive` in `lead`
subscribes to `patcher.lead.trigger`. To bridge them from script:

```python
yse.cancel_all()

# When bass fires, also fire lead.
yse.on("patcher.bass.trigger", lambda v: yse.send("patcher.lead.trigger", v))
```

### Example 4 — error handling

The script:

```python
yse.send("kick.volume", {"too": "complex"})  # not a bus value type
```

Produces, on the host's error callback:

```
Traceback (most recent call last):
  File "<script>", line 1, in <module>
    yse.send("kick.volume", {"too": "complex"})
TypeError: yse.send: value must be int, float, str, or list of numbers
```

(A `str` *is* a valid bus value — `yse.send("kick.volume", "loud")` publishes
the string and the engine-side `volume` setter ignores the type mismatch
silently, per the [engine-property mapping](#mapping-to-engine-properties). The
example above uses a `dict`, which is genuinely outside the
[value type table](#value-types).)

The interpreter state is cleared; subsequent `yse_run_script` calls
work normally.

### Example 5 — generation tagging across reloads

Evaluation 1:

```python
def go(v):
    yse.send("kick.trigger", v)
yse.on("clock", go)
```

Evaluation 2, without `cancel_all`:

```python
def go(v):
    yse.send("snare.trigger", v)
yse.on("clock", go)
```

Now both subscribers fire on every `clock` publish. The old `go` is
still bound (the closure captured by `yse.on` holds it), and the new
`go` is bound too. The composer wanted the *replacement* behaviour;
they should have written:

```python
yse.cancel_all()  # tear down everything from evaluation 1
def go(v):
    yse.send("snare.trigger", v)
yse.on("clock", go)
```

---

## Cross-references

This document is the source of truth for the surface. The implementing
issues each cite it:

- [#121][gh-121] — Global named bus primitive (substrate for every
  address).
- [#122][gh-122] — Patcher `gSend` / `gReceive` on the bus (the
  `patcher.<name>.<slot>` prefix).
- [#123][gh-123] — Sound and channel properties on the bus (the
  `sound.<name>.<prop>` and `channel.<name>.<prop>` prefixes).
- [#124][gh-124] — CPython embedding infrastructure (script thread,
  GIL, queues, frozen stdlib).
- [#125][gh-125] — C API (`yse_run_script`, error callback).
- [#126][gh-126] — `yse` Python module + DSL implementation (the
  primitives in this document).
- [#127][gh-127] — Hot-reload cleanup + scheduling lifecycle
  (generation counter, `cancel_all`, `fresh_scope`).
- [#388][gh-388] — Synth note/controller events on the bus (the
  `synth.<name>.<event>` prefix).

When implementing any of the above, treat this document as the
authority on user-visible shape. If implementation makes one of these
sections wrong, update this document in the same PR.

[gh-119]: https://github.com/yvanvds/yse-soundengine/issues/119
[gh-120]: https://github.com/yvanvds/yse-soundengine/issues/120
[gh-121]: https://github.com/yvanvds/yse-soundengine/issues/121
[gh-122]: https://github.com/yvanvds/yse-soundengine/issues/122
[gh-123]: https://github.com/yvanvds/yse-soundengine/issues/123
[gh-124]: https://github.com/yvanvds/yse-soundengine/issues/124
[gh-125]: https://github.com/yvanvds/yse-soundengine/issues/125
[gh-126]: https://github.com/yvanvds/yse-soundengine/issues/126
[gh-127]: https://github.com/yvanvds/yse-soundengine/issues/127
[gh-388]: https://github.com/yvanvds/yse-soundengine/issues/388
