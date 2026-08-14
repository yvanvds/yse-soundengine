// Tests for .array.iter (issue #798) — Max's array.iter ("output an array's
// elements one at a time") on the name-addressed value model .array settled
// (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.iter <name>" resolves the name once,
//     on the control thread, and an `array <name>` message is honoured only
//     when it names the array already bound — ArrayReferenceNames' bounded
//     compare, never a registry lookup on a message path.
//   - **a walk is one send per element, first to last, typed, with the done
//     bang last.** Each element leaves as the int, float or symbol it spells
//     — SendAtom's rule, the whole point of emitting one at a time. The done
//     bang is the carry exception to right-to-left, and it fires even for an
//     empty array — .uzi's rule for a count of zero — but never for a
//     refused walk.
//   - **the walk is snapshotted** — the decision #798 asked to be written
//     down, and the place the renumbering warning bites hardest: a remove
//     arriving mid-walk moves every element above the cursor down by one.
//     A write arriving mid-walk (including from the elements' own subgraph)
//     changes the array but not the walk in flight, and two .array.iter on
//     one name walk independently: the cursor is the loop over the object's
//     own snapshot, never state in the shared store.
//   - **a trigger arriving mid-walk is refused and counted** — the loop-back
//     cord that would otherwise restart the walk under itself, .uzi's
//     re-entrant start rule.
//   - **the walk crosses the control/audio boundary and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread asks for
//     the walk" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayIter.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::BangSink;
using TestHelpers::MultiSink;
using TestHelpers::Wire;
using YSE::PATCHER::gArray;
using YSE::PATCHER::gArrayIter;

namespace {

  // Records every message it receives, in order and with its type, into a
  // shared log — the walk's output *is* a sequence (elements first to last,
  // done bang last, each typed), so a sink that only kept the last message
  // could not tell a complete walk from a truncated one, and one that did
  // not record types could not tell SendAtom's int from its symbol. `prefix`
  // tells two rigs apart in one log, and `onAny` lets a case act from
  // *inside* the walk — the mid-walk mutation and the loop-back trigger are
  // both things the elements' own subgraph does.
  struct SeqSink : YSE::PATCHER::pObject {
    std::vector<std::string>* log = nullptr;
    std::string prefix;
    std::function<void()> onAny;

    SeqSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + "i:" + std::to_string(v));
        if (onAny) onAny();
      });
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + "f:" + std::to_string(static_cast<int>(v * 2)));
        if (onAny) onAny();
      });
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + "l:" + v);
        if (onAny) onAny();
      });
      inputs.back().RegisterBang([this](int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + "<done>");
      });
    }
    const char* Type() const override {
      return "seq_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // An .array and an .array.iter on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters). One sink per outlet, both writing one log, so the element/done
  // order is asserted rather than assumed.
  struct Rig {
    std::vector<std::string> events;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    SeqSink elementOut;
    SeqSink doneOut;
    gArray array;
    gArrayIter iter;

    Rig(const std::string& patcherName, const std::string& name) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      iter.SetParent(&p);
      iter.SetParams(name);
      elementOut.log = &events;
      doneOut.log = &events;
      Wire(iter, 0, elementOut);
      Wire(iter, 1, doneOut);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Run(YSE::THREAD thread = YSE::T_GUI) {
      events.clear();
      iter.GetInlet(0)->SetBang(thread);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.iter: registered, two inlets, two outlets (#798)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_ITER);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".array.iter");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 2);

    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_ITER)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array.iter: the trigger takes bang and list and no bare number, the reference "
            "inlet only list text (#798)") {
    // No int or float handler anywhere: a bare number names no array, and an
    // object that streamed up to 256 messages on any stray number that
    // reached it would be a trap — .uzi's reasoning.
    gArrayIter g;
    const unsigned int trigger = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((trigger & YSE::PATCHER::IT_BANG) != 0);
    CHECK((trigger & YSE::PATCHER::IT_LIST) != 0);
    CHECK((trigger & YSE::PATCHER::IT_INT) == 0);
    CHECK((trigger & YSE::PATCHER::IT_FLOAT) == 0);
    const unsigned int ref = g.GetInlet(1)->GetAcceptedTypes();
    CHECK((ref & YSE::PATCHER::IT_LIST) != 0);
    CHECK((ref & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── the walk ───────────────────────────────────────────────────────────────

  TEST_CASE("array.iter: streams every element first to last, typed, done bang last (#798)") {
    // The walk itself: one send per element, in order, each leaving as the
    // int, float or symbol it spells — SendAtom's rule, so each element
    // reaches the inlets an uncollected value would have reached — and the
    // done bang after the last element, never before.
    Rig rig("ai798a", "a798a");
    rig.Store("append 60 2.5 kick");

    rig.Run();
    REQUIRE(rig.events.size() == 4);
    CHECK(rig.events[0] == "i:60");
    CHECK(rig.events[1] == "f:5"); // 2.5, logged doubled to dodge float text
    CHECK(rig.events[2] == "l:kick");
    CHECK(rig.events[3] == "<done>");
    CHECK(rig.iter.Dropped() == 0);
  }

  TEST_CASE("array.iter: an empty array is just the done bang (#798)") {
    // .uzi's rule for a count of zero: a loop that does not run is not an
    // error, and the "and afterwards, do this" branch is not silently
    // skipped. The same holds for an unnamed .array.iter, whose private
    // array is empty by construction.
    Rig rig("ai798b", "a798b");
    rig.Run();
    REQUIRE(rig.events.size() == 1);
    CHECK(rig.events[0] == "<done>");
    CHECK(rig.iter.Dropped() == 0);

    std::vector<std::string> events;
    SeqSink elementOut;
    SeqSink doneOut;
    elementOut.log = &events;
    doneOut.log = &events;
    gArrayIter unnamed;
    Wire(unnamed, 0, elementOut);
    Wire(unnamed, 1, doneOut);
    CHECK(unnamed.ArrayName().empty());
    CHECK(unnamed.Address().empty());
    unnamed.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(events.size() == 1);
    CHECK(events[0] == "<done>");
    CHECK(unnamed.Dropped() == 0);
  }

  TEST_CASE("array.iter: the reference triggers on the trigger inlet, acknowledges on its "
            "own, and anything else is refused (#798)") {
    Rig rig("ai798c", "a798c");
    rig.Store("append lead piano");

    // The array's own reference — the message its .array emits on a bang —
    // walks, exactly as a bang does.
    const std::uint64_t before = rig.iter.Dropped();
    rig.events.clear();
    rig.iter.GetInlet(0)->SetList("array a798c", YSE::T_GUI);
    REQUIRE(rig.events.size() == 3);
    CHECK(rig.events[0] == "l:lead");
    CHECK(rig.events[1] == "l:piano");
    CHECK(rig.events[2] == "<done>");
    CHECK(rig.iter.Dropped() == before);

    // A reference to an array this object is not bound to, and any other
    // message, are refused and counted, never resolved: a registry lookup is
    // a mutex, and this may be the audio thread. A refused walk emits
    // nothing — no elements and no done bang.
    rig.events.clear();
    rig.iter.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    rig.iter.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.events.empty());
    CHECK(rig.iter.Dropped() == before + 2);

    // The reference inlet acknowledges the bound array silently and refuses
    // anything else — gDictSlice's inlet rule. Neither triggers a walk.
    rig.iter.GetInlet(1)->SetList("array a798c", YSE::T_GUI);
    CHECK(rig.iter.Dropped() == before + 2);
    rig.iter.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(rig.iter.Dropped() == before + 3);
    CHECK(rig.events.empty());
  }

  // ─── the snapshot, and re-entrancy ──────────────────────────────────────────

  TEST_CASE("array.iter: the walk is a snapshot — a mid-walk renumbering write moves the "
            "store, not the walk (#798)") {
    // The decision #798 asked to be written down, proven from the position
    // that forces it: the elements' own subgraph writes into the array being
    // walked. From inside the first element's send a delete removes a later
    // element — renumbering everything above the cursor, the hazard the
    // issue names — and an append adds a new one. The walk still emits every
    // element the array held at the trigger, exactly once, because it walks
    // the snapshot the trigger took, not the live table. The store itself
    // has moved: the deleted element is gone and the appended one is there,
    // which is also the proof that no guard was held across the sends.
    Rig rig("ai798d", "a798d");
    rig.Store("append c4 e4 g4");

    bool acted = false;
    rig.elementOut.onAny = [&] {
      if (acted) return;
      acted = true;
      rig.Store("delete 2");
      rig.Store("append b4");
    };

    rig.Run();
    REQUIRE(rig.events.size() == 4);
    CHECK(rig.events[0] == "l:c4");
    CHECK(rig.events[1] == "l:e4");
    CHECK(rig.events[2] == "l:g4");
    CHECK(rig.events[3] == "<done>");
    CHECK(rig.iter.Dropped() == 0);
    CHECK(rig.array.Count() == 3);
    CHECK(rig.array.ElementAt(0) == "c4");
    CHECK(rig.array.ElementAt(1) == "e4");
    CHECK(rig.array.ElementAt(2) == "b4");
  }

  TEST_CASE("array.iter: a trigger arriving mid-walk is refused and counted (#798)") {
    // The loop-back: a cord from the element outlet round to the inlet
    // re-enters the handler from inside the walk, and letting it through
    // would restart the walk and rewrite the snapshot being walked. Refused
    // and counted — .uzi's re-entrant start rule — and the running walk
    // completes untouched.
    Rig rig("ai798e", "a798e");
    rig.Store("append 1 2");

    bool acted = false;
    rig.elementOut.onAny = [&] {
      if (acted) return;
      acted = true;
      rig.iter.GetInlet(0)->SetBang(YSE::T_GUI);
    };

    const std::uint64_t before = rig.iter.Dropped();
    rig.Run();
    REQUIRE(rig.events.size() == 3);
    CHECK(rig.events[0] == "i:1");
    CHECK(rig.events[1] == "i:2");
    CHECK(rig.events[2] == "<done>");
    CHECK(rig.iter.Dropped() == before + 1);
  }

  TEST_CASE("array.iter: two .array.iter on one name walk independently (#798)") {
    // .coll's per-object pointer rule, in the shape it takes here: the
    // cursor is the loop over the object's own snapshot, so a second
    // .array.iter triggered from inside the first one's walk — on the very
    // same name — runs its whole walk to completion, nested, and neither
    // disturbs the other. Nothing about a walk lives in the shared store.
    std::vector<std::string> events;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ai798f");
    SeqSink elementA;
    SeqSink doneA;
    SeqSink elementB;
    SeqSink doneB;
    elementA.log = &events;
    doneA.log = &events;
    elementB.log = &events;
    doneB.log = &events;
    elementA.prefix = "A:";
    doneA.prefix = "A:";
    elementB.prefix = "B:";
    doneB.prefix = "B:";

    gArray array;
    gArrayIter iterA;
    gArrayIter iterB;
    array.SetParent(&p);
    array.SetParams("a798f");
    iterA.SetParent(&p);
    iterA.SetParams("a798f");
    iterB.SetParent(&p);
    iterB.SetParams("a798f");
    Wire(iterA, 0, elementA);
    Wire(iterA, 1, doneA);
    Wire(iterB, 0, elementB);
    Wire(iterB, 1, doneB);

    array.GetInlet(0)->SetList("append 1 2", YSE::T_GUI);

    bool acted = false;
    elementA.onAny = [&] {
      if (acted) return;
      acted = true;
      iterB.GetInlet(0)->SetBang(YSE::T_GUI);
    };

    iterA.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(events.size() == 6);
    CHECK(events[0] == "A:i:1");
    CHECK(events[1] == "B:i:1");
    CHECK(events[2] == "B:i:2");
    CHECK(events[3] == "B:<done>");
    CHECK(events[4] == "A:i:2");
    CHECK(events[5] == "A:<done>");
    CHECK(iterA.Dropped() == 0);
    CHECK(iterB.Dropped() == 0);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.iter: the address form is the patcher's, and RefreshBinding follows it "
            "(#798)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ai798g_before");

    gArrayIter g;
    g.SetParams("a798g");
    g.SetParent(&p);
    CHECK(g.ArrayName() == "a798g");
    CHECK(g.Address() == "ai798g_before.a798g");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "ai798g_before.a798g");

    p.SetName("ai798g_after");
    g.RefreshBinding();
    CHECK(g.Address() == "ai798g_after.a798g");
  }

  TEST_CASE("array.iter: patcherImplementation::SetName re-anchors it (#798)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. The keeper
    // holds the old-address store (it is not in the patcher's object map, so
    // the rename does not touch it): before the rename a bang streams the
    // keeper's element; after it the walk reads a fresh empty array under
    // the new prefix, so a second bang is the done bang alone.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ai798h_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a798h");
    keeper.GetInlet(0)->SetList("append lead", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_ITER, "a798h");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &sinkHandle, 0);
    p.Connect(h, 1, &sinkHandle, 0);

    h->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "lead");
    CHECK(sink.gotBang);

    p.SetName("ai798h_after");
    sink.reset();
    h->SetBang(0);
    CHECK_FALSE(sink.gotList);
    CHECK(sink.gotBang);
    CHECK(keeper.Count() == 1);
  }

  TEST_CASE("array.iter: wired from the array's reference outlet, banging the array streams "
            "(#798)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the .array's reference outlet into the walk, a bang on the
    // .array, and the elements at the far end. The family's gesture — bang
    // the array, out stream the elements.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    BangSink done;
    YSE::pHandle doneHandle(&done);
    YSE::patcher p;
    p.create(2);
    p.name("ai798i");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a798i");
    YSE::pHandle* iter = p.CreateObject(YSE::OBJ::G_ARRAY_ITER, "a798i");
    REQUIRE(array != nullptr);
    REQUIRE(iter != nullptr);
    p.Connect(array, 1, iter, 0);
    p.Connect(iter, 0, &sinkHandle, 0);
    p.Connect(iter, 1, &doneHandle, 0);

    array->SetListData(0, "append 60 kick");
    array->SetBang(0);
    // The last element to leave before the done bang; the int 60 went first.
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "kick");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 60);
    CHECK(done.gotBang);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.iter: a walk asked for over in-patcher delivery lands on T_DSP (#798)") {
    // A .r feeding the walk dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread asks for the walk" is the ordinary
    // case, and the whole path is a snapshot, bounded assigns and typed
    // sends over a scratch the object already owns.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ai798j");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a798j");
    keeper.GetInlet(0)->SetList("append 60 64", YSE::T_GUI);

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go798j");
    YSE::pHandle* iter = p.CreateObject(YSE::OBJ::G_ARRAY_ITER, "a798j");
    REQUIRE(recv != nullptr);
    REQUIRE(iter != nullptr);
    p.Connect(recv, 0, iter, 0);
    p.Connect(iter, 0, &sinkHandle, 0);
    p.Connect(iter, 1, &sinkHandle, 0);

    p.PassData(std::string("array a798j"), "go798j", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 64); // the last element to leave
    CHECK(sink.gotBang);
  }

  TEST_CASE("array.iter: no message path allocates (#798)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: the walk (bang and reference alike, int, float and
    // symbol elements and the done bang out), the empty walk, the wrong-name
    // refusal, the unknown message and the reference-inlet acknowledgement —
    // on T_DSP, in-patcher delivery's thread.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeAI798";
    const std::string wrongName = "array somewhere_else_long";
    const std::string unknown = "frobnicate something quite long indeed";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ai798k");
    MultiSink elementOut;
    BangSink doneOut;
    MultiSink emptyElement;
    BangSink emptyDone;
    gArray array;
    gArrayIter iter;
    gArrayIter unnamed;
    array.SetParent(&p);
    array.SetParams("probeAI798");
    iter.SetParent(&p);
    iter.SetParams("probeAI798");
    unnamed.SetParent(&p);
    Wire(iter, 0, elementOut);
    Wire(iter, 1, doneOut);
    Wire(unnamed, 0, emptyElement);
    Wire(unnamed, 1, emptyDone);

    // An int, a float and a symbol, so all three of SendAtom's paths run
    // under the probe — the symbol wide enough to outgrow every small-string
    // buffer the scratch assign must absorb.
    array.GetInlet(0)->SetList("append 60 2.5 a_symbol_past_every_small_string_buffer", YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    iter.GetInlet(0)->SetBang(YSE::T_GUI);
    unnamed.GetInlet(0)->SetBang(YSE::T_GUI);
    iter.GetInlet(0)->SetList(reference, YSE::T_GUI);
    iter.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    iter.GetInlet(0)->SetList(unknown, YSE::T_GUI);
    iter.GetInlet(1)->SetList(reference, YSE::T_GUI);
    const std::uint64_t before = iter.Dropped();
    const int bangs = doneOut.bangCount;
    const int emptyBangs = emptyDone.bangCount;

    elementOut.reset();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      iter.GetInlet(0)->SetBang(YSE::T_DSP);
      unnamed.GetInlet(0)->SetBang(YSE::T_DSP);
      iter.GetInlet(0)->SetList(reference, YSE::T_DSP);
      iter.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      iter.GetInlet(0)->SetList(unknown, YSE::T_DSP);
      iter.GetInlet(1)->SetList(reference, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. Two walks ran to their done bang (all three
    // element types out), the empty walk banged, and both refusals were
    // counted.
    CHECK(elementOut.gotInt);
    CHECK(elementOut.gotFloat);
    CHECK(elementOut.gotList);
    CHECK(elementOut.listValue == "a_symbol_past_every_small_string_buffer");
    CHECK(doneOut.bangCount == bangs + 2);
    CHECK(emptyDone.bangCount == emptyBangs + 1);
    CHECK_FALSE(emptyElement.gotList);
    CHECK(iter.Dropped() == before + 2);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.iter: params survive a DumpJSON / ParseJSON round trip (#798)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_ARRAY_ITER, "walkme798");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".array.iter"));
    CHECK(copy->GetParams() == std::string("walkme798"));
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 2);
  }

  TEST_CASE("array.iter: carries complete documentation metadata (#798)") {
    gArrayIter g;
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "name");
  }
}
