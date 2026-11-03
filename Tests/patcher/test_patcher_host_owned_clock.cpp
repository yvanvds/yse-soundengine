// Tests for host-owned domain clocks, seen from inside a patch (issue #1254).
//
// A host that publishes a clock for patches to follow — Phi's `domain.drum` —
// marks it with `system::setClockHostOwned`. The claim under test is the one a
// patch author meets:
//
//   - the three writers (`.transport`, `.setclock`, `.tempo`) cannot change its
//     tempo, on either of their routes — inline by name off the audio callback,
//     and through the bridge's wait-free write when a message is delivered
//     from inside `Calculate`;
//   - each refusal is reported once in the engine log, naming the clock;
//   - the patch can still read the clock and count on it;
//   - the host's own `setTempo` keeps working.
//
// Everything here runs against a real `patcherImplementation`, real objects
// created by name, and a real `CLOCK::domainClock` advancing underneath.
//
// Registered in the `clock` suite, not `patcher`: the cases drive
// CLOCK::Manager().update() directly on the test thread, which would race a
// live audio thread from another suite in the shared monolithic process. No
// audio device required.

#include <doctest/doctest.h>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "clock/clockManager.h"
#include "internal/rtLogQueue.h"
#include "log.hpp"
#include "patcher/inlet.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/time/clockBridge.h"
#include "patcher/time/messageScheduler.h"
#include "patcher/sinks.hpp"
#include "system.hpp"

namespace {

  using YSE::PATCHER::patcherImplementation;

  constexpr float kTickSeconds = 0.25f;

  // One block in the engine's own order: advance every clock, then render.
  void Tick(patcherImplementation& p) {
    YSE::CLOCK::Manager().update(kTickSeconds);
    p.Calculate(YSE::T_DSP);
  }

  // The engine log as a host sees it: a real handler installed through the
  // public API, with the level raised so `E_WARNING` lines are not filtered.
  // Locked because pool threads in the shared process may log while it is
  // installed.
  struct LogCapture : YSE::logHandler {
    YSE::ERROR_LEVEL previous;

    LogCapture() : previous(YSE::Log().getLevel()) {
      // Whatever is still queued belongs to an earlier test.
      YSE::INTERNAL::RtLog().drain();
      YSE::Log().setLevel(YSE::EL_WARNING);
      YSE::Log().setHandler(this);
    }
    ~LogCapture() override {
      YSE::Log().setHandler(nullptr);
      YSE::Log().setLevel(previous);
    }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;
    LogCapture(LogCapture&&) = delete;
    LogCapture& operator=(LogCapture&&) = delete;

    void AddMessage(const std::string& message) override {
      const std::lock_guard<std::mutex> lock(mutex);
      lines.push_back(message);
    }

    // What `system::update()` does once per tick for lines posted from the
    // audio callback.
    void Drain() {
      YSE::INTERNAL::RtLog().drain();
    }

    std::size_t Count(const std::string& needle) const {
      const std::lock_guard<std::mutex> lock(mutex);
      std::size_t found = 0;
      for (const std::string& line : lines) {
        if (line.find(needle) != std::string::npos) found++;
      }
      return found;
    }

  private:
    mutable std::mutex mutex;
    std::vector<std::string> lines;
  };

  // The line every refusal logs, for an object and a clock.
  std::string Refusal(const char* object, const std::string& clock) {
    return std::string("patcher: ") + object + ": clock '" + clock + "' is owned by the host";
  }

  // Records every number it is sent: floats from `.transport` / `.setclock`,
  // ints from `.tempo`.
  struct Recorder : TestHelpers::SinkBase {
    std::vector<float> seen;

