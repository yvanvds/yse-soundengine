#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include "inlet.h"
#include "outlet.h"
#include "pEnums.h"
#include "../headers/enums.hpp"
#include "../headers/defines.hpp"
#include "../dsp/buffer.hpp"
#include "pObjectList.hpp"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-literal-operator"
#pragma clang diagnostic ignored "-Wtautological-overlap-compare"
#endif
#include "../utils/json.hpp"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include "../utils/vector.hpp"
#include "parameters.h"
#include "patcher.hpp"

namespace YSE {
  namespace PATCHER {

    struct GraphState;
    class patcherImplementation;
    class messageScheduler;
    struct deferredMessage;
    class fileScheduler;
    struct fileResult;
    class clockBridge;

    typedef std::function<void(int, int)> intCallbackFunc;
    typedef std::function<void(int, float)> floatCallbackFunc;

    class API pObject {
    public:
      pObject(bool isDSPObject, pObject* parent = nullptr);
      virtual ~pObject() {}

      // The GraphState the owning patcher has pinned for the current audio
      // block, or null when the patcher is between blocks or this object has no
      // patcher (standalone / unit-test use). Outlets and inlets consult it to
      // resolve topology on the audio thread without a lock (issue #226).
      const GraphState* CurrentBlockGraph() const;

      // The owning patcher's deferred-message scheduler (issue #628), or null
      // for a standalone object / the patcher itself. RT-safe on any thread —
      // one pointer hop, like CurrentBlockGraph() — so a message handler may
      // call it mid-dispatch to arm a deferral.
      messageScheduler* Scheduler() const;

      // The owning patcher's domain-clock bridge (issue #688), or null for a
      // standalone object / the patcher itself. RT-safe on any thread — one
      // pointer hop, like Scheduler() — so a message handler may bind a clock
      // by name mid-dispatch. Binding is wait-free; the name is resolved
      // against CLOCK::Manager() on the background pool, because that lookup
      // takes the manager's mutex.
      clockBridge* Clocks() const;

      // Deferred-message delivery (issue #628). Called by the scheduler on the
      // patcher's dispatch thread, inside a fresh messageEventScope, when a
      // message this object armed comes due. Default: ignore — only objects
      // that schedule ever receive one. Everything RT-applicable applies: no
      // allocation, no locks, no I/O.
      virtual void DeliverDeferred(const deferredMessage& msg, THREAD thread);

      // The owning patcher's file-I/O scheduler (issue #683), or null for a
      // standalone object, for the patcher itself, or for a patcher no
      // file-capable object has joined. RT-safe on any thread — one pointer hop
      // and one acquire load, like Scheduler() — so a message handler may call
      // it mid-dispatch to ask for a file.
      fileScheduler* FileIO() const;

      // Ask the owning patcher to build its file scheduler. Control thread
      // only: an object that can read or write files calls this once, from its
      // SetParent override, which is where the patcher becomes known and where
      // allocating half a megabyte of slot table is affordable. No-op for a
      // standalone object. See fileScheduler's header for the full recipe.
      void EnableFileIO();

      // File-request completion (issue #683). Called by the file scheduler on
      // the patcher's dispatch thread, inside a fresh messageEventScope, when a
      // read or write this object asked for has finished. Default: ignore —
      // only objects that request files ever receive one. Everything
      // RT-applicable applies: no allocation, no locks, no I/O, and
      // `result.bytes` is only valid for the duration of the call.
      virtual void DeliverFileResult(const fileResult& result, THREAD thread);

      // "The patch you are in has finished loading" (issue #547). Called by the
      // owning patcher on every object a `ParseJSON` created, once, after the
      // whole parsed graph has been compiled and published — never during the
      // build. `.loadbang` sends its bang here and `.loadmess` its stored
      // message; default: nothing, so only the objects that need it pay for it.
      //
      // This is `Teardown`'s mirror image and is deliberately shaped like it:
      // control thread, dispatched with T_GUI, outside the patcher's mutex, and
      // as one pass over a snapshot of the objects rather than a call woven into
      // the build loop. See patcherImplementation::LoadbangObjects for why each
      // of those is load-bearing.
      //
      // **Only a load fires it.** An object created live through
      // `CreateObject` never receives this — see the same place for the
      // reasoning, and gLoadbang.h for what it means to a patch.
      virtual void Loadbang(THREAD thread);

