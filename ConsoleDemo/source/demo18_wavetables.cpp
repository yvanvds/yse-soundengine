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

  std::vector<Flt> amplitudes;
  amplitudes.emplace_back(1.0);
  amplitudes.emplace_back(0.2);
  amplitudes.emplace_back(0.1);
  amplitudes.emplace_back(0.05);

  wavetable.createFourierTable(amplitudes, 500, 0.f);
  YSE::DSP::SaveToFile("wavetable", wavetable);
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