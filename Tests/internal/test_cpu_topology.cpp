// Tests for YSE::INTERNAL::cpuTopology — processor topology for render worker
// sizing and placement (issue #862, epic #856).
//
// The classification rule and the sysfs parser are driven with synthetic
// machines (a fake sysfs tree in a temp directory for the Linux/Android
// parser, so it runs on every host); the live machine is only checked for
// consistency, since its shape depends on the runner.

#include <doctest/doctest.h>
#include "internal/cpuTopology.h"
#include "internal/renderScheduler.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using YSE::INTERNAL::cpuTopology;
using YSE::INTERNAL::renderScheduler;

namespace {

  // A machine of `perf` cores at capacity `hi` followed by `eff` at `lo`.
  cpuTopology machineOf(int perf, double hi, int eff, double lo) {
    cpuTopology t;
    Int cpu = 0;
    for (int i = 0; i < perf + eff; ++i) {
      cpuTopology::core c;
      c.cpus = {cpu};
      ++cpu;
      c.capacity = i < perf ? hi : lo;
      t.cores.push_back(c);
    }
    t.classify();
    return t;
  }

  // A throwaway sysfs CPU tree. Each CPU gets topology/core_cpus_list (or the
  // older thread_siblings_list) and optionally cpu_capacity or
  // cpufreq/cpuinfo_max_freq.
  struct FakeSysfs {
    std::filesystem::path root;

    FakeSysfs() {
      static std::atomic<int> counter{0};
      root = std::filesystem::temp_directory_path() /
             ("yse_fake_sysfs_" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
              std::to_string(counter.fetch_add(1)));
      std::filesystem::create_directories(root);
    }
    ~FakeSysfs() {
      std::error_code ec;
      std::filesystem::remove_all(root, ec);
    }
    FakeSysfs(const FakeSysfs&) = delete;
    FakeSysfs& operator=(const FakeSysfs&) = delete;

    void write(const std::string& relative, const std::string& text) const {
      const std::filesystem::path p = root / relative;
      std::filesystem::create_directories(p.parent_path());
      std::ofstream(p) << text << "\n";
    }
    void cpu(int id, const std::string& siblings, const char* capacityFile = nullptr,
             const std::string& capacity = "", bool oldName = false) const {
      const std::string base = "cpu" + std::to_string(id);
      write(base + (oldName ? "/topology/thread_siblings_list" : "/topology/core_cpus_list"),
            siblings);
      if (capacityFile != nullptr) write(base + "/" + capacityFile, capacity);
    }
    std::string path() const {
      return root.string();
    }
  };

} // namespace