      // "You are about to go away" (issue #758). Called by the owning patcher
      // on every object it is about to unwire — from Clear(), and on the one
      // object DeleteObject() removes — while every cord in the patch is still
      // connected, so an object that has left something sounding *outside* the
      // patch can release it through its own outlets while there is still an
      // outlet to release it through. `.midiflush` sends its note-offs here,
      // `.makenote` its pending releases, `.metro` stops its timer. Default:
      // nothing, so only the objects that need it pay for it.
      //
      // Control thread, dispatched with T_GUI and deliberately *not* under the
      // patcher's mutex — see patcherImplementation::TeardownObjects for both,
      // and for why the whole pass has to finish before anything is unwired.
      //
      // Called at most once per object and never again afterwards: whatever
      // this sends is the last thing the object ever sends.
      virtual void Teardown(THREAD thread);

      // Detach this object from every peer it is wired to, without freeing it,
      // so the next GraphState holds no reference to it (issue #226).
      void UnwireFromPeers();

      virtual const char* Type() const = 0;
      virtual void Calculate(THREAD thread) = 0;

      // Whether this object must be visited once per audio block even though
      // nothing in the patch drives it (issue #529).
      //
      // The render traversal is a *push* from the DSP start points, and a
      // control object is only ever reached because a message arrived at one of
      // its inlets. That leaves no home at all for an object whose input comes
      // from outside the patch — the MIDI-input family, whose events arrive on
      // RtMidi's own thread and wait in a lock-free queue until someone drains
      // them. Such an object has no inlets to be pushed through and is not a
      // DSP object, so without this it would simply never run.
      //
      // An object that answers true is listed in the GraphState's `pollers` and
      // has `Calculate()` called at the top of the block, alongside the value,
      // deferred-message and file-completion drains and before the DSP
      // traversal — so an event that arrives between two blocks reaches the
      // patch in the block that follows it, and anything it triggers is
      // rendered by that same block. Everything RT-applicable applies to that
      // Calculate: no allocation, no locks, no I/O.
      //
      // Deliberately *not* the same question as IsDSPObject(): a poller is
      // control-rate and must stay so, or it would report itself as an
      // audio-rate object to every palette and binding that reads the metadata,
      // and its inlets would start refusing GUI-dispatched values.
      virtual bool WantsBlockPoll() const {
        return false;
      }

      virtual void SetMessage(const std::string& message, float value) = 0;

      void SetParams(const std::string& args);
      const std::string& GetParams();

      // Live-SetParams support (issue #234). A re-parse on a published object
      // must not mutate it in place; the owning patcher asks these to decide
      // and stage the RT-safe route instead.
      bool ParamsNeedRebuild() const {
        return parms.NeedsRebuild();
      }
      int BuildParamPlan(const std::string& args, ParamOp* ops, int cap) {
        return parms.BuildPlan(args, ops, cap);
      }
      // Take over the persistent identity of the object this one replaces:
      // the storage ID (DumpJSON references) and the GUI properties. Pin
      // layout, params, and DSP state are deliberately not copied — the
      // replacement was just built from the new param string.
      //
      // The instance tag is deliberately not copied either, and cannot be: a
      // #234 replacement is a *new instance* that inherits a storage slot, not
      // a continuation of the old object, and a message armed on the old one
      // must not be delivered to it (issue #733). `instanceTag_` being const is
      // what makes that unbreakable rather than merely intended.
      void CopyStorageIdentity(const pObject& from) {
        ID = from.ID;
        guiProperties = from.guiProperties;
        // Which subpatcher the object sits in is persistent identity too — it
        // is serialised, and a replacement that lost it would silently move the
        // object to the top level of the patch (issue #545).
        container = from.container;
      }

