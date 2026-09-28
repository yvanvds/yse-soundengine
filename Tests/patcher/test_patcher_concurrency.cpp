// Concurrency stress test for the patcher (issue #229) — the acceptance gate
// for epic #189 (wait-free graph rearchitecture).
//
// The races the epic fixes were found by code audit, not by a test: nothing
// exercised concurrent live editing of a *rendering* patcher, so a regression
// would slip through. This test drives one thread rendering blocks
// (`Calculate`, the audio-thread entry point) while other threads mutate the
// same patcher, and is meant to be run under ThreadSanitizer + AddressSanitizer
// (see the `tsan` / `asan` presets and the CI legs). On pre-epic code it trips
// TSan/ASan almost immediately; on post-epic code it must stay clean.
//
// Threading model — deliberately mirrors the ownership split the epic designed:
//   * one render thread  : the audio thread. Calls `Calculate` in a tight loop,
//                          lock-free, reading only the published GraphState.
//   * one editor thread  : the structural / file-handler control path. Owns the
//                          entire handle lifecycle (create/delete/connect/
//                          disconnect/ParseJSON/Clear/SetName), so no other
//                          thread can ever touch a freed handle. All of these
//                          serialise on the patcher's `mtx`.
//   * two value threads  : the value control path. PassBang/PassData address
//                          receivers by *name* (never by handle), so they are
//                          safe regardless of what the editor does to the graph,
//                          and two of them stress the multi-producer queue.
//
// Coverage is limited to the paths the epic already made safe. In-place
// `pHandle::SetParams` re-parse (which reallocates a live object's pins and is
// NOT routed through the swap) is a separate, still-open race tracked as #234;
// this harness is extended to drive it once that lands. Pin-count reallocation
// is exercised here only through the safe paths: creating `gGate`/`gSwitch`
// with varying pin counts (the `PARM_PARSE` grow runs off-thread under `mtx`
// before the swap) and re-parsing whole graphs via ParseJSON.
//
// doctest assertion macros are not thread-safe, so the worker threads call none
// of them: a race/UAF is caught by the sanitizer aborting the process, and the
// logical post-conditions are asserted on the main thread after every worker
// has joined. Logging is silenced for the duration — the engine logger writes a
// shared ofstream without a lock (a debug build logs on every edit), so leaving
// it on would trip TSan on the logger rather than on the code under test.
//
// No audio device required.

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "dsp/buffer.hpp"
#include "implementations/logImplementation.h"
#include "support/timer_pacing.hpp"

using YSE::PATCHER::patcherImplementation;

namespace {

  // Receiver names shared between the editor (which creates/destroys them) and
  // the value threads (which target them by name). A small pool keeps the "hit"
  // rate high while still racing creation/deletion, so both the delivered and
  // the not-found value paths are exercised.
  const char* const kReceiverNames[] = {"a", "b", "c"};
  constexpr int kReceiverCount = 3;

  // A raii guard that silences the global logger for the lifetime of the test
  // and restores the previous level on the way out. Set before any worker
  // thread starts and restored after all have joined, so the single write to
  // the shared `level` happens-before / happens-after every read.
  struct LogSilencer {
    YSE::ERROR_LEVEL previous;
    LogSilencer() : previous(YSE::INTERNAL::LogImpl().getLevel()) {
      YSE::INTERNAL::LogImpl().setLevel(YSE::EL_NONE);
    }
    ~LogSilencer() {
      YSE::INTERNAL::LogImpl().setLevel(previous);
    }
  };

  // A small, self-contained graph (noise -> dac, plus a receiver named "a")
  // dumped once at setup. The editor re-parses this via ParseJSON to exercise
  // the file-handler swap path with a known, bounded object set.
  std::string makeSavedGraph() {
    patcherImplementation seed(1, nullptr);
    YSE::pHandle* noise = seed.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* dac = seed.CreateObject(YSE::OBJ::D_DAC, "");
    seed.Connect(noise, 0, dac, 0);
    seed.CreateObject(YSE::OBJ::G_RECEIVE, "a");
    return seed.DumpJSON();
  }

