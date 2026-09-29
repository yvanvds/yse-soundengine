# Patcher time: domain clocks and timers

How patcher objects reach the engine's [domain clocks](../../PROJECT_OVERVIEW.md#12b-domain-clocks)
and `timerThread` without locking on the audio callback. User-facing behaviour is in
[documentation/source/patcher/time.rst](../../documentation/source/patcher/time.rst). Moved out of
PROJECT_OVERVIEW.md by issue #890.

**Patcher domain clocks.** [patcher/time/clockBridge.h](../../YseEngine/patcher/time/clockBridge.h)
/ [.cpp](../../YseEngine/patcher/time/clockBridge.cpp) is the patcher's side of the [domain
clocks](../../PROJECT_OVERVIEW.md#12b-domain-clocks) (issue
[#688](https://github.com/yvanvds/yse-soundengine/issues/688)), and it exists for the reason the
file scheduler ([patcher_file_io.md](patcher_file_io.md)) does: `CLOCK::Manager().lookup(name)`
walks the manager's list under a mutex, and a patcher message handler cannot know it is off the
audio callback. So the bridge splits the problem where the lock is — `Bind(name, length)` claims a
slot from a fixed table with one CAS per slot (wait-free, a bounded `memcpy` of the name, refusals
counted rather than logged) and returns a permanent `Handle`, a pre-allocated per-slot
`threadPoolJob` does the `lookup` on the background pool, and `Beat(handle, beat)` is then two
acquire loads — the slot's clock pointer and `domainClock::beatPosition()`. Bindings are never
released, so there is no generation counter and no stale handle; `CAPACITY` bounds *clock names* per
patcher rather than objects, since `Bind` is idempotent by name. A resolved slot also keeps the
`shared_ptr` the lookup returned ([#707](https://github.com/yvanvds/yse-soundengine/issues/707)) —
that is what reconciles a permanent binding with a `destroyClock` that can arrive at any moment: the
clock stops advancing but cannot be freed under the binding, so `Beat` reads a frozen beat and a
wait armed on it simply never comes due. The share is taken on the pool and released in
`~clockBridge` after `WaitIdle`, so no refcount work lands on a read path. A name with no live clock
stays unresolved and `Beat` answers **false** (not zero), and `Poll` — called from the top of
`Calculate`, rate-limited — retries it, so a patch that names its clock before the host creates it
starts when the clock appears. Objects reach it through `pObject::Clocks()`. `messageScheduler`
consumes it: `ScheduleBangOnClock(target, tag, binding, beats)` arms the *same* slot with a beat
deadline instead of a block deadline — slot lifecycle, arm-order delivery, cancellation and
`GraphState` target re-resolution are untouched, only the deadline test differs — and a wait armed
before its binding resolved is baselined at `clockBridge::ResolveBeat`, i.e. the wait starts when
the clock starts existing. `.qlist` is the first consumer: `clock <name>` reads the leading number
of a numeric cue as beats on that clock instead of milliseconds (`clock` alone goes back), so a cue
list follows tempo changes and ramps and holds where it stands when the domain pauses. The binding
is run-time state and is not saved with the patch. `.seq` is the second consumer
([#704](https://github.com/yvanvds/yse-soundengine/issues/704)) and a different shape of one: `clock
<name>` does not change what a stored number means — the tape stays milliseconds and a millisecond
`start` is untouched — it supplies the **ticks** of Max's `start -1`, one tick to 1/24 of a beat,
which is the MIDI clock Max's "48 tick messages per second" is at 120 BPM. So the sequence follows
the domain's tempo, a `tick` from the patch is ignored while a clock drives, and a bare `clock`
hands the ticks back. Its tick count is read off `Beat` at each wakeup rather than counted from the
wakeups themselves: a beat deadline is armed relative to the beat it was armed at but delivered at
the first block boundary past it, so counting deliveries would drop that overshoot every tick — and
above ~215 BPM, where a tick is shorter than a block, would cap the rate at one tick per block. The
arm is a polling rate; the clock is the time base. `.delay` and `.metro` are the third and fourth
consumers ([#705](https://github.com/yvanvds/yse-soundengine/issues/705)), and the first two where
`clock` is a **port** rather than an addition — Max's `setclock` names both objects explicitly. They
gain two independent things, as Max keeps them independent: `clock <name>` names the clock (bare
`clock` gives back Max's millisecond clock), and a *tempo-relative time* — a note value (`4n`,
`4nd`, `8nt`) or a tick count (`1440 ticks`) — is the delay/interval in beats instead of
milliseconds. The syntax is parsed by
[patcher/time/timeValue.h](../../YseEngine/patcher/time/timeValue.h), shared between the two for the
reason `pSelector` is shared: two objects reading one syntax must read it identically. The unit
travels with the *value*, not with the clock — any plain number puts the object back on
milliseconds, Max's "the number is stored as the number of milliseconds" — and `bars.beats.units`,
`quantize` and `transport` stay out because all three need a meter and a `domainClock` is a bare
beat accumulator with none. Three departures, all documented in the object headers: a bound clock
does **not** scale milliseconds (a `domainClock` has no millisecond scale, only beats), a
tempo-relative time with **no** clock bound does not fire at all (this patcher has no transport to
fall back on, and re-reading `4n` as 1 ms would be worse), and `.metro` decides which engine a run
uses — `timerThread` (through `timerBridge`, see **Patcher timers** below) for milliseconds, the
scheduler for beats — at the toggle, so a unit change reaches the next start. `.metro` carries
`.seq`'s drift lesson twice over: its bang count is `floor((beat - base) / interval)` read off the
clock, *and* each wakeup is armed at the **absolute** next grid point rather than one interval from
here. Both halves are load-bearing and both are pinned by tests that fail without them — the first
stops a sub-block interval capping at one bang per block, the second stops the wakeups clumping onto
every other block. That grid is also what decided
[#711](https://github.com/yvanvds/yse-soundengine/issues/711)'s question — `.metro` had only Max's
`int` of the four start/stop methods its left inlet documents, and `bang`, `float` and `stop` all
now route through the one `Toggle` path: a start (bang, or a non-zero `int`/`float`) cancels the
armed wakeup and takes a **new** `beatBase` at the beat the message landed on, which is Max's own
"the metro will re-start itself and begin scheduling subsequent bang messages from the moment we
triggered it" and the reason one button re-phases several metros into sync, while a stop (`stop`, or
a zero `int`/`float`) clears `beatOn`, cancels the wakeup and emits nothing. Neither edits a running
grid — a start replaces it whole, a stop retires it — so neither can disturb the two disciplines
above. The start bang stays synchronous, unlike `.delay`'s deferral: Max documents it as immediate,
and `outlet::Send*`'s send-depth ceiling
([#236](https://github.com/yvanvds/yse-soundengine/issues/236)) already bounds the
outlet-into-own-inlet cycle a `bang` method makes drawable.

**Patcher timers.** [patcher/time/timerBridge.h](../../YseEngine/patcher/time/timerBridge.h) /
[.cpp](../../YseEngine/patcher/time/timerBridge.cpp) is `.metro`'s side of `timerThread`, and it
exists for the reason the file scheduler and the clock bridge above do (issue
[#718](https://github.com/yvanvds/yse-soundengine/issues/718)): every entry point `timerThread` has
is forbidden on the audio callback — `Add` takes a mutex and allocates two container nodes plus the
caller's `std::function`, `SetPeriod` takes the same mutex, and `ClearTimer` blocks on a condition
variable until an in-flight callback returns — and `.metro`'s toggle called all three straight from
a message handler, which a `.delay` wired into its left inlet puts on the callback. The engine is
unchanged (a millisecond metro is still a real OS timer at millisecond resolution, still ticking
while the engine is paused); what changed is the caller. Each `.metro` claims a slot for its
lifetime, a start / stop / retime writes *wanted* state into it — running or not, at which period,
since which start — and a per-slot pre-allocated `threadPoolJob` reconciles the timer to match. The
object picks the mechanism itself from `patcherImplementation::CallingThread`
([#690](https://github.com/yvanvds/yse-soundengine/issues/690)) rather than from the `THREAD` tag,
forwarding the tag unaltered: on the callback the request is wait-free and the pool does the locking
a hop later, off it the reconcile is inline, so a control-thread toggle keeps the exact timing and
the exact stop-means-stopped handshake it always had. The slot table is a process singleton rather
than an object member on purpose — the [#227](https://github.com/yvanvds/yse-soundengine/issues/227)
reclaimer frees retired objects *on the background pool* and `~threadPoolJob` joins, so an
object-owned job would spin its own destructor on the single worker already inside it — and
`Release` therefore never joins, it only takes the slot's mutex and does `ClearTimer`'s blocking
handshake, which a destructor may.
