// Tests for .dict.iter (issue #773) — Max's dict.iter ("stream the content
// of a dictionary") on the name-addressed value model .dict settled (#550).
//
// What has to be proven, and what every case below is one of:
//
//   - **the dictionary is bound from the creation argument.** A dictionary
//     never travels down a cord, so ".dict.iter <name>" resolves the name
//     once, on the control thread, and a `dictionary <name>` message is
//     honoured only when it names the dictionary already bound —
//     DictReferenceNames' bounded compare, never a registry lookup on a
//     message path.
//   - **a walk is one "<path> <value...>" list per entry, in storage order,
//     with the done bang last.** The done bang is the carry exception to
//     right-to-left, and it fires even for an empty dictionary — .uzi's rule
//     for a count of zero — but never for a refused walk.
//   - **the walk is snapshotted** — the decision #773 asked to be written
//     down. A set or delete arriving mid-walk (including from the pairs' own
//     subgraph) changes the dictionary but not the walk in flight, and two
//     .dict.iter on one name walk independently: the cursor is the loop over
//     the object's own snapshot, never state in the shared store.
//   - **a trigger arriving mid-walk is refused and counted** — the loop-back
//     cord that would otherwise restart the walk under itself, .uzi's
//     re-entrant start rule.
//   - **the walk crosses the control/audio boundary and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread asks for
//     the walk" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names a dictionary uses names of its own — one case's
// contents must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "patcher/genericObjects/gDict.h"
#include "patcher/genericObjects/gDictIter.h"
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
using YSE::PATCHER::gDict;
using YSE::PATCHER::gDictIter;

namespace {

  // Records every list and bang it receives, in order, into a shared log —
  // OrderSink's idea carried one step further: the walk's output *is* a
  // sequence (pairs in storage order, done bang last), so a sink that only
  // kept the last message could not tell a complete walk from a truncated
  // one. `prefix` tells two rigs apart in one log, and `onList` lets a case
  // act from *inside* the walk — the mid-walk mutation and the loop-back
  // trigger are both things the pairs' own subgraph does.
  struct SeqSink : YSE::PATCHER::pObject {
    std::vector<std::string>* log = nullptr;
    std::string prefix;
    std::function<void(const std::string&)> onList;

    SeqSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + v);
        if (onList) onList(v);
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

  // A .dict and a .dict.iter on one name, sharing one patcherImplementation
  // so the name actually binds ("<patcherName>.<name>" needs a patcher to
  // prefix with — a parentless object stays private). The sinks are declared
  // before the objects so they are torn down last, while the outlets wired
  // to them still exist (see sinks.hpp on why that matters). One sink per
  // outlet, both writing one log, so the pair/done order is asserted rather
  // than assumed.
  struct Rig {
    std::vector<std::string> events;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    SeqSink pairOut;
    SeqSink doneOut;
    gDict dict;
    gDictIter iter;

    Rig(const std::string& patcherName, const std::string& name) {
      p.SetName(patcherName);
      dict.SetParent(&p);
      dict.SetParams(name);
      iter.SetParent(&p);
      iter.SetParams(name);
      pairOut.log = &events;
      doneOut.log = &events;
      Wire(iter, 0, pairOut);
      Wire(iter, 1, doneOut);
    }

    void Store(const std::string& message) {
      dict.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Run(YSE::THREAD thread = YSE::T_GUI) {
      events.clear();
      iter.GetInlet(0)->SetBang(thread);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("dict.iter: registered, one inlet, two outlets (#773)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_ITER);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".dict.iter");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 2);
  }

  TEST_CASE("dict.iter: appears in the registry's name list (#773)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_DICT_ITER)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("dict.iter: the inlet takes bang and list, and no bare number (#773)") {
    // No int or float handler: a bare number names no dictionary, and an
    // object that streamed up to 256 messages on any stray number that
    // reached it would be a trap — .uzi's reasoning.
    gDictIter g;
    const unsigned int accepted = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_BANG) != 0);
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
    CHECK((accepted & YSE::PATCHER::IT_INT) == 0);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) == 0);
  }

  // ─── the walk ───────────────────────────────────────────────────────────────