      // The `patcher` (subpatcher) object this one lives inside, or null when
      // it sits at the top level of its patcher (issue #545).
      //
      // Nesting is *only* this pointer. A subpatcher's contents are not a
      // second graph: every object in a patch lives in the one
      // `patcherImplementation::objects` map, takes a graph id from the one id
      // space and is compiled into the one GraphState, however deeply it is
      // nested. So this is a control-thread annotation and nothing more — it is
      // read by `Connect`/`Disconnect` (to resolve a subpatcher façade to the
      // `.inlet` / `.outlet` object that carries the pin), by `DeleteObject`
      // (to collect the containment subtree), and by `DumpJson` / `ParseJSON`.
      // The audio thread never reads it, which is why it needs no
      // synchronisation of its own beyond the patcher's mutex.
      //
      // Written only by `patcherImplementation::SetObjectContainer`, which
      // rejects a cycle, and by `ParseJSON` restoring one. See gSubpatcher.h.
      inline pObject* Container() const {
        return container;
      }
      inline void SetContainer(pObject* c) {
        container = c;
      }
      inline pObject* Parent() const {
        return parent;
      }

      // Returns false when the inlet refuses the connection (it already has a
      // buffer source, or the edge is a duplicate). The caller must not record
      // the edge on the outlet side in that case: a one-sided outlet->inlet
      // edge is invisible to Disconnect/UnwireFromPeers and ends up as a
      // dangling inlet* in every GraphState built after the target object is
      // deleted (issue #237).
      bool ConnectInlet(outlet* from, int toPin);
      void DisconnectInlet(outlet* from, int toPin);

      virtual void ConnectOutlet(inlet* dest, int toPin);
      // outputs will be disconnected from the other side

      inline int NumInputs() const {
        return (int)inputs.size();
      }
      inline int NumOutputs() const {
        return (int)outputs.size();
      }

      OUT_TYPE GetOutputType(unsigned int output) const;
      inlet* GetInlet(int number);
      outlet* GetOutlet(int number);

      // Edge introspection. Control thread only — never called from the audio
      // callback, which walks the pinned GraphState instead.
      //
      // All three take an outlet number from outside and range-check it, the
      // way GetOutputType does: an object's outlet count changes under a
      // SetParams that re-parses its arguments, so a caller holding an outlet
      // number from before the re-parse can hand back one that no longer
      // exists, and outputs[] is a vector (issue #737).
      //
      // A query that cannot be answered — outlet past the object's outlet
      // count, or connection past that outlet's edge count — reports:
      //   GetConnections           0, the number of edges a nonexistent
      //                            outlet has.
      //   GetConnectionTarget      kNoObjectID. Not 0: object IDs start at 0
      //                            (issue #730) so 0 is a real target.
      //   GetConnectionTargetInlet kNoInletIndex. Not 0 either: 0 is the
      //                            leftmost inlet and the one most edges
      //                            arrive at (issue #736).
      unsigned int GetConnections(unsigned int outlet);
      unsigned int GetConnectionTarget(unsigned int outlet, unsigned int connection);
      unsigned int GetConnectionTargetInlet(unsigned int outlet, unsigned int connection);

      virtual void ResetDSP();
      void CalculateIfReady(THREAD thread);
      bool IsDSPStartPoint();
      inline bool IsDSPObject() {
        return DSP;
      }

      // Editor decoration — geometry, colour, anything the host wants
      // persisted alongside the patch. Opaque to the engine, serialised under
      // the "gui" key by DumpJson, control thread only. Deliberately *not*
      // where live control state lives: see the GUI value protocol below.
      std::string GetGuiProperty(const std::string& key);
      void SetGuiProperty(const std::string& key, const std::string& value);

