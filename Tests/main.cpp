// Defines the doctest runner.  Exactly one translation unit must contain this.
//
// Custom runner so we can shut down the engine via the public lifecycle API
// before static destructors fire. Without this the PortAudio callback thread
// keeps iterating channel->sounds while SOUND/CHANNEL/REVERB manager statics
// clear their `implementations` lists, and the full combined unit run hangs
// at process exit.
//
// The four-phase race fix (Phases A-D) closed the runtime races that made
// calling System().close() during teardown unsafe in earlier revisions.
// close() now provides the proper ordered shutdown:
//   1. Pa_StopStream / Pa_CloseStream         (audio thread stops)
//   2. slowThreads.shutdown() / fastThreads.shutdown()
//   3. Global().active = false
// After close() returns, all engine threads are gone. Subsequent static
// destructors then run on the main thread alone, with nothing racing them.
//
// Include order matters: yse.hpp must precede the doctest IMPLEMENT include
// because doctest's implementation pulls in <windows.h>, whose wingdi.h
// defines RELATIVE as a macro that collides with YSE::RELATIVE in
// sound.hpp's SOUND_INTENT-style enum.

#include "yse.hpp"
#include "support/null_device.hpp"

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

// Honour YSE_TEST_FORCED_RATE before any test (or doctest's discovery pass)
// can construct an SAMPLERATE-baking DSP object. The variable is read on the
// first call into the test binary so it works in both the desktop main()
// below and the Android NativeActivity entry in android_entry.hpp.
namespace TestHelpers {
  void applyForcedSampleRateFromEnv() {
    const char* forced = std::getenv("YSE_TEST_FORCED_RATE");
    if (!forced || !*forced) return;
    unsigned long parsed = std::strtoul(forced, nullptr, 10);
    if (parsed == 0) return;
    // SAMPLERATE has its default static-init value at this point; no engine
    // session is open yet, so the lock in INTERNAL::Global() is still false
    // and this write is permitted.
    YSE::SAMPLERATE = (UInt)parsed;
  }
} // namespace TestHelpers

#if defined(__ANDROID__)
#include "support/android_entry.hpp"
#else

namespace {

  // Every doctest option that either narrows the run or only asks a question
  // about it. A desktop command line carrying none of them means "run all
  // ~4200 cases in this one process", which is the invocation refused below.
  // Short and long spellings both listed: doctest accepts either, with one or
  // two leading dashes and an optional `=value` / `:value` suffix.
  const char* const kSelectionOptions[] = {
      "test-suite",
      "ts",
      "test-suite-exclude",
      "tse",
      "test-case",
      "tc",
      "test-case-exclude",
      "tce",
      "subcase",
      "sc",
      "subcase-exclude",
      "sce",
      "source-file",
      "sf",
      "source-file-exclude",
      "sfe",
      "list-test-cases",
      "ltc",
      "list-test-suites",
      "lts",
      "list-reporters",
      "lr",
      "count",
      "c",
      "no-run",
      "nr",
      "help",
      "h",
      "?",
      "version",
      "v",
  };

  bool isSelectionOption(const char* arg) {
    if (arg == nullptr || arg[0] != '-') return false;
    const char* name = arg + 1;
    if (*name == '-') ++name;
    for (const char* option : kSelectionOptions) {
      const std::size_t length = std::strlen(option);
      if (std::strncmp(name, option, length) != 0) continue;
      const char tail = name[length];
      if (tail == '\0' || tail == '=' || tail == ':') return true;
    }
    return false;
  }

  bool hasSelection(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
      if (isSelectionOption(argv[i])) return true;
    }
    return false;
  }

} // namespace

int main(int argc, char** argv) {
  // Refuse the whole-suite-in-one-process invocation (issue #712).
  //
  // The supported entry point is CTest: `yse_unit_tests` excludes every suite
  // that drives System::close() / initOffline() (see Tests/CMakeLists.txt) and
  // each of those runs in its own process, because tearing down the
  // process-global thread pools and engine state pulls the ground out from
  // under whatever else shares the process. Running the binary bare puts them
  // all back together, and the result is not a slow test run but a corrupted
  // one: measured here, a bare run dies at ~168 s with an access violation
  // (0xC0000005), reproducibly, six runs out of six.
  //
  // Failing here rather than crashing 168 seconds later matters beyond the
  // wasted time. A crash at that point is what puts a half-dead yse_tests.exe
  // on the machine — Windows keeps the image mapped while the fault is
  // handled, so the next build's link fails with "Permission denied" on
  // bin/yse_tests.exe and reads as a toolchain problem rather than as
  // wreckage from the previous run (issue #712). The refusal below is the
  // cheapest way to stop producing those processes in the first place.
  //
  // Anything that narrows the run — `--test-suite=`, `--test-case=`, the
  // `--test-suite-exclude=` list CTest passes — is a supported invocation and
  // passes straight through, as do doctest's query flags (`--list-test-cases`,
  // `--count`, `--help`, ...) which never run a test case. Set
  // YSE_TESTS_ALLOW_MONOLITHIC=1 to drive the unsupported run deliberately,
  // which is what anyone debugging that crash needs.
  //
  // Android is unaffected: its entry point is android_entry.hpp above, where
  // the NativeActivity has no command line to pass filters on and the whole
  // suite in one process is the only thing available.
  if (!hasSelection(argc, argv) && std::getenv("YSE_TESTS_ALLOW_MONOLITHIC") == nullptr) {
    std::cerr << "yse_tests: refusing to run the whole suite in one process.\n"
                 "  The suites that drive System::close() / initOffline() must each run in\n"
                 "  their own process; sharing one corrupts the run (it faults at ~168 s) and\n"
                 "  can leave a yse_tests.exe behind that blocks the next link.\n"
                 "  Run `ctest --preset tests-debug` (or `python yse.py test`) instead, or\n"
                 "  narrow this invocation with --test-suite=<name> / --test-case=<pattern>.\n"
                 "  Set YSE_TESTS_ALLOW_MONOLITHIC=1 to run it anyway.\n";
    return EXIT_FAILURE;
  }

  TestHelpers::applyForcedSampleRateFromEnv();

  doctest::Context context;
  context.applyCommandLine(argc, argv);
  const int res = context.run();
  if (context.shouldExit()) return res;

  if (TestHelpers::engineInitialized()) {
    YSE::System().close();
  }
  return res;
}

#endif
