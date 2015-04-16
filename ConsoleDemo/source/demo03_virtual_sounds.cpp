#include <iostream>
#include <cstdlib>
#include <forward_list>
#include "yse.hpp"
#if defined YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

std::forward_list<YSE::sound> sounds;
Int counter = 0;

void addSound();

void error(const char * message) {
  std::cout << message << std::endl;
}

int main() {
  YSE::System().init();
  YSE::Log().setCallback(error);

  /* Using 20000 sounds is a bit much, but done here for demonstration purposes.
     If the number of active sounds is > maxSounds, YSE will find the most significant
     sounds and virtualize the others.
  
     Note that the virtualization selection (which is what maxSounds sets) is not optimized
     for numbers this large. In normal circumstances you'd have like 200 sounds active 
     at most and set maxSounds to 50. This should be enough to filter out the least audible
     sounds from dsp processing and save some processing power for other parts of your software.
  */
  YSE::System().maxSounds(100);

  std::cout << "Press the spacebar to add a sound at a random position." << std::endl;
  std::cout << "...or e to exit." << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
      case ' ': for (int i = 0; i < 10; i++) addSound(); break;
      case 'e': goto exit;
      }
    }

    YSE::System().sleep(100);
    YSE::System().update();
#if defined YSE_WINDOWS
    _cprintf_s("Sounds: %d / Audio thread CPU Load: %.2f \r", counter, YSE::System().cpuLoad());
#endif
#if defined YSE_MAC
    printf("Sounds: %d / Audio thread CPU Load: %.2f \r", counter, YSE::System().cpuLoad());
#endif
  }

exit:
  sounds.clear();

  YSE::System().close();
  return 0;
}

void addSound() {
  sounds.emplace_front();

  switch (YSE::Random(4)) {
  case 0: sounds.front().create("contact.ogg", &YSE::ChannelAmbient(), true); break;
  case 1: sounds.front().create("drone.ogg", &YSE::ChannelVoice(), true); break;
  case 2: sounds.front().create("kick.ogg", &YSE::ChannelMusic(), true); break;
  case 3: sounds.front().create("pulse1.ogg", &YSE::ChannelFX(), true); break;
  }
  if (sounds.front().isValid()) {
    sounds.front().setPosition(YSE::Vec(YSE::Random(20) - 10, YSE::Random(20) - 10, YSE::Random(20) - 10));
    sounds.front().play().setVolume(0.1); // it can get very loud with 100's of sounds
    counter++;
  }
}
