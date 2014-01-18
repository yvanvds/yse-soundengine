#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::sound sound;

int main() {
  YSE::System.init();
  sound.create("drone.ogg", NULL, true);

  for (UInt i = 0; i < YSE::System.numDevices(); i++) {
    YSE::audioDevice & dev = YSE::System.getDevice(i);
    if (dev.outputs()) {
      std::cout << i << ": " << dev.name() 
                << " on host: " << dev.host() 
                << " has " << dev.outputs() 
                << " Outputs." << std::endl;
    }
  }

  sound.play();
  YSE::System.update();

  while (true) {
    Int value;
    std::cout << "choose a device or type -1 to exit: ";
    std::cin >> value; // yes I know, no error checks in this example...

    if (value >= 0 && value < YSE::System.numDevices()) {
      YSE::System.setDevice(value);
    } else goto exit;
  }    

exit:
  YSE::System.close();
  return 0;
}
