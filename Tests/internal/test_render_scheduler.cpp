// Tests for YSE::INTERNAL::renderScheduler — the task-graph render scheduler
// (issue #859, epic #856). These drive the scheduler directly with synthetic
// task graphs (no engine session): every task runs exactly once per block, a
// continuation runs only after all of its dependencies, the calling thread
// steals whatever the workers do not run, parked workers are woken for every
// block that needs them, and the graph can be rebuilt between blocks.
//
// The channel-level oracle — bit-identical mixes at 0/1/2/N workers — is the
// rendergolden suite (Tests/channel/test_render_golden.cpp).

#include <doctest/doctest.h>
#include "internal/renderScheduler.h"
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using YSE::INTERNAL::renderScheduler;
using YSE::INTERNAL::renderTask;

namespace {

  // A graph node that checks, when it runs, that every dependency has already
  // run in this block, then arrives at its successor.
  struct Node : renderTask {
    std::vector<Node*> deps;
    Node* successor = nullptr;
    std::atomic<int> runs{0};
    std::atomic<int>* violations = nullptr;
    std::atomic<std::thread::id> ranOn{};

    void execute(renderScheduler& s) override {
      // Runs before this one = this block's index; each dependency must have
      // completed this block already.
      const int block = runs.load(std::memory_order_relaxed);
      for (Node* d : deps)
        if (d->runs.load(std::memory_order_acquire) != block + 1) violations->fetch_add(1);
      ranOn.store(std::this_thread::get_id(), std::memory_order_relaxed);
      runs.fetch_add(1, std::memory_order_release);
      if (successor != nullptr) s.arrive(*successor);
    }
  };

  // A two-level channel-shaped graph: `groups` sub-roots, each fed by `leaves`
  // leaves, all feeding one root (plus one leaf directly under the root, like
  // a channel's own sounds).
  struct TreeGraph {
    std::vector<std::unique_ptr<Node>> nodes;
    Node* root = nullptr;
    std::atomic<int> violations{0};

    Node* make() {
      nodes.push_back(std::make_unique<Node>());
      nodes.back()->violations = &violations;
      return nodes.back().get();
    }

    TreeGraph(int groups, int leaves) {
      root = make();
      Node* own = make();
      own->successor = root;
      root->deps.push_back(own);
      for (int g = 0; g < groups; ++g) {
        Node* sub = make();
        sub->successor = root;
        root->deps.push_back(sub);
        for (int l = 0; l < leaves; ++l) {
          Node* leaf = make();
          leaf->successor = sub;
          sub->deps.push_back(leaf);
        }
      }
    }

    void build(renderScheduler& s) {
      s.beginBuild();
      for (auto& n : nodes) {
        if (n->deps.empty())
          s.addLeaf(*n);
        else
          s.setDependencies(*n, static_cast<Int>(n->deps.size()));
      }
      s.endBuild();
    }

    bool allRan(int blocks) const {
      for (const auto& n : nodes)
        if (n->runs.load() != blocks) return false;
      return true;
    }
  };

  // Poll `pred` until it holds or `timeout` elapses; returns the final value.
  template <typename Pred>
  bool waitFor(Pred pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
      if (std::chrono::steady_clock::now() > deadline) return pred();
      std::this_thread::yield();
    }
    return true;
  }

  // A leaf that holds its thread until `expected` leaves are inside at the
  // same time (or a timeout), proving that many threads are working the block.
  struct BarrierLeaf : renderTask {
    std::atomic<int>* inside = nullptr;
    std::atomic<int>* timeouts = nullptr;
    int expected = 0;
    renderTask* successor = nullptr;

    void execute(renderScheduler& s) override {
      inside->fetch_add(1, std::memory_order_acq_rel);
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      while (inside->load(std::memory_order_acquire) < expected) {
        if (std::chrono::steady_clock::now() > deadline) {
          timeouts->fetch_add(1);
          break;
        }
        std::this_thread::yield();
      }
      s.arrive(*successor);
    }
  };

  struct CountRoot : renderTask {
    std::atomic<int> runs{0};
    void execute(renderScheduler&) override {
      runs.fetch_add(1, std::memory_order_relaxed);
    }
  };

  void busyWait(std::chrono::microseconds d) {
    const auto until = std::chrono::steady_clock::now() + d;
    while (std::chrono::steady_clock::now() < until) {}
  }

  // A task that burns a fixed wall time, records the thread it ran on, and
  // arrives at its successor (if any).
  struct BusyTask : renderTask {
    std::chrono::microseconds work{0};
    renderTask* successor = nullptr;
    std::atomic<std::thread::id> ranOn{};
    void execute(renderScheduler& s) override {
      busyWait(work);
      ranOn.store(std::this_thread::get_id(), std::memory_order_relaxed);
      if (successor != nullptr) s.arrive(*successor);
    }
  };

} // namespace

