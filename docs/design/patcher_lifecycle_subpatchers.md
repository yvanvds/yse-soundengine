# Patcher load/teardown passes and subpatchers

The load and teardown passes and the subpatcher model (flat storage, nested addressing, signal
crossing). User-facing behaviour is in
[documentation/source/patcher/subpatchers.rst](../../documentation/source/patcher/subpatchers.rst).
Moved out of PROJECT_OVERVIEW.md by issue #890.

**Patcher load and teardown passes.** A patcher tells its objects when the patch around them begins
and ends, through two symmetric virtuals on `pObject` that default to nothing, so only the objects
that need them pay for them. `Loadbang(THREAD)` (issue
[#547](https://github.com/yvanvds/yse-soundengine/issues/547)) is driven by
`patcherImplementation::LoadbangObjects` as the **last** step of `ParseJSON` — after every object
has been created, every cord restored, and the compiled `GraphState` published with the single
atomic swap of issue [#228](https://github.com/yvanvds/yse-soundengine/issues/228) — because that is
the only moment at which "loading finished" is true: anything fired earlier travels down cords that
do not exist yet, or exist for only part of the patch. `Teardown(THREAD)` (issue
[#758](https://github.com/yvanvds/yse-soundengine/issues/758)) is its mirror, driven by
`TeardownObjects` as the **first** step of `Clear()` / `DeleteObject`, while every cord is still
connected, so an object holding something sounding outside the patch can release it through its own
outlets. Both run on the control thread, dispatched with `T_GUI`, over a snapshot of the objects
taken under the patcher's mutex but dispatched **outside** it — a send runs the whole subgraph
behind the object's outlets, and that subgraph may hold a `.forward`, `.qlist`, `.bag` or `.mtr`,
every one of which calls `PassBang`/`PassData`, which take that same (non-recursive) mutex.
`.loadbang` and `.loadmess` are the load hook's only users; the load pass visits only the objects
*that* parse created, and an object built live through `CreateObject` never receives one — its inlet
is the manual trigger, which is Max's own answer for the same case.

**Subpatchers: flat storage, nested addressing.** `patcher` (a subpatcher inside another patch),
`.inlet` and `.outlet` (its boundary) land in issue
[#545](https://github.com/yvanvds/yse-soundengine/issues/545), and the model is the whole of the
feature. A subpatcher's contents are **not** a second graph: every object in a patch, top level or
nested twenty deep, lives in the one `patcherImplementation::objects` map, takes a graph id from the
one id space, and is compiled into the one `GraphState`. Nesting is a single control-thread
annotation, `pObject::Container()`, plus one `"container"` key in the serialised object (written
only when there is one, so a patch with no subpatchers serialises byte for byte as before). The
issue asked whether the nested graph should be flattened at publish time or kept genuinely nested;
this is the stronger answer — it is never unflat, so there is no flattening step and no second
representation to keep in step. Three consequences follow and each is a test: the audio thread's
traversal cost does not grow with nesting depth (a subpatcher object appears in no edge, no
start-point list and no poller list); an edit *inside* a subpatcher is an ordinary edit through the
same single atomic `GraphState` swap as a top-level one
([#226](https://github.com/yvanvds/yse-soundengine/issues/226) /
[#227](https://github.com/yvanvds/yse-soundengine/issues/227) /
[#228](https://github.com/yvanvds/yse-soundengine/issues/228)); and nothing about nesting is read on
the audio thread at all. The façade owns **no pins** — `Connect`/`Disconnect` resolve "inlet *N* of
a subpatcher" to the boundary object inside it whose index is *N* and record an ordinary cord
straight to it, so the compiled graph contains no subpatchers and every downstream mechanism (graph
compiler, reclaimer, serialiser, `UnwireFromPeers`) needs to know nothing about them. That is a
correctness requirement rather than an economy: a boundary that were a real pin vector would have to
be resized on an object the render is concurrently walking, which is exactly what issue
[#234](https://github.com/yvanvds/yse-soundengine/issues/234) exists because it cannot do.
`patcher::SubpatcherInlets` / `SubpatcherOutlets` report the boundary shape instead, mirrored as
`yse_patcher_subpatcher_inlets` / `_outlets`; membership is set with `patcher::SetContainer`
(`yse_patcher_set_container`), which refuses a container that is not a `patcher` object and refuses
any move that would make containment cyclic — `DeleteObject`'s subtree walk would otherwise not
terminate. Lifetime and the two lifecycle passes above extend to nesting without a special case:
deleting a subpatcher deletes its contents transitively, with `Teardown` over the whole subtree
before any of it is unwired, and a nested `.loadbang` fires in the same single post-publish pass as
the top level in no defined order relative to it, because one atomic swap installs every level at
once.

**Signal crossing at a subpatcher boundary.** `~inlet` and `~outlet` (issue
[#764](https://github.com/yvanvds/yse-soundengine/issues/764)) are the audio-rate half of that
boundary, and they change nothing below the resolution above. **What the render does at the boundary
is forward a pointer**: the patcher's signal path already passes buffers *by address* — a DSP object
computes into a buffer it owns and hands the address down the cord with `outlet::SendBuffer`, and
every reader stores that address and reads through it — so a pass-through has nothing to copy.
`Calculate()` sends on the exact `DSP::buffer*` that arrived, so the object on the far side of the
boundary is handed the very buffer the object on the near side produced. A crossing therefore costs
one `SetBuffer`, one `Calculate()` and one `SendBuffer` fan-out: no buffer copy, no extra buffer
allocated, no level of indirection added to the audio path, and nothing that scales with block size
or nesting depth. The graph compiler needed no change at all — a boundary cord compiles to a plain
DSP edge and `IsDSPStartPoint` / `HasActiveDSPConnection` place the boundary objects in the
traversal unaided. They are separate objects rather than a signal-capable `.inlet` because
`IsDSPObject()` decides start-point selection, `T_GUI` inlet dispatch and what every palette and
binding is told the object *is*, and one object cannot answer both honestly — the same split as `.+`
and `~+`. The **index space is shared per side**: a subpatcher has one set of inlet pins, "inlet 2"
is a single pin whose rate is a property of what sits behind it, and
`patcherImplementation::BoundarySide` is what makes the two lookups (`BoundaryChild`,
`BoundaryPinCount`) count both rates as one boundary — so `SubpatcherInlets` on a subpatcher holding
a `.inlet 0` beside a `~inlet 1` answers 2.
