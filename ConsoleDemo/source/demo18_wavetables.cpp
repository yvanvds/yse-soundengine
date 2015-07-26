#include <iostream>
#include <cstdlib>
#include <cstdio>
#include "yse.hpp"

#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::DSP::wavetable wavetable;
YSE::sound sound;

int main() {
  YSE::System().init();

  
  wavetable.createTriangle(8, 1024);
  //YSE::DSP::SaveToFile("triangle", wavetable);

  sound.create(wavetable, nullptr, true).play();

  std::cout << "...or e to exit." << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
      

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