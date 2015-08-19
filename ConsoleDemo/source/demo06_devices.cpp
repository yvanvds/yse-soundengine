#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::sound sound;
float appTime = 0;

void showDevices() {
  std::cout << "This example just lists the available devices on your system. If more than one is availble, you can switch to another device." << std::endl;
  const std::vector<YSE::device> & list = YSE::System().getDevices();
  for (UInt i = 0; i < list.size(); i++) {
    if (list[i].getOutputChannelNames().size() > 0) {
      std::cout << i << ": " << list[i].getName()
        << " on host: " << list[i].getTypeName()
        << " has " << list[i].getOutputChannelNames().size()
        << " Outputs." << std::endl;
    }
  }
}

int main() {
  YSE::System().init();
  sound.create("drone.ogg", NULL, true);

  showDevices();
  std::cout << "choose a device or type e to exit: ";

  sound.play();
  YSE::System().update();

  while (true) {
    if (_kbhit()) {
      int choice = -1;
      char ch = _getch();
      // this code is not very good because we don't know how many 
      // devices there are, but it will do for an example
      switch (ch) {
      case '0': choice = 0; break;
      case '1': choice = 1; break;
      case '2': choice = 2; break;
      case '3': choice = 3; break;
      case '4': choice = 4; break;
      case '5': choice = 5; break;
      case '6': choice = 6; break;
      case '7': choice = 7; break;
      case '8': choice = 8; break;
      case '9': choice = 9; break;
      case 'e': goto exit;
      }
      if (choice >= 0) {
        const std::vector<YSE::device> & list = YSE::System().getDevices();
        if (choice < list.size()) {
          YSE::deviceSetup setup;
          setup.setOutput(list[choice]);
          YSE::System().closeCurrentDevice();
          YSE::System().openDevice(setup);
        }
      }
    }

    appTime += 0.1;
    YSE::Vec pos;
    pos.set(sin(appTime) * 5, 0, cos(appTime) * 5);
    sound.setPosition(pos);

    YSE::System().update();
    YSE::System().sleep(100);
    
    
  }

exit:
  YSE::System().close();
  return 0;
}
