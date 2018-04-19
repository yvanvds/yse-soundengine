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

    private:
      sound testSound;
      shepard * shep;
    };

    AudioTest & Test();
  }
}