// Regression tests for YSE::INTERNAL::EmitNoThrow — the noexcept log path a
// destructor must use (issue #433).
//
// The house guard the engine's twelve teardown destructors used,
// `catch (...) { LogImpl().emit(...); }`, was not itself exception-safe:
// emit() builds a std::string and hands it to a possibly host-installed
// logHandler, so a throwing sink (or a failing allocation) escaped the catch
// block of an implicitly-noexcept destructor and called std::terminate() —
// the exact failure the guard was added in #414 to prevent.
//
// Isolated in its own process (yse_tests_logsafety) for the same reason as
// yse_tests_capisurface: these cases swap the process-global log sink for one
// that throws on every message, which no other suite sharing the process — and
// in particular no live audio thread — could be expected to survive.

#include <doctest/doctest.h>

#include "implementations/logImplementation.h"
#include "log.hpp"

#include <stdexcept>
#include <string>

namespace {

  // A sink that fails the way a real one can: out of memory, a dead pipe to the
  // host's console, an exception thrown by embedder code inside AddMessage().
  class ThrowingHandler : public YSE::logHandler {
  public:
    void AddMessage(const std::string&) override {
      ++calls;
      throw std::runtime_error("log sink failed");
    }
    int calls = 0;
  };

  // Installs a sink, and a level that lets errors through, for the duration of
  // one case — and puts both back afterwards even if an assertion unwinds.
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

  // Mirrors the shape of every guarded destructor in the engine: a teardown
  // step that throws, and a handler that reports it. Implicitly noexcept, so a
  // handler that throws does not merely fail an assertion here — it terminates
  // the process, which is precisely the regression under test.
  struct GuardedDestructor {
    ~GuardedDestructor() {
      try {
        throw std::runtime_error("teardown failed");
      } catch (...) {
        YSE::INTERNAL::EmitNoThrow(YSE::E_ERROR, "GuardedDestructor swallowed exception");
      }
    }
  };

} // namespace

TEST_SUITE("logsafety") {

  // Pins the hazard itself, so the fix below is measured against something
  // real: the old call form propagates the sink's exception to its caller.
  TEST_CASE("emit() propagates a throwing log sink") {
    ThrowingHandler handler;
    ScopedSink sink(&handler);

    CHECK_THROWS_AS(YSE::INTERNAL::LogImpl().emit(YSE::E_ERROR, "boom"), std::runtime_error);
    CHECK(handler.calls == 1);
  }

  TEST_CASE("EmitNoThrow absorbs a throwing log sink") {
    ThrowingHandler handler;
    ScopedSink sink(&handler);

    CHECK_NOTHROW(YSE::INTERNAL::EmitNoThrow(YSE::E_ERROR, "teardown failed"));
    // The message reached the sink; only its exception was absorbed.
    CHECK(handler.calls == 1);
  }

  TEST_CASE("a noexcept destructor survives a throwing log sink") {
    ThrowingHandler handler;
    ScopedSink sink(&handler);

    {
      GuardedDestructor guarded;
      (void)guarded;
    }

    CHECK(handler.calls == 1);
  }

  TEST_CASE("EmitNoThrow accepts a null message") {
    ThrowingHandler handler;
    ScopedSink sink(&handler);

    CHECK_NOTHROW(YSE::INTERNAL::EmitNoThrow(YSE::E_ERROR, nullptr));
    CHECK(handler.calls == 1);
  }

  TEST_CASE("EmitNoThrow honours the log level filter") {
    ThrowingHandler handler;
    ScopedSink sink(&handler);
    YSE::Log().setLevel(YSE::EL_NONE);

    CHECK_NOTHROW(YSE::INTERNAL::EmitNoThrow(YSE::E_ERROR, "dropped before the sink"));
    CHECK(handler.calls == 0);
  }
}