TEST_SUITE("internal") {

  TEST_CASE("renderScheduler: every task runs once per block, after its dependencies (#859)") {
    for (int workers : {0, 1, 2, 4}) {
      INFO("workers: " << workers);
      renderScheduler s(workers);
      // These tasks are far cheaper than a wake, so the serial gate (#861)
      // would keep every block on the calling thread; this case is about the
      // parallel protocol.
      s.setSerialGating(false);
      CHECK(s.workerCount() == workers);
      TreeGraph g(6, 5);
      g.build(s);
      constexpr int kBlocks = 400;
      for (int b = 0; b < kBlocks; ++b)
        s.run(*g.root);
      CHECK(g.allRan(kBlocks)); // nothing lost, nothing run twice
      CHECK(g.violations.load() == 0); // no continuation ran early
    }
  }

  TEST_CASE("renderScheduler: with zero workers the calling thread runs the whole graph (#859)") {
    renderScheduler s(0);
    TreeGraph g(3, 4);
    g.build(s);
    for (int b = 0; b < 10; ++b)
      s.run(*g.root);
    CHECK(g.allRan(10));
    // Compared outside CHECK: doctest cannot stringify std::thread::id on
    // libstdc++.
    for (const auto& n : g.nodes) {
      const bool onCaller = n->ranOn.load() == std::this_thread::get_id();
      CHECK(onCaller);
    }
  }

  TEST_CASE("renderScheduler: the calling thread steals every leaf no worker takes (#859)") {
    // Leaves are dealt out to three worker lists, but the workers are shut
    // down: the only way a leaf on their lists can run is the calling thread
    // stealing it. Without stealing, run() would never see the root finish.
    renderScheduler s(3);
    s.shutdown();
    TreeGraph g(4, 3);
    g.build(s);
    for (int b = 0; b < 5; ++b)
      s.run(*g.root);
    CHECK(g.allRan(5));
    CHECK(g.violations.load() == 0);
    for (const auto& n : g.nodes) {
      const bool onCaller = n->ranOn.load() == std::this_thread::get_id();
      CHECK(onCaller);
    }
  }

  TEST_CASE("renderScheduler: parked workers are woken for every block that needs them (#859)") {
    // Four leaves, one per list (the calling thread + three workers), each
    // holding its thread until all four are running at once. A lost wake —
    // a worker sleeping through a block — leaves one leaf unclaimed by a
    // distinct thread and trips the barrier's timeout. The gap between blocks
    // sweeps across the workers' post-block spin window (50 us), so blocks
    // open in every phase of a worker's spin -> announce -> park sequence.
    constexpr int kWorkers = 3;
    renderScheduler s(kWorkers);
    s.setSerialGating(false); // every block must open to the workers
    std::atomic<int> inside{0};
    std::atomic<int> timeouts{0};
    CountRoot root;
    std::vector<std::unique_ptr<BarrierLeaf>> leaves;
    s.beginBuild();
    for (int i = 0; i <= kWorkers; ++i) {
      leaves.push_back(std::make_unique<BarrierLeaf>());
      leaves.back()->inside = &inside;
      leaves.back()->timeouts = &timeouts;
      leaves.back()->expected = kWorkers + 1;
      leaves.back()->successor = &root;
      s.addLeaf(*leaves.back());
    }
    s.setDependencies(root, kWorkers + 1);
    s.endBuild();

    constexpr int kBlocks = 300;
    for (int b = 0; b < kBlocks; ++b) {
      inside.store(0);
      s.run(root);
      const auto gap = (b % 10 == 0) ? std::chrono::nanoseconds(0)
                                     : std::chrono::nanoseconds(20000 + (b * 173) % 80000);
      const auto until = std::chrono::steady_clock::now() + gap;
      while (std::chrono::steady_clock::now() < until) {}
      if (timeouts.load() != 0) break; // one lost wake is enough to fail
    }
    CHECK(timeouts.load() == 0);
    CHECK(root.runs.load() > 0);
  }

  TEST_CASE("renderScheduler: idle workers park, shutdown wakes them, startup revives (#859)") {
    renderScheduler s(4);
    const bool allParked = waitFor([&] { return s.parkedWorkers() == 4; });
    CHECK(allParked);

    // A block wakes them and they go back to sleep afterwards.
    TreeGraph g(4, 2);
    g.build(s);
    s.run(*g.root);
    CHECK(g.allRan(1));
    const bool reParked = waitFor([&] { return s.parkedWorkers() == 4; });
    CHECK(reParked);

    // A parked worker blocks in the kernel with no timeout: shutdown() must
    // unpark it or joining the worker threads hangs.
    const auto t0 = std::chrono::steady_clock::now();
    s.shutdown();
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 1000);
    CHECK(s.parkedWorkers() == 0);

    s.startup();
    const bool revived = waitFor([&] { return s.parkedWorkers() == 4; });
    CHECK(revived);
    s.run(*g.root);
    CHECK(g.allRan(2));
  }

  TEST_CASE("renderScheduler: setWorkerCount re-sizes and marks the graph dirty (#859)") {
    renderScheduler s(2);
    TreeGraph g(5, 3);
    g.build(s);
    CHECK_FALSE(s.isDirty());
    s.run(*g.root);

    // Re-sizing re-deals the leaves, so the caller must rebuild first.
    s.setWorkerCount(0);
    CHECK(s.workerCount() == 0);
    CHECK(s.isDirty());
    g.build(s);
    s.run(*g.root);

    s.setWorkerCount(3);
    CHECK(s.workerCount() == 3);
    g.build(s);
    s.run(*g.root);

    CHECK(s.requestedWorkerCount() == 3);

    // -1 re-applies the auto-sizing rule (#861): physical cores - 1, capped.
    s.setWorkerCount(-1);
    CHECK(s.requestedWorkerCount() == -1);
    CHECK(s.workerCount() == renderScheduler::autoWorkerCount());
    CHECK(s.workerCount() >= 0);
    CHECK(s.workerCount() <= renderScheduler::MAX_AUTO_WORKERS);
    g.build(s);
    s.run(*g.root);

    // Any negative request is auto; an absurd one is clamped.
    s.setWorkerCount(-7);
    CHECK(s.requestedWorkerCount() == -1);
    CHECK(s.workerCount() == renderScheduler::autoWorkerCount());
    s.setWorkerCount(0);
    g.build(s);
    s.run(*g.root);

    CHECK(g.allRan(5));
    CHECK(g.violations.load() == 0);

    s.markDirty();
    CHECK(s.isDirty());
  }

  TEST_CASE("renderScheduler: a rebuilt graph replaces the old one between blocks (#859)") {
    renderScheduler s(2);
    TreeGraph first(3, 3);
    TreeGraph second(2, 6);
    first.build(s);
    for (int b = 0; b < 20; ++b)
      s.run(*first.root);
    second.build(s);
    for (int b = 0; b < 30; ++b)
      s.run(*second.root);
    CHECK(first.allRan(20)); // untouched after the rebuild
    CHECK(second.allRan(30));
    CHECK(first.violations.load() == 0);
    CHECK(second.violations.load() == 0);
  }

  TEST_CASE("renderScheduler: a block cheaper than a wake renders serially (#861)") {
    // Serial gating: in real-time cadence, a scene whose whole block costs
    // less than waking a worker is rendered by the calling thread alone, and
    // no worker is signalled. These nodes cost well under a microsecond; a
    // park/wake round trip costs microseconds at best, and the estimate
    // starts at INITIAL_WAKE_COST_NS.
    renderScheduler s(2);
    TreeGraph g(2, 2);
    g.build(s);
    // Pin "light": a sanitizer build can make even these nodes cost more than
    // this machine's measured wake. A 50 ms wake keeps them far below it (the
    // one real wake below averages in a quarter of its latency).
    s.setWakeCost(50.0e6f);

    // Back to back (offline rendering), even a light block opens: the workers
    // are still in their post-block spin and join for a cache miss.
    for (int b = 0; b < 64; ++b)
      s.run(*g.root);
    CHECK(g.allRan(64));
    CHECK(g.violations.load() == 0);
    CHECK(s.blockCost() > 0.f);
    CHECK(s.blockCost() < s.wakeCost());
    CHECK_FALSE(s.lastBlockSerial());

    // Real-time cadence: blocks far apart, every worker parked. Now the light
    // block is rendered by this thread alone and nobody is woken for it.
    const bool parked = waitFor([&s] { return s.parkedWorkers() == 2; });
    REQUIRE(parked);
    for (int b = 0; b < 32; ++b) {
      busyWait(std::chrono::microseconds(200)); // past the 50 us spin window
      s.run(*g.root);
      CHECK(s.lastBlockSerial());
    }
    CHECK(s.parkedWorkers() == 2);
    CHECK(g.allRan(96));
    CHECK(g.violations.load() == 0);
    // The last block ran entirely on this thread.
    for (const auto& n : g.nodes) {
      const bool onCaller = n->ranOn.load() == std::this_thread::get_id();
      CHECK(onCaller);
    }

    // Gating off, the same block opens to the workers.
    s.setSerialGating(false);
    s.run(*g.root);
    CHECK_FALSE(s.lastBlockSerial());
    CHECK(g.allRan(97));
  }

  TEST_CASE("renderScheduler: a block heavier than a wake fans out to the workers (#861)") {
    // Four 300 us leaves (1.2 ms of work) under one root: above the pinned
    // 100 us wake cost, so the gate keeps opening blocks and the workers take
    // leaves.
    renderScheduler s(3);
    s.setWakeCost(100000.f);
    CountRoot root;
    std::vector<std::unique_ptr<BusyTask>> leaves;
    s.beginBuild();
    for (int i = 0; i < 4; ++i) {
      leaves.push_back(std::make_unique<BusyTask>());
      leaves.back()->work = std::chrono::microseconds(300);
      leaves.back()->successor = &root;
      s.addLeaf(*leaves.back());
    }
    s.setDependencies(root, 4);
    s.endBuild();

    bool sawWorker = false;
    for (int b = 0; b < 24; ++b) {
      s.run(root);
      for (const auto& l : leaves)
        if (l->ranOn.load() != std::this_thread::get_id()) sawWorker = true;
    }
    CHECK(root.runs.load() == 24);
    CHECK(s.blockCost() > 1000000.f * 0.9f);
    CHECK(s.blockCost() > s.wakeCost());
    CHECK_FALSE(s.lastBlockSerial());
    CHECK(sawWorker);
  }

  TEST_CASE("renderScheduler: a task's cost is its own time, not its continuations' (#861)") {
    // leaf (200 us) -> root (60 us), run by the calling thread. The root runs
    // inline inside the leaf's arrive(), so the leaf's elapsed time includes
    // it; the measured cost must not.
    renderScheduler s(0);
    BusyTask root;
    root.work = std::chrono::microseconds(60);
    BusyTask leaf;
    leaf.work = std::chrono::microseconds(200);
    leaf.successor = &root;
    s.beginBuild();
    s.addLeaf(leaf);
    s.setDependencies(root, 1);
    s.endBuild();
    CHECK(leaf.cost() == 0.f); // never timed yet
    for (int b = 0; b < 4 * static_cast<int>(renderScheduler::COST_SAMPLE_PERIOD); ++b)
      s.run(root);
    INFO("leaf cost ns: " << leaf.cost() << ", root cost ns: " << root.cost());
    CHECK(leaf.cost() >= 190000.f);
    CHECK(leaf.cost() < 245000.f); // 260 us if the root were charged to it
    CHECK(root.cost() >= 55000.f);
    CHECK(root.cost() < 150000.f);
    // The block estimate is the sum of both.
    CHECK(s.blockCost() >= 250000.f);
  }

  TEST_CASE("renderScheduler: leaves are dealt by measured cost (#861)") {
    // Two lists (calling thread + one worker). Round-robin would put both
    // expensive leaves on list 0; the cost-driven deal balances them.
    renderScheduler s(1);
    CountRoot root;
    std::array<BusyTask, 4> leaves;
    const float costs[] = {1000.f, 1000.f, 10.f, 10.f};
    for (std::size_t i = 0; i < leaves.size(); ++i) {
      leaves[i].successor = &root;
      leaves[i].setCost(costs[i]);
    }
    s.beginBuild();
    for (auto& l : leaves)
      s.addLeaf(l);
    s.setDependencies(root, 4);
    s.endBuild();
    CHECK(s.assignedCost(0) == doctest::Approx(1010.f));
    CHECK(s.assignedCost(1) == doctest::Approx(1010.f));
    CHECK_FALSE(s.isDirty()); // every leaf was measured: no rebalance pending
    s.run(root);
    CHECK_FALSE(s.isDirty());

    // Unmeasured leaves weigh 1 ns each: round-robin, and one rebuild is asked
    // for once the first sample has measured them.
    for (auto& l : leaves)
      l.setCost(0.f);
    s.beginBuild();
    for (auto& l : leaves)
      s.addLeaf(l);
    s.setDependencies(root, 4);
    s.endBuild();
    CHECK(s.assignedCost(0) == doctest::Approx(2.f));
    CHECK(s.assignedCost(1) == doctest::Approx(2.f));
    s.run(root); // a fresh build is always sampled
    CHECK(s.isDirty());
    for (const auto& l : leaves)
      CHECK(l.cost() > 0.f);
    CHECK(root.runs.load() == 2);
  }

  TEST_CASE("renderScheduler: auto worker count is physical cores - 1, capped (#861)") {
    const Int n = renderScheduler::autoWorkerCount();
    CHECK(n >= 0);
    CHECK(n <= renderScheduler::MAX_AUTO_WORKERS);
    const auto logical = static_cast<Int>(std::thread::hardware_concurrency());
    if (logical > 0) CHECK(n < logical); // never more workers than logical CPUs - 1
    renderScheduler s(-1);
    CHECK(s.workerCount() == n);
    CHECK(s.requestedWorkerCount() == -1);
    renderScheduler big(1000);
    CHECK(big.workerCount() == renderScheduler::MAX_WORKERS);
  }

  TEST_CASE("renderScheduler: workers are placed on performance cores first (#862)") {
    using YSE::INTERNAL::cpuTopology;
    const cpuTopology& topo = cpuTopology::machine();
    const auto order = topo.placementOrder();
    // One more worker than there are cores, so the plan wraps around.
    const Int count = static_cast<Int>(order.size()) + 1;
    renderScheduler s(count > renderScheduler::MAX_WORKERS ? renderScheduler::MAX_WORKERS : count);
    const Int n = s.workerCount();
    for (Int i = 1; i <= n; ++i) {
      if (order.empty()) {
        CHECK(s.workerCore(i) == nullptr);
      } else {
        // Worker i takes entry i % size: entry 0 — the first performance
        // core — is the calling thread's until every other core has a worker.
        CHECK(s.workerCore(i) == order[static_cast<std::size_t>(i) % order.size()]);
      }
    }
    CHECK(s.workerCore(0) == nullptr);
    CHECK(s.workerCore(n + 1) == nullptr);

    // Each worker tries its hint first thing on its own thread.
    const bool settled = waitFor([&s] { return s.placementHintsPending() == 0; });
    REQUIRE(settled);
    const Int accepted = s.placementHintsAccepted();
    INFO(s.describePlacement());
    CHECK(s.describePlacement().rfind("render workers: " + std::to_string(n) + " (requested)", 0) ==
          0);
#if defined(_WIN32)
    // An ideal processor within the process's own affinity is always
    // accepted.
    if (!order.empty()) CHECK(accepted == n);
#elif defined(__linux__)
    // Uniform machine: no hint (a single-core mask would be a hard pin).
    // Hybrid: every worker on a performance core narrows itself to that
    // cluster; the wrapped-around ones on efficiency cores are left alone.
    if (!topo.hybrid) {
      CHECK(accepted == 0);
    } else {
      CHECK(accepted <= topo.performanceCores());
    }
#else
    CHECK(accepted == 0);
#endif

    // Performance cores fill first: a worker lands on an efficiency core only
    // once every performance core but the calling thread's has one, and the
    // auto count gives every worker a physical core of its own.
    renderScheduler a(-1);
    const Int perf = topo.performanceCores();
    for (Int i = 1; i <= a.workerCount(); ++i) {
      const auto* c = a.workerCore(i);
      if (c == nullptr) continue;
      CHECK(c->efficient == (i >= perf));
      for (Int j = 1; j < i; ++j)
        CHECK(a.workerCore(j) != c);
    }

    // A re-size re-plans; the blocks still run.
    TreeGraph g(2, 3);
    s.setWorkerCount(2);
    g.build(s);
    for (int b = 0; b < 8; ++b)
      s.run(*g.root);
    CHECK(g.allRan(8));
    CHECK(g.violations.load() == 0);
    if (!order.empty()) CHECK(s.workerCore(1) == order[1 % order.size()]);
  }

  TEST_CASE("renderScheduler: a graph without leaves is refused, not spun on (#859)") {
    renderScheduler s(1);
    CountRoot root;
    s.beginBuild();
    s.setDependencies(root, 1); // nothing will ever arrive
    s.endBuild();
    s.run(root); // must return
    CHECK(root.runs.load() == 0);
  }

} // TEST_SUITE
