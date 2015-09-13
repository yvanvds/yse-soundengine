#include <iostream>
#include <cstdlib>
#include <cstdio>
#include "yse.hpp"

#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

/** Wavetables:

    Wavetables are the foundation for FM synths. In this simple example
    a wavetable is Triangular wave with 8 harmonics and a length of 1024 samples
    is created. In reality, you will build a synth with this, but to
    keep this example as short as possible it will be played by a sound object.

*/

YSE::DSP::wavetable wavetable;
YSE::sound sound;

int main() {
  YSE::System().init();

  // create a Triangle wavetable with 8 harmonics, 1024 samples long
  wavetable.createTriangle(2, 1024);

  // the sound will play this buffer (looping)
  sound.create(wavetable, nullptr, true).play();

  std::cout << "In this demo a small wavetable is created and played as a loop." << std::endl;
  std::cout << "(Press e to exit.)" << std::endl;

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