// End-to-end check of render worker sizing and placement (issue #862, epic
// #856): an engine session started with the auto thread count spawns one
// worker per usable physical core but one, fills the performance cores first,
// logs the count and each worker's core at init, and renders with the placed
// workers.
//
// ISOLATION: part of the "lifecycle" suite (its own ctest process, excluded
// from yse_unit_tests) because it drives System::close()/initOffline() and
// changes the process-global log level and handler.

#include <doctest/doctest.h>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "channel/channelInterface.hpp"
#include "internal/cpuTopology.h"
#include "internal/global.h"
#include "internal/renderScheduler.h"

namespace {

  class RecordingHandler : public YSE::logHandler {
  public:
    void AddMessage(const std::string& message) override {
      messages.push_back(message);
    }
    std::string find(const std::string& needle) const {
      for (const auto& m : messages)
        if (m.find(needle) != std::string::npos) return m;
      return {};
    }
    std::vector<std::string> messages;
  };

} // namespace

TEST_SUITE("lifecycle") {

  TEST_CASE("lifecycle: an auto-sized session places its render workers and logs it (#862)") {
    using YSE::INTERNAL::cpuTopology;
    using YSE::INTERNAL::renderScheduler;
    YSE::System().close();

    RecordingHandler handler;
    const YSE::ERROR_LEVEL previousLevel = YSE::Log().getLevel();
    const int previousThreads = YSE::System().renderThreads();
    YSE::Log().setLevel(YSE::EL_DEBUG);
    YSE::Log().setHandler(&handler);
    YSE::System().renderThreads(-1);

    const bool up = YSE::System().initOffline();
    // Detach the handler before any REQUIRE can leave it dangling.
    YSE::Log().setHandler(nullptr);
    YSE::Log().setLevel(previousLevel);
    if (!up) {
      YSE::System().renderThreads(previousThreads);
      return; // no offline device on this host
    }

    const Int workers = renderScheduler::autoWorkerCount();
    renderScheduler& render = YSE::INTERNAL::Global().renderer();
    CHECK(render.workerCount() == workers);

    // The init log line names the count and every worker's core.
    const std::string line = handler.find("render workers: ");
    INFO("log line: " << line);
    REQUIRE_FALSE(line.empty());
    CHECK(line.find("render workers: " + std::to_string(workers) + " (auto)") != std::string::npos);
    const cpuTopology& topo = cpuTopology::machine();
    if (!topo.cores.empty()) {
      CHECK(line.find(std::to_string(topo.cores.size()) + " physical cores") != std::string::npos);
      if (topo.hybrid) CHECK(line.find("hybrid") != std::string::npos);
    }
    for (Int i = 1; i <= workers; ++i) {
      const cpuTopology::core* c = render.workerCore(i);
      if (c == nullptr) continue;
      CHECK(line.find(cpuTopology::describe(*c)) != std::string::npos);
      // Performance cores first, each worker on a core of its own, none on
      // entry 0 (the calling thread's) with the default count.
      CHECK(c->efficient == (i >= topo.performanceCores()));
      CHECK(c != topo.placementOrder().front());
      for (Int j = 1; j < i; ++j)
        CHECK(render.workerCore(j) != c);
    }

    // Each worker applied its hint before its first block; on Windows the
    // ideal processor is always accepted.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (render.placementHintsPending() != 0 && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(render.placementHintsPending() == 0);
#if defined(_WIN32)
    if (!topo.cores.empty()) CHECK(render.placementHintsAccepted() == workers);
#endif

    // The placed workers render: a small scene, gating off so the blocks
    // really open to them.
    render.setSerialGating(false);
    std::vector<YSE::channel> channels(4);
    for (std::size_t i = 0; i < channels.size(); ++i)
      channels[i].create(("placement" + std::to_string(i)).c_str(), YSE::ChannelMaster());
    YSE::System().renderOffline(64);
    CHECK(std::isfinite(YSE::ChannelMaster().getPeakLinearPost()));
    render.setSerialGating(true);

    channels.clear();
    YSE::System().close();
    YSE::System().renderThreads(previousThreads);
  }

} // TEST_SUITE("lifecycle")
