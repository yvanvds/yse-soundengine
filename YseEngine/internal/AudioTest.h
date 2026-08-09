

#ifndef _YSE_AUDIO_TEST_H
#define _YSE_AUDIO_TEST_H

#pragma once
#include "../yse.hpp"

class shepard;

namespace YSE {
  namespace INTERNAL {

    class AudioTest {
    public:
      AudioTest();
      ~AudioTest();
      void On(bool value);

      /** The diagnostic sound this driver owns (exposed for tests). The
       *  interface is process-global, but its implementation is session state —
       *  isValid() is false before the first session and between close() and
       *  the next On() (issue #717). */
      sound& source();

    private:
      /** (Re)build the diagnostic sound for the current session and report
       *  whether there is one to drive. See the definition for why it cannot
       *  simply outlive a close(). */
      bool ensureSound();

      sound testSound;
      shepard* shep;
    };

    AudioTest& Test();
  } // namespace INTERNAL
} // namespace YSE
#endif
