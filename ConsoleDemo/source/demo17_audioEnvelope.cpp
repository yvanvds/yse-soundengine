#include <iostream>
#include <cstdlib>
#include <cstdio>
#include "yse.hpp"

#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#define _popen popen
#endif

/** Audio Envelope:

    You can retrieve the envelope from a sample or save/load envelope files.
    An envelope can be applied to another sound buffer.
*/


YSE::DSP::buffer piano;
YSE::DSP::drawableBuffer drone;
YSE::DSP::buffer droneOrig;
YSE::DSP::drawableBuffer sinModified;

YSE::DSP::envelope snareEnvelope;
YSE::DSP::envelope pianoEnvelope;

YSE::DSP::sine sine;

YSE::sound sineSound;
YSE::sound droneSound;

FILE * gnuPlot = nullptr;

int main() {
  YSE::System().init();

  // while DSP objects can be used as a constant sound generator,
  // you can also use them to create a static sample.
  YSE::DSP::buffer & sinOrig = sine(800, 44100); // 1 second audiobuffer
  sinModified = sinOrig;
  sineSound.create(sinModified, nullptr, true, 0.5);
  
  
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

  

  droneSound.create(drone, nullptr, true, 0.5);

  std::cout << "This example applies an envelope object to a sample. Please choose: " << std::endl;
  std::cout << "1 to toggle drone sound" << std::endl;
  std::cout << "2 to toggle sine sound" << std::endl;
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
      case '1': droneSound.toggle(); break;
      case '2': sineSound.toggle(); break;
      case '3': drone = droneOrig;
                drone.applyEnvelope(snareEnvelope);
                sinModified = sinOrig;
                sinModified.applyEnvelope(snareEnvelope);
                break;
    
      case '4': drone = droneOrig;
                drone.applyEnvelope(pianoEnvelope);
                sinModified = sinOrig;
                sinModified.applyEnvelope(pianoEnvelope);
                break;
     
      case '5': drone = droneOrig;
                sinModified = sinOrig;
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
                sinModified = sinOrig;
                sinModified.applyEnvelope(pianoEnvelope);
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
