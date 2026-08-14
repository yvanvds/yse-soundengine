// Tests for .array.deserialize (issue #797) — Max's array.deserialize ("build
// an array from serialised text") on the name-addressed value model .array
// settled (#548). The reader half of the JSON form .array already saves with
// a patch (ArrayToJson / DumpState).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.deserialize <name>" resolves the name
//     once, on the control thread, and a parsed document replaces exactly
//     that array.
//   - **the parse is off the message path.** nlohmann allocates without
//     bound, so the inlet is a wait-free hand-off to the background pool
//     (arrayParser) and the result is installed by the block poll — which is
//     why every case pumps Calculate() after ArrayParser().WaitIdle() instead
//     of expecting the document to land synchronously.
//   - **the round trip is the point.** The document .array saves with a
//     patch (DumpState's "contents") comes back element for element through
//     this object — the same ArrayFromJson, whichever road a document
//     arrives by.
//   - **refusal, never truncation; failure, never damage.** A document past
//     what a list payload carries is refused whole; one that does not parse
//     to a JSON array is counted and the bound array keeps its contents.
//   - **nothing on the submit or install path allocates.** In-patcher
//     delivery dispatches on T_DSP, so "the audio thread hands over a
//     document" is the ordinary case.
//
// No audio device required. The registry is process-wide, so every case that
// names an array uses names of its own.

#include <doctest/doctest.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

#include "internal/global.h"
#include "internal/threadPool.h"
#include "patcher/genericObjects/arrayParser.h"
#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayDeserialize.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"
#include "support/timer_pacing.hpp"
#include "utils/json.hpp"

using TestHelpers::Wire;
using YSE::PATCHER::ArrayParser;
using YSE::PATCHER::gArray;
using YSE::PATCHER::gArrayDeserialize;

namespace {

  // Records every reference it receives — a count as well as the last text,
  // so a failed parse (which must send nothing) is tellable from one that
  // announced again.
  struct RefSink : YSE::PATCHER::pObject {
    std::string received;
    int count = 0;

