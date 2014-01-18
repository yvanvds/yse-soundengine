#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::sound drone;
YSE::reverb custom;

int main() {
  YSE::System.init();

  // load handclap sound, non-looping
  drone.create("drone.ogg", NULL, true);

  if (!drone.valid()) {
    std::cout << "sound 'drone.ogg' not found" << std::endl;
    std::cin.get();
    goto exit;
  }

  // set global reverb


  custom.create();
  custom.roomsize(1.0);
  custom.damp(0.4);
  custom.wet(5.0);
  custom.dry(5.0);
  custom.modFreq(1);
  custom.modWidth(0.0);
	custom.reflectionTime(0, 200).reflectionTime(1, 400).reflectionTime(2,600).reflectionTime(3,800);
	custom.reflectionGain(0,0.5).reflectionGain(1,0.3).reflectionGain(2,0.2).reflectionGain(3,0.1);
  custom.pos(YSE::Vec(0)).size(10).rolloff(2);
  custom.active(true);

  std::cout << "Use q/a to set roomsize, w/s to set damping."  << std::endl;
  std::cout << "Use e/d to set wet sound, r/f to set dry sound."<< std::endl;
  std::cout << "Use t/g to set modulation frequency, y/h to set modulation width." << std::endl;
  std::cout << "Use 1/2 to set r1st delay time, 3/4 to set r1st delay gain." << std::endl;
  std::cout << "Use 5/6 to set r2nd delay time, 7/8 to set r2nd delay gain." << std::endl;
  std::cout << "Use u/i to set r3rd delay time, o/p to set r3rd delay gain." << std::endl;
  std::cout << "Use j/k to set r4th delay time, l/; to set r4th delay gain." << std::endl;

  std::cout << "...or 0 to exit."                                   << std::endl;
  std::cout << "room|damp|wet|dry|freq|width|dt1|dg1|dt2|dg2|dt3|dg3|dt4|dg4" << std::endl;
  drone.pos(YSE::Vec(0));
  drone.play();

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch(ch) {
        case 'q': custom.roomsize(custom.roomsize() + 0.1); break;
        case 'a': custom.roomsize(custom.roomsize() - 0.1); break;
        case 'w': custom.damp(custom.damp() + 0.1); break;
        case 's': custom.damp(custom.damp() - 0.1); break;
        case 'e': custom.wet(custom.wet() + 0.1); break;
        case 'd': custom.wet(custom.wet() - 0.1); break;
        case 'r': custom.dry(custom.dry() + 0.1); break;
        case 'f': custom.dry(custom.dry() - 0.1); break;
        case 't': custom.modFreq(custom.modFreq() + 0.1); break;
        case 'g': custom.modFreq(custom.modFreq() - 0.1); break;
        case 'y': custom.modWidth(custom.modWidth() + 0.1); break;
        case 'h': custom.modWidth(custom.modWidth() - 0.1); break;
        case '1': custom.reflectionTime(0, custom.reflectionTime(0) + 1); break;
        case '2': custom.reflectionTime(0, custom.reflectionTime(0) - 1); break;
        case '3': custom.reflectionGain(0, custom.reflectionGain(0) + 0.1); break;
        case '4': custom.reflectionGain(0, custom.reflectionGain(0) - 0.1); break;
        case '5': custom.reflectionTime(1, custom.reflectionTime(1) + 1); break;
        case '6': custom.reflectionTime(1, custom.reflectionTime(1) - 1); break;
        case '7': custom.reflectionGain(1, custom.reflectionGain(1) + 0.1); break;
        case '8': custom.reflectionGain(1, custom.reflectionGain(1) - 0.1); break;
        case 'u': custom.reflectionTime(2, custom.reflectionTime(2) + 1); break;
        case 'i': custom.reflectionTime(2, custom.reflectionTime(2) - 1); break;
        case 'o': custom.reflectionGain(2, custom.reflectionGain(2) + 0.1); break;
        case 'p': custom.reflectionGain(2, custom.reflectionGain(2) - 0.1); break;
        case 'j': custom.reflectionTime(3, custom.reflectionTime(3) + 1); break;
        case 'k': custom.reflectionTime(3, custom.reflectionTime(3) - 1); break;
        case 'l': custom.reflectionGain(3, custom.reflectionGain(3) + 0.1); break;
        case ';': custom.reflectionGain(3, custom.reflectionGain(3) - 0.1); break;        
        case '0': goto exit;
      }
    }

    YSE::System.sleep (100);
    YSE::System.update(   );
    
// I should figure out how to do a cprintf_s on mac, but it's not that urgent, right?
#ifdef WINDOWS
    _cprintf_s("%4.1f|%4.1f|%3.1f|%3.1f|%4.1f|%5.1f|%i|%3.1f|%3i|%3.1f|%3i|%3.1f|%3i|%3.1f\r", custom.roomsize(), custom.damp(), custom.wet(), custom.dry(), 
      custom.modFreq(), custom.modWidth(), custom.reflectionTime(0), custom.reflectionGain(0), custom.reflectionTime(1), custom.reflectionGain(1), 
      custom.reflectionTime(2), custom.reflectionGain(2), custom.reflectionTime(3), custom.reflectionGain(3));
#endif
  }

exit:
  YSE::System.close();
  return 0;
}
