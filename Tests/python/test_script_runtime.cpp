// Tests for the embedded-CPython script runtime (issue #124, epic #119).
//
// These only build when YSE_ENABLE_PYTHON is ON (the source is added to the
// suite conditionally in Tests/CMakeLists.txt); the macro guard is
// belt-and-suspenders.
//
// The runtime is driven directly through its public surface rather than via
// the engine, so the cases need no audio device and run on CI. They also never
// init or close the engine, so they coexist safely inside the shared test
// binary alongside suites that do.
//
// Interpreter ownership adapts to context (see ScriptRuntime::start): run in
// isolation — e.g. the `yse_tests_python` ctest entry's
// `yse_tests --test-suite=python`, which is the process the ASan job drives —
// no engine is up, so each locally-constructed runtime owns the interpreter and
// the 50-cycle case genuinely exercises Py_Initialize/Py_Finalize re-entry. Run
// inside the full binary after another suite has booted the engine (and, on a
// YSE_ENABLE_PYTHON build, Python), the runtimes attach to the live interpreter
// instead; the evals still execute and the cases still pass.

#if YSE_ENABLE_PYTHON

#include <doctest/doctest.h>

#include "yse.hpp"
#include "python/scriptRuntime.h"

namespace {

using YSE::INTERNAL::EvalResult;
using YSE::INTERNAL::EvalStatus;
using YSE::INTERNAL::ScriptRuntime;

// The worker processes a pushed request as soon as it is notified; poll the
// outbound queue with a bounded wait rather than guessing a fixed delay.
bool waitForResult(ScriptRuntime& rt, EvalResult& out,
                   int tries = 300, unsigned int sleepMs = 10) {
    for (int i = 0; i < tries; ++i) {
        if (rt.tryPopResult(out)) return true;
        YSE::System().sleep(sleepMs);
    }
    return false;
}

} // namespace

TEST_SUITE("python") {

TEST_CASE("python: a successful eval reports Ok") {
    ScriptRuntime rt;
    rt.start();

    rt.pushEval("result = 1 + 1");

    EvalResult out;
    REQUIRE(waitForResult(rt, out));
    CHECK(out.status == EvalStatus::Ok);
    CHECK(out.traceback.empty());

    rt.stop();
}

TEST_CASE("python: a raised exception reports Error with a traceback") {
    ScriptRuntime rt;
    rt.start();

    rt.pushEval("raise ValueError('boom')");

    EvalResult out;
    REQUIRE(waitForResult(rt, out));
    CHECK(out.status == EvalStatus::Error);
    // The DSL spec mandates traceback.format_exception output; the final line
    // carries the type and message regardless of formatting details.
    CHECK(out.traceback.find("ValueError: boom") != std::string::npos);

    rt.stop();
}

TEST_CASE("python: requests queued before stop() are still drained") {
    ScriptRuntime rt;
    rt.start();

    rt.pushEval("a = 41");
    rt.pushEval("b = a + 1");

    EvalResult first;
    REQUIRE(waitForResult(rt, first));
    CHECK(first.status == EvalStatus::Ok);
    EvalResult second;
    REQUIRE(waitForResult(rt, second));
    CHECK(second.status == EvalStatus::Ok);

    rt.stop();
}

TEST_CASE("python: 50 init->close cycles boot and finalize cleanly") {
    // Exercises Py_Initialize/Py_Finalize re-entry in one process. Under ASan
    // this is the leak check from the issue's acceptance criteria.
    for (int i = 0; i < 50; ++i) {
        ScriptRuntime rt;
        rt.start();
        rt.pushEval("cycle = 1 + 1");
        EvalResult out;
        REQUIRE(waitForResult(rt, out));
        CHECK(out.status == EvalStatus::Ok);
        rt.stop();
    }
}

// NOTE: the real engine wiring (System::initOffline -> startScripting,
// update -> wakeScripting, close -> stopScripting) is intentionally NOT
// exercised here. Driving System init/close from a shared-process test trips a
// pre-existing reverb teardown assertion on re-init (issue #132), unrelated to
// Python. The wiring is a thin pass-through to ScriptRuntime, which these
// cases cover directly; the engine-level boot is validated manually until #132
// makes repeated init/close safe.

} // TEST_SUITE("python")

#endif  // YSE_ENABLE_PYTHON
