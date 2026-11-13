# Sample buffers

The shared, named, multichannel sample buffer behind `~buffer` and the sampling family of the MSP
port: ownership, lifetime, reclamation and naming. Decided in design gate
[#1039](https://github.com/yvanvds/yse-soundengine/issues/1039) (epic
[#1034](https://github.com/yvanvds/yse-soundengine/issues/1034)). The API is documented in
[dsp/sampling/sampleBuffer.hpp](../../YseEngine/dsp/sampling/sampleBuffer.hpp), the wrapper in
[patcher/sampling/dBuffer.h](../../YseEngine/patcher/sampling/dBuffer.h).

## The pieces

| Class | Role | Threads |
| --- | --- | --- |
| `DSP::sampleBuffer` | channels × frames of `Flt`, plus a sample rate | edits: control and background; state queries: any |
| `DSP::sampleBufferRegistry` | process-wide names → buffers, and ids → buffers | control and background (one mutex) |
| `DSP::sampleBufferReader` | one reader's claim on a buffer | bind: control; `pin`: audio |
| `DSP::sampleView` | one block's read access to a pinned storage | the thread that pinned it |
| `DSP::sampleBufferWriter` | one writer's claim on a buffer, writing in place (#1144) | bind: control; `pin`: one thread at a time |
| `DSP::sampleWriteView` | one block's read and write access to a pinned storage | the thread that pinned it |
| `~buffer` (`PATCHER::dBuffer`) | the patcher face: arguments and messages | messages: any; work: background |

All of it is a public C++ API in `yse.hpp`. A host can create `drums`, load it, and have a patch's
`~groove drums` play it, or the reverse, without a patcher being involved on the host side.

## Ownership and lifetime

A buffer is held by `std::shared_ptr`. Everything that uses one holds one: the host, each `~buffer`,
each sampling object's `sampleBufferReader`. The registry holds names **weakly**, the rule
[`namedStore.h`](../../YseEngine/patcher/namedStore.h) set for `.coll` / `.dict` / `.array`: a buffer
lives while something holds it, and its name is free again after the last holder lets go. Strong
ownership would make every name a patch ever spelled immortal.

`acquire(name)` creates an empty buffer (one channel, no frames) when nobody holds the name, so a
reader that is created before its `~buffer` gets the same buffer the `~buffer` will later fill. A
`~buffer` with only a name adopts what the buffer holds; its arguments (size, channels, file) are
applied when it is created.

The audio thread never copies or drops a `shared_ptr<sampleBuffer>`. Readers bind on the control
thread and keep their pointer for life; a sampling object whose name changes is rebuilt (its
`PARM_PARSE` callbacks make `ParamsNeedRebuild()` true), so its binding never changes under a
running block. The last reference to a buffer is therefore dropped on a control thread, or on the
background pool when the patcher's epoch reclaimer frees the last object holding it, or in a
`~buffer` task (see below). None of those is the audio thread.

## Naming: one flat, process-wide namespace

Names are **not** scoped to the patcher (`patcher.<name>.<slot>`, #893), unlike `.coll`, `.dict`,
`.array` and `.s` / `.r`. Three reasons:

1. **The host has to address them.** The two-layer requirement is that C++ and patches share
   buffers. A host loading `drums` should not have to know which patcher will play it, or that
   patcher's name.
2. **`~poly` voices** ([#1040](https://github.com/yvanvds/yse-soundengine/issues/1040)) are
   separate patcher instances. A sampler voice must play the buffer its parent loaded; scoping by
   patcher would give every voice its own empty buffer.
3. **It is Max's arrangement.** `buffer~` names are global across patchers, and a sample buffer is
   an asset loaded once and played by many patches.

The cost is that two unrelated patches that both say `~buffer drums` share it. That is what Max
does too, and a patch that wants a private buffer leaves the name out (or picks a unique one). A
patcher rename does not touch buffers, so `~buffer` needs no `OnPatcherRenamed` hook.

The registry keeps its own map, separate from `namedStore.h`'s per-type maps: a buffer is a public
DSP object that exists without a patcher, and it needs an id lookup for background tasks.

## Mutation and reclamation

The hard part: readers (`~play`, `~groove`) run on the render thread; `read`, `size`, `clear` and
file loads happen elsewhere; readers must never see a freed or half-written buffer, and the audio
thread must never free one.

**Copy on write.** A buffer's samples live in an immutable `storage` block (planar: channel after
channel). Every change builds a new block on the calling thread — resize, file decode, or a copy of
the current block that the edit then changes — and publishes it with one `seq_cst` store to an
atomic pointer. A per-buffer mutex, taken only by control and background threads, serialises
edits, so two concurrent edits cannot lose each other. A block's shape never changes after it is
published, and only a `sampleBufferWriter` writes its samples. Copying costs memory and time in
proportion to the buffer, which is right for loading and editing and wrong for writing a few
samples at audio rate; see *Writers* below.

**Hazard pointers, one per reader.** A `sampleBufferReader` registers a hazard slot with its buffer
when it binds (control thread, may allocate). `pin()` on the audio thread loads the published
pointer, stores it in the slot, and re-loads the published pointer; if a change slipped in between
it goes round again. The returned `sampleView` clears the slot when it is destroyed. Pinning is
lock-free and allocation-free; it repeats only when an edit is published inside that window, and
edits come from control threads, so in practice it runs once.

**Retirement.** Publishing moves the old block to the buffer's retired list. `collect()` frees
every retired block that no hazard slot names. The ordering argument: the edit stores the new
pointer (`seq_cst`) before it scans the slots (`seq_cst`); a reader that ends up using the old
block stored it in its slot (`seq_cst`) before re-loading the published pointer and seeing it
unchanged, so that store precedes the edit's publish, and the scan sees it.

**When collection runs.** Every edit collects right after it publishes, and a reader that unbinds
collects. A block that a reader was holding at that moment waits: its buffer is put on the
registry's pending list, and `system::update()` calls `sampleBufferRegistry::collect()` on every
tick, which is the same control-thread tick that drains the named bus and the RT log. A reader
re-pins every block, so a held block is free one block after the edit. A host that runs without
`update()` calls `collect()` itself.

**Why not the alternatives.**
- *The patcher's epoch reclaimer* (`GraphState`, two blocks + pin count) is per patcher, but a
  buffer is shared by many patchers and by C++ code that has no patcher at all.
- *A reader count with a grace wait* (the callback-pair pattern in `midi/device.hpp`) needs the
  count to reach zero. With the render scheduler's worker threads (#859), many voices on several
  threads can read one buffer in overlapping blocks, so zero is never guaranteed; per-reader slots
  cannot starve.
- *`std::atomic<std::shared_ptr>`* is not lock-free on the toolchains we build with, and a reader
  dropping the last reference would free on the audio thread.

**Control-side readers** (a `write` to disk, `getSamples`, `copyFrom`) take a `shared_ptr` copy of
the current block under the mutex instead of a hazard slot. The retired list drops its reference
when no hazard holds the block; if a control-side reader still has it, that reader frees it.

## `~buffer`: every message is a background task

A message handler runs on whichever thread dispatched it, often the audio thread, and every
`~buffer` message allocates or touches the disk. So a handler only packs the message into a fixed,
trivially copyable record and hands it to the patcher's
[`fileScheduler`](../../YseEngine/patcher/io/fileScheduler.h) through a new third operation,
`RequestTask(target, tag, function, args, bytes)`: wait-free, no allocation, the record copied into
the slot. The background pool has one worker and a FIFO ring, so tasks run one at a time **in the
order they were sent** — `clear`, `fill 1`, `getsamps` happen in that order and the query sees both
edits. The completion comes back at the top of a later block through `DeliverFileResult`, inside
its own dispatch frame: a bang for a finished `read`, or a query's answer.

The task finds its buffer by **id** in the registry, never through the object: the object may have
been deleted by a live edit while the task ran (the same rule `fileScheduler` already has for its
jobs). The `shared_ptr` the lookup returns is dropped on the background thread.

`fileScheduler` already existed for this purpose, but its `READ` slots carry the file inline and are
capped at 128 KiB, which no audio file fits. `TASK` keeps everything else — the fixed table, the
snapshot re-resolution on delivery, the "refused and counted" rule — and lets the caller supply the
work. Limits inherited from it: `fileScheduler::CAPACITY` (4) messages in flight per patcher, and
results only while the patcher renders.

Creation arguments are the exception: the size and the file are applied synchronously in
`PARM_PARSE`, which is the control thread, so a patch that has just been loaded has its samples, as
in Max.

## Reading a buffer from a sampling object

The recipe the sampling issues (#1138–#1152) build on, also in the contributor guide:

1. Hold a `DSP::sampleBufferReader` member.
2. In `PARM_PARSE` (control thread), `reader.bind(DSP::sampleBufferRegistry::acquire(name))`.
   Register the parse callback so a name change rebuilds the object rather than rebinding a live
   one.
3. In `CALC()`, `DSP::sampleView view = reader.pin();` once, at the top. Read through `view` for the
   whole block; let it go at the end of `CALC()`. An empty view (no samples yet) outputs silence.
4. Play at the right speed: `view.sampleRate()` is the rate the samples are at, and the engine runs
   at `SAMPLERATE`. Do not cache either across blocks; a new file can change the first, an engine
   restart the second.
5. Never hold a view across blocks, and never pin the same reader from two threads at once: one
   reader is one claim.

The DSP class of a sampling object (`DSP::bufferPlayer`, `DSP::groovePlayer`, ...) takes a
`sampleView` (or the reader) per block rather than a `sampleBuffer`, so it stays a plain one-thread
class as the MSP conventions require.

## Reporting on a buffer from a control object

`~info` ([#1138](https://github.com/yvanvds/yse-soundengine/issues/1138)) reads a buffer's details,
not its samples, so it holds no reader. `sampleBuffer::info()` returns a `sampleBufferInfo`: the
shape and rate of the current block plus the last file read (path, channel count, and the
`sampleInstrument` — base note, detune, key and velocity range, gain, sustain and release loops —
from libsndfile's `SFC_GET_INSTRUMENT`, i.e. an AIFF `INST` or WAV `smpl` chunk), all taken under
the buffer mutex so they agree. The file details live beside `owner` under the mutex rather than in
the published block: no audio-thread reader needs them, and edits that rebuild the block
(`setSize`, `crop`) keep them for free. A successful `read` / `replace` sets them, `copyFrom` copies
them, nothing else touches them.

Because `info()` locks and copies a string, a `~info` bang is a `fileScheduler` task like a
`~buffer` query: the record carries the buffer **name**, the task resolves it with
`sampleBufferRegistry::find` on the background pool, and the answer comes back in the same record.
Resolving by name at each bang (instead of holding the buffer) lets `set <name>` work from the audio
thread without a lookup there, and finds a buffer created after the object.

## Writers: in place, with relaxed atomics (#1144)

`~poke` ([#1144](https://github.com/yvanvds/yse-soundengine/issues/1144)), `~peek`'s writes and
`~record` ([#1143](https://github.com/yvanvds/yse-soundengine/issues/1143)) write samples at audio
rate, where copy on write is the wrong tool. The constraints #1039 set: the audio thread never
allocates or frees a block and never takes the buffer mutex, and a writer must not race the
control-side copies.

**Decision: write the published block in place.** `DSP::sampleBufferWriter` is the reader's twin:
bind on a control thread (it attaches a hazard slot of its own), `pin()` once per block on any one
thread, and the `DSP::sampleWriteView` it returns has `set` / `write` next to `at` / `read`. Pinning
is the reader's hazard claim, so a block a writer holds is never freed under it. The block's shape
stays immutable; only its samples change, so nothing about publishing or retirement moves, and
`version()` does not count writes (a write is not a new file).

**Why the copies do not race.** Every write is a relaxed atomic store
(`detail::StoreSample`: `__atomic_store` on GCC/Clang, an aligned volatile store on MSVC; C++17 has
no `atomic_ref`), and every read of a published block that can overlap one is a relaxed atomic
load: `sampleView::at` / `linear`, the write view's reads, and every control-side copy (`edit`,
`setSize`, `crop`, `copyFrom`, `getSamples`, `write` to disk), which now copy through
`CopySamples` rather than `memcpy`. On every target both are a plain load or store, so readers pay
nothing; the copies lose vectorisation, which is control-thread time. TSan sees well-defined
atomics rather than a race. A reader that walks `sampleView::channel()` with plain loads is fine
while every writer of the buffer runs on its thread (one patcher's objects do), and is a data race
when a writer on another render thread writes the same block at the same moment; `at` / `linear`
are the cross-thread-safe reads.

**What an edit made during writes means.** An edit copies the block, changes the copy and
publishes it. Writes that land in the old block after the copy passed them are not in the new one,
and a writer that pinned the old block before the publish finishes its block there; the next pin
writes the new block. So an edit made while a writer writes wins. The alternative, edits that wait
for writers, would make a control thread spin on the audio thread or the audio thread wait on an
edit; neither is worth it for what is a user editing a buffer that is being recorded into.

**Why not a block of the writer's own.** A writer that publishes its own block has to copy the
whole buffer each block (readers must see the rest of it), or keep two blocks in step, which is
the same copy. In place costs nothing per block.

**The claim is one thread's.** One writer is one claim, as one reader is. `~poke` pins one writer
in `CALC()` and a second, guarded by its `exchange(true)` flag, for a list written from a message;
`~peek` has only the guarded one, since everything it does arrives as messages, on any thread,
and it answers at once (so it works with the audio stopped). The guard's loser drops its message,
the stateful-object rule.

The stress test in `Tests/dsp/test_sampleBuffer.cpp` ("a writer and a reader race edits safely")
runs a writer, a reader, an editor that resizes, fills and copies out, and a collector, and is
meant to be run under ASan and TSan.

**For `~record` (#1143)**: bind a `sampleBufferWriter` in `PARM_PARSE`, pin it at the top of
`CALC()`, and `write(channel, position, block, length)` each block, wrapping at the end of the
buffer yourself. Nothing else is needed from the store. As built, `DSP::bufferRecorder` writes
one stretch per channel up to the end point and wraps there. Its loop points are a
`DSP::sampleRegion` (`dsp/sampling/sampleRegion.hpp`): milliseconds turned into frames each block
at the pinned view's rate and length, end 0 meaning the buffer's end, with `phase(frame)` for a
0-1 sync output. The looping players (`~groove`, `~phasegroove`) and `~play` take their loop
points and sync the same way.

**For `~play` (#1140)**: `DSP::bufferPlayer` keeps its points in milliseconds, not a
`sampleRegion`, because Max's `start <start> <end>` plays backward when the end comes first and
`start 500 0` means "down to 0", so an end of 0 cannot mean the buffer's end there (the end of the
buffer is an end below 0). It reads with `sampleView::cubic`, the `interpolate4` kernel on a pinned
view with positions held inside the buffer, and loops through `DSP::loopCrossfade`
(`dsp/sampling/loopCrossfade.hpp`): Max's `loopinterp` / `interptime`, the last `fade` frames
before the jump mixed with the frames one loop length away, in either direction. Both are meant
for `~groove` and `~phasegroove`.

**For `~groove` (#1141)**: `DSP::groovePlayer` takes its loop points as a `sampleRegion` (Max's
`loopend 0` is the buffer's end) made per block from the pinned view, and its sync output is
`sampleRegion::phase` over the loop. The speed is a signal, so the position moves by
`speed * bufferRate / engineRate` frames a sample, kept in ms between blocks; loop `ON` jumps by the
overshoot (crossfaded through `loopCrossfade` in the direction it plays) and `PENDULUM` reflects off
the loop points. The output buffers and the channel-mixing read are `DSP::playerOutputs`
(`dsp/sampling/playerOutputs.hpp`), factored out of `bufferPlayer` so every player shares them.

**For `~phasegroove` (#1142)**: Max's `phasegroove~` turns a ramp into the speed of a `groove~` it
finds through the cord. A YSE object does not look at what it is connected to, so here the ramp
drives the player: `groovePlayer::phase(view, phases)`, a second way into `DSP::groovePlayer` next
to `operator()`, reads frame `low + phase * (high - low)` of the same `sampleRegion` loop, through
the same `playerOutputs` and `loopCrossfade`, with sync the wrapped phase. It takes no speed, so
neither `Wrap` nor `OffTheEnd` applies: the ramp says where to read and the position needs no
integrating, so it cannot drift from the tempo. The crossfade's direction comes from the ramp's
slope the short way round, so a wrap from 1 to 0 keeps it rising.

## Rebinding by name: one mailbox for the family (#1256)

`set <name>` on `~peek` and `~poke`, and on the players, `~index` and `~record` after them,
cannot bind in the message handler: `acquire` locks the registry and `bind` attaches a hazard
slot under the buffer mutex, and the message may be on the audio thread. `~cycle` solved this for
its table in #1192; #1256 lifted the mechanism into `PATCHER::bufferBinding<Binding>`
(`patcher/sampling/bufferBinding.h`), templated on what is bound (`sampleBufferReader`,
`sampleBufferWriter`, or a struct of several with a `bind` that binds each).

**Decision: bind in a task, hand over through a one-slot mailbox.** `Request` / `HandleSet` copy
the name into a fixed record and submit it as a `fileScheduler` `TASK` (wait-free, no allocation).
On the background pool the task makes and binds a new binding and exchanges it into `incoming`,
freeing any unread one there (the last request wins). The thread that pins calls `Take`: one
exchange swaps it into `current`, the old one goes to `retired`, and a release task frees it. A
take waits while `retired` is still full, so the taking thread never frees and never waits. The
mailbox is found by id in a process-wide table, not through the object, so a task in flight
outlives the object's deletion. The core is type-erased (`void*` plus create / destroy function
pointers) in `bufferBinding.cpp`; the template only casts.

**Who takes.** `~poke` takes at the top of `CALC()` while holding its list guard, since the list
writes through the second writer of the same binding; a list holding the guard defers the take
one block. `~peek` has no `CALC()` and takes in its index and list handlers under its guard, so a
rebind reaches it with the audio stopped (the scheduler slots themselves come back only when the
patcher renders). `~index` (#1145) takes at the top of `CALC()` with no guard: no message of
its reads the buffer, so its reader belongs to the audio thread alone. `~table` (#1148) takes the same way; its
messages that need the table (a number, `bang`, `goto`, `next`, `prev`) are queued as commands
and applied after the take, so they too stay on the audio thread, and Max's `name` attribute
message is a second spelling of `set`. `~lookup` (#1147) takes the same way as `~index`: its
numbers and messages only store atomics that `CALC()` hands to `DSP::transferLookup`. `~cycle` keeps its own copy: its record also carries an offset and a size that
are merged across superseded requests, which the generic mailbox does not have.

A rebind is not saved: `DumpJSON` writes the creation argument, as for `~cycle`.

## Not ported from Max

`set` / `name` (renaming from a message would look a name up on the audio thread; change the
argument instead), the display window (`open`, `wclose`, `bang`), `import` / `importreplace` (`read`
already decodes every format libsndfile does), `readraw`, `samptype`, `replacechannel`, `trim`,
`clearlow`, `enumerate`, `printmodtime`, `fill sinc`, and the file-format attributes (`write`
follows the message or the file's extension). Queries are `get<attribute>` messages with the
answer on a second outlet.
