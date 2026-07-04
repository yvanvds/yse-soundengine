// Object-level concurrency regression tests for patcher runtime objects
// (issue #197). These target the per-object read-modify-write / index-vs-read
// races between a control thread (GUI / timer) and the audio thread that the
// graph-swap epic (#189/#234) does NOT cover, because they live inside a single
// published object rather than in the topology:
//
//   * gToggle::Bang  — `value = !value` was an atomic load + a separate atomic
//                      store, not a real RMW, so two concurrent flips collapse
//                      into one (a lost update). The parity test below fails on
//                      that code and passes once Bang is a compare-exchange.
//   * gButton        — GUI_VALUE() read-then-cleared `on` in two steps, so a
//                      press landing between the load and the store was dropped.
//                      exchange(false) closes the window.
//   * gGate          — the value handlers read the `activeOutlet` atomic several
//                      times (bounds check + index). A concurrent SetActiveOutlet
//                      could satisfy the check with a small value and then have
//                      the index sample a large one -> out-of-bounds `outputs[]`
//                      access. Loading it once into a local removes the TOCTOU;
//                      the churn test is an AddressSanitizer gate (tests-asan),
//                      exactly like test_patcher_concurrency.cpp.
//
// The patcher handlers are public (the PATCHER_CLASS macro opens `public:`), so
// the tests drive them directly from several threads — that is precisely the
// real hazard: pHandle::Set* run an inlet handler synchronously on the control
// thread while the audio thread runs the same handler during traversal.
//
// doctest assertion macros are not thread-safe, so worker threads never call
// them: they only touch the object under test, and every CHECK runs on the main
// thread after all workers have joined. No audio device required.

#include <doctest/doctest.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "patcher/guiObjects/gButton.h"
#include "patcher/guiObjects/gToggle.h"
#include "patcher/genericObjects/gGate.h"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;