  // A live object the editor owns: its handle plus a stable, monotonically
  // increasing creation order. Edges are only ever added from a lower `seq` to a
  // higher one, so the message/DSP graph the editor builds is always acyclic —
  // a value delivered into it can't feed back and recurse forever (patcher
  // objects have no message-loop guard; feedback recursion is a separate,
  // pre-existing hazard, tracked as #236). `seq` is a fixed total order, so the
  // acyclicity survives deletions (which shuffle vector positions).
  struct Node {
    YSE::pHandle* h;
    int seq;
  };

  // The editor thread body: a deterministic (fixed-seed) randomized mix of every
  // safe control-thread operation, run for `iterations` steps. It owns `live`,
  // the set of handles it has created and not yet deleted, so it never hands a
  // freed handle to any other thread (there is none) or reuses one itself.
  void runEditor(patcherImplementation& p, const std::string& savedGraph, int iterations) {
    // Fixed seed on purpose: a stress failure must be replayable bit-for-bit, so
    // the editor's operation stream is deterministic across runs.
    std::mt19937 rng(0xC0FFEE); // NOLINT(cert-msc51-cpp,bugprone-random-generator-seed)
    auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); };

    std::vector<Node> live;
    live.reserve(32);
    int nextRecv = 0;
    int nextSeq = 0;

    for (int i = 0; i < iterations; ++i) {
      // Weighted operation choice: mostly cheap create/connect/disconnect churn,
      // with rarer heavyweight file-handler ops so the swap/retire/reclaim path
      // runs constantly without the object set exploding.
      int op = pick(100);

      if (op < 30 && live.size() < 20) {
        // CREATE — spread across plain, DSP and pin-reallocating generic types.
        // gSend/gReceive bus feedback is deliberately out of scope: gSend
        // publishes through the process-global NamedBus, a different subsystem
        // from the patcher GraphState epic. gReceive is kept (it is the value-
        // queue delivery target); gSend is not created here.
        int kind = pick(5);
        YSE::pHandle* h = nullptr;
        switch (kind) {
        case 0:
          h = p.CreateObject(YSE::OBJ::D_NOISE, "");
          break;
        case 1:
          h = p.CreateObject(YSE::OBJ::D_DAC, "");
          break;
        case 2:
          // gGate with 2..5 outlets: PARM_PARSE grows `outputs` at construction.
          h = p.CreateObject(YSE::OBJ::G_GATE, std::to_string(2 + pick(4)));
          break;
        case 3:
          // gSwitch with 2..5 inlets: PARM_PARSE grows `inputs` at construction.
          h = p.CreateObject(YSE::OBJ::G_SWITCH, std::to_string(2 + pick(4)));
          break;
        case 4:
          h = p.CreateObject(YSE::OBJ::G_RECEIVE, kReceiverNames[nextRecv++ % kReceiverCount]);
          break;
        default:
          break;
        }
        if (h != nullptr) live.push_back({h, nextSeq++});

      } else if (op < 55 && live.size() >= 2) {
        // CONNECT outlet 0 -> inlet 0, always from the lower-seq object to the
        // higher-seq one so the graph stays acyclic (see Node). Only when both
        // pins actually exist (a ~dac has no outlet, a ~noise no inlet). This
        // models a well-behaved editor; the harness targets concurrency, not
        // input validation — Disconnect with a missing pin is a separate latent
        // null-deref, tracked as #235.
        int a = pick(static_cast<int>(live.size()));
        int b = pick(static_cast<int>(live.size()));
        if (live[a].seq > live[b].seq) std::swap(a, b);
        if (a != b && live[a].h->GetOutputs() > 0 && live[b].h->GetInputs() > 0)
          p.Connect(live[a].h, 0, live[b].h, 0);

      } else if (op < 70 && live.size() >= 2) {
        // DISCONNECT — harmless if the pair was never connected, as long as both
        // pins exist and the direction matches a possible CONNECT (lower -> higher).
        int a = pick(static_cast<int>(live.size()));
        int b = pick(static_cast<int>(live.size()));
        if (live[a].seq > live[b].seq) std::swap(a, b);
        if (a != b && live[a].h->GetOutputs() > 0 && live[b].h->GetInputs() > 0)
          p.Disconnect(live[a].h, 0, live[b].h, 0);

      } else if (op < 82 && !live.empty()) {
        // DELETE a random live object and drop it from our set.
        int a = pick(static_cast<int>(live.size()));
        p.DeleteObject(live[a].h);
        live.erase(live.begin() + a);

      } else if (op < 90) {
        // RENAME — re-anchors every gReceive bus subscription.
        p.SetName("stress_" + std::to_string(pick(4)));

      } else if (op < 96) {
        // FILE-HANDLER: clear the whole graph then re-parse the saved one in a
        // single swap. Clear frees every handle we own, so reset `live` too;
        // the re-parsed objects are owned by the patcher (untracked here) and
        // reclaimed by the next Clear or at teardown.
        p.Clear();
        live.clear();
        p.ParseJSON(savedGraph);

      } else {
        // DUMP — reads the object set under mtx; cheap consistency exercise.
        volatile std::size_t n = p.DumpJSON().size();
        (void)n;
      }
    }
  }

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("concurrency: live-edit a rendering patcher under sanitizers") {
    LogSilencer silence;

    patcherImplementation p(1, nullptr);

    // Seed a baseline so the very first rendered block has a graph to walk.
    YSE::pHandle* noise = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(noise, 0, dac, 0);
    p.CreateObject(YSE::OBJ::G_RECEIVE, "a");

    const std::string savedGraph = makeSavedGraph();

    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> blocks{0};

    // Audio thread: render continuously, lock-free.
    std::thread render([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        p.Calculate(YSE::T_DSP);
        blocks.fetch_add(1, std::memory_order_relaxed);
      }
    });

    // Value threads: flood bang/int/float/list at the receiver-name pool. Names
    // are re-resolved against the pinned snapshot on the audio thread, so a
    // concurrently-deleted receiver simply drops the message.
    auto valueBody = [&](int seed) {
      std::mt19937 rng(static_cast<unsigned>(0xA5 + seed));
      while (!stop.load(std::memory_order_relaxed)) {
        const char* to = kReceiverNames[rng() % kReceiverCount];
        switch (rng() % 4) {
        case 0:
          p.PassBang(to, YSE::T_GUI);
          break;
        case 1:
          p.PassData(static_cast<int>(rng() % 1000), to, YSE::T_GUI);
          break;
        case 2:
          p.PassData(static_cast<float>(rng() % 1000) * 0.01f, to, YSE::T_GUI);
          break;
        case 3:
          p.PassData(std::string("v") + std::to_string(rng() % 100), to, YSE::T_GUI);
          break;
        default:
          break;
        }
      }
    };
    std::thread value0(valueBody, 0);
    std::thread value1(valueBody, 1);

    // Editor thread: the structural / file-handler churn. When it finishes the
    // stress window is over.
    std::thread editor([&] { runEditor(p, savedGraph, 4000); });

    editor.join();
    stop.store(true, std::memory_order_relaxed);
    value0.join();
    value1.join();
    render.join();

    // The audio thread kept running throughout.
    CHECK(blocks.load() > 0);

    // The patcher is still fully functional after the storm: a known graph
    // renders audio again.
    p.Clear();
    YSE::pHandle* n2 = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* d2 = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(n2, 0, d2, 0);
    p.Calculate(YSE::T_DSP);
    CHECK_FALSE(p.output[0].isSilent());

    // Retired state drains on the background pool rather than piling up until
    // teardown. Keep the epoch advancing so any snapshot inside the +2 grace
    // can cross it, then assert only a handful remain pending. The budget is
    // counted in deliveries of the suite's reference timer rather than in
    // milliseconds (issue #753), so it stretches with machine load exactly as
    // the background pool does.
    TestHelpers::pacedPump(
        2000, [&] { return p.PendingRetired() <= 4; },
        [&] {
          p.Connect(n2, 0, d2, 0);
          p.Calculate(YSE::T_DSP);
          p.Disconnect(n2, 0, d2, 0);
          p.Calculate(YSE::T_DSP);
          p.Calculate(YSE::T_DSP);
        });
    CHECK(p.PendingRetired() <= 4);
  }

  // Issue #937: the three enumeration calls walked `objects` without mtx, so a
  // host reading the patch on one thread (an editor redrawing it) while another
  // thread edited it iterated a std::map that was being inserted into and erased
  // from. The reader never dereferences what it gets back: a handle it is
  // handed can be deleted by the editor a moment later, which is the host's
  // lifetime contract and not this issue. What must hold is that the lookup
  // itself never walks a tree another thread is rebalancing — TSan reports that
  // as a data race on the map nodes, ASan as a use-after-free of an erased one.
  TEST_CASE("concurrency: enumerating objects while another thread edits") {
    LogSilencer silence;

    patcherImplementation p(1, nullptr);
    const std::string savedGraph = makeSavedGraph();

    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> reads{0};

    std::thread reader([&] {
      std::uint64_t found = 0;
      while (!stop.load(std::memory_order_relaxed)) {
        const unsigned int count = p.Objects();
        for (unsigned int i = 0; i <= count; i++) {
          if (p.GetHandleFromList(i) != nullptr) found++;
        }
        for (unsigned int id = 0; id < 8; id++) {
          if (p.GetHandleFromID(id) != nullptr) found++;
        }
        reads.fetch_add(1, std::memory_order_relaxed);
      }
      (void)found;
    });

    // Churn the object set through every path that inserts into or erases from
    // it: single create/delete, Clear, and a ParseJSON build.
    std::thread editor([&] {
      std::vector<YSE::pHandle*> live;
      for (int i = 0; i < 1500; ++i) {
        switch (i % 5) {
        case 0:
        case 1:
          live.push_back(p.CreateObject(YSE::OBJ::G_GATE, "2"));
          break;
        case 2:
          if (!live.empty()) {
            p.DeleteObject(live.back());
            live.pop_back();
          }
          break;
        case 3:
          if (i % 50 == 3) {
            p.Clear();
            live.clear();
            p.ParseJSON(savedGraph);
          }
          break;
        default:
          live.push_back(p.CreateObject(YSE::OBJ::G_RECEIVE, "a"));
          break;
        }
      }
    });

    editor.join();
    stop.store(true, std::memory_order_relaxed);
    reader.join();

    CHECK(reads.load() > 0);

    // The enumeration still answers correctly once the churn is over: every
    // listed object is found again by its own ID, and nothing past the end is.
    const unsigned int count = p.Objects();
    CHECK(count > 0u);
    for (unsigned int i = 0; i < count; i++) {
      YSE::pHandle* h = p.GetHandleFromList(i);
      REQUIRE(h != nullptr);
      CHECK(p.GetHandleFromID(h->GetID()) == h);
    }
    CHECK(p.GetHandleFromList(count) == nullptr);
  }

  // Issue #940: DumpState took each stateful object's store guard as a
  // try-lock and returned without writing when it lost, so a save that reached
  // a `.coll` while a message was inside it left the object's "state" key out
  // of the file — silently. A second thread keeps a message in the store almost
  // all the time; every one of the saves must still carry the contents.
  TEST_CASE("concurrency: saving a patch while a message is using a stateful object") {
    LogSilencer silence;

    patcherImplementation p(1, nullptr);
    YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL, "");
    REQUIRE(coll != nullptr);
    coll->SetListData(0, "1 60 100");

    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> writes{0};

    // Rewrites the same entry over and over, so the contents stay one known
    // entry while the store guard is held for most of the thread's time.
    std::thread writer([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        coll->SetListData(0, "1 60 100");
        writes.fetch_add(1, std::memory_order_relaxed);
      }
    });

    int missing = 0;
    for (int i = 0; i < 400; i++) {
      const std::string dump = p.DumpJSON();
      if (dump.find("\"state\"") == std::string::npos) missing++;
    }

    stop.store(true, std::memory_order_relaxed);
    writer.join();

    CHECK(writes.load() > 0);
    CHECK(missing == 0);
  }

  // Issue #962: a control-thread send made while a block was rendering read
  // the renderer's pinned snapshot (the pin was one per-patcher atomic, visible
  // to every thread), and could still be walking its fan-out when the
  // reclaimer freed it — the grace period counts the renderer's blocks, not
  // the sender's. TSan reports the reclaimer's `delete` racing the sender's
  // read in `outlet::resolveTargets` / `inlet::WaitingForDSP`. Here one thread
  // renders, one sends into a wired slider pair, and the test thread retires a
  // snapshot per edit on an unrelated pair, so graphs are freed all the time.
  // The edits never touch the pair being sent through: this is about the
  // snapshot's lifetime, not about the live wiring changing under a send
  // (that is #963).
  TEST_CASE("concurrency: control-thread sends during a render never read a retired snapshot") {
    LogSilencer silence;

    patcherImplementation p(1, nullptr);
    YSE::pHandle* from = p.CreateObject(YSE::OBJ::G_SLIDER, "");
    YSE::pHandle* to = p.CreateObject(YSE::OBJ::G_SLIDER, "");
    YSE::pHandle* keep = p.CreateObject(YSE::OBJ::G_SLIDER, "");
    YSE::pHandle* other = p.CreateObject(YSE::OBJ::G_SLIDER, "");
    REQUIRE(from != nullptr);
    REQUIRE(to != nullptr);
    REQUIRE(keep != nullptr);
    REQUIRE(other != nullptr);
    p.Connect(from, 0, to, 0);

    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> blocks{0};
    std::atomic<std::uint64_t> sends{0};

    std::thread render([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        p.Calculate(YSE::T_DSP);
        blocks.fetch_add(1, std::memory_order_relaxed);
      }
    });
    std::thread sender([&] {
      int i = 0;
      while (!stop.load(std::memory_order_relaxed)) {
        from->SetFloatData(0, static_cast<float>(i++ % 100) * 0.01f);
        sends.fetch_add(1, std::memory_order_relaxed);
      }
    });

    for (int i = 0; i < 2000; i++) {
      p.Connect(keep, 0, other, 0);
      p.Disconnect(keep, 0, other, 0);
    }

    stop.store(true, std::memory_order_relaxed);
    sender.join();
    render.join();

    CHECK(blocks.load() > 0);
    CHECK(sends.load() > 0);
    // The pair still carries a value once the storm is over.
    from->SetFloatData(0, 0.5f);
    CHECK(to->GetGuiValue() == from->GetGuiValue());
  }

  // Issue #963: a control-thread send walked the live wiring (`outlet::
  // connections`, `inlet::dspConnection`) while a structural edit on another
  // control thread rewrote it under mtx — DeleteObject's UnwireFromPeers swaps
  // the vectors out and frees them, Connect's push_back reallocates them. The
  // send takes no lock, so TSan reports the edit's write racing the send's read
  // in `outlet::SendFloat` / `inlet::WaitingForDSP`, and a send that loaded
  // the old buffer can index freed memory. No render thread: both sides are
  // control threads, so this is about the wiring, not a snapshot's lifetime
  // (#962). Here one thread fans a value out of `from` into six churned sliders
  // and a fixed one, while the test thread deletes, re-creates and re-wires
  // the six.
  TEST_CASE("concurrency: a control-thread send races no structural edit on another thread") {
    LogSilencer silence;

    patcherImplementation p(1, nullptr);
    YSE::pHandle* from = p.CreateObject(YSE::OBJ::G_SLIDER, "");
    YSE::pHandle* fixed = p.CreateObject(YSE::OBJ::G_SLIDER, "");
    REQUIRE(from != nullptr);
    REQUIRE(fixed != nullptr);
    p.Connect(from, 0, fixed, 0);

    constexpr int kChurned = 6;
    std::vector<YSE::pHandle*> churned;
    churned.reserve(kChurned);
    auto wireNew = [&] {
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SLIDER, "");
      p.Connect(from, 0, h, 0);
      p.Connect(h, 0, fixed, 0);
      return h;
    };
    for (int i = 0; i < kChurned; i++)
      churned.push_back(wireNew());

    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> sends{0};
    std::thread sender([&] {
      int i = 0;
      while (!stop.load(std::memory_order_relaxed)) {
        from->SetFloatData(0, static_cast<float>(i++ % 100) * 0.01f);
        sends.fetch_add(1, std::memory_order_relaxed);
      }
    });

    for (int round = 0; round < 300; round++) {
      for (int i = 0; i < kChurned; i++) {
        p.DeleteObject(churned[i]);
        churned[i] = wireNew();
      }
    }

    stop.store(true, std::memory_order_relaxed);
    sender.join();

    CHECK(sends.load() > 0);
    // Every cord still carries the value once the storm is over.
    from->SetFloatData(0, 0.5f);
    CHECK(fixed->GetGuiValue() == from->GetGuiValue());
    for (YSE::pHandle* h : churned)
      CHECK(h->GetGuiValue() == from->GetGuiValue());
  }

  // Issue #963, the reported shape: a `.preset` recall on one thread pushes
  // into sliders the test thread keeps deleting and re-creating. The recall's
  // objectPin (#961) keeps the objects allocated; what raced was the wiring
  // each recalled slider sends through, which DeleteObject unwires under mtx
  // while the slider's outlet is being walked.
  TEST_CASE("concurrency: a .preset recall races no delete of the sliders it recalls") {
    LogSilencer silence;

    patcherImplementation p(1, nullptr);
    YSE::pHandle* preset = p.CreateObject(YSE::OBJ::G_PRESET, "4");
    YSE::pHandle* kept = p.CreateObject(YSE::OBJ::G_SLIDER, "");
    REQUIRE(preset != nullptr);
    REQUIRE(kept != nullptr);

    constexpr int kChurned = 6;
    std::vector<YSE::pHandle*> churned;
    churned.reserve(kChurned);
    auto wireNew = [&] {
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SLIDER, "");
      p.Connect(h, 0, kept, 0);
      return h;
    };
    for (int i = 0; i < kChurned; i++) {
      churned.push_back(wireNew());
      churned.back()->SetFloatData(0, 0.25f);
    }
    preset->SetListData(0, "store 0");

    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> recalls{0};
    std::thread recaller([&] {
      int i = 0;
      while (!stop.load(std::memory_order_relaxed)) {
        if (++i % 8 == 0) {
          preset->SetListData(0, "store 1");
        } else {
          preset->SetIntData(0, 0);
        }
        recalls.fetch_add(1, std::memory_order_relaxed);
      }
    });

    for (int round = 0; round < 300; round++) {
      for (int i = 0; i < kChurned; i++) {
        p.DeleteObject(churned[i]);
        churned[i] = wireNew();
      }
    }

    stop.store(true, std::memory_order_relaxed);
    recaller.join();

    CHECK(recalls.load() > 0);
    // The patch is still whole: a slider pushes into the kept one.
    churned[0]->SetFloatData(0, 0.75f);
    CHECK(kept->GetGuiValue() == churned[0]->GetGuiValue());
  }

} // TEST_SUITE("patcher")
