#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

int selectedObject = 1;

// create some sounds
YSE::sound sound1;
YSE::sound sound2;

enum direction {
  FORWARD  ,
  BACKWARD ,
  LEFT     ,
  RIGHT    ,
};

void moveObject(direction d);
void reset();


int main() {
  // initialize audio system
  YSE::System.init();

  // load a sound in memory and get a pointer to it
  sound1.create("drone.ogg", NULL, true).play();
  sound2.create("kick.ogg" , NULL, true).play();
  reset();

  std::cout << "Initial positions (xyz) are:"                                             << std::endl;
  std::cout << "Listener:  0 / 0 / 0"                                                     << std::endl;
  std::cout << "Sound 1 : -5 / 0 / 5"                                                     << std::endl;
  std::cout << "Sound 2 :  5 / 0 / 5"                                                     << std::endl;
  std::cout << std::endl;
  std::cout << "Press 1 to select sound 1, 2 for sound 2 and 3 for listener."             << std::endl;
  std::cout << "Use adws to move selected object (left/right/forward/backward)."          << std::endl;
  std::cout << "Press 'r' to reset all objects to the initial positions."                 << std::endl;
  std::cout << "Press 'e' to exit this program."                                          << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch(ch) {
        case '1': selectedObject = 1; break;
        case '2': selectedObject = 2; break;
        case '3': selectedObject = 3; break;
        case 'a': moveObject(LEFT     ); break;
        case 'd': moveObject(RIGHT    ); break;
        case 'w': moveObject(FORWARD  ); break;
        case 's': moveObject(BACKWARD ); break;
        case 'r': reset(); break;
        case 'e': goto exit;
      }
    }

    YSE::System.sleep (100);
    YSE::System.update(   );
  }    

exit:
  YSE::System.close();
  return 0;
}

void moveObject(direction d) {
  if (selectedObject < 3) {
    YSE::sound * s;
    if (selectedObject == 1) s = &sound1;
    else s = &sound2;
    YSE::Vec pos = s->pos();
    switch(d) {
      case FORWARD  : pos.z += 0.5f; s->pos(pos); break;
      case BACKWARD : pos.z -= 0.5f; s->pos(pos); break;
      case LEFT     : pos.x -= 0.5f; s->pos(pos); break;
      case RIGHT    : pos.x += 0.5f; s->pos(pos); break;
    }
  } else {
    // you do not have to create the listener object, it's already there
    YSE::Vec pos = YSE::Listener.pos();
    switch(d) {
      case FORWARD  : pos.z += 0.5f; YSE::Listener.pos(pos); break;
      case BACKWARD : pos.z -= 0.5f; YSE::Listener.pos(pos); break;
      case LEFT     : pos.x -= 0.5f; YSE::Listener.pos(pos); break;
      case RIGHT    : pos.x += 0.5f; YSE::Listener.pos(pos); break;
    }
  }
}

void reset() {
  // YSE has a very flexible vector class built in
  YSE::Vec pos;
  pos.zero();   YSE::Listener.pos(pos);
  pos.set(-5, 0, 5);  sound1.pos(pos);
  pos.set( 5, 0, 5);  sound2.pos(pos);
}