      // ─── GUI value protocol (issue #551) ────────────────────────────────
      //
      // What a host polls to draw an object, and what `.preset` reads and
      // pushes back. Stated here, once, because every GUI object implements
      // it and because the structured controls (`.rslider`, `.multislider`,
      // `.matrixctrl`, `.function`) cannot be built until it is settled.
      //
      // SHAPE — GUI state is a list of string cells.
      //
      //   GetGuiValue()        The whole state as one string. For a scalar
      //                        control that is the value; for a structured
      //                        one it is every cell, space separated, in
      //                        index order. One call, one allocation — this
      //                        is the bulk read a host uses to refresh a
      //                        whole widget, and the form `.preset` stores.
      //   GetGuiValueCount()   How many cells the object has. 1 unless
      //                        overridden.
      //   GetGuiValueAt(i)     Cell i, and "" past the end. The default *is*
      //                        GetGuiValue() at index 0, so every object that
      //                        predates this protocol answers exactly what it
      //                        always answered — by construction, not by
      //                        copied code. This is the targeted read, so a
      //                        host repainting one cell of a 16x16 matrix
      //                        does not re-parse the whole state.
      //   GuiValueIsSettable() Whether the write half below holds.
      //
      // Cells are strings rather than a typed payload because the controls
      // that need this share no element type — `.rslider` a float pair,
      // `.multislider` floats, `.matrixctrl` cell states, `.function`
      // breakpoint pairs — and because it keeps the C ABI at one string-copy
      // call per read instead of a family of typed array accessors, each
      // with array-ownership rules of its own.
      //
      // Deliberately *not* guiProperties. That map is editor decoration
      // written by the host and serialised with the patch; it is a plain
      // std::map with no synchronisation of its own, so live control state —
      // which the audio thread writes — cannot live in it.
      //
      // WRITE — there is no GUI-value setter, and there will not be one.
      //
      // State goes *into* an object the way every other value does: as a
      // message through an inlet, on the control thread, via
      // pHandle::SetListData / SetFloatData / SetIntData / SetBang, or from
      // inside the patch down a cord. `.preset` restores a patch by sending
      // each object its stored string and never by reaching into another
      // object's fields — which would race the audio thread and skip every
      // clamp, side effect and outlet send the object's own inlet handler
      // performs.
      //
      // An object that answers true to GuiValueIsSettable() promises the
      // round trip that makes that usable:
      //
      //   * inlet 0 accepts, as a list, the exact string GetGuiValue()
      //     produced, and restores the state it described;
      //   * inlet 0 also accepts "set <index> <value>" to write one cell.
      //
      // The "set" keyword is what disambiguates the two, and it is why the
      // cell form is not the bare "<index> <value>" it would like to be: a
      // two-cell control such as `.rslider` cannot otherwise tell the whole
      // state "0 1" from "cell 0 := 1". A leading token no numeric cell can
      // ever hold settles it for every arity at once.
      //
      // ONE EXCEPTION, and it is the one the argument above names its own
      // premise for: **a one-cell control whose cell is free text** does not
      // offer the "set" form. "A leading token no numeric cell can ever hold"
      // is exactly what a *text* cell can hold, so honouring it would mean
      // `.textedit` (issue #560) could not contain the text "set 0 hello" —
      // a text field that silently swallows what was typed into it. Nothing is
      // given up in exchange: with one cell the whole-state write *is* the cell
      // write, with the same string, so the round trip below — which is the
      // half `.preset` needs — holds in full and GuiValueIsSettable() stays an
      // honest true. The carve-out is deliberately that narrow. A structured
      // control still needs "set" to address a cell, whatever its cells hold,
      // and a numeric scalar has no reason to want it.
      //
      // What the object does *besides* storing the value is its own
      // business — whether a restore also emits on its outlet is a
      // per-object decision, not part of this protocol.
      //
      // False by default, and false for most objects that predate this
      // protocol: their inlet 0 takes an int or a float rather than the
      // display string GetGuiValue() hands back, so claiming the round trip
      // would be a lie. Opting one in is a per-object migration and belongs
      // with the object that needs it — `.preset` was that object, and #846
      // migrated the scalar controls `.slider`, `.i`, `.f`, `.dial`,
      // `.incdec` and `.t`, each of which now takes its own display string
      // back as a list on inlet 0 (`.t` accepts the "on"/"off" it reports).
      // `.b` stays out on purpose: its GUI value reports and clears a pending
      // press — an event, not a state — so there is nothing a restore could
      // meaningfully write back, and writing "on" would replay the press.
      //
      // THREADS — the contract every implementation is bound by.
      //
      // All four are polled from the *host* thread while Calculate() runs on
      // the audio thread. Therefore:
      //
      //  - Every field they touch is written concurrently and must be atomic
      //    (or otherwise wait-free). None of them takes a lock: the host
      //    polls every frame and the audio thread must never wait on a
      //    repaint.
      //  - A read-modify-write is one step. gButton's poll clears the press
      //    it reports; written as a load and then a store it dropped a press
      //    that landed in between (issue #197) — hence exchange().
      //  - A poll may therefore be destructive, and reads are not
      //    idempotent. Poll a given object through one of the two forms per
      //    frame — the bulk read *or* the per-cell reads — never both, or a
      //    consume-on-read object reports its event to one and not the other.
      //  - A multi-cell read is not atomic across cells. The count and the
      //    cells are sampled one call at a time, so a host can see cell 3
      //    from before an edit and cell 4 from after it, and can see a count
      //    a live SetParams has already changed. A torn *frame* is fine for a
      //    repaint; a torn *access* is not, so an override of GetGuiValueAt
      //    range-checks against the count it reads now and answers "" rather
      //    than indexing. A host that needs a coherent snapshot uses the bulk
      //    read, which is one call.
      //
      // Tests/patcher/test_patcher_object_races.cpp exercises the thread
      // contract; Tests/patcher/test_patcher_gui_protocol.cpp pins the shape.
      virtual std::string GetGuiValue() {
        return "";
      }
      virtual unsigned int GetGuiValueCount() const {
        return 1;
      }
      virtual std::string GetGuiValueAt(unsigned int index) {
        return index == 0 ? GetGuiValue() : std::string();
      }
      virtual bool GuiValueIsSettable() const {
        return false;
      }

