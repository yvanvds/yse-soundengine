#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

Flt occludeValue = 0;
YSE::sound sound;

Flt occlusionFunction(const YSE::Vec& pos, const YSE::Vec& listener) {
  // for simplicity's sake, we just check with a global var here.
  // In reality you should do a raycast here to check these positions with
  // the physx or bullet implementation in your game.
  return occludeValue;
}



int main() {
  if (!YSE::System().init()) goto exit;
  YSE::System().occlusionCallback(occlusionFunction);

  sound.create("contact.ogg", NULL, true).setOcclusion(true);

  std::cout << "This is a basic implementation of the sound occlusion callback." << std::endl;
  std::cout << "Use q/a to increase / decrease sound occlusion." << std::endl;
  std::cout << "...or e to exit." << std::endl;

  sound.play();

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
      case 'q': occludeValue += 0.1; break;
      case 'a': occludeValue -= 0.1; break;
      case 'e': goto exit;
      }
    }

    YSE::System().sleep(100);
    YSE::System().update();
  }

exit:
  YSE::System().close();
  return 0;
}