// Signal loops and feedback in the patcher graph (issue #1038).
//
//  - A loop of ordinary signal cords is refused when the cord that would close
//    it is made, with a log naming the loop, so the patch keeps sounding
//    instead of falling silent.
//  - A link cord (~tapin -> ~tapout) is not a dependency: it is never pushed
//    and never holds an object back, so feedback through a delay line is
//    legal, makes sound, and does not depend on the order objects run in.
//
// The feedback patch used throughout:
//
//   [~+ 1] ---> [~tapin] ==link==> [~tapout D] ---> [~dac]
//                  ^                    |
//                  +---- [~* 0.5] <-----+
//
// With D a whole number of samples, the line holds z[n] = 1 + 0.5 z[n - D],
// so z[n] = sum_{k=0}^{floor(n/D)} 0.5^k, and the output is z[n - D].

#include <doctest/doctest.h>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pObject.h"
#include "dsp/buffer.hpp"
#include "headers/constants.hpp"
#include "implementations/logImplementation.h"
#include "internal/rtLogQueue.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using YSE::PATCHER::patcherImplementation;

namespace {

  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // The engine log, as a host sees it (cf. test_patcher_dict_print).
  struct LogCapture : YSE::logHandler {
    LogCapture() {
      YSE::INTERNAL::RtLog().drain();
      YSE::Log().setHandler(this);
    }
    ~LogCapture() override {
      YSE::Log().setHandler(nullptr);
    }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;
    LogCapture(LogCapture&&) = delete;
    LogCapture& operator=(LogCapture&&) = delete;

    void AddMessage(const std::string& message) override {
      const std::lock_guard<std::mutex> lock(mutex);
      lines.push_back(message);
    }
    bool Saw(const std::string& needle) const {
      const std::lock_guard<std::mutex> lock(mutex);
      for (const std::string& line : lines) {
        if (line.find(needle) != std::string::npos) return true;
      }
      return false;
    }

    mutable std::mutex mutex;
    std::vector<std::string> lines;
  };

