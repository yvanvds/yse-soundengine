#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::sound snare;
YSE::reverb bathroom, hall, sewer, custom;

int main() {
  YSE::System.init();

  // load handclap sound, non-looping
  snare.create("snare.ogg", NULL);

  if (!snare.valid()) {
    std::cout << "sound 'snare.ogg' not found" << std::endl;
    std::cin.get();
    goto exit;
  }

  // set global reverb
  YSE::GlobalReverb.active(true);
  YSE::GlobalReverb.preset(YSE::REVERB_GENERIC);

  // 'world' reverbs can be added at specific positions
  // size is the maximum distance from the reverb at which it its influence is at maximum level
  // rolloff indicates how far outside its size it will drop to zero influence (linear curve)
  
  // add reverb at 5 meter
  bathroom.create();
  bathroom.pos(YSE::Vec(0,0,5)).size(1).rolloff(1);
  bathroom.preset(YSE::REVERB_BATHROOM);

  // add reverb at 10 meter
  hall.create();
  hall.pos(YSE::Vec(0,0,10)).size(1).rolloff(1);
  hall.preset(YSE::REVERB_HALL);

  // add reverb at 15 meter
  sewer.create();
  sewer.pos(YSE::Vec(0,0,15)).size(1).rolloff(1);
  sewer.preset(YSE::REVERB_SEWERPIPE);

  // add reverb at 20 meter
  custom.create();
  custom.pos(YSE::Vec(0,0,20)).size(1).rolloff(1);
  // for this reverb we use custom parameters instead of a preset
  // (these are meant to sound awkward)
  custom.roomsize(1.0).damp(0.1).wet(1.0).dry(0.0).modFreq(6.5).modWidth(0.7);
	custom.reflectionTime(0, 1000).reflectionTime(1, 1500).reflectionTime(2,2100).reflectionTime(3,2999);
	custom.reflectionGain(0,0.5).reflectionGain(1,0.6).reflectionGain(2,0.8).reflectionGain(3,0.9);

  std::cout << "Use q/a to move sound and listener forward / back." << std::endl;
  std::cout << "Use w/s to toggle global reverb on / off."          << std::endl;
  std::cout << "...or e to exit."                                   << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch(ch) {
        case 'q': {
          YSE::Vec pos = YSE::Listener.pos();
          pos.z += 0.1;
          YSE::Listener.pos(pos);
          snare.pos(pos);
          break;
        }
        case 'a':{
          YSE::Vec pos = YSE::Listener.pos();
          pos.z -= 0.1;
          YSE::Listener.pos(pos);
          snare.pos(pos);
          break;
        }
        case 'w': YSE::GlobalReverb.active(true ); break;
        case 's': YSE::GlobalReverb.active(false); break;

        case 'e': goto exit;
      }
    }

    if (snare.stopped()) {
      snare.play();
    }

    YSE::System.sleep (100);
    YSE::System.update(   );
#ifdef WINDOWS
    _cprintf_s("Position (z): %.2f \r", YSE::Listener.pos().z);
#endif
  }

exit:
  YSE::System.close();
  return 0;
}
