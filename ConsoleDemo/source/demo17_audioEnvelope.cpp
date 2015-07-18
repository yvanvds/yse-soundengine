#include <iostream>
#include <cstdlib>
#include <cstdio>
#include "yse.hpp"
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif


YSE::audioBuffer buffer;
YSE::DSP::envelope envelope;

int main() {
  YSE::System().init();

  // setting the last parameter to true will enable streaming
  if (!buffer.create("snare.ogg")) {
    std::cout << "sound 'snare.ogg' not found" << std::endl;
    std::cin.get();
    goto exit;
  }

  
  envelope.create(buffer.getChannel(0), 30);
  envelope.toFile("snare2.env");

  FILE * gnuPlot = _popen("gnuplot -persistent", "w");
  fprintf(gnuPlot, "%s \n", "set title \"SNARE\"");
  fprintf(gnuPlot, "%s \n", "plot 'snare.env' with lines");
  std::cin.get();


exit:
  YSE::System().close();
  return 0;
}