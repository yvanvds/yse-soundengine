// Tests for the bounded random walk .drunk (issue #453), and for the
// RandomSource it is built on — the per-object, seedable PRNG that the rest of
// the patcher's random family (.urn, .decide, .prob) will share.
//
// "The output was in range" is not coverage for a random walk: the range
// invariant holds for a constant, for white noise, and for everything in
// between. What actually characterises .drunk is
//
//   * the step distribution — |step| < |stepSize|, uniform over the magnitudes
//     that leaves, with zero included only when stepSize is positive;
//   * the boundary behaviour — clipping, which piles probability onto the two
//     edges, as opposed to the reflecting boundary .pong would give;
//   * reproducibility — a seeded walk replays exactly, including *how many*
//     draws it takes per bang.
//
// So the tests below seed everything and assert on the sequence itself.
// Nothing here needs an audio device.

#include <doctest/doctest.h>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gDrunk.h"
#include "patcher/math/gRandomSource.h"
#include "patcher/sinks.hpp"

using TestHelpers::IntSink;
using YSE::PATCHER::RandomSource;

namespace {

  // A .drunk wired to an int sink, driven the way a patch would drive it.
  struct DrunkRig {
    YSE::PATCHER::gDrunk drunk;
    IntSink sink;

    explicit DrunkRig(const char* params = nullptr) {
      drunk.ConnectOutlet(sink.GetInlet(0), 0);
      sink.ConnectInlet(drunk.GetOutlet(0), 0);
      if (params != nullptr) drunk.SetParams(params);
    }

    // Bang the hot inlet and return what came out.
    int Bang() {
      sink.gotInt = false;
      drunk.GetInlet(0)->SetBang(YSE::T_GUI);
      return sink.received;
    }

    void SetRange(int r) {
      drunk.GetInlet(1)->SetInt(r, YSE::T_GUI);
    }
    void SetStep(int s) {
      drunk.GetInlet(2)->SetInt(s, YSE::T_GUI);
    }
    // `set <n>` — moves the walk without emitting.
    void Place(int v) {
      drunk.GetInlet(0)->SetList("set " + std::to_string(v), YSE::T_GUI);
    }
    void Reseed(int s) {
      drunk.GetInlet(0)->SetList("seed " + std::to_string(s), YSE::T_GUI);
    }

    std::vector<int> Walk(int steps) {
      std::vector<int> out;
      out.reserve(static_cast<size_t>(steps));
      for (int i = 0; i < steps; ++i)
        out.push_back(Bang());
      return out;
    }
  };

  // The deltas between successive emitted values — the step distribution,
  // which is the thing worth asserting on.
  std::vector<int> Deltas(const std::vector<int>& walk, int from) {
    std::vector<int> deltas;
    deltas.reserve(walk.size());
    int previous = from;
    for (int v : walk) {
      deltas.push_back(v - previous);
      previous = v;
    }
    return deltas;
  }

  // A seed that is far from the range and the step size, so nothing can pass
  // by coincidence.
  constexpr int SEED = 918273;

} // namespace

