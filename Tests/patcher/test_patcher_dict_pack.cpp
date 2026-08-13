// Tests for .dict.pack (issue #775) — Max's dict.pack on the name-addressed
// value model .dict settled (#550).
//
// What has to be proven, and what every case below is one of:
//
//   - **the arguments are the name, then the shape.** A dictionary never
//     travels down a cord, so the packed dictionary is bound from the first
//     creation argument, resolved once on the control thread, and every
//     argument after it is a key path declaring one inlet — gPack's
//     argument-driven inlet count on gDict's binding rules.
//   - **only inlet 0 releases.** A cold inlet stores its key's value and
//     stays quiet; the hot inlet (a value, a bang, or the bound
//     dictionary's reference) replaces the bound dictionary whole — one
//     entry per key path, current values — and sends the reference.
//   - **a list is one value, not a spread.** A dictionary value is list
//     text, so a multi-token message at an inlet is the array its key
//     holds.
//   - **refusal, never truncation.** An over-long value is refused whole
//     and counted, the slot keeping what it had; a reference naming an
//     unbound dictionary is refused, never resolved and never stored.
//   - **the pack crosses the control/audio boundary and nothing
//     allocates.** In-patcher delivery dispatches on T_DSP, so "the audio
//     thread packs" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names a dictionary uses names of its own — one case's
// contents must not be visible to the next.

#include <doctest/doctest.h>
#include <string>

#include "patcher/genericObjects/gDict.h"
#include "patcher/genericObjects/gDictPack.h"
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
using TestHelpers::Wire;
using YSE::PATCHER::gDict;
using YSE::PATCHER::gDictPack;

namespace {

  // A .dict and a .dict.pack over one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // keeper .dict is how a test reads what the pack wrote, through the store
  // itself rather than through the object under test. The sink is declared
  // before the objects so they are torn down first, while the inlet it is
  // wired to still exists (see sinks.hpp on why that matters).
  struct Rig {
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    MultiSink out;
    gDict keeper;
    gDictPack pack;

