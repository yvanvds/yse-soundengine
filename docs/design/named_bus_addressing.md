# Named-bus addressing: patchers, sounds, channels, synths

How engine objects expose themselves on the global named bus (`INTERNAL::Bus()`, see
[PROJECT_OVERVIEW.md §10](../../PROJECT_OVERVIEW.md#10-threading--concurrency-model)). The
user-visible address grammar is locked by [live_coding_dsl.md](live_coding_dsl.md). Moved out of
PROJECT_OVERVIEW.md by issue #890.

**Patcher naming and the global bus.** Every `YSE::patcher` carries a name — auto-generated as
`"patcher_<N>"` or set via the chainable `patcher::name(const std::string&)` (issue
[#122](https://github.com/yvanvds/yse-soundengine/issues/122)). Inside the patcher, `gSend`
publishes each incoming value to the [global named
bus](../../PROJECT_OVERVIEW.md#10-threading--concurrency-model) under
`"patcher.<patcherName>.<dataName>"` while still firing the in-patcher `PassData` path for
back-compat (opt-out with the second `gSend` argument: `"name 1"` skips local delivery). `gReceive`
subscribes to the same address on construction and unsubscribes on destruction, so two patchers with
the same name route their `gSend`/`gReceive` pairs together while patchers with distinct names stay
isolated even when their inner `dataName` values collide. Renaming a patcher transparently
re-subscribes every `gReceive` it owns. Every name-scoped address — bus sends/receives and the
shared `.value`/`.coll`/`.dict*`/`.array*` stores — is spelled in exactly one place,
`patcherImplementation::ScopedAddress(name)` / `ScopedAddressPrefix()` (control thread only), and a
rename re-anchors objects through the virtual `pObject::OnPatcherRenamed()` hook rather than a
per-type chain in `SetName` (issue [#893](https://github.com/yvanvds/yse-soundengine/issues/893)).

**Sound and channel bus addressing.** `YSE::sound` and `YSE::channel` gain an optional chainable
`name(const std::string&)` setter (issue
[#123](https://github.com/yvanvds/yse-soundengine/issues/123)) that exposes their properties on the
[global named bus](../../PROJECT_OVERVIEW.md#10-threading--concurrency-model). A named sound
subscribes to `sound.<name>.volume`, `sound.<name>.speed` (both `float`, also accepting `int`), and
`sound.<name>.position` (a 3-element `list[float]` → `Pos`); a named channel subscribes to
`channel.<name>.volume`. The callbacks reuse the existing message setters, so no new audio-thread
surface is opened. Anonymous instances are not addressable; passing `""` clears the name. Names are
unique *producers* per prefix: a second sound (or channel) claiming a live name is rejected and
logged via `E_FILE_ERROR`, first registration wins. Registration/deregistration is tied to
construction/destruction and guarded by `Global().isActive()`, so destructors running after
`System::close()` and naming while the engine is down are safe no-ops. The channel's bus name is
independent of the log label passed to `create()` (now stored as `logName`). The C ABI mirrors are
`yse_sound_set_name` / `yse_channel_set_name` (issue
[#905](https://github.com/yvanvds/yse-soundengine/issues/905)); `yse_channel_get_name` copies the
log label snprintf-style. The user-visible address grammar is locked by
[docs/design/live_coding_dsl.md](live_coding_dsl.md).

**Synth bus addressing.** `YSE::synth` follows the same pattern (issue
[#388](https://github.com/yvanvds/yse-soundengine/issues/388)): a chainable `name(const
std::string&)` setter registers note/controller *event* addresses — `synth.<name>.note` (`[channel,
note, velocity]`), `.off` (`[channel, note(, velocity)]`), `.cc` (`[channel, number, value]`, CC
64/66/67 = pedals), `.bend` (`[channel, value]`), `.aftertouch` (`[channel, note, value]`), and
`.alloff` (`int`/`float` channel, or a bang = all channels). All payloads except `alloff` are
`list[float]`; channel/note elements are rounded to int. The subscribers run on the control thread
and enqueue through the synth's existing RT-safe message inbox (the same path as `noteOn()` etc.),
so no new audio-thread surface is opened. Naming semantics (unique producer, `""` clears,
`Global().isActive()` guard) match the sound/channel contract above; the C ABI mirror is
`yse_synth_set_name`. Shapes are locked by the spec's "Mapping to synth events" section.