TEST_SUITE("patcher") {

  // ─── gToggle ────────────────────────────────────────────────────────────────

  TEST_CASE("gToggle: concurrent bangs preserve toggle parity (atomic RMW)") {
    // An even total number of flips must return the toggle to its initial
    // "off" state. With the old load+store flip, concurrent bangs lose updates
    // and the final parity comes out wrong on some runs; the compare-exchange
    // RMW makes every flip count, so the result is deterministic.
    constexpr int kThreads = 4;
    constexpr int kFlipsPerThread = 60000; // kThreads*kFlipsPerThread is even
    static_assert((kThreads * kFlipsPerThread) % 2 == 0, "total flips must be even");

    // A few trials so a rare lucky interleaving on broken code still gets caught.
    for (int trial = 0; trial < 5; ++trial) {
      YSE::PATCHER::gToggle t;
      std::atomic<bool> go{false};

      std::vector<std::thread> flippers;
      flippers.reserve(kThreads);
      for (int i = 0; i < kThreads; ++i) {
        flippers.emplace_back([&] {
          while (!go.load(std::memory_order_acquire)) {}
          for (int n = 0; n < kFlipsPerThread; ++n) {
            t.Bang(0, YSE::T_GUI);
          }
        });
      }
      go.store(true, std::memory_order_release);
      for (auto& th : flippers)
        th.join();

      // Even number of real toggles from "off" -> back to "off".
      CHECK(t.GetGuiValue() == "off");
    }
  }

  // ─── gButton ────────────────────────────────────────────────────────────────

  TEST_CASE("gButton: a press is never lost across a concurrent poll (exchange)") {
    // The producer presses the button once per round; a poller polls in a tight
    // loop. With atomic read-and-clear (exchange), the press is always observed
    // either by the poller or by the main-thread drain poll after the producer
    // stops — it can never be silently cleared. Read-then-clear could drop the
    // press that lands between its load and store.
    constexpr int kRounds = 20000;
    int losses = 0;

    for (int round = 0; round < kRounds; ++round) {
      YSE::PATCHER::gButton b;
      std::atomic<bool> pressed{false};
      std::atomic<bool> sawOn{false};
      std::atomic<bool> stop{false};

      std::thread poller([&] {
        while (!stop.load(std::memory_order_acquire)) {
          if (b.GetGuiValue() == "on") sawOn.store(true, std::memory_order_release);
        }
      });

      b.Bang(0, YSE::T_GUI);
      pressed.store(true, std::memory_order_release);
      stop.store(true, std::memory_order_release);
      poller.join();

      // Drain: after the poller stopped, the press must be visible somewhere.
      const bool drainOn = (b.GetGuiValue() == "on");
      if (!sawOn.load(std::memory_order_acquire) && !drainOn) ++losses;
    }

    CHECK(losses == 0);
  }

  TEST_CASE("gButton: concurrent presses and polls keep the coalescing invariant") {
    // High-contention smoke/ASan exercise: many presses, one poller. Every
    // observed "on" must correspond to at least one press (never a phantom),
    // and the object must survive the storm.
    YSE::PATCHER::gButton b;
    std::atomic<bool> stop{false};
    std::atomic<long> presses{0};
    std::atomic<long> observed{0};

    std::thread poller([&] {
      while (!stop.load(std::memory_order_acquire)) {
        if (b.GetGuiValue() == "on") observed.fetch_add(1, std::memory_order_relaxed);
      }
      // Final drain so the last press is accounted for.
      if (b.GetGuiValue() == "on") observed.fetch_add(1, std::memory_order_relaxed);
    });

    std::vector<std::thread> pressers;
    pressers.reserve(3);
    for (int i = 0; i < 3; ++i) {
      pressers.emplace_back([&] {
        for (int n = 0; n < 100000; ++n) {
          b.Bang(0, YSE::T_GUI);
          presses.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }
    for (auto& th : pressers)
      th.join();
    stop.store(true, std::memory_order_release);
    poller.join();

    // Presses coalesce, so observed <= presses, and at least one must be seen.
    CHECK(observed.load() >= 1);
    CHECK(observed.load() <= presses.load());
  }

  // ─── gGate ──────────────────────────────────────────────────────────────────

  TEST_CASE("gGate: activeOutlet churn never indexes out of bounds (ASan gate)") {
    // AddressSanitizer regression for the index-vs-read TOCTOU: on the old code
    // the value handler bounds-checked `activeOutlet` and then re-read it for the
    // index, so a concurrent SetActiveOutlet flipping it to a large value between
    // the two reads drove `outputs[large]` out of bounds. Loading it once fixes
    // it. Under tests-asan this trips on the broken code; it must stay clean on
    // the fixed code. In a plain (non-ASan) build it is a liveness smoke test.
    // Storm with no outlets connected: the OOB index happens inside the handler
    // before any downstream send, so connections are unnecessary — and leaving
    // them off keeps the value threads from racing on a shared sink's plain
    // fields (which would false-positive the TSan leg rather than exercise the
    // object under test).
    YSE::PATCHER::gGate gate; // default: 2 value outlets, activeOutlet == 0
    std::atomic<bool> stop{false};

    // Selector thread: alternate the active outlet between a valid index (1) and
    // a wildly out-of-range one (a large value). The out-of-range selections must
    // simply be dropped by the handler's bounds check, never indexed.
    std::thread selector([&] {
      int flip = 0;
      while (!stop.load(std::memory_order_acquire)) {
        gate.SetActiveOutlet((flip++ & 1) ? 1 : 1000000, 0, YSE::T_GUI);
      }
    });

    // Value threads: hammer the value inlet with every message kind.
    auto valueBody = [&] {
      while (!stop.load(std::memory_order_acquire)) {
        gate.SetIntValue(7, 1, YSE::T_GUI);
        gate.SetFloatValue(1.5f, 1, YSE::T_GUI);
        gate.SetBangValue(1, YSE::T_GUI);
        gate.SetListValue("x", 1, YSE::T_GUI);
      }
    };
    std::thread v0(valueBody), v1(valueBody);

    for (volatile int spin = 0; spin < 2000000; ++spin) {}
    stop.store(true, std::memory_order_release);
    selector.join();
    v0.join();
    v1.join();

    // Survived the storm; a well-formed selection still routes correctly.
    MultiSink sink0;
    gate.ConnectOutlet(sink0.GetInlet(0), 0);
    sink0.ConnectInlet(gate.GetOutlet(0), 0);
    gate.SetActiveOutlet(1, 0, YSE::T_GUI);
    gate.SetIntValue(42, 1, YSE::T_GUI);
    CHECK(sink0.gotInt);
    CHECK(sink0.intValue == 42);
  }

} // TEST_SUITE("patcher")