  bool OutputIs(patcherImplementation& p, float value) {
    YSE::DSP::buffer& out = p.output[0];
    if (out.getLength() == 0) return false;
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      if (std::fabs(out.getPtr()[i] - value) > 1e-6f) return false;
    }
    return true;
  }

  // D samples as the ms argument ~tapout takes.
  std::string DelayArg(unsigned int samples) {
    return std::to_string(static_cast<double>(samples) * 1000.0 /
                          static_cast<double>(YSE::SAMPLERATE));
  }

  // The closed form in the header comment.
  double Expected(unsigned int n, unsigned int delay) {
    if (n < delay) return 0.0;
    const unsigned int m = n - delay;
    double z = 0.0;
    double g = 1.0;
    for (unsigned int k = 0; k <= m / delay; ++k) {
      z += g;
      g *= 0.5;
    }
    return z;
  }

  struct Feedback {
    YSE::pHandle* src = nullptr;
    YSE::pHandle* tapin = nullptr;
    YSE::pHandle* tapout = nullptr;
    YSE::pHandle* gain = nullptr;
    YSE::pHandle* dac = nullptr;
  };

  // The feedback patch, with its objects created in one order or the other
  // and its cords made in one order or the other, so the objects' handle
  // addresses and cord order differ between the two builds.
  Feedback Build(patcherImplementation& p, unsigned int delay, bool reversed) {
    Feedback f;
    if (!reversed) {
      f.src = p.CreateObject(YSE::OBJ::D_ADD, "1");
      f.tapin = p.CreateObject(YSE::OBJ::D_TAPIN, "1000");
      f.tapout = p.CreateObject(YSE::OBJ::D_TAPOUT, DelayArg(delay));
      f.gain = p.CreateObject(YSE::OBJ::D_MULTIPLY, "0.5");
      f.dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      p.Connect(f.src, 0, f.tapin, 0);
      p.Connect(f.tapin, 0, f.tapout, 0);
      p.Connect(f.tapout, 0, f.dac, 0);
      p.Connect(f.tapout, 0, f.gain, 0);
      p.Connect(f.gain, 0, f.tapin, 0);
    } else {
      f.dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      f.gain = p.CreateObject(YSE::OBJ::D_MULTIPLY, "0.5");
      f.tapout = p.CreateObject(YSE::OBJ::D_TAPOUT, DelayArg(delay));
      f.tapin = p.CreateObject(YSE::OBJ::D_TAPIN, "1000");
      f.src = p.CreateObject(YSE::OBJ::D_ADD, "1");
      p.Connect(f.gain, 0, f.tapin, 0);
      p.Connect(f.tapout, 0, f.gain, 0);
      p.Connect(f.tapout, 0, f.dac, 0);
      p.Connect(f.tapin, 0, f.tapout, 0);
      p.Connect(f.src, 0, f.tapin, 0);
    }
    return f;
  }

  // Render `blocks` blocks and check every sample against the closed form.
  void CheckClosedForm(patcherImplementation& p, unsigned int delay, int blocks,
                       unsigned int first = 0) {
    unsigned int n = first;
    for (int b = 0; b < blocks; ++b) {
      p.Calculate(YSE::T_DSP);
      for (unsigned int i = 0; i < N; ++i, ++n) {
        REQUIRE(p.output[0].getPtr()[i] == doctest::Approx(Expected(n, delay)));
      }
    }
  }

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("signal loops: a loop of signal cords is refused and named (#1038)") {
    LogCapture log;
    patcherImplementation p(1, nullptr);
    YSE::pHandle* src = p.CreateObject(YSE::OBJ::D_ADD, "0.25");
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::D_MULTIPLY, "1");
    YSE::pHandle* b = p.CreateObject(YSE::OBJ::D_MULTIPLY, "1");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(src, 0, a, 0);
    p.Connect(a, 0, b, 0);
    p.Connect(b, 0, dac, 0);

    SUBCASE("through two objects") {
      p.Connect(b, 0, a, 0);
      CHECK(b->GetConnections(0) == 1); // only the dac
      CHECK(log.Saw("would close a signal loop"));
      CHECK(log.Saw("~* #" + std::to_string(a->GetID())));
    }
    SUBCASE("into itself") {
      p.Connect(a, 0, a, 0);
      CHECK(a->GetConnections(0) == 1);
      CHECK(log.Saw("would close a signal loop"));
    }
    SUBCASE("into an inlet other than the one the path came in by") {
      p.Connect(b, 0, a, 1);
      CHECK(b->GetConnections(0) == 1);
      CHECK(log.Saw("would close a signal loop"));
    }

    // The patch still sounds: before #1038 the loop silenced it.
    p.Calculate(YSE::T_DSP);
    CHECK(OutputIs(p, 0.25f));
  }

  TEST_CASE("signal loops: fan-out that meets again is not a loop (#1038)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* src = p.CreateObject(YSE::OBJ::D_ADD, "0.25");
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::D_MULTIPLY, "2");
    YSE::pHandle* b = p.CreateObject(YSE::OBJ::D_MULTIPLY, "1");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(src, 0, a, 0);
    p.Connect(src, 0, b, 0);
    p.Connect(a, 0, b, 0); // src reaches b twice: a diamond, not a loop
    p.Connect(b, 0, dac, 0);
    CHECK(a->GetConnections(0) == 1);
    p.Calculate(YSE::T_DSP);
    CHECK(OutputIs(p, 0.75f));
  }

  TEST_CASE("signal loops: a control cord back up the chain is not a signal loop (#1038)") {
    // ~snapshot's outlet is a control outlet: what it sends lands after the
    // traversal (#1037), so it holds nothing back and is allowed.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* src = p.CreateObject(YSE::OBJ::D_ADD, "0.25");
    YSE::pHandle* snap = p.CreateObject(YSE::OBJ::D_SNAPSHOT, "");
    p.Connect(src, 0, snap, 0);
    p.Connect(snap, 0, src, 1);
    CHECK(snap->GetConnections(0) == 1);
  }

  TEST_CASE("signal loops: feedback through ~tapin / ~tapout makes the closed-form sound "
            "(#1038)") {
    const unsigned int delay = 2 * N;
    patcherImplementation p(1, nullptr);
    const Feedback f = Build(p, delay, false);
    // Every cord was accepted, the one closing the loop included.
    CHECK(f.gain->GetConnections(0) == 1);
    CHECK(f.tapin->GetConnections(0) == 1);
    CheckClosedForm(p, delay, 10);
  }

  TEST_CASE("signal loops: the feedback output does not depend on build order (#1038)") {
    // Creation order changes the handle addresses the patcher's object map is
    // keyed by, and cord order changes fan-out order: neither may change a
    // sample.
    const unsigned int delay = N + 7; // not a whole number of blocks
    patcherImplementation forward(1, nullptr);
    patcherImplementation backward(1, nullptr);
    Build(forward, delay, false);
    Build(backward, delay, true);
    for (int b = 0; b < 12; ++b) {
      forward.Calculate(YSE::T_DSP);
      backward.Calculate(YSE::T_DSP);
      for (unsigned int i = 0; i < N; ++i)
        REQUIRE(forward.output[0].getPtr()[i] == backward.output[0].getPtr()[i]);
    }
  }

  TEST_CASE("signal loops: a non-feedback delay starts at once (#1038)") {
    // ~tapout reads a line ~tapin has not written yet this block: still one
    // block of delay, never a block of silence on top.
    const unsigned int delay = N;
    patcherImplementation p(1, nullptr);
    YSE::pHandle* src = p.CreateObject(YSE::OBJ::D_ADD, "1");
    YSE::pHandle* tapin = p.CreateObject(YSE::OBJ::D_TAPIN, "100");
    YSE::pHandle* tapout = p.CreateObject(YSE::OBJ::D_TAPOUT, DelayArg(delay));
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(src, 0, tapin, 0);
    p.Connect(tapin, 0, tapout, 0);
    p.Connect(tapout, 0, dac, 0);
    p.Calculate(YSE::T_DSP);
    CHECK(OutputIs(p, 0.f));
    p.Calculate(YSE::T_DSP);
    CHECK(OutputIs(p, 1.f));
  }

  TEST_CASE("signal loops: a link only ends on ~tapout's left inlet, once (#1038)") {
    LogCapture log;
    patcherImplementation p(1, nullptr);
    YSE::pHandle* tapin = p.CreateObject(YSE::OBJ::D_TAPIN, "100");
    YSE::pHandle* other = p.CreateObject(YSE::OBJ::D_TAPIN, "100");
    YSE::pHandle* tapout = p.CreateObject(YSE::OBJ::D_TAPOUT, "10 20");
    YSE::pHandle* mul = p.CreateObject(YSE::OBJ::D_MULTIPLY, "1");

    p.Connect(tapin, 0, mul, 0);
    CHECK(tapin->GetConnections(0) == 0);
    CHECK(log.Saw("only connects to an inlet that reads the line"));

    p.Connect(tapin, 0, tapout, 1);
    CHECK(tapin->GetConnections(0) == 0);

    p.Connect(tapin, 0, tapout, 0);
    CHECK(tapin->GetConnections(0) == 1);
    p.Connect(other, 0, tapout, 0);
    CHECK(other->GetConnections(0) == 0);
    CHECK(log.Saw("already has a delay-line link"));

    // An ordinary signal into the same inlet is the delay time, and is fine.
    p.Connect(mul, 0, tapout, 0);
    CHECK(mul->GetConnections(0) == 1);
  }

  TEST_CASE("signal loops: the feedback patch survives a DumpJSON / ParseJSON round trip "
            "(#1038)") {
    const unsigned int delay = 2 * N;
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      Build(src, delay, false);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CheckClosedForm(dst, delay, 8);
  }

  TEST_CASE("signal loops: a saved loop loses the cord that closes it on load (#1038)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* c = src.CreateObject(YSE::OBJ::D_ADD, "0.25");
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_MULTIPLY, "1");
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_MULTIPLY, "1");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      src.Connect(c, 0, a, 0);
      src.Connect(a, 0, b, 0);
      src.Connect(b, 0, dac, 0);
      // A file written by a build without the check: wired past Connect.
      TestHelpers::Wire(*src.GetObjectFromID(b->GetID()), 0, *src.GetObjectFromID(a->GetID()), 0);
      dump = src.DumpJSON();
      src.GetObjectFromID(a->GetID())
          ->DisconnectInlet(src.GetObjectFromID(b->GetID())->GetOutlet(0), 0);
    }
    LogCapture log;
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(log.Saw("would close a signal loop"));
    dst.Calculate(YSE::T_DSP);
    CHECK(OutputIs(dst, 0.25f));
  }

  TEST_CASE("signal loops: a live re-parse of ~tapin or ~tapout keeps the link (#1038)") {
    const unsigned int delay = 2 * N;
    patcherImplementation p(1, nullptr);
    const Feedback f = Build(p, delay, false);

    SUBCASE("~tapin") {
      p.SetObjectParams(f.tapin, "500");
      CHECK(f.tapin->GetConnections(0) == 1);
    }
    SUBCASE("~tapout") {
      p.SetObjectParams(f.tapout, DelayArg(delay));
      CHECK(f.tapin->GetConnections(0) == 1);
    }
    // Nothing has rendered yet, so the rebuilt patch gives the closed form.
    CheckClosedForm(p, delay, 6);
  }

  TEST_CASE("signal loops: deleting ~tapin leaves ~tapout silent (#1038)") {
    const unsigned int delay = N;
    patcherImplementation p(1, nullptr);
    const Feedback f = Build(p, delay, false);
    for (int b = 0; b < 4; ++b)
      p.Calculate(YSE::T_DSP);
    p.DeleteObject(f.tapin);
    p.Calculate(YSE::T_DSP);
    CHECK(OutputIs(p, 0.f));
  }

  TEST_CASE("signal loops: rendering the feedback patch allocates nothing (#1038)") {
    patcherImplementation p(1, nullptr);
    Build(p, N + 3, false);
    p.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      for (int b = 0; b < 16; ++b)
        p.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  TEST_CASE("signal loops: links made and removed while rendering keep every block whole "
            "(#1038)") {
    // One render thread, one editor toggling the link. Each block reads the
    // link from its pinned snapshot: the line or silence, never a freed ~tapin.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* src = p.CreateObject(YSE::OBJ::D_ADD, "1");
    YSE::pHandle* tapin = p.CreateObject(YSE::OBJ::D_TAPIN, "100");
    YSE::pHandle* tapout = p.CreateObject(YSE::OBJ::D_TAPOUT, DelayArg(N));
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(src, 0, tapin, 0);
    p.Connect(tapout, 0, dac, 0);
    for (int b = 0; b < 4; ++b)
      p.Calculate(YSE::T_DSP);

    std::atomic<bool> stop{false};
    std::atomic<int> bad{0};
    std::atomic<int> blocks{0};
    std::thread render([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        p.Calculate(YSE::T_DSP);
        if (!OutputIs(p, 0.f) && !OutputIs(p, 1.f)) bad.fetch_add(1);
        blocks.fetch_add(1, std::memory_order_relaxed);
      }
    });
    for (int i = 0; i < 2000; ++i) {
      p.Connect(tapin, 0, tapout, 0);
      p.Disconnect(tapin, 0, tapout, 0);
    }
    while (blocks.load() < 100) {
      p.Connect(tapin, 0, tapout, 0);
      p.Disconnect(tapin, 0, tapout, 0);
    }
    stop = true;
    render.join();
    CHECK(bad.load() == 0);
  }

} // TEST_SUITE("patcher")
