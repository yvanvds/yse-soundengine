#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

YSE::channel customChannel;
YSE::sound kick, pulse;

int main() {
  YSE::System.init();

  // create a custom channel and add it to the main channel
  customChannel.create();

  // add a sound to your custom channel
  kick.create("kick.ogg", &customChannel, true);

  // add a sound to the music channel
  pulse.create("pulse1.ogg", &YSE::ChannelMusic, true);

  std::cout << "Use q/a to change the globalChannel volume up and down." << std::endl;
  std::cout << "Use w/s to change the customChannel volume up and down." << std::endl;
  std::cout << "Use e/d to change the musicChannel volume up and down."  << std::endl;
  std::cout << "Use r to delete the customChannel."                      << std::endl;
  std::cout << "...or 0 to exit."                                        << std::endl;

  kick.play();
  pulse.play();

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch(ch) {
        case 'q': YSE::ChannelGlobal.volume(YSE::ChannelGlobal.volume() + 0.1); break;
        case 'a': YSE::ChannelGlobal.volume(YSE::ChannelGlobal.volume() - 0.1); break;

        // you can delete this channel while running, so check the pointer before accessing it
        case 'w': if (customChannel.valid()) customChannel.volume(customChannel.volume() + 0.1); break;
        case 's': if (customChannel.valid()) customChannel.volume(customChannel.volume() - 0.1); break;
        case 'e': YSE::ChannelMusic.volume(YSE::ChannelMusic.volume() + 0.1); break;
        case 'd': YSE::ChannelMusic.volume(YSE::ChannelMusic.volume() - 0.1); break;
        case 'r': {
          customChannel.release();
          std::cout << "The custom channel is deleted. All sounds and subchannels are automatically moved to the parent channel." << std::endl;
          break;
        }

        case '0': goto exit;
      }
    }

    YSE::System.sleep (100);
    YSE::System.update(   );
  }    

exit:
  YSE::System.close();
  return 0;
}