TEST_SUITE("internal") {

  TEST_CASE("cpuTopology: parseCpuList reads sysfs CPU lists (#862)") {
    CHECK(cpuTopology::parseCpuList("0-3,8,10-11") == std::vector<Int>{0, 1, 2, 3, 8, 10, 11});
    CHECK(cpuTopology::parseCpuList("5") == std::vector<Int>{5});
    CHECK(cpuTopology::parseCpuList("3,1,1") == std::vector<Int>{1, 3}); // sorted, unique
    CHECK(cpuTopology::parseCpuList("").empty());
    CHECK(cpuTopology::parseCpuList("x,2,4-1,7-") == std::vector<Int>{2});
  }

  TEST_CASE("cpuTopology: a uniform machine is not hybrid (#862)") {
    const cpuTopology t = machineOf(8, 1024.0, 0, 0.0);
    CHECK_FALSE(t.hybrid);
    CHECK(t.performanceCores() == 8);

    // Intel favoured cores (Turbo Boost Max 3.0): a few hundred MHz apart is
    // one class, not a hybrid part.
    const cpuTopology tbmt = machineOf(2, 5300000.0, 6, 5100000.0);
    CHECK_FALSE(tbmt.hybrid);
    CHECK(tbmt.performanceCores() == 8);

    // Any unknown capacity: assume uniform rather than guess.
    cpuTopology unknown = machineOf(4, 2.0, 4, 1.0);
    REQUIRE(unknown.hybrid);
    unknown.cores[5].capacity = 0.0;
    unknown.classify();
    CHECK_FALSE(unknown.hybrid);
    CHECK(unknown.performanceCores() == 8);
  }

  TEST_CASE("cpuTopology: hybrid parts keep only the performance cores (#862)") {
    // Windows EfficiencyClass 1/0 (+1): the #812 bench machine, 4 Zen 5 + 8
    // Zen 5c.
    const cpuTopology zen = machineOf(4, 2.0, 8, 1.0);
    CHECK(zen.hybrid);
    CHECK(zen.performanceCores() == 4);
    for (std::size_t i = 0; i < zen.cores.size(); ++i)
      CHECK(zen.cores[i].efficient == (i >= 4));

    // Linux cpufreq max on the same part: 5.1 GHz vs 3.3 GHz.
    const cpuTopology zenFreq = machineOf(4, 5100000.0, 8, 3300000.0);
    CHECK(zenFreq.hybrid);
    CHECK(zenFreq.performanceCores() == 4);

    // A three-cluster phone (prime + big + little): only the little cluster
    // is "efficiency".
    cpuTopology phone = machineOf(1, 1024.0, 3, 870.0);
    for (int i = 0; i < 4; ++i) {
      cpuTopology::core c;
      c.cpus = {static_cast<Int>(4 + i)};
      c.capacity = 400.0;
      phone.cores.push_back(c);
    }
    phone.classify();
    CHECK(phone.hybrid);
    CHECK(phone.performanceCores() == 4);

    // Placement order: performance cores first, then efficiency cores.
    const auto order = zen.placementOrder();
    REQUIRE(order.size() == 12);
    for (std::size_t i = 0; i < order.size(); ++i)
      CHECK(order[i]->efficient == (i >= 4));
  }

  TEST_CASE("cpuTopology: the auto worker count is usable physical cores - 1 (#862)") {
    // Hybrid: efficiency cores count too — the heavy scenes keep scaling onto
    // them; placement, not the count, prefers the performance cores.
    CHECK(renderScheduler::autoWorkerCount(machineOf(2, 2.0, 2, 1.0)) == 3);
    CHECK(renderScheduler::autoWorkerCount(machineOf(4, 2.0, 8, 1.0)) ==
          renderScheduler::MAX_AUTO_WORKERS);
    // Uniform: physical cores - 1, capped.
    CHECK(renderScheduler::autoWorkerCount(machineOf(4, 1.0, 0, 0.0)) == 3);
    CHECK(renderScheduler::autoWorkerCount(machineOf(16, 1.0, 0, 0.0)) ==
          renderScheduler::MAX_AUTO_WORKERS);
    CHECK(renderScheduler::autoWorkerCount(machineOf(1, 1.0, 0, 0.0)) == 0);
    // Unknown topology: the #861 fallback on logical CPUs.
    Int logical = static_cast<Int>(std::thread::hardware_concurrency());
    Int expected = logical - 1;
    if (expected > renderScheduler::MAX_AUTO_WORKERS) expected = renderScheduler::MAX_AUTO_WORKERS;
    if (expected < 0) expected = 0;
    CHECK(renderScheduler::autoWorkerCount(cpuTopology()) == expected);
    // The live rule is the same function on the live machine.
    CHECK(renderScheduler::autoWorkerCount() ==
          renderScheduler::autoWorkerCount(cpuTopology::machine()));
  }

  TEST_CASE("cpuTopology: sysfs parser merges SMT siblings and reads capacity (#862)") {
    // x86 hybrid: 2 SMT performance cores (cpus 0/1, 2/3) and 4 single-thread
    // efficiency cores (4..7), capacity from cpufreq only.
    FakeSysfs fs;
    fs.write("possible", "0-8");
    for (int c = 0; c < 4; ++c)
      fs.cpu(c, c < 2 ? "0-1" : "2-3", "cpufreq/cpuinfo_max_freq", "5100000");
    for (int c = 4; c < 8; ++c)
      fs.cpu(c, std::to_string(c), "cpufreq/cpuinfo_max_freq", "3300000", /*oldName=*/true);
    // cpu8 is possible but offline: no topology directory.

    const cpuTopology t = cpuTopology::fromSysfs(fs.path(), {});
    REQUIRE(t.cores.size() == 6);
    CHECK(t.cores[0].cpus == std::vector<Int>{0, 1});
    CHECK(t.cores[1].cpus == std::vector<Int>{2, 3});
    CHECK(t.cores[2].cpus == std::vector<Int>{4});
    CHECK(t.hybrid);
    CHECK(t.performanceCores() == 2);
    CHECK_FALSE(t.cores[0].efficient);
    CHECK(t.cores[5].efficient);
    // 8 logical CPUs, 6 physical cores: 5 workers, not 7.
    CHECK(renderScheduler::autoWorkerCount(t) == 5);

    // The process affinity mask drops CPUs (a container's cpuset): without
    // cpu 0 the first core keeps its other sibling; without 4..5 two
    // efficiency cores disappear.
    std::vector<bool> allowed(16, true);
    allowed[0] = false;
    allowed[4] = false;
    allowed[5] = false;
    const cpuTopology masked = cpuTopology::fromSysfs(fs.path(), allowed);
    REQUIRE(masked.cores.size() == 4);
    CHECK(masked.cores[0].cpus == std::vector<Int>{1});
    CHECK(masked.performanceCores() == 2);
    CHECK(renderScheduler::autoWorkerCount(masked) == 3);
  }

  TEST_CASE("cpuTopology: sysfs parser on an arm64 big.LITTLE layout (#862)") {
    // cpu_capacity wins over cpufreq; no "possible" file (scan until the
    // limit, skipping gaps); every CPU its own core.
    FakeSysfs fs;
    for (int c = 0; c < 8; ++c) {
      const std::string cap = c < 4 ? "380" : (c < 7 ? "870" : "1024");
      fs.cpu(c, std::to_string(c), "cpu_capacity", cap);
      fs.write("cpu" + std::to_string(c) + "/cpufreq/cpuinfo_max_freq", "1800000");
    }
    const cpuTopology t = cpuTopology::fromSysfs(fs.path(), {});
    REQUIRE(t.cores.size() == 8);
    CHECK(t.hybrid);
    CHECK(t.performanceCores() == 4); // 3 big + 1 prime
    CHECK(renderScheduler::autoWorkerCount(t) == 7);
    // The little cores have the lowest ids here: placement must still start
    // with the big ones, so worker 1 lands on a big core, not cpu1.
    const auto order = t.placementOrder();
    REQUIRE(order.size() == 8);
    CHECK(order[0]->cpus.front() == 4);
    CHECK(order[1]->cpus.front() == 5);
    CHECK_FALSE(order[3]->efficient);
    CHECK(order[4]->cpus.front() == 0);
    CHECK(order[4]->efficient);

    // A uniform part with no capacity information at all.
    FakeSysfs flat;
    flat.write("possible", "0-3");
    for (int c = 0; c < 4; ++c)
      flat.cpu(c, std::to_string(c));
    const cpuTopology u = cpuTopology::fromSysfs(flat.path(), {});
    CHECK(u.cores.size() == 4);
    CHECK_FALSE(u.hybrid);
    CHECK(u.performanceCores() == 4);

    // Not a sysfs tree at all: empty, and the fallback applies.
    const cpuTopology none = cpuTopology::fromSysfs(flat.path() + "/missing", {});
    CHECK(none.cores.empty());
  }

  TEST_CASE("cpuTopology: the live machine is consistent (#862)") {
    const cpuTopology& t = cpuTopology::machine();
    MESSAGE("cores: " << t.cores.size() << ", performance: " << t.performanceCores() << ", hybrid: "
                      << t.hybrid << ", auto workers: " << renderScheduler::autoWorkerCount());
#if defined(_WIN32) || defined(__linux__)
    // Both platforms report their topology to an unprivileged process.
    CHECK_FALSE(t.cores.empty());
#endif
    const auto logical = static_cast<Int>(std::thread::hardware_concurrency());
    Int cpus = 0;
    for (const auto& c : t.cores) {
      CHECK_FALSE(c.cpus.empty());
      cpus += static_cast<Int>(c.cpus.size());
    }
    if (logical > 0) CHECK(cpus <= logical);
    CHECK(t.performanceCores() >= (t.cores.empty() ? 0 : 1));
  }

} // TEST_SUITE