    Rig(const std::string& patcherName, const std::string& dictName, const std::string& keyList) {
      p.SetName(patcherName);
      keeper.SetParent(&p);
      keeper.SetParams(dictName);
      pack.SetParent(&p);
      pack.SetParams(dictName + " " + keyList);
      Wire(pack, 0, out);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("dict.pack: one inlet per key path, one outlet (#775)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_PACK, "ctl775a freq gain pan");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".dict.pack");
    CHECK(h->GetInputs() == 3);
    CHECK(h->GetOutputs() == 1);

    // With no key paths at all the object is a bare trigger: one inlet that
    // packs the empty dictionary, one outlet for the reference.
    YSE::pHandle* bare = p.CreateObject(YSE::OBJ::G_DICT_PACK);
    REQUIRE(bare != nullptr);
    CHECK(bare->GetInputs() == 1);
    CHECK(bare->GetOutputs() == 1);
  }

  TEST_CASE("dict.pack: appears in the registry's name list (#775)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_DICT_PACK)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("dict.pack: bang on the hot inlet only, values everywhere (#775)") {
    // gPack's rule: bang goes on the inlet that releases, which is where Max
    // documents it, and nowhere else — GetAcceptedTypes() reports the real
    // contract.
    gDictPack g;
    g.SetParams("ctl775b freq gain");
    const unsigned int hot = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((hot & YSE::PATCHER::IT_BANG) != 0);
    CHECK((hot & YSE::PATCHER::IT_INT) != 0);
    CHECK((hot & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((hot & YSE::PATCHER::IT_LIST) != 0);
    const unsigned int cold = g.GetInlet(1)->GetAcceptedTypes();
    CHECK((cold & YSE::PATCHER::IT_BANG) == 0);
    CHECK((cold & YSE::PATCHER::IT_INT) != 0);
    CHECK((cold & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((cold & YSE::PATCHER::IT_LIST) != 0);

    // The bare trigger has nothing to store, so it takes no number at all:
    // bang and list (the reference) only.
    gDictPack bare;
    const unsigned int trigger = bare.GetInlet(0)->GetAcceptedTypes();
    CHECK((trigger & YSE::PATCHER::IT_BANG) != 0);
    CHECK((trigger & YSE::PATCHER::IT_INT) == 0);
    CHECK((trigger & YSE::PATCHER::IT_FLOAT) == 0);
    CHECK((trigger & YSE::PATCHER::IT_LIST) != 0);
  }

  // ─── the pack ───────────────────────────────────────────────────────────────

  TEST_CASE("dict.pack: cold inlets store quietly, the hot inlet releases (#775)") {
    Rig rig("dp775a", "ctl775c", "freq gain pan");
    REQUIRE(rig.pack.KeyCount() == 3);

    // Load the right-hand values first: nothing leaves, nothing is packed.
    rig.pack.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    rig.pack.GetInlet(2)->SetList(std::string("hard left"), YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.keeper.Count() == 0);

    // The leftmost inlet carries the finished dictionary out: one entry per
    // key path, in argument order, and the reference on the outlet.
    rig.pack.GetInlet(0)->SetInt(440, YSE::T_GUI);
    REQUIRE(rig.out.gotList);
    CHECK(rig.out.listValue == "dictionary ctl775c");
    CHECK(rig.keeper.Count() == 3);
    CHECK(rig.keeper.Lookup("freq") == "440");
    CHECK(rig.keeper.Lookup("gain") == "0.5");
    CHECK(rig.keeper.Lookup("pan") == "hard left");
    CHECK(rig.pack.Dropped() == 0);
  }

  TEST_CASE("dict.pack: a bang packs as it stands, and 'set' stores quietly (#775)") {
    Rig rig("dp775b", "ctl775d", "freq gain");

    // "set <value>" on the hot inlet performs exactly the store the plain
    // message would have performed and suppresses only the release — how a
    // patch loads the hot slot and chooses when to send.
    rig.pack.GetInlet(0)->SetList(std::string("set 440"), YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.pack.ValueAt(0) == "440");

    // A bang stores nothing and packs: the loaded freq, and gain's slot
    // never written — its path with an empty value, the declared shape
    // rather than a gap.
    rig.pack.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(rig.out.gotList);
    CHECK(rig.out.listValue == "dictionary ctl775d");
    CHECK(rig.keeper.Count() == 2);
    CHECK(rig.keeper.Lookup("freq") == "440");
    CHECK(rig.keeper.KeyAt(1) == "gain");
    CHECK(rig.keeper.Lookup("gain").empty());
  }

  TEST_CASE("dict.pack: a pack replaces the bound dictionary whole (#775)") {
    // A stale entry from another writer — or an earlier shape — cannot
    // survive into the packed unit: the dictionary is the shape the
    // arguments declared, nothing more. gDictJoin's rule for its target.
    Rig rig("dp775c", "ctl775e", "freq");
    rig.keeper.GetInlet(0)->SetList(std::string("set stale 1"), YSE::T_GUI);
    rig.keeper.GetInlet(0)->SetList(std::string("set voice::1::amp 0.3"), YSE::T_GUI);
    REQUIRE(rig.keeper.Count() == 2);

    rig.pack.GetInlet(0)->SetInt(220, YSE::T_GUI);
    CHECK(rig.keeper.Count() == 1);
    CHECK(rig.keeper.Lookup("freq") == "220");
    CHECK(rig.keeper.Lookup("stale").empty());
  }

  TEST_CASE("dict.pack: a list is one value, and paths nest in the key (#775)") {
    // A dictionary value is list text, so a multi-token message is the
    // array its key holds — where .pack would spread it rightwards — and a
    // "::" path is one entry as deep as it spells.
    Rig rig("dp775d", "ctl775f", "voice::1::env gain");
    rig.pack.GetInlet(1)->SetList(std::string("0.2 0.5 0.9"), YSE::T_GUI);
    rig.pack.GetInlet(0)->SetList(std::string("10 200 4000 80"), YSE::T_GUI);

    REQUIRE(rig.out.gotList);
    CHECK(rig.keeper.Count() == 2);
    CHECK(rig.keeper.Lookup("voice::1::env") == "10 200 4000 80");
    CHECK(rig.keeper.Lookup("gain") == "0.2 0.5 0.9");
  }

  TEST_CASE("dict.pack: a duplicate key path is one entry, and the later inlet wins (#775)") {
    Rig rig("dp775e", "ctl775g", "dup dup");
    REQUIRE(rig.pack.KeyCount() == 2);
    rig.pack.GetInlet(1)->SetList(std::string("second"), YSE::T_GUI);
    rig.pack.GetInlet(0)->SetList(std::string("first"), YSE::T_GUI);

    CHECK(rig.keeper.Count() == 1);
    CHECK(rig.keeper.Lookup("dup") == "second");
  }

  TEST_CASE("dict.pack: an unnamed pack lands in a private store and says nothing (#775)") {
    // No arguments means no name and no keys — .dict's rule that an unnamed
    // object does not pool on "<patcherName>.". The bare trigger still
    // packs (the empty dictionary, into the private store) but has no name
    // to pass on, so the outlet stays silent.
    MultiSink sink;
    gDictPack g;
    Wire(g, 0, sink);
    CHECK(g.DictName().empty());
    CHECK(g.Address().empty());
    CHECK(g.Reference().empty());
    g.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(sink.gotList);
    CHECK(g.Count() == 0);
    CHECK(g.Dropped() == 0);

    // And a value handed to the bare trigger has no slot to land in:
    // refused and counted rather than silently swallowed.
    g.GetInlet(0)->SetList(std::string("some value"), YSE::T_GUI);
    CHECK(g.Dropped() == 1);
  }

  // ─── refusals ───────────────────────────────────────────────────────────────

  TEST_CASE("dict.pack: an over-long value is refused whole, the slot keeps what it had (#775)") {
    Rig rig("dp775f", "ctl775h", "note");
    rig.pack.GetInlet(0)->SetList(std::string("kept"), YSE::T_GUI);
    REQUIRE(rig.keeper.Lookup("note") == "kept");

    const std::uint64_t before = rig.pack.Dropped();
    const std::string tooLong(YSE::PATCHER::dictStore::VALUE_CAPACITY + 1, 'x');
    rig.out.reset();
    rig.pack.GetInlet(0)->SetList(tooLong, YSE::T_GUI);
    CHECK(rig.pack.Dropped() == before + 1);
    // The refusal is the store's, not the trigger's: the hot inlet still
    // packed and released, with the value it already held.
    CHECK(rig.out.gotList);
    CHECK(rig.keeper.Lookup("note") == "kept");
  }

  TEST_CASE("dict.pack: the bound reference triggers on the hot inlet, is acknowledged on a "
            "cold one, and anything else is refused (#775)") {
    Rig rig("dp775g", "ctl775i", "freq gain");
    rig.pack.GetInlet(1)->SetFloat(0.75f, YSE::T_GUI);
    const std::uint64_t before = rig.pack.Dropped();

    // The bound dictionary's own reference — the message its .dict emits on
    // a bang — packs and re-emits, storing nothing.
    rig.out.reset();
    rig.pack.GetInlet(0)->SetList(std::string("dictionary ctl775i"), YSE::T_GUI);
    REQUIRE(rig.out.gotList);
    CHECK(rig.out.listValue == "dictionary ctl775i");
    CHECK(rig.keeper.Lookup("gain") == "0.75");
    CHECK(rig.pack.ValueAt(0).empty());
    CHECK(rig.pack.Dropped() == before);

    // On a cold inlet it is acknowledged and nothing more — a patch may
    // wire the reference across without it being stored as a value or
    // counted as an error.
    rig.out.reset();
    rig.pack.GetInlet(1)->SetList(std::string("dictionary ctl775i"), YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.pack.ValueAt(1) == "0.75");
    CHECK(rig.pack.Dropped() == before);

    // A reference naming anything else is refused and counted on every
    // inlet — never resolved (a registry lookup is a mutex, and this may be
    // the audio thread) and never stored (an identity is not a value).
    rig.pack.GetInlet(0)->SetList(std::string("dictionary elsewhere"), YSE::T_GUI);
    rig.pack.GetInlet(1)->SetList(std::string("dictionary elsewhere"), YSE::T_GUI);
    CHECK(rig.pack.Dropped() == before + 2);
    CHECK(rig.pack.ValueAt(0).empty());
    CHECK(rig.pack.ValueAt(1) == "0.75");
  }

  TEST_CASE("dict.pack: a key path past its capacity declares no inlet (#775)") {
    // Refused at parse time — the control thread, where the skip can be
    // logged — rather than truncated into a key nobody spelled.
    gDictPack g;
    const std::string longKey(YSE::PATCHER::dictStore::KEY_CAPACITY + 1, 'k');
    g.SetParams("ctl775j freq " + longKey + " gain");
    CHECK(g.KeyCount() == 2);
    CHECK(g.KeyAt(0) == "freq");
    CHECK(g.KeyAt(1) == "gain");
    CHECK(g.NumInputs() == 2);
  }

  // ─── the family, end to end ─────────────────────────────────────────────────

  TEST_CASE("dict.pack: feeds .dict.iter through the public patcher API (#775)") {
    // The flow a patch actually wires: the pack's reference outlet into a
    // .dict.iter bound to the same name, values loaded cold-first, and the
    // packed unit streaming out the far end. The producer feeding the rest
    // of the family, which is what the object exists for.
    MultiSink pairs;
    YSE::pHandle pairsHandle(&pairs);
    TestHelpers::BangSink done;
    YSE::pHandle doneHandle(&done);
    YSE::patcher p;
    p.create(2);
    p.name("dp775h");
    YSE::pHandle* pk = p.CreateObject(YSE::OBJ::G_DICT_PACK, "live775 freq gain");
    YSE::pHandle* it = p.CreateObject(YSE::OBJ::G_DICT_ITER, "live775");
    REQUIRE(pk != nullptr);
    REQUIRE(it != nullptr);
    p.Connect(pk, 0, it, 0);
    p.Connect(it, 0, &pairsHandle, 0);
    p.Connect(it, 1, &doneHandle, 0);

    pk->SetFloatData(1, 0.5f);
    pk->SetIntData(0, 440);

    // The reference triggered the walk: both entries streamed, in argument
    // order, then the done bang.
    CHECK(done.gotBang);
    REQUIRE(pairs.gotList);
    CHECK(pairs.listValue == "gain 0.5");
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("dict.pack: the address form is the patcher's, and RefreshBinding follows it (#775)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dp775i_before");

    gDictPack g;
    g.SetParams("ctl775k freq");
    g.SetParent(&p);
    CHECK(g.Address() == "dp775i_before.ctl775k");
    CHECK(g.Reference() == "dictionary ctl775k");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "dp775i_before.ctl775k");

    p.SetName("dp775i_after");
    g.RefreshBinding();
    CHECK(g.Address() == "dp775i_after.ctl775k");
  }

  TEST_CASE("dict.pack: patcherImplementation::SetName re-anchors it (#775)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by
    // the patcher, without anybody calling RefreshBinding by hand. The
    // keeper holds the old-address store (it is not in the patcher's object
    // map, so the rename does not touch it): before the rename a pack lands
    // in it; after the rename a pack lands in a fresh dictionary under the
    // new prefix, and the keeper's contents stay exactly as they were.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dp775j_before");

    gDict keeper;
    keeper.SetParent(&p);
    keeper.SetParams("ctl775l");

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_PACK, "ctl775l lead");
    REQUIRE(h != nullptr);

    h->SetListData(0, "piano");
    CHECK(keeper.Count() == 1);
    CHECK(keeper.Lookup("lead") == "piano");

    p.SetName("dp775j_after");
    h->SetListData(0, "organ");
    // The old store was neither replaced nor extended: the pack now writes
    // the dictionary under the new prefix.
    CHECK(keeper.Count() == 1);
    CHECK(keeper.Lookup("lead") == "piano");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("dict.pack: a pack asked for over in-patcher delivery lands on T_DSP (#775)") {
    // A .r feeding the hot inlet dispatches on T_DSP when the block drains
    // it (issue #225) — "the audio thread packs" is the ordinary case, and
    // the whole path is bounded copies into pre-reserved rows and a send of
    // a string the object already owns.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dp775k");

    gDict keeper;
    keeper.SetParent(&p);
    keeper.SetParams("ctl775m");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go775");
    YSE::pHandle* pk = p.CreateObject(YSE::OBJ::G_DICT_PACK, "ctl775m freq gain");
    REQUIRE(recv != nullptr);
    REQUIRE(pk != nullptr);
    p.Connect(recv, 0, pk, 0);
    p.Connect(pk, 0, &sinkHandle, 0);

    pk->SetFloatData(1, 0.5f);

    p.PassData(std::string("dictionary ctl775m"), "go775", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "dictionary ctl775m");
    CHECK(keeper.Count() == 2);
    CHECK(keeper.Lookup("gain") == "0.5");
  }

  TEST_CASE("dict.pack: no message path allocates (#775)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: the bang, the hot and cold stores (number and
    // list alike), the quiet 'set', the reference trigger, the cold
    // acknowledgment, the unknown-reference refusal and the over-long
    // refusal.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string longValue = "a value comfortably past every small-string buffer there is";
    const std::string setMessage = "set another value past the small-string optimisation";
    const std::string reference = "dictionary probeD775";
    const std::string unknown = "dictionary probeSomewhereElse775";
    const std::string tooLong(YSE::PATCHER::dictStore::VALUE_CAPACITY + 1, 'x');

    Rig rig("dp775l", "probeD775", "freq gain");

    // Warm every path — including the sink's list assignment — so
    // first-call machinery is not what the probe catches.
    rig.pack.GetInlet(1)->SetList(longValue, YSE::T_GUI);
    rig.pack.GetInlet(0)->SetList(setMessage, YSE::T_GUI);
    rig.pack.GetInlet(0)->SetInt(440, YSE::T_GUI);
    rig.pack.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    rig.pack.GetInlet(0)->SetBang(YSE::T_GUI);
    rig.pack.GetInlet(0)->SetList(reference, YSE::T_GUI);
    rig.pack.GetInlet(1)->SetList(reference, YSE::T_GUI);
    rig.pack.GetInlet(0)->SetList(unknown, YSE::T_GUI);
    rig.pack.GetInlet(0)->SetList(tooLong, YSE::T_GUI);
    const std::uint64_t before = rig.pack.Dropped();

    rig.out.reset();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.pack.GetInlet(1)->SetList(longValue, YSE::T_DSP);
      rig.pack.GetInlet(0)->SetList(setMessage, YSE::T_DSP);
      rig.pack.GetInlet(0)->SetInt(880, YSE::T_DSP);
      rig.pack.GetInlet(1)->SetFloat(0.25f, YSE::T_DSP);
      rig.pack.GetInlet(0)->SetBang(YSE::T_DSP);
      rig.pack.GetInlet(0)->SetList(reference, YSE::T_DSP);
      rig.pack.GetInlet(1)->SetList(reference, YSE::T_DSP);
      rig.pack.GetInlet(0)->SetList(unknown, YSE::T_DSP);
      rig.pack.GetInlet(0)->SetList(tooLong, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. Three packs released (the int, the bang and
    // the reference), the stores landed, and the two refusals counted.
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "dictionary probeD775");
    CHECK(rig.keeper.Count() == 2);
    CHECK(rig.keeper.Lookup("freq") == "880");
    CHECK(rig.keeper.Lookup("gain") == "0.25");
    CHECK(rig.pack.Dropped() == before + 2);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("dict.pack: params survive a DumpJSON / ParseJSON round trip (#775)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_DICT_PACK, "ctl775n freq gain pan");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".dict.pack"));
    CHECK(copy->GetParams() == std::string("ctl775n freq gain pan"));
    CHECK(copy->GetInputs() == 3);
    CHECK(copy->GetOutputs() == 1);
  }

  TEST_CASE("dict.pack: carries complete documentation metadata (#775)") {
    gDictPack g;
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 2);
    CHECK(docs[0].name == "name");
    CHECK(docs[1].name == "keys");

    // A re-parse rebuilds the ports; none may come back undocumented, and
    // each key inlet is labelled with the path it writes.
    g.SetParams("ctl775o freq voice::1::gain");
    REQUIRE(g.NumInputs() == 2);
    CHECK(g.GetInlet(0)->GetDocLabel() == "freq");
    CHECK(g.GetInlet(1)->GetDocLabel() == "voice::1::gain");
    CHECK_FALSE(g.GetInlet(0)->GetDocDescription().empty());
    CHECK_FALSE(g.GetInlet(1)->GetDocDescription().empty());
    CHECK_FALSE(g.GetOutlet(0)->GetDocLabel().empty());
  }
}