      // Storage ID — the number this object is written as by DumpJson, and the
      // number every outlet pointing *at* it writes as its connection target.
      //
      // Handed out by the owning patcher (issue #730). It used to come from a
      // process-wide counter, which made a saved patch's IDs a record of how
      // many patcher objects the process had already built rather than a
      // property of the patch. Per-patcher, the same patch built the same way
      // always serialises to the same bytes.
      //
      // Unique among the patcher's *live* objects, and no more than that: the
      // patcher hands out the smallest ID no live object holds
      // (patcherImplementation::ClaimStorageID), so a deleted object's number
      // goes to the next object created and the numbering stays as small as the
      // patch is (issue #733). It carried a second job until #733 — the
      // impersonation guard the message and file schedulers use to tell a live
      // target from a recycled allocation at the same address — and could not
      // be reused while it did. That job now belongs to the instance tag below,
      // which is why this one is free to shrink.
      //
      // The flip side of reuse: an ID names an object only for as long as that
      // object lives. Code holding an ID across a delete (GetHandleFromID, the
      // C ABI's yse_patcher_get_handle_from_id) can be handed the object that
      // inherited the number rather than nothing at all.
      //
      // kNoStorageID is what an object outside a patcher carries — a standalone
      // object in a unit-test rig, or the patcher itself. Such an object is
      // never serialised and never reachable from a GraphState, so the sentinel
      // is only ever observed through GetID() by code that built the object
      // itself. GetID() returns it as UINT_MAX, which matches no real ID.
      static constexpr int kNoStorageID = -1;

      // kNoStorageID as GetID() hands it back: the ID that belongs to no
      // object. Also what GetConnectionTarget answers when it cannot name a
      // target, and the value the C ABI publishes as YSE_PATCHER_ID_NONE
      // (issue #732) — yse_patcher.cpp static_asserts the two are the same
      // number so they cannot drift apart.
      //
      // That static_assert pins the *value*; what keeps the value meaning "no
      // object" is that no real ID ever reaches it. Storage IDs are allocated
      // as the smallest number no live object holds, so the largest one a
      // patcher can issue is its live object count — bounded by how many
      // objects fit in memory, which is nowhere near 2^32 (issue #733). Under
      // the pre-#733 monotonic counter the same guarantee rested on the counter
      // never being advanced 2^32 times; reuse makes it structural instead.
      static constexpr unsigned int kNoObjectID = static_cast<unsigned int>(kNoStorageID);

      // The inlet index that names no inlet: what GetConnectionTargetInlet
      // answers when it cannot name one. Not 0 — inlet 0 is the leftmost inlet
      // and the one most edges in a patch arrive at, so 0 as a failure marker
      // is indistinguishable from the commonest real answer (issue #736).
      //
      // Deliberately a separate name from kNoObjectID even though the two hold
      // the same number: an inlet index and an object ID are different
      // quantities, and a comparison written against the wrong one should read
      // wrong. They agree on UINT_MAX because it is the one value unreachable
      // in either domain — an object would need 2^32 inlets — and because a
      // binding that stores either in a signed 32-bit integer then sees the
      // same -1. The C ABI publishes this as YSE_PATCHER_INLET_NONE, which
      // yse_patcher.cpp static_asserts against this constant so the two cannot
      // drift apart.
      static constexpr unsigned int kNoInletIndex = static_cast<unsigned int>(-1);

