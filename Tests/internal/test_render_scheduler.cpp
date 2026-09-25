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

} // namespace

TEST_SUITE("internal") {

  TEST_CASE("renderScheduler: every task runs once per block, after its dependencies (#859)") {
    for (int workers : {0, 1, 2, 4}) {
      INFO("workers: " << workers);
      renderScheduler s(workers);
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

    // -1 re-applies the auto-sizing rule, cap included.
    s.setWorkerCount(-1);
    CHECK(s.workerCount() >= 1);
    CHECK(s.workerCount() <= renderScheduler::MAX_AUTO_WORKERS);
    g.build(s);
    s.run(*g.root);

    CHECK(g.allRan(4));
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
