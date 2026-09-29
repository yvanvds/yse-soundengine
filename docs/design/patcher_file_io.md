# Patcher file I/O

How file-reading and file-writing patcher objects avoid disk access on the audio callback.
User-facing behaviour is in
[documentation/source/patcher/files.rst](../../documentation/source/patcher/files.rst). Moved out of
PROJECT_OVERVIEW.md by issue #890.

**Patcher file I/O.** A patcher message handler runs on whichever thread dispatched the message —
in-patcher delivery dispatches on `T_DSP`, and `THREAD` is a *dispatch-semantics* tag rather than a
thread identity — so an object cannot detect that it is off the audio callback and must never open a
file from a `read` / `write` handler.
[patcher/io/fileScheduler.h](../../YseEngine/patcher/io/fileScheduler.h) is the shared plumbing that
makes those messages work anyway (issue
[#683](https://github.com/yvanvds/yse-soundengine/issues/683)), built to the same shape as the
deferred-message scheduler beside it: `RequestRead` / `RequestWrite` claim a slot from a fixed table
with one CAS per slot (wait-free, no allocation, refusals counted rather than logged), the disk work
runs on the background pool (`INTERNAL::Global().addSlowJob`) honouring the host's `IO()` virtual
file system for reads, and `DeliverComplete` hands the result to `pObject::DeliverFileResult` at the
top of `patcherImplementation::Calculate` inside a fresh `messageEventScope`. The background job
holds no `pObject` at all — only its own slot — and delivery re-resolves the target against the
block's pinned `GraphState` by pointer *and* `GetID()`, so deleting an object with a read in flight
is safe by construction. The slot table is half a megabyte, so a patcher builds one only when a
file-capable object joins it: such an object calls `pObject::EnableFileIO()` from its `SetParent`
override, and `pObject::FileIO()` returns null until then. `.coll` is the first consumer (`read`,
`readagain`, `write`, `writeagain` over Max's `<address>, <message>;` text format) and `.textfile`
the second ([#687](https://github.com/yvanvds/yse-soundengine/issues/687) — `read` / `write` over
plain lines, plus the `filename` creation argument read from its `SetParent` override, which is
where "when the object is loaded" happens for a patcher object); `.qlist` is the third
([#689](https://github.com/yvanvds/yse-soundengine/issues/689) — `read` / `write` over Max's
semicolon-terminated cue-list format, with no filename argument because a `.qlist` saves its cue
list with the patcher instead), `.mtr` is the fourth
([#691](https://github.com/yvanvds/yse-soundengine/issues/691) — `read` / `write` over Max's `track
<n>; <delta> <message>; end;` tape format, per-inlet as Max's are, and the one member whose "file
read done" outlet Max does not document at all, added anyway because Max's read is synchronous where
this one cannot be), and `.seq` is the fifth
([#692](https://github.com/yvanvds/yse-soundengine/issues/692) — the only *binary* consumer: `read`
takes standard MIDI files (format 0 and 1, merged by absolute tick) as well as Max's text form, and
`write [format]` produces one, with the `filename` creation argument read from `SetParent` as
`.textfile`'s is). Objects that grow a "file read done" outlet **append** it rather than inserting
it in Max's position, so no saved patch's cords shift. `.seq`'s parser is its own rather than
`YseEngine/midi/`'s, and deliberately: `MIDI::fileImpl` reads a filesystem *path* into
`std::vector`s it then sorts twice, where a completion is handed the bytes already and runs on the
audio thread. The format's *byte primitives* are shared, though —
[midi/midiBytes.hpp](../../YseEngine/midi/midiBytes.hpp), issue
[#698](https://github.com/yvanvds/yse-soundengine/issues/698) — since they are pure and
allocation-free and were only ever unshareable because they had been anonymous-namespace statics; it
is the parsers above them that stay apart.
