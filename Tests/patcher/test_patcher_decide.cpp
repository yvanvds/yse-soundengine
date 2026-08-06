// Tests for .decide (issue #455) — a random 0 or 1 on every bang.
//
// The object is three lines of code, so the tests are where the substance is.
// "The output was 0 or 1" is not coverage: a constant 0, a strict 0,1,0,1
// alternation and a coin that comes up heads 70% of the time all satisfy it,
// and all three would be wrong. What actually characterises a coin flip is
//
//   * both outcomes occur — not just in one lucky run, but from every seed;
//   * the split is even — to within a band derived from the binomial, not
//     guessed;
//   * successive flips are independent — an alternation has a perfect 50/50
//     split and is not random at all, so the pair distribution is checked too;
//   * reproducibility — a seeded object replays its flips exactly, one draw is
//     taken per flip and nothing else draws, and a re-seed restarts the
//     identical sequence.
//
// ### Why the statistical checks cannot flake
//
// Every test below seeds the object explicitly, and RandomSource is pure
// integer arithmetic over a per-object counter — no clock, no thread identity,
// no floating point. So each sequence is a fixed, platform-independent list of
// numbers and every count below is deterministic: the tolerance is not what
// keeps CI green, it is what keeps the assertion *meaningful* if the generator
// is ever legitimately changed.
//
// The bands are still derived rather than guessed. Over N flips of a fair coin
// the number of ones is Binomial(N, 1/2), with standard deviation sqrt(N)/2.
// Every band below is +/- 6 sigma, a two-sided tail probability of about 2e-9 —
// wide enough that a replacement generator of honest quality passes.
//
// N then follows from the smallest bias worth catching, because a 6-sigma band
// is +/- 3*sqrt(N) ones and a bias of p sits N*(p - 1/2) away from the middle.
// N = 1000000 gives a band of +/- 3000, i.e. +/- 0.3%, so a coin as mildly
// crooked as 50.5/49.5 misses it by 10 sigma. That number is not academic: a
// deliberately mutated 51/49 implementation was measured against N = 100000
// and *passed*, because 6 sigma there is +/- 0.95% and one percent of bias is
// only 6.3 sigma — close enough to the edge that sampling noise carried it
// back inside. The large samples below exist for that reason.
//
// Nothing here needs an audio device.

#include <doctest/doctest.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gDecide.h"
#include "patcher/sinks.hpp"

using TestHelpers::IntSink;

namespace {

  // A .decide wired to an int sink, driven the way a patch would drive it.
  struct DecideRig {
    YSE::PATCHER::gDecide decide;
    IntSink sink;

    explicit DecideRig(const char* params = nullptr) {
      decide.ConnectOutlet(sink.GetInlet(0), 0);
      sink.ConnectInlet(decide.GetOutlet(0), 0);
      if (params != nullptr) decide.SetParams(params);
    }

    // Bang the hot inlet and return what came out.
    int Bang() {
      sink.gotInt = false;
      decide.GetInlet(0)->SetBang(YSE::T_GUI);
      REQUIRE(sink.gotInt);
      return sink.received;
    }

    std::vector<int> Flips(int count) {
      std::vector<int> out;
      out.reserve(static_cast<size_t>(count));
      for (int i = 0; i < count; ++i)
        out.push_back(Bang());
      return out;
    }

    // `seed <n>` on the hot inlet — the .drunk / .urn spelling.
    void Reseed(int s) {
      decide.GetInlet(0)->SetList("seed " + std::to_string(s), YSE::T_GUI);
    }
    // An int on the right inlet — Max's spelling of the same thing.
    void ReseedRight(int s) {
      decide.GetInlet(1)->SetInt(s, YSE::T_GUI);
    }
  };

  // A thread-safe sink for the concurrency case. The plain IntSink above would
  // *be* the race rather than observe it: four threads writing one `int
  // received` is undefined behaviour, and ThreadSanitizer would rightly report
  // the sink instead of the object under test.
  struct TallyDecideSink : YSE::PATCHER::pObject {
    std::atomic<int>& ones;
    std::atomic<int>& total;
    std::atomic<int>& strays;

