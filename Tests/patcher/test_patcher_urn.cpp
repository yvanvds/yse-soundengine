// Tests for .urn (issue #454) — random numbers without repetition.
//
// "The output was in range" says nothing about this object: .random satisfies
// that too, and the whole point of .urn is what .random does *not* give. What
// characterises it is
//
//   * exhaustion — over one cycle every value in [0, limit) comes out exactly
//     once, in some order, and none of them comes out twice;
//   * the empty urn — once the cycle is done, outlet 0 goes silent and outlet 1
//     bangs, on every further bang, until something refills it;
//   * refilling — `clear` and a new limit both start a fresh cycle, and a limit
//     change discards the drawn set rather than carrying it into a range it no
//     longer describes;
//   * reproducibility — a seeded object replays the same orders, and two
//     objects on the same seed agree value for value.
//
// The no-repetition guarantee is also asserted under concurrent bangs, because
// the cursor claim is the only thing that makes it hold there, and a refactor
// back to load/store would leave every other test in this file passing.
//
// Nothing here needs an audio device.

#include <doctest/doctest.h>
#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gUrn.h"
#include "patcher/sinks.hpp"

using TestHelpers::BangSink;
using TestHelpers::IntSink;

namespace {

  // A .urn with both outlets wired: the values on outlet 0, the empty
  // notification on outlet 1.
  struct UrnRig {
    YSE::PATCHER::gUrn urn;
    IntSink values;
    BangSink empty;

    explicit UrnRig(const char* params = nullptr) {
      urn.ConnectOutlet(values.GetInlet(0), 0);
      values.ConnectInlet(urn.GetOutlet(0), 0);
      urn.ConnectOutlet(empty.GetInlet(0), 1);
      empty.ConnectInlet(urn.GetOutlet(1), 0);
      if (params != nullptr) urn.SetParams(params);
    }

    // Bang the hot inlet. Returns true when a value came out (and leaves it in
    // `values.received`), false when the urn reported itself empty instead.
    bool Bang() {
      values.gotInt = false;
      empty.gotBang = false;
      urn.GetInlet(0)->SetBang(YSE::T_GUI);
      return values.gotInt;
    }

    // One full cycle: bang `count` times and collect what came out. Fails the
    // test rather than silently short-changing the caller if the urn empties
    // early.
    std::vector<int> Draw(int count) {
      std::vector<int> out;
      out.reserve(static_cast<size_t>(count));
      for (int i = 0; i < count; ++i) {
        CAPTURE(i);
        REQUIRE(Bang());
        out.push_back(values.received);
      }
      return out;
    }

    void SetLimit(int n) {
      urn.GetInlet(1)->SetInt(n, YSE::T_GUI);
    }
    void Clear() {
      urn.GetInlet(0)->SetList("clear", YSE::T_GUI);
    }
    void Reseed(int s) {
      urn.GetInlet(0)->SetList("seed " + std::to_string(s), YSE::T_GUI);
    }
  };

  // Is `drawn` a permutation of [0, limit)? That single question is the
  // object's contract: right length, right values, no duplicates.
  bool IsPermutation(const std::vector<int>& drawn, int limit) {
    if (static_cast<int>(drawn.size()) != limit) return false;
    std::set<int> seen;
    for (int v : drawn) {
      if (v < 0 || v >= limit) return false;
      if (!seen.insert(v).second) return false;
    }
    return true;
  }

  // Thread-safe sinks for the concurrency case. The plain IntSink/BangSink pair
  // above would be the race rather than observe it: four threads writing one
  // `int received` is undefined behaviour, and ThreadSanitizer would rightly
  // report the *sink* instead of the object under test.
  struct TallySink : YSE::PATCHER::pObject {
    std::vector<std::atomic<int>> counts; // hits per value
    std::atomic<int> total{0}; // values received
    std::atomic<int> strays{0}; // values outside [0, size)

