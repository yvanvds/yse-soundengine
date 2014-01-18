#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::sound sound;

// write your own error callback function
void errorCallback(const char * message) {
  std::cout << message << std::endl;
}


int main() {
  YSE::System.init();
  // set error callback function and change reporting level to include warnings
  YSE::Error.setCallback(errorCallback).level(YSE::EL_WARNINGS);

  // load a file that doesn't exist
  sound.create("bogus.ogg", NULL, true);
  std::cout << "press enter to close this program." << std::endl;

  std::cin.get();
  YSE::System.close();
  return 0;
}