    TallyDecideSink(std::atomic<int>& onesCount, std::atomic<int>& totalCount,
                    std::atomic<int>& strayCount)
      : pObject(false), ones(onesCount), total(totalCount), strays(strayCount) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        total.fetch_add(1);
        if (v == 1)
          ones.fetch_add(1);
        else if (v != 0)
          strays.fetch_add(1);
      });
    }
    const char* Type() const override {
      return "decide_tally_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  int CountOnes(const std::vector<int>& flips) {
    int ones = 0;
    for (int v : flips) {
      if (v == 1) ones++;
    }
    return ones;
  }

  // A seed unrelated to anything else in this file, so nothing can pass by
  // coincidence.
  constexpr int SEED = 918273;

  // The large-sample size and its 6-sigma half-width. sigma = sqrt(N)/2 = 500
  // at N = 1000000, so 6 sigma is 3000 — a band of +/- 0.3%.
  constexpr int BIG = 1000000;
  constexpr int BIG_TOLERANCE = 3000;

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("decide: creatable through the registry with the documented shape (#455)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DECIDE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".decide"));
    CHECK(std::string(YSE::OBJ::G_DECIDE) == std::string(".decide"));
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
  }

  TEST_CASE("decide: is listed by the registry (#455)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == std::string(YSE::OBJ::G_DECIDE)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("decide: names its single parameter (#455)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_DECIDE));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == std::string("seed"));
  }

  TEST_CASE("decide: params survive a DumpJSON / ParseJSON round trip (#455)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_DECIDE, "4242") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".decide") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".decide"));
    CHECK(h->GetParams() == std::string("4242"));
  }

  TEST_CASE("decide: a reloaded patch replays the same flips (#455)") {
    // The point of putting the seed in the creation parameter rather than only
    // in a message: save, reload, and the composition is the same one.
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_DECIDE, "4242") != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);

    DecideRig reference("4242");
    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);

    // Drive the reloaded object through its handle — the same route a patcher
    // editor takes — and read back what it emitted, comparing against a fresh
    // object built from the same parameter string.
    for (int i = 0; i < 64; ++i) {
      CAPTURE(i);
      h->SetBang(0);
      CHECK(h->GetGuiValue() == std::to_string(reference.Bang()));
    }
  }

  // ─── the output alphabet ────────────────────────────────────────────────────

  TEST_CASE("decide: nothing but 0 and 1 ever comes out (#455)") {
    DecideRig rig(std::to_string(SEED).c_str());
    for (int v : rig.Flips(BIG)) {
      REQUIRE(v >= 0);
      REQUIRE(v <= 1);
    }
  }

  TEST_CASE("decide: both outcomes occur, from every seed (#455)") {
    // A stuck coin is the first thing to rule out, and ruling it out for one
    // seed rules out very little — a generator whose stream depends on the seed
    // can be stuck for one seed and healthy for the next. 200 distinct seeds,
    // 40 flips each: an honest coin gives all-same with probability 2^-39 per
    // seed, so a failure here is a real defect and not bad luck.
    for (int s = 1; s <= 200; ++s) {
      CAPTURE(s);
      DecideRig rig(std::to_string(s * 7919).c_str()); // a prime stride
      const std::vector<int> flips = rig.Flips(40);
      const int ones = CountOnes(flips);
      CHECK(ones > 0);
      CHECK(ones < 40);
    }
  }

  TEST_CASE("decide: seed 0 also produces both outcomes (#455)") {
    // The unseeded path takes its stream from the engine generator rather than
    // from the parameter, so it is a separate code path and gets its own check.
    DecideRig rig("0");
    const int ones = CountOnes(rig.Flips(1000));
    CHECK(ones > 0);
    CHECK(ones < 1000);
  }

  // ─── fairness ───────────────────────────────────────────────────────────────

  TEST_CASE("decide: the split is even over a large sample (#455)") {
    // A million flips; a fair coin has sigma = sqrt(1000000)/2 = 500, so the
    // 6-sigma band is 500000 +/- 3000. A coin biased to 51/49 misses it by 20
    // sigma and one biased to 50.5/49.5 by 10, so this is a real fairness
    // assertion and not a formality. Ten times fewer flips would not be: see
    // the note at the top of the file.
    DecideRig rig(std::to_string(SEED).c_str());
    const int ones = CountOnes(rig.Flips(BIG));
    CAPTURE(ones);
    CHECK(ones > (BIG / 2) - BIG_TOLERANCE);
    CHECK(ones < (BIG / 2) + BIG_TOLERANCE);
  }

  TEST_CASE("decide: the split is even for several unrelated seeds (#455)") {
    // Fairness has to hold per stream, not only on average across streams: a
    // generator can be balanced overall while being lopsided from any given
    // seed. 200000 flips each, sigma = sqrt(200000)/2 = 223.6, 6 sigma = 1342 —
    // still tight enough (+/- 0.67%) that a 51/49 stream misses it by 9 sigma.
    constexpr int N = 200000;
    constexpr int TOLERANCE = 1342;
    for (int s : {1, 2, 3, 4242, 65535, 999983}) {
      CAPTURE(s);
      DecideRig rig(std::to_string(s).c_str());
      const int ones = CountOnes(rig.Flips(N));
      CAPTURE(ones);
      CHECK(ones > (N / 2) - TOLERANCE);
      CHECK(ones < (N / 2) + TOLERANCE);
    }
  }

  TEST_CASE("decide: successive flips are independent, not alternating (#455)") {
    // The check the even-split test cannot make: 0,1,0,1,... splits perfectly
    // 50/50 and is not a coin. Under independence each of the four transitions
    // 00 / 01 / 10 / 11 has probability 1/4, so over M = 999999 pairs the count
    // of each is Binomial(M, 1/4): mean ~250000, sigma = sqrt(M*3/16) = 433,
    // and the 6-sigma band is +/- 2598. A strict alternation puts zero in two
    // of the four buckets, and a coin that merely favours repeats shows up as
    // a shifted 00/11 pair.
    DecideRig rig(std::to_string(SEED).c_str());
    const std::vector<int> flips = rig.Flips(BIG);

    int transitions[4] = {0, 0, 0, 0};
    for (size_t i = 1; i < flips.size(); ++i)
      transitions[(flips[i - 1] * 2) + flips[i]]++;

    const int pairs = BIG - 1;
    const int expected = pairs / 4;
    constexpr int TOLERANCE = 2598;
    for (int t = 0; t < 4; ++t) {
      CAPTURE(t);
      CAPTURE(transitions[t]);
      CHECK(transitions[t] > expected - TOLERANCE);
      CHECK(transitions[t] < expected + TOLERANCE);
    }
  }

  TEST_CASE("decide: no long runs of a single outcome (#455)") {
    // A generator whose bit is nearly constant over stretches of the stream
    // could still pass the aggregate tests above. The longest run of identical
    // outcomes in a million fair flips is about log2(1000000) ~ 20, and a run
    // of 60 anywhere in the sample has probability below 1000000 * 2^-59, i.e.
    // under 2e-12. So 60 is a wide margin that still catches a sticky bit.
    DecideRig rig(std::to_string(SEED).c_str());
    const std::vector<int> flips = rig.Flips(BIG);

    int longest = 0;
    int current = 0;
    int previous = -1;
    for (int v : flips) {
      current = (v == previous) ? current + 1 : 1;
      previous = v;
      if (current > longest) longest = current;
    }
    CAPTURE(longest);
    CHECK(longest > 1); // a strict alternation would leave this at 1
    CHECK(longest < 60);
  }

  // ─── reproducibility ────────────────────────────────────────────────────────

  TEST_CASE("decide: the same seed replays the same flips (#455)") {
    DecideRig a("4242");
    DecideRig b("4242");
    CHECK(a.Flips(500) == b.Flips(500));
  }

  TEST_CASE("decide: a different seed gives a different sequence (#455)") {
    // Adjacent seeds specifically: they are what an off-by-one in the seed
    // normalisation collapses onto the same stream.
    DecideRig a("4242");
    DecideRig b("4243");
    CHECK(a.Flips(500) != b.Flips(500));
  }

  TEST_CASE("decide: seed 0 leaves neighbouring objects on separate streams (#455)") {
    // Two unseeded objects must not flip in lockstep — that is the whole point
    // of per-object state rather than the engine's shared thread_local stream.
    DecideRig a("0");
    DecideRig b("0");
    CHECK(a.Flips(500) != b.Flips(500));
  }

  TEST_CASE("decide: `seed <n>` restarts the identical sequence (#455)") {
    DecideRig rig("4242");
    const std::vector<int> first = rig.Flips(200);
    rig.Reseed(4242);
    CHECK(rig.Flips(200) == first);
  }

  TEST_CASE("decide: an int on the right inlet restarts the sequence too (#455)") {
    // Max's spelling. It must be the same restart, not a different one.
    DecideRig rig("4242");
    const std::vector<int> first = rig.Flips(200);
    rig.ReseedRight(4242);
    CHECK(rig.Flips(200) == first);
  }

  TEST_CASE("decide: re-seeding with a new number changes what comes next (#455)") {
    DecideRig a(std::to_string(SEED).c_str());
    DecideRig b(std::to_string(SEED).c_str());
    CHECK(a.Flips(100) == b.Flips(100)); // identical so far

    b.Reseed(4242);
    CHECK(a.Flips(300) != b.Flips(300));
  }

  TEST_CASE("decide: `seed 0` picks an arbitrary stream at runtime (#455)") {
    // Max documents 0 as "unpredictable". Two objects that were in lockstep
    // must part ways once both take their own arbitrary stream.
    DecideRig a("4242");
    DecideRig b("4242");
    REQUIRE(a.Flips(50) == b.Flips(50));
    a.Reseed(0);
    b.Reseed(0);
    CHECK(a.Flips(500) != b.Flips(500));
  }

  TEST_CASE("decide: a float on the right inlet is truncated to a seed (#455)") {
    DecideRig a("1");
    DecideRig b("1");
    a.decide.GetInlet(1)->SetInt(4242, YSE::T_GUI);
    b.decide.GetInlet(1)->SetFloat(4242.9f, YSE::T_GUI);
    CHECK(a.Flips(200) == b.Flips(200));
  }

  // ─── one draw per flip ──────────────────────────────────────────────────────

  TEST_CASE("decide: exactly one draw per flip, whichever message triggers it (#455)") {
    // Max: "int — In left inlet: same as bang." If either path drew twice, or
    // an int drew a different number of times than a bang, the two sequences
    // would diverge immediately.
    DecideRig banged("4242");
    DecideRig inted("4242");

    std::vector<int> fromInts;
    fromInts.reserve(200);
    for (int i = 0; i < 200; ++i) {
      inted.sink.gotInt = false;
      inted.decide.GetInlet(0)->SetInt(i, YSE::T_GUI); // the value is irrelevant
      REQUIRE(inted.sink.gotInt);
      fromInts.push_back(inted.sink.received);
    }
    CHECK(banged.Flips(200) == fromInts);
  }

  TEST_CASE("decide: a float on the left inlet flips the coin as well (#455)") {
    DecideRig banged("4242");
    DecideRig floated("4242");

    std::vector<int> fromFloats;
    fromFloats.reserve(200);
    for (int i = 0; i < 200; ++i) {
      floated.sink.gotInt = false;
      floated.decide.GetInlet(0)->SetFloat(0.5f * static_cast<float>(i), YSE::T_GUI);
      REQUIRE(floated.sink.gotInt);
      fromFloats.push_back(floated.sink.received);
    }
    CHECK(banged.Flips(200) == fromFloats);
  }

  TEST_CASE("decide: nothing but a flip consumes a draw (#455)") {
    // Messages the object ignores must not advance the stream — otherwise a
    // seeded patch would replay differently the moment an unrelated message
    // passed through.
    DecideRig quiet("4242");
    DecideRig noisy("4242");
    for (const char* junk : {"seed", "seed x", "clear", "", "banana", "set 4", "seeded 5"}) {
      CAPTURE(junk);
      noisy.decide.GetInlet(0)->SetList(junk, YSE::T_GUI);
    }
    // A list on the seed inlet is not a message this object understands either.
    noisy.decide.GetInlet(1)->SetList("seed 99", YSE::T_GUI);
    CHECK(quiet.Flips(200) == noisy.Flips(200));
  }

  TEST_CASE("decide: a malformed `seed` argument is ignored (#455)") {
    DecideRig a("4242");
    DecideRig b("4242");
    b.Reseed(0); // establishes that b is otherwise reachable
    b.Reseed(4242); // ...and back to the shared stream
    b.decide.GetInlet(0)->SetList("seed x", YSE::T_GUI);
    b.decide.GetInlet(0)->SetList("seedy 4", YSE::T_GUI);
    CHECK(a.Flips(200) == b.Flips(200));
  }

  TEST_CASE("decide: a negative seed is a legal stream (#455)") {
    // ReadIntArg accepts a sign, and every 32-bit word is a valid SplitMix64
    // stream, so this must behave like any other seed rather than degenerate.
    DecideRig a("-4242");
    DecideRig b("-4242");
    const std::vector<int> flips = a.Flips(2000);
    CHECK(flips == b.Flips(2000));
    const int ones = CountOnes(flips);
    CHECK(ones > 0);
    CHECK(ones < 2000);
  }

  // ─── GUI value ──────────────────────────────────────────────────────────────

  TEST_CASE("decide: the GUI value follows the last emitted flip (#455)") {
    DecideRig rig(std::to_string(SEED).c_str());
    for (int i = 0; i < 200; ++i) {
      CAPTURE(i);
      const int value = rig.Bang();
      CHECK(rig.decide.GetGuiValue() == std::to_string(value));
    }
  }

  TEST_CASE("decide: an unbanged object reports a defined GUI value (#455)") {
    DecideRig rig(std::to_string(SEED).c_str());
    CHECK(rig.decide.GetGuiValue() == std::string("0"));
  }

  // ─── concurrency ────────────────────────────────────────────────────────────

  TEST_CASE("decide: concurrent bangs consume each draw exactly once (#455)") {
    // Inlet handlers run synchronously on whichever thread sent the message, so
    // a GUI bang and an audio-thread bang can meet inside one object. The draw
    // counter is advanced with fetch_add, which means every bang claims a
    // *distinct* position in the stream however the threads interleave — so the
    // multiset of values four threads produce is exactly the multiset one
    // thread produces over the same number of flips. That equality is asserted
    // rather than a loose "roughly even": a load/store counter would hand two
    // threads the same position and skew it.
    //
    // A shared plain-int sink would itself be the race, so the tally is atomic
    // and doctest's (not thread-safe) macros only run after the join. Doubles
    // as an AddressSanitizer / ThreadSanitizer gate.
    constexpr int WORKERS = 4;
    constexpr int PER_WORKER = BIG / WORKERS;

    // What a single thread produces over the same stream positions.
    DecideRig reference("4242");
    const int expectedOnes = CountOnes(reference.Flips(BIG));

    YSE::PATCHER::gDecide decide;
    decide.SetParams("4242");

    std::atomic<int> ones{0};
    std::atomic<int> total{0};
    std::atomic<int> strays{0};
    TallyDecideSink tally(ones, total, strays);
    decide.ConnectOutlet(tally.GetInlet(0), 0);
    tally.ConnectInlet(decide.GetOutlet(0), 0);

    std::atomic<bool> go{false};
    std::vector<std::thread> workers;
    workers.reserve(WORKERS);
    for (int t = 0; t < WORKERS; ++t) {
      workers.emplace_back([&] {
        while (!go.load(std::memory_order_acquire)) {}
        for (int i = 0; i < PER_WORKER; ++i)
          decide.GetInlet(0)->SetBang(YSE::T_GUI);
      });
    }
    go.store(true, std::memory_order_release);
    for (auto& w : workers)
      w.join();

    CHECK(total.load() == BIG);
    CHECK(strays.load() == 0);
    CHECK(ones.load() == expectedOnes);
  }

} // TEST_SUITE