    explicit TallySink(int size) : pObject(false), counts(static_cast<size_t>(size)) {
      for (auto& c : counts)
        c.store(0);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        total.fetch_add(1);
        if (v >= 0 && v < static_cast<int>(counts.size()))
          counts[static_cast<size_t>(v)].fetch_add(1);
        else
          strays.fetch_add(1);
      });
    }
    const char* Type() const override {
      return "tally_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  struct BangTallySink : YSE::PATCHER::pObject {
    std::atomic<int> count{0};

    BangTallySink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { count.fetch_add(1); });
    }
    const char* Type() const override {
      return "bang_tally_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A seed far from every limit used below, so nothing can pass by coincidence.
  constexpr int SEED = 918273;

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("urn: creatable through the registry with the documented shape (#454)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_URN);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".urn"));
    CHECK(std::string(YSE::OBJ::G_URN) == std::string(".urn"));
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::BANG);
  }

  TEST_CASE("urn: is listed by the registry (#454)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == std::string(YSE::OBJ::G_URN)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("urn: names both parameters in order (#454)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_URN));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 2);
    CHECK(docs[0].name == std::string("limit"));
    CHECK(docs[1].name == std::string("seed"));
  }

  TEST_CASE("urn: params survive a DumpJSON / ParseJSON round trip (#454)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_URN, "16 4242") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".urn") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".urn"));
    CHECK(h->GetParams() == std::string("16 4242"));
  }

  // ─── exhaustion ─────────────────────────────────────────────────────────────

  TEST_CASE("urn: one cycle is a permutation of the whole range (#454)") {
    // The central claim: every value exactly once, none twice, nothing outside.
    for (int limit : {2, 5, 12, 64, 257}) {
      CAPTURE(limit);
      UrnRig rig((std::to_string(limit) + " " + std::to_string(SEED)).c_str());
      CHECK(IsPermutation(rig.Draw(limit), limit));
    }
  }

  TEST_CASE("urn: the order is actually shuffled, not counted off (#454)") {
    // A .counter would pass the permutation test above. Over 200 values an
    // identity order is astronomically unlikely, so any match means the bag was
    // never shuffled.
    UrnRig rig(("200 " + std::to_string(SEED)).c_str());
    const std::vector<int> cycle = rig.Draw(200);

    int inPlace = 0;
    for (size_t i = 0; i < cycle.size(); ++i) {
      if (cycle[i] == static_cast<int>(i)) inPlace++;
    }
    // Expected number of fixed points in a random permutation is 1, whatever
    // the size; 200 would mean no shuffle at all.
    CHECK(inPlace < 20);
  }

  TEST_CASE("urn: the last value is not held back or handed out early (#454)") {
    // A partial-Fisher-Yates implementation that gets its bounds wrong tends to
    // pin the final element: it either never appears or always appears last.
    // Run many independent cycles and watch where limit-1 lands.
    constexpr int LIMIT = 8;
    std::vector<int> positionsOfLast;
    for (int run = 0; run < 200; ++run) {
      UrnRig rig((std::to_string(LIMIT) + " " + std::to_string(1000 + run)).c_str());
      const std::vector<int> cycle = rig.Draw(LIMIT);
      REQUIRE(IsPermutation(cycle, LIMIT));
      for (int i = 0; i < LIMIT; ++i) {
        if (cycle[i] == LIMIT - 1) positionsOfLast.push_back(i);
      }
    }
    REQUIRE(positionsOfLast.size() == 200);
    std::set<int> distinct(positionsOfLast.begin(), positionsOfLast.end());
    // Every slot should be reachable for the highest value.
    CHECK(distinct.size() == static_cast<size_t>(LIMIT));
  }

  TEST_CASE("urn: a limit of 1 yields 0 once, then empties (#454)") {
    // Max's default. Documented, and easy to get wrong at the boundary.
    UrnRig rig;
    REQUIRE(rig.Bang());
    CHECK(rig.values.received == 0);
    CHECK(rig.empty.bangCount == 0);
    CHECK_FALSE(rig.Bang());
    CHECK(rig.empty.bangCount == 1);
  }

  // ─── the empty urn ──────────────────────────────────────────────────────────

  TEST_CASE("urn: an exhausted urn bangs outlet 1 and emits nothing on outlet 0 (#454)") {
    constexpr int LIMIT = 6;
    UrnRig rig((std::to_string(LIMIT) + " " + std::to_string(SEED)).c_str());
    const std::vector<int> cycle = rig.Draw(LIMIT);
    REQUIRE(IsPermutation(cycle, LIMIT));
    CHECK(rig.empty.bangCount == 0); // not one bang too early

    const int lastValue = rig.values.received;
    for (int i = 1; i <= 20; ++i) {
      CAPTURE(i);
      CHECK_FALSE(rig.Bang()); // outlet 0 silent...
      CHECK(rig.empty.bangCount == i); // ...outlet 1 reports, every time
      CHECK(rig.values.received == lastValue); // and nothing new was pushed
    }
  }

  TEST_CASE("urn: banging an empty urn forever does not corrupt it (#454)") {
    // The cursor is claimed with a compare-exchange precisely so that bangs on
    // an empty urn cannot run it past the limit and wrap. Bang it hard, then
    // check the next cycle is still a complete permutation.
    constexpr int LIMIT = 4;
    UrnRig rig((std::to_string(LIMIT) + " " + std::to_string(SEED)).c_str());
    REQUIRE(IsPermutation(rig.Draw(LIMIT), LIMIT));
    for (int i = 0; i < 100000; ++i)
      REQUIRE_FALSE(rig.Bang());
    CHECK(rig.empty.bangCount == 100000);

    rig.Clear();
    CHECK(IsPermutation(rig.Draw(LIMIT), LIMIT));
  }

  // ─── clear ──────────────────────────────────────────────────────────────────

  TEST_CASE("urn: `clear` refills the urn (#454)") {
    constexpr int LIMIT = 10;
    UrnRig rig((std::to_string(LIMIT) + " " + std::to_string(SEED)).c_str());
    REQUIRE(IsPermutation(rig.Draw(LIMIT), LIMIT));
    REQUIRE_FALSE(rig.Bang()); // empty

    rig.Clear();
    CHECK(IsPermutation(rig.Draw(LIMIT), LIMIT));
  }

  TEST_CASE("urn: `clear` mid-cycle discards the drawn set (#454)") {
    // Half a cycle, then clear: the next full cycle must be complete again,
    // not just the four values that had not come out yet.
    constexpr int LIMIT = 10;
    UrnRig rig((std::to_string(LIMIT) + " " + std::to_string(SEED)).c_str());
    (void)rig.Draw(6);
    rig.Clear();
    CHECK(IsPermutation(rig.Draw(LIMIT), LIMIT));
  }

  TEST_CASE("urn: successive cycles are different orders (#454)") {
    // A refill continues the random stream rather than restarting it, so the
    // object does not loop the same order forever the way Max's does without an
    // explicit `seed 0`.
    constexpr int LIMIT = 50;
    UrnRig rig((std::to_string(LIMIT) + " " + std::to_string(SEED)).c_str());
    const std::vector<int> first = rig.Draw(LIMIT);
    rig.Clear();
    const std::vector<int> second = rig.Draw(LIMIT);
    CHECK(first != second);
    CHECK(IsPermutation(second, LIMIT));
  }

  TEST_CASE("urn: an unknown list message is ignored (#454)") {
    constexpr int LIMIT = 8;
    UrnRig rig((std::to_string(LIMIT) + " " + std::to_string(SEED)).c_str());
    (void)rig.Draw(5);
    for (const char* junk : {"cleared", "clea", "", "seed", "banana", "clear 3"}) {
      CAPTURE(junk);
      rig.urn.GetInlet(0)->SetList(junk, YSE::T_GUI);
    }
    // Still mid-cycle: exactly the three undrawn values are left.
    CHECK(rig.Draw(3).size() == 3u);
    CHECK_FALSE(rig.Bang());
  }

  // ─── changing the limit ─────────────────────────────────────────────────────

  TEST_CASE("urn: a new limit refills over the new range (#454)") {
    UrnRig rig(("4 " + std::to_string(SEED)).c_str());
    REQUIRE(IsPermutation(rig.Draw(4), 4));
    REQUIRE_FALSE(rig.Bang());

    rig.SetLimit(9);
    CHECK(IsPermutation(rig.Draw(9), 9));
  }

  TEST_CASE("urn: changing the limit mid-cycle starts over (#454)") {
    // The drawn set cannot survive a range change: values it records may not
    // exist in the new range, and values in the new range would otherwise never
    // be reachable. Max refills, and so does this.
    UrnRig rig(("20 " + std::to_string(SEED)).c_str());
    (void)rig.Draw(15);
    rig.SetLimit(7);
    CHECK(IsPermutation(rig.Draw(7), 7));
    CHECK_FALSE(rig.Bang());
  }

  TEST_CASE("urn: re-sending the same limit still refills (#454)") {
    UrnRig rig(("6 " + std::to_string(SEED)).c_str());
    (void)rig.Draw(6);
    REQUIRE_FALSE(rig.Bang());
    rig.SetLimit(6);
    CHECK(IsPermutation(rig.Draw(6), 6));
  }

  TEST_CASE("urn: the limit is clamped to 1..4096 (#454)") {
    // Max's documented bounds, and here also the fixed capacity of the bag —
    // an out-of-range limit must clamp, never index past the array.
    UrnRig rig(("10 " + std::to_string(SEED)).c_str());

    for (int bad : {0, -1, -9999}) {
      CAPTURE(bad);
      rig.SetLimit(bad);
      REQUIRE(rig.Bang());
      CHECK(rig.values.received == 0);
      CHECK_FALSE(rig.Bang()); // a single value, so one bang exhausts it
    }

    rig.SetLimit(999999);
    const std::vector<int> cycle = rig.Draw(YSE::PATCHER::gUrn::CAPACITY);
    CHECK(IsPermutation(cycle, YSE::PATCHER::gUrn::CAPACITY));
    CHECK_FALSE(rig.Bang());
  }

  TEST_CASE("urn: the limit inlet takes floats too (#454)") {
    UrnRig rig(("20 " + std::to_string(SEED)).c_str());
    rig.urn.GetInlet(1)->SetFloat(5.9f, YSE::T_GUI); // limit 5
    CHECK(IsPermutation(rig.Draw(5), 5));
    CHECK_FALSE(rig.Bang());
  }

  TEST_CASE("urn: an out-of-range creation limit is clamped, not honoured (#454)") {
    UrnRig rig(("100000 " + std::to_string(SEED)).c_str());
    const std::vector<int> cycle = rig.Draw(YSE::PATCHER::gUrn::CAPACITY);
    CHECK(IsPermutation(cycle, YSE::PATCHER::gUrn::CAPACITY));
    CHECK_FALSE(rig.Bang());
  }

  // ─── reproducibility ────────────────────────────────────────────────────────

  TEST_CASE("urn: the same seed replays the same orders (#454)") {
    constexpr int LIMIT = 64;
    UrnRig a(("64 4242"));
    UrnRig b(("64 4242"));

    const std::vector<int> firstA = a.Draw(LIMIT);
    const std::vector<int> firstB = b.Draw(LIMIT);
    CHECK(firstA == firstB);

    // ...and the second cycle too, which only holds if the refill draws from
    // the same stream position in both.
    a.Clear();
    b.Clear();
    CHECK(a.Draw(LIMIT) == b.Draw(LIMIT));
  }

  TEST_CASE("urn: a different seed gives a different order (#454)") {
    UrnRig a("64 4242");
    UrnRig b("64 4243");
    CHECK(a.Draw(64) != b.Draw(64));
  }

  TEST_CASE("urn: seed 0 leaves neighbouring objects on separate streams (#454)") {
    UrnRig a("64 0");
    UrnRig b("64 0");
    CHECK(a.Draw(64) != b.Draw(64));
  }

  TEST_CASE("urn: `seed <n>` reshuffles what is left without emptying the urn (#454)") {
    // Max's `seed` sets the generator; it does not clear the urn. So the values
    // already handed out must stay out, and the rest must still complete the
    // permutation — in a new order.
    constexpr int LIMIT = 40;
    UrnRig rig(("40 " + std::to_string(SEED)).c_str());
    const std::vector<int> firstHalf = rig.Draw(20);

    rig.Reseed(4242);
    const std::vector<int> secondHalf = rig.Draw(20);
    CHECK_FALSE(rig.Bang()); // exactly LIMIT values came out in total

    std::vector<int> whole = firstHalf;
    whole.insert(whole.end(), secondHalf.begin(), secondHalf.end());
    CHECK(IsPermutation(whole, LIMIT));
  }

  TEST_CASE("urn: `seed <n>` actually changes what comes next (#454)") {
    constexpr int LIMIT = 60;
    UrnRig a(("60 " + std::to_string(SEED)).c_str());
    UrnRig b(("60 " + std::to_string(SEED)).c_str());
    CHECK(a.Draw(20) == b.Draw(20)); // identical so far

    b.Reseed(4242);
    CHECK(a.Draw(40) != b.Draw(40));
  }

  TEST_CASE("urn: a malformed `seed` argument is ignored (#454)") {
    UrnRig a(("40 " + std::to_string(SEED)).c_str());
    UrnRig b(("40 " + std::to_string(SEED)).c_str());
    b.urn.GetInlet(0)->SetList("seed x", YSE::T_GUI);
    b.urn.GetInlet(0)->SetList("seed", YSE::T_GUI);
    CHECK(a.Draw(40) == b.Draw(40));
  }

  // ─── GUI value ──────────────────────────────────────────────────────────────

  TEST_CASE("urn: the GUI value follows the last emitted number (#454)") {
    UrnRig rig(("30 " + std::to_string(SEED)).c_str());
    for (int i = 0; i < 30; ++i) {
      REQUIRE(rig.Bang());
      CHECK(rig.urn.GetGuiValue() == std::to_string(rig.values.received));
    }
    // The empty bang leaves it where it was rather than resetting it.
    const std::string held = rig.urn.GetGuiValue();
    CHECK_FALSE(rig.Bang());
    CHECK(rig.urn.GetGuiValue() == held);
  }

  // ─── concurrency ────────────────────────────────────────────────────────────

  TEST_CASE("urn: concurrent bangs still hand out every value exactly once (#454)") {
    // Inlet handlers run on whichever thread sent the message, so two bangs can
    // meet inside one object. The cursor is claimed with a compare-exchange for
    // exactly this case: the no-repetition guarantee has to survive contention,
    // not merely be unlikely to break under it. A load/store cursor passes every
    // other test in this file and fails this one.
    //
    // A shared plain-int sink would itself be the race, so the tallies are
    // atomic and doctest's (not thread-safe) macros only run after the join.
    // Doubles as an AddressSanitizer / ThreadSanitizer gate.
    constexpr int LIMIT = 2048;
    constexpr int WORKERS = 4;

    YSE::PATCHER::gUrn urn;
    urn.SetParams("2048 4242");

    TallySink tally(LIMIT);
    urn.ConnectOutlet(tally.GetInlet(0), 0);
    tally.ConnectInlet(urn.GetOutlet(0), 0);

    BangTallySink empties;
    urn.ConnectOutlet(empties.GetInlet(0), 1);
    empties.ConnectInlet(urn.GetOutlet(1), 0);

    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    threads.reserve(WORKERS);
    for (int t = 0; t < WORKERS; ++t) {
      threads.emplace_back([&] {
        while (!go.load(std::memory_order_acquire)) {}
        for (int i = 0; i < LIMIT; ++i)
          urn.GetInlet(0)->SetBang(YSE::T_GUI);
      });
    }
    go.store(true, std::memory_order_release);
    for (auto& th : threads)
      th.join();

    // Exactly LIMIT values came out across all four threads...
    CHECK(tally.total.load() == LIMIT);
    // ...each of them exactly once — the no-repetition guarantee under
    // contention. A cursor claimed with load/store hands the same slot to two
    // threads and leaves a later one unread, so both halves of this fail.
    int seenOnce = 0;
    int duplicated = 0;
    for (int v = 0; v < LIMIT; ++v) {
      const int count = tally.counts[static_cast<size_t>(v)].load();
      if (count == 1) seenOnce++;
      if (count > 1) duplicated++;
    }
    CHECK(seenOnce == LIMIT);
    CHECK(duplicated == 0);
    CHECK(tally.strays.load() == 0);
    // ...and every remaining bang found the urn empty.
    CHECK(empties.count.load() == (WORKERS * LIMIT) - LIMIT);
  }

} // TEST_SUITE
