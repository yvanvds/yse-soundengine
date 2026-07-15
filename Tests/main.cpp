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

int main(int argc, char** argv) {
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