    RefSink() : pObject(false) {
      received.reserve(512);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        received = v;
        count++;
      });
    }
    const char* Type() const override {
      return "ref_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // An .array keeper and an .array.deserialize on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // keeper is what the assertions read the array through. The sink is
  // declared before the objects so it is torn down last, while the outlet
  // wired to it still exists (see sinks.hpp on why that matters).
  struct Rig {
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    RefSink out;
    gArray keeper;
    gArrayDeserialize des;

    Rig(const std::string& patcherName, const std::string& name) {
      p.SetName(patcherName);
      keeper.SetParent(&p);
      keeper.SetParams(name);
      des.SetParent(&p);
      des.SetParams(name);
      Wire(des, 0, out);
    }

    void Submit(const std::string& document, YSE::THREAD thread = YSE::T_GUI) {
      des.GetInlet(0)->SetList(document, thread);
    }

    // The deferred half, made deterministic: join the background parse, then
    // pump the poll the patcher would run at the top of its next block.
    void Pump(YSE::THREAD thread = YSE::T_GUI) {
      ArrayParser().WaitIdle();
      des.Calculate(thread);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.deserialize: registered, one inlet, one outlet (#797)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_DESERIALIZE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".array.deserialize");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
  }

  TEST_CASE("array.deserialize: appears in the registry's name list (#797)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_DESERIALIZE)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array.deserialize: the inlet takes a list and nothing else (#797)") {
    // The document is the trigger: a bang carries no document, and a bare
    // number is a JSON document but not a JSON array, so neither has a
    // handler.
    gArrayDeserialize g;
    const unsigned int accepted = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
    CHECK((accepted & YSE::PATCHER::IT_BANG) == 0);
    CHECK((accepted & YSE::PATCHER::IT_INT) == 0);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) == 0);
  }

  // ─── creation arguments ─────────────────────────────────────────────────────

  TEST_CASE("array.deserialize: the name is the creation argument, and '' resets (#797)") {
    {
      gArrayDeserialize g;
      CHECK(g.ArrayName().empty());
      CHECK(g.Address().empty());
      CHECK(g.Reference().empty());
    }
    {
      gArrayDeserialize g;
      g.SetParams("seq797");
      CHECK(g.ArrayName() == "seq797");
      CHECK(g.Reference() == "array seq797");
    }
    {
      // SetParams("") has to leave the no-argument object behind rather than
      // one still holding the previous binding.
      gArrayDeserialize g;
      g.SetParams("seq797");
      REQUIRE(g.ArrayName() == "seq797");
      g.SetParams("");
      CHECK(g.ArrayName().empty());
      CHECK(g.Address().empty());
      CHECK(g.Reference().empty());
    }
  }

  // ─── the document, in ───────────────────────────────────────────────────────

  TEST_CASE("array.deserialize: a document becomes the array, then the reference (#797)") {
    // The core: typed elements respelled as the atoms they are, the reference
    // out the outlet once — and only once — the result is installed. Before
    // the pump, nothing has happened: the parse is on the pool, which is the
    // design, not a latency bug.
    Rig rig("ad797a", "a797a");
    rig.Submit("[60,62.5,\"kick\"]");
    CHECK(rig.keeper.Count() == 0);
    CHECK(rig.out.count == 0);

    rig.Pump();
    REQUIRE(rig.out.count == 1);
    CHECK(rig.out.received == "array a797a");
    CHECK(rig.des.Parsed() == 1);
    CHECK(rig.des.Failed() == 0);
    CHECK(rig.des.Dropped() == 0);

    REQUIRE(rig.keeper.Count() == 3);
    CHECK(rig.keeper.ElementAt(0) == "60");
    CHECK(rig.keeper.ElementAt(1) == "62.5");
    CHECK(rig.keeper.ElementAt(2) == "kick");
  }

  TEST_CASE("array.deserialize: a parsed document replaces the array whole (#797)") {
    // ArrayFromJson's contract: a stale element from an earlier run must not
    // survive inside the parsed sequence.
    Rig rig("ad797b", "a797b");
    rig.keeper.GetInlet(0)->SetList("append stale1 stale2 stale3", YSE::T_GUI);
    REQUIRE(rig.keeper.Count() == 3);

    rig.Submit("[7,8]");
    rig.Pump();
    REQUIRE(rig.keeper.Count() == 2);
    CHECK(rig.keeper.ElementAt(0) == "7");
    CHECK(rig.keeper.ElementAt(1) == "8");
  }

  TEST_CASE("array.deserialize: external JSON is respelled as the atoms it can be (#797)") {
    // A document need not come from a saved patch: a host hands over
    // booleans, nulls and sub-documents this type never spells. They load by
    // ArrayFromJson's reading — true/false as 1/0; null, an empty string and
    // a nested array or object skipped, because an element is one atom and
    // none of those spells one; the rest of the document still loads.
    Rig rig("ad797c", "a797c");
    rig.Submit("[true,false,null,[1,2],{\"a\":1},\"x\",\"\",2.5]");
    rig.Pump();
    REQUIRE(rig.des.Parsed() == 1);
    REQUIRE(rig.keeper.Count() == 4);
    CHECK(rig.keeper.ElementAt(0) == "1");
    CHECK(rig.keeper.ElementAt(1) == "0");
    CHECK(rig.keeper.ElementAt(2) == "x");
    CHECK(rig.keeper.ElementAt(3) == "2.5");
  }

  TEST_CASE("array.deserialize: an empty document is an emptied array, announced (#797)") {
    // "[]" is what an empty array spells, so it must deserialise — to a
    // cleared array, not to a failure.
    Rig rig("ad797d", "a797d");
    rig.keeper.GetInlet(0)->SetList("append lead pad", YSE::T_GUI);
    REQUIRE(rig.keeper.Count() == 2);

    rig.Submit("[]");
    rig.Pump();
    CHECK(rig.des.Parsed() == 1);
    CHECK(rig.keeper.Count() == 0);
    CHECK(rig.out.count == 1);
  }

  TEST_CASE("array.deserialize: an unnamed object fills its private array, silently (#797)") {
    // The family's rule for the empty name — private, never pooled — and the
    // reference rule that follows: nothing to name, nothing to announce. The
    // document still parses and installs, which Parsed() is here to see.
    RefSink out;
    gArrayDeserialize g;
    Wire(g, 0, out);
    g.GetInlet(0)->SetList("[1,2]", YSE::T_GUI);
    ArrayParser().WaitIdle();
    g.Calculate(YSE::T_GUI);
    CHECK(g.Parsed() == 1);
    CHECK(g.Dropped() == 0);
    CHECK(out.count == 0);
  }

  // ─── failure, never damage ──────────────────────────────────────────────────

  TEST_CASE("array.deserialize: a malformed document fails, counted, changing nothing (#797)") {
    Rig rig("ad797e", "a797e");
    rig.keeper.GetInlet(0)->SetList("append keep", YSE::T_GUI);
    REQUIRE(rig.keeper.Count() == 1);

    rig.Submit("[60,");
    rig.Pump();
    CHECK(rig.des.Failed() == 1);
    CHECK(rig.des.Parsed() == 0);
    CHECK(rig.out.count == 0);
    // The bound array is untouched: a bad document never costs an array its
    // contents.
    CHECK(rig.keeper.Count() == 1);
    CHECK(rig.keeper.ElementAt(0) == "keep");
  }

  TEST_CASE("array.deserialize: a document that is not a JSON array fails (#797)") {
    // An array is a JSON array. A bare number, an object, a bare string, and
    // an array *reference* wired here by mistake are all failures — not empty
    // arrays, and never a resolve of the name on a message path.
    Rig rig("ad797f", "a797f");
    const char* const bad[] = {"5", "{\"a\":1}", "\"text\"", "array a797f"};
    std::uint64_t expected = 0;
    for (const char* document : bad) {
      rig.Submit(document);
      rig.Pump();
      expected++;
      CHECK(rig.des.Failed() == expected);
    }
    CHECK(rig.des.Parsed() == 0);
    CHECK(rig.out.count == 0);
  }

  // ─── refusal, never truncation ──────────────────────────────────────────────

  TEST_CASE("array.deserialize: a document past the payload bound is refused whole (#797)") {
    // The bound is what the patcher's value queue carries (kValueListCap - 1
    // = DOCUMENT_CAPACITY): a longer document could arrive over a direct cord
    // but never over a .s/.r, and the prefix of a JSON document is a
    // different document — so nothing is parsed at all, and the loss is
    // counted.
    Rig rig("ad797g", "a797g");
    std::string document = "[\"";
    document.append(gArrayDeserialize::DOCUMENT_CAPACITY, 'a');
    document += "\"]";
    REQUIRE(document.size() > gArrayDeserialize::DOCUMENT_CAPACITY);

    rig.Submit(document);
    CHECK(rig.des.Dropped() == 1);
    rig.Pump();
    CHECK(rig.des.Parsed() == 0);
    CHECK(rig.des.Failed() == 0);
    CHECK(rig.out.count == 0);
    CHECK(rig.keeper.Count() == 0);
  }

  TEST_CASE("array.deserialize: a document arriving mid-parse is refused, not queued (#797)") {
    // One slot, one document: remembering a second ask without its bytes
    // would re-parse the old text as the new one, so the loser is refused
    // and counted — the family's busy rule. Park the pool's one worker in a
    // blocker so the first document provably cannot have finished.
    struct Blocker : YSE::INTERNAL::threadPoolJob {
      std::atomic<bool> running{false};
      std::atomic<bool> release{false};
      void run() override {
        running.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire))
          std::this_thread::yield();
      }
    };

    Blocker blocker;
    YSE::INTERNAL::Global().addSlowJob(&blocker);
    TestHelpers::pacedUntil(5000, [&] { return blocker.running.load(std::memory_order_acquire); });
    REQUIRE(blocker.running.load(std::memory_order_acquire));

    Rig rig("ad797h", "a797h");
    rig.Submit("[1]");
    rig.Submit("[2]");
    CHECK(rig.des.Dropped() == 1);

    blocker.release.store(true, std::memory_order_release);
    blocker.join();
    rig.Pump();

    // The first document is the one that landed.
    CHECK(rig.des.Parsed() == 1);
    REQUIRE(rig.keeper.Count() == 1);
    CHECK(rig.keeper.ElementAt(0) == "1");
  }

  // ─── the round trip — the saved form read back ──────────────────────────────

  TEST_CASE("array.deserialize: the document a saved .array spells comes back whole (#797)") {
    // The reader half working against the writer half: DumpState's
    // "contents" — the exact JSON form a saved patch carries — fed to this
    // object reproduces the array element for element, spellings included
    // ("120." canonicalises to "120.0" on the way through JSON, the same
    // number, stable from the second round trip on — gArray's documented
    // respelling, this object being the same reader).
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ad797i");
    RefSink refOut;
    gArray src;
    gArray dst;
    gArrayDeserialize des;

    src.SetParent(&p);
    src.SetParams("src797i");
    dst.SetParent(&p);
    dst.SetParams("dst797i");
    des.SetParent(&p);
    des.SetParams("dst797i");
    Wire(des, 0, refOut);

    src.GetInlet(0)->SetList("append 60 62.5 kick", YSE::T_GUI);
    src.GetInlet(0)->SetList("append 120.", YSE::T_GUI);
    REQUIRE(src.Count() == 4);

    nlohmann::json state;
    src.DumpState(state);
    REQUIRE(state.find("contents") != state.end());
    const std::string document = state["contents"].dump();
    REQUIRE(document.size() <= gArrayDeserialize::DOCUMENT_CAPACITY);

    des.GetInlet(0)->SetList(document, YSE::T_GUI);
    ArrayParser().WaitIdle();
    des.Calculate(YSE::T_GUI);
    REQUIRE(des.Parsed() == 1);
    REQUIRE(refOut.count == 1);
    CHECK(refOut.received == "array dst797i");

    REQUIRE(dst.Count() == 4);
    CHECK(dst.ElementAt(0) == "60");
    CHECK(dst.ElementAt(1) == "62.5");
    CHECK(dst.ElementAt(2) == "kick");
    CHECK(dst.ElementAt(3) == "120.0");
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.deserialize: the address form is the patcher's, and RefreshBinding follows it "
            "(#797)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ad797k_before");

    gArrayDeserialize g;
    g.SetParams("a797k");
    g.SetParent(&p);
    CHECK(g.ArrayName() == "a797k");
    CHECK(g.Address() == "ad797k_before.a797k");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "ad797k_before.a797k");

    p.SetName("ad797k_after");
    g.RefreshBinding();
    CHECK(g.Address() == "ad797k_after.a797k");
  }

  TEST_CASE("array.deserialize: patcherImplementation::SetName re-anchors it (#797)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. The keepers
    // are standalone, so each stays on the address it bound: after the
    // rename, a parsed document lands in the new-prefix array and the old
    // one keeps what it had.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ad797l_before");

    gArray keeperOld;
    keeperOld.SetParent(&p);
    keeperOld.SetParams("a797l");

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_DESERIALIZE, "a797l");
    REQUIRE(h != nullptr);

    h->SetListData(0, "[1]");
    ArrayParser().WaitIdle();
    p.Calculate(YSE::T_DSP);
    REQUIRE(keeperOld.Count() == 1);
    CHECK(keeperOld.ElementAt(0) == "1");

    p.SetName("ad797l_after");
    gArray keeperNew;
    keeperNew.SetParent(&p);
    keeperNew.SetParams("a797l");

    h->SetListData(0, "[2]");
    ArrayParser().WaitIdle();
    p.Calculate(YSE::T_DSP);
    REQUIRE(keeperNew.Count() == 1);
    CHECK(keeperNew.ElementAt(0) == "2");
    // The old-prefix array was not what the renamed object filled.
    REQUIRE(keeperOld.Count() == 1);
    CHECK(keeperOld.ElementAt(0) == "1");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.deserialize: the block poll delivers a document sent through the patcher "
            "(#797)") {
    // The full in-patcher path, exactly as a patch runs it: the host passes
    // the document to a .r, the value drain hands it to the inlet on the
    // audio thread (T_DSP block), the submit rides to the pool, and the
    // *next* block's poll — WantsBlockPoll, the reason this object is in the
    // GraphState's pollers at all — installs it and announces it. No
    // hand-pumped Calculate anywhere: the patcher's own dispatch does it.
    RefSink out;
    YSE::pHandle outHandle(&out);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ad797m");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a797m");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "doc797m");
    YSE::pHandle* des = p.CreateObject(YSE::OBJ::G_ARRAY_DESERIALIZE, "a797m");
    REQUIRE(recv != nullptr);
    REQUIRE(des != nullptr);
    p.Connect(recv, 0, des, 0);
    p.Connect(des, 0, &outHandle, 0);

    p.PassData(std::string("[60,62,67]"), "doc797m", YSE::T_GUI);
    p.Calculate(YSE::T_DSP); // drains the value queue: the submit happens here
    ArrayParser().WaitIdle(); // the parse finishes on the pool
    CHECK(out.count == 0); // nothing announced yet — no block has polled
    p.Calculate(YSE::T_DSP); // the poll installs and announces

    REQUIRE(out.count == 1);
    CHECK(out.received == "array a797m");
    REQUIRE(keeper.Count() == 3);
    CHECK(keeper.ElementAt(0) == "60");
    CHECK(keeper.ElementAt(1) == "62");
    CHECK(keeper.ElementAt(2) == "67");
  }

  TEST_CASE("array.deserialize: neither the submit nor the install allocates (#797)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // both halves the object runs on the patcher's threads: the hand-off
    // (submit, the over-long refusal, the busy refusal — T_DSP, the ordinary
    // case) and the block poll's install-and-announce, plus the idle poll a
    // patch pays every block. The parse itself allocates on the background
    // pool, which is the design — and the probe is thread-scoped (#701), so
    // what it measures here is exactly the paths under test.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    Rig rig("ad797n", "probeA797");
    const std::string document = "[60,62.5,\"kick\",\"warm\"]";
    std::string overlong = "[\"";
    overlong.append(gArrayDeserialize::DOCUMENT_CAPACITY, 'a');
    overlong += "\"]";

    // Warm every path — including the sink's assignment and the store's
    // element assigns — so first-call machinery is not what the probe
    // catches.
    rig.Submit(document);
    rig.Pump();
    REQUIRE(rig.des.Parsed() == 1);
    const std::uint64_t before = rig.des.Dropped();

    int submitCount = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.Submit(document, YSE::T_DSP); // the hand-off
      rig.Submit(document, YSE::T_DSP); // refused: the slot is busy
      rig.Submit(overlong, YSE::T_DSP); // refused: past the payload bound
      submitCount = TestHelpers::g_alloc_count.load();
    }
    CHECK(submitCount == 0);

    ArrayParser().WaitIdle();

    int installCount = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.des.Calculate(YSE::T_DSP); // the poll: install + announce
      rig.des.Calculate(YSE::T_DSP); // the idle poll: one atomic load
      installCount = TestHelpers::g_alloc_count.load();
    }
    CHECK(installCount == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing.
    CHECK(rig.des.Parsed() == 2);
    CHECK(rig.des.Dropped() == before + 2);
    CHECK(rig.out.count == 2);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.deserialize: params survive a DumpJSON / ParseJSON round trip (#797)") {
    // The creation argument has to come back: a reloaded patch whose
    // .array.deserialize lost its name would fill a different array.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_ARRAY_DESERIALIZE, "cfg797");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".array.deserialize"));
    CHECK(copy->GetParams() == std::string("cfg797"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 1);
  }

  TEST_CASE("array.deserialize: carries complete documentation metadata (#797)") {
    gArrayDeserialize g;
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "name");
  }
}
