#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::sound sound;
YSE::DSP::MODULES::granulator gran(500000, 16);
Flt transpose = 0;
Flt transposeRandom = 0;

int main() {
  YSE::System().init();

  // load sounds
  sound.create("drone.ogg", NULL, true);
  sound.set2D(true);

  gran.grainFrequency(20).grainLength(20000).gain(0.1).grainTranspose(transpose, transposeRandom);
  
  sound.setDSP(&gran);
  sound.play();

  std::cout << "This example granulates a sound." << std::endl;
  std::cout << "Use s/x to increase/decrease grain frequency" << std::endl;
  std::cout << "Use d/c to increase/decrease grain length" << std::endl;
  std::cout << "Use f/v to increase/decrease pitch" << std::endl;
  std::cout << "Use g/b to increase/decrease pitch random part" << std::endl;
  std::cout << "...or e to exit." << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
      case 's': {
                  gran.grainFrequency(gran.grainFrequency() + 1);
                  break;
      }
      case 'x': {
                  if (gran.grainFrequency() > 1)
                    gran.grainFrequency(gran.grainFrequency() - 1);
                  break;
      }
      case 'd': {
                  gran.grainLength(gran.grainLength() + 100);
                  break;
      }
      case 'c': {
                  if (gran.grainLength() > 400)
                    gran.grainLength(gran.grainLength() - 100);
                  break;
      }
      case 'f': {
                  if (transpose < 2.0) transpose += 0.1;
                  gran.grainTranspose(transpose, transposeRandom);
                  break;
      }
      case 'v': {
                  if (transpose > -0.9) transpose -= 0.1;
                  gran.grainTranspose(transpose, transposeRandom);
                  break;
      }
      case 'g': {
                  if (transposeRandom < 1) transposeRandom += 0.1;
                  gran.grainTranspose(transpose, transposeRandom);
                  break;
      }
      case 'b': {
                  if (transposeRandom > 0) transposeRandom -= 0.1;
                  gran.grainTranspose(transpose, transposeRandom);
                  break;
      }


      case 'e': goto exit;
      }
    }

    YSE::System().sleep(100);
    YSE::System().update();

#ifdef YSE_WINDOWS

    _cprintf_s("Frequency: %d, Length: %d, Transpose: %.1f, Random: %.1f \r", gran.grainFrequency(), gran.grainLength(), transpose, transposeRandom);
    

#endif
  }

exit:
  YSE::System().close();
  return 0;
}