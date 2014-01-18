#include <iostream>
#include <cstdlib>
#include <conio.h>
#include <time.h>

/* This should not be here because it's not ready yet. But it is... */

#include "yse.hpp"
using namespace YSE;


class myTrack : public MUSIC::track {
public:
  INSTRUMENTS::sampler piano;
  myTrack(Int interval);
  void update();

  MUSIC::note note;

  // tracks run in a separate thread, so if you want to change
  // variables from within the main thread, you should use the
  // atomic version (i.e. aBool instead of Bool)
  aBool play;
};

myTrack::myTrack(Int interval) : track(interval) { // don't forget to pass the interval to the parent constructor, must be done in initialization list
  piano.create("g.ogg", 4, NOTE::G4, ChannelGlobal);
  addInstrument(piano);
  play = true;
  note(NOTE::C4, 0.7, 300);
}

void myTrack::update() {
  if (!play) return;

  piano.play(note);  
  note++;
  if (note > NOTE::G4) note(NOTE::C4, 0.7, 300);
  
}

class myTrack2 : public MUSIC::track {
public:
  INSTRUMENTS::sineSynth sine;
  myTrack2(Int interval);
  void update();

  MUSIC::note note;

  // tracks run in a separate thread, so if you want to change
  // variables from within the main thread, you should use the
  // atomic version (i.e. aBool instead of Bool)
  aBool play;
};

myTrack2::myTrack2(Int interval) : track(interval) { // don't forget to pass the interval to the parent constructor, must be done in initialization list
  sine.create(4);
  addInstrument(sine);
  play = true;
  note(NOTE::C4, 0.7, 300);
}

void myTrack2::update() {
  if (!play) return;

  sine.play(note);  
  note++;
  if (note > NOTE::G4) note(NOTE::C4, 0.7, 300);
  
}


int main() {
  System.init();
  myTrack track(200);
  myTrack2 track2(400);
  //track.start();
  track2.start();
  std::cout << "press a to start, s to stop the track" << std::endl;
  std::cout << "press e to exit" << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch(ch) {
        case 'a': track.play = true ; break;
        case 's': track.play = false; break;
        case 'e': goto exit;
      }
    }

    System.sleep (100);
    System.update(   );
  }    

exit:

  System.close();
  return 0;
}
