#include <iostream>
#include <cstdlib>
#include <ctime>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

/* 3D:

  This demo is about 3D positioning. It's a bit clumsy to do in a 
  little console demo. Most important is the member function setPosition,
  and the helper object YSE::Vec which holds xyz coordinates.

  */

int selectedObject = 1;

// create some sounds
YSE::sound sound1;
YSE::sound sound2;

enum direction {
  FORWARD,
  BACKWARD,
  LEFT,
  RIGHT,
};

void moveObject(direction d);
void reset();


int main() {
  // initialize audio system
  YSE::System().init();

  // load a sound in memory and get a pointer to it
  sound1.create("drone.ogg", NULL, true).play();
  sound2.create("kick.ogg", NULL, true).play();
  reset();

  std::cout << "This demonstrates how sounds and the listener can be moved in 3D." << std::endl;
  std::cout << "Initial positions (xyz) are:" << std::endl;
  std::cout << "Listener:  0 / 0 / 0" << std::endl;
  std::cout << "Sound 1 : -5 / 0 / 5" << std::endl;
  std::cout << "Sound 2 :  5 / 0 / 5" << std::endl;
  std::cout << std::endl;
  std::cout << "Press 1 to select sound 1, 2 for sound 2 and 3 for listener." << std::endl;
  std::cout << "Use adws to move selected object (left/right/forward/backward)." << std::endl;
  std::cout << "Press 'r' to reset all objects to the initial positions." << std::endl;
  std::cout << "Press 'e' to exit this program." << std::endl;

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
      case '1': selectedObject = 1; break;
      case '2': selectedObject = 2; break;
      case '3': selectedObject = 3; break;
      case 'a': moveObject(LEFT); break;
      case 'd': moveObject(RIGHT); break;
      case 'w': moveObject(FORWARD); break;
      case 's': moveObject(BACKWARD); break;
      case 'r': reset(); break;
      case 'e': goto exit;
      }
    }

    YSE::System().sleep(100);
    //YSE::Vec pos = sound1.getPosition();
    //pos.x = sin(std::clock() / static_cast<Flt>(CLOCKS_PER_SEC)) * 10;
    //sound1.setPosition(pos);
    YSE::System().update();
  }

exit:
  YSE::System().close();
  return 0;
}

void moveObject(direction d) {
  if (selectedObject < 3) {
    YSE::sound * s;
    if (selectedObject == 1) s = &sound1;
    else s = &sound2;
    YSE::Vec pos = s->getPosition();
    switch (d) {
    case FORWARD: pos.z += 0.5f; s->setPosition(pos); break;
    case BACKWARD: pos.z -= 0.5f; s->setPosition(pos); break;
    case LEFT: pos.x -= 0.5f; s->setPosition(pos); break;
    case RIGHT: pos.x += 0.5f; s->setPosition(pos); break;
    }
  }
  else {
    // you do not have to create the listener object, it's already there
    YSE::Vec pos = YSE::Listener().getPosition();
    switch (d) {
    case FORWARD: pos.z += 0.5f; YSE::Listener().setPosition(pos); break;
    case BACKWARD: pos.z -= 0.5f; YSE::Listener().setPosition(pos); break;
    case LEFT: pos.x -= 0.5f; YSE::Listener().setPosition(pos); break;
    case RIGHT: pos.x += 0.5f; YSE::Listener().setPosition(pos); break;
    }
  }
}

void reset() {
  // YSE has a very flexible vector class built in
  YSE::Vec pos;
  pos.zero();   YSE::Listener().setPosition(pos);
  pos.set(-5, 0, 5);  sound1.setPosition(pos);
  pos.set(5, 0, 5);  sound2.setPosition(pos);
}