      inline unsigned int GetID() {
        return ID;
      }
      // Control thread only, and only from the owning patcher: called once when
      // the object joins it, before the object is published to any GraphState.
      inline void SetStorageID(int id) {
        ID = id;
      }

      // Instance tag — the identity half of what the storage ID used to carry
      // alone (issue #733).
      //
      // messageScheduler and fileScheduler hold a raw pObject* armed possibly
      // long before it is due, far outside the two-block grace the #227
      // reclaimer proves for in-flight snapshots, so the pointer is never
      // trusted by itself: a delivery is accepted only when the pinned
      // GraphState holds an object whose pointer *and* instance tag both match
      // what was armed. The tag is what stops a fresh object allocated at a
      // reclaimed address from impersonating the dead one it replaced.
      //
      // Three properties make it fit for that and unfit for anything else:
      //
      //  - It is stamped in the pObject constructor from a process-wide atomic
      //    counter, so every object ever built in this process has a different
      //    one — including objects that never join a patcher, which is what
      //    lets a standalone test rig exercise the guard honestly.
      //  - It is never reused and never reset. 64 bits at one object per
      //    nanosecond is 584 years, so "monotonic forever" is not an
      //    approximation.
      //  - It is never serialised, never copied, and never exposed past the
      //    engine. That is why it may grow without bound where the storage ID
      //    may not: nothing outside the two schedulers ever reads it, so a
      //    large number costs nothing and a dump does not depend on it.
      //
      // kNoInstanceTag (0) is issued to no object; it is the initial value of a
      // scheduler slot's armed tag, so an unarmed slot matches nothing.
      static constexpr std::uint64_t kNoInstanceTag = 0;
      inline std::uint64_t InstanceTag() const {
        return instanceTag_;
      }
      void DumpJson(nlohmann::json::value_type& json);

      // Persistent state an object has beyond its creation parameters (issue
      // #494). Default: none — nothing is written and the serialised object is
      // byte for byte what it was, so only an object that overrides these pays
      // for them.
      //
      // The parameter string cannot carry this: a parameter is what the object
      // was *created* with, not what it has since been told, and rewriting it
      // from run-time state would make loading and re-saving a patch quietly
      // change the arguments the author typed. `.coll` is the first object
      // whose contents are the thing worth saving.
      //
      // Both are control thread only. DumpState runs inside
      // patcherImplementation::DumpJSON under its mtx; RestoreState runs inside
      // ParseJSON, on a freshly built object the audio thread cannot see yet.
      virtual void DumpState(nlohmann::json::value_type&) {}
      virtual void RestoreState(const nlohmann::json::value_type&) {}

      virtual void SetParent(pObject* parent);
      inline const std::string& DataName() {
        return dataName;
      }

      // Documentation surface. The fields are populated in derived-class
      // constructors via the ADD_DESCRIPTION / ADD_CATEGORY macros; consumed
      // by the test_doc_coverage doctest and (later) by binding-side
      // metadata generators. RT-cold.
      const std::string& GetDescription() const {
        return description;
      }
      pCategory GetCategory() const {
        return category;
      }
      const std::vector<ParamDoc>& GetParamDocs() const {
        return parms.GetDocs();
      }

    protected:
      std::vector<inlet> inputs;
      std::vector<outlet> outputs;
      std::map<std::string, std::string> guiProperties;

      Parameters parms;
      pObject* parent;
      // The enclosing subpatcher, or null at the top level — see Container()
      // above (issue #545). Deliberately separate from `parent`, which is the
      // owning patcherImplementation and is what every RT hop goes through:
      // conflating the two would put a subpatcher on the path from an object to
      // its patcher's pinned GraphState, scheduler and clocks, and make that
      // path's cost depend on nesting depth.
      pObject* container = nullptr;
      bool DSP;

      // for storage — see GetID() / kNoStorageID above
      int ID = kNoStorageID;

      // Scheduler identity — see InstanceTag() above. const on purpose: it is
      // the one thing about an object no live edit may transfer, and
      // CopyStorageIdentity would otherwise be one line away from handing a
      // #234 replacement its predecessor's pending messages.
      const std::uint64_t instanceTag_;

      // for incoming data
      std::string dataName;

      // Documentation metadata — see GetDescription() / GetCategory().
      std::string description;
      pCategory category = pCategory::UNSET;
    };

  }
}