    Recorder() : SinkBase(false) {
      seen.reserve(64);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) { seen.push_back(v); });
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { seen.push_back(static_cast<float>(v)); });
    }
    const char* Type() const override {
      return "host_owned_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // One object in a real patcher, its outlets into one recorder.
  struct Rig {
    Recorder out;
    patcherImplementation patcher{1, nullptr};
    YSE::pHandle outHandle{&out};
    YSE::pHandle* obj = nullptr;

    Rig(const char* type, const std::string& args) {
      obj = patcher.CreateObject(type, args);
      REQUIRE(obj != nullptr);
      for (int i = 0; i < obj->GetOutputs(); i++)
        patcher.Connect(obj, i, &outHandle, 0);
      patcher.Clocks()->WaitIdle();
    }

    void Tick() {
      ::Tick(patcher);
    }
  };

  // A clock the host made and claimed, already running.
  void MakeHostClock(const std::string& name, float bpm) {
    REQUIRE(YSE::System().createClock(name, bpm));
    REQUIRE(YSE::System().setClockHostOwned(name, true));
  }

  void DropClock(const std::string& name) {
    YSE::CLOCK::Manager().destroyClock(name);
    YSE::CLOCK::Manager().update(0.01f);
  }

} // namespace

TEST_SUITE("clock") {

  TEST_CASE("host-owned clock: '.transport stop' leaves the tempo and logs a warning (#1254)") {
    // The bug the issue reports: a `.transport domain.drum` with `stop` set the
    // host's clock to 0 and stopped every object on it while the host's domain
    // played on.
    auto& mgr = YSE::CLOCK::Manager();
    LogCapture log;
    MakeHostClock("ho.tr", 120.f);

    Rig rig(YSE::OBJ::G_TRANSPORT, "ho.tr 90");
    rig.Tick();
    REQUIRE(mgr.currentTempo("ho.tr") == doctest::Approx(120.f));

    rig.obj->SetListData(0, "stop");
    rig.Tick();
    CHECK(mgr.currentTempo("ho.tr") == doctest::Approx(120.f));
    CHECK(log.Count(Refusal(".transport", "ho.tr")) == 1);

    // Start, a tempo message and the cold inlet are all refused too — and the
    // warning is one line for the run, not one per write.
    rig.obj->SetListData(0, "start");
    rig.obj->SetListData(0, "tempo 60");
    rig.obj->SetFloatData(1, 200.f);
    rig.Tick();
    CHECK(mgr.currentTempo("ho.tr") == doctest::Approx(120.f));
    CHECK(log.Count(Refusal(".transport", "ho.tr")) == 1);

    // The patch can still read it: bang reports the host's tempo and the beat
    // the clock has really reached (three quarter-second blocks at 120 BPM).
    rig.out.seen.clear();
    rig.obj->SetBang(0);
    REQUIRE(rig.out.seen.size() == 2);
    CHECK(rig.out.seen[0] == doctest::Approx(120.f));
    CHECK(rig.out.seen[1] == doctest::Approx(1.5f));

    // The host still drives it.
    YSE::System().setTempo("ho.tr", 100.f);
    rig.Tick();
    CHECK(mgr.currentTempo("ho.tr") == doctest::Approx(100.f));

    // Given back to everyone, the transport's lever works again.
    REQUIRE(YSE::System().setClockHostOwned("ho.tr", false));
    rig.obj->SetListData(0, "stop");
    rig.Tick();
    CHECK(mgr.currentTempo("ho.tr") == doctest::Approx(0.f));

    DropClock("ho.tr");
  }

  TEST_CASE("host-owned clock: a '.transport stop' delivered from inside Calculate is refused "
            "too (#1254)") {
    // The audio-callback route: `.delay` -> `.i 0` -> `.transport` puts the
    // stop on the audio thread, where the write goes through the bridge's
    // wait-free path and the warning through the real-time log queue.
    auto& mgr = YSE::CLOCK::Manager();
    LogCapture log;
    MakeHostClock("ho.rt", 120.f);

    Rig rig(YSE::OBJ::G_TRANSPORT, "ho.rt 120");
    YSE::pHandle* del = rig.patcher.CreateObject(YSE::OBJ::G_DELAY, "");
    YSE::pHandle* zero = rig.patcher.CreateObject(YSE::OBJ::G_INT, "0");
    REQUIRE(del != nullptr);
    REQUIRE(zero != nullptr);
    rig.patcher.Connect(del, 0, zero, 0);
    rig.patcher.Connect(zero, 0, rig.obj, 0);
    rig.patcher.Clocks()->WaitIdle();
    REQUIRE(rig.patcher.Clocks()->Resolved(1));

    del->SetBang(0);
    const std::uint64_t due = YSE::PATCHER::messageScheduler::BlocksForMillis(5);
    for (std::uint64_t block = 0; block <= due; ++block)
      rig.Tick();
    rig.Tick();

    CHECK(mgr.currentTempo("ho.rt") == doctest::Approx(120.f));
    // Posted from the audio callback, so it reaches the log at the next drain.
    log.Drain();
    CHECK(log.Count(Refusal(".transport", "ho.rt")) == 1);

    DropClock("ho.rt");
  }

  TEST_CASE("host-owned clock: '.setclock' cannot retempo it (#1254)") {
    auto& mgr = YSE::CLOCK::Manager();
    LogCapture log;
    MakeHostClock("ho.sc", 120.f);

    Rig rig(YSE::OBJ::G_SETCLOCK, "ho.sc 240");
    // First registration wins: joining the patch did not retempo the clock.
    rig.Tick();
    REQUIRE(mgr.currentTempo("ho.sc") == doctest::Approx(120.f));

    rig.obj->SetFloatData(0, 0.f);
    rig.obj->SetListData(0, "tempo 60 1");
    rig.Tick();
    CHECK(mgr.currentTempo("ho.sc") == doctest::Approx(120.f));
    CHECK(log.Count(Refusal(".setclock", "ho.sc")) == 1);

    // Reading still works: bang sends the beat (two blocks at 120 BPM).
    rig.out.seen.clear();
    rig.obj->SetBang(0);
    REQUIRE(rig.out.seen.size() == 1);
    CHECK(rig.out.seen[0] == doctest::Approx(1.0f));

    DropClock("ho.sc");
  }

  TEST_CASE("host-owned clock: '.tempo' counts on it at the host's tempo without writing it "
            "(#1254)") {
    // `.tempo` writes its own tempo onto the clock when it starts. On a
    // host-owned clock that write is refused, and the count follows the host.
    auto& mgr = YSE::CLOCK::Manager();
    LogCapture log;
    MakeHostClock("ho.tp", 120.f);

    // 1 / 4: one quarter note (one beat) per count, at 60 BPM if it could set it.
    Rig rig(YSE::OBJ::G_TEMPO, "ho.tp 60 1 4");
    rig.obj->SetIntData(0, 1);
    CHECK(log.Count(Refusal(".tempo", "ho.tp")) == 1);

    // Eight quarter-second blocks at the host's 120 BPM is four beats: the
    // start's 0 and then 1, 2, 3 and 0 again. At 60 BPM it would be two.
    for (int i = 0; i < 8; i++)
      rig.Tick();
    CHECK(mgr.currentTempo("ho.tp") == doctest::Approx(120.f));
    REQUIRE(rig.out.seen.size() >= 5);
    CHECK(rig.out.seen[0] == doctest::Approx(0.f));
    CHECK(rig.out.seen[1] == doctest::Approx(1.f));
    CHECK(rig.out.seen[4] == doctest::Approx(0.f));

    // The tempo inlet is refused as well, without a second warning.
    rig.obj->SetFloatData(1, 30.f);
    rig.Tick();
    CHECK(mgr.currentTempo("ho.tp") == doctest::Approx(120.f));
    CHECK(log.Count(Refusal(".tempo", "ho.tp")) == 1);

    DropClock("ho.tp");
  }

} // TEST_SUITE
