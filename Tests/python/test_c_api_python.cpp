// C ABI scripting surface tests (issue #125, epic #119).
//
// Unlike test_script_runtime.cpp (compiled only when YSE_ENABLE_PYTHON is ON),
// this TU is always built: the C API ships the same three symbols in both
// configurations, and the acceptance criteria cover the OFF build too. The
// cases branch on the macro internally.
//
// OFF cases need no engine — the OFF stub answers entirely at the C boundary.
// ON cases need the interpreter booted and update() draining results, so they
// drive the engine through TestHelpers::engineInit() and skip gracefully when
// no audio device is available (CI), like the other engine-dependent suites.
// engineInit() boots the engine exactly once per process and never closes it
// (main.cpp does, at exit), so these cases never trip the re-init assertion
// tracked in issue #132.

#include <doctest/doctest.h>

#include <string>

#include "yse_c/yse_python.h"

#if YSE_ENABLE_PYTHON
#include "yse.hpp"
#include "support/null_device.hpp"
#endif

namespace {

// Userdata for the error callback: counts deliveries and keeps the last
// traceback so assertions can inspect it.
struct ErrSink {
  int         count = 0;
  std::string last;
};

void YSE_C_CALLBACK captureCb(const char* traceback, void* userdata) {
  auto* sink = static_cast<ErrSink*>(userdata);
  sink->count++;
  sink->last = traceback != nullptr ? traceback : "";
}

} // namespace

TEST_SUITE("python") {

TEST_CASE("c-api python: yse_python_enabled reflects the build configuration") {
#if YSE_ENABLE_PYTHON
  CHECK(yse_python_enabled() == 1);
#else
  CHECK(yse_python_enabled() == 0);
#endif
}

#if !YSE_ENABLE_PYTHON

TEST_CASE("c-api python (OFF): run_script reports the missing feature synchronously") {
  ErrSink sink;
  yse_set_script_error_callback(&captureCb, &sink);

  // No interpreter to defer to: the callback must fire before run_script
  // returns, with the documented string.
  yse_run_script("result = 1 + 1");
  CHECK(sink.count == 1);
  CHECK(sink.last == "YSE compiled without YSE_ENABLE_PYTHON");

  yse_set_script_error_callback(nullptr, nullptr);
}

TEST_CASE("c-api python (OFF): run_script with no callback registered is a safe no-op") {
  yse_set_script_error_callback(nullptr, nullptr);
  yse_run_script("anything");  // must not crash with no callback installed
  CHECK(yse_python_enabled() == 0);
}

#else  // YSE_ENABLE_PYTHON

namespace {

// Pump update() until the sink has observed at least `expected` callbacks, or
// the budget is exhausted. The script thread is asynchronous, so the engine
// delivers "within one tick" only after the worker has run; poll rather than
// assume a single update() suffices.
bool pumpUntil(ErrSink& sink, int expected, int tries = 300, unsigned ms = 10) {
  for (int i = 0; i < tries; ++i) {
    YSE::System().update();
    if (sink.count >= expected) return true;
    YSE::System().sleep(ms);
  }
  return false;
}

} // namespace

TEST_CASE("c-api python (ON): a clean script fires no error callback") {
  if (!TestHelpers::engineInit()) return;
  ErrSink sink;
  yse_set_script_error_callback(&captureCb, &sink);

  yse_run_script("result = 1 + 1");
  // Give the worker ample ticks; a successful eval must stay silent.
  for (int i = 0; i < 20; ++i) {
    YSE::System().update();
    YSE::System().sleep(10);
  }
  CHECK(sink.count == 0);

  yse_set_script_error_callback(nullptr, nullptr);
}

TEST_CASE("c-api python (ON): a raised exception delivers a traceback") {
  if (!TestHelpers::engineInit()) return;
  ErrSink sink;
  yse_set_script_error_callback(&captureCb, &sink);

  yse_run_script("raise ValueError('boom')");
  REQUIRE(pumpUntil(sink, 1));
  CHECK(sink.count == 1);
  CHECK(sink.last.find("ValueError: boom") != std::string::npos);
  // The DSL spec / issue #125 mandate the "<script>" source filename.
  CHECK(sink.last.find("\"<script>\"") != std::string::npos);

  yse_set_script_error_callback(nullptr, nullptr);
}

TEST_CASE("c-api python (ON): a syntax error delivers a SyntaxError traceback") {
  if (!TestHelpers::engineInit()) return;
  ErrSink sink;
  yse_set_script_error_callback(&captureCb, &sink);

  yse_run_script("def f(:\n  pass\n");
  REQUIRE(pumpUntil(sink, 1));
  CHECK(sink.last.find("SyntaxError") != std::string::npos);
  // SyntaxError formatting names the source file too; must read "<script>".
  CHECK(sink.last.find("\"<script>\"") != std::string::npos);

  yse_set_script_error_callback(nullptr, nullptr);
}

TEST_CASE("c-api python (ON): replacing the callback retires the previous one") {
  if (!TestHelpers::engineInit()) return;
  ErrSink first, second;
  yse_set_script_error_callback(&captureCb, &first);
  // Swap before any error is produced; the error must reach only `second`.
  yse_set_script_error_callback(&captureCb, &second);

  yse_run_script("raise RuntimeError('after swap')");
  REQUIRE(pumpUntil(second, 1));
  CHECK(second.count == 1);
  CHECK(first.count == 0);

  yse_set_script_error_callback(nullptr, nullptr);
}

#endif  // YSE_ENABLE_PYTHON

} // TEST_SUITE("python")