  TEST_CASE("dict.iter: streams every pair in storage order, done bang last (#773)") {
    // The walk itself: one "<path> <value...>" list per entry, in storage
    // order — the order paths were first written — and the done bang after
    // the last pair, never before. Whole "::" paths, a multi-token value as
    // the list it is, and an entry holding nothing as its path alone.
    Rig rig("di773a", "d773a");
    rig.Store("set voice::1::freq 440");
    rig.Store("set chord 0 4 7");
    rig.Store("set muted");

    rig.Run();
    REQUIRE(rig.events.size() == 4);
    CHECK(rig.events[0] == "voice::1::freq 440");
    CHECK(rig.events[1] == "chord 0 4 7");
    CHECK(rig.events[2] == "muted");
    CHECK(rig.events[3] == "<done>");
    CHECK(rig.iter.Dropped() == 0);
  }

  TEST_CASE("dict.iter: an empty dictionary is just the done bang (#773)") {
    // .uzi's rule for a count of zero: a loop that does not run is not an
    // error, and the "and afterwards, do this" branch is not silently
    // skipped. The same holds for an unnamed .dict.iter, whose private
    // dictionary is empty by construction.
    Rig rig("di773b", "d773b");
    rig.Run();
    REQUIRE(rig.events.size() == 1);
    CHECK(rig.events[0] == "<done>");
    CHECK(rig.iter.Dropped() == 0);

    std::vector<std::string> events;
    SeqSink pairOut;
    SeqSink doneOut;
    pairOut.log = &events;
    doneOut.log = &events;
    gDictIter unnamed;
    Wire(unnamed, 0, pairOut);
    Wire(unnamed, 1, doneOut);
    CHECK(unnamed.DictName().empty());
    CHECK(unnamed.Address().empty());
    unnamed.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(events.size() == 1);
    CHECK(events[0] == "<done>");
    CHECK(unnamed.Dropped() == 0);
  }

