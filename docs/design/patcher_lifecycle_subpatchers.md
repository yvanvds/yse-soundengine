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
the audio thread except the mute words below, through pointers the snapshot holds. The façade owns **no pins** — `Connect`/`Disconnect` resolve "inlet *N* of
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

**Muting a subpatcher.** `~mute` (issue [#1137](https://github.com/yvanvds/yse-soundengine/issues/1137))
switches a subpatcher's signal objects off and on, and is the mechanism `~poly` voice muting is meant
to reuse ([#1040](https://github.com/yvanvds/yse-soundengine/issues/1040)). The state is a mute word
on the `gSubpatcher` — an `std::atomic<uint8_t>` with two bits, `MUTE_CONTENTS` (the objects directly
inside) and `MUTE_NESTED` (the objects in its nested subpatchers, any depth) — set with
`fetch_or`/`fetch_and` from whichever thread runs the `~mute`. A `~mute` cord resolves like any cord to
a subpatcher, to the boundary object behind the pin; `BuildGraph` records, per inlet graph id, which
subpatcher a boundary inlet stands for (`GraphState::inletSubpatcher`), and `outlet::SendSubpatchMute`
walks the outlet's targets in the snapshot the calling thread pins and sets the bits, delivering
nothing to the inlet. The render never walks the containment tree: `BuildGraph` gives every render
step (and every `~dac`) a slice of `GraphState::muteChain`, the mute words of the subpatchers around
it, innermost first, and the step is muted when the innermost has `MUTE_CONTENTS` or any outer one has
`MUTE_NESTED` — no load at all at the top level, one per enclosing subpatcher otherwise. A muted
step does not compute; `pObject::SendSilence` sends silence on its signal outlets instead, which each
target inlet writes into its own scratch buffer (`inlet::SetSilence`, counted like any arrival, so
readiness and fan-in sums are unchanged). That keeps what the subpatcher feeds outside running, and
makes a muted subpatcher output zeros rather than a stale block, so `~pass` needs no logic of its
own. Because the snapshot now carries containment, `SetContainer` publishes. The mute is runtime
state like the patcher's DSP switch (#1043): not saved, and a `SetParams` rebuild of the subpatcher
starts unmuted. `~begin` is a constant 0; the old "switch off the chain before a gate" behaviour is
not implemented, since a subpatcher and `~mute` cover it.

## Voices: `~poly` (issue #1040)

The design gate for MSP's `poly~`, `in`, `out`, `in~`, `out~` and `thispoly~`, which became `~poly`,
`.in`, `.out`, `~in`, `~out` and `~thispoly` (`patcher/genericObjects/dPoly`). The user guide is the
"Voices" section of [subpatchers.rst](../../documentation/source/patcher/subpatchers.rst).

**Instancing model: copies in the flat graph, not a nested graph per voice.** A `~poly` is a third
container type beside `patcher` and `~pfft`. What the user puts inside is the *definition* of a
voice (the template): it is saved, edited and addressed like any subpatcher's contents, but it does
not run. The patcher makes the voices itself (`SyncPolyVoicesUnlocked`, called first in every
`RebuildAndPublish`): per voice, a `polyVoice` root and a copy of every template object, created from
the registry with the template's parameters and saved state, wired to each other the way the
template is wired, and told their voice (`pObject::JoinVoice`). Copies live in
`patcherImplementation::polyInstances_`, not in `objects`: no handle, not saved, not enumerated. They
take graph ids from the one id space and are compiled into the one `GraphState`, each voice as a
domain of its own (`GraphState::polyDomain`, like a `~pfft`'s `spectralDomain`), so an edit is still
one atomic publish, a removed voice goes through the ordinary epoch reclaimer, the scheduler and file
deliveries validate copies like any object, and nothing new had to be proved for TSan. A nested
`GraphState` per voice was rejected: it would have needed its own publication, reclamation, id space,
scheduler and name scope per voice, a second copy of all the machinery #226–#228 and #963 made safe,
for nothing the flat form cannot do. The voices are rebuilt only when a fingerprint of the template
(types, parameters, containment and inner cords) or the voice count changes; a structural `SetParams`
on a template object always takes the rebuild path. Rebuilt voices lose their running state, like an
object a structural `SetParams` replaces, and get no `Teardown`; `Clear` and deleting the `~poly` do
run `Teardown` on them, and a load fires each voice's `Loadbang` (the template's never fires). A
`~poly` cannot be nested in a `~poly` or a `~pfft` (copies of copies are not made); a `patcher` or a
`~pfft` inside a voice is copied with it.

**Render scheduler (#856).** A patcher stays one task, as #856 decided. Voices are compiled as
separate domains whose renders touch only their own objects and their slot in the `~poly`'s sums, so
voices are the natural unit if parallelism inside a patcher is ever wanted; nothing here does that.

**Signal I/O.** The `~poly`'s pins come from its arguments (`inlets`, `outlets`, `messages`), as a
`~pfft`'s do, because a pin count derived from the contents would be a pin-count change on a live
object (#234). Inlet N carries a signal to every voice's `~in N` (a pointer, no copy) and messages to
`.in N`. In its step the `~poly` loads each voice's `~in`, walks the voice's compiled render order and
adds each `~out N` into signal outlet N. No cord crosses the boundary (`Connect` refuses it, and a
cord a later `SetContainer` puts across is left out of `outletTargets`); a `~dac` inside does
nothing.

**Message routing.** Handled in the `~poly`'s list handler, on any inlet, against the snapshot the
calling thread pins: `target` (0 all, N one, negative none), `note` and `midinote` (allocated),
`allnotesoff`, `mute`, `steal`, `setvalue`, and `list` as the escape; anything else goes to the
target. `.out N` sends the voice number on the last outlet, then the message on message outlet N.
Allocation is `DSP::voiceAllocator` (`dsp/routing/voiceAllocator`): the free-voice / oldest-steal
policy `.poly` had, extracted into a C++ class so `.poly`, `~poly` and C++ code share it (the issue's
recommendation: the allocator is a C++ class, instancing stays a patcher feature). A voice is busy
while its `~thispoly` says so (a number, or a signal such as an envelope) and from the moment it is
given a note until the end of the next block, so a chord sent at once spreads even when busy comes
from a signal. Allocator words take a test-and-set guard (dropped and counted on contention, as on
`.poly`); plain data does not.

**Muting.** A voice's root carries a mute word with `gSubpatcher`'s two bits; `~poly`'s `mute` and
`~thispoly`'s `mute` set both. The root's word is in every copy's `GraphState::muteChain` (#1137's
mechanism), and the `~poly` also skips a fully muted voice's whole render. A rebuild keeps each
voice's mute.

**Out-of-band delivery, and #756.** `pObject::VoiceEventSink` / `DeliverVoiceEvent` let a `~poly`
reach objects in a voice without a cord: the compiled `polyVoiceDomain::sinks` list, delivered
synchronously on the thread that runs the `~poly`'s handler. `~thispoly` is the first sink (the
`mute` report). `.polymidiin` (#756, `mPolyMidiIn`, beside `~thispoly` in `dPoly`) is the second: a
zero-inlet sink with one list outlet, taking the `MIDI` and `MPE` kinds of `voiceEvent` and sending
each message's bytes (`145 60 100`), which `.mpeparse` / `.midiparse` in the voice decode. The two
words on the `~poly`'s router:

- `midievent <status> <data> [<data>] [<detune>]`: a note-on through `DSP::voiceAllocator` as
  `midinote` (busy test, pending, steal), its note-off (128, or velocity 0) to the voice holding
  the key, poly pressure to the voices holding the key, every other channel message to the target.
  The voice remembers the note-on's channel (`noteChannel`, under the guard) so `allnotesoff` ends
  it with a note-off on that channel to `.polymidiin`, and a `midinote` note as before. Detune is
  ignored.
- `mpeevent <master> <zone> <voice> <channel> <status> <data> [<data>]`: Max's `mpemode 1`
  always — direct to the named voice, -1 every voice, a voice the `~poly` lacks (or 0) nothing; no
  allocator, no guard, no busy. The status byte's channel comes from the channel field, so a
  message number spelled with or without its channel means the same.

**The `mpeevent` shape decision (#756).** Max documents `mpeevent` as six ints (zone first channel,
zone index, voice, channel, message number, data) without saying what the last two hold. Here they
are the message's wire bytes, status (with channel) first, then one or two data bytes, so the event
is 6 or 7 ints and loses nothing. `.mpeparse` keeps the specification's channel-as-identity model
on its six expression outlets and grows a seventh, rightmost, `mpeevent` outlet (sent first) that
carries every channel message on the zone's master or member channels, configuration included: the
zone's master channel (1/16), the zone (1 lower, 2 upper), the voice (the member channel's place in
the zone from 1; -1 on the master), the channel. The voice number is derived from the channel, so
the two models agree and nothing new is invented. A channel outside the zone sends no event.
`.mpeformat` does not send `mpeevent` (Max's sends one for `vst~`, which does not exist here).

**The rest of the surface (#1294).**

- `voices <n>` at run time. The `~poly` stores the count asked for (`dPoly::WantedVoices`, an
  atomic, any thread) and asks for the deferred sync a `~send`'s `set` uses
  (`RequestSignalBusSync`), which now also rebuilds when a `~poly`'s asked-for count differs from the
  voices it has. The fingerprint no longer holds the count: with the same template,
  `SyncPolyVoicesUnlocked` trims voices from the end (`TrimPolyInstance`, retired like any copy) or
  builds new ones after the last (`BuildPolyInstanceUnlocked` starts at the voices the instance
  has), so the voices that stay keep their copies and their running state. Before the publish,
  `ClearVoices` resets the per-voice state of the voices being added (nothing renders them yet);
  after it, `PublishVoices` stores the published count (`Voices()`) and clamps the target. The
  allocator's size follows on the next routing word under the guard (`Enter`), which also releases
  the notes of removed voices, so no lock is shared with the audio thread and nothing allocates
  there. Growing the allocator only after the publish means no note can go to a voice no snapshot
  renders. A removed voice gets no `Teardown` (the sync runs under `mtx`, and a removed voice is
  like a rebuilt one), a new one no `Loadbang` (as for voices rebuilt by an edit). The count is
  run-time state, like mute: not saved, and a `SetParams` of the `~poly` starts from the argument.
- `busymap` / `mutemap [<outlet>]`: one 0/1 per voice of the pinned snapshot out of message outlet
  `outlet`, built in the reserved scratch string under the guard. Busy is what allocation sees
  (`~thispoly`'s flag, or pending), not "holds a pitch".
- `exclude <voice> <0|1>`: a per-voice flag passed to `DSP::voiceAllocator` as a *skip* test, a
  new optional predicate beside the busy one. It cannot be a third term of the busy test as the
  issue suggested: busy voices are still stolen, and an excluded one must not be.
- `bypass <voice> <0|1>`: the voice's render is skipped (it resumes where it stopped, like a
  muted one) and inlet N's signal is added to outlet N in its place. Mute wins over bypass.
- `~thispoly automute <0|1>`: a per-voice flag kept on the `~poly` (so a rebuild keeps it, as it
  keeps mute). `~thispoly` mutes its voice when its busy flag falls and unmutes it when it rises,
  through `dPoly::MuteVoice` so every `~thispoly` in the voice reports it. Since a muted voice does
  not render, a busy flag from a signal could never rise again; so the `~poly` unmutes an automuted
  voice when it gives it a note (`dPoly::Start`), before delivering it, and pending keeps it busy
  for the block its signal needs.

**Not done here** (Max surface left out or deferred): Max's external patch files, `args`, `up` / `down`, `parallel`, `threadcount`, `vs`, `open` / `wclose`,
`legacynotemode`. Dropped by #756: `mpemode` (always on: `mpeevent` only makes sense with direct
allocation, since the controller has already given each note its channel), `zone` (an `.mpeparse`
watches one zone, so a `~poly` fed by it only sees that zone's events), and `midimode` (a
`midievent` channel message already goes to the target, every voice by default; `mpeevent` voice -1
reaches every voice). Pin numbers are 0-based as on `~pfft`; voice numbers are 1-based.
