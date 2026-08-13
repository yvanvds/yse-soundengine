// Tests for .dict.unpack (issue #781) — Max's dict.unpack on the
// name-addressed value model .dict settled (#550). The dictionary
// counterpart of .unpack, and .dict.pack's inverse: a structure arriving as
// one reference becomes several separate values a patch can wire
// individually.
//
// What has to be proven, and what every case below is one of:
//
//   - **the dictionary is bound from the first creation argument.** A
//     dictionary never travels down a cord, so ".dict.unpack <name> <path>
//     ..." resolves the name once, on the control thread, and a `dictionary
//     <name>` message is honoured only when it names the dictionary already
//     bound — DictReferenceNames' bounded compare, never a registry lookup
//     on a message path.
//   - **each path's value leaves its own outlet, typed by its spelling.**
//     SendAtom's classification — a single number leaves as the int or
//     float it spells, anything else as list text — right to left,
//     gUnpack's ordering, and a path the dictionary does not hold sends
//     nothing rather than a zero, .dict's miss rule.
//   - **the read crosses the control/audio boundary and nothing
//     allocates.** In-patcher delivery dispatches on T_DSP, so "the audio
//     thread unpacks a dictionary" is the ordinary case.
//
// No audio device required. The registry is process-wide, so every case
// that names a dictionary uses names of its own.

#include <doctest/doctest.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "patcher/genericObjects/gDict.h"
#include "patcher/genericObjects/gDictPack.h"
#include "patcher/genericObjects/gDictUnpack.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using TestHelpers::OrderSink;
using TestHelpers::Wire;
using YSE::PATCHER::gDict;
using YSE::PATCHER::gDictPack;
using YSE::PATCHER::gDictUnpack;

namespace {

  // A MultiSink that can act from *inside* a send — the loop-back trigger
  // is a thing an outlet's own subgraph does.
  struct HookSink : YSE::PATCHER::pObject {
    int count = 0;
    std::function<void()> onValue;

    HookSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int, int, YSE::THREAD) { Hit(); });
      inputs.back().RegisterFloat([this](float, int, YSE::THREAD) { Hit(); });
      inputs.back().RegisterList([this](const std::string&, int, YSE::THREAD) { Hit(); });
    }
    const char* Type() const override {
      return "hook_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

  private:
    void Hit() {
      count++;
      if (onValue) onValue();
    }
  };

  // A .dict and a .dict.unpack on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private).
  // Three key paths, one sink each. The sinks are declared before the
  // objects so they are torn down last, while the outlets wired to them
  // still exist (see sinks.hpp on why that matters).
  struct Rig {
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    MultiSink outA;
    MultiSink outB;
    MultiSink outC;
    gDict dict;
    gDictUnpack unpack;

    Rig(const std::string& patcherName, const std::string& name, const std::string& keys) {
      p.SetName(patcherName);
      dict.SetParent(&p);
      dict.SetParams(name);
      unpack.SetParent(&p);
      unpack.SetParams(name + " " + keys);
      Wire(unpack, 0, outA);
      Wire(unpack, 1, outB);
      Wire(unpack, 2, outC);
    }

    void Store(const std::string& message) {
      dict.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Bang(YSE::THREAD thread = YSE::T_GUI) {
      unpack.GetInlet(0)->SetBang(thread);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("dict.unpack: registered, one inlet, one outlet per key path (#781)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_UNPACK, "d781a freq gain pan");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".dict.unpack");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 3);
  }

  TEST_CASE("dict.unpack: appears in the registry's name list (#781)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_DICT_UNPACK)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("dict.unpack: with no key arguments there are no outlets (#781)") {
    // There is no default key to invent — .dict.route's reasoning — and a
    // trigger on the bare object reads nothing, silently.
    {
      gDictUnpack g;
      CHECK(g.KeyCount() == 0);
      CHECK(g.NumOutputs() == 0);
    }
    {
      gDictUnpack g;
      g.SetParams("d781b");
      CHECK(g.KeyCount() == 0);
      CHECK(g.NumOutputs() == 0);
      g.GetInlet(0)->SetBang(YSE::T_GUI);
      CHECK(g.Unpacked() == 0);
      CHECK(g.Dropped() == 0);
    }
  }

  TEST_CASE("dict.unpack: the inlet takes bang and list, and no bare number (#781)") {
    // No int or float handler: a bare number names no dictionary — the
    // family's rule.
    gDictUnpack g;
    const unsigned int accepted = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_BANG) != 0);
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
    CHECK((accepted & YSE::PATCHER::IT_INT) == 0);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) == 0);
  }

  // ─── creation arguments ─────────────────────────────────────────────────────

  TEST_CASE("dict.unpack: the name then the key paths, and '' resets (#781)") {
    gDictUnpack g;
    g.SetParams("d781c freq voice::1::freq");
    CHECK(g.DictName() == "d781c");
    REQUIRE(g.KeyCount() == 2);
    CHECK(g.KeyAt(0) == "freq");
    CHECK(g.KeyAt(1) == "voice::1::freq");
    CHECK(g.NumOutputs() == 2);

    // SetParams("") has to leave the no-argument object behind rather than
    // one still holding the previous binding and outlets.
    g.SetParams("");
    CHECK(g.DictName().empty());
    CHECK(g.Address().empty());
    CHECK(g.KeyCount() == 0);
    CHECK(g.NumOutputs() == 0);
  }

  // ─── the unpack ─────────────────────────────────────────────────────────────

  TEST_CASE("dict.unpack: each path's value leaves its outlet, typed by its spelling (#781)") {
    // SendAtom's classification: a value spelling a whole number leaves as
    // an int, one with a decimal point as a float, and anything else — a
    // symbol, or a whole list, a dictionary value being list text — as one
    // list message, .dict.pack's "a list is a value, not a spread" read
    // backwards.
    Rig rig("du781d", "d781d", "freq gain name");
    rig.Store("set freq 440");
    rig.Store("set gain 0.5");
    rig.Store("set name warm pad");
    rig.Bang();

    CHECK(rig.unpack.Unpacked() == 1);
    CHECK(rig.unpack.Dropped() == 0);
    CHECK(rig.outA.gotInt);
    CHECK(rig.outA.intValue == 440);
    CHECK_FALSE(rig.outA.gotList);
    CHECK(rig.outB.gotFloat);
    CHECK(rig.outB.floatValue == doctest::Approx(0.5f));
    CHECK(rig.outC.gotList);
    CHECK(rig.outC.listValue == "warm pad");
  }

  TEST_CASE("dict.unpack: outlets fire right to left (#781)") {
    // gUnpack's ordering, Max's universal order — what lets the right-hand
    // values land in cold inlets before the leftmost one sets the result
    // off. A test that only counted hits could not tell the orders apart,
    // which is what OrderSink's shared log is for.
    std::vector<char> log;
    OrderSink outA;
    OrderSink outB;
    OrderSink outC;
    outA.log = &log;
    outA.tag = 'a';
    outB.log = &log;
    outB.tag = 'b';
    outC.log = &log;
    outC.tag = 'c';

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("du781e");
    gDict dict;
    dict.SetParent(&p);
    dict.SetParams("d781e");
    gDictUnpack unpack;
    unpack.SetParent(&p);
    unpack.SetParams("d781e freq gain pan");
    Wire(unpack, 0, outA);
    Wire(unpack, 1, outB);
    Wire(unpack, 2, outC);

    dict.GetInlet(0)->SetList("set freq 440", YSE::T_GUI);
    dict.GetInlet(0)->SetList("set gain 0.5", YSE::T_GUI);
    dict.GetInlet(0)->SetList("set pan -0.3", YSE::T_GUI);
    unpack.GetInlet(0)->SetBang(YSE::T_GUI);

    REQUIRE(log.size() == 3);
    CHECK(log[0] == 'c');
    CHECK(log[1] == 'b');
    CHECK(log[2] == 'a');
  }

  TEST_CASE("dict.unpack: a path the dictionary does not hold sends nothing (#781)") {
    // .dict's miss rule: a fabricated 0 would read downstream as a value
    // the dictionary held. The hits still fire; the miss stays silent.
    Rig rig("du781f", "d781f", "freq gain pan");
    rig.Store("set freq 440");
    rig.Store("set pan -0.3");
    rig.Bang();

    CHECK(rig.unpack.Unpacked() == 1);
    CHECK(rig.outA.gotInt);
    CHECK_FALSE(rig.outB.gotInt);
    CHECK_FALSE(rig.outB.gotFloat);
    CHECK_FALSE(rig.outB.gotList);
    CHECK(rig.outC.gotFloat);
  }

  TEST_CASE("dict.unpack: an entry holding nothing sends nothing (#781)") {
    // "set <path>" stores a path with no value — a hit to .dict's get, but
    // an object with nothing to say says nothing.
    Rig rig("du781g", "d781g", "freq gain pan");
    rig.Store("set gain");
    rig.Bang();
    CHECK(rig.unpack.Unpacked() == 1);
    CHECK_FALSE(rig.outB.gotInt);
    CHECK_FALSE(rig.outB.gotFloat);
    CHECK_FALSE(rig.outB.gotList);
  }

  TEST_CASE("dict.unpack: a nested path reads one entry deep in the tree (#781)") {
    // Nesting lives in the key — "voice::1::freq" is one flat entry, and a
    // key-path argument spells it unchanged.
    Rig rig("du781h", "d781h", "voice::1::freq voice::2::freq meta::title");
    rig.Store("set voice::1::freq 440");
    rig.Store("set meta::title warm");
    rig.Bang();
    CHECK(rig.outA.gotInt);
    CHECK(rig.outA.intValue == 440);
    CHECK_FALSE(rig.outB.gotInt);
    CHECK(rig.outC.gotList);
    CHECK(rig.outC.listValue == "warm");
  }

  TEST_CASE("dict.unpack: a duplicate path is two outlets reading one entry (#781)") {
    Rig rig("du781i", "d781i", "freq freq gain");
    rig.Store("set freq 440");
    rig.Bang();
    CHECK(rig.outA.gotInt);
    CHECK(rig.outA.intValue == 440);
    CHECK(rig.outB.gotInt);
    CHECK(rig.outB.intValue == 440);
  }

  TEST_CASE("dict.unpack: the reference unpacks, anything else is refused (#781)") {
    Rig rig("du781j", "d781j", "freq gain pan");
    rig.Store("set freq 440");

    // The dictionary's own reference — the message its .dict emits on a
    // bang — unpacks, exactly as a bang does.
    const std::uint64_t before = rig.unpack.Dropped();
    rig.unpack.GetInlet(0)->SetList("dictionary d781j", YSE::T_GUI);
    CHECK(rig.unpack.Unpacked() == 1);
    CHECK(rig.outA.gotInt);
    CHECK(rig.unpack.Dropped() == before);

    // A reference to a dictionary this object is not bound to, and any
    // other message, are refused and counted, never resolved: a registry
    // lookup is a mutex, and this may be the audio thread. A refused
    // trigger sends nothing.
    rig.unpack.GetInlet(0)->SetList("dictionary somewhere_else", YSE::T_GUI);
    rig.unpack.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.unpack.Unpacked() == 1);
    CHECK(rig.unpack.Dropped() == before + 2);
  }

  TEST_CASE("dict.unpack: a trigger looping back from an outlet is refused (#781)") {
    // The object emits in a loop, so a cord from an outlet back to the
    // inlet re-enters the handler from inside the walk. The guard held
    // across snapshot and sends drops it, counted.
    HookSink out;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("du781k");
    gDict dict;
    dict.SetParent(&p);
    dict.SetParams("d781k");
    gDictUnpack unpack;
    unpack.SetParent(&p);
    unpack.SetParams("d781k freq");
    Wire(unpack, 0, out);
    dict.GetInlet(0)->SetList("set freq 440", YSE::T_GUI);

    int reentered = 0;
    out.onValue = [&unpack, &reentered]() {
      if (reentered++ > 0) return; // once is the proof; don't recurse forever
      unpack.GetInlet(0)->SetBang(YSE::T_GUI);
    };

    const std::uint64_t before = unpack.Dropped();
    unpack.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(out.count == 1);
    CHECK(unpack.Unpacked() == 1);
    CHECK(unpack.Dropped() == before + 1);
  }

  // ─── the pair ───────────────────────────────────────────────────────────────

  TEST_CASE("dict.unpack: .dict.pack wired into .dict.unpack is the identity (#781)") {
    // The pair gesture the two objects were named for: a pack's reference
    // outlet feeds the unpack, so writing the pack's hot inlet pops the
    // values out the other side — several cords in, several cords out, one
    // dictionary in between.
    MultiSink outFreq;
    MultiSink outGain;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("du781l");
    gDictPack pack;
    pack.SetParent(&p);
    pack.SetParams("d781l freq gain");
    gDictUnpack unpack;
    unpack.SetParent(&p);
    unpack.SetParams("d781l freq gain");
    Wire(pack, 0, unpack);
    Wire(unpack, 0, outFreq);
    Wire(unpack, 1, outGain);

    // Load the cold inlet, then release from the hot one — gPack's rule.
    pack.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    pack.GetInlet(0)->SetInt(440, YSE::T_GUI);

    CHECK(unpack.Unpacked() == 1);
    CHECK(outFreq.gotInt);
    CHECK(outFreq.intValue == 440);
    CHECK(outGain.gotFloat);
    CHECK(outGain.floatValue == doctest::Approx(0.5f));
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE(
      "dict.unpack: the address form is the patcher's, and RefreshBinding follows it (#781)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("du781m_before");

    gDictUnpack g;
    g.SetParams("d781m freq");
    g.SetParent(&p);
    CHECK(g.DictName() == "d781m");
    CHECK(g.Address() == "du781m_before.d781m");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "du781m_before.d781m");

    p.SetName("du781m_after");
    g.RefreshBinding();
    CHECK(g.Address() == "du781m_after.d781m");
  }

  TEST_CASE("dict.unpack: patcherImplementation::SetName re-anchors it (#781)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by
    // the patcher, without anybody calling RefreshBinding by hand. The
    // keeper holds the old-address store; after the rename the unpack reads
    // a fresh empty dictionary under the new prefix — every path misses.
    MultiSink out;
    YSE::pHandle outHandle(&out);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("du781n_before");

    gDict keeper;
    keeper.SetParent(&p);
    keeper.SetParams("d781n");
    keeper.GetInlet(0)->SetList("set freq 440", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_UNPACK, "d781n freq");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &outHandle, 0);

    h->SetBang(0);
    REQUIRE(out.gotInt);
    CHECK(out.intValue == 440);

    out.reset();
    p.SetName("du781n_after");
    h->SetBang(0);
    CHECK_FALSE(out.gotInt);
  }

  TEST_CASE(
      "dict.unpack: wired from the dict's reference outlet, banging the dict unpacks (#781)") {
    // The flow a patch actually wires: the .dict's reference outlet into
    // the unpack, a bang on the .dict, and the dictionary hands its values
    // out. Max's own gesture.
    Rig rig("du781o", "d781o", "freq gain pan");
    Wire(rig.dict, 1, rig.unpack);
    rig.Store("set gain 0.5");

    rig.dict.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.unpack.Unpacked() == 1);
    CHECK(rig.outB.gotFloat);
    CHECK(rig.outB.floatValue == doctest::Approx(0.5f));
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("dict.unpack: an unpack asked for over in-patcher delivery lands on T_DSP (#781)") {
    // A .r feeding the unpack dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread unpacks a dictionary" is the
    // ordinary case, and the whole path is one bounded snapshot and a row
    // of sends.
    MultiSink outA;
    MultiSink outB;
    YSE::pHandle aHandle(&outA);
    YSE::pHandle bHandle(&outB);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("du781p");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go781p");
    YSE::pHandle* dict = p.CreateObject(YSE::OBJ::G_DICT, "d781p");
    YSE::pHandle* unpack = p.CreateObject(YSE::OBJ::G_DICT_UNPACK, "d781p freq gain");
    REQUIRE(recv != nullptr);
    REQUIRE(dict != nullptr);
    REQUIRE(unpack != nullptr);
    p.Connect(recv, 0, unpack, 0);
    p.Connect(unpack, 0, &aHandle, 0);
    p.Connect(unpack, 1, &bHandle, 0);

    dict->SetListData(0, "set freq 440");
    dict->SetListData(0, "set gain 0.5");

    p.PassData(std::string("dictionary d781p"), "go781p", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);

    REQUIRE(outA.gotInt);
    CHECK(outA.intValue == 440);
    REQUIRE(outB.gotFloat);
    CHECK(outB.floatValue == doctest::Approx(0.5f));
  }

  TEST_CASE("dict.unpack: no message path allocates (#781)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: an unpack with hits and misses (bang and
    // reference alike), the wrong-name refusal, and the unknown message.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "dictionary probeD781";
    const std::string wrongName = "dictionary somewhere_else_long";
    const std::string unknown = "frobnicate something quite long indeed";

    Rig rig("du781q", "probeD781", "freq gain name");
    rig.Store("set freq 440");
    rig.Store("set name a value past every small-string buffer");

    // Warm every path — including the sinks' list assignment — so
    // first-call machinery is not what the probe catches.
    rig.Bang();
    rig.unpack.GetInlet(0)->SetList(reference, YSE::T_GUI);
    rig.unpack.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    rig.unpack.GetInlet(0)->SetList(unknown, YSE::T_GUI);
    const std::uint64_t unpackedBefore = rig.unpack.Unpacked();
    const std::uint64_t droppedBefore = rig.unpack.Dropped();

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.Bang(YSE::T_DSP);
      rig.unpack.GetInlet(0)->SetList(reference, YSE::T_DSP);
      rig.unpack.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      rig.unpack.GetInlet(0)->SetList(unknown, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. Two completed unpacks, and both refusals
    // counted.
    CHECK(rig.unpack.Unpacked() == unpackedBefore + 2);
    CHECK(rig.unpack.Dropped() == droppedBefore + 2);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("dict.unpack: params survive a DumpJSON / ParseJSON round trip (#781)") {
    // The creation arguments have to come back: a reloaded patch whose
    // .dict.unpack lost its name or its paths would read a different
    // dictionary — or nothing — out of different outlets.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_DICT_UNPACK, "cfg781 freq voice::1::freq");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".dict.unpack"));
    CHECK(copy->GetParams() == std::string("cfg781 freq voice::1::freq"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 2);
  }

  TEST_CASE("dict.unpack: carries complete documentation metadata (#781)") {
    gDictUnpack g;
    g.SetParams("d781r freq gain");
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 2);
    CHECK(docs[0].name == "name");
    CHECK(docs[1].name == "keys");
    // The rebuilt outlets stay documented after a re-parse — the label is
    // the key path itself.
    for (int i = 0; i < g.NumOutputs(); i++) {
      CHECK_FALSE(g.GetOutlet(i)->GetDocLabel().empty());
      CHECK_FALSE(g.GetOutlet(i)->GetDocDescription().empty());
    }
  }
}
