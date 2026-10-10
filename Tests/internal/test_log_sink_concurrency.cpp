// Regression test for the engine log sink under concurrent emitters (#820).
//
// `logImplementation::logMessage()` delivered every line with nothing holding
// the emitting threads apart: it wrote the shared std::ofstream, or called a
// host-installed logHandler, straight from whichever thread happened to be
// reporting. Several engine threads report — the control thread, the slow-pool
// file loader, the MIDI hub — and an unfiltered TSan run of the `sound` suite
// caught two of them inside the same 8 KB filebuf:
//
//   Write of size 8 by thread T1:
//     logImplementation::emit()      implementations/logImplementation.cpp:125
//     YSE::log::sendMessage()        log.cpp:42
//     soundFile::loadNonStreaming()  internal/lsfSoundfile.cpp:106
//   Previous read of size 8 by main thread:
//     logImplementation::emit()      implementations/logImplementation.cpp:125
//     YSE::log::sendMessage()        log.cpp:42
//     YSE::sound::~sound()           sound/soundInterface.cpp:103
//   Location is heap block of size 8192 allocated by main thread:
//     YSE::INTERNAL::LogImpl()       implementations/logImplementation.cpp:30
//
// The same race reached host code through the other branch, where it is worse
// than an interleaved line: a Windows ASan run reddened intermittently with a
// heap-buffer-overflow whose whole stack was logMessage() -> a test handler's
// std::vector.
//
// So the contract this file pins is the one a host actually needs: **the engine
// delivers one line at a time**. A logHandler may keep ordinary, unsynchronised
// state, because AddMessage() is never entered from two threads at once. The
// handler below deliberately does exactly that — a plain push_back — and the
// only thing making it safe is the engine's lock.
//
// Runs in the isolated yse_tests_logsafety process, like the EmitNoThrow cases
// next door and for the same reason: it swaps the process-global log sink.

#include <doctest/doctest.h>

#include "log.hpp"

#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

  // A host sink of the ordinary kind: state of its own, no lock in sight. Every
  // RecordingHandler in this test suite is shaped this way, and so is the
  // typical embedder's (append to a console buffer, push to a UI list).
  class RecordingHandler : public YSE::logHandler {
  public:
    void AddMessage(const std::string& message) override {
      messages.push_back(message);
    }
    std::vector<std::string> messages;
  };

  // Installs a sink, and a level that lets messages through, for one case —
  // and puts both back even if an assertion unwinds.
  class ScopedSink {
  public:
    explicit ScopedSink(YSE::logHandler* handler) : previousLevel(YSE::Log().getLevel()) {
      YSE::Log().setLevel(YSE::EL_DEBUG);
      YSE::Log().setHandler(handler);
    }
    ~ScopedSink() {
      YSE::Log().setHandler(nullptr);
      YSE::Log().setLevel(previousLevel);
    }
    ScopedSink(const ScopedSink&) = delete;
    ScopedSink& operator=(const ScopedSink&) = delete;

  private:
    YSE::ERROR_LEVEL previousLevel;
  };

  constexpr int kEmitters = 4;
  constexpr int kLinesPerEmitter = 250;

} // namespace

TEST_SUITE("logsafety") {

  TEST_CASE("the log sink delivers one line at a time to a host handler (#820)") {
    RecordingHandler handler;
    ScopedSink sink(&handler);

    // Every line is unique, so the assertions below can tell "arrived" from
    // "arrived twice" without counting anything itself.
    std::vector<std::thread> emitters;
    emitters.reserve(kEmitters);
    for (int t = 0; t < kEmitters; ++t) {
      emitters.emplace_back([t] {
        for (int i = 0; i < kLinesPerEmitter; ++i) {
          const std::string line =
              "820 emitter " + std::to_string(t) + " line " + std::to_string(i);
          YSE::Log().sendMessage(line.c_str());
        }
      });
    }
    for (std::thread& e : emitters)
      e.join();

    // Nothing lost. Unsynchronised push_back from four threads loses elements
    // to a torn size long before it corrupts the buffer, so this is the
    // assertion that fails first without the fix — under a sanitizer the run
    // does not get this far at all.
    CHECK(handler.messages.size() == static_cast<size_t>(kEmitters * kLinesPerEmitter));

    // Nothing duplicated or shredded: distinct inputs stay distinct on the way
    // out, which a half-written vector slot cannot manage.
    const std::set<std::string> distinct(handler.messages.begin(), handler.messages.end());
    CHECK(distinct.size() == handler.messages.size());
  }

  TEST_CASE("the log sink survives emitters racing a handler swap (#820)") {
    // setHandler() re-points the very pointer logMessage() dispatches on, and
    // every scoped sink in the test suite clears it on the way out. Swapping it
    // while other threads are mid-emit is therefore not a contrived case but
    // the ordinary teardown one; it must not tear the handler out from under a
    // call already inside AddMessage().
    RecordingHandler handler;
    ScopedSink sink(&handler);

    std::vector<std::thread> emitters;
    emitters.reserve(kEmitters);
    for (int t = 0; t < kEmitters; ++t) {
      emitters.emplace_back([t] {
        for (int i = 0; i < kLinesPerEmitter; ++i) {
          const std::string line = "820 swap " + std::to_string(t) + " line " + std::to_string(i);
          YSE::Log().sendMessage(line.c_str());
        }
      });
    }

    // Back and forth between the handler and the file sink while they run: the
    // lines that land in the file are lost to the handler by design, so the
    // only claim here is that both destinations stay intact.
    for (int i = 0; i < 20; ++i) {
      YSE::Log().setHandler(nullptr);
      YSE::Log().setHandler(&handler);
    }

    for (std::thread& e : emitters)
      e.join();

    // Whatever reached the handler reached it whole.
    for (const std::string& m : handler.messages) {
      CHECK(m.find("820 swap ") != std::string::npos);
    }
  }
}
