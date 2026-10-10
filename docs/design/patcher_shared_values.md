# Patcher shared values: named stores, dict and array

Why mutable shared values are addressed by name rather than passed down a cord, and the `.dict` /
`.array` families built on that decision. User-facing behaviour is in
[documentation/source/patcher/data.rst](../../documentation/source/patcher/data.rst). Moved out of
PROJECT_OVERVIEW.md by issue #890.

**Mutable shared values are addressed by name, never passed down a cord.** The settled answer to the
question the array ([#548](https://github.com/yvanvds/yse-soundengine/issues/548)), string
([#549](https://github.com/yvanvds/yse-soundengine/issues/549)) and dict
([#550](https://github.com/yvanvds/yse-soundengine/issues/550)) epics all asked, decided at the dict
gate and binding on the other two. A cord in this patcher is not a message object: `OUT_TYPE` is
`BANG / FLOAT / INT / BUFFER / LIST / ANY` and a send is a direct synchronous call carrying a value
by value (or a borrowed `const std::string&` the receiver must copy before it returns). There is no
atom, no variant and no refcounted box in the transport, so **an `OUT_TYPE` cannot carry an
identity** — the finding `.textedit` ([#560](https://github.com/yvanvds/yse-soundengine/issues/560))
recorded. Adding one would put reference-count traffic on `outlet::Send*`, which is routinely the
audio callback; it would make a value's lifetime span the `GraphState` swap
([#226](https://github.com/yvanvds/yse-soundengine/issues/226)/[#227](https://github.com/yvanvds/yse-soundengine/issues/227)),
so an edit that deletes an object could free a value another object is mid-read on; and it would
hand every existing object a payload kind it has no handler for. So the model is Max's own:
**storage lives in the named registry** (`patcher/namedStore.h`,
[#684](https://github.com/yvanvds/yse-soundengine/issues/684)) keyed as
`"patcher.<patcherName>.<name>"`; the registry holds stores **weakly**, so ownership is exactly
"whoever addresses the name" and a value is freed when the last object naming it goes; the name is
**resolved once, on the control thread** (`SetParent` / `PARM_PARSE` / `RefreshBinding`), because
`AcquireNamedStore` takes a mutex and may allocate; and what travels down a cord is the **name** —
`.dict` emits `dictionary <name>` and the receiving object binds that itself. No message path ever
copies or drops the `shared_ptr`, so no refcount operation lands on the audio thread. The store
behind a name is a table allocated whole in its constructor plus `.value`'s non-blocking `busy`
try-lock, so a mutation from a rendering graph is one pointer hop, one `exchange` and a bounded
`assign`. The corollary the whole family inherits: **re-pointing an object at a different name from
a message is not portable** (Max's `refer`), and an operator that genuinely needs run-time rebinding
uses `clockBridge`'s shape — a wait-free claim on a fixed slot plus a background-pool resolve. The
named bus is unchanged by this and stays a *value* bus: it carries the name, never the contents.

**`.dict` and the dict.\* family.**
[patcher/genericObjects/gDict.h](../../YseEngine/patcher/genericObjects/gDict.h) /
[.cpp](../../YseEngine/patcher/genericObjects/gDict.cpp) is the first type built on that decision
(issue [#550](https://github.com/yvanvds/yse-soundengine/issues/550)) — a nested key/value
dictionary shared by name. `dictStore` is 256 entries, a 128-character key path and a 256-character
value, all reserved at construction. **Nesting lives in the key**: `voice::1::freq` is one flat
entry three levels deep, `::` being Max's own path separator, because a tree of nodes is an
allocation per branch on a path that may be the audio callback. It becomes a real tree again at the
boundary — `DictToJson` expands `::` into nested JSON objects with typed leaves and `DictFromJson`
flattens them back, through the `nlohmann::json` layer `DumpJSON`/`ParseJSON` already runs on rather
than a second JSON implementation. Contents ride `pObject::DumpState` / `RestoreState` under
`.coll`'s rule: **everyone writes and only the creator restores**. `DictFind` / `DictStoreAt` /
`DictEraseAt` / `DictReferenceNames` are the store's public, guard-holding, allocation-free API —
what the twelve `dict.*` operations are written against.

**`.array` and the array.\* family.**
[patcher/genericObjects/gArray.h](../../YseEngine/patcher/genericObjects/gArray.h) /
[.cpp](../../YseEngine/patcher/genericObjects/gArray.cpp) is the second type built on that decision
(issue [#548](https://github.com/yvanvds/yse-soundengine/issues/548)) — an ordered, index-addressed
sequence shared by name, with `array <name>` as the reference a cord carries. Everything about the
addressing, the weak registry ownership, the control-thread resolve, the `busy` try-lock and the
everyone-writes/creator-restores persistence rule is `.dict`'s, unchanged. What it adds is
**order**, and three things follow from it that are decided once here for the whole family rather
than per object. **An element is one atom** (`arrayStore` is 256 elements of at most 64 characters,
all reserved at construction), not list text as a `dictEntry`'s value is: an array and the list text
it spells have to be the same thing seen twice, and `["0", "4 7"]` and `["0 4", "7"]` render to
identical characters, so per-element list text would make `getvalue` lossy and `array.tolist` unable
to round-trip. **An index is a position** — out of range is a miss on `get` and a counted refusal
elsewhere, negatives included; `array.wrap` and `array.rotate` are the objects that exist to provide
wrapping and rotation. **`insert` and `delete` renumber**, so an object that walks an array keeps
its own cursor and states on its own issue what a concurrent write does to the walk. JSON is
correspondingly shorter than `.dict`'s — an array *is* a JSON array, so `ArrayToJson` /
`ArrayFromJson` only type the elements and skip sub-documents, which have no atom that spells them.
`ArrayFind` / `ArraySetAt` / `ArrayInsertAt` / `ArrayAppend` / `ArrayEraseAt` /
`ArrayReferenceNames` are the store's public, guard-holding, allocation-free API the `array.*`
operations are written against.
