#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::sound snare;
YSE::reverb bathroom, hall, sewer, custom;

int main() {
  YSE::System().init();

  // load handclap sound, non-looping
  snare.create("snare.ogg", NULL, true);

  if (!snare.isValid()) {
    std::cout << "sound 'snare.ogg' not found" << std::endl;
    std::cin.get();
    goto exit;
  }

  // set global reverb
  YSE::System().getGlobalReverb().setActive(true);
  YSE::System().getGlobalReverb().setPreset(YSE::REVERB_GENERIC);
  YSE::ChannelMaster().attachReverb();

  // 'world' reverbs can be added at specific positions
  // size is the maximum distance from the reverb at which it its influence is at maximum level
  // rolloff indicates how far outside its size it will drop to zero influence (linear curve)

  // add reverb at 5 meter
  bathroom.create();
  bathroom.setPosition(YSE::Vec(0, 0, 5)).setSize(1).setRollOff(1);
  bathroom.setPreset(YSE::REVERB_BATHROOM);

  // add reverb at 10 meter
  hall.create();
  hall.setPosition(YSE::Vec(0, 0, 10)).setSize(1).setRollOff(1);
  hall.setPreset(YSE::REVERB_HALL);

  // add reverb at 15 meter
  sewer.create();
  sewer.setPosition(YSE::Vec(0, 0, 15)).setSize(1).setRollOff(1);
  sewer.setPreset(YSE::REVERB_SEWERPIPE);

  // add reverb at 20 meter
  custom.create();
  custom.setPosition(YSE::Vec(0, 0, 20)).setSize(1).setRollOff(1);
  // for this reverb we use custom parameters instead of a preset
  // (these are meant to sound awkward)
  custom.setRoomSize(1.0).setDamping(0.1).setDryWetBalance(0.0, 1.0).setModulation(6.5, 0.7);
  custom.setReflection(0, 1000, 0.5).setReflection(1, 1500, 0.6);
  custom.setReflection(2, 2100, 0.8).setReflection(3, 2999, 0.9);

  std::cout << "This example as one global reverb. On top of that, there are several localized reverbs which will alter the listener's experience when moving around." << std::endl;
  std::cout << "Use q/a to move sound and listener forward / back." << std::endl;
  std::cout << "Use w/s to toggle global reverb on / off." << std::endl;
  std::cout << "...or e to exit." << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
      case 'q': {
                  YSE::Vec pos = YSE::Listener().getPosition();
                  pos.z += 0.1;
                  YSE::Listener().setPosition(pos);
                  snare.setPosition(pos);
                  break;
      }
      case 'a':{
                 YSE::Vec pos = YSE::Listener().getPosition();
                 pos.z -= 0.1;
                 YSE::Listener().setPosition(pos);
                 snare.setPosition(pos);
                 break;
      }
      case 'w': YSE::System().getGlobalReverb().setActive(true); break;
      case 's': YSE::System().getGlobalReverb().setActive(false); break;

      case 'e': goto exit;
      }
    }

    if (snare.isStopped()) {
      snare.play();
    }

    YSE::System().sleep(100);
    YSE::System().update();
#ifdef WINDOWS
    _cprintf_s("Position (z): %.2f \r", YSE::Listener().getPosition().z);
#endif
  }

exit:
  YSE::System().close();
  return 0;
}