  TEST_CASE("dict.iter: the reference triggers, anything else is refused (#773)") {
    Rig rig("di773c", "d773c");
    rig.Store("set lead piano");

    // The dictionary's own reference — the message its .dict emits on a
    // bang — walks, exactly as a bang does.
    const std::uint64_t before = rig.iter.Dropped();
    rig.events.clear();
    rig.iter.GetInlet(0)->SetList("dictionary d773c", YSE::T_GUI);
    REQUIRE(rig.events.size() == 2);
    CHECK(rig.events[0] == "lead piano");
    CHECK(rig.events[1] == "<done>");
    CHECK(rig.iter.Dropped() == before);

    // A reference to a dictionary this object is not bound to, and any
    // other message, are refused and counted, never resolved: a registry
    // lookup is a mutex, and this may be the audio thread. A refused walk
    // emits nothing — no pairs and no done bang.
    rig.events.clear();
    rig.iter.GetInlet(0)->SetList("dictionary somewhere_else", YSE::T_GUI);
    rig.iter.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.events.empty());
    CHECK(rig.iter.Dropped() == before + 2);
  }

  // ─── the snapshot, and re-entrancy ──────────────────────────────────────────

  TEST_CASE("dict.iter: the walk is a snapshot — a mid-walk mutation moves the store, "
            "not the walk (#773)") {
    // The decision #773 asked to be written down, proven from the position
    // that forces it: the pairs' own subgraph stores into the dictionary
    // being walked. From inside the first pair's send a delete removes a
    // later entry and a set adds a new one — and the walk still emits every
    // entry the dictionary held at the trigger, exactly once, because it
    // walks the snapshot the trigger took, not the live table. The store
    // itself has moved: the deleted entry is gone and the added one is
    // there, which is also the proof that no guard was held across the
    // sends.
    Rig rig("di773d", "d773d");
    rig.Store("set a 1");
    rig.Store("set b 2");
    rig.Store("set c 3");

    bool acted = false;
    rig.pairOut.onList = [&](const std::string&) {
      if (acted) return;
      acted = true;
      rig.Store("delete c");
      rig.Store("set d 4");
    };

    rig.Run();
    REQUIRE(rig.events.size() == 4);
    CHECK(rig.events[0] == "a 1");
    CHECK(rig.events[1] == "b 2");
    CHECK(rig.events[2] == "c 3");
    CHECK(rig.events[3] == "<done>");
    CHECK(rig.iter.Dropped() == 0);
    CHECK(rig.dict.Count() == 3);
    CHECK(rig.dict.Lookup("c").empty());
    CHECK(rig.dict.Lookup("d") == "4");
  }

  TEST_CASE("dict.iter: a trigger arriving mid-walk is refused and counted (#773)") {
    // The loop-back: a cord from the pair outlet round to the inlet
    // re-enters the handler from inside the walk, and letting it through
    // would restart the walk and rewrite the snapshot being walked. Refused
    // and counted — .uzi's re-entrant start rule — and the running walk
    // completes untouched.
    Rig rig("di773e", "d773e");
    rig.Store("set a 1");
    rig.Store("set b 2");

    bool acted = false;
    rig.pairOut.onList = [&](const std::string&) {
      if (acted) return;
      acted = true;
      rig.iter.GetInlet(0)->SetBang(YSE::T_GUI);
    };

    const std::uint64_t before = rig.iter.Dropped();
    rig.Run();
    REQUIRE(rig.events.size() == 3);
    CHECK(rig.events[0] == "a 1");
    CHECK(rig.events[1] == "b 2");
    CHECK(rig.events[2] == "<done>");
    CHECK(rig.iter.Dropped() == before + 1);
  }

  TEST_CASE("dict.iter: two .dict.iter on one name walk independently (#773)") {
    // .coll's per-object pointer rule, in the shape it takes here: the
    // cursor is the loop over the object's own snapshot, so a second
    // .dict.iter triggered from inside the first one's walk — on the very
    // same name — runs its whole walk to completion, nested, and neither
    // disturbs the other. Nothing about a walk lives in the shared store.
    std::vector<std::string> events;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("di773f");
    SeqSink pairA;
    SeqSink doneA;
    SeqSink pairB;
    SeqSink doneB;
    pairA.log = &events;
    doneA.log = &events;
    pairB.log = &events;
    doneB.log = &events;
    pairA.prefix = "A:";
    doneA.prefix = "A:";
    pairB.prefix = "B:";
    doneB.prefix = "B:";

    gDict dict;
    gDictIter iterA;
    gDictIter iterB;
    dict.SetParent(&p);
    dict.SetParams("d773f");
    iterA.SetParent(&p);
    iterA.SetParams("d773f");
    iterB.SetParent(&p);
    iterB.SetParams("d773f");
    Wire(iterA, 0, pairA);
    Wire(iterA, 1, doneA);
    Wire(iterB, 0, pairB);
    Wire(iterB, 1, doneB);

    dict.GetInlet(0)->SetList("set a 1", YSE::T_GUI);
    dict.GetInlet(0)->SetList("set b 2", YSE::T_GUI);

    bool acted = false;
    pairA.onList = [&](const std::string&) {
      if (acted) return;
      acted = true;
      iterB.GetInlet(0)->SetBang(YSE::T_GUI);
    };

    iterA.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(events.size() == 6);
    CHECK(events[0] == "A:a 1");
    CHECK(events[1] == "B:a 1");
    CHECK(events[2] == "B:b 2");
    CHECK(events[3] == "B:<done>");
    CHECK(events[4] == "A:b 2");
    CHECK(events[5] == "A:<done>");
    CHECK(iterA.Dropped() == 0);
    CHECK(iterB.Dropped() == 0);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("dict.iter: the address form is the patcher's, and RefreshBinding follows it (#773)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("di773g_before");

    gDictIter g;
    g.SetParams("d773g");
    g.SetParent(&p);
    CHECK(g.DictName() == "d773g");
    CHECK(g.Address() == "di773g_before.d773g");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "di773g_before.d773g");

    p.SetName("di773g_after");
    g.RefreshBinding();
    CHECK(g.Address() == "di773g_after.d773g");
  }

  TEST_CASE("dict.iter: patcherImplementation::SetName re-anchors it (#773)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by
    // the patcher, without anybody calling RefreshBinding by hand. The
    // keeper holds the old-address store (it is not in the patcher's object
    // map, so the rename does not touch it): before the rename a bang
    // streams the keeper's entry; after it the walk reads a fresh empty
    // dictionary under the new prefix, so a second bang is the done bang
    // alone.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("di773h_before");

    gDict keeper;
    keeper.SetParent(&p);
    keeper.SetParams("d773h");
    keeper.GetInlet(0)->SetList("set lead piano", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_ITER, "d773h");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &sinkHandle, 0);
    p.Connect(h, 1, &sinkHandle, 0);

    h->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "lead piano");
    CHECK(sink.gotBang);

    p.SetName("di773h_after");
    sink.reset();
    h->SetBang(0);
    CHECK_FALSE(sink.gotList);
    CHECK(sink.gotBang);
  }

  TEST_CASE("dict.iter: wired from the dict's reference outlet, banging the dict streams (#773)") {
    // The flow a patch actually wires, end to end through the public
    // patcher API: the .dict's reference outlet into the walk, a bang on
    // the .dict, and the pairs at the far end. Max's own gesture — bang the
    // dict, out stream the contents.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    BangSink done;
    YSE::pHandle doneHandle(&done);
    YSE::patcher p;
    p.create(2);
    p.name("di773i");
    YSE::pHandle* dict = p.CreateObject(YSE::OBJ::G_DICT, "d773i");
    YSE::pHandle* iter = p.CreateObject(YSE::OBJ::G_DICT_ITER, "d773i");
    REQUIRE(dict != nullptr);
    REQUIRE(iter != nullptr);
    p.Connect(dict, 1, iter, 0);
    p.Connect(iter, 0, &sinkHandle, 0);
    p.Connect(iter, 1, &doneHandle, 0);

    dict->SetListData(0, "set lead piano");
    dict->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "lead piano");
    CHECK(done.gotBang);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("dict.iter: a walk asked for over in-patcher delivery lands on T_DSP (#773)") {
    // A .r feeding the walk dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread asks for the walk" is the ordinary
    // case, and the whole path is a snapshot, bounded appends and sends of
    // a buffer the object already owns.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("di773j");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go773j");
    YSE::pHandle* dict = p.CreateObject(YSE::OBJ::G_DICT, "d773j");
    YSE::pHandle* iter = p.CreateObject(YSE::OBJ::G_DICT_ITER, "d773j");
    REQUIRE(recv != nullptr);
    REQUIRE(dict != nullptr);
    REQUIRE(iter != nullptr);
    p.Connect(recv, 0, iter, 0);
    p.Connect(iter, 0, &sinkHandle, 0);
    p.Connect(iter, 1, &sinkHandle, 0);

    dict->SetListData(0, "set lead piano");

    p.PassData(std::string("dictionary d773j"), "go773j", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "lead piano");
    CHECK(sink.gotBang);
  }

  TEST_CASE("dict.iter: no message path allocates (#773)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: the walk (bang and reference alike, pairs and
    // done bang out), the wrong-name refusal and the unknown message.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer, and that buffer is not
    // the same width everywhere (15 on libstdc++, 22 on libc++).
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "dictionary probeD773";
    const std::string wrongName = "dictionary somewhere_else_long";
    const std::string unknown = "frobnicate something quite long indeed";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("di773k");
    MultiSink pairOut;
    BangSink doneOut;
    gDict dict;
    gDictIter iter;
    dict.SetParent(&p);
    dict.SetParams("probeD773");
    iter.SetParent(&p);
    iter.SetParams("probeD773");
    Wire(iter, 0, pairOut);
    Wire(iter, 1, doneOut);

    dict.GetInlet(0)->SetList("set voice::1::freq a value past every small-string buffer",
                              YSE::T_GUI);
    dict.GetInlet(0)->SetList("set chord 0 4 7 12", YSE::T_GUI);

    // Warm every path — including the sink's list assignment — so
    // first-call machinery is not what the probe catches.
    iter.GetInlet(0)->SetBang(YSE::T_GUI);
    iter.GetInlet(0)->SetList(reference, YSE::T_GUI);
    iter.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    iter.GetInlet(0)->SetList(unknown, YSE::T_GUI);
    const std::uint64_t before = iter.Dropped();
    const int bangs = doneOut.bangCount;

    pairOut.reset();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      iter.GetInlet(0)->SetBang(YSE::T_DSP);
      iter.GetInlet(0)->SetList(reference, YSE::T_DSP);
      iter.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      iter.GetInlet(0)->SetList(unknown, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. Two walks ran to their done bang, and both
    // refusals were counted.
    CHECK(pairOut.gotList);
    CHECK(pairOut.listValue == "chord 0 4 7 12");
    CHECK(doneOut.bangCount == bangs + 2);
    CHECK(iter.Dropped() == before + 2);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("dict.iter: params survive a DumpJSON / ParseJSON round trip (#773)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_DICT_ITER, "walkme773");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".dict.iter"));
    CHECK(copy->GetParams() == std::string("walkme773"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 2);
  }

  TEST_CASE("dict.iter: carries complete documentation metadata (#773)") {
    gDictIter g;
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "name");
  }
}