TEST_SUITE("patcher") {

  // ─── RandomSource ───────────────────────────────────────────────────────────
  // The shared per-object generator. .urn / .decide / .prob build on this, so
  // its contract is pinned here rather than only through .drunk.

  TEST_CASE("RandomSource: the same seed replays the same sequence (#453)") {
    RandomSource a;
    RandomSource b;
    a.Seed(4242);
    b.Seed(4242);

    for (int i = 0; i < 256; ++i) {
      CAPTURE(i);
      REQUIRE(a.Next() == b.Next());
    }

    // ...and re-seeding restarts it from the beginning rather than continuing.
    a.Seed(4242);
    b.Seed(4242);
    CHECK(a.Next() == b.Next());
  }

  TEST_CASE("RandomSource: adjacent seeds give unrelated sequences (#453)") {
    // Adjacent seeds are the interesting case, not distant ones: an earlier
    // version normalised the seed with `| 1`, which silently gave every even
    // seed the same stream as its odd successor. Walk a few consecutive pairs.
    for (unsigned int base : {1u, 41u, 4242u, 65535u, 0x7FFFFFFFu}) {
      CAPTURE(base);
      RandomSource a;
      RandomSource b;
      a.Seed(base);
      b.Seed(base + 1);

      int identical = 0;
      for (int i = 0; i < 256; ++i) {
        if (a.Next() == b.Next()) identical++;
      }
      CHECK(identical == 0);
    }
  }

  TEST_CASE("RandomSource: Draws() counts one per draw (#453)") {
    RandomSource r;
    r.Seed(7);
    CHECK(r.Draws() == 0u);
    for (unsigned int i = 1; i <= 10; ++i) {
      (void)r.Next();
      CHECK(r.Draws() == i);
    }
    r.Seed(7);
    CHECK(r.Draws() == 0u);
  }

  TEST_CASE("RandomSource: Bounded stays in range and covers it (#453)") {
    RandomSource r;
    r.Seed(SEED);

    // The empty bound has no legal answer but 0, and must not divide.
    CHECK(r.Bounded(0) == 0u);
    CHECK(r.Bounded(1) == 0u);

    std::map<unsigned int, int> counts;
    constexpr unsigned int BOUND = 8;
    constexpr int DRAWS = 40000;
    for (int i = 0; i < DRAWS; ++i) {
      const unsigned int v = r.Bounded(BOUND);
      REQUIRE(v < BOUND);
      counts[v]++;
    }
    // Uniform to within a generous margin — this is a distribution check, not
    // a statistics exam. Expected 5000 each.
    REQUIRE(counts.size() == BOUND);
    for (const auto& entry : counts) {
      CAPTURE(entry.first);
      CHECK(entry.second > 4000);
      CHECK(entry.second < 6000);
    }
  }

  TEST_CASE("RandomSource: Between honours both ends and the empty range (#453)") {
    RandomSource r;
    r.Seed(SEED);

    CHECK(r.Between(5, 5) == 5); // empty
    CHECK(r.Between(5, 3) == 5); // inverted

    bool sawLow = false;
    bool sawHigh = false;
    for (int i = 0; i < 5000; ++i) {
      const int v = r.Between(-3, 4); // [-3, 3]
      REQUIRE(v >= -3);
      REQUIRE(v <= 3);
      if (v == -3) sawLow = true;
      if (v == 3) sawHigh = true;
    }
    CHECK(sawLow);
    CHECK(sawHigh);
  }

  TEST_CASE("RandomSource: NextFloat stays in [0, 1) (#453)") {
    RandomSource r;
    r.Seed(SEED);
    float lowest = 1.f;
    float highest = 0.f;
    for (int i = 0; i < 20000; ++i) {
      const float v = r.NextFloat();
      REQUIRE(v >= 0.f);
      REQUIRE(v < 1.f);
      if (v < lowest) lowest = v;
      if (v > highest) highest = v;
    }
    // Both tails should actually be visited over 20k draws.
    CHECK(lowest < 0.01f);
    CHECK(highest > 0.99f);
  }

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("drunk: creatable through the registry with the documented shape (#453)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DRUNK);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".drunk"));
    CHECK(std::string(YSE::OBJ::G_DRUNK) == std::string(".drunk"));
    CHECK(h->GetInputs() == 3);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
  }

  TEST_CASE("drunk: is listed by the registry (#453)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == std::string(YSE::OBJ::G_DRUNK)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("drunk: names all three parameters in order (#453)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_DRUNK));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 3);
    const std::vector<std::string> expected = {"range", "stepSize", "seed"};
    for (size_t i = 0; i < expected.size(); ++i) {
      CAPTURE(i);
      CHECK(docs[i].name == expected[i]);
    }
  }

  TEST_CASE("drunk: params survive a DumpJSON / ParseJSON round trip (#453)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_DRUNK, "64 4 12345") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".drunk") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".drunk"));
    CHECK(h->GetParams() == std::string("64 4 12345"));
  }

  // ─── reproducibility ────────────────────────────────────────────────────────

  TEST_CASE("drunk: a seeded walk replays exactly (#453)") {
    DrunkRig a("1000 6 4242");
    DrunkRig b("1000 6 4242");
    a.Place(500);
    b.Place(500);

    const std::vector<int> walkA = a.Walk(300);
    const std::vector<int> walkB = b.Walk(300);
    CHECK(walkA == walkB);

    // A walk that never moves would satisfy the above trivially.
    bool moved = false;
    for (size_t i = 1; i < walkA.size(); ++i) {
      if (walkA[i] != walkA[i - 1]) moved = true;
    }
    CHECK(moved);
  }

  TEST_CASE("drunk: a different seed gives a different walk (#453)") {
    DrunkRig a("1000 6 4242");
    DrunkRig b("1000 6 4243");
    a.Place(500);
    b.Place(500);
    CHECK(a.Walk(300) != b.Walk(300));
  }

  TEST_CASE("drunk: seed 0 leaves neighbouring objects on separate streams (#453)") {
    // Two unseeded objects must not walk in lockstep — that is the whole point
    // of per-object state rather than the engine's shared thread_local stream.
    DrunkRig a("1000 6 0");
    DrunkRig b("1000 6 0");
    a.Place(500);
    b.Place(500);
    CHECK(a.Walk(300) != b.Walk(300));
  }

  TEST_CASE("drunk: `seed <n>` restarts the sequence at runtime (#453)") {
    DrunkRig a("1000 6 4242");
    a.Place(500);
    const std::vector<int> first = a.Walk(50);

    a.Reseed(4242);
    a.Place(500);
    const std::vector<int> second = a.Walk(50);
    CHECK(first == second);
  }

  // ─── step distribution ──────────────────────────────────────────────────────
  // A wide range with the walk parked in the middle, so clipping cannot touch
  // the results and every delta observed is a real draw.

  TEST_CASE("drunk: |step| is strictly below stepSize, zero included (#453)") {
    DrunkRig rig("100000 4 4242"); // deltas must land in [-3, 3]
    rig.Place(50000);

    const std::vector<int> walk = rig.Walk(20000);
    std::map<int, int> counts;
    for (int d : Deltas(walk, 50000)) {
      REQUIRE(d >= -3);
      REQUIRE(d <= 3);
      counts[d]++;
    }

    // All seven outcomes occur, roughly uniformly (expected 20000/7 ≈ 2857).
    REQUIRE(counts.size() == 7);
    for (const auto& entry : counts) {
      CAPTURE(entry.first);
      CHECK(entry.second > 2400);
      CHECK(entry.second < 3300);
    }
  }

  TEST_CASE("drunk: the default step size draws from {-1, 0, +1} (#453)") {
    DrunkRig rig("100000 2 4242");
    rig.Place(50000);

    std::map<int, int> counts;
    for (int d : Deltas(rig.Walk(9000), 50000)) {
      REQUIRE(d >= -1);
      REQUIRE(d <= 1);
      counts[d]++;
    }
    REQUIRE(counts.size() == 3);
    for (const auto& entry : counts) {
      CAPTURE(entry.first);
      CHECK(entry.second > 2500); // expected 3000 each
      CHECK(entry.second < 3500);
    }
  }

  TEST_CASE("drunk: a negative step size never produces a zero step (#453)") {
    DrunkRig rig("100000 -4 4242"); // {-3,-2,-1,+1,+2,+3}
    rig.Place(50000);

    std::map<int, int> counts;
    for (int d : Deltas(rig.Walk(18000), 50000)) {
      REQUIRE(d != 0);
      REQUIRE(d >= -3);
      REQUIRE(d <= 3);
      counts[d]++;
    }
    REQUIRE(counts.size() == 6);
    for (const auto& entry : counts) {
      CAPTURE(entry.first);
      CHECK(entry.second > 2500); // expected 3000 each
      CHECK(entry.second < 3500);
    }
  }

  TEST_CASE("drunk: a step size of 1 or 0 leaves the walk standing still (#453)") {
    for (int limit : {1, 0, -1}) {
      CAPTURE(limit);
      DrunkRig rig("100000 2 4242");
      rig.Place(50000);
      rig.SetStep(limit);
      for (int i = 0; i < 100; ++i)
        CHECK(rig.Bang() == 50000);
    }
  }

  TEST_CASE("drunk: one draw per bang, whatever the step size (#453)") {
    // The standing-still case must still consume its draw, or a seeded
    // sequence would shift the moment the step size changed. Object A idles
    // for 40 bangs with a step size that cannot move it, then walks; object B
    // walks from the start. If both consume exactly one draw per bang, A's
    // steps from bang 41 on are B's steps from bang 41 on.
    DrunkRig a("100000 1 4242");
    DrunkRig b("100000 6 4242");
    a.Place(50000);
    b.Place(50000);

    for (int i = 0; i < 40; ++i)
      CHECK(a.Bang() == 50000); // step size 1 cannot move it
    (void)b.Walk(40);

    a.SetStep(6);
    const int startB = b.sink.received; // read before the walk moves it
    const std::vector<int> tailA = Deltas(a.Walk(60), 50000);
    const std::vector<int> tailB = Deltas(b.Walk(60), startB);
    CHECK(tailA == tailB);
  }

  // ─── boundaries ─────────────────────────────────────────────────────────────

  TEST_CASE("drunk: the walk never leaves [0, range) (#453)") {
    DrunkRig rig("8 5 4242"); // a range narrow enough to hit both edges often
    for (int v : rig.Walk(5000)) {
      REQUIRE(v >= 0);
      REQUIRE(v < 8);
    }
  }

  TEST_CASE("drunk: the boundary clips rather than reflects (#453)") {
    // With a step size far wider than the range, clipping piles almost all the
    // probability onto the two edges: any draw pushing past a boundary lands
    // *on* it. A reflecting or wrapping boundary would instead spread the
    // output roughly evenly over the five legal values, so the concentration
    // below is the signature that distinguishes the two.
    DrunkRig rig("5 100 4242"); // values 0..4, deltas in [-99, 99]
    const std::vector<int> walk = rig.Walk(4000);

    std::map<int, int> counts;
    for (int v : walk) {
      REQUIRE(v >= 0);
      REQUIRE(v < 5);
      counts[v]++;
    }
    const int edges = counts[0] + counts[4];
    CHECK(edges > 3400); // ~98% in practice; a reflecting boundary gives ~40%
    CHECK(counts[0] > 0);
    CHECK(counts[4] > 0);
    // The interior is still reachable — the walk is clipped, not pinned. Only
    // the 3 deltas out of 199 that land inside get there, so this is a small
    // count, but a zero would mean the object had stopped walking entirely.
    CHECK((counts[1] + counts[2] + counts[3]) > 0);
  }

  TEST_CASE("drunk: a degenerate range collapses onto 0 (#453)") {
    for (int range : {1, 0, -5}) {
      CAPTURE(range);
      DrunkRig rig("100 8 4242");
      rig.SetRange(range);
      for (int i = 0; i < 50; ++i)
        CHECK(rig.Bang() == 0);
    }
  }

  TEST_CASE("drunk: narrowing the range pulls the stored value in at once (#453)") {
    DrunkRig rig("1000 2 4242");
    rig.Place(900);
    rig.SetRange(10);
    CHECK(rig.drunk.GetGuiValue() == std::string("9")); // 10 is exclusive
  }

  // ─── setting the position ───────────────────────────────────────────────────

  TEST_CASE("drunk: an int on inlet 0 sets the position and emits it (#453)") {
    DrunkRig rig("1000 2 4242");
    rig.sink.gotInt = false;
    rig.drunk.GetInlet(0)->SetInt(432, YSE::T_GUI);
    CHECK(rig.sink.gotInt);
    CHECK(rig.sink.received == 432);
    CHECK(rig.drunk.GetGuiValue() == std::string("432"));
  }

  TEST_CASE("drunk: a position outside the range is clipped on the way in (#453)") {
    DrunkRig rig("10 2 4242");
    rig.drunk.GetInlet(0)->SetInt(999, YSE::T_GUI);
    CHECK(rig.sink.received == 9);
    rig.drunk.GetInlet(0)->SetInt(-999, YSE::T_GUI);
    CHECK(rig.sink.received == 0);
  }

  TEST_CASE("drunk: a float on inlet 0 is truncated (#453)") {
    DrunkRig rig("1000 2 4242");
    rig.drunk.GetInlet(0)->SetFloat(43.9f, YSE::T_GUI);
    CHECK(rig.sink.received == 43);
  }

  TEST_CASE("drunk: `set <n>` moves the walk without emitting (#453)") {
    DrunkRig rig("1000 2 4242");
    rig.drunk.GetInlet(0)->SetInt(100, YSE::T_GUI); // prime the sink
    rig.sink.gotInt = false;

    rig.Place(700);
    CHECK_FALSE(rig.sink.gotInt); // nothing was emitted
    CHECK(rig.drunk.GetGuiValue() == std::string("700"));

    // ...but the next bang walks on from where `set` left it.
    const int next = rig.Bang();
    CHECK(next >= 699);
    CHECK(next <= 701);
  }

  TEST_CASE("drunk: a malformed list is ignored rather than acted on (#453)") {
    DrunkRig rig("1000 2 4242");
    rig.Place(500);
    for (const char* junk : {"set", "set x", "seed", "reset", "", "banana"}) {
      CAPTURE(junk);
      rig.drunk.GetInlet(0)->SetList(junk, YSE::T_GUI);
      CHECK(rig.drunk.GetGuiValue() == std::string("500"));
    }
  }

  TEST_CASE("drunk: the range and step inlets take floats too (#453)") {
    DrunkRig rig("1000 2 4242");
    rig.Place(500);
    rig.drunk.GetInlet(1)->SetFloat(10.9f, YSE::T_GUI); // range 10
    rig.drunk.GetInlet(2)->SetFloat(3.9f, YSE::T_GUI); // step limit 3
    for (int i = 0; i < 200; ++i) {
      const int v = rig.Bang();
      REQUIRE(v >= 0);
      REQUIRE(v < 10);
    }
  }

  // ─── concurrency smoke test ─────────────────────────────────────────────────
  // Inlet handlers run synchronously on whichever thread sent the message, so
  // a GUI set and an audio-thread bang can meet inside one object. Nothing here
  // may tear or leave the value outside the range. doctest's macros are not
  // thread-safe, so the workers only touch the object and every check runs
  // after the join. Doubles as an AddressSanitizer / ThreadSanitizer gate.

  TEST_CASE("drunk: concurrent bangs keep the value inside the range (#453)") {
    // No sink is wired up here on purpose: a shared sink would itself be the
    // race (four threads writing one plain int), and the object under test is
    // the drunk, not the sink. The emitted value goes nowhere; what is checked
    // is the state the object is left in.
    YSE::PATCHER::gDrunk drunk;
    drunk.SetParams("32 5 4242");

    std::atomic<bool> go{false};
    std::vector<std::thread> workers;
    workers.reserve(4);
    for (int t = 0; t < 4; ++t) {
      workers.emplace_back([&] {
        while (!go.load(std::memory_order_acquire)) {}
        for (int i = 0; i < 20000; ++i) {
          drunk.GetInlet(0)->SetBang(YSE::T_GUI);
        }
      });
    }
    go.store(true, std::memory_order_release);
    for (auto& w : workers)
      w.join();

    const int finalValue = std::stoi(drunk.GetGuiValue());
    CHECK(finalValue >= 0);
    CHECK(finalValue < 32);
  }

} // TEST_SUITE
