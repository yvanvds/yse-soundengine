#include <iostream>
#include <cstdlib>
#include <cstdio>
#include "yse.hpp"

#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif


AUDIOBUFFER piano;
AUDIOBUFFER drone;
AUDIOBUFFER droneOrig;

YSE::DSP::envelope snareEnvelope;
YSE::DSP::envelope pianoEnvelope;

YSE::sound sound;

FILE * gnuPlot = nullptr;

int main() {
  YSE::System().init();

  if (!YSE::DSP::LoadFromFile("g.ogg", piano)) {
    std::cout << "sound 'g.ogg' not found" << std::endl;
    std::cin.get();
    goto exit;
  }

  if (!YSE::DSP::LoadFromFile("drone.ogg", drone)) {
    std::cout << "sound 'drone.ogg' not found" << std::endl;
    std::cin.get();
    goto exit;
  }

  // drone sound is a bit short, so we'll copy it
  drone.copyFrom(drone, 0, drone.getLength(), drone.getLength());
  droneOrig = drone;

  snareEnvelope.create("snare.env"); // create from envelope file
  pianoEnvelope.create(piano); // create from audio buffer

  sound.create(drone, nullptr, true);

  std::cout << "Sounds are loaded. Please choose: " << std::endl;
  std::cout << "1 to play" << std::endl;
  std::cout << "2 to stop" << std::endl;
  std::cout << "3 to apply snare envelope" << std::endl;
  std::cout << "4 to apply piano envelope" << std::endl;
  std::cout << "5 to reset sound" << std::endl;
  std::cout << "6 to save snare and piano envelopes to file" << std::endl;
  std::cout << "7 view snare envelope (only with gnuplot installed, seems to display on exit)" << std::endl;
  std::cout << "8 normalize piano envelope and apply" << std::endl;
  std::cout << "...or e to exit." << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
      case '1': sound.play(); break;
      case '2': sound.stop(); break;
      case '3': drone = droneOrig;
                drone.applyEnvelope(snareEnvelope);
                break;
    
      case '4': drone = droneOrig;
                drone.applyEnvelope(pianoEnvelope);
                break;
     
      case '5': drone = droneOrig;
                break;
      
      case '6': snareEnvelope.saveToFile("snare.env");
                pianoEnvelope.saveToFile("piano.env");
                break;

      case '7': if(!gnuPlot) gnuPlot = _popen("gnuplot -persist", "w");
                fprintf(gnuPlot, "%s \n", "set title \"SNARE\"");
                fprintf(gnuPlot, "%s \n", "plot 'snare.env' with lines");

                break;

      case '8': pianoEnvelope.normalize();
                drone = droneOrig;
                drone.applyEnvelope(pianoEnvelope);
                break;

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