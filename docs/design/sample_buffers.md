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
edits, so two concurrent edits cannot lose each other. A block is never written after it is
published, so a reader reads samples nobody is writing. Copying costs memory and time in
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

## Writers (deferred to their own issues)

`~record` ([#1143](https://github.com/yvanvds/yse-soundengine/issues/1143)) and `~poke`
([#1144](https://github.com/yvanvds/yse-soundengine/issues/1144)) write samples at audio rate.
Copy on write is the wrong tool for that, and a published block is immutable by this design. Those
issues add a writer of their own; the constraint this design puts on them:

- the audio thread still never allocates or frees a block, and still never takes the buffer mutex;
- control-side edits copy the published block, so a recorder writing into that same block at the
  same moment would race with the copy. The writer needs either a block of its own that it
  publishes, or control-side edits that wait for it. Which one is #1143's decision;
- the decision is theirs, but it must pass the same three-thread stress test
  (`Tests/dsp/test_sampleBuffer.cpp`, "a reader never sees freed or half-made storage") under ASan
  and TSan with a writer thread added.

## Not ported from Max

`set` / `name` (renaming from a message would look a name up on the audio thread; change the
argument instead), the display window (`open`, `wclose`, `bang`), `import` / `importreplace` (`read`
already decodes every format libsndfile does), `readraw`, `samptype`, `replacechannel`, `trim`,
`clearlow`, `enumerate`, `printmodtime`, `fill sinc`, and the file-format attributes (`write`
follows the message or the file's extension). Queries are `get<attribute>` messages with the
answer on a second outlet.