// these macro's should make creating patcher objects a bit easier
//
// The members these macros emit all override pure virtuals on pObject --
// PATCHER_CLASS always opens `class X : public pObject` -- so they are marked
// `override`. clang-tidy's modernize-use-override cannot rewrite inside a macro
// body, which is why they were the one group the #573 sweep did not reach; left
// unmarked they also made every patcher object that marks anything else
// `override` trip -Winconsistent-missing-override.
#define PATCHER_CLASS(className, name)                                                             \
  class className : public pObject {                                                               \
  public:                                                                                          \
    className();                                                                                   \
    const char* Type() const override {                                                            \
      return name;                                                                                 \
    }                                                                                              \
    CREATE(className)
#define CREATE(className)                                                                          \
  static pObject* Create() {                                                                       \
    return new className();                                                                        \
  }

#define _DO_MESSAGES void SetMessage(const std::string& message, float value) override;
#define _NO_MESSAGES                                                                               \
  void SetMessage(const std::string&, float) override {}
#define MESSAGES() void className::SetMessage(const std::string& message, float value)

#define _DO_CALCULATE void Calculate(YSE::THREAD thread) override;
#define _NO_CALCULATE                                                                              \
  void Calculate(YSE::THREAD) override {}
#define CALC() void className::Calculate(YSE::THREAD thread)

#define _DO_RESET void ResetDSP() override;
#define RESET()                                                                                    \
  void className::ResetDSP() {                                                                     \
    pObject::ResetDSP();

#define _BUFFER_IN(funcName) void funcName(YSE::DSP::buffer* buffer, int inlet, YSE::THREAD thread);
#define _FLOAT_IN(funcName) void funcName(float value, int inlet, YSE::THREAD thread);
#define _INT_IN(funcName) void funcName(int value, int inlet, YSE::THREAD thread);
#define _BANG_IN(funcName) void funcName(int inlet, YSE::THREAD thread);
#define _LIST_IN(funcName) void funcName(const std::string& value, int inlet, YSE::THREAD thread);

#define BUFFER_IN(funcName)                                                                        \
  void className::funcName(YSE::DSP::buffer* buffer, int inlet, YSE::THREAD thread)
#define FLOAT_IN(funcName) void className::funcName(float value, int inlet, YSE::THREAD thread)
#define INT_IN(funcName) void className::funcName(int value, int inlet, YSE::THREAD thread)
#define BANG_IN(funcName) void className::funcName(int inlet, YSE::THREAD thread)
#define LIST_IN(funcName)                                                                          \
  void className::funcName(const std::string& value, int inlet, YSE::THREAD thread)

#define ADD_IN_0 inputs.emplace_back(this, true, 0)
#define ADD_IN_1 inputs.emplace_back(this, false, 1)
#define ADD_IN_2 inputs.emplace_back(this, false, 2)
#define ADD_IN_3 inputs.emplace_back(this, false, 3)
#define ADD_IN_4 inputs.emplace_back(this, false, 4)
#define ADD_IN_5 inputs.emplace_back(this, false, 5)

#define REG_BUFFER_IN(funcName)                                                                    \
  inputs.back().RegisterBuffer(std::bind(&className::funcName, this, std::placeholders::_1,        \
                                         std::placeholders::_2, std::placeholders::_3))
#define REG_FLOAT_IN(funcName)                                                                     \
  inputs.back().RegisterFloat(std::bind(&className::funcName, this, std::placeholders::_1,         \
                                        std::placeholders::_2, std::placeholders::_3))
#define REG_INT_IN(funcName)                                                                       \
  inputs.back().RegisterInt(std::bind(&className::funcName, this, std::placeholders::_1,           \
                                      std::placeholders::_2, std::placeholders::_3))
#define REG_LIST_IN(funcName)                                                                      \
  inputs.back().RegisterList(std::bind(&className::funcName, this, std::placeholders::_1,          \
                                       std::placeholders::_2, std::placeholders::_3))
#define REG_BANG_IN(funcName)                                                                      \
  inputs.back().RegisterBang(                                                                      \
      std::bind(&className::funcName, this, std::placeholders::_1, std::placeholders::_2))

#define ADD_OUT_BUFFER outputs.emplace_back(this, OUT_TYPE::BUFFER)
#define ADD_OUT_FLOAT outputs.emplace_back(this, OUT_TYPE::FLOAT)
#define ADD_OUT_INT outputs.emplace_back(this, OUT_TYPE::INT)
#define ADD_OUT_BANG outputs.emplace_back(this, OUT_TYPE::BANG)
#define ADD_OUT_LIST outputs.emplace_back(this, OUT_TYPE::LIST)
#define ADD_OUT_ANY outputs.emplace_back(this, OUT_TYPE::ANY)

#define ADD_PARAM(var) parms.Register(var)

// Documentation macros — populate the metadata fields read by the
// test_doc_coverage doctest. RT-cold: all of these only run during
// pObject construction. ``range`` is a free-form string (e.g. "0-127",
// "0.0-1.0", "20-20000 Hz", or "" when not applicable).
#define ADD_DESCRIPTION(text) description = (text)
#define ADD_CATEGORY(cat) category = (cat)
#define INLET_DOC(idx, label, doc, range) inputs[(idx)].SetDoc((label), (doc), (range))
#define OUTLET_DOC(idx, label, doc, range) outputs[(idx)].SetDoc((label), (doc), (range))
#define PARAM_DOC(name, defaultVal, doc, range) parms.SetDoc((name), (defaultVal), (doc), (range))

#define _HAS_GUI std::string GetGuiValue() override;
#define GUI_VALUE() std::string className::GetGuiValue()

// Structured GUI state (issue #551) — the multi-cell form of _HAS_GUI. Declares
// the whole-state read, the cell count and the per-cell read, and asserts the
// write half of the protocol, so an object that uses this macro *must* register
// a list handler on inlet 0 that accepts both the string GetGuiValue() produced
// and "set <index> <value>" for one cell. Read the GUI value protocol block above
// before writing one; the thread contract there is not optional.
#define _HAS_GUI_CELLS                                                                             \
  std::string GetGuiValue() override;                                                              \
  unsigned int GetGuiValueCount() const override;                                                  \
  std::string GetGuiValueAt(unsigned int index) override;                                          \
  bool GuiValueIsSettable() const override {                                                       \
    return true;                                                                                   \
  }
#define GUI_VALUE_COUNT() unsigned int className::GetGuiValueCount() const
#define GUI_VALUE_AT() std::string className::GetGuiValueAt(unsigned int index)

// The *scalar* settable control (issue #551) — _HAS_GUI plus the write half of
// the protocol, and nothing else. A one-cell object needs none of
// _HAS_GUI_CELLS' machinery: the base already answers 1 to GetGuiValueCount()
// and answers GetGuiValue() to GetGuiValueAt(0), by construction rather than by
// copied code. What it still has to do is register a list handler on inlet 0
// that accepts both the string GetGuiValue() produced and "set 0 <value>" —
// which is the whole of the promise this macro makes on the object's behalf, so
// do not reach for it because a control happens to be simple. Read the GUI value
// protocol block above first; the thread contract there is not optional, and it
// carries the one carve-out from the "set" half: a one-cell control holding free
// text (`.textedit`) drops it, because a text cell can hold the keyword. The
// pre-protocol scalar controls were migrated onto this macro by #846 —
// `.slider`, `.i`, `.f`, `.dial`, `.incdec` and `.t` each grew the inlet-0
// list handler the promise requires (`.t` accepts the "on" / "off" its read
// produces). `.b` deliberately stays off it: its GUI value is a
// consume-on-read press report, an event no restore could meaningfully write
// back, so neither half of the promise is its to make.
#define _HAS_GUI_SETTABLE                                                                          \
  std::string GetGuiValue() override;                                                              \
  bool GuiValueIsSettable() const override {                                                       \
    return true;                                                                                   \
  }

#define CONSTRUCT_DSP() className::className() : pObject(true)
#define CONSTRUCT() className::className() : pObject(false)

#define _PARM_CLEAR void ClearParams();
#define _PARM_PARSE void ParseParams();
#define PARM_CLEAR() void className::ClearParams()
#define PARM_PARSE() void className::ParseParams()
#define REG_PARM_CLEAR parms.RegisterClear(std::bind(&className::ClearParams, this))
#define REG_PARM_PARSE parms.RegisterParse(std::bind(&className::ParseParams, this))
