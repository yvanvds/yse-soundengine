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

#if defined(__ANDROID__)
#  include "support/android_entry.hpp"
#else

int main(int argc, char** argv) {
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
