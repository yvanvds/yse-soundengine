#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::sound contact, drone, note;
YSE::sound * currentSound = NULL;
YSE::DSP::MODULES::sweepFilter contactSweep(YSE::DSP::MODULES::sweepFilter::TRIANGLE);
YSE::DSP::MODULES::sweepFilter droneSweep(YSE::DSP::MODULES::sweepFilter::SAW);
YSE::DSP::MODULES::sweepFilter noteSweep(YSE::DSP::MODULES::sweepFilter::TRIANGLE);
YSE::DSP::MODULES::sweepFilter * filter;

int main() {
  YSE::System().init();

  // load sounds
  contact.create("contact.ogg", NULL, true);
  drone.create("drone.ogg", NULL, true);
  note.create("c.wav", NULL, true);

  if (!contact.isValid()) {
    std::cout << "sound 'contact.ogg' not found" << std::endl;
    std::cin.get();
    goto exit;
  }

  if (!drone.isValid()) {
    std::cout << "sound 'drone.ogg' not found" << std::endl;
    std::cin.get();
    goto exit;
  }

  if (!note.isValid()) {
    std::cout << "sound 'c.wav' not found" << std::endl;
    std::cin.get();
    goto exit;
  }

  contact.setDSP(&contactSweep);
  drone.setDSP(&droneSweep);
  note.setDSP(&noteSweep);

  droneSweep.speed(-7);
  droneSweep.depth(49);
  droneSweep.frequency(19);

  contactSweep.speed(4);
  contactSweep.depth(55);
  contactSweep.frequency(90);

  noteSweep.speed(-11);
  noteSweep.depth(51);
  noteSweep.frequency(60);



  std::cout << "This example provides 3 sounds and a sweep filter." << std::endl;
  std::cout << "Use 1, 2, and 3 to select the active sound." << std::endl;
  std::cout << "Use s/x to increase/decrease sweep speed" << std::endl;
  std::cout << "Use d/c to increase/decrease sweep depth" << std::endl;
  std::cout << "Use f/v to increase/decrease sweep frequency" << std::endl;
  std::cout << "...or e to exit." << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
      case '1': {
                  if (currentSound) currentSound->pause();
                  currentSound = &contact;
                  filter = &contactSweep;
                  currentSound->play();
                  break;
      }
      case '2': {
                  if (currentSound) currentSound->pause();
                  currentSound = &drone;
                  filter = &droneSweep;
                  currentSound->play();
                  break;
      }
      case '3': {
                  if (currentSound) currentSound->pause();
                  currentSound = &note;
                  filter = &noteSweep;
                  currentSound->play();
                  break;
      }
      
      case 's': {
                  if (currentSound) {
                    filter->speed(filter->speed() + 1);
                  }
                  break;
      }

      case 'x': {
                  if (currentSound) {
                    filter->speed(filter->speed() - 1);
                  }
                  break;
      }

      case 'd': {
                  if (currentSound) {
                    filter->depth(filter->depth() + 1);
                  }
                  break;
      }

      case 'c': {
                  if (currentSound) {
                    filter->depth(filter->depth() - 1);
                  }
                  break;
      }

      case 'f': {
                  if (currentSound) {
                    filter->frequency(filter->frequency() + 1);
                  }
                  break;
      }

      case 'v': {
                  if (currentSound) {
                    filter->frequency(filter->frequency() - 1);
                  }
                  break;
      }

      case 'e': goto exit;
      }
    }

    YSE::System().sleep(100);
    YSE::System().update();

#ifdef YSE_WINDOWS
    if (currentSound) {
      _cprintf_s("Speed: %.2f, Depth: %d, Frequency: %d \r", filter->speed(), filter->depth(), filter->frequency());
    }
    
#endif
  }

exit:
  YSE::System().close();
  return 0